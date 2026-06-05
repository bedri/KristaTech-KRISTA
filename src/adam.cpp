// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "primitives/block.h"
#include "hash.h"
#include "masternodeman.h"
#include "util.h"
#include "sync.h"
#include "main.h"
#include <algorithm>
#include <map>

RecursiveMutex cs_adam_seeds;
std::map<uint256, uint256> mapAdamSeeds;

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

uint256 GetAdamSeed(const CBlockIndex* pindex) {
    if (pindex == nullptr) return uint256();
    
    const Consensus::Params& consensus = Params().GetConsensus();
    if (pindex->nHeight < consensus.nAdamHeight) {
        return pindex->GetBlockHash();
    }
    
    {
        LOCK(cs_adam_seeds);
        auto it = mapAdamSeeds.find(pindex->GetBlockHash());
        if (it != mapAdamSeeds.end()) {
            return it->second;
        }
    }
    
    // Fallback: load block from disk to calculate rolling seed recursively
    uint256 prevSeed = GetAdamSeed(pindex->pprev);
    
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex)) {
        LogPrintf("GetAdamSeed: Failed to read block from disk at height %d\n", pindex->nHeight);
        return prevSeed;
    }
    
    CHashWriter ss(SER_GETHASH, 0);
    ss << prevSeed;
    ss << block.vAdamVRFProof;
    uint256 newSeed = ss.GetHash();
    
    {
        LOCK(cs_adam_seeds);
        mapAdamSeeds[pindex->GetBlockHash()] = newSeed;
    }
    
    return newSeed;
}

bool SelectAdamNodes(const uint256& hashAdamSeed, const Consensus::Params& params, std::vector<CPubKey>& vSelectedMinersOut, CPubKey& coordinatorOut) {
    std::vector<CPubKey> pool = GetAdamMinerPool();
    int minCount = params.nAdamMinersCount;
    if (pool.size() < (size_t)minCount) {
        return false;
    }
    
    std::vector<MinerRank> rankedMiners;
    for (const auto& key : pool) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << hashAdamSeed;
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

bool VerifyAdamSolution(const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits) {
    if (vchSolution.empty()) return false;
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        
        // Calculate hash of the puzzle
        CHashWriter hw(SER_GETHASH, 0);
        hw << hashAdamSeed;
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

bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey) {
    if (vchProof.empty()) return false;
    return coordinatorKey.Verify(prevSeed, vchProof);
}

bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey) {
    if (block.vAdamCoordinatorSig.empty()) {
        return false;
    }
    return coordinatorKey.Verify(block.GetHash(), block.vAdamCoordinatorSig);
}

