// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "primitives/block.h"
#include "hash.h"
#include "masternodeman.h"
#include "util.h"
#include <algorithm>

CKey GetAdamDeterministicKey(int index) {
    std::string seed = "adam_miner_seed_" + std::to_string(index);
    uint256 secret = Hash(seed.begin(), seed.end());
    CKey key;
    key.Set(secret.begin(), secret.end(), true);
    return key;
}

CPubKey GetAdamDeterministicPubKey(int index) {
    return GetAdamDeterministicKey(index).GetPubKey();
}

std::vector<CPubKey> GetAdamMinerPool() {
    std::vector<CPubKey> pool;
    // Try to get keys from masternode list
    std::vector<CMasternode> vMns = mnodeman.GetFullMasternodeVector();
    for (auto& mn : vMns) {
        if (mn.IsEnabled() && mn.pubKeyMasternode.IsValid()) {
            pool.push_back(mn.pubKeyMasternode);
        }
    }
    
    // If masternode list is too small, fallback/supplement with deterministic pool keys
    if (pool.size() < 10) {
        pool.clear();
        for (int i = 0; i < 10; ++i) {
            pool.push_back(GetAdamDeterministicPubKey(i));
        }
    }
    return pool;
}

struct MinerRank {
    CPubKey key;
    uint256 hash;
    
    bool operator<(const MinerRank& other) const {
        return hash < other.hash;
    }
};

bool SelectAdamNodes(const uint256& hashPrevBlock, const Consensus::Params& params, std::vector<CPubKey>& vSelectedMinersOut, CPubKey& coordinatorOut) {
    std::vector<CPubKey> pool = GetAdamMinerPool();
    int minCount = params.nAdamMinersCount;
    if (pool.size() < (size_t)minCount) {
        return false;
    }
    
    std::vector<MinerRank> rankedMiners;
    for (const auto& key : pool) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << hashPrevBlock;
        ss << key;
        MinerRank rank;
        rank.key = key;
        rank.hash = ss.GetHash();
        rankedMiners.push_back(rank);
    }
    
    std::sort(rankedMiners.begin(), rankedMiners.end());
    
    vSelectedMinersOut.clear();
    // Select first nAdamMinersCount as miners
    for (int i = 0; i < minCount && i < (int)rankedMiners.size(); ++i) {
        vSelectedMinersOut.push_back(rankedMiners[i].key);
    }
    
    // Select the next one as coordinator (minCount-th one, or if pool size is exactly minCount, wrap around to 0)
    if ((int)rankedMiners.size() > minCount) {
        coordinatorOut = rankedMiners[minCount].key;
    } else {
        coordinatorOut = rankedMiners[0].key;
    }
    
    return true;
}

#include "pow.h"
#include "streams.h"

bool VerifyAdamSolution(const uint256& hashPrevBlock, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits) {
    if (vchSolution.empty()) return false;
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        
        // Calculate hash of the puzzle
        CHashWriter hw(SER_GETHASH, 0);
        hw << hashPrevBlock;
        hw << minerKey;
        hw << nNonce;
        uint256 puzzleHash = hw.GetHash();
        
        // Verify miner's signature on the puzzle hash
        if (!minerKey.Verify(puzzleHash, vchSig)) {
            return false;
        }
        
        // Verify the difficulty
        if (!CheckProofOfWork(puzzleHash, nBits)) {
            return false;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey) {
    if (block.vAdamCoordinatorSig.empty()) {
        return false;
    }
    return coordinatorKey.Verify(block.GetHash(), block.vAdamCoordinatorSig);
}

