// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq.h"
#include "main.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "adam.h"
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

bool CQuorumSignature::Verify(const CQuorum& quorum) const
{
    if (quorum.members.empty() || signatures.empty() || signatures.size() > 32) return false;

    // Threshold is majority (at least 3 out of 5 signatures) for BFT security
    size_t threshold = (quorum.members.size() / 2 + 1);
    if (threshold < 2) threshold = 2;
    if (quorum.members.size() < threshold) threshold = quorum.members.size();

    size_t validSigsCount = 0;
    std::set<COutPoint> verifiedMembers;
    std::vector<CBLSPubKey> vBlsPubKeys;
    std::vector<CBLSSignature> vBlsSigs;

    for (const auto& sigPair : signatures) {
        const COutPoint& memberOutpoint = sigPair.first;
        const std::vector<unsigned char>& sig = sigPair.second;
        if (sig.empty() || sig.size() > 512) return false;

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

        // Verify hybrid BLS12-381 + secp256k1 signature (CBLSSignedData)
        bool fSigValid = false;
        try {
            CDataStream ss(sig, SER_NETWORK, PROTOCOL_VERSION);
            CBLSSignedData signedData;
            ss >> signedData;
            if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
                CBLSPubKey pinnedBlsPubKey;
                if (GetPinnedBLSPubKey(memberPubKey, pinnedBlsPubKey) && signedData.blsPubKey != pinnedBlsPubKey) {
                    return false;
                }
                uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                if (memberPubKey.Verify(hashPubKey, signedData.ecdsaSig) &&
                    signedData.blsSig.Verify(signedData.blsPubKey, blockHash)) {
                    validSigsCount++;
                    verifiedMembers.insert(memberOutpoint);
                    vBlsPubKeys.push_back(signedData.blsPubKey);
                    vBlsSigs.push_back(signedData.blsSig);
                    fSigValid = true;
                }
            }
        } catch (...) {}
        if (!fSigValid) return false;
    }

    if (validSigsCount < threshold || vBlsSigs.size() < threshold) {
        return false;
    }

    // Verify aggregated BLS12-381 signature across all signing quorum members
    CBLSPubKey aggPubKey = CBLSPubKey::Aggregate(vBlsPubKeys);
    CBLSSignature aggSig = CBLSSignature::Aggregate(vBlsSigs);
    if (!aggPubKey.IsValid() || !aggSig.IsValid() || !aggSig.Verify(aggPubKey, blockHash)) {
        return false;
    }

    return true;
}

bool CQuorumSignature::AggregateBLSSignatures(const CQuorum& quorum, CBLSPubKey& aggPubKeyOut, CBLSSignature& aggSigOut) const
{
    aggPubKeyOut.Reset();
    aggSigOut.Reset();
    if (quorum.members.empty() || signatures.empty() || signatures.size() > 32) return false;

    std::set<COutPoint> verifiedMembers;
    std::vector<CBLSPubKey> vBlsPubKeys;
    std::vector<CBLSSignature> vBlsSigs;

    for (const auto& sigPair : signatures) {
        const COutPoint& memberOutpoint = sigPair.first;
        const std::vector<unsigned char>& sig = sigPair.second;
        if (sig.empty() || sig.size() > 512) return false;
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

        bool fSigValid = false;
        try {
            CDataStream ss(sig, SER_NETWORK, PROTOCOL_VERSION);
            CBLSSignedData signedData;
            ss >> signedData;
            if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
                CBLSPubKey pinnedBlsPubKey;
                if (GetPinnedBLSPubKey(memberPubKey, pinnedBlsPubKey) && signedData.blsPubKey != pinnedBlsPubKey) {
                    return false;
                }
                uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                if (memberPubKey.Verify(hashPubKey, signedData.ecdsaSig) &&
                    signedData.blsSig.Verify(signedData.blsPubKey, blockHash)) {
                    verifiedMembers.insert(memberOutpoint);
                    vBlsPubKeys.push_back(signedData.blsPubKey);
                    vBlsSigs.push_back(signedData.blsSig);
                    fSigValid = true;
                }
            }
        } catch (...) {}
        if (!fSigValid) return false;
    }

    size_t threshold = (quorum.members.size() / 2 + 1);
    if (threshold < 2) threshold = 2;
    if (quorum.members.size() < threshold) threshold = quorum.members.size();

    if (vBlsSigs.size() < threshold) {
        return false;
    }

    aggPubKeyOut = CBLSPubKey::Aggregate(vBlsPubKeys);
    aggSigOut = CBLSSignature::Aggregate(vBlsSigs);
    return aggPubKeyOut.IsValid() && aggSigOut.IsValid() && aggSigOut.Verify(aggPubKeyOut, blockHash);
}

std::vector<CQuorumMember> ElectQuorumMembers(int nHeight, int nQuorumSize, const CBlockIndex* pindexPrev)
{
    std::vector<CQuorumMember> members;
    
    // Get active masternodes with strict on-chain 4,200 KRISTA collateral + authorization verification
    std::vector<CMasternode> enabledMns;
    if (!Params().IsRegTestNet()) {
        LOCK(cs_main);
        int refHeight = (nHeight > 0) ? (nHeight - 1) : (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
        std::set<COutPoint> seenOutpoints;
        for (const auto& kv : mapMasternodeCollateralOutpoints) {
            if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, refHeight, 2880)) {
                if (!seenOutpoints.count(kv.second) &&
                    IsValidMasternodeCollateralAndAuth(kv.first, kv.second, refHeight)) {
                    CMasternode mn;
                    mn.pubKeyMasternode = kv.first;
                    mn.vin.prevout = kv.second;
                    enabledMns.push_back(mn);
                    seenOutpoints.insert(kv.second);
                }
            }
        }
    }

    std::sort(enabledMns.begin(), enabledMns.end(), [](const CMasternode& a, const CMasternode& b) {
        if (a.vin.prevout != b.vin.prevout) {
            return a.vin.prevout < b.vin.prevout;
        }
        return a.pubKeyMasternode < b.pubKeyMasternode;
    });
    
    if (enabledMns.size() < 5) {
        enabledMns.clear();
        std::vector<CPubKey> pool = GetAdamMinerPool(nHeight - 1);
        enabledMns.reserve(pool.size());
        for (size_t i = 0; i < pool.size(); ++i) {
            CMasternode mn;
            mn.pubKeyMasternode = pool[i];
            mn.vin.prevout = COutPoint(Hash(pool[i].begin(), pool[i].end()), i);
            enabledMns.push_back(mn);
        }
    }
    
    // Get rolling seed (prev block seed or epoch seed if rotation is active)
    uint256 seed;
    bool fRotationActive = false;
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        fRotationActive = (nHeight >= 1400);
    } else if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        fRotationActive = (nHeight >= 1000);
    } else { // regtest
        fRotationActive = (nHeight >= 800);
    }

    {
        LOCK(cs_main);
        int seedHeight = nHeight - 1;
        if (fRotationActive) {
            int dkgInterval = (Params().NetworkID() == CBaseChainParams::MAIN) ? 100 : 10;
            seedHeight = (nHeight / dkgInterval) * dkgInterval - 1;
        }
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
    }
    
    // Sort masternodes deterministically by a score based on seed and outpoint
    std::vector<std::pair<uint256, CMasternode>> sortedMns;
    for (auto& mn : enabledMns) {
        CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
        ss << seed;
        ss << mn.vin.prevout.hash;
        ss << mn.vin.prevout.n;
        sortedMns.push_back({ss.GetHash(), mn});
    }
    
    std::sort(sortedMns.begin(), sortedMns.end(), [](const std::pair<uint256, CMasternode>& a, const std::pair<uint256, CMasternode>& b) {
        return a.first < b.first;
    });
    
    // Standard DKG behavior (top N) - Ensures all nodes agree on identical quorum members
    size_t count = std::min((size_t)nQuorumSize, sortedMns.size());
    for (size_t i = 0; i < count; ++i) {
        const CMasternode& mn = sortedMns[i].second;
        members.push_back(CQuorumMember(mn.vin.prevout, mn.pubKeyMasternode));
    }
    
    LogPrint(BCLog::MASTERNODE, "%s: Elected %d members for quorum at height %d (out of %d total active masternodes)\n", 
             __func__, members.size(), nHeight, enabledMns.size());
    
    return members;
}

CQuorum RunDKG(int nHeight, const CBlockIndex* pindexPrev)
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
    
    quorum.members = ElectQuorumMembers(nHeight, 5, pindexPrev); // 5-member quorum size
    if (!quorum.members.empty()) {
        // Primary leader/representative key of the deterministically ranked 5-member BFT quorum
        quorum.quorumPubKey = quorum.members[0].pubKeyMasternode;
    }
    return quorum;
}

CQuorum GetActiveQuorum(int nHeight, const CBlockIndex* pindexPrev)
{
    // Check if Quorum Rotation is active
    bool fRotationActive = false;
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        fRotationActive = (nHeight >= 1400);
    } else if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        fRotationActive = (nHeight >= 1000);
    } else { // regtest
        fRotationActive = (nHeight >= 800);
    }

    if (fRotationActive) {
        return RunDKG(nHeight, pindexPrev);
    }

    // Quorum changes every 100 blocks
    int nDkgHeight = (nHeight / 100) * 100;
    if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
        nDkgHeight = (nHeight / 10) * 10;
    }
    int nPoMBLHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight;
    if (nDkgHeight < nPoMBLHeight) {
        nDkgHeight = nPoMBLHeight;
    }
    return RunDKG(nDkgHeight, pindexPrev);
}

bool GetMasternodePrivKey(const CPubKey& pubKey, CKey& key)
{
    // 1. Try to find key in local active masternode list or config
    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
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
    // NOTE: masternode.conf (masternodeConfig) is intentionally NOT consulted here. It belongs to the
    // cold controller wallet, which only needs the operator keys to broadcast start messages. LLMQ /
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
        for (int i = 0; i < 15; ++i) {
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
    int64_t nTime;
};
RecursiveMutex cs_signed_proposals;
std::map<uint256, SignedProposal> mapSignedProposalByPrev;
} // namespace

bool AllowQuorumSignature(const uint256& prevHash, const CPubKey& coordinator, const uint256& blockHash)
{
    const int64_t now = GetTime();
    LOCK(cs_signed_proposals);

    if (mapSignedProposalByPrev.size() > 1000) {
        for (auto it = mapSignedProposalByPrev.begin(); it != mapSignedProposalByPrev.end();) {
            if (now - it->second.nTime > 3600) it = mapSignedProposalByPrev.erase(it);
            else ++it;
        }
    }

    auto it = mapSignedProposalByPrev.find(prevHash);
    if (it == mapSignedProposalByPrev.end()) {
        mapSignedProposalByPrev[prevHash] = {blockHash, coordinator, now};
        return true;
    }
    SignedProposal& prev = it->second;
    if (prev.blockHash == blockHash) {
        return true; // re-broadcast / duplicate of the block we already signed
    }
    if (prev.coordinator == coordinator) {
        // Same coordinator refreshed its template (new tx / time); it cannot complete two competing blocks itself.
        prev.blockHash = blockHash;
        prev.nTime = now;
        return true;
    }
    if (now - prev.nTime >= QUORUM_EQUIVOCATION_GUARD_SECS) {
        // The earlier coordinator had a full rotation slice to finish; allow the next fallback coordinator.
        prev = {blockHash, coordinator, now};
        return true;
    }
    LogPrintf("%s: Refusing to sign block %s from coordinator %s on parent %s: already signed block %s from coordinator %s %ds ago\n",
        __func__, blockHash.ToString(), coordinator.GetID().ToString(), prevHash.ToString(),
        prev.blockHash.ToString(), prev.coordinator.GetID().ToString(), (int)(now - prev.nTime));
    return false;
}

} // namespace llmq
