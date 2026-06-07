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

RecursiveMutex cs_adam_solutions;
std::map<uint256, std::map<CPubKey, std::vector<unsigned char>>> mapAdamSolutionsCache;

bool IsModelDActive(int nHeight) {
    if (Params().NetworkIDString() == "regtest") {
        return nHeight >= 200;
    }
    return nHeight >= 100001;
}

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

    // In regtest always use deterministic keys so the autoloop solver can sign.
    if (Params().NetworkIDString() == "regtest") {
        for (int i = 0; i < 15; ++i) {
            pool.push_back(GetAdamDeterministicPubKey(i));
        }
        return pool;
    }

    // Try to get keys from masternode list
    std::vector<CMasternode> vMns = mnodeman.GetFullMasternodeVector();
    for (auto& mn : vMns) {
        if (mn.IsEnabled() && mn.pubKeyMasternode.IsValid()) {
            pool.push_back(mn.pubKeyMasternode);
        }
    }
    
    // If masternode list is too small, fallback/supplement with deterministic pool keys
    if (pool.size() < 15) {
        pool.clear();
        for (int i = 0; i < 15; ++i) {
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

int GetAdamPuzzleAlgo(const uint256& hashAdamSeed, const CPubKey& minerKey) {
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
        default: return "DoubleSHA256";
    }
}

uint256 CalculateAdamPuzzleHash(int algoIndex, const unsigned char* pbegin, const unsigned char* pend) {
    size_t len = pend - pbegin;
    static const unsigned char pblank[1] = {};
    const void* data = (pbegin == pend ? static_cast<const void*>(pblank) : static_cast<const void*>(pbegin));
    
    switch (algoIndex) {
        case 0: { // blake
            sph_blake512_context ctx;
            uint512 hash;
            sph_blake512_init(&ctx);
            sph_blake512(&ctx, data, len);
            sph_blake512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 1: { // bmw
            sph_bmw512_context ctx;
            uint512 hash;
            sph_bmw512_init(&ctx);
            sph_bmw512(&ctx, data, len);
            sph_bmw512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 2: { // groestl
            sph_groestl512_context ctx;
            uint512 hash;
            sph_groestl512_init(&ctx);
            sph_groestl512(&ctx, data, len);
            sph_groestl512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 3: { // jh
            sph_jh512_context ctx;
            uint512 hash;
            sph_jh512_init(&ctx);
            sph_jh512(&ctx, data, len);
            sph_jh512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 4: { // keccak
            sph_keccak512_context ctx;
            uint512 hash;
            sph_keccak512_init(&ctx);
            sph_keccak512(&ctx, data, len);
            sph_keccak512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 5: { // skein
            sph_skein512_context ctx;
            uint512 hash;
            sph_skein512_init(&ctx);
            sph_skein512(&ctx, data, len);
            sph_skein512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 6: { // luffa
            sph_luffa512_context ctx;
            uint512 hash;
            sph_luffa512_init(&ctx);
            sph_luffa512(&ctx, data, len);
            sph_luffa512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 7: { // cubehash
            sph_cubehash512_context ctx;
            uint512 hash;
            sph_cubehash512_init(&ctx);
            sph_cubehash512(&ctx, data, len);
            sph_cubehash512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 8: { // shavite
            sph_shavite512_context ctx;
            uint512 hash;
            sph_shavite512_init(&ctx);
            sph_shavite512(&ctx, data, len);
            sph_shavite512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 9: { // simd
            sph_simd512_context ctx;
            uint512 hash;
            sph_simd512_init(&ctx);
            sph_simd512(&ctx, data, len);
            sph_simd512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 10: { // echo
            sph_echo512_context ctx;
            uint512 hash;
            sph_echo512_init(&ctx);
            sph_echo512(&ctx, data, len);
            sph_echo512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 11: { // X11KVS
            return HashX11KVS(pbegin, pend);
        }
        case 12: { // DoubleSHA256
            return Hash(pbegin, pend);
        }
        default:
            return Hash(pbegin, pend);
    }
}

bool VerifyAdamSolution(const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion) {
    if (vchSolution.empty()) return false;
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        
        // Calculate hash of the puzzle
        int algoIndex = 12; // DoubleSHA256 by default
        if (nVersion >= 11) {
            algoIndex = GetAdamPuzzleAlgo(hashAdamSeed, minerKey);
        }
        
        CDataStream ssInput(SER_GETHASH, 0);
        ssInput << hashAdamSeed;
        ssInput << minerKey;
        ssInput << nNonce;
        
        uint256 puzzleHash = CalculateAdamPuzzleHash(algoIndex, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
        
        // Verify miner's signature on the puzzle hash
        if (!minerKey.Verify(puzzleHash, vchSig)) {
            return false;
        }
        
        // Verify the difficulty
        if (!Params().IsRegTestNet()) {
            bool fNegative;
            bool fOverflow;
            uint256 bnTarget;
            bnTarget.SetCompact(nBits, &fNegative, &fOverflow);
            if (fNegative || bnTarget.IsNull() || fOverflow) return false;

            uint256 scaledTarget = bnTarget << 12;
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
    return coordinatorKey.Verify(prevSeed, vchProof);
}

bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey) {
    if (block.vAdamCoordinatorSig.empty()) {
        LogPrintf("VerifyAdamCoordinatorSig: Signature is empty!\n");
        return false;
    }
    bool result = coordinatorKey.Verify(block.GetHash(), block.vAdamCoordinatorSig);
    LogPrintf("VerifyAdamCoordinatorSig: key: %s, hash: %s, sig_size: %d, result: %d\n",
        coordinatorKey.GetID().ToString(), block.GetHash().ToString(), block.vAdamCoordinatorSig.size(), result);
    LogPrintf("VerifyAdamCoordinatorSig details: ver=%d, prev=%s, merkle=%s, time=%u, bits=%08x, nonce=%u, miners=%d, solutions=%d, vrf=%d, sig=%d\n",
        block.nVersion, block.hashPrevBlock.ToString(), block.hashMerkleRoot.ToString(), block.nTime, block.nBits, block.nNonce,
        block.vAdamMiners.size(), block.vAdamSolutions.size(), block.vAdamVRFProof.size(), block.vAdamCoordinatorSig.size());
    return result;
}

