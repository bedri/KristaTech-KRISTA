// Copyright (c) 2026 The KRISTA Developers
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
bool g_fMasternodePingStateInitialized = false;

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

bool ExtractOnChainMasternodePing(const CTransaction& tx, CPubKey& pubKeyMasternodeOut, COutPoint& collateralOutpointOut) {
    if (tx.IsCoinBase() || tx.vin.empty()) return false;

    CPubKey foundPubKey;
    bool fSignedInput = false;

    // 1. Check inputs for Taproot script-path witness leaf (<33-byte pubkey> OP_CHECKSIGVERIFY OP_1)
    for (const CTxIn& txin : tx.vin) {
        std::vector<std::vector<unsigned char>> stack;
        CScript::const_iterator pc = txin.scriptSig.begin();
        opcodetype opcode;
        std::vector<unsigned char> vch;
        while (pc < txin.scriptSig.end()) {
            if (txin.scriptSig.GetOp(pc, opcode, vch)) {
                if (opcode >= 0 && opcode <= OP_PUSHDATA4) {
                    stack.push_back(vch);
                }
            } else {
                break;
            }
        }
        if (stack.size() >= 2) {
            const std::vector<unsigned char>& leaf_script_bytes = stack[stack.size() - 2];
            CScript leaf_script(leaf_script_bytes.begin(), leaf_script_bytes.end());
            if (leaf_script.size() == 36 && leaf_script[0] == 33 && leaf_script[34] == OP_CHECKSIGVERIFY && leaf_script[35] == OP_1) {
                CPubKey pubKey(leaf_script.begin() + 1, leaf_script.begin() + 34);
                if (pubKey.IsValid()) {
                    foundPubKey = pubKey;
                    fSignedInput = true;
                    break;
                }
            }
        }
    }

    // 2. Check outputs for direct MESCAL MasternodePing script (<33-byte pubkey> OP_CHECKSIGVERIFY OP_1).
    // IMPORTANT: Output-only matches MUST be accompanied by a valid ECDSA signature (vchPingSig) from foundPubKey
    // inside the OP_RETURN payload over Hash(collateralOutpoint || tx.vin[0].prevout) to prevent keep-alive spoofing.
    if (!foundPubKey.IsValid()) {
        for (const CTxOut& out : tx.vout) {
            const CScript& script = out.scriptPubKey;
            if (script.size() == 36 && script[0] == 33 && script[34] == OP_CHECKSIGVERIFY && script[35] == OP_1) {
                CPubKey pubKey(script.begin() + 1, script.begin() + 34);
                if (pubKey.IsValid()) {
                    foundPubKey = pubKey;
                    fSignedInput = false;
                    break;
                }
            }
        }
    }

    if (!foundPubKey.IsValid()) return false;

    // 3. Extract collateralOutpoint + cryptographic authorization from OP_RETURN output
    for (const CTxOut& out : tx.vout) {
        if (out.scriptPubKey.IsUnspendable() && out.scriptPubKey.size() >= 38 && out.scriptPubKey[0] == OP_RETURN) {
            CScript::const_iterator o_pc = out.scriptPubKey.begin() + 1;
            opcodetype o_opcode;
            std::vector<unsigned char> o_vch;
            if (out.scriptPubKey.GetOp(o_pc, o_opcode, o_vch) && o_vch.size() >= 36) {
                try {
                    CDataStream o_ss(o_vch, SER_NETWORK, PROTOCOL_VERSION);
                    COutPoint collateralOutpoint;
                    o_ss >> collateralOutpoint;

                    if (!fSignedInput) {
                        // Output-based ping MUST carry a valid ECDSA signature by foundPubKey over (collateralOutpoint || tx.vin[0].prevout)
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
                        // Consume optional vchPingSig if present on input-signed ping
                        std::vector<unsigned char> vchPingSig;
                        o_ss >> vchPingSig;
                    }

                    // Extract and verify optional on-chain Hot/Cold collateral delegation proof
                    if (!o_ss.empty()) {
                        bool hasDelegation = false;
                        o_ss >> hasDelegation;
                        if (hasDelegation && !o_ss.empty()) {
                            COnChainMasternodeDelegation del;
                            o_ss >> del;
                            if (del.pubKeyMasternode == foundPubKey && VerifyOnChainDelegationSignature(del, collateralOutpoint)) {
                                mapOnChainCollateralDelegations[collateralOutpoint] = del;
                            }
                        }
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

bool IsValidMasternodeCollateralAndAuth(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight) {
    if (!pubKeyMasternode.IsValid() || collateralOutpoint.IsNull()) {
        return false;
    }
    if (!pcoinsTip) {
        return false;
    }
    Coin coin;
    if (!pcoinsTip->GetCoin(collateralOutpoint, coin) || coin.IsSpent()) {
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

    // 2. On-chain Hot/Cold delegation proof carried inside OP_RETURN (works during IBD even when P2P mnodeman is empty)
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

    // 3. Fallback to verified CMasternodeBroadcast in mnodeman before first on-chain delegation is mined
    CMasternode* pmn = mnodeman.Find(CTxIn(collateralOutpoint));
    if (pmn && pmn->pubKeyMasternode == pubKeyMasternode && pmn->pubKeyCollateralAddress.IsValid()) {
        CScript collateralScript = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
        if (coin.out.scriptPubKey == collateralScript) {
            return true;
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

bool RegisterOnChainMasternodePing(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight) {
    if (!IsValidMasternodeCollateralAndAuth(pubKeyMasternode, collateralOutpoint, nHeight)) {
        return false;
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
    return true;
}

void RegisterBlockMasternodeActivity(const CBlock& block, int nHeight) {
    LOCK(cs_main);
    for (const CTransaction& tx : block.vtx) {
        CPubKey pubKeyMasternode;
        COutPoint collateralOutpoint;
        if (ExtractOnChainMasternodePing(tx, pubKeyMasternode, collateralOutpoint)) {
            RegisterOnChainMasternodePing(pubKeyMasternode, collateralOutpoint, nHeight);
        }
    }

    // Zero-fee automatic on-chain heartbeat & BLS key pinning from verified ADAM/LLMQ block participation
    if (block.nVersion >= 11) {
        for (size_t i = 0; i < block.vAdamMiners.size(); ++i) {
            if (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty()) {
                const CPubKey& mnKey = block.vAdamMiners[i];
                if (!mnKey.IsValid()) continue;
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
                    RegisterOnChainMasternodePing(mnKey, itOut->second, nHeight);
                } else {
                    CMasternode* pmn = mnodeman.Find(mnKey);
                    if (pmn && pmn->IsEnabled()) {
                        RegisterOnChainMasternodePing(mnKey, pmn->vin.prevout, nHeight);
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
                    CPubKey matchedCoordKey;

                    // 1. O(1) Fast-path: check block.vAdamMiners.back() first (version 11 / fallback coordinator)
                    if (!block.vAdamMiners.empty() && block.vAdamMiners.back().IsValid() &&
                        block.vAdamMiners.back().Verify(hashPubKey, signedData.ecdsaSig)) {
                        matchedCoordKey = block.vAdamMiners.back();
                    }

                    // 2. O(1) Fast-path: check primary coordinator from SelectAdamNodes
                    if (!matchedCoordKey.IsValid()) {
                        BlockMap::iterator itPrev = mapBlockIndex.find(block.hashPrevBlock);
                        if (itPrev != mapBlockIndex.end() && itPrev->second) {
                            uint256 prevSeed = GetAdamSeed(itPrev->second);
                            std::vector<CPubKey> vExpectedMiners;
                            CPubKey primaryCoord;
                            if (!prevSeed.IsNull() &&
                                SelectAdamNodes(prevSeed, Params().GetConsensus(), vExpectedMiners, primaryCoord, nHeight) &&
                                primaryCoord.IsValid() && primaryCoord.Verify(hashPubKey, signedData.ecdsaSig)) {
                                matchedCoordKey = primaryCoord;
                            }
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
                        std::vector<CPubKey> vPool = GetAdamMinerPool(nHeight - 1);
                        for (const auto& candKey : vPool) {
                            if (candKey.IsValid() && candKey.Verify(hashPubKey, signedData.ecdsaSig)) {
                                matchedCoordKey = candKey;
                                break;
                            }
                        }
                    }

                    if (matchedCoordKey.IsValid()) {
                        PinBLSPubKey(matchedCoordKey, coordBlsPubKey, nHeight);
                        auto itOut = mapMasternodeCollateralOutpoints.find(matchedCoordKey);
                        if (itOut != mapMasternodeCollateralOutpoints.end()) {
                            RegisterOnChainMasternodePing(matchedCoordKey, itOut->second, nHeight);
                        } else {
                            CMasternode* pmn = mnodeman.Find(matchedCoordKey);
                            if (pmn && pmn->IsEnabled()) {
                                RegisterOnChainMasternodePing(matchedCoordKey, pmn->vin.prevout, nHeight);
                            }
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
                for (const auto& sigPair : qsig.signatures) {
                    const COutPoint& outpoint = sigPair.first;
                    CPubKey quorumMnKey;
                    auto itDel = mapOnChainCollateralDelegations.find(outpoint);
                    if (itDel != mapOnChainCollateralDelegations.end() && itDel->second.pubKeyMasternode.IsValid()) {
                        quorumMnKey = itDel->second.pubKeyMasternode;
                    } else {
                        CMasternode* pmn = mnodeman.Find(CTxIn(outpoint));
                        if (pmn && pmn->pubKeyMasternode.IsValid()) {
                            quorumMnKey = pmn->pubKeyMasternode;
                        }
                    }
                    if (quorumMnKey.IsValid()) {
                        RegisterOnChainMasternodePing(quorumMnKey, outpoint, nHeight);
                        CBLSPubKey quorumBlsPubKey;
                        if (ExtractBLSPubKeyFromSignedData(sigPair.second, quorumBlsPubKey)) {
                            PinBLSPubKey(quorumMnKey, quorumBlsPubKey, nHeight);
                        }
                    }
                }
            } catch (...) {}
        }
    }
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
        ExportPinnedBLSPubKeys(state.mapPinnedBLS);
    }
    return pblocktree->WriteMasternodePingState(state);
}

void InitializeOnChainMasternodePings() {
    LOCK(cs_main);
    mapMasternodeLastActiveHeight.clear();
    mapMasternodeActiveHeights.clear();
    mapMasternodeCollateralOutpoints.clear();
    mapOnChainCollateralDelegations.clear();
    ClearPinnedBLSPubKeys();
    const CBlockIndex* pindexTip = chainActive.Tip();
    if (!pindexTip || pindexTip->nHeight <= 0) {
        g_fMasternodePingStateInitialized = true;
        return;
    }

    int nTipHeight = pindexTip->nHeight;
    int nStartHeight = std::max(1, Params().GetConsensus().vUpgrades[Consensus::UPGRADE_ADAM].nActivationHeight);
    bool fLoadedFromDB = false;

    COnChainMasternodeStateCache cachedState;
    if (pblocktree && pblocktree->ReadMasternodePingState(cachedState)) {
        auto mi = mapBlockIndex.find(cachedState.hashBestBlock);
        if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
            int nCachedHeight = mi->second->nHeight;
            mapMasternodeCollateralOutpoints = std::move(cachedState.mapCollateralOutpoints);
            mapOnChainCollateralDelegations = std::move(cachedState.mapCollateralDelegations);
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

uint256 GetMinerPoWLimit(const std::string& networkId) {
    if (networkId == "main") {
        return uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 28 bits
    } else if (networkId == "test") {
        return uint256S("00000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 20 bits
    } else { // regtest
        return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 8 bits
    }
}

uint256 GetZeroCoinPoWLimit(const std::string& networkId) {
    if (networkId == "main") {
        return uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // 24 bits
    } else if (networkId == "test") {
        return uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // ~1 min
    } else { // regtest
        return uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"); // fast
    }
}

struct CachedBlockRegEntry {
    bool isCoinLock;
    COutPoint outpoint;
    CAmount nValue;
    CPubKey pubkey;
    int64_t lockTime;
    uint256 challenge;
};

std::vector<CPubKey> GetAdamMinerPool(int nHeight) {
    static RecursiveMutex cs_miner_pool_cache;
    static std::map<uint256, std::vector<CPubKey>> mapMinerPoolCache;
    static std::map<uint256, std::vector<CachedBlockRegEntry>> mapBlockRegCache;
    static std::set<CPubKey> cachedBootstrapKeys;
    static int cachedBootstrapScannedUpTo = 0;
    static uint256 cachedGenesisHash;

    LOCK(cs_main);
    LOCK(cs_miner_pool_cache);
    if (mapMinerPoolCache.size() > 500) {
        mapMinerPoolCache.clear();
    }
    if (mapBlockRegCache.size() > 5000) {
        mapBlockRegCache.clear();
    }
    CBlockIndex* pindexTip = nullptr;
    if (nHeight == -1) {
        pindexTip = chainActive.Tip();
    } else if (nHeight >= 0 && nHeight <= chainActive.Height()) {
        pindexTip = chainActive[nHeight];
    }
    if (!pindexTip) {
        pindexTip = chainActive.Tip();
    }
    if (pindexTip && mapMinerPoolCache.count(pindexTip->GetBlockHash())) {
        return mapMinerPoolCache[pindexTip->GetBlockHash()];
    }

    std::set<CPubKey> uniqueKeys;

    if (Params().IsRegTestNet()) {
        for (int i = 0; i < 15; ++i) {
            uniqueKeys.insert(GetAdamDeterministicPubKey(i));
        }
    }

    // Automatically register bootstrap miners from blocks 1 to 199 (cached in memory to avoid disk reads)
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
                if (!block.vtx.empty()) {
                    const CTransaction& coinbaseTx = block.vtx[0];
                    if (!coinbaseTx.vout.empty()) {
                        const CTxOut& vout = coinbaseTx.vout[0];
                        CScript::const_iterator pc = vout.scriptPubKey.begin();
                        opcodetype opcode;
                        std::vector<unsigned char> vchPubKey;
                        if (vout.scriptPubKey.GetOp(pc, opcode, vchPubKey) && (vchPubKey.size() == 33 || vchPubKey.size() == 65)) {
                            if (vout.scriptPubKey.GetOp(pc, opcode) && opcode == OP_CHECKSIG) {
                                CPubKey pubkey(vchPubKey);
                                if (pubkey.IsValid()) {
                                    cachedBootstrapKeys.insert(pubkey);
                                }
                            }
                        }
                    }
                }
                cachedBootstrapScannedUpTo = h;
            }
        }
        for (const auto& k : cachedBootstrapKeys) {
            uniqueKeys.insert(k);
        }
    }

    int currentRefHeight = pindexTip ? pindexTip->nHeight : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
    int poolThreshold = Params().GetConsensus().GetAdamThreshold(currentRefHeight + 1);
    std::vector<CPubKey> vOnChainMns;
    std::set<COutPoint> seenCollateralOutpoints;
    for (const auto& kv : mapMasternodeCollateralOutpoints) {
        if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, currentRefHeight, 2880)) {
            if (!seenCollateralOutpoints.count(kv.second) &&
                IsValidMasternodeCollateralAndAuth(kv.first, kv.second, currentRefHeight)) {
                vOnChainMns.push_back(kv.first);
                seenCollateralOutpoints.insert(kv.second);
                uniqueKeys.insert(kv.first);
            }
        }
    }

    if ((int)vOnChainMns.size() < poolThreshold) {
        std::vector<CMasternode> vMns = mnodeman.GetFullMasternodeVector();
        for (auto& mn : vMns) {
            if (!mn.IsEnabled() || !mn.pubKeyMasternode.IsValid()) continue;
            if (!Params().IsRegTestNet()) {
                if (seenCollateralOutpoints.count(mn.vin.prevout)) continue;
                if (!IsValidMasternodeCollateralAndAuth(mn.pubKeyMasternode, mn.vin.prevout, currentRefHeight)) continue;
                seenCollateralOutpoints.insert(mn.vin.prevout);
            }
            uniqueKeys.insert(mn.pubKeyMasternode);
        }
    }

    std::set<CPubKey> zeroCoinKeys;
    if (pindexTip) {
        int nTipHeight = pindexTip->nHeight;
        int nRegPeriod = 2880;
        if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
            if (IsModelDActive(nTipHeight + 1)) {
                nRegPeriod = 100;
            }
        }
        int nLimit = std::max(0, nTipHeight - nRegPeriod);
        uint256 powLimitTarget = GetMinerPoWLimit(Params().NetworkIDString());

        uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString());

        for (int h = nTipHeight; h > nLimit; --h) {
            CBlockIndex* pindex = chainActive[h];
            if (!pindex) continue;

            const uint256 blockHash = pindex->GetBlockHash();
            auto cacheIt = mapBlockRegCache.find(blockHash);
            if (cacheIt == mapBlockRegCache.end()) {
                std::vector<CachedBlockRegEntry> vecEntries;
                CBlock block;
                if (ReadBlockFromDisk(block, pindex)) {
                    int nZeroCoinTxInBlock = 0;
                    for (const auto& tx : block.vtx) {
                        bool isZeroCoinTx = IsZeroCoinPoWTransaction(tx);
                        if (isZeroCoinTx) {
                            if (tx.vout.size() != 1 || nZeroCoinTxInBlock >= 1) continue;
                            nZeroCoinTxInBlock++;
                        }
                        uint256 txid = tx.GetHash();
                        for (size_t i = 0; i < tx.vout.size(); ++i) {
                            const auto& vout = tx.vout[i];
                            if (vout.scriptPubKey.size() < 60) continue;
                            CPubKey pubkey;
                            int64_t lockTime = 0;
                            CKeyID pubkeyHash;

                            if (MatchCoinLockRegistration(vout.scriptPubKey, pubkey, lockTime, pubkeyHash)) {
                                if (pubkey.GetID() != pubkeyHash) continue;
                                if (vout.nValue < MINER_REGISTRATION_LOCK_AMOUNT) continue;
                                CachedBlockRegEntry entry;
                                entry.isCoinLock = true;
                                entry.outpoint = COutPoint(txid, i);
                                entry.nValue = vout.nValue;
                                entry.pubkey = pubkey;
                                entry.lockTime = lockTime;
                                vecEntries.push_back(entry);
                            } else {
                                std::vector<unsigned char> nonce;
                                uint256 challenge;
                                if (MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash)) {
                                    if (pubkey.GetID() != pubkeyHash) continue;
                                    if (nonce.size() != 4) continue;
                                    uint32_t nNonce = nonce[0] | (nonce[1] << 8) | (nonce[2] << 16) | (nonce[3] << 24);
                                    CHashWriter ss(SER_GETHASH, 0);
                                    ss << nNonce;
                                    ss << challenge;
                                    ss << pubkey;
                                    uint256 puzzleHash = ss.GetHash();
                                    if (vout.nValue == 0) {
                                        if (!isZeroCoinTx) continue;
                                        if (puzzleHash > zeroCoinTarget) continue;
                                    } else {
                                        if (vout.nValue < 10000) continue;
                                        if (puzzleHash > powLimitTarget) continue;
                                    }
                                    CachedBlockRegEntry entry;
                                    entry.isCoinLock = false;
                                    entry.outpoint = COutPoint(txid, i);
                                    entry.nValue = vout.nValue;
                                    entry.pubkey = pubkey;
                                    entry.lockTime = lockTime;
                                    entry.challenge = challenge;
                                    vecEntries.push_back(entry);
                                }
                            }
                        }
                    }
                }
                cacheIt = mapBlockRegCache.emplace(blockHash, std::move(vecEntries)).first;
            }

            for (const auto& entry : cacheIt->second) {
                if (entry.lockTime < pindex->nHeight + nRegPeriod) continue;
                if (entry.isCoinLock) {
                    if (!pcoinsTip->HaveCoin(entry.outpoint)) continue;
                    uniqueKeys.insert(entry.pubkey);

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
                    bool challengeValid = false;
                    auto miChallenge = mapBlockIndex.find(entry.challenge);
                    if (miChallenge != mapBlockIndex.end() && miChallenge->second) {
                        CBlockIndex* pindexChallenge = miChallenge->second;
                        if (pindexChallenge->nHeight >= 0 && pindexChallenge->nHeight <= chainActive.Height() &&
                            chainActive[pindexChallenge->nHeight] && chainActive[pindexChallenge->nHeight]->GetBlockHash() == entry.challenge) {
                            if (pindexChallenge->nHeight >= pindex->nHeight - 100 && pindexChallenge->nHeight < pindex->nHeight) {
                                challengeValid = true;
                            }
                        }
                    }
                    if (!challengeValid) continue;

                    if (entry.nValue > 0) {
                        if (entry.nValue < 10000 || !pcoinsTip->HaveCoin(entry.outpoint)) continue;
                        uniqueKeys.insert(entry.pubkey);
                    } else {
                        zeroCoinKeys.insert(entry.pubkey);
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

    // Zero-coin PoW registrations can only fill bootstrap slots when funded Masternode/Coin-Lock pool is below nAdamMinersCount + 2
    size_t maxBootstrapPoolSize = (size_t)(Params().GetConsensus().nAdamMinersCount + 2);
    if (uniqueKeys.size() < maxBootstrapPoolSize) {
        for (const auto& zKey : zeroCoinKeys) {
            if (uniqueKeys.size() >= maxBootstrapPoolSize) break;
            uniqueKeys.insert(zKey);
        }
    }

    std::vector<CPubKey> resultPool;
    for (const auto& key : uniqueKeys) {
        resultPool.push_back(key);
    }
    if (pindexTip && resultPool.size() >= (size_t)Params().GetConsensus().GetAdamThreshold(pindexTip->nHeight) && masternodeSync.IsSynced()) {
        mapMinerPoolCache[pindexTip->GetBlockHash()] = resultPool;
    }
    return resultPool;
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


bool SelectAdamNodes(const uint256& hashAdamSeed, const Consensus::Params& params, std::vector<CPubKey>& vSelectedMinersOut, CPubKey& coordinatorOut, int nBlockHeight) {
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
    std::vector<CPubKey> pool = GetAdamMinerPool(nHeight);
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
                    IsValidMasternodeCollateralAndAuth(kv.first, kv.second, currentHeight)) {
                    vMns.push_back(kv.first);
                    seenCollateralOutpoints.insert(kv.second);
                }
            }
        }
        if ((int)vMns.size() < threshold) {
            std::vector<CMasternode> vFullMns = mnodeman.GetFullMasternodeVector();
            for (auto& mn : vFullMns) {
                if (mn.IsEnabled() && mn.pubKeyMasternode.IsValid() &&
                    !seenCollateralOutpoints.count(mn.vin.prevout) &&
                    IsValidMasternodeCollateralAndAuth(mn.pubKeyMasternode, mn.vin.prevout, currentHeight)) {
                    vMns.push_back(mn.pubKeyMasternode);
                    seenCollateralOutpoints.insert(mn.vin.prevout);
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
    static std::vector<CPubKey> cachedPool;
    static std::vector<CPubKey> cachedMns;
    static std::vector<CPubKey> cachedSelectedMiners;
    static CPubKey cachedCoordinator;
    {
        LOCK(cs_select_cache);
        if (!cachedSeed.IsNull() &&
            cachedSeed == hashAdamSeed &&
            cachedMinCount == minCount &&
            cachedThreshold == threshold &&
            cachedPool == pool &&
            cachedMns == vMns) {
            vSelectedMinersOut = cachedSelectedMiners;
            coordinatorOut = cachedCoordinator;
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
    
    if ((int)vMns.size() > threshold) {
        // Rule 1: Active Masternodes count > Threshold.
        // Rank active masternodes separately to find the highest-ranked masternode
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
        
        // The coordinator is the highest ranked masternode
        coordinatorOut = rankedMns[0].key;
        
        // Remove the coordinator from the rankedPool to elect miners
        std::vector<MinerRank> remainingPool;
        remainingPool.reserve(rankedPool.size());
        for (const auto& r : rankedPool) {
            if (r.key != coordinatorOut) {
                remainingPool.push_back(r);
            }
        }
        
        // Elect miners from the remaining pool
        vSelectedMinersOut.clear();
        int electCount = std::min(minCount, (int)remainingPool.size());
        for (int i = 0; i < electCount; ++i) {
            vSelectedMinersOut.push_back(remainingPool[i].key);
        }
    } else {
        // Rule 2: Active Masternodes count <= Threshold (or none active).
        // Elect miners from the ranked combined pool
        vSelectedMinersOut.clear();
        int electCount = std::min(minCount, (int)rankedPool.size());
        for (int i = 0; i < electCount; ++i) {
            vSelectedMinersOut.push_back(rankedPool[i].key);
        }
        
        // Coordinator is the "extra" element in the ranked list after the miners
        if ((int)rankedPool.size() > electCount) {
            coordinatorOut = rankedPool[electCount].key;
        } else {
            coordinatorOut = rankedPool[0].key;
        }
    }

    {
        LOCK(cs_select_cache);
        cachedSeed = hashAdamSeed;
        cachedMinCount = minCount;
        cachedThreshold = threshold;
        cachedPool = std::move(pool);
        cachedMns = std::move(vMns);
        cachedSelectedMiners = vSelectedMinersOut;
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
        if (nVersion == 11) {
            int algoIndex = GetAdamPuzzleAlgo(hashAdamSeed, minerKey, true);
            puzzleHash = CalculateAdamPuzzleHash(algoIndex, inputBuf, inputBuf + inputLen);
        } else if (nVersion >= 12) {
            int minerIdx = knownMinerIdx;
            if (minerIdx < 0) {
                std::vector<CPubKey> vExpectedMiners;
                CPubKey expectedCoordinator;
                if (SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator, nHeight)) {
                    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                        if (vExpectedMiners[i] == minerKey) {
                            minerIdx = i;
                            break;
                        }
                    }
                }
            }
            if (minerIdx < 0) {
                return false;
            }
            int algo1 = -1, algo2 = -1, algo3 = -1;
            GetAdam3PermutationAlgos(hashPrevBlock, minerKey, algo1, algo2, algo3);
            
            uint256 hash3 = CalculateAdamPuzzleHash(algo3, inputBuf, inputBuf + inputLen);
            int i_factor = minerIdx + 1;
            arith_uint256 val1 = UintToArith256(hash3) * i_factor;
            uint256 multiplied1 = ArithToUint256(val1);
            
            uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
            arith_uint256 val2 = UintToArith256(hash2) * i_factor;
            uint256 multiplied2 = ArithToUint256(val2);
            
            puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
        } else {
            puzzleHash = CalculateAdamPuzzleHash(12, inputBuf, inputBuf + inputLen);
        }
        
        // Verify miner's signature on the puzzle hash
        bool verifyRes = VerifyBLSWithECDSAFallback(puzzleHash, minerKey, vchSig);
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
    CPubKey expectedCoordinator;
    if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, pindexPrev->nHeight + 1)) {
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
        bool elected = false;
        for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
            if (vExpectedMiners[i] == msg.minerKey) {
                elected = true;
                break;
            }
        }
        if (!elected) continue;

        if (VerifyAdamSolution(hash, adamSeed, msg.minerKey, msg.vchSolution, nBits, dummyHeader.nVersion, nNextHeight)) {
            LOCK(cs_adam_solutions);
            mapAdamSolutionsCache[hash][msg.minerKey] = msg.vchSolution;
            LogPrintf("ProcessOrphanAdamSolutions: Successfully verified and cached orphan adamsol for miner key %s and tip %s\n",
                msg.minerKey.GetID().ToString(), hash.ToString());
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

bool ExtractPoBLSTicketFromSolution(const std::vector<unsigned char>& vchSolution, int nBlockHeight, uint256& ticketHashOut) {
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
            hasTicket = ExtractPoBLSTicketFromSolution(it->second, nBlockHeight, ticketHash);
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
    rotationIndexOut = ((nTimeElapsed - ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) / 30) % vRankedValidators.size();
    int prevRotationIndex = (rotationIndexOut + (int)vRankedValidators.size() - 1) % vRankedValidators.size();
    activeFallbackOut = vRankedValidators[rotationIndexOut];
    prevFallbackOut = vRankedValidators[prevRotationIndex];
    return true;
}




