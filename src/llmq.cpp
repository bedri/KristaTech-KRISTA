// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq.h"
#include "main.h"
#include "activemasternodeman.h"
#include "adam.h"
#include "evo/deterministicmns.h"
#include "masternode.h"
#include "util.h"
#include "sync.h"
#include "messagesigner.h"
#include "chainparams.h"
#include "utilstrencodings.h"
#include "uint256.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <algorithm>
#include <set>

namespace llmq {

static RecursiveMutex cs_quorum_cache;
static std::map<std::pair<int, uint256>, CQuorum> mapQuorumCache;
static uint64_t cachedQuorumPingGen = 0;

void ClearQuorumCache()
{
    LOCK(cs_quorum_cache);
    mapQuorumCache.clear();
}

size_t GetQuorumThreshold(size_t nQuorumSize)
{
    size_t threshold = (2 * nQuorumSize) / 3 + 1;
    if (threshold < 2) threshold = 2;
    if (nQuorumSize < threshold) threshold = nQuorumSize;
    return threshold;
}

static CKeyID GetCompressedKeyID(const CPubKey& pubkey) {
    if (pubkey.IsCompressed()) return pubkey.GetID();
    if (pubkey.size() == 65) {
        unsigned char comp_vch[33];
        comp_vch[0] = (pubkey[64] % 2 == 0) ? 0x02 : 0x03;
        memcpy(comp_vch + 1, pubkey.begin() + 1, 32);
        return CPubKey(comp_vch, comp_vch + 33).GetID();
    }
    return pubkey.GetID();
}

static bool ComparePubKeys(const CPubKey& pk1, const CPubKey& pk2) {
    if (pk1 == pk2) return true;
    if (!pk1.IsValid() || !pk2.IsValid()) return false;
    return CXOnlyPubKey(pk1) == CXOnlyPubKey(pk2);
}

bool CQuorumSignature::Verify(const CQuorum& quorum, const CBlockIndex* pindexPrev, BLSDomain domain, int nVerifyHeight) const
{
    if (quorum.members.empty() || signatures.empty() || signatures.size() > 32) return false;
    const int nSigHeight = (nVerifyHeight >= 0) ? nVerifyHeight : (pindexPrev ? (pindexPrev->nHeight + 1) : quorum.nHeight);
    const std::string& aggDst = GetBLSDomainTag(domain);

    // Threshold is BFT supermajority (floor(2N/3)+1)
    const size_t threshold = GetQuorumThreshold(quorum.members.size());

    size_t validSigsCount = 0;
    std::set<COutPoint> verifiedMembers;
    std::set<CBLSPubKey> verifiedBlsKeys;
    std::vector<CBLSPubKey> vBlsPubKeys;
    std::vector<CBLSSignature> vBlsSigs;

    for (const auto& sigPair : signatures) {
        const COutPoint& memberOutpoint = sigPair.first;
        const std::vector<unsigned char>& sig = sigPair.second;
        if (sig.empty() || sig.size() > 512 || !HasExactBLSSignedDataEncoding(sig)) return false;

        // Verify that this member is part of the quorum
        bool isMember = false;
        CPubKey memberPubKey;
        for (const auto& member : quorum.members) {
            if (member.collateralOutpoint == memberOutpoint) {
                isMember = true;
                memberPubKey = member.pubKeyMasternode;
                break;
            }
        }

        if (!isMember || !memberPubKey.IsValid()) return false;
        if (verifiedMembers.count(memberOutpoint) > 0) return false; // Prevent double signing by same member

        // Verify hybrid BLS12-381 + secp256k1 or KTDIP3 v2 pure operator BLS signature
        CBLSPubKey verifiedBlsPk;
        CBLSSignature verifiedBlsSig;
        if (!VerifyAdamOrDip3Signature(blockHash, memberPubKey, sig, nSigHeight, pindexPrev, &verifiedBlsPk, &verifiedBlsSig, domain) ||
            !verifiedBlsPk.IsValid() || !verifiedBlsSig.IsValid()) {
            return false;
        }
        if (!verifiedBlsKeys.insert(verifiedBlsPk).second) {
            return false;
        }
        validSigsCount++;
        verifiedMembers.insert(memberOutpoint);
        vBlsPubKeys.push_back(verifiedBlsPk);
        vBlsSigs.push_back(verifiedBlsSig);
    }

    if (validSigsCount < threshold || vBlsSigs.size() < threshold) {
        return false;
    }

    // Verify aggregated BLS12-381 signature across all signing quorum members
    CBLSPubKey aggPubKey = CBLSPubKey::Aggregate(vBlsPubKeys);
    CBLSSignature aggSig = CBLSSignature::Aggregate(vBlsSigs);
    if (!aggPubKey.IsValid() || !aggSig.IsValid() || !aggSig.Verify(aggPubKey, blockHash, aggDst)) {
        return false;
    }

    return true;
}

bool CQuorumSignature::AggregateBLSSignatures(const CQuorum& quorum, CBLSPubKey& aggPubKeyOut, CBLSSignature& aggSigOut, const CBlockIndex* pindexPrev, BLSDomain domain, int nVerifyHeight) const
{
    aggPubKeyOut.Reset();
    aggSigOut.Reset();
    if (quorum.members.empty() || signatures.empty() || signatures.size() > 32) return false;
    const int nSigHeight = (nVerifyHeight >= 0) ? nVerifyHeight : (pindexPrev ? (pindexPrev->nHeight + 1) : quorum.nHeight);
    const std::string& aggDst = GetBLSDomainTag(domain);

    std::set<COutPoint> verifiedMembers;
    std::set<CBLSPubKey> verifiedBlsKeys;
    std::vector<CBLSPubKey> vBlsPubKeys;
    std::vector<CBLSSignature> vBlsSigs;

    for (const auto& sigPair : signatures) {
        const COutPoint& memberOutpoint = sigPair.first;
        const std::vector<unsigned char>& sig = sigPair.second;
        if (sig.empty() || sig.size() > 512 || !HasExactBLSSignedDataEncoding(sig)) return false;
        if (verifiedMembers.count(memberOutpoint) > 0) return false;

        bool isMember = false;
        CPubKey memberPubKey;
        for (const auto& member : quorum.members) {
            if (member.collateralOutpoint == memberOutpoint) {
                isMember = true;
                memberPubKey = member.pubKeyMasternode;
                break;
            }
        }
        if (!isMember || sig.empty() || !memberPubKey.IsValid()) return false;

        CBLSPubKey verifiedBlsPk;
        CBLSSignature verifiedBlsSig;
        if (!VerifyAdamOrDip3Signature(blockHash, memberPubKey, sig, nSigHeight, pindexPrev, &verifiedBlsPk, &verifiedBlsSig, domain) ||
            !verifiedBlsPk.IsValid() || !verifiedBlsSig.IsValid()) {
            return false;
        }
        if (!verifiedBlsKeys.insert(verifiedBlsPk).second) {
            return false;
        }
        verifiedMembers.insert(memberOutpoint);
        vBlsPubKeys.push_back(verifiedBlsPk);
        vBlsSigs.push_back(verifiedBlsSig);
    }

    const size_t threshold = GetQuorumThreshold(quorum.members.size());

    if (vBlsSigs.size() < threshold) {
        return false;
    }

    aggPubKeyOut = CBLSPubKey::Aggregate(vBlsPubKeys);
    aggSigOut = CBLSSignature::Aggregate(vBlsSigs);
    return aggPubKeyOut.IsValid() && aggSigOut.IsValid() && aggSigOut.Verify(aggPubKeyOut, blockHash, aggDst);
}

int GetTargetQuorumSize(int nHeight, size_t nActiveOnChainMNs)
{
    if (nHeight <= 0) {
        return 5;
    }
    if (nActiveOnChainMNs >= 100) return 15;
    if (nActiveOnChainMNs >= 50)  return 11;
    if (nActiveOnChainMNs >= 25)  return 9;
    return 5;
}

std::vector<CQuorumMember> ElectQuorumMembers(int nHeight, int nQuorumSize, const CBlockIndex* pindexPrev, const CCoinsViewCache* pView)
{
    std::vector<CQuorumMember> members;

    // Get rolling seed (prev block seed or epoch seed if rotation is active)
    uint256 seed;
    bool fRotationActive = false;
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        fRotationActive = (nHeight >= 200);
    } else if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        fRotationActive = (nHeight >= 200);
    } else { // regtest
        fRotationActive = (nHeight >= 800);
    }

    int seedHeight = nHeight - 1;
    if (fRotationActive) {
        int dkgInterval = (Params().NetworkID() == CBaseChainParams::MAIN) ? 100 : 10;
        seedHeight = (nHeight / dkgInterval) * dkgInterval - 1;
    }

    const CBlockIndex* pSnapshotIdx = nullptr;
    {
        LOCK(cs_main);
        const CBlockIndex* pSeedIndex = nullptr;
        if (pindexPrev) {
            if (pindexPrev->nHeight == seedHeight) {
                pSeedIndex = pindexPrev;
            } else if (pindexPrev->nHeight > seedHeight) {
                pSeedIndex = pindexPrev->GetAncestor(seedHeight);
            }
        }
        if (!pSeedIndex && chainActive.Tip() && seedHeight <= chainActive.Height()) {
            pSeedIndex = chainActive[seedHeight];
        }
        if (pSeedIndex) {
            seed = GetAdamSeed(pSeedIndex);
        }
        if (seedHeight > 1) {
            const int snapHeight = seedHeight - 1;
            if (pindexPrev && pindexPrev->nHeight >= snapHeight) {
                pSnapshotIdx = pindexPrev->GetAncestor(snapHeight);
            } else if (chainActive.Tip() && snapHeight <= chainActive.Height()) {
                pSnapshotIdx = chainActive[snapHeight];
            }
        }
    }

    struct QuorumCandidate {
        COutPoint collateralOutpoint;
        CPubKey pubKeyMasternode;
        uint256 proTxHash;
    };

    // Get active masternodes with strict on-chain 4,200 KRISTA collateral + authorization verification
    std::vector<QuorumCandidate> enabledMns;
    dip3::CDeterministicMNList mnList;
    const bool fUseDip3 = (deterministicMNManager != nullptr);
    {
        LOCK(cs_main);
        int refHeight = (nHeight > 0) ? (nHeight - 1) : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
        std::set<COutPoint> seenOutpoints;
        if (fUseDip3) {
            const CBlockIndex* pListIdx = pindexPrev;
            if (!pListIdx && refHeight >= 0 && chainActive.Tip() && refHeight <= chainActive.Height()) {
                pListIdx = chainActive[refHeight];
            }
            mnList = pListIdx
                ? deterministicMNManager->GetListForBlock(pListIdx)
                : deterministicMNManager->GetListAtChainTip();

            bool fHasSnapshot = false;
            dip3::CDeterministicMNList snapshotList;
            if (pSnapshotIdx && pSnapshotIdx->nHeight > 0 &&
                deterministicMNManager->HasListForBlock(pSnapshotIdx->GetBlockHash())) {
                snapshotList = deterministicMNManager->GetListForBlock(pSnapshotIdx);
                fHasSnapshot = true;
            }

            for (const auto& dmn : mnList.GetValidMNs()) {
                if (fHasSnapshot) {
                    const dip3::CDeterministicMN* snapDmn = snapshotList.GetMN(dmn.proTxHash);
                    if (!snapDmn || !snapDmn->state.IsValid()) {
                        continue;
                    }
                }
                if (dmn.state.pubKeyOwner.IsValid() && seenOutpoints.insert(dmn.collateralOutpoint).second) {
                    enabledMns.push_back(QuorumCandidate{dmn.collateralOutpoint, dmn.state.pubKeyOwner, dmn.proTxHash});
                }
            }
        } else {
            for (const auto& kv : mapMasternodeCollateralOutpoints) {
                if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, refHeight, 2880)) {
                    if (!seenOutpoints.count(kv.second) &&
                        IsValidMasternodeCollateralAndAuth(kv.first, kv.second, refHeight, nullptr, pView)) {
                        enabledMns.push_back(QuorumCandidate{kv.second, kv.first, UINT256_ZERO});
                        seenOutpoints.insert(kv.second);
                    }
                }
            }
        }
    }

    std::sort(enabledMns.begin(), enabledMns.end(), [](const QuorumCandidate& a, const QuorumCandidate& b) {
        if (a.collateralOutpoint != b.collateralOutpoint) {
            return a.collateralOutpoint < b.collateralOutpoint;
        }
        if (a.proTxHash != b.proTxHash) {
            return a.proTxHash < b.proTxHash;
        }
        return a.pubKeyMasternode < b.pubKeyMasternode;
    });

    const size_t nVerifiedOnChainMNs = enabledMns.size();
    const int nEffectiveQuorumSize = (nQuorumSize > 0) ? nQuorumSize : GetTargetQuorumSize(nHeight, nVerifiedOnChainMNs);

    auto scoreCandidate = [&](const COutPoint& prevout, const uint256& proTxHash) -> uint256 {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << seed;
        ss << prevout.hash;
        ss << prevout.n;
        ss << proTxHash;
        return ss.GetHash();
    };

    // Sort verified Active Masternodes deterministically by score(seed, collateral outpoint, proTxHash)
    std::vector<std::pair<uint256, QuorumCandidate>> sortedMns;
    sortedMns.reserve(enabledMns.size());
    for (const auto& cand : enabledMns) {
        sortedMns.push_back({scoreCandidate(cand.collateralOutpoint, cand.proTxHash), cand});
    }
    std::sort(sortedMns.begin(), sortedMns.end(), [](const std::pair<uint256, QuorumCandidate>& a, const std::pair<uint256, QuorumCandidate>& b) {
        return a.first < b.first;
    });

    std::set<CPubKey> selectedPubKeys;
    std::set<CBLSPubKey> seenQuorumBlsKeys;
    {
        for (size_t i = 0; i < sortedMns.size() && members.size() < (size_t)nEffectiveQuorumSize; ++i) {
            const QuorumCandidate& mn = sortedMns[i].second;
            CBLSPubKey candBls;
            if (fUseDip3) {
                const dip3::CDeterministicMN* dmn = mnList.GetMNByOwner(mn.pubKeyMasternode);
                if (dmn && dmn->state.blsPubKeyOperator.IsValid()) {
                    candBls = dmn->state.blsPubKeyOperator;
                }
            }
            if (!candBls.IsValid() && !fUseDip3) {
                GetPinnedBLSPubKey(mn.pubKeyMasternode, candBls, MakeActiveChainPinPredicate(pindexPrev));
            }
            if (candBls.IsValid() && !seenQuorumBlsKeys.insert(candBls).second) {
                continue;
            }
            members.push_back(CQuorumMember(mn.collateralOutpoint, mn.pubKeyMasternode));
            selectedPubKeys.insert(mn.pubKeyMasternode);
        }
    }

    // Bootstrap fallback when < 5 verified Active Masternodes exist:
    // Keep all 1..4 real Active Masternodes guaranteed in the quorum and fill ONLY the remaining slots
    // using quota-isolated validator candidates (at most 1 Coin-Lock + up to 3 primary Funded PoW-Locks,
    // followed by Open PoW overflow and last-resort Coin-Lock fallback), preventing multi-lock quorum capture.
    if (nVerifiedOnChainMNs < 5 && members.size() < (size_t)nEffectiveQuorumSize) {
        std::set<CPubKey> mnPoolKeys, coinLockKeys, fundedPoWKeys, bootstrapKeys;
        std::vector<CPubKey> pool = GetAdamMinerPool(nHeight - 1, pView, &mnPoolKeys, &coinLockKeys, nullptr, nullptr, &fundedPoWKeys, &bootstrapKeys);

        std::vector<std::pair<uint256, CQuorumMember>> vCoinLockRanked;
        std::vector<std::pair<uint256, CQuorumMember>> vFundedPoWRanked;
        std::vector<std::pair<uint256, CQuorumMember>> vBootstrapRanked;
        std::vector<std::pair<uint256, CQuorumMember>> vZeroOrUntieredRanked;

        for (size_t i = 0; i < pool.size(); ++i) {
            const CPubKey& pk = pool[i];
            if (selectedPubKeys.count(pk) || mnPoolKeys.count(pk)) continue;
            const COutPoint synthOut(Hash(pk.begin(), pk.end()), static_cast<uint32_t>(i));
            const std::pair<uint256, CQuorumMember> item(scoreCandidate(synthOut, UINT256_ZERO), CQuorumMember(synthOut, pk));
            if (coinLockKeys.count(pk)) {
                vCoinLockRanked.push_back(item);
            } else if (fundedPoWKeys.count(pk)) {
                vFundedPoWRanked.push_back(item);
            } else if (bootstrapKeys.count(pk)) {
                vBootstrapRanked.push_back(item);
            } else {
                vZeroOrUntieredRanked.push_back(item);
            }
        }

        auto cmpScore = [](const std::pair<uint256, CQuorumMember>& a, const std::pair<uint256, CQuorumMember>& b) {
            return a.first < b.first;
        };
        std::sort(vCoinLockRanked.begin(), vCoinLockRanked.end(), cmpScore);
        std::sort(vFundedPoWRanked.begin(), vFundedPoWRanked.end(), cmpScore);
        std::sort(vBootstrapRanked.begin(), vBootstrapRanked.end(), cmpScore);
        std::sort(vZeroOrUntieredRanked.begin(), vZeroOrUntieredRanked.end(), cmpScore);

        // Primary quota-capped funded fallback tier: max 1 Coin-Lock + max 3 Funded PoW-Locks, ranked fairly by score
        std::vector<std::pair<uint256, CQuorumMember>> vPrimaryQuotaFunded;
        const size_t nClPrimary = std::min<size_t>(vCoinLockRanked.size(), ADAM_COIN_LOCK_TRACK_QUOTA);
        const size_t nFpPrimary = std::min<size_t>(vFundedPoWRanked.size(), ADAM_OPEN_POW_TRACK_QUOTA);
        for (size_t i = 0; i < nClPrimary; ++i) vPrimaryQuotaFunded.push_back(vCoinLockRanked[i]);
        for (size_t i = 0; i < nFpPrimary; ++i) vPrimaryQuotaFunded.push_back(vFundedPoWRanked[i]);
        std::sort(vPrimaryQuotaFunded.begin(), vPrimaryQuotaFunded.end(), cmpScore);

        auto appendUpToQuorum = [&](const std::vector<std::pair<uint256, CQuorumMember>>& vec, size_t startIdx) {
            for (size_t i = startIdx; i < vec.size() && members.size() < (size_t)nEffectiveQuorumSize; ++i) {
                const CPubKey& candPk = vec[i].second.pubKeyMasternode;
                if (selectedPubKeys.count(candPk)) {
                    continue;
                }
                CBLSPubKey candBls;
                if (GetPinnedBLSPubKey(candPk, candBls, MakeActiveChainPinPredicate(pindexPrev)) && candBls.IsValid() && !seenQuorumBlsKeys.insert(candBls).second) {
                    continue;
                }
                if (selectedPubKeys.insert(candPk).second) {
                    members.push_back(vec[i].second);
                }
            }
        };

        appendUpToQuorum(vPrimaryQuotaFunded, 0);
        appendUpToQuorum(vFundedPoWRanked, nFpPrimary);
        appendUpToQuorum(vBootstrapRanked, 0);
        appendUpToQuorum(vZeroOrUntieredRanked, 0);
        appendUpToQuorum(vCoinLockRanked, nClPrimary);
    }

    LogPrint(BCLog::MASTERNODE, "%s: Elected %d members for quorum at height %d (verified active masternodes: %d)\n",
             __func__, members.size(), nHeight, nVerifiedOnChainMNs);

    return members;
}

CQuorum RunDKG(int nHeight, const CBlockIndex* pindexPrev, const CCoinsViewCache* pView)
{
    CQuorum quorum;
    quorum.nHeight = nHeight;
    
    uint256 blockHash;
    if (pindexPrev) {
        if (pindexPrev->nHeight == nHeight) {
            quorum.quorumHash = pindexPrev->GetBlockHash();
        } else if (pindexPrev->nHeight > nHeight) {
            const CBlockIndex* pAncestor = pindexPrev->GetAncestor(nHeight);
            if (pAncestor) quorum.quorumHash = pAncestor->GetBlockHash();
        }
    }
    if (quorum.quorumHash.IsNull()) {
        if (GetBlockHash(blockHash, nHeight)) {
            quorum.quorumHash = blockHash;
        }
    }
    
    quorum.members = ElectQuorumMembers(nHeight, 0, pindexPrev, pView); // Dynamically scaled quorum size (5 -> 9 -> 11 -> 15)
    if (!quorum.members.empty()) {
        // Primary leader/representative key of the deterministically ranked BFT quorum
        quorum.quorumPubKey = quorum.members[0].pubKeyMasternode;
    }
    return quorum;
}

CQuorum GetActiveQuorum(int nHeight, const CBlockIndex* pindexPrev, const CCoinsViewCache* pView)
{
    // Check if Quorum Rotation is active
    bool fRotationActive = false;
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        fRotationActive = (nHeight >= 200);
    } else if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        fRotationActive = (nHeight >= 200);
    } else { // regtest
        fRotationActive = (nHeight >= 800);
    }

    int nTargetHeight = nHeight;
    if (!fRotationActive) {
        nTargetHeight = (nHeight / 100) * 100;
        if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
            nTargetHeight = (nHeight / 10) * 10;
        }
        int nPoMBLHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight;
        if (nTargetHeight < nPoMBLHeight) {
            nTargetHeight = nPoMBLHeight;
        }
    }

    bool fCanLookupCache = false;
    bool fCanPopulateCache = false;
    uint256 prevBlockHash;
    if (pindexPrev) {
        prevBlockHash = pindexPrev->GetBlockHash();
        const bool fViewMatchesPrev = !pView ||
            pView == pcoinsTip ||
            (!pView->GetBestBlock().IsNull() && pView->GetBestBlock() == prevBlockHash);
        if (fViewMatchesPrev) {
            bool fOnActiveChain = false;
            bool fCoinsTipAtPrev = false;
            {
                LOCK(cs_main);
                fOnActiveChain = chainActive.Contains(pindexPrev);
                fCoinsTipAtPrev = (pcoinsTip && !pcoinsTip->GetBestBlock().IsNull() && pcoinsTip->GetBestBlock() == prevBlockHash);
            }
            const bool fDip3Required = deterministicMNManager && pindexPrev->nHeight > 0;
            const bool fDip3Ready = !fDip3Required || deterministicMNManager->HasListForBlock(prevBlockHash);
            fCanLookupCache = fOnActiveChain && fDip3Ready && !prevBlockHash.IsNull();
            fCanPopulateCache = fCanLookupCache && fCoinsTipAtPrev;
        }
    }

    if (fCanLookupCache) {
        const auto cacheKey = std::make_pair(nTargetHeight, prevBlockHash);
        {
            LOCK(cs_quorum_cache);
            const uint64_t pingGen = g_nMasternodePingStateGen.load();
            if (cachedQuorumPingGen != pingGen) {
                mapQuorumCache.clear();
                cachedQuorumPingGen = pingGen;
            }
            auto it = mapQuorumCache.find(cacheKey);
            if (it != mapQuorumCache.end()) {
                return it->second;
            }
        }
        CQuorum q = RunDKG(nTargetHeight, pindexPrev, pView);
        if (fCanPopulateCache && !q.members.empty()) {
            LOCK(cs_quorum_cache);
            const uint64_t pingGen = g_nMasternodePingStateGen.load();
            if (cachedQuorumPingGen != pingGen) {
                mapQuorumCache.clear();
                cachedQuorumPingGen = pingGen;
            }
            if (mapQuorumCache.size() >= 64) {
                mapQuorumCache.erase(mapQuorumCache.begin());
            }
            mapQuorumCache[cacheKey] = q;
        }
        return q;
    }

    return RunDKG(nTargetHeight, pindexPrev, pView);
}

bool GetMasternodePrivKey(const CPubKey& pubKey, CKey& key)
{
    // 1. Try to find key in local active masternode list or config
    for (const auto& activeMasternode : amnodeman.GetActiveMasternodesCopy()) {
        if (ComparePubKeys(activeMasternode.pubKeyMasternode, pubKey)) {
            CKey k;
            CPubKey pk = pubKey;
            if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, k, pk)) {
                key = k;
                return true;
            }
        }
    }
    std::string strMnPrivKey = GetArg("-masternodeprivkey", "");
    if (!strMnPrivKey.empty()) {
        CKey k;
        CPubKey pk;
        if (CMessageSigner::GetKeysFromSecret(strMnPrivKey, k, pk)) {
            if (ComparePubKeys(pk, pubKey)) {
                key = k;
                return true;
            }
        }
    }
    // NOTE: Cold controller wallet metadata is intentionally NOT consulted here. LLMQ /
    // coordinator signing must happen exclusively on the hot node (activemasternode.conf), otherwise the
    // cold wallet would impersonate its masternodes and mask hot-node downtime.

    // 2. Try to find the key in the wallet
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        if (pwalletMain->GetKey(pubKey.GetID(), key)) {
            return true;
        }
        CKeyID compID = GetCompressedKeyID(pubKey);
        if (compID != pubKey.GetID() && pwalletMain->GetKey(compID, key)) {
            return true;
        }
    }
#endif



    // 3. For Regtest only, check deterministic keys or derive from public key hash
    if (Params().IsRegTestNet()) {
        for (int i = 0; i < 20; ++i) {
            if (ComparePubKeys(GetAdamDeterministicPubKey(i), pubKey)) {
                key = GetAdamDeterministicKey(i);
                return true;
            }
        }
        uint256 hash = Hash(pubKey.begin(), pubKey.end());
        key.Set(hash.begin(), hash.end(), true);
        return true;
    }

    return false;
}

namespace {
struct SignedProposal {
    uint256 blockHash;
    CPubKey coordinator;
    int64_t nTime{0};
    std::map<CPubKey, uint256> mapCoordBlockHash;
    std::set<uint256> rejectedBlocks;
};
RecursiveMutex cs_signed_proposals;
std::map<uint256, SignedProposal> mapSignedProposalByPrev;
} // namespace

bool AllowQuorumSignature(const uint256& prevHash, const CPubKey& coordinator, const uint256& blockHash)
{
    const int64_t now = GetTime();
    uint256 prevHashCandidate;
    CPubKey prevCoordinator;
    int64_t prevTime = 0;

    {
        LOCK(cs_signed_proposals);

        if (mapSignedProposalByPrev.size() > 1000) {
            for (auto it = mapSignedProposalByPrev.begin(); it != mapSignedProposalByPrev.end();) {
                if (now - it->second.nTime > 3600) it = mapSignedProposalByPrev.erase(it);
                else ++it;
            }
        }
        while (mapSignedProposalByPrev.size() >= 2000) {
            auto itOldest = mapSignedProposalByPrev.begin();
            for (auto it = mapSignedProposalByPrev.begin(); it != mapSignedProposalByPrev.end(); ++it) {
                if (it->second.nTime < itOldest->second.nTime) {
                    itOldest = it;
                }
            }
            mapSignedProposalByPrev.erase(itOldest);
        }

        auto it = mapSignedProposalByPrev.find(prevHash);
        if (it == mapSignedProposalByPrev.end()) {
            SignedProposal sp;
            sp.blockHash = blockHash;
            sp.coordinator = coordinator;
            sp.nTime = now;
            sp.mapCoordBlockHash.emplace(coordinator, blockHash);
            mapSignedProposalByPrev.emplace(prevHash, std::move(sp));
            return true;
        }
        SignedProposal& prev = it->second;
        auto itCoord = prev.mapCoordBlockHash.find(coordinator);
        if (itCoord != prev.mapCoordBlockHash.end() && itCoord->second != blockHash) {
            LogPrintf("%s: Refusing equivocating block %s from coordinator %s on parent %s: coordinator already signed different block %s\n",
                __func__, blockHash.ToString(), coordinator.GetID().ToString(), prevHash.ToString(), itCoord->second.ToString());
            return false;
        }
        if (prev.rejectedBlocks.count(blockHash) > 0) {
            LogPrintf("%s: Refusing displaced block %s from coordinator %s on parent %s\n",
                __func__, blockHash.ToString(), coordinator.GetID().ToString(), prevHash.ToString());
            return false;
        }
        if (prev.blockHash == blockHash) {
            prev.mapCoordBlockHash.emplace(coordinator, blockHash);
            return true; // re-broadcast / duplicate of the current signed block
        }
        if (coordinator == prev.coordinator || now - prev.nTime < QUORUM_EQUIVOCATION_GUARD_SECS) {
            LogPrintf("%s: Refusing to sign block %s from coordinator %s on parent %s: already signed block %s from coordinator %s %ds ago\n",
                __func__, blockHash.ToString(), coordinator.GetID().ToString(), prevHash.ToString(),
                prev.blockHash.ToString(), prev.coordinator.GetID().ToString(), (int)(now - prev.nTime));
            return false;
        }
        prevHashCandidate = prev.blockHash;
        prevCoordinator = prev.coordinator;
        prevTime = prev.nTime;
    }

    // Outside cs_signed_proposals: check whether the prior proposal already received threshold shares or entered mapBlockIndex.
    bool fPrevLocked = false;
    CBlockIndex* pPrevIdx = nullptr;
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(prevHashCandidate) > 0) {
            fPrevLocked = true;
        } else {
            auto miPrev = mapBlockIndex.find(prevHash);
            if (miPrev != mapBlockIndex.end()) {
                pPrevIdx = miPrev->second;
            }
        }
    }
    if (!fPrevLocked) {
        size_t nShares = 0;
        {
            LOCK(cs_quorum_sigs);
            auto itSigs = mapQuorumBlockSigs.find(prevHashCandidate);
            if (itSigs != mapQuorumBlockSigs.end()) {
                nShares = itSigs->second.size();
            }
        }
        if (nShares > 0) {
            int nEvalHeight = pPrevIdx ? (pPrevIdx->nHeight + 1) : 1;
            size_t nQuorumSize = 5;
            if (pPrevIdx) {
                const CQuorum q = GetActiveQuorum(nEvalHeight, pPrevIdx);
                if (!q.members.empty()) {
                    nQuorumSize = q.members.size();
                }
            }
            if (nShares >= GetQuorumThreshold(nQuorumSize)) {
                fPrevLocked = true;
            }
        }
    }

    LOCK(cs_signed_proposals);
    auto it = mapSignedProposalByPrev.find(prevHash);
    if (it == mapSignedProposalByPrev.end()) {
        SignedProposal sp;
        sp.blockHash = blockHash;
        sp.coordinator = coordinator;
        sp.nTime = now;
        sp.mapCoordBlockHash.emplace(coordinator, blockHash);
        mapSignedProposalByPrev.emplace(prevHash, std::move(sp));
        return true;
    }
    SignedProposal& prev = it->second;
    if (fPrevLocked) {
        LogPrintf("%s: Refusing to sign block %s from coordinator %s on parent %s: previous block %s is already locked in block index or quorum shares\n",
            __func__, blockHash.ToString(), coordinator.GetID().ToString(), prevHash.ToString(), prevHashCandidate.ToString());
        return false;
    }
    if (prev.blockHash != prevHashCandidate) {
        return prev.blockHash == blockHash && !prev.rejectedBlocks.count(blockHash);
    }
    if (prev.rejectedBlocks.count(blockHash) > 0) {
        return false;
    }
    if (coordinator != prev.coordinator && now - prev.nTime >= QUORUM_EQUIVOCATION_GUARD_SECS) {
        prev.rejectedBlocks.insert(prev.blockHash);
        prev.blockHash = blockHash;
        prev.coordinator = coordinator;
        prev.nTime = now;
        prev.mapCoordBlockHash[coordinator] = blockHash;
        return true;
    }
    return false;
}

} // namespace llmq
