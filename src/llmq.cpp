// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "llmq.h"
#include "masternodeman.h"
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

bool CQuorumSignature::Verify(const CQuorum& quorum) const
{
    if (quorum.members.empty()) return false;

    // Threshold is 75% of the quorum size (at least 2 signatures)
    size_t threshold = (quorum.members.size() * 3 / 4);
    if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
        if (quorum.nHeight >= Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight) {
            threshold = 2;
        } else {
            threshold = 0;
        }
    } else {
        if (threshold < 2) threshold = 2;
        if (quorum.members.size() < threshold) threshold = quorum.members.size();
    }

    size_t validSigsCount = 0;
    std::set<COutPoint> verifiedMembers;

    for (const auto& sigPair : signatures) {
        const COutPoint& memberOutpoint = sigPair.first;
        const std::vector<unsigned char>& sig = sigPair.second;

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

        if (!isMember) continue;
        if (verifiedMembers.count(memberOutpoint) > 0) continue; // Prevent double signing by same member

        // Verify signature of blockHash using memberPubKey
        if (memberPubKey.Verify(blockHash, sig)) {
            validSigsCount++;
            verifiedMembers.insert(memberOutpoint);
        }
    }

    return validSigsCount >= threshold;
}

std::vector<CQuorumMember> ElectQuorumMembers(int nHeight, int nQuorumSize)
{
    std::vector<CQuorumMember> members;
    
    // Get all enabled masternodes from mnodeman
    std::vector<CMasternode> vMns = mnodeman.GetFullMasternodeVector();
    std::vector<CMasternode> enabledMns;
    for (auto& mn : vMns) {
        if (mn.IsEnabled()) {
            LogPrintf("ElectQuorumMembers: MN ID: %s, IsEnabled: %d, NetworkID: %d\n", mn.pubKeyMasternode.GetID().ToString().c_str(), mn.IsEnabled(), (int)Params().NetworkID());
            enabledMns.push_back(mn);
        }
    }
    
    if (enabledMns.size() < 5) {
        enabledMns.clear();
        std::vector<CPubKey> pool = GetAdamMinerPool(nHeight - 1);
        std::vector<CMasternode> filteredPool;
        for (size_t i = 0; i < pool.size(); ++i) {
            CMasternode mn;
            mn.pubKeyMasternode = pool[i];
            mn.vin.prevout = COutPoint(Hash(pool[i].begin(), pool[i].end()), i);
            filteredPool.push_back(mn);
        }
        if (filteredPool.size() >= 5) {
            enabledMns = filteredPool;
        } else {
            for (size_t i = 0; i < pool.size(); ++i) {
                CMasternode mn;
                mn.pubKeyMasternode = pool[i];
                mn.vin.prevout = COutPoint(Hash(pool[i].begin(), pool[i].end()), i);
                enabledMns.push_back(mn);
            }
        }
    }
    
    // Get rolling seed (prev block seed)
    uint256 seed;
    {
        LOCK(cs_main);
        // Find the index of the block at nHeight - 1
        if (chainActive.Tip() && nHeight - 1 <= chainActive.Height()) {
            const CBlockIndex* pindexPrev = chainActive[nHeight - 1];
            if (pindexPrev) {
                seed = GetAdamSeed(pindexPrev);
            }
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
    
    // Select the top nQuorumSize members
    size_t count = std::min((size_t)nQuorumSize, sortedMns.size());
    for (size_t i = 0; i < count; ++i) {
        const CMasternode& mn = sortedMns[i].second;
        members.push_back(CQuorumMember(mn.vin.prevout, mn.pubKeyMasternode));
    }
    
    LogPrint(BCLog::MASTERNODE, "%s: Elected %d members for quorum at height %d (out of %d total active masternodes)\n", 
             __func__, members.size(), nHeight, enabledMns.size());
    
    return members;
}

CQuorum RunDKG(int nHeight)
{
    CQuorum quorum;
    quorum.nHeight = nHeight;
    
    uint256 blockHash;
    if (GetBlockHash(blockHash, nHeight)) {
        quorum.quorumHash = blockHash;
    }
    
    quorum.members = ElectQuorumMembers(nHeight, 5); // 5-member quorum size
    if (!quorum.members.empty()) {
        quorum.quorumPubKey = quorum.members[0].pubKeyMasternode; // Dummy key
    }
    return quorum;
}

CQuorum GetActiveQuorum(int nHeight)
{
    // Quorum changes every 100 blocks
    int nDkgHeight = (nHeight / 100) * 100;
    if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
        nDkgHeight = (nHeight / 10) * 10;
    }
    int nPoMBLHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight;
    if (nDkgHeight < nPoMBLHeight) {
        nDkgHeight = nPoMBLHeight;
    }
    return RunDKG(nDkgHeight);
}

bool GetMasternodePrivKey(const CPubKey& pubKey, CKey& key)
{
    // 1. Try to find key in local active masternode list
    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
        if (activeMasternode.pubKeyMasternode == pubKey) {
            CKey k;
            CPubKey pk = pubKey;
            if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, k, pk)) {
                key = k;
                return true;
            }
        }
    }

    // 2. Try to find the key in the wallet
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        if (pwalletMain->GetKey(pubKey.GetID(), key)) {
            return true;
        }
    }
#endif



    // 3. Check deterministic keys (seed-based)
    for (int i = 0; i < 15; ++i) {
        if (GetAdamDeterministicPubKey(i) == pubKey) {
            key = GetAdamDeterministicKey(i);
            return true;
        }
    }

    // 4. For Regtest, fallback to deterministic derivation from the public key hash
    if (Params().IsRegTestNet()) {
        uint256 hash = Hash(pubKey.begin(), pubKey.end());
        key.Set(hash.begin(), hash.end(), true);
        return true;
    }

    return false;
}

} // namespace llmq
