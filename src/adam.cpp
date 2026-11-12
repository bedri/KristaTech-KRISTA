// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "llmq.h"
#include "txdb.h"
#include "utilstrencodings.h"
#include "primitives/block.h"
#include "hash.h"
#include "crypto/bls.h"
#include "masternodeman.h"
#include "util.h"
#include "sync.h"
#include "main.h"
#include "arith_uint256.h"
#include <algorithm>
#include <map>
#include <set>
#include "script/standard.h"
#include "script/mescal.h"
#include "keystore.h"
#include "spork.h"
#include "masternode-sync.h"

RecursiveMutex cs_adam_seeds;
std::map<uint256, uint256> mapAdamSeeds;
std::map<uint256, uint256> mapSeedToBlockHash;

RecursiveMutex cs_recent_vrf_proofs;
std::map<uint256, std::vector<unsigned char>> mapRecentVRFProofs;

RecursiveMutex cs_adam_solutions;
std::map<uint256, std::map<CPubKey, std::vector<unsigned char>>> mapAdamSolutionsCache;
std::map<uint256, std::vector<CAdamSolutionMsg>> mapOrphanAdamSolutions;

std::map<CPubKey, int> mapMasternodeLastActiveHeight;
std::map<CPubKey, std::set<int>> mapMasternodeActiveHeights;
std::map<CPubKey, COutPoint> mapMasternodeCollateralOutpoints;
std::map<COutPoint, COnChainMasternodeDelegation> mapOnChainCollateralDelegations;
static std::map<COutPoint, int> mapCollateralDelegationFirstHeight;
static std::map<COutPoint, std::vector<std::pair<int, COnChainMasternodeDelegation>>> mapCollateralDelegationHistory;
bool g_fMasternodePingStateInitialized = false;

static void RecordCollateralDelegationWithHistory(const COutPoint& collateralOutpoint, const COnChainMasternodeDelegation& newDel, int nHeight) {
    auto itOld = mapOnChainCollateralDelegations.find(collateralOutpoint);
    if (itOld != mapOnChainCollateralDelegations.end()) {
        if (itOld->second.pubKeyMasternode != newDel.pubKeyMasternode) {
            int prevFirstHeight = 1;
            auto itFirst = mapCollateralDelegationFirstHeight.find(collateralOutpoint);
            if (itFirst != mapCollateralDelegationFirstHeight.end()) {
                prevFirstHeight = itFirst->second;
            }
            auto& vecHist = mapCollateralDelegationHistory[collateralOutpoint];
            vecHist.push_back(std::make_pair(prevFirstHeight, itOld->second));
            if (vecHist.size() > 16) {
                vecHist.erase(vecHist.begin());
            }
            mapCollateralDelegationFirstHeight[collateralOutpoint] = nHeight;
        } else {
            auto itFirst = mapCollateralDelegationFirstHeight.find(collateralOutpoint);
            if (itFirst != mapCollateralDelegationFirstHeight.end()) {
                itFirst->second = std::min(itFirst->second, nHeight);
            } else {
                mapCollateralDelegationFirstHeight[collateralOutpoint] = nHeight;
            }
        }
    } else {
        mapCollateralDelegationFirstHeight[collateralOutpoint] = nHeight;
    }
    mapOnChainCollateralDelegations[collateralOutpoint] = newDel;
}

void RevertOnChainCollateralDelegation(const COutPoint& collateralOutpoint, const CPubKey& disconnectedPubKey, int nDisconnectedHeight, CPubKey* pRestoredOldKeyOut) {
    LOCK(cs_main);
    if (pRestoredOldKeyOut) {
        *pRestoredOldKeyOut = CPubKey();
    }
    auto itDel = mapOnChainCollateralDelegations.find(collateralOutpoint);
    if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode == disconnectedPubKey) {
        int firstHeight = nDisconnectedHeight;
        auto itFirst = mapCollateralDelegationFirstHeight.find(collateralOutpoint);
        if (itFirst != mapCollateralDelegationFirstHeight.end()) {
            firstHeight = itFirst->second;
        }
        if (firstHeight >= nDisconnectedHeight) {
            auto itHist = mapCollateralDelegationHistory.find(collateralOutpoint);
            if (itHist != mapCollateralDelegationHistory.end() && !itHist->second.empty()) {
                int restoredFirstHeight = itHist->second.back().first;
                itDel->second = itHist->second.back().second;
                if (pRestoredOldKeyOut) {
                    *pRestoredOldKeyOut = itDel->second.pubKeyMasternode;
                }
                itHist->second.pop_back();
                if (itHist->second.empty()) {
                    mapCollateralDelegationHistory.erase(itHist);
                }
                mapCollateralDelegationFirstHeight[collateralOutpoint] = restoredFirstHeight;
            } else {
                mapOnChainCollateralDelegations.erase(itDel);
                mapCollateralDelegationFirstHeight.erase(collateralOutpoint);
            }
        }
    }
}
static uint64_t g_nMasternodePingStateGen = 1;

void BumpMasternodePingStateGen() {
    ++g_nMasternodePingStateGen;
}

static bool VerifyOnChainDelegationSignature(const COnChainMasternodeDelegation& del, const COutPoint& collateralOutpoint) {
    if (!del.pubKeyCollateralAddress.IsValid() || !del.pubKeyMasternode.IsValid() || del.vchCollateralSig.empty()) {
        return false;
    }
    CMasternode mn;
    mn.vin = CTxIn(collateralOutpoint);
    mn.addr = del.addr;
    mn.pubKeyCollateralAddress = del.pubKeyCollateralAddress;
    mn.pubKeyMasternode = del.pubKeyMasternode;
    mn.sigTime = del.sigTime;
    mn.protocolVersion = del.protocolVersion;
    mn.nMessVersion = del.nMessVersion;
    mn.SetVchSig(del.vchCollateralSig);
    CMasternodeBroadcast mnb(mn);
    return mnb.CheckSignature();
}

bool ExtractOnChainMasternodePing(const CTransaction& tx, CPubKey& pubKeyMasternodeOut, COutPoint& collateralOutpointOut, bool fRecordDelegation, COnChainMasternodeDelegation* pDelegationOut, const CCoinsViewCache* pView, int nHeight, bool fStrictRelayFormat) {
    if (tx.IsCoinBase() || tx.vin.empty()) return false;

    if (fStrictRelayFormat) {
        if (tx.vin.size() > 2 || tx.vout.size() < 2 || tx.vout.size() > 3) {
            return false;
        }
        int nOpReturnCount = 0;
        for (const CTxOut& out : tx.vout) {
            if (!out.scriptPubKey.empty() && out.scriptPubKey[0] == OP_RETURN) {
                if (out.nValue != 0 || out.scriptPubKey.size() > 400) {
                    return false;
                }
                ++nOpReturnCount;
            }
        }
        if (nOpReturnCount != 1) {
            return false;
        }
    }

    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;
    CPubKey foundPubKey;
    bool fSignedInput = false;

    // 1. Check inputs for Taproot script-path witness leaf (<33/65-byte pubkey> OP_CHECKSIGVERIFY OP_1)
    for (const CTxIn& txin : tx.vin) {
        if (!txin.scriptSig.IsPushOnly()) {
            continue;
        }
        std::vector<std::vector<unsigned char>> stack;
        CScript::const_iterator pc = txin.scriptSig.begin();
        opcodetype opcode;
        std::vector<unsigned char> vch;
        bool fParsedCleanly = true;
        while (pc < txin.scriptSig.end()) {
            if (txin.scriptSig.GetOp(pc, opcode, vch)) {
                if (opcode >= 0 && opcode <= OP_PUSHDATA4) {
                    stack.push_back(vch);
                }
            } else {
                fParsedCleanly = false;
                break;
            }
        }
        if (fParsedCleanly && stack.size() == 3) {
            const std::vector<unsigned char>& control_block = stack[2];
            if (control_block.size() < 33 || (control_block.size() - 33) % 32 != 0 || (control_block[0] & 0xfe) != 0xc0) {
                continue;
            }
            if (fStrictRelayFormat && coinsView && coinsView->HaveCoinInCache(txin.prevout)) {
                const Coin& spentCoin = coinsView->AccessCoin(txin.prevout);
                const CScript& prevSpk = spentCoin.out.scriptPubKey;
                if (spentCoin.IsSpent() || prevSpk.size() != 34 || prevSpk[0] != OP_1 || prevSpk[1] != 32) {
                    continue;
                }
            }
            const std::vector<unsigned char>& leaf_script_bytes = stack[1];
            CScript leaf_script(leaf_script_bytes.begin(), leaf_script_bytes.end());
            if ((leaf_script.size() == 36 && leaf_script[0] == 33 && leaf_script[34] == OP_CHECKSIGVERIFY && leaf_script[35] == OP_1) ||
                (leaf_script.size() == 68 && leaf_script[0] == 65 && leaf_script[66] == OP_CHECKSIGVERIFY && leaf_script[67] == OP_1)) {
                CPubKey pubKey(leaf_script.begin() + 1, leaf_script.begin() + 1 + leaf_script[0]);
                if (pubKey.IsValid()) {
                    foundPubKey = pubKey;
                    fSignedInput = true;
                    break;
                }
            }
        }
    }

    // 2. Check outputs for direct MESCAL MasternodePing script (<33/65-byte pubkey> OP_CHECKSIGVERIFY OP_1).
    // IMPORTANT: Output-only matches (and all pings under UPGRADE_HARDENING_V1 / relay checks) MUST be accompanied
    // by a valid ECDSA signature (vchPingSig) from foundPubKey inside the OP_RETURN payload over
    // Hash(collateralOutpoint || tx.vin[0].prevout) to prevent keep-alive spoofing.
    if (!foundPubKey.IsValid()) {
        for (const CTxOut& out : tx.vout) {
            const CScript& script = out.scriptPubKey;
            if ((script.size() == 36 && script[0] == 33 && script[34] == OP_CHECKSIGVERIFY && script[35] == OP_1) ||
                (script.size() == 68 && script[0] == 65 && script[66] == OP_CHECKSIGVERIFY && script[67] == OP_1)) {
                CPubKey pubKey(script.begin() + 1, script.begin() + 1 + script[0]);
                if (pubKey.IsValid()) {
                    foundPubKey = pubKey;
                    fSignedInput = false;
                    break;
                }
            }
        }
    }

    if (!foundPubKey.IsValid()) return false;

    const int effectiveHeight = (nHeight >= 0) ? nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight + 1 : 1);
    const bool fRequirePingSig = !fSignedInput || fStrictRelayFormat ||
                                 Params().GetConsensus().NetworkUpgradeActive(effectiveHeight, Consensus::UPGRADE_HARDENING_V1);

    // 3. Extract collateralOutpoint + cryptographic authorization from OP_RETURN output
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey.IsUnspendable() && out.scriptPubKey.size() >= 38 && out.scriptPubKey[0] == OP_RETURN) {
            CScript::const_iterator o_pc = out.scriptPubKey.begin() + 1;
            opcodetype o_opcode;
            std::vector<unsigned char> o_vch;
            if (out.scriptPubKey.GetOp(o_pc, o_opcode, o_vch) && o_vch.size() >= 36) {
                if (fStrictRelayFormat && o_pc != out.scriptPubKey.end()) {
                    return false;
                }
                try {
                    CDataStream o_ss(o_vch, SER_NETWORK, PROTOCOL_VERSION);
                    COutPoint collateralOutpoint;
                    o_ss >> collateralOutpoint;

                    if (fRequirePingSig) {
                        if (o_ss.empty()) {
                            return false;
                        }
                        std::vector<unsigned char> vchPingSig;
                        o_ss >> vchPingSig;
                        CHashWriter ssPingHash(SER_GETHASH, 0);
                        ssPingHash << collateralOutpoint << tx.vin[0].prevout;
                        if (vchPingSig.empty() || !foundPubKey.Verify(ssPingHash.GetHash(), vchPingSig)) {
                            return false;
                        }
                    } else if (!o_ss.empty()) {
                        // Consume optional vchPingSig if present on legacy input-signed ping
                        std::vector<unsigned char> vchPingSig;
                        o_ss >> vchPingSig;
                    }

                    // Extract and verify optional on-chain Hot/Cold collateral delegation proof
                    if (!o_ss.empty()) {
                        bool hasDelegation = false;
                        o_ss >> hasDelegation;
                        if (hasDelegation) {
                            if (o_ss.empty()) {
                                return false;
                            }
                            COnChainMasternodeDelegation del;
                            o_ss >> del;
                            const bool fDelValid = (del.pubKeyMasternode == foundPubKey &&
                                                    VerifyOnChainDelegationSignature(del, collateralOutpoint));
                            if (fStrictRelayFormat && !fDelValid) {
                                return false;
                            }
                            if (fDelValid) {
                                if (pDelegationOut) {
                                    *pDelegationOut = del;
                                }
                                if (fRecordDelegation && coinsView) {
                                    Coin coin;
                                    int checkHeight = (nHeight >= 0) ? nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
                                    if (coinsView->GetCoin(collateralOutpoint, coin) && !coin.IsSpent() &&
                                        coin.out.nValue == CMasternode::GetMasternodeNodeCollateral(checkHeight) &&
                                        coin.out.scriptPubKey == GetScriptForDestination(del.pubKeyCollateralAddress.GetID())) {
                                        RecordCollateralDelegationWithHistory(collateralOutpoint, del, checkHeight);
                                    }
                                }
                            }
                        }
                    }

                    if (fStrictRelayFormat && !o_ss.empty()) {
                        return false;
                    }

                    pubKeyMasternodeOut = foundPubKey;
                    collateralOutpointOut = collateralOutpoint;
                    return true;
                } catch (...) {}
            }
        }
    }

    return false;
}

bool IsValidMasternodeCollateralAndAuth(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight, const COnChainMasternodeDelegation* pInlineDelegation, const CCoinsViewCache* pView) {
    if (!pubKeyMasternode.IsValid() || collateralOutpoint.IsNull()) {
        return false;
    }
    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;
    if (!coinsView) {
        return false;
    }
    Coin coin;
    if (!coinsView->GetCoin(collateralOutpoint, coin) || coin.IsSpent()) {
        return false;
    }
    int checkHeight = (nHeight >= 0) ? nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
    if (coin.out.nValue != CMasternode::GetMasternodeNodeCollateral(checkHeight)) {
        return false;
    }

    // 1. Direct on-chain ownership: 4,200 KRISTA collateral UTXO pays directly to pubKeyMasternode's P2PKH address
    CScript expectedP2PKH = GetScriptForDestination(pubKeyMasternode.GetID());
    if (coin.out.scriptPubKey == expectedP2PKH) {
        return true;
    }

    // 2. Inline delegation proof (for read-only mempool / GetMinRelayFee verification without mutating global state)
    if (pInlineDelegation && pInlineDelegation->pubKeyMasternode == pubKeyMasternode && pInlineDelegation->pubKeyCollateralAddress.IsValid()) {
        CScript collateralScript = GetScriptForDestination(pInlineDelegation->pubKeyCollateralAddress.GetID());
        if (coin.out.scriptPubKey == collateralScript && VerifyOnChainDelegationSignature(*pInlineDelegation, collateralOutpoint)) {
            return true;
        }
    }

    // 3. On-chain Hot/Cold delegation proof carried inside OP_RETURN (works during IBD even when P2P mnodeman is empty)
    auto itDel = mapOnChainCollateralDelegations.find(collateralOutpoint);
    if (itDel != mapOnChainCollateralDelegations.end()) {
        const COnChainMasternodeDelegation& del = itDel->second;
        if (del.pubKeyMasternode == pubKeyMasternode && del.pubKeyCollateralAddress.IsValid()) {
            CScript collateralScript = GetScriptForDestination(del.pubKeyCollateralAddress.GetID());
            if (coin.out.scriptPubKey == collateralScript && VerifyOnChainDelegationSignature(del, collateralOutpoint)) {
                return true;
            }
        }
    }

    return false;
}

bool IsMasternodeActiveAtHeight(const CPubKey& pubKeyMasternode, int nQueryHeight, int nWindow) {
    if (!pubKeyMasternode.IsValid()) return false;

    auto itSet = mapMasternodeActiveHeights.find(pubKeyMasternode);
    if (itSet != mapMasternodeActiveHeights.end() && !itSet->second.empty()) {
        auto itUpper = itSet->second.upper_bound(nQueryHeight);
        if (itUpper != itSet->second.begin()) {
            --itUpper;
            int pingHeight = *itUpper;
            if (pingHeight <= nQueryHeight && (nQueryHeight - pingHeight) <= nWindow) {
                return true;
            }
        }
    }

    auto itLast = mapMasternodeLastActiveHeight.find(pubKeyMasternode);
    if (itLast != mapMasternodeLastActiveHeight.end()) {
        int pingHeight = itLast->second;
        if (pingHeight <= nQueryHeight && (nQueryHeight - pingHeight) <= nWindow) {
            return true;
        }
    }

    return false;
}

bool RegisterOnChainMasternodePing(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight, const COnChainMasternodeDelegation* pInlineDelegation, const CCoinsViewCache* pView) {
    if (!IsValidMasternodeCollateralAndAuth(pubKeyMasternode, collateralOutpoint, nHeight, pInlineDelegation, pView)) {
        return false;
    }
    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;
    if (pInlineDelegation && pInlineDelegation->pubKeyMasternode == pubKeyMasternode && pInlineDelegation->pubKeyCollateralAddress.IsValid() && coinsView) {
        Coin coin;
        if (coinsView->GetCoin(collateralOutpoint, coin) && !coin.IsSpent() &&
            coin.out.scriptPubKey == GetScriptForDestination(pInlineDelegation->pubKeyCollateralAddress.GetID()) &&
            VerifyOnChainDelegationSignature(*pInlineDelegation, collateralOutpoint)) {
            RecordCollateralDelegationWithHistory(collateralOutpoint, *pInlineDelegation, nHeight);
        }
    }
    // Enforce strict 1-to-1 bijection between collateralOutpoint and pubKeyMasternode
    for (auto it = mapMasternodeCollateralOutpoints.begin(); it != mapMasternodeCollateralOutpoints.end(); ) {
        if (it->second == collateralOutpoint && it->first != pubKeyMasternode) {
            mapMasternodeLastActiveHeight.erase(it->first);
            mapMasternodeActiveHeights.erase(it->first);
            it = mapMasternodeCollateralOutpoints.erase(it);
        } else {
            ++it;
        }
    }
    auto& setHeights = mapMasternodeActiveHeights[pubKeyMasternode];
    setHeights.insert(nHeight);
    if (setHeights.size() > 64) {
        int nCutoffHeight = nHeight - 5760;
        for (auto itH = setHeights.begin(); itH != setHeights.end() && setHeights.size() > 1; ) {
            if (*itH < nCutoffHeight) {
                itH = setHeights.erase(itH);
            } else {
                break;
            }
        }
    }
    mapMasternodeLastActiveHeight[pubKeyMasternode] = *setHeights.rbegin();
    mapMasternodeCollateralOutpoints[pubKeyMasternode] = collateralOutpoint;
    ++g_nMasternodePingStateGen;
    return true;
}

static void CacheConnectedBlockMinerRegs(const CBlock& block, int nHeight);

void RegisterBlockMasternodeActivity(const CBlock& block, int nHeight, const CCoinsViewCache* pView) {
    LOCK(cs_main);
    CacheConnectedBlockMinerRegs(block, nHeight);
    for (const CTransaction& tx : block.vtx) {
        CPubKey pubKeyMasternode;
        COutPoint collateralOutpoint;
        COnChainMasternodeDelegation inlineDel;
        if (ExtractOnChainMasternodePing(tx, pubKeyMasternode, collateralOutpoint, false, &inlineDel, pView, nHeight, false)) {
            RegisterOnChainMasternodePing(pubKeyMasternode, collateralOutpoint, nHeight, &inlineDel, pView);
        }
    }

    // Zero-fee automatic on-chain heartbeat & BLS key pinning from verified ADAM/LLMQ block participation
    if (block.nVersion >= 11) {
        CBlockIndex* pindexPrev = nullptr;
        uint256 prevSeed;
        auto itPrev = mapBlockIndex.find(block.hashPrevBlock);
        if (itPrev != mapBlockIndex.end() && itPrev->second) {
            pindexPrev = itPrev->second;
            prevSeed = GetAdamSeed(pindexPrev);
        }
        const bool fFallbackMode = (block.nVersion == 11);
        const uint256 hashBlock = block.GetHash();
        const size_t minersToVerify = fFallbackMode
            ? (block.vAdamMiners.size() > 0 ? block.vAdamMiners.size() - 1 : 0)
            : block.vAdamMiners.size();

        for (size_t i = 0; i < minersToVerify; ++i) {
            if (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty()) {
                const CPubKey& mnKey = block.vAdamMiners[i];
                if (!mnKey.IsValid() || prevSeed.IsNull()) continue;
                if (!VerifyAdamSolution(block.hashPrevBlock, prevSeed, mnKey, block.vAdamSolutions[i], block.nBits, block.nVersion, nHeight, fFallbackMode ? -1 : (int)i)) {
                    continue;
                }
                try {
                    CDataStream ssSol(block.vAdamSolutions[i], SER_NETWORK, PROTOCOL_VERSION);
                    uint32_t nNonce = 0;
                    std::vector<unsigned char> vchSig;
                    ssSol >> nNonce >> vchSig;
                    CBLSPubKey blsPubKey;
                    if (ExtractBLSPubKeyFromSignedData(vchSig, blsPubKey)) {
                        PinBLSPubKey(mnKey, blsPubKey, nHeight);
                    }
                } catch (...) {}

                auto itOut = mapMasternodeCollateralOutpoints.find(mnKey);
                if (itOut != mapMasternodeCollateralOutpoints.end()) {
                    RegisterOnChainMasternodePing(mnKey, itOut->second, nHeight, nullptr, pView);
                }
            }
        }
        if (!block.vAdamCoordinatorSig.empty()) {
            CBLSPubKey coordBlsPubKey;
            if (ExtractBLSPubKeyFromSignedData(block.vAdamCoordinatorSig, coordBlsPubKey)) {
                try {
                    CDataStream ssCoord(block.vAdamCoordinatorSig, SER_NETWORK, PROTOCOL_VERSION);
                    CBLSSignedData signedData;
                    ssCoord >> signedData;
                    uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                    CPubKey matchedCoordKey;

                    // 1. O(1) Fast-path: check block.vAdamMiners.back() first (version 11 / fallback coordinator)
                    if (!block.vAdamMiners.empty() && block.vAdamMiners.back().IsValid() &&
                        block.vAdamMiners.back().Verify(hashPubKey, signedData.ecdsaSig)) {
                        matchedCoordKey = block.vAdamMiners.back();
                    }

                    // 2. O(1) Fast-path: check primary coordinator from SelectAdamNodes
                    if (!matchedCoordKey.IsValid() && !prevSeed.IsNull()) {
                        std::vector<CPubKey> vExpectedMiners;
                        CPubKey primaryCoord;
                        if (SelectAdamNodes(prevSeed, Params().GetConsensus(), vExpectedMiners, primaryCoord, nHeight, pView) &&
                            primaryCoord.IsValid() && primaryCoord.Verify(hashPubKey, signedData.ecdsaSig)) {
                            matchedCoordKey = primaryCoord;
                        }
                    }

                    // 3. Fast-path: check remaining miners in block.vAdamMiners (for PoBLS fallback-rotated coordinators)
                    if (!matchedCoordKey.IsValid()) {
                        for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                            if (it->IsValid() && it->Verify(hashPubKey, signedData.ecdsaSig)) {
                                matchedCoordKey = *it;
                                break;
                            }
                        }
                    }

                    // 4. Fallback: scan candidate pool if not matched above
                    if (!matchedCoordKey.IsValid()) {
                        std::vector<CPubKey> vPool = GetAdamMinerPool(nHeight - 1, pView);
                        for (const auto& candKey : vPool) {
                            if (candKey.IsValid() && candKey.Verify(hashPubKey, signedData.ecdsaSig)) {
                                matchedCoordKey = candKey;
                                break;
                            }
                        }
                    }

                    if (matchedCoordKey.IsValid() && VerifyBLSWithECDSAFallback(hashBlock, matchedCoordKey, block.vAdamCoordinatorSig)) {
                        PinBLSPubKey(matchedCoordKey, coordBlsPubKey, nHeight);
                        if (!block.vAdamVRFProof.empty() && !prevSeed.IsNull() &&
                            VerifyAdamVRFProof(prevSeed, block.vAdamVRFProof, matchedCoordKey)) {
                            CBLSPubKey vrfBlsPubKey;
                            if (ExtractBLSPubKeyFromSignedData(block.vAdamVRFProof, vrfBlsPubKey)) {
                                PinBLSPubKey(matchedCoordKey, vrfBlsPubKey, nHeight);
                            }
                        }
                        auto itOut = mapMasternodeCollateralOutpoints.find(matchedCoordKey);
                        if (itOut != mapMasternodeCollateralOutpoints.end()) {
                            RegisterOnChainMasternodePing(matchedCoordKey, itOut->second, nHeight, nullptr, pView);
                        }
                    }
                } catch (...) {}
            }
        }
        if (!block.vQuorumSig.empty()) {
            try {
                CDataStream ss(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
                llmq::CQuorumSignature qsig;
                ss >> qsig;
                if (qsig.blockHash == hashBlock) {
                    llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev, pView);
                    for (const auto& sigPair : qsig.signatures) {
                        const COutPoint& outpoint = sigPair.first;
                        CPubKey quorumMnKey;
                        if (!quorum.members.empty()) {
                            for (const auto& member : quorum.members) {
                                if (member.collateralOutpoint == outpoint && member.pubKeyMasternode.IsValid()) {
                                    quorumMnKey = member.pubKeyMasternode;
                                    break;
                                }
                            }
                            if (!quorumMnKey.IsValid()) continue;
                        } else {
                            auto itDel = mapOnChainCollateralDelegations.find(outpoint);
                            if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode.IsValid()) {
                                quorumMnKey = itDel->second.pubKeyMasternode;
                            } else {
                                for (const auto& kv : mapMasternodeCollateralOutpoints) {
                                    if (kv.second == outpoint && kv.first.IsValid()) {
                                        quorumMnKey = kv.first;
                                        break;
                                    }
                                }
                            }
                        }
                        if (quorumMnKey.IsValid() && VerifyBLSWithECDSAFallback(hashBlock, quorumMnKey, sigPair.second)) {
                            RegisterOnChainMasternodePing(quorumMnKey, outpoint, nHeight, nullptr, pView);
                            CBLSPubKey quorumBlsPubKey;
                            if (ExtractBLSPubKeyFromSignedData(sigPair.second, quorumBlsPubKey)) {
                                PinBLSPubKey(quorumMnKey, quorumBlsPubKey, nHeight);
                            }
                        }
                    }
                }
            } catch (...) {}
        }
    }
}

bool RestoreMasternodeActivityFromBlock(const CBlock& block, int nHeight, const CPubKey& targetPubKey, const COutPoint& targetCollateral, const CCoinsViewCache* pView, CPubKey* pRestoredPubKeyOut) {
    LOCK(cs_main);
    for (const CTransaction& prevTx : block.vtx) {
        CPubKey p_pubkey;
        COutPoint p_collateral;
        COnChainMasternodeDelegation p_del;
        if (ExtractOnChainMasternodePing(prevTx, p_pubkey, p_collateral, false, &p_del, pView, nHeight, false) &&
            (p_pubkey == targetPubKey || (!targetCollateral.IsNull() && p_collateral == targetCollateral))) {
            if (RegisterOnChainMasternodePing(p_pubkey, p_collateral, nHeight, &p_del, pView)) {
                if (pRestoredPubKeyOut) *pRestoredPubKeyOut = p_pubkey;
                return true;
            }
        }
    }

    if (block.nVersion < 11 || targetCollateral.IsNull()) {
        return false;
    }

    CBlockIndex* pindexPrev = nullptr;
    uint256 prevSeed;
    auto itPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (itPrev != mapBlockIndex.end() && itPrev->second) {
        pindexPrev = itPrev->second;
        prevSeed = GetAdamSeed(pindexPrev);
    }
    const bool fFallbackMode = (block.nVersion == 11);
    const uint256 hashBlock = block.GetHash();
    const size_t minersToVerify = fFallbackMode
        ? (block.vAdamMiners.size() > 0 ? block.vAdamMiners.size() - 1 : 0)
        : block.vAdamMiners.size();

    auto matchesTarget = [&](const CPubKey& candidateKey) -> bool {
        if (!candidateKey.IsValid()) return false;
        if (candidateKey == targetPubKey) return true;
        auto itDel = mapOnChainCollateralDelegations.find(targetCollateral);
        if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode == candidateKey) {
            return true;
        }
        return false;
    };

    for (size_t i = 0; i < minersToVerify; ++i) {
        if (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty()) {
            const CPubKey& mnKey = block.vAdamMiners[i];
            if (!matchesTarget(mnKey) || prevSeed.IsNull()) continue;
            if (VerifyAdamSolution(block.hashPrevBlock, prevSeed, mnKey, block.vAdamSolutions[i], block.nBits, block.nVersion, nHeight, fFallbackMode ? -1 : (int)i)) {
                if (RegisterOnChainMasternodePing(mnKey, targetCollateral, nHeight, nullptr, pView)) {
                    if (pRestoredPubKeyOut) *pRestoredPubKeyOut = mnKey;
                    return true;
                }
            }
        }
    }

    if (!block.vAdamCoordinatorSig.empty()) {
        CBLSPubKey coordBlsPubKey;
        if (ExtractBLSPubKeyFromSignedData(block.vAdamCoordinatorSig, coordBlsPubKey)) {
            try {
                CDataStream ssCoord(block.vAdamCoordinatorSig, SER_NETWORK, PROTOCOL_VERSION);
                CBLSSignedData signedData;
                ssCoord >> signedData;
                uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                CPubKey candidateKey = targetPubKey;
                if (!candidateKey.IsValid() || !candidateKey.Verify(hashPubKey, signedData.ecdsaSig)) {
                    auto itDel = mapOnChainCollateralDelegations.find(targetCollateral);
                    if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode.IsValid() &&
                        itDel->second.pubKeyMasternode.Verify(hashPubKey, signedData.ecdsaSig)) {
                        candidateKey = itDel->second.pubKeyMasternode;
                    } else {
                        candidateKey = CPubKey();
                    }
                }
                if (candidateKey.IsValid() && VerifyBLSWithECDSAFallback(hashBlock, candidateKey, block.vAdamCoordinatorSig)) {
                    if (RegisterOnChainMasternodePing(candidateKey, targetCollateral, nHeight, nullptr, pView)) {
                        if (pRestoredPubKeyOut) *pRestoredPubKeyOut = candidateKey;
                        return true;
                    }
                }
            } catch (...) {}
        }
    }

    if (!block.vQuorumSig.empty()) {
        try {
            CDataStream ss(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
            llmq::CQuorumSignature qsig;
            ss >> qsig;
            if (qsig.blockHash == hashBlock) {
                llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev, pView);
                for (const auto& sigPair : qsig.signatures) {
                    if (sigPair.first != targetCollateral) continue;
                    CPubKey quorumMnKey;
                    for (const auto& member : quorum.members) {
                        if (member.collateralOutpoint == targetCollateral && member.pubKeyMasternode.IsValid()) {
                            quorumMnKey = member.pubKeyMasternode;
                            break;
                        }
                    }
                    if (!quorumMnKey.IsValid()) {
                        if (targetPubKey.IsValid() && VerifyBLSWithECDSAFallback(hashBlock, targetPubKey, sigPair.second)) {
                            quorumMnKey = targetPubKey;
                        } else {
                            auto itDel = mapOnChainCollateralDelegations.find(targetCollateral);
                            if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode.IsValid()) {
                                quorumMnKey = itDel->second.pubKeyMasternode;
                            }
                        }
                    }
                    if (quorumMnKey.IsValid() && VerifyBLSWithECDSAFallback(hashBlock, quorumMnKey, sigPair.second)) {
                        if (RegisterOnChainMasternodePing(quorumMnKey, targetCollateral, nHeight, nullptr, pView)) {
                            if (pRestoredPubKeyOut) *pRestoredPubKeyOut = quorumMnKey;
                            return true;
                        }
                    }
                }
            }
        } catch (...) {}
    }

    return false;
}

bool FlushMasternodePingStateToDisk(const uint256& hashBestBlock) {
    if (!pblocktree || hashBestBlock.IsNull()) {
        return false;
    }
    COnChainMasternodeStateCache state;
    {
        LOCK(cs_main);
        state.hashBestBlock = hashBestBlock;
        state.mapLastActiveHeight = mapMasternodeLastActiveHeight;
        state.mapActiveHeights = mapMasternodeActiveHeights;
        state.mapCollateralOutpoints = mapMasternodeCollateralOutpoints;
        state.mapCollateralDelegations = mapOnChainCollateralDelegations;
        state.mapCollateralDelegationFirstHeight = mapCollateralDelegationFirstHeight;
        state.mapCollateralDelegationHistory = mapCollateralDelegationHistory;
        ExportPinnedBLSPubKeys(state.mapPinnedBLS);
    }
    bool fPingWritten = pblocktree->WriteMasternodePingState(state);
    FlushAdamMinerRegCacheToDisk(hashBestBlock);
    return fPingWritten;
}

void InitializeOnChainMasternodePings(bool fForceRescan) {
    LOCK(cs_main);
    ++g_nMasternodePingStateGen;
    mapMasternodeLastActiveHeight.clear();
    mapMasternodeActiveHeights.clear();
    mapMasternodeCollateralOutpoints.clear();
    mapOnChainCollateralDelegations.clear();
    mapCollateralDelegationFirstHeight.clear();
    mapCollateralDelegationHistory.clear();
    ClearPinnedBLSPubKeys();
    const CBlockIndex* pindexTip = chainActive.Tip();
    if (!pindexTip || pindexTip->nHeight <= 0) {
        InitializeAdamMinerRegCache(fForceRescan);
        g_fMasternodePingStateInitialized = true;
        return;
    }

    int nTipHeight = pindexTip->nHeight;
    int nStartHeight = std::max(1, Params().GetConsensus().vUpgrades[Consensus::UPGRADE_ADAM].nActivationHeight);
    bool fLoadedFromDB = false;

    COnChainMasternodeStateCache cachedState;
    if (!fForceRescan && pblocktree && pblocktree->ReadMasternodePingState(cachedState)) {
        auto mi = mapBlockIndex.find(cachedState.hashBestBlock);
        if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
            int nCachedHeight = mi->second->nHeight;
            mapMasternodeCollateralOutpoints = std::move(cachedState.mapCollateralOutpoints);
            mapOnChainCollateralDelegations = std::move(cachedState.mapCollateralDelegations);
            mapCollateralDelegationFirstHeight = std::move(cachedState.mapCollateralDelegationFirstHeight);
            mapCollateralDelegationHistory = std::move(cachedState.mapCollateralDelegationHistory);
            for (const auto& delPair : mapOnChainCollateralDelegations) {
                if (!mapCollateralDelegationFirstHeight.count(delPair.first)) {
                    mapCollateralDelegationFirstHeight[delPair.first] = 1;
                }
            }
            ImportPinnedBLSPubKeys(cachedState.mapPinnedBLS, nCachedHeight);

            for (const auto& pair : cachedState.mapActiveHeights) {
                if (!pair.first.IsValid()) continue;
                std::set<int> validHeights;
                for (int h : pair.second) {
                    if (h <= nCachedHeight) {
                        validHeights.insert(h);
                    }
                }
                if (!validHeights.empty()) {
                    mapMasternodeLastActiveHeight[pair.first] = *validHeights.rbegin();
                    mapMasternodeActiveHeights[pair.first] = std::move(validHeights);
                }
            }
            for (const auto& pair : cachedState.mapLastActiveHeight) {
                if (pair.first.IsValid() && pair.second <= nCachedHeight && !mapMasternodeLastActiveHeight.count(pair.first)) {
                    mapMasternodeLastActiveHeight[pair.first] = pair.second;
                    mapMasternodeActiveHeights[pair.first].insert(pair.second);
                }
            }

            nStartHeight = nCachedHeight + 1;
            fLoadedFromDB = true;
            LogPrintf("InitializeOnChainMasternodePings: Loaded LevelDB masternode/BLS state at height %d (%u masternodes, %u delegations, %u pinned BLS keys), scanning %d delta blocks...\n",
                nCachedHeight,
                (unsigned int)mapMasternodeLastActiveHeight.size(),
                (unsigned int)mapOnChainCollateralDelegations.size(),
                (unsigned int)cachedState.mapPinnedBLS.size(),
                std::max(0, nTipHeight - nCachedHeight));
        }
    }

    for (int h = nStartHeight; h <= nTipHeight; ++h) {
        CBlockIndex* pindex = chainActive[h];
        if (!pindex) continue;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) continue;
        RegisterBlockMasternodeActivity(block, pindex->nHeight);
    }

    ++g_nMasternodePingStateGen;
    InitializeAdamMinerRegCache(fForceRescan);
    g_fMasternodePingStateInitialized = true;
    if (!fLoadedFromDB || nStartHeight <= nTipHeight) {
        FlushMasternodePingStateToDisk(pindexTip->GetBlockHash());
    }
    LogPrintf("InitializeOnChainMasternodePings: Ready with %u active masternodes and %u delegations at height %d.\n",
        (unsigned int)mapMasternodeLastActiveHeight.size(),
        (unsigned int)mapOnChainCollateralDelegations.size(),
        nTipHeight);
}

CScript GetMasternodePingScript(const CPubKey& pubKeyMasternode) {
    static RecursiveMutex cs_ping_script_cache;
    static std::map<CPubKey, CScript> mapPingScriptCache;
    {
        LOCK(cs_ping_script_cache);
        auto it = mapPingScriptCache.find(pubKeyMasternode);
        if (it != mapPingScriptCache.end()) {
            return it->second;
        }
    }

    std::string jsonStr = "{\"basic\":{\"SigVerify\":{\"role\":\"check-signature-verification\",\"inputs\":[{\"name\":\"Pubkey\",\"type\":\"pubkey\",\"value\":\"" + HexStr(pubKeyMasternode) + "\"}]},\n\"TrueVal\":{\"role\":\"number\",\"inputs\":[{\"name\":\"Number\",\"type\":\"number\",\"value\":1}]}},\n\"contract\":{\"MasternodePing\":{\"actions\":[{\"type\":\"basic\",\"name\":\"SigVerify\"},{\"type\":\"basic\",\"name\":\"TrueVal\"}]}},\n\"active_contract\":\"MasternodePing\"}";
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    if (!errorStr.empty()) {
        LogPrintf("GetMasternodePingScript: MESCAL compilation failed for key %s: %s\n", pubKeyMasternode.GetID().ToString(), errorStr);
        script.clear();
        script << ToByteVector(pubKeyMasternode) << OP_CHECKSIGVERIFY << OP_1;
    }

    {
        LOCK(cs_ping_script_cache);
        if (mapPingScriptCache.size() > 2000) {
            mapPingScriptCache.clear();
        }
        mapPingScriptCache[pubKeyMasternode] = script;
    }
    return script;
}

bool IsModelDActive(int nHeight) {
    return Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MODELD);
}

bool IsAdamValidatorSplitActive(int nHeight) {
    if (nHeight < 0) return false;
    if (IsModelDActive(nHeight)) return true;
    const Consensus::Params& consensus = Params().GetConsensus();
    return IsAdamActive(nHeight, consensus) &&
           consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_HARDENING_V2);
}

bool IsAdamDualTrackActive(int nHeight) {
    const Consensus::Params& consensus = Params().GetConsensus();
    return IsAdamActive(nHeight, consensus) && nHeight >= consensus.nAdamDualTrackHeight;
}

CKey GetAdamDeterministicKey(int index) {
    std::string seedBase = GetArg("-adamminerseed", "");
    if (seedBase.empty()) {
        seedBase = "adam_miner_seed_";
    }
    std::string seed = seedBase + std::to_string(index);
    uint256 secret = Hash(seed.begin(), seed.end());
    CKey key;
    key.Set(secret.begin(), secret.end(), true);
    return key;
}

CPubKey GetAdamDeterministicPubKey(int index) {
    return GetAdamDeterministicKey(index).GetPubKey();
}

uint256 GetMinerPoWLimit(const std::string& networkId, bool fHardeningV2) {
    if (fHardeningV2) {
        // Under UPGRADE_HARDENING_V2, Funded 10 KRISTA PoW-Lock solves 7-level recursive HashX11KVS (~2,200 H/s per desktop core):
        if (networkId == "main") {
            return uint256S("0001ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 15 zero bits (~32,768 X11KVS hashes, ~14.8s CPU)
        } else if (networkId == "test") {
            return uint256S("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 13 zero bits (~8,192 X11KVS hashes, ~3.7s CPU)
        } else { // regtest
            return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 8 zero bits (~256 X11KVS hashes, ~0.11s CPU)
        }
    }
    if (networkId == "main") {
        return uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 28 bits (SHA256d)
    } else if (networkId == "test") {
        return uint256S("00000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 20 bits (SHA256d)
    } else { // regtest
        return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 8 bits (SHA256d)
    }
}

uint256 GetZeroCoinPoWLimit(const std::string& networkId, bool fHardeningV2) {
    if (fHardeningV2) {
        // Under UPGRADE_HARDENING_V2, Zero-Coin PoW solves 7-level recursive HashX11KVS (~2,200 H/s per desktop core):
        if (networkId == "main") {
            return uint256S("0003ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 14 zero bits (~16,384 X11KVS hashes, ~7.4s CPU)
        } else if (networkId == "test") {
            return uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 12 zero bits (~4,096 X11KVS hashes, ~1.8s CPU)
        } else { // regtest
            return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 8 zero bits (~256 X11KVS hashes, ~0.11s CPU)
        }
    }
    if (networkId == "main") {
        return uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 24 bits (SHA256d)
    } else if (networkId == "test") {
        return uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 17 bits (SHA256d)
    } else { // regtest
        return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 8 bits (SHA256d)
    }
}

int GetZeroCoinRegPeriod(int nHeight) {
    const int evalHeight = std::max(0, nHeight);
    const bool fHardeningV2 = Params().GetConsensus().NetworkUpgradeActive(evalHeight, Consensus::UPGRADE_HARDENING_V2);
    if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
        if (IsModelDActive(evalHeight + 1)) {
            return fHardeningV2 ? 50 : 100;
        }
    }
    return fHardeningV2 ? ADAM_ZEROCOIN_POW_REG_PERIOD_V2 : 2880;
}

void BuildMinerPoWRegistrationHeaderPrefix(const uint256& challenge, const CPubKey& pubkey, unsigned char outPrefix76[76]) {
    memcpy(outPrefix76, "KRPL", 4);
    memcpy(outPrefix76 + 4, challenge.begin(), 32);
    if (pubkey.size() == 33) {
        memcpy(outPrefix76 + 36, pubkey.begin(), 33);
    } else {
        uint256 pkDigest = Hash(pubkey.begin(), pubkey.end());
        outPrefix76[36] = 0x04;
        memcpy(outPrefix76 + 37, pkDigest.begin(), 32);
    }
    memcpy(outPrefix76 + 69, "POWLKv2", 7);
}

uint256 ComputeMinerPoWRegistrationHash(uint32_t nNonce, const uint256& challenge, const CPubKey& pubkey, bool fHardeningV2) {
    if (!fHardeningV2) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << nNonce;
        ss << challenge;
        ss << pubkey;
        return ss.GetHash();
    }
    unsigned char buf[80];
    BuildMinerPoWRegistrationHeaderPrefix(challenge, pubkey, buf);
    WriteLE32(buf + 76, nNonce);
    return HashX11KVS(buf, buf + 80);
}

void BuildZeroCoinRegistrationHeaderPrefix(const uint256& challenge, const CPubKey& pubkey, unsigned char outPrefix76[76]) {
    memcpy(outPrefix76, "KRZC", 4);
    memcpy(outPrefix76 + 4, challenge.begin(), 32);
    if (pubkey.size() == 33) {
        memcpy(outPrefix76 + 36, pubkey.begin(), 33);
    } else {
        uint256 pkDigest = Hash(pubkey.begin(), pubkey.end());
        outPrefix76[36] = 0x04;
        memcpy(outPrefix76 + 37, pkDigest.begin(), 32);
    }
    memcpy(outPrefix76 + 69, "SYBILV2", 7);
}

uint256 ComputeZeroCoinRegistrationHash(uint32_t nNonce, const uint256& challenge, const CPubKey& pubkey, bool fHardeningV2) {
    if (!fHardeningV2) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << nNonce;
        ss << challenge;
        ss << pubkey;
        return ss.GetHash();
    }
    unsigned char buf[80];
    BuildZeroCoinRegistrationHeaderPrefix(challenge, pubkey, buf);
    WriteLE32(buf + 76, nNonce);
    return HashX11KVS(buf, buf + 80);
}

static RecursiveMutex cs_miner_pool_cache;
static std::map<uint256, std::vector<CPubKey>> mapMinerPoolCache;
static std::map<uint256, std::set<CPubKey>> mapMasternodePoolCache;
static std::map<uint256, std::set<CPubKey>> mapCoinLockPoolCache;
static std::map<uint256, std::set<CPubKey>> mapFundedPoWPoolCache;
static std::map<uint256, std::set<CPubKey>> mapBootstrapPoolCache;
static std::map<uint256, size_t> mapExplicitPoolCountCache;
static std::map<uint256, size_t> mapFundedPoolCountCache;
static std::map<uint256, std::vector<CCachedBlockRegEntry>> mapBlockRegCache;
static std::set<CPubKey> cachedBootstrapKeys;
static int cachedBootstrapScannedUpTo = 0;
static uint256 cachedGenesisHash;
static uint64_t cachedPoolStateGen = 0;

static void ExtractBlockMinerRegEntries(const CBlock& block, int nBlockHeight, std::vector<CCachedBlockRegEntry>& vecEntriesOut) {
    vecEntriesOut.clear();
    const bool fHardeningV2 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, nBlockHeight), Consensus::UPGRADE_HARDENING_V2);
    const uint256 powLimitTarget = GetMinerPoWLimit(Params().NetworkIDString(), fHardeningV2);
    const uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), fHardeningV2);
    int nZeroCoinTxInBlock = 0;
    int nPoWLockTxInBlock = 0;
    const bool fDualTrackRules = IsAdamDualTrackActive(nBlockHeight);
    for (const auto& tx : block.vtx) {
        bool isZeroCoinTx = IsZeroCoinPoWTransaction(tx);
        if (isZeroCoinTx) {
            if (tx.vout.size() != 1 || nZeroCoinTxInBlock >= 1) continue;
            nZeroCoinTxInBlock++;
        }
        uint256 txid = tx.GetHash();
        int nPoWLockOutputsInTx = 0;
        for (size_t i = 0; i < tx.vout.size(); ++i) {
            const auto& vout = tx.vout[i];
            if (vout.scriptPubKey.size() < 60) continue;
            CPubKey pubkey;
            int64_t lockTime = 0;
            CKeyID pubkeyHash;

            if (MatchCoinLockRegistration(vout.scriptPubKey, pubkey, lockTime, pubkeyHash)) {
                if (pubkey.GetID() != pubkeyHash) continue;
                if (vout.nValue < MINER_REGISTRATION_LOCK_AMOUNT) continue;
                CCachedBlockRegEntry entry;
                entry.isCoinLock = true;
                entry.outpoint = COutPoint(txid, i);
                entry.nValue = vout.nValue;
                entry.pubkey = pubkey;
                entry.lockTime = lockTime;
                vecEntriesOut.push_back(entry);
            } else {
                std::vector<unsigned char> nonce;
                uint256 challenge;
                if (MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash)) {
                    if (fDualTrackRules) {
                        if (nPoWLockOutputsInTx >= 1 || nPoWLockTxInBlock >= ADAM_MAX_POW_LOCK_TX_PER_BLOCK) {
                            continue;
                        }
                    }
                    if (pubkey.GetID() != pubkeyHash) continue;
                    if (nonce.size() != 4) continue;
                    uint32_t nNonce = nonce[0] | (nonce[1] << 8) | (nonce[2] << 16) | (nonce[3] << 24);
                    if (vout.nValue == 0) {
                        if (!isZeroCoinTx) continue;
                        if (fHardeningV2 && !pubkey.IsCompressed()) continue;
                        uint256 puzzleHash = ComputeZeroCoinRegistrationHash(nNonce, challenge, pubkey, fHardeningV2);
                        if (puzzleHash > zeroCoinTarget) continue;
                    } else {
                        const CAmount minPoWLockVal = fDualTrackRules ? POW_LOCK_REGISTRATION_AMOUNT : 10000;
                        if (vout.nValue < minPoWLockVal) continue;
                        if (fHardeningV2 && !pubkey.IsCompressed()) continue;
                        uint256 puzzleHash = ComputeMinerPoWRegistrationHash(nNonce, challenge, pubkey, fHardeningV2);
                        if (puzzleHash > powLimitTarget) continue;
                    }
                    if (nPoWLockOutputsInTx == 0) {
                        nPoWLockTxInBlock++;
                    }
                    nPoWLockOutputsInTx++;
                    CCachedBlockRegEntry entry;
                    entry.isCoinLock = false;
                    entry.outpoint = COutPoint(txid, i);
                    entry.nValue = vout.nValue;
                    entry.pubkey = pubkey;
                    entry.lockTime = lockTime;
                    entry.challenge = challenge;
                    vecEntriesOut.push_back(entry);
                }
            }
        }
    }
}

static void ExtractBootstrapCoinbaseKey(const CBlock& block, std::set<CPubKey>& keysOut) {
    if (block.vtx.empty() || block.vtx[0].vout.empty()) return;
    const CTxOut& vout = block.vtx[0].vout[0];
    CScript::const_iterator pc = vout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> vchPubKey;
    if (vout.scriptPubKey.GetOp(pc, opcode, vchPubKey) && (vchPubKey.size() == 33 || vchPubKey.size() == 65)) {
        if (vout.scriptPubKey.GetOp(pc, opcode) && opcode == OP_CHECKSIG) {
            CPubKey pubkey(vchPubKey);
            if (pubkey.IsValid()) {
                keysOut.insert(pubkey);
            }
        }
    }
}

static void CacheConnectedBlockMinerRegs(const CBlock& block, int nHeight) {
    if (nHeight <= 0) return;
    LOCK(cs_miner_pool_cache);
    if (nHeight <= 199 && nHeight < Params().GetConsensus().nAdamBootstrapLimit) {
        const uint256 genesisHash = chainActive.Genesis() ? chainActive.Genesis()->GetBlockHash() : UINT256_ZERO;
        if (!genesisHash.IsNull() && genesisHash != cachedGenesisHash) {
            cachedBootstrapKeys.clear();
            cachedBootstrapScannedUpTo = 0;
            cachedGenesisHash = genesisHash;
        }
        ExtractBootstrapCoinbaseKey(block, cachedBootstrapKeys);
        if (nHeight == cachedBootstrapScannedUpTo + 1) {
            cachedBootstrapScannedUpTo = nHeight;
        }
    }
    if (mapBlockRegCache.size() > 5000) {
        mapBlockRegCache.clear();
    }
    const uint256 blockHash = block.GetHash();
    if (!mapBlockRegCache.count(blockHash)) {
        std::vector<CCachedBlockRegEntry> vecEntries;
        ExtractBlockMinerRegEntries(block, nHeight, vecEntries);
        mapBlockRegCache.emplace(blockHash, std::move(vecEntries));
    }
}

bool FlushAdamMinerRegCacheToDisk(const uint256& hashBestBlock) {
    if (!pblocktree || hashBestBlock.IsNull()) {
        return false;
    }
    CAdamBlockRegStateCache state;
    {
        LOCK(cs_main);
        LOCK(cs_miner_pool_cache);
        state.hashBestBlock = hashBestBlock;
        state.cachedGenesisHash = chainActive.Genesis() ? chainActive.Genesis()->GetBlockHash() : cachedGenesisHash;
        state.cachedBootstrapScannedUpTo = cachedBootstrapScannedUpTo;
        state.cachedBootstrapKeys = cachedBootstrapKeys;

        CBlockIndex* pindexBest = nullptr;
        auto mi = mapBlockIndex.find(hashBestBlock);
        if (mi != mapBlockIndex.end() && mi->second) {
            pindexBest = mi->second;
        } else if (chainActive.Tip() && chainActive.Tip()->GetBlockHash() == hashBestBlock) {
            pindexBest = chainActive.Tip();
        }

        state.vRecentBlockRegs.reserve(2880);
        for (int count = 0; pindexBest && count < 2880 && pindexBest->nHeight > 0; pindexBest = pindexBest->pprev, ++count) {
            const uint256 blkHash = pindexBest->GetBlockHash();
            auto it = mapBlockRegCache.find(blkHash);
            if (it != mapBlockRegCache.end()) {
                state.vRecentBlockRegs.emplace_back(blkHash, it->second);
            }
        }
    }
    return pblocktree->WriteAdamMinerRegCache(state);
}

void InitializeAdamMinerRegCache(bool fForceRescan) {
    LOCK(cs_main);
    LOCK(cs_miner_pool_cache);
    mapMinerPoolCache.clear();
    mapMasternodePoolCache.clear();
    mapCoinLockPoolCache.clear();
    mapFundedPoWPoolCache.clear();
    mapBootstrapPoolCache.clear();
    mapExplicitPoolCountCache.clear();
    mapFundedPoolCountCache.clear();
    mapBlockRegCache.clear();
    cachedBootstrapKeys.clear();
    cachedBootstrapScannedUpTo = 0;
    cachedGenesisHash = chainActive.Genesis() ? chainActive.Genesis()->GetBlockHash() : UINT256_ZERO;

    const CBlockIndex* pindexTip = chainActive.Tip();
    if (!pindexTip || pindexTip->nHeight <= 0) {
        return;
    }

    const int nTipHeight = pindexTip->nHeight;
    const int nWindowStart = std::max(1, nTipHeight - 2880 + 1);
    int nDeltaStart = nWindowStart;
    bool fLoadedFromDB = false;

    CAdamBlockRegStateCache cachedState;
    if (!fForceRescan && pblocktree && pblocktree->ReadAdamMinerRegCache(cachedState)) {
        auto mi = mapBlockIndex.find(cachedState.hashBestBlock);
        if (mi != mapBlockIndex.end() && mi->second && chainActive.Contains(mi->second) &&
            (cachedGenesisHash.IsNull() || cachedState.cachedGenesisHash.IsNull() || cachedState.cachedGenesisHash == cachedGenesisHash)) {
            int nCachedHeight = mi->second->nHeight;
            for (auto& pair : cachedState.vRecentBlockRegs) {
                mapBlockRegCache.emplace(pair.first, std::move(pair.second));
            }
            if (cachedState.cachedBootstrapScannedUpTo <= nCachedHeight) {
                cachedBootstrapKeys = std::move(cachedState.cachedBootstrapKeys);
                cachedBootstrapScannedUpTo = cachedState.cachedBootstrapScannedUpTo;
            }
            nDeltaStart = std::max(nWindowStart, nCachedHeight + 1);
            fLoadedFromDB = true;
            LogPrintf("InitializeAdamMinerRegCache: Loaded LevelDB ADAM reg cache at height %d (%u recent blocks, bootstrap up to %d), scanning %d delta blocks...\n",
                nCachedHeight,
                (unsigned int)mapBlockRegCache.size(),
                cachedBootstrapScannedUpTo,
                std::max(0, nTipHeight - nDeltaStart + 1));
        }
    }

    const int nBootstrapLimit = Params().GetConsensus().nAdamBootstrapLimit;
    if (nTipHeight < nBootstrapLimit) {
        const int nScanLimit = std::min(199, nTipHeight);
        for (int h = cachedBootstrapScannedUpTo + 1; h <= nScanLimit; ++h) {
            CBlockIndex* pindex = chainActive[h];
            if (!pindex) break;
            CBlock block;
            if (ReadBlockFromDisk(block, pindex)) {
                ExtractBootstrapCoinbaseKey(block, cachedBootstrapKeys);
                if (h >= nWindowStart && !mapBlockRegCache.count(pindex->GetBlockHash())) {
                    std::vector<CCachedBlockRegEntry> vecEntries;
                    ExtractBlockMinerRegEntries(block, h, vecEntries);
                    mapBlockRegCache.emplace(pindex->GetBlockHash(), std::move(vecEntries));
                }
                cachedBootstrapScannedUpTo = h;
            }
        }
    }

    for (int h = nDeltaStart; h <= nTipHeight; ++h) {
        CBlockIndex* pindex = chainActive[h];
        if (!pindex) continue;
        const uint256 blockHash = pindex->GetBlockHash();
        if (mapBlockRegCache.count(blockHash)) continue;
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) continue;
        std::vector<CCachedBlockRegEntry> vecEntries;
        ExtractBlockMinerRegEntries(block, h, vecEntries);
        mapBlockRegCache.emplace(blockHash, std::move(vecEntries));
    }

    if (!fLoadedFromDB || nDeltaStart <= nTipHeight) {
        FlushAdamMinerRegCacheToDisk(pindexTip->GetBlockHash());
    }
}

std::vector<CPubKey> GetAdamMinerPool(int nHeight, const CCoinsViewCache* pView, std::set<CPubKey>* pMasternodeKeysOut, std::set<CPubKey>* pCoinLockKeysOut, size_t* pExplicitCountOut, size_t* pFundedCountOut, std::set<CPubKey>* pFundedPoWKeysOut, std::set<CPubKey>* pBootstrapKeysOut) {
    LOCK(cs_main);
    LOCK(cs_miner_pool_cache);
    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;
    const bool fUsePoolCache = !pView || (pcoinsTip && pView->GetBestBlock() == pcoinsTip->GetBestBlock());
    if (cachedPoolStateGen != g_nMasternodePingStateGen) {
        mapMinerPoolCache.clear();
        mapMasternodePoolCache.clear();
        mapCoinLockPoolCache.clear();
        mapFundedPoWPoolCache.clear();
        mapBootstrapPoolCache.clear();
        mapExplicitPoolCountCache.clear();
        mapFundedPoolCountCache.clear();
        cachedPoolStateGen = g_nMasternodePingStateGen;
    }
    if (mapMinerPoolCache.size() > 500) {
        mapMinerPoolCache.clear();
        mapMasternodePoolCache.clear();
        mapCoinLockPoolCache.clear();
        mapFundedPoWPoolCache.clear();
        mapBootstrapPoolCache.clear();
        mapExplicitPoolCountCache.clear();
        mapFundedPoolCountCache.clear();
    }
    if (mapBlockRegCache.size() > 5000) {
        mapBlockRegCache.clear();
    }
    CBlockIndex* pindexTip = nullptr;
    if (pView && !pView->GetBestBlock().IsNull()) {
        auto itView = mapBlockIndex.find(pView->GetBestBlock());
        if (itView != mapBlockIndex.end() && itView->second) {
            if (nHeight == -1 || nHeight == itView->second->nHeight) {
                pindexTip = itView->second;
            } else if (nHeight >= 0 && nHeight < itView->second->nHeight) {
                pindexTip = itView->second->GetAncestor(nHeight);
            }
        }
    }
    if (!pindexTip) {
        if (nHeight == -1) {
            pindexTip = chainActive.Tip();
        } else if (nHeight >= 0 && nHeight <= chainActive.Height()) {
            pindexTip = chainActive[nHeight];
        }
    }
    if (!pindexTip) {
        pindexTip = chainActive.Tip();
    }
    if (fUsePoolCache && pindexTip && mapMinerPoolCache.count(pindexTip->GetBlockHash())) {
        if (pMasternodeKeysOut) {
            *pMasternodeKeysOut = mapMasternodePoolCache[pindexTip->GetBlockHash()];
        }
        if (pCoinLockKeysOut) {
            *pCoinLockKeysOut = mapCoinLockPoolCache[pindexTip->GetBlockHash()];
        }
        if (pFundedPoWKeysOut) {
            *pFundedPoWKeysOut = mapFundedPoWPoolCache[pindexTip->GetBlockHash()];
        }
        if (pBootstrapKeysOut) {
            *pBootstrapKeysOut = mapBootstrapPoolCache[pindexTip->GetBlockHash()];
        }
        if (pExplicitCountOut) {
            *pExplicitCountOut = mapExplicitPoolCountCache[pindexTip->GetBlockHash()];
        }
        if (pFundedCountOut) {
            *pFundedCountOut = mapFundedPoolCountCache[pindexTip->GetBlockHash()];
        }
        return mapMinerPoolCache[pindexTip->GetBlockHash()];
    }

    std::set<CPubKey> uniqueKeys;
    std::set<CPubKey> masternodeKeys;
    std::set<CPubKey> coinLockKeys;
    std::set<CPubKey> fundedPoWKeys;
    std::set<CPubKey> bootstrapKeys;
    std::set<CPubKey> fundedValidatorKeys;

    if (Params().IsRegTestNet()) {
        for (int i = 0; i < 15; ++i) {
            uniqueKeys.insert(GetAdamDeterministicPubKey(i));
        }
    }

    int currentRefHeight = pindexTip ? pindexTip->nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
    const bool fHardeningV1 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, currentRefHeight), Consensus::UPGRADE_HARDENING_V1);
    const bool fHardeningV2 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, currentRefHeight), Consensus::UPGRADE_HARDENING_V2);
    const int nHardeningV2Height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_HARDENING_V2].nActivationHeight;

    std::set<COutPoint> seenCollateralOutpoints;
    for (const auto& kv : mapMasternodeCollateralOutpoints) {
        if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, currentRefHeight, 2880)) {
            if (!seenCollateralOutpoints.count(kv.second) &&
                IsValidMasternodeCollateralAndAuth(kv.first, kv.second, currentRefHeight, nullptr, pView)) {
                seenCollateralOutpoints.insert(kv.second);
                uniqueKeys.insert(kv.first);
                masternodeKeys.insert(kv.first);
                fundedValidatorKeys.insert(kv.first);
            }
        }
    }

    std::set<CPubKey> zeroCoinKeys;
    std::vector<CPubKey> zeroCoinKeysOrdered;
    if (pindexTip) {
        int nTipHeight = pindexTip->nHeight;
        int nRegPeriod = 2880;
        if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (IsModelDActive(nTipHeight + 1)) {
                nRegPeriod = 100;
            }
        }
        const int nZeroActivePeriod = GetZeroCoinRegPeriod(nTipHeight);
        int nLimit = std::max(0, nTipHeight - nRegPeriod);

        for (int h = nTipHeight; h > nLimit; --h) {
            CBlockIndex* pindex = (pindexTip && (pindexTip->pprev || h == nTipHeight))
                                      ? pindexTip->GetAncestor(h)
                                      : (h <= chainActive.Height() ? chainActive[h] : nullptr);
            if (!pindex) continue;

            const uint256 blockHash = pindex->GetBlockHash();
            auto cacheIt = mapBlockRegCache.find(blockHash);
            if (cacheIt == mapBlockRegCache.end()) {
                std::vector<CCachedBlockRegEntry> vecEntries;
                CBlock block;
                if (ReadBlockFromDisk(block, pindex)) {
                    ExtractBlockMinerRegEntries(block, pindex->nHeight, vecEntries);
                }
                cacheIt = mapBlockRegCache.emplace(blockHash, std::move(vecEntries)).first;
            }

            const bool fEntryV2 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, pindex->nHeight), Consensus::UPGRADE_HARDENING_V2);
            for (const auto& entry : cacheIt->second) {
                if (fHardeningV1 && entry.lockTime >= LOCKTIME_THRESHOLD) continue;
                if (entry.isCoinLock) {
                    if (entry.lockTime < pindex->nHeight + nRegPeriod) continue;
                    if (!coinsView || !coinsView->HaveCoin(entry.outpoint)) continue;
                    uniqueKeys.insert(entry.pubkey);
                    coinLockKeys.insert(entry.pubkey);
                    fundedValidatorKeys.insert(entry.pubkey);

                    if (pindexTip) {
                        int64_t remaining = entry.lockTime - pindexTip->nHeight;
                        if (remaining > 0 && (remaining == 240 || remaining == 100 || remaining == 20 || remaining == 5)) {
                            static std::set<std::pair<CKeyID, int64_t>> setWarnedCoinLock;
                            std::pair<CKeyID, int64_t> warnKey = std::make_pair(entry.pubkey.GetID(), remaining);
                            if (!setWarnedCoinLock.count(warnKey)) {
                                setWarnedCoinLock.insert(warnKey);
                                LogPrintf("ADAM WARNING: Coin-Lock miner registration for key %s is expiring in %d blocks (~%d minutes). Please renew!\n",
                                          entry.pubkey.GetID().ToString(), remaining, remaining * 30 / 60);
                            }
                        }
                    }
                } else {
                    const bool isZeroCoinEntry = (entry.nValue == 0);
                    const int nRequiredPeriod = isZeroCoinEntry
                        ? (fEntryV2 ? GetZeroCoinRegPeriod(pindex->nHeight) : nRegPeriod)
                        : nRegPeriod;
                    if (entry.lockTime < pindex->nHeight + nRequiredPeriod) continue;

                    if (isZeroCoinEntry && fHardeningV2) {
                        const int64_t nBaseHeight = fEntryV2
                            ? pindex->nHeight
                            : std::max(pindex->nHeight, std::max(0, nHardeningV2Height));
                        const int64_t nEffectiveExpiry = std::min<int64_t>(entry.lockTime, nBaseHeight + nZeroActivePeriod);
                        if (nTipHeight >= nEffectiveExpiry) continue;
                    }

                    const int nMaxChallengeAge = (isZeroCoinEntry && fEntryV2)
                        ? ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2
                        : 100;
                    bool challengeValid = false;
                    auto miChallenge = mapBlockIndex.find(entry.challenge);
                    if (miChallenge != mapBlockIndex.end() && miChallenge->second) {
                        CBlockIndex* pindexChallenge = miChallenge->second;
                        const bool isChallengeAncestor = pindex->pprev
                            ? (pindex->GetAncestor(pindexChallenge->nHeight) == pindexChallenge)
                            : (pindexChallenge->nHeight <= chainActive.Height() &&
                               chainActive[pindexChallenge->nHeight] == pindexChallenge);
                        if (pindexChallenge->nHeight >= 0 && isChallengeAncestor) {
                            if (pindexChallenge->nHeight >= pindex->nHeight - nMaxChallengeAge && pindexChallenge->nHeight < pindex->nHeight) {
                                challengeValid = true;
                            }
                        }
                    }
                    if (!challengeValid) continue;

                    if (entry.nValue > 0) {
                        const CAmount minPoWLockVal = IsAdamDualTrackActive(pindex->nHeight) ? POW_LOCK_REGISTRATION_AMOUNT : 10000;
                        if (entry.nValue < minPoWLockVal || !coinsView || !coinsView->HaveCoin(entry.outpoint)) continue;
                        uniqueKeys.insert(entry.pubkey);
                        fundedPoWKeys.insert(entry.pubkey);
                        fundedValidatorKeys.insert(entry.pubkey);
                    } else {
                        if (zeroCoinKeys.insert(entry.pubkey).second && zeroCoinKeysOrdered.size() < MAX_ZEROCOIN_BOOTSTRAP_MINERS) {
                            zeroCoinKeysOrdered.push_back(entry.pubkey);
                        }
                    }

                    if (pindexTip) {
                        int64_t remaining = entry.lockTime - pindexTip->nHeight;
                        if (remaining > 0 && (remaining == 240 || remaining == 100 || remaining == 20 || remaining == 5)) {
                            static std::set<std::pair<CKeyID, int64_t>> setWarnedPoWLock;
                            std::pair<CKeyID, int64_t> warnKey = std::make_pair(entry.pubkey.GetID(), remaining);
                            if (!setWarnedPoWLock.count(warnKey)) {
                                setWarnedPoWLock.insert(warnKey);
                                LogPrintf("ADAM WARNING: PoW-Lock miner registration for key %s is expiring in %d blocks (~%d minutes). Please renew!\n",
                                          entry.pubkey.GetID().ToString(), remaining, remaining * 30 / 60);
                            }
                        }
                    }
                }
            }
        }
    }

    for (const auto& k : masternodeKeys) {
        fundedPoWKeys.erase(k);
    }
    for (const auto& k : coinLockKeys) {
        fundedPoWKeys.erase(k);
    }

    const size_t nFundedValidatorCount = fundedValidatorKeys.size();
    const size_t maxBootstrapPoolSize = (size_t)(Params().GetConsensus().nAdamMinersCount + 2);
    const size_t maxZeroCoinCap = fHardeningV2 ? MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 : MAX_ZEROCOIN_BOOTSTRAP_MINERS;

    auto populateBootstrapCoinbaseKeys = [&]() {
        if (uniqueKeys.size() < maxBootstrapPoolSize) {
            int nBootstrapLimit = Params().GetConsensus().nAdamBootstrapLimit;
            if ((Params().NetworkIDString() == "main" || Params().NetworkIDString() == "test" || Params().NetworkIDString() == "regtest") && (!pindexTip || pindexTip->nHeight < nBootstrapLimit)) {
                uint256 genesisHash = chainActive.Genesis() ? chainActive.Genesis()->GetBlockHash() : UINT256_ZERO;
                if (genesisHash != cachedGenesisHash) {
                    cachedBootstrapKeys.clear();
                    cachedBootstrapScannedUpTo = 0;
                    cachedGenesisHash = genesisHash;
                }
                int nScanLimit = std::min(199, pindexTip ? pindexTip->nHeight : 0);
                for (int h = cachedBootstrapScannedUpTo + 1; h <= nScanLimit; ++h) {
                    CBlockIndex* pindex = chainActive[h];
                    if (!pindex) break;
                    CBlock block;
                    if (ReadBlockFromDisk(block, pindex)) {
                        ExtractBootstrapCoinbaseKey(block, cachedBootstrapKeys);
                        cachedBootstrapScannedUpTo = h;
                    }
                }
                for (const auto& k : cachedBootstrapKeys) {
                    if (uniqueKeys.size() >= maxBootstrapPoolSize) break;
                    if (uniqueKeys.insert(k).second) {
                        bootstrapKeys.insert(k);
                    }
                }
            }
        }
    };

    size_t nExplicitValidatorCount = 0;
    if (fHardeningV2) {
        // Under UPGRADE_HARDENING_V2:
        // 1. Preserve Block 1..199 real Classic PoW coinbase miners FIRST so Zero-Coin Sybil registrations cannot starve them out.
        // 2. Admit newest distinct Zero-Coin PoW keys (up to MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16) ONLY while nFundedValidatorCount < 13.
        populateBootstrapCoinbaseKeys();
        if (nFundedValidatorCount < maxBootstrapPoolSize) {
            size_t nAdmittedZeroCoin = 0;
            for (const auto& zKey : zeroCoinKeysOrdered) {
                if (fundedValidatorKeys.count(zKey) || bootstrapKeys.count(zKey)) continue;
                if (nAdmittedZeroCoin >= maxZeroCoinCap) break;
                if (uniqueKeys.insert(zKey).second) {
                    ++nAdmittedZeroCoin;
                }
            }
        }
        nExplicitValidatorCount = uniqueKeys.size() - bootstrapKeys.size();
    } else if (fHardeningV1) {
        // Fair Zero-Coin PoW admission: gated strictly on real funded validator count (< 13),
        // including all newest valid Zero-Coin registrations (up to MAX_ZEROCOIN_BOOTSTRAP_MINERS) without pubkey-order truncation.
        if (nFundedValidatorCount < maxBootstrapPoolSize) {
            size_t nAdmittedZeroCoin = 0;
            for (const auto& zKey : zeroCoinKeysOrdered) {
                if (nAdmittedZeroCoin >= maxZeroCoinCap) break;
                uniqueKeys.insert(zKey);
                ++nAdmittedZeroCoin;
            }
        }
        nExplicitValidatorCount = uniqueKeys.size();
        populateBootstrapCoinbaseKeys();
    } else {
        if (uniqueKeys.size() < maxBootstrapPoolSize) {
            for (const auto& zKey : zeroCoinKeys) {
                if (uniqueKeys.size() >= maxBootstrapPoolSize) break;
                uniqueKeys.insert(zKey);
            }
        }
        nExplicitValidatorCount = uniqueKeys.size();
        populateBootstrapCoinbaseKeys();
    }

    if (pMasternodeKeysOut) {
        *pMasternodeKeysOut = masternodeKeys;
    }
    if (pCoinLockKeysOut) {
        *pCoinLockKeysOut = coinLockKeys;
    }
    if (pFundedPoWKeysOut) {
        *pFundedPoWKeysOut = fundedPoWKeys;
    }
    if (pBootstrapKeysOut) {
        *pBootstrapKeysOut = bootstrapKeys;
    }
    if (pExplicitCountOut) {
        *pExplicitCountOut = nExplicitValidatorCount;
    }
    if (pFundedCountOut) {
        *pFundedCountOut = nFundedValidatorCount;
    }

    std::vector<CPubKey> resultPool;
    for (const auto& key : uniqueKeys) {
        resultPool.push_back(key);
    }
    if (fUsePoolCache && pindexTip && resultPool.size() >= (size_t)Params().GetConsensus().GetAdamThreshold(pindexTip->nHeight)) {
        mapMinerPoolCache[pindexTip->GetBlockHash()] = resultPool;
        mapMasternodePoolCache[pindexTip->GetBlockHash()] = masternodeKeys;
        mapCoinLockPoolCache[pindexTip->GetBlockHash()] = coinLockKeys;
        mapFundedPoWPoolCache[pindexTip->GetBlockHash()] = fundedPoWKeys;
        mapBootstrapPoolCache[pindexTip->GetBlockHash()] = bootstrapKeys;
        mapExplicitPoolCountCache[pindexTip->GetBlockHash()] = nExplicitValidatorCount;
        mapFundedPoolCountCache[pindexTip->GetBlockHash()] = nFundedValidatorCount;
    }
    return resultPool;
}

std::vector<CAdamValidatorPoolInfo> GetAdamMinerPoolDetailed(int nHeight) {
    LOCK(cs_main);
    LOCK(cs_miner_pool_cache);
    std::set<CPubKey> mnKeys;
    std::set<CPubKey> coinLockKeys;
    std::set<CPubKey> fundedPoWKeys;
    std::set<CPubKey> bootstrapKeys;
    std::vector<CPubKey> pool = GetAdamMinerPool(nHeight, nullptr, &mnKeys, &coinLockKeys, nullptr, nullptr, &fundedPoWKeys, &bootstrapKeys);

    CBlockIndex* pindexTip = (nHeight == -1)
        ? chainActive.Tip()
        : ((nHeight >= 0 && nHeight <= chainActive.Height()) ? chainActive[nHeight] : chainActive.Tip());
    std::map<CPubKey, int64_t> mapBestFundedUnlock;
    std::map<CPubKey, int64_t> mapBestZeroCoinUnlock;

    if (pindexTip) {
        const int nTipHeight = pindexTip->nHeight;
        const bool fHardeningV1 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, nTipHeight), Consensus::UPGRADE_HARDENING_V1);
        const bool fHardeningV2 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, nTipHeight), Consensus::UPGRADE_HARDENING_V2);
        const int nHardeningV2Height = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_HARDENING_V2].nActivationHeight;
        int nRegPeriod = 2880;
        if ((Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) &&
            IsModelDActive(nTipHeight + 1)) {
            nRegPeriod = 100;
        }
        const int nZeroActivePeriod = GetZeroCoinRegPeriod(nTipHeight);
        const int nLimit = std::max(0, nTipHeight - nRegPeriod);
        for (int h = nTipHeight; h > nLimit; --h) {
            CBlockIndex* pindex = pindexTip->GetAncestor(h);
            if (!pindex) continue;
            auto cacheIt = mapBlockRegCache.find(pindex->GetBlockHash());
            if (cacheIt == mapBlockRegCache.end()) continue;
            const bool fEntryV2 = Params().GetConsensus().NetworkUpgradeActive(std::max(0, pindex->nHeight), Consensus::UPGRADE_HARDENING_V2);
            for (const auto& entry : cacheIt->second) {
                if (fHardeningV1 && entry.lockTime >= LOCKTIME_THRESHOLD) continue;
                if (entry.isCoinLock) {
                    if (entry.lockTime < pindex->nHeight + nRegPeriod) continue;
                    if (!pcoinsTip || !pcoinsTip->HaveCoin(entry.outpoint)) continue;
                    mapBestFundedUnlock[entry.pubkey] = std::max(mapBestFundedUnlock[entry.pubkey], entry.lockTime);
                } else if (entry.nValue > 0) {
                    if (entry.lockTime < pindex->nHeight + nRegPeriod) continue;
                    const CAmount minPoWLockVal = IsAdamDualTrackActive(pindex->nHeight) ? POW_LOCK_REGISTRATION_AMOUNT : 10000;
                    if (entry.nValue < minPoWLockVal || !pcoinsTip || !pcoinsTip->HaveCoin(entry.outpoint)) continue;
                    mapBestFundedUnlock[entry.pubkey] = std::max(mapBestFundedUnlock[entry.pubkey], entry.lockTime);
                } else {
                    const int nRequiredPeriod = fEntryV2 ? GetZeroCoinRegPeriod(pindex->nHeight) : nRegPeriod;
                    if (entry.lockTime < pindex->nHeight + nRequiredPeriod) continue;
                    int64_t nEffectiveExpiry = entry.lockTime;
                    if (fHardeningV2) {
                        const int64_t nBaseHeight = fEntryV2
                            ? pindex->nHeight
                            : std::max(pindex->nHeight, std::max(0, nHardeningV2Height));
                        nEffectiveExpiry = std::min<int64_t>(entry.lockTime, nBaseHeight + nZeroActivePeriod);
                        if (nTipHeight >= nEffectiveExpiry) continue;
                    }
                    mapBestZeroCoinUnlock[entry.pubkey] = std::max(mapBestZeroCoinUnlock[entry.pubkey], nEffectiveExpiry);
                }
            }
        }
    }

    std::vector<CAdamValidatorPoolInfo> result;
    result.reserve(pool.size());
    for (const CPubKey& pk : pool) {
        CAdamValidatorPoolInfo info;
        info.pubkey = pk;
        if (mnKeys.count(pk)) {
            info.track = "masternode";
            info.nUnlockHeight = 0;
        } else if (coinLockKeys.count(pk)) {
            info.track = "coin_lock";
            info.nUnlockHeight = mapBestFundedUnlock[pk];
        } else if (fundedPoWKeys.count(pk)) {
            info.track = "pow_lock";
            info.nUnlockHeight = mapBestFundedUnlock[pk];
        } else if (bootstrapKeys.count(pk)) {
            info.track = "bootstrap";
            info.nUnlockHeight = 0;
        } else if (mapBestZeroCoinUnlock.count(pk)) {
            info.track = "zerocoin_pow";
            info.nUnlockHeight = mapBestZeroCoinUnlock[pk];
        } else {
            info.track = "bootstrap";
            info.nUnlockHeight = 0;
        }
        result.push_back(std::move(info));
    }
    return result;
}

size_t GetExplicitAdamMinerPoolCount(int nHeight, const CCoinsViewCache* pView) {
    size_t nExplicitCount = 0;
    GetAdamMinerPool(nHeight, pView, nullptr, nullptr, &nExplicitCount, nullptr);
    return nExplicitCount;
}

size_t GetFundedAdamMinerPoolCount(int nHeight, const CCoinsViewCache* pView) {
    size_t nFundedCount = 0;
    GetAdamMinerPool(nHeight, pView, nullptr, nullptr, nullptr, &nFundedCount);
    return nFundedCount;
}

int GetActiveMasternodePoolCount(int nHeight, const CCoinsViewCache* pView) {
    std::set<CPubKey> masternodeKeys;
    GetAdamMinerPool(nHeight, pView, &masternodeKeys, nullptr);
    return (int)masternodeKeys.size();
}

struct MinerRank {
    CPubKey key;
    uint256 hash;
    
    bool operator<(const MinerRank& other) const {
        return hash < other.hash;
    }
};

uint256 ComputeNextAdamSeed(const uint256& prevSeed, const std::vector<unsigned char>& vAdamVRFProof) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << prevSeed;
    if (!vAdamVRFProof.empty()) {
        try {
            CDataStream ssProof(vAdamVRFProof, SER_NETWORK, PROTOCOL_VERSION);
            CBLSSignedData signedData;
            ssProof >> signedData;
            if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
                ss.write((const char*)signedData.blsPubKey.begin(), 48);
                ss.write((const char*)signedData.blsSig.begin(), 96);
                return ss.GetHash();
            }
        } catch (...) {}
    }
    ss << vAdamVRFProof;
    return ss.GetHash();
}

uint256 GetAdamSeed(const CBlockIndex* pindex) {
    if (pindex == nullptr) return uint256();
    
    LOCK(cs_main);
    const Consensus::Params& consensus = Params().GetConsensus();
    if (!IsAdamActive(pindex->nHeight, consensus)) {
        return pindex->GetBlockHash();
    }
    
    std::vector<const CBlockIndex*> path;
    const CBlockIndex* curr = pindex;
    uint256 seed;
    bool foundCached = false;
    
    while (curr != nullptr) {
        if (!IsAdamActive(curr->nHeight, consensus)) {
            seed = curr->GetBlockHash();
            foundCached = true;
            break;
        }
        
        {
            LOCK(cs_adam_seeds);
            auto it = mapAdamSeeds.find(curr->GetBlockHash());
            if (it != mapAdamSeeds.end()) {
                seed = it->second;
                foundCached = true;
                break;
            }
        }
        
        path.push_back(curr);
        curr = curr->pprev;
    }
    
    if (!foundCached) {
        return uint256();
    }
    
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const CBlockIndex* pindexCurr = *it;
        CBlock block;
        bool gotBlock = false;
        if (ReadBlockFromDisk(block, pindexCurr)) {
            gotBlock = true;
        } else {
            LOCK(cs_recent_vrf_proofs);
            auto itVRF = mapRecentVRFProofs.find(pindexCurr->GetBlockHash());
            if (itVRF != mapRecentVRFProofs.end()) {
                block.vAdamVRFProof = itVRF->second;
                gotBlock = true;
            }
        }

        if (!gotBlock) {
            LogPrintf("GetAdamSeed: Failed to read block from disk or cache at height %d\n", pindexCurr->nHeight);
            return uint256();
        }

        seed = ComputeNextAdamSeed(seed, block.vAdamVRFProof);
        
        {
            LOCK(cs_adam_seeds);
            mapAdamSeeds[pindexCurr->GetBlockHash()] = seed;
            mapSeedToBlockHash[seed] = pindexCurr->GetBlockHash();
        }
    }
    
    return seed;
}


void ElectDualTrackValidators(
    const uint256& hashAdamSeed,
    const std::vector<CPubKey>& pool,
    const std::set<CPubKey>& primaryColKeys,
    const CPubKey& coordinatorKey,
    int minCount,
    std::vector<CPubKey>& vSelectedMinersOut,
    const std::set<CPubKey>& secondaryColKeys,
    std::vector<CPubKey>* pvStandbyMinersOut,
    const std::set<CPubKey>* pFundedPoWKeys,
    const std::set<CPubKey>* pBootstrapKeys)
{
    std::vector<MinerRank> rankedPool;
    rankedPool.reserve(pool.size());
    for (const auto& key : pool) {
        if (coordinatorKey.IsValid() && key == coordinatorKey) {
            continue;
        }
        CHashWriter ss(SER_GETHASH, 0);
        ss << hashAdamSeed;
        ss << key;
        MinerRank rank;
        rank.key = key;
        rank.hash = ss.GetHash();
        rankedPool.push_back(rank);
    }
    std::sort(rankedPool.begin(), rankedPool.end());

    int electCount = std::min(minCount, (int)rankedPool.size());
    vSelectedMinersOut.clear();
    if (pvStandbyMinersOut) {
        pvStandbyMinersOut->clear();
    }
    if (electCount <= 0) {
        return;
    }

    const bool fTieredV2 = (pFundedPoWKeys != nullptr || pBootstrapKeys != nullptr);
    std::vector<MinerRank> vPrimaryColRanked;
    std::vector<MinerRank> vSecondaryColRanked;
    std::vector<MinerRank> vFundedPoWRanked;
    std::vector<MinerRank> vBootstrapRanked;
    std::vector<MinerRank> vZeroCoinRanked;
    std::vector<MinerRank> vOpenPowRanked;
    vPrimaryColRanked.reserve(rankedPool.size());
    vSecondaryColRanked.reserve(rankedPool.size());
    vOpenPowRanked.reserve(rankedPool.size());
    if (fTieredV2) {
        vFundedPoWRanked.reserve(rankedPool.size());
        vBootstrapRanked.reserve(rankedPool.size());
        vZeroCoinRanked.reserve(rankedPool.size());
        for (const auto& r : rankedPool) {
            if (primaryColKeys.count(r.key) > 0) {
                vPrimaryColRanked.push_back(r);
            } else if (secondaryColKeys.count(r.key) > 0) {
                vSecondaryColRanked.push_back(r);
            } else if (pFundedPoWKeys && pFundedPoWKeys->count(r.key) > 0) {
                vFundedPoWRanked.push_back(r);
            } else if (pBootstrapKeys && pBootstrapKeys->count(r.key) > 0) {
                vBootstrapRanked.push_back(r);
            } else {
                vZeroCoinRanked.push_back(r);
            }
        }
    } else {
        for (const auto& r : rankedPool) {
            if (primaryColKeys.count(r.key) > 0) {
                vPrimaryColRanked.push_back(r);
            } else if (secondaryColKeys.count(r.key) > 0) {
                vSecondaryColRanked.push_back(r);
            } else {
                vOpenPowRanked.push_back(r);
            }
        }
    }

    int nTargetTrack1 = std::min(ADAM_COLLATERALIZED_TRACK_QUOTA, electCount);
    int nTargetPrimary = std::min(ADAM_MASTERNODE_TRACK_QUOTA, nTargetTrack1);
    int nTargetSecondary = std::max(0, nTargetTrack1 - nTargetPrimary);
    int nTargetTrack2 = std::max(0, electCount - nTargetTrack1);

    // Track 1 (8 Collateralized seats):
    // - Sub-track 1A (7 seats): Active Masternodes (4,200 KRISTA)
    // - Sub-track 1B (1 seat):  Priority Coin-Lock validator (250 KRISTA)
    // Immediate intra-Track-1 spillover between Sub-track 1A and Sub-track 1B:
    int nTakePrimary = std::min((int)vPrimaryColRanked.size(), nTargetPrimary);
    int nTakeSecondary = std::min((int)vSecondaryColRanked.size(), nTargetSecondary);

    int nTrack1Rem = nTargetTrack1 - (nTakePrimary + nTakeSecondary);
    if (nTrack1Rem > 0) {
        int nExtraPrimary = (int)vPrimaryColRanked.size() - nTakePrimary;
        if (nExtraPrimary > 0) {
            int nAdd = std::min(nTrack1Rem, nExtraPrimary);
            nTakePrimary += nAdd;
            nTrack1Rem -= nAdd;
        }
    }
    if (nTrack1Rem > 0) {
        int nExtraSecondary = (int)vSecondaryColRanked.size() - nTakeSecondary;
        if (nExtraSecondary > 0) {
            int nAdd = std::min(nTrack1Rem, nExtraSecondary);
            nTakeSecondary += nAdd;
            nTrack1Rem -= nAdd;
        }
    }

    if (fTieredV2) {
        // Under UPGRADE_HARDENING_V2, unselected 250 KRISTA Coin-Lock validators overflow into Tier 2A
        // to compete by VRF alongside 10 KRISTA Funded PoW-Lock validators (eliminating Track Crowding),
        // strictly ahead of Tier 2B (Block 1..199 Bootstrap) and Tier 2C (0 KRISTA Zero-Coin PoW).
        std::vector<MinerRank> vTier2AFunded;
        vTier2AFunded.reserve((vSecondaryColRanked.size() - (size_t)nTakeSecondary) + vFundedPoWRanked.size());
        for (size_t i = (size_t)nTakeSecondary; i < vSecondaryColRanked.size(); ++i) {
            vTier2AFunded.push_back(vSecondaryColRanked[i]);
        }
        vTier2AFunded.insert(vTier2AFunded.end(), vFundedPoWRanked.begin(), vFundedPoWRanked.end());
        std::sort(vTier2AFunded.begin(), vTier2AFunded.end());

        vOpenPowRanked.insert(vOpenPowRanked.end(), vTier2AFunded.begin(), vTier2AFunded.end());
        vOpenPowRanked.insert(vOpenPowRanked.end(), vBootstrapRanked.begin(), vBootstrapRanked.end());
        vOpenPowRanked.insert(vOpenPowRanked.end(), vZeroCoinRanked.begin(), vZeroCoinRanked.end());
    }

    // Track 2 (3 Open PoW / Independent VRF seats)
    int nTakeOpenPoW = std::min((int)vOpenPowRanked.size(), nTargetTrack2);

    // Inter-track bidirectional dynamic spillover so all electCount seats are always filled
    int nRemainingSeats = electCount - (nTakePrimary + nTakeSecondary + nTakeOpenPoW);
    if (nRemainingSeats > 0) {
        int nExtraPrimaryAvail = (int)vPrimaryColRanked.size() - nTakePrimary;
        if (nExtraPrimaryAvail > 0) {
            int nAddPrimary = std::min(nRemainingSeats, nExtraPrimaryAvail);
            nTakePrimary += nAddPrimary;
            nRemainingSeats -= nAddPrimary;
        }
    }
    if (!fTieredV2 && nRemainingSeats > 0) {
        int nExtraSecondaryAvail = (int)vSecondaryColRanked.size() - nTakeSecondary;
        if (nExtraSecondaryAvail > 0) {
            int nAddSecondary = std::min(nRemainingSeats, nExtraSecondaryAvail);
            nTakeSecondary += nAddSecondary;
            nRemainingSeats -= nAddSecondary;
        }
    }
    if (nRemainingSeats > 0) {
        int nExtraPoWAvail = (int)vOpenPowRanked.size() - nTakeOpenPoW;
        if (nExtraPoWAvail > 0) {
            int nAddPoW = std::min(nRemainingSeats, nExtraPoWAvail);
            nTakeOpenPoW += nAddPoW;
            nRemainingSeats -= nAddPoW;
        }
    }

    std::vector<MinerRank> vElectedRanks;
    vElectedRanks.reserve(nTakePrimary + nTakeSecondary + nTakeOpenPoW);
    for (int i = 0; i < nTakePrimary; ++i) {
        vElectedRanks.push_back(vPrimaryColRanked[i]);
    }
    for (int i = 0; i < nTakeSecondary; ++i) {
        vElectedRanks.push_back(vSecondaryColRanked[i]);
    }
    for (int i = 0; i < nTakeOpenPoW; ++i) {
        vElectedRanks.push_back(vOpenPowRanked[i]);
    }
    std::sort(vElectedRanks.begin(), vElectedRanks.end());

    vSelectedMinersOut.reserve(vElectedRanks.size());
    for (const auto& r : vElectedRanks) {
        vSelectedMinersOut.push_back(r.key);
    }

    if (pvStandbyMinersOut) {
        pvStandbyMinersOut->reserve(rankedPool.size() - vSelectedMinersOut.size());
        for (size_t i = (size_t)nTakePrimary; i < vPrimaryColRanked.size(); ++i) {
            pvStandbyMinersOut->push_back(vPrimaryColRanked[i].key);
        }
        if (!fTieredV2) {
            for (size_t i = (size_t)nTakeSecondary; i < vSecondaryColRanked.size(); ++i) {
                pvStandbyMinersOut->push_back(vSecondaryColRanked[i].key);
            }
        }
        for (size_t i = (size_t)nTakeOpenPoW; i < vOpenPowRanked.size(); ++i) {
            pvStandbyMinersOut->push_back(vOpenPowRanked[i].key);
        }
    }
}

bool SelectAdamNodes(
    const uint256& hashAdamSeed,
    const Consensus::Params& params,
    std::vector<CPubKey>& vSelectedMinersOut,
    CPubKey& coordinatorOut,
    int nBlockHeight,
    const CCoinsViewCache* pView,
    std::vector<CPubKey>* pvStandbyMinersOut)
{
    int nHeight = (nBlockHeight > 0) ? (nBlockHeight - 1) : -1;
    if (nHeight < 0) {
        LOCK(cs_adam_seeds);
        auto it = mapSeedToBlockHash.find(hashAdamSeed);
        if (it != mapSeedToBlockHash.end()) {
            uint256 blockHash = it->second;
            if (mapBlockIndex.count(blockHash)) {
                nHeight = mapBlockIndex[blockHash]->nHeight;
            }
        }
    }
    std::set<CPubKey> collateralizedKeys;
    std::set<CPubKey> coinLockKeys;
    std::set<CPubKey> fundedPoWKeys;
    std::set<CPubKey> bootstrapKeys;
    std::vector<CPubKey> pool = GetAdamMinerPool(nHeight, pView, &collateralizedKeys, &coinLockKeys, nullptr, nullptr, &fundedPoWKeys, &bootstrapKeys);
    int minCount = params.nAdamMinersCount;
    CBlockIndex* pindexTip = nullptr;
    {
        LOCK(cs_main);
        pindexTip = chainActive.Tip();
    }
    int effectiveBlockHeight = (nBlockHeight > 0) ? nBlockHeight : ((nHeight >= 0) ? (nHeight + 1) : (pindexTip ? pindexTip->nHeight + 1 : 0));
    if (effectiveBlockHeight > 0) {
        if (!params.NetworkUpgradeActive(effectiveBlockHeight, Consensus::UPGRADE_POMBL)) {
            minCount = 11;
        }
    } else {
        minCount = 11;
    }
    const bool fDualTrack = IsAdamDualTrackActive(effectiveBlockHeight);
    const bool fHardeningV2 = params.NetworkUpgradeActive(std::max(0, effectiveBlockHeight), Consensus::UPGRADE_HARDENING_V2);
    int threshold = params.GetAdamThreshold(nHeight >= 0 ? nHeight : (pindexTip ? pindexTip->nHeight : 0));
    if (pool.size() < (size_t)threshold) {
        return false;
    }
    
    // Get active masternodes list to evaluate Rule 1 vs Rule 2
    std::vector<CPubKey> vMns;
    if (!Params().IsRegTestNet()) {
        LOCK(cs_main);
        int currentHeight = (nHeight >= 0 ? nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0));
        std::set<COutPoint> seenCollateralOutpoints;
        for (const auto& kv : mapMasternodeCollateralOutpoints) {
            if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, currentHeight, 2880)) {
                if (!seenCollateralOutpoints.count(kv.second) &&
                    IsValidMasternodeCollateralAndAuth(kv.first, kv.second, currentHeight, nullptr, pView)) {
                    vMns.push_back(kv.first);
                    seenCollateralOutpoints.insert(kv.second);
                }
            }
        }
        std::sort(vMns.begin(), vMns.end());
        vMns.erase(std::unique(vMns.begin(), vMns.end()), vMns.end());
    }

    static RecursiveMutex cs_select_cache;
    static uint256 cachedSeed;
    static int cachedMinCount = -1;
    static int cachedThreshold = -1;
    static bool cachedDualTrack = false;
    static bool cachedHardeningV2 = false;
    static std::vector<CPubKey> cachedPool;
    static std::set<CPubKey> cachedCollateralized;
    static std::set<CPubKey> cachedCoinLocks;
    static std::set<CPubKey> cachedFundedPoW;
    static std::set<CPubKey> cachedBootstrap;
    static std::vector<CPubKey> cachedMns;
    static std::vector<CPubKey> cachedSelectedMiners;
    static std::vector<CPubKey> cachedStandbyMiners;
    static CPubKey cachedCoordinator;
    {
        LOCK(cs_select_cache);
        if (!cachedSeed.IsNull() &&
            cachedSeed == hashAdamSeed &&
            cachedMinCount == minCount &&
            cachedThreshold == threshold &&
            cachedDualTrack == fDualTrack &&
            cachedHardeningV2 == fHardeningV2 &&
            cachedPool == pool &&
            cachedCollateralized == collateralizedKeys &&
            cachedCoinLocks == coinLockKeys &&
            cachedFundedPoW == fundedPoWKeys &&
            cachedBootstrap == bootstrapKeys &&
            cachedMns == vMns) {
            vSelectedMinersOut = cachedSelectedMiners;
            coordinatorOut = cachedCoordinator;
            if (pvStandbyMinersOut) {
                *pvStandbyMinersOut = cachedStandbyMiners;
            }
            return true;
        }
    }
    
    // Rank all keys in the combined pool by hashing seed + key
    std::vector<MinerRank> rankedPool;
    rankedPool.reserve(pool.size());
    for (const auto& key : pool) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << hashAdamSeed;
        ss << key;
        MinerRank rank;
        rank.key = key;
        rank.hash = ss.GetHash();
        rankedPool.push_back(rank);
    }
    std::sort(rankedPool.begin(), rankedPool.end());

    std::vector<CPubKey> vStandbyMiners;

    std::set<CPubKey> activeMnSet(collateralizedKeys.begin(), collateralizedKeys.end());
    activeMnSet.insert(vMns.begin(), vMns.end());
    const bool hasTieredValidators = fHardeningV2 && fDualTrack &&
        (!activeMnSet.empty() || !coinLockKeys.empty() || !fundedPoWKeys.empty() || !bootstrapKeys.empty());

    if (hasTieredValidators) {
        // Under UPGRADE_HARDENING_V2 + Dual-Track, elect Coordinator smoothly across ALL funded tiers
        // using Sybil-immune stake-weighted deterministic VRF (Active MN = 4200, Coin-Lock = 250, Funded PoW-Lock = 10),
        // eliminating the 7-MN exclusivity cliff while preserving 100% splitting-attack invariance.
        std::vector<std::pair<CPubKey, uint64_t>> weightedCandidates;
        weightedCandidates.reserve(pool.size());
        uint64_t totalFundedWeight = 0;
        for (const auto& key : pool) {
            uint64_t w = 0;
            if (activeMnSet.count(key)) {
                w = 4200;
            } else if (coinLockKeys.count(key)) {
                w = 250;
            } else if (fundedPoWKeys.count(key)) {
                w = 10;
            }
            if (w > 0) {
                weightedCandidates.push_back(std::make_pair(key, w));
                totalFundedWeight += w;
            }
        }

        coordinatorOut = CPubKey();
        if (totalFundedWeight > 0) {
            CHashWriter ssCoord(SER_GETHASH, 0);
            ssCoord << hashAdamSeed;
            ssCoord << std::string("ADAM_WEIGHTED_COORD_V2");
            const arith_uint256 hCoord = UintToArith256(ssCoord.GetHash());
            const arith_uint256 wTotal(totalFundedWeight);
            const uint64_t targetTicket = (hCoord - (hCoord / wTotal) * wTotal).GetLow64();
            uint64_t cumWeight = 0;
            for (const auto& wc : weightedCandidates) {
                cumWeight += wc.second;
                if (targetTicket < cumWeight) {
                    coordinatorOut = wc.first;
                    break;
                }
            }
        }
        if (!coordinatorOut.IsValid()) {
            for (const auto& r : rankedPool) {
                if (bootstrapKeys.count(r.key)) {
                    coordinatorOut = r.key;
                    break;
                }
            }
        }
        if (!coordinatorOut.IsValid()) {
            coordinatorOut = rankedPool[0].key;
        }
        const CPubKey excludedCoord = (pool.size() > (size_t)minCount) ? coordinatorOut : CPubKey();
        ElectDualTrackValidators(hashAdamSeed, pool, activeMnSet, excludedCoord, minCount, vSelectedMinersOut, coinLockKeys, &vStandbyMiners, &fundedPoWKeys, &bootstrapKeys);
    } else if ((int)vMns.size() > threshold) {
        // Legacy Rule 1: Active Masternodes count > Threshold.
        std::vector<MinerRank> rankedMns;
        rankedMns.reserve(vMns.size());
        for (const auto& key : vMns) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << hashAdamSeed;
            ss << key;
            MinerRank rank;
            rank.key = key;
            rank.hash = ss.GetHash();
            rankedMns.push_back(rank);
        }
        std::sort(rankedMns.begin(), rankedMns.end());
        coordinatorOut = rankedMns[0].key;

        if (fDualTrack) {
            ElectDualTrackValidators(hashAdamSeed, pool, collateralizedKeys, coordinatorOut, minCount, vSelectedMinersOut, coinLockKeys, &vStandbyMiners,
                                     fHardeningV2 ? &fundedPoWKeys : nullptr,
                                     fHardeningV2 ? &bootstrapKeys : nullptr);
        } else {
            std::vector<MinerRank> remainingPool;
            remainingPool.reserve(rankedPool.size());
            for (const auto& r : rankedPool) {
                if (r.key != coordinatorOut) {
                    remainingPool.push_back(r);
                }
            }
            vSelectedMinersOut.clear();
            int electCount = std::min(minCount, (int)remainingPool.size());
            for (int i = 0; i < electCount; ++i) {
                vSelectedMinersOut.push_back(remainingPool[i].key);
            }
            for (size_t i = (size_t)electCount; i < remainingPool.size(); ++i) {
                vStandbyMiners.push_back(remainingPool[i].key);
            }
        }
    } else {
        // Untiered / Regtest synthetic fallback when no tiered validators exist
        if (fDualTrack) {
            ElectDualTrackValidators(hashAdamSeed, pool, collateralizedKeys, CPubKey(), minCount, vSelectedMinersOut, coinLockKeys, &vStandbyMiners,
                                     fHardeningV2 ? &fundedPoWKeys : nullptr,
                                     fHardeningV2 ? &bootstrapKeys : nullptr);
            std::set<CPubKey> electedSet(vSelectedMinersOut.begin(), vSelectedMinersOut.end());
            coordinatorOut = rankedPool[0].key;
            for (const auto& r : rankedPool) {
                if (!electedSet.count(r.key)) {
                    coordinatorOut = r.key;
                    break;
                }
            }
            vStandbyMiners.erase(std::remove(vStandbyMiners.begin(), vStandbyMiners.end(), coordinatorOut), vStandbyMiners.end());
        } else {
            vSelectedMinersOut.clear();
            int electCount = std::min(minCount, (int)rankedPool.size());
            for (int i = 0; i < electCount; ++i) {
                vSelectedMinersOut.push_back(rankedPool[i].key);
            }
            if ((int)rankedPool.size() > electCount) {
                coordinatorOut = rankedPool[electCount].key;
                for (size_t i = (size_t)electCount + 1; i < rankedPool.size(); ++i) {
                    vStandbyMiners.push_back(rankedPool[i].key);
                }
            } else {
                coordinatorOut = rankedPool[0].key;
            }
        }
    }

    if (pvStandbyMinersOut) {
        *pvStandbyMinersOut = vStandbyMiners;
    }

    {
        LOCK(cs_select_cache);
        cachedSeed = hashAdamSeed;
        cachedMinCount = minCount;
        cachedThreshold = threshold;
        cachedDualTrack = fDualTrack;
        cachedHardeningV2 = fHardeningV2;
        cachedPool = std::move(pool);
        cachedCollateralized = std::move(collateralizedKeys);
        cachedCoinLocks = std::move(coinLockKeys);
        cachedFundedPoW = std::move(fundedPoWKeys);
        cachedBootstrap = std::move(bootstrapKeys);
        cachedMns = std::move(vMns);
        cachedSelectedMiners = vSelectedMinersOut;
        cachedStandbyMiners = std::move(vStandbyMiners);
        cachedCoordinator = coordinatorOut;
    }
    
    return true;
}

int GetAdamPuzzleAlgo(const uint256& hashAdamSeed, const CPubKey& minerKey, bool fFallbackMode) {
    if (fFallbackMode) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << hashAdamSeed;
        ss << minerKey;
        uint256 h = ss.GetHash();
        arith_uint256 tmp = UintToArith256(h);
        return (tmp - (tmp / 18) * 18).GetLow64();
    }
    std::vector<CPubKey> vExpectedMiners;
    CPubKey expectedCoordinator;
    const Consensus::Params& params = Params().GetConsensus();
    if (!SelectAdamNodes(hashAdamSeed, params, vExpectedMiners, expectedCoordinator)) {
        return 12; // Fallback to DoubleSHA256
    }
    
    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
        if (vExpectedMiners[i] == minerKey) {
            return i % 13;
        }
    }
    return 12; // Fallback to DoubleSHA256
}

std::string GetAdamPuzzleAlgoName(int algoIndex) {
    switch (algoIndex) {
        case 0: return "blake";
        case 1: return "bmw";
        case 2: return "groestl";
        case 3: return "jh";
        case 4: return "keccak";
        case 5: return "skein";
        case 6: return "luffa";
        case 7: return "cubehash";
        case 8: return "shavite";
        case 9: return "simd";
        case 10: return "echo";
        case 11: return "X11KVS";
        case 12: return "DoubleSHA256";
        case 13: return "hamsi";
        case 14: return "fugue";
        case 15: return "shabal";
        case 16: return "whirlpool";
        case 17: return "haval";
        default: return "DoubleSHA256";
    }
}


bool VerifyAdamSolution(const uint256& hashPrevBlock, const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion, int nHeight, int knownMinerIdx) {
    if (vchSolution.empty()) {
        return false;
    }
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        
        unsigned char inputBuf[104];
        const unsigned int keyLen = minerKey.size();
        memcpy(inputBuf, hashAdamSeed.begin(), 32);
        inputBuf[32] = static_cast<unsigned char>(keyLen);
        if (keyLen > 0) {
            memcpy(inputBuf + 33, minerKey.begin(), keyLen);
        }
        WriteLE32(inputBuf + 33 + keyLen, nNonce);
        const size_t inputLen = 37 + keyLen;
        
        uint256 puzzleHash;
        bool verifyRes = false;
        if (nVersion == 11) {
            int algoIndex = GetAdamPuzzleAlgo(hashAdamSeed, minerKey, true);
            puzzleHash = CalculateAdamPuzzleHash(algoIndex, inputBuf, inputBuf + inputLen);
            verifyRes = VerifyBLSWithECDSAFallback(puzzleHash, minerKey, vchSig);
        } else if (nVersion >= 12) {
            auto computeV12PuzzleHash = [&](int idx) -> uint256 {
                int algo1 = -1, algo2 = -1, algo3 = -1;
                GetAdam3PermutationAlgos(hashPrevBlock, minerKey, algo1, algo2, algo3);
                uint256 hash3 = CalculateAdamPuzzleHash(algo3, inputBuf, inputBuf + inputLen);
                int i_factor = idx + 1;
                arith_uint256 val1 = UintToArith256(hash3) * i_factor;
                uint256 multiplied1 = ArithToUint256(val1);
                uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
                arith_uint256 val2 = UintToArith256(hash2) * i_factor;
                uint256 multiplied2 = ArithToUint256(val2);
                return CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
            };

            int minerIdx = knownMinerIdx;
            if (minerIdx >= 0) {
                puzzleHash = computeV12PuzzleHash(minerIdx);
                verifyRes = VerifyBLSWithECDSAFallback(puzzleHash, minerKey, vchSig);
            }
            if (!verifyRes) {
                std::vector<CPubKey> vExpectedMiners;
                std::vector<CPubKey> vStandbyMiners;
                CPubKey expectedCoordinator;
                int poolIdx = -1;
                if (SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator, nHeight, nullptr, &vStandbyMiners)) {
                    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                        if (vExpectedMiners[i] == minerKey) {
                            poolIdx = (int)i;
                            break;
                        }
                    }
                    if (poolIdx < 0) {
                        for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
                            if (vStandbyMiners[s] == minerKey) {
                                poolIdx = (int)vExpectedMiners.size() + (int)s;
                                break;
                            }
                        }
                    }
                }
                if (poolIdx >= 0 && poolIdx != minerIdx) {
                    puzzleHash = computeV12PuzzleHash(poolIdx);
                    verifyRes = VerifyBLSWithECDSAFallback(puzzleHash, minerKey, vchSig);
                }
            }
        } else {
            puzzleHash = CalculateAdamPuzzleHash(12, inputBuf, inputBuf + inputLen);
            verifyRes = VerifyBLSWithECDSAFallback(puzzleHash, minerKey, vchSig);
        }
        
        if (!verifyRes) {
            return false;
        }
        
        // Verify the difficulty
        if (!Params().IsRegTestNet()) {
            bool fNegative;
            bool fOverflow;
            uint256 bnTarget;
            bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
            if (fNegative || bnTarget.IsNull() || fOverflow) return false;

            int height = nHeight;
            if (height == -1) {
                LOCK(cs_main);
                BlockMap::iterator mi = mapBlockIndex.find(hashPrevBlock);
                if (mi != mapBlockIndex.end()) {
                    height = mi->second->nHeight + 1;
                } else {
                    height = chainActive.Height() + 1;
                }
            }
            const auto& consensusParams = Params().GetConsensus();
            bool fFallbackMode = (nVersion == 11) || !consensusParams.NetworkUpgradeActive(height, Consensus::UPGRADE_MODELD);
            int shift = (consensusParams.NetworkUpgradeActive(height, Consensus::UPGRADE_ADAM_V2) && !fFallbackMode) ? 
                        consensusParams.nAdamDifficultyShiftV2 : consensusParams.nAdamDifficultyShiftV1;
            uint256 scaledTarget = bnTarget << shift;
            uint256 powLimit = Params().GetConsensus().powLimit;
            if (scaledTarget > powLimit || scaledTarget < bnTarget) {
                scaledTarget = powLimit;
            }

            if (puzzleHash > scaledTarget) {
                return false;
            }
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void ResolveAdamCommitteeForTime(
    const uint256& hashPrevBlock,
    const uint256& hashAdamSeed,
    int nHeight,
    int64_t nBlockTimeDiff,
    unsigned int nBits,
    int nVersion,
    const std::vector<CPubKey>& vExpectedMiners,
    const std::vector<CPubKey>& vStandbyMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions,
    std::vector<CPubKey>& vEffectiveMinersOut,
    std::vector<std::vector<unsigned char>>& vEffectiveSolutionsOut,
    int& nValidSolutionsOut,
    const std::set<CPubKey>* pPrimaryColKeys,
    const std::set<CPubKey>* pSecondaryColKeys,
    const CCoinsViewCache* pView)
{
    vEffectiveMinersOut = vExpectedMiners;
    vEffectiveSolutionsOut.assign(vExpectedMiners.size(), std::vector<unsigned char>());
    nValidSolutionsOut = 0;

    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
        const auto& minerKey = vExpectedMiners[i];
        auto solIt = mapSolutions.find(minerKey);
        if (solIt != mapSolutions.end() && !solIt->second.empty() &&
            VerifyAdamSolution(hashPrevBlock, hashAdamSeed, minerKey, solIt->second, nBits, nVersion, nHeight, (int)i)) {
            vEffectiveSolutionsOut[i] = solIt->second;
            nValidSolutionsOut++;
        }
    }

    if (nBlockTimeDiff >= ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS && !vStandbyMiners.empty()) {
        std::set<CPubKey> localMnKeys;
        std::set<CPubKey> localCoinLockKeys;
        const bool fDualTrack = IsAdamDualTrackActive(nHeight);
        if (fDualTrack && (!pPrimaryColKeys || !pSecondaryColKeys)) {
            int nPoolHeight = (nHeight > 0) ? (nHeight - 1) : -1;
            GetAdamMinerPool(nPoolHeight, pView, &localMnKeys, &localCoinLockKeys);
            if (!pPrimaryColKeys) pPrimaryColKeys = &localMnKeys;
            if (!pSecondaryColKeys) pSecondaryColKeys = &localCoinLockKeys;
        }

        auto getValidatorTrack = [&](const CPubKey& k) -> int {
            if (!fDualTrack) return 0;
            if (pPrimaryColKeys && pPrimaryColKeys->count(k) > 0) return 0;
            if (pSecondaryColKeys && pSecondaryColKeys->count(k) > 0) return 1;
            return 2;
        };

        std::vector<std::pair<CPubKey, int>> vTrackStandby[3];
        for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
            int t = getValidatorTrack(vStandbyMiners[s]);
            vTrackStandby[t].push_back({vStandbyMiners[s], (int)s});
        }

        int nSlot = 1 + (int)((nBlockTimeDiff - ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS) / ADAM_COMMITTEE_FALLBACK_SLOT_SECS);
        int prevStandbyIdxInTrack[3] = {-1, -1, -1};
        int nReplacedInTrack[3] = {0, 0, 0};

        for (size_t i = 0; i < vEffectiveMinersOut.size(); ++i) {
            if (!vEffectiveSolutionsOut[i].empty()) {
                continue;
            }
            int t = getValidatorTrack(vExpectedMiners[i]);
            size_t maxTrackIdx = std::min(vTrackStandby[t].size(), (size_t)(nReplacedInTrack[t] + nSlot));
            for (size_t k = (size_t)(prevStandbyIdxInTrack[t] + 1); k < maxTrackIdx; ++k) {
                const CPubKey& standbyKey = vTrackStandby[t][k].first;
                int standbyPoolIdx = (int)vExpectedMiners.size() + vTrackStandby[t][k].second;
                auto solIt = mapSolutions.find(standbyKey);
                if (solIt != mapSolutions.end() && !solIt->second.empty() &&
                    (VerifyAdamSolution(hashPrevBlock, hashAdamSeed, standbyKey, solIt->second, nBits, nVersion, nHeight, (int)i) ||
                     VerifyAdamSolution(hashPrevBlock, hashAdamSeed, standbyKey, solIt->second, nBits, nVersion, nHeight, standbyPoolIdx))) {
                    vEffectiveMinersOut[i] = standbyKey;
                    vEffectiveSolutionsOut[i] = solIt->second;
                    nValidSolutionsOut++;
                    prevStandbyIdxInTrack[t] = (int)k;
                    nReplacedInTrack[t]++;
                    break;
                }
            }
        }
    }
}

bool ValidateAdamCommitteeForTime(
    const uint256& hashPrevBlock,
    const uint256& hashAdamSeed,
    int nHeight,
    int64_t nBlockTimeDiff,
    unsigned int nBits,
    int nVersion,
    const std::vector<CPubKey>& vBlockMiners,
    const std::vector<std::vector<unsigned char>>& vBlockSolutions,
    const std::vector<CPubKey>& vExpectedMiners,
    const std::vector<CPubKey>& vStandbyMiners,
    const std::set<CPubKey>* pPrimaryColKeys,
    const std::set<CPubKey>* pSecondaryColKeys,
    const CCoinsViewCache* pView)
{
    if (vBlockMiners.size() != vExpectedMiners.size() || vBlockSolutions.size() != vBlockMiners.size()) {
        return false;
    }
    if (vBlockMiners == vExpectedMiners) {
        return true;
    }
    if (nBlockTimeDiff < ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS || vStandbyMiners.empty()) {
        return false;
    }

    std::set<CPubKey> localMnKeys;
    std::set<CPubKey> localCoinLockKeys;
    const bool fDualTrack = IsAdamDualTrackActive(nHeight);
    if (fDualTrack && (!pPrimaryColKeys || !pSecondaryColKeys)) {
        int nPoolHeight = (nHeight > 0) ? (nHeight - 1) : -1;
        GetAdamMinerPool(nPoolHeight, pView, &localMnKeys, &localCoinLockKeys);
        if (!pPrimaryColKeys) pPrimaryColKeys = &localMnKeys;
        if (!pSecondaryColKeys) pSecondaryColKeys = &localCoinLockKeys;
    }

    auto getValidatorTrack = [&](const CPubKey& k) -> int {
        if (!fDualTrack) return 0;
        if (pPrimaryColKeys && pPrimaryColKeys->count(k) > 0) return 0;
        if (pSecondaryColKeys && pSecondaryColKeys->count(k) > 0) return 1;
        return 2;
    };

    std::vector<std::pair<CPubKey, int>> vTrackStandby[3];
    for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
        int t = getValidatorTrack(vStandbyMiners[s]);
        vTrackStandby[t].push_back({vStandbyMiners[s], (int)s});
    }

    int nSlot = 1 + (int)((nBlockTimeDiff - ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS) / ADAM_COMMITTEE_FALLBACK_SLOT_SECS);
    int prevStandbyIdxInTrack[3] = {-1, -1, -1};
    int nReplacedInTrack[3] = {0, 0, 0};

    std::set<CPubKey> seenKeys;
    for (size_t i = 0; i < vBlockMiners.size(); ++i) {
        const CPubKey& key = vBlockMiners[i];
        if (!key.IsValid() || !seenKeys.insert(key).second) {
            return false;
        }
        if (key == vExpectedMiners[i]) {
            continue;
        }
        int expectedTrack = getValidatorTrack(vExpectedMiners[i]);
        int actualTrack = getValidatorTrack(key);
        if (actualTrack != expectedTrack) {
            return false;
        }

        int trackStandbyIdx = -1;
        int globalStandbyIdx = -1;
        for (size_t k = 0; k < vTrackStandby[expectedTrack].size(); ++k) {
            if (vTrackStandby[expectedTrack][k].first == key) {
                trackStandbyIdx = (int)k;
                globalStandbyIdx = vTrackStandby[expectedTrack][k].second;
                break;
            }
        }
        if (trackStandbyIdx < 0 ||
            trackStandbyIdx <= prevStandbyIdxInTrack[expectedTrack] ||
            trackStandbyIdx >= nReplacedInTrack[expectedTrack] + nSlot ||
            vBlockSolutions[i].empty()) {
            return false;
        }
        int standbyPoolIdx = (int)vExpectedMiners.size() + globalStandbyIdx;
        if (!VerifyAdamSolution(hashPrevBlock, hashAdamSeed, key, vBlockSolutions[i], nBits, nVersion, nHeight, (int)i) &&
            !VerifyAdamSolution(hashPrevBlock, hashAdamSeed, key, vBlockSolutions[i], nBits, nVersion, nHeight, standbyPoolIdx)) {
            return false;
        }
        prevStandbyIdxInTrack[expectedTrack] = trackStandbyIdx;
        nReplacedInTrack[expectedTrack]++;
    }
    return true;
}

bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey) {
    if (vchProof.empty()) return false;
    return VerifyBLSWithECDSAFallback(prevSeed, coordinatorKey, vchProof);
}

bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey, const uint256* pBlockHash) {
    if (block.vAdamCoordinatorSig.empty()) {
        LogPrintf("VerifyAdamCoordinatorSig: Signature is empty!\n");
        return false;
    }
    const uint256 blockHash = pBlockHash ? *pBlockHash : block.GetHash();
    bool result = VerifyBLSWithECDSAFallback(blockHash, coordinatorKey, block.vAdamCoordinatorSig);
    LogPrint(BCLog::MASTERNODE, "VerifyAdamCoordinatorSig: key: %s, hash: %s, sig_size: %d, result: %d\n",
        coordinatorKey.GetID().ToString(), blockHash.ToString(), block.vAdamCoordinatorSig.size(), result);
    LogPrint(BCLog::MASTERNODE, "VerifyAdamCoordinatorSig details: ver=%d, prev=%s, merkle=%s, time=%u, bits=%08x, nonce=%u, miners=%d, solutions=%d, vrf=%d, sig=%d\n",
        block.nVersion, block.hashPrevBlock.ToString(), block.hashMerkleRoot.ToString(), block.nTime, block.nBits, block.nNonce,
        block.vAdamMiners.size(), block.vAdamSolutions.size(), block.vAdamVRFProof.size(), block.vAdamCoordinatorSig.size());
    return result;
}

void ProcessOrphanAdamSolutions(const uint256& hash) {
    std::vector<CAdamSolutionMsg> vOrphans;
    {
        LOCK(cs_adam_solutions);
        auto it = mapOrphanAdamSolutions.find(hash);
        if (it == mapOrphanAdamSolutions.end()) {
            return;
        }
        vOrphans = it->second;
        mapOrphanAdamSolutions.erase(it);
    }

    CBlockIndex* pindexPrev = nullptr;
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash)) {
            pindexPrev = mapBlockIndex[hash];
        }
    }
    if (!pindexPrev) return;

    uint256 adamSeed = GetAdamSeed(pindexPrev);
    const Consensus::Params& consensus = Params().GetConsensus();
    std::vector<CPubKey> vExpectedMiners;
    std::vector<CPubKey> vStandbyMiners;
    CPubKey expectedCoordinator;
    if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, pindexPrev->nHeight + 1, nullptr, &vStandbyMiners)) {
        return;
    }

    CBlockHeader dummyHeader;
    int nNextHeight = pindexPrev->nHeight + 1;
    if (consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
        dummyHeader.nVersion = 12;
    } else {
        dummyHeader.nVersion = 11;
    }
    unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);

    for (const auto& msg : vOrphans) {
        int electedIdx = -1;
        for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
            if (vExpectedMiners[i] == msg.minerKey) {
                electedIdx = (int)i;
                break;
            }
        }
        if (electedIdx < 0) {
            for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
                if (vStandbyMiners[s] == msg.minerKey) {
                    electedIdx = (int)vExpectedMiners.size() + (int)s;
                    break;
                }
            }
        }
        if (electedIdx < 0) continue;

        if (VerifyAdamSolution(hash, adamSeed, msg.minerKey, msg.vchSolution, nBits, dummyHeader.nVersion, nNextHeight, electedIdx)) {
            bool isNew = false;
            bool shouldVerifyOld = false;
            std::vector<unsigned char> vchOldSol;
            {
                LOCK(cs_adam_solutions);
                auto& solutionsForBlock = mapAdamSolutionsCache[hash];
                auto solIt = solutionsForBlock.find(msg.minerKey);
                if (solIt == solutionsForBlock.end()) {
                    solutionsForBlock[msg.minerKey] = msg.vchSolution;
                    isNew = true;
                } else {
                    vchOldSol = solIt->second;
                    shouldVerifyOld = true;
                }
            }
            if (shouldVerifyOld &&
                !VerifyAdamSolution(hash, adamSeed, msg.minerKey, vchOldSol, nBits, dummyHeader.nVersion, nNextHeight, electedIdx)) {
                LOCK(cs_adam_solutions);
                mapAdamSolutionsCache[hash][msg.minerKey] = msg.vchSolution;
                isNew = true;
            }
            if (isNew) {
                LogPrintf("ProcessOrphanAdamSolutions: Successfully verified and cached orphan adamsol for miner key %s and tip %s\n",
                    msg.minerKey.GetID().ToString(), hash.ToString());
            }
        }
    }
}

uint256 ComputePoBLSTicket(const CBLSPubKey& blsPubKey, const CBLSSignature& blsSig, int nBlockHeight) {
    CHashWriter ss(SER_GETHASH, 0);
    ss.write((const char*)blsPubKey.begin(), 48);
    ss.write((const char*)blsSig.begin(), 96);
    ss << nBlockHeight;
    return ss.GetHash();
}

bool ExtractPoBLSTicketFromSolution(const std::vector<unsigned char>& vchSolution, int nBlockHeight, uint256& ticketHashOut, const CPubKey* pMinerKey) {
    if (vchSolution.empty()) return false;
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce = 0;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        if (vchSig.empty()) return false;

        CDataStream ssSig(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ssSig >> signedData;
        if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
            if (pMinerKey != nullptr) {
                if (!pMinerKey->IsValid()) return false;
                uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                if (!pMinerKey->Verify(hashPubKey, signedData.ecdsaSig)) {
                    return false;
                }
                CBLSPubKey pinnedBlsPubKey;
                if (GetPinnedBLSPubKey(*pMinerKey, pinnedBlsPubKey) && signedData.blsPubKey != pinnedBlsPubKey) {
                    return false;
                }
            }
            ticketHashOut = ComputePoBLSTicket(signedData.blsPubKey, signedData.blsSig, nBlockHeight);
            return true;
        }
    } catch (...) {}
    return false;
}

uint256 ComputePoBLSTarget(const uint256& hashAdamSeed, int nBlockHeight) {
    CHashWriter ss(SER_GETHASH, 0);
    ss << hashAdamSeed;
    ss << nBlockHeight;
    return ss.GetHash();
}

uint256 ComputePoBLSXORDistance(const uint256& ticketHash, const uint256& targetHash) {
    arith_uint256 d = UintToArith256(ticketHash);
    d ^= UintToArith256(targetHash);
    return ArithToUint256(d);
}

std::vector<CPubKey> RankValidatorsByPoBLSDistance(
    const uint256& hashAdamSeed,
    int nBlockHeight,
    const std::vector<CPubKey>& vMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions)
{
    uint256 targetHash = ComputePoBLSTarget(hashAdamSeed, nBlockHeight);
    struct ValidatorDistance {
        CPubKey key;
        uint256 distance;
        bool hasValidTicket;
        bool operator<(const ValidatorDistance& other) const {
            if (hasValidTicket != other.hasValidTicket) {
                return hasValidTicket > other.hasValidTicket;
            }
            if (distance != other.distance) {
                return distance < other.distance;
            }
            return key < other.key;
        }
    };

    std::vector<ValidatorDistance> ranked;
    ranked.reserve(vMiners.size());
    for (const auto& minerKey : vMiners) {
        uint256 ticketHash;
        bool hasTicket = false;
        auto it = mapSolutions.find(minerKey);
        if (it != mapSolutions.end() && !it->second.empty()) {
            hasTicket = ExtractPoBLSTicketFromSolution(it->second, nBlockHeight, ticketHash, &minerKey);
        }
        if (!hasTicket) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << hashAdamSeed;
            ss << minerKey;
            ss << nBlockHeight;
            ticketHash = ss.GetHash();
        }
        ValidatorDistance vd;
        vd.key = minerKey;
        vd.distance = ComputePoBLSXORDistance(ticketHash, targetHash);
        vd.hasValidTicket = hasTicket;
        ranked.push_back(vd);
    }

    std::sort(ranked.begin(), ranked.end());
    std::vector<CPubKey> result;
    result.reserve(ranked.size());
    for (const auto& vd : ranked) {
        result.push_back(vd.key);
    }
    return result;
}

bool GetAdamFallbackCoordinatorForSlot(
    const uint256& hashAdamSeed,
    int nBlockHeight,
    int64_t nTimeElapsed,
    const std::vector<CPubKey>& vMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions,
    CPubKey& activeFallbackOut,
    CPubKey& prevFallbackOut,
    int& rotationIndexOut)
{
    if (nTimeElapsed < ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS || vMiners.empty()) {
        return false;
    }
    std::vector<CPubKey> vRankedValidators = RankValidatorsByPoBLSDistance(hashAdamSeed, nBlockHeight, vMiners, mapSolutions);
    if (vRankedValidators.empty()) {
        return false;
    }
    int64_t nFallbackElapsed = nTimeElapsed - ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS;
    int64_t nRawSlot = nFallbackElapsed / 30;
    int64_t nSlotOffset = nFallbackElapsed % 30;
    rotationIndexOut = nRawSlot % vRankedValidators.size();
    activeFallbackOut = vRankedValidators[rotationIndexOut];
    if (nRawSlot > 0 && nSlotOffset < 5) {
        int prevRotationIndex = (rotationIndexOut + (int)vRankedValidators.size() - 1) % vRankedValidators.size();
        prevFallbackOut = vRankedValidators[prevRotationIndex];
    } else {
        prevFallbackOut = activeFallbackOut;
    }
    return true;
}

RecursiveMutex cs_pos_coord_sigs;
std::map<uint256, CPoSCoordSigEntry> mapPoSCoordSigs;
std::map<uint256, CSeenPoSCoordReqEntry> mapSeenPoSCoordReqs;

bool RecordSeenPoSCoordReq(const uint256& reqHash, const CPubKey& targetCoordinator, const CBlockHeader& draftHeader, const uint256& adamSeed)
{
    LOCK(cs_pos_coord_sigs);
    if (mapSeenPoSCoordReqs.count(reqHash) > 0) {
        return false;
    }
    if (mapSeenPoSCoordReqs.size() >= 1000) {
        auto itOldest = mapSeenPoSCoordReqs.begin();
        for (auto it = mapSeenPoSCoordReqs.begin(); it != mapSeenPoSCoordReqs.end(); ++it) {
            if (it->second.nTime < itOldest->second.nTime) {
                itOldest = it;
            }
        }
        if (itOldest != mapSeenPoSCoordReqs.end()) {
            mapSeenPoSCoordReqs.erase(itOldest);
        }
    }
    CBlockHeader cleanHeader = draftHeader;
    cleanHeader.vAdamVRFProof.clear();
    cleanHeader.vAdamCoordinatorSig.clear();
    cleanHeader.vQuorumSig.clear();
    CSeenPoSCoordReqEntry entry;
    entry.targetCoordinator = targetCoordinator;
    entry.draftHeader = std::move(cleanHeader);
    entry.adamSeed = adamSeed;
    entry.nTime = GetTime();
    mapSeenPoSCoordReqs[reqHash] = std::move(entry);
    return true;
}

uint256 ComputePoSCoordRequestHash(const CBlock& block, const CPubKey& targetCoordinator)
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    static const std::string strDomain = "KRISTA_POS_COORD_REQ_V1";
    ss << strDomain;
    ss << block.nVersion;
    ss << block.hashPrevBlock;
    ss << block.hashMerkleRoot;
    ss << block.nTime;
    ss << block.nBits;
    ss << block.nNonce;
    ss << block.vAdamMiners;
    ss << block.vAdamSolutions;
    ss << block.vtx;
    ss << targetCoordinator;
    return ss.GetHash();
}

bool SignPoSCoordRequest(const CBlock& block, const uint256& reqHash, const CKeyStore& keystore, std::vector<unsigned char>& vchSigOut)
{
    vchSigOut.clear();
    if (!block.IsProofOfStake() || block.vtx.size() < 2 || block.vtx[1].vout.size() < 2) {
        return false;
    }
    CKeyID keyID;
    if (!block.vtx[1].vout[1].GetKeyIDFromUTXO(keyID)) {
        return false;
    }
    CKey key;
    if (!keystore.GetKey(keyID, key) || !key.IsValid()) {
        return false;
    }
    CBLSSecretKey blsKey = DeriveBLSFromCKey(key);
    if (!blsKey.IsValid()) {
        return false;
    }
    return SignBLSWithECDSAFallback(reqHash, key, blsKey, vchSigOut);
}

bool VerifyPoSCoordRequestAuth(const CBlock& block, const uint256& reqHash, const std::vector<unsigned char>& vchSig, bool enableP2PKH)
{
    if (vchSig.empty() || vchSig.size() > 512) {
        return false;
    }
    if (!block.IsProofOfStake() || block.vtx.size() < 2 || block.vtx[1].vin.empty() || block.vtx[1].vout.size() < 2) {
        return false;
    }
    txnouttype whichType;
    std::vector<std::vector<unsigned char>> vSolutions;
    const CTxOut& txout = block.vtx[1].vout[1];
    if (!Solver(txout.scriptPubKey, whichType, vSolutions)) {
        return false;
    }
    CPubKey pubkey;
    if (whichType == TX_PUBKEY) {
        pubkey = CPubKey(vSolutions[0]);
    } else if (whichType == TX_PUBKEYHASH) {
        if (!enableP2PKH) {
            return false;
        }
        const CScript& scriptSig = block.vtx[1].vin[0].scriptSig;
        CScript::const_iterator pc = scriptSig.begin();
        opcodetype op1, op2;
        std::vector<unsigned char> vchSigIn, vchPubKeyIn;
        if (!scriptSig.GetOp(pc, op1, vchSigIn) || vchSigIn.empty() ||
            !scriptSig.GetOp(pc, op2, vchPubKeyIn) || vchPubKeyIn.empty()) {
            return false;
        }
        pubkey = CPubKey(vchPubKeyIn);
        if (!pubkey.IsValid() || pubkey.GetID() != CKeyID(uint160(vSolutions[0]))) {
            return false;
        }
    } else {
        return false;
    }
    if (!pubkey.IsValid()) {
        return false;
    }
    return VerifyBLSWithECDSAFallback(reqHash, pubkey, vchSig);
}





