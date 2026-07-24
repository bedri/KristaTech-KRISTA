// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
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

CScript GetMasternodePingScript(const CPubKey& pubKeyMasternode) {
    std::string jsonStr = "{\"basic\":{\"SigVerify\":{\"role\":\"check-signature-verification\",\"inputs\":[{\"name\":\"Pubkey\",\"type\":\"pubkey\",\"value\":\"" + HexStr(pubKeyMasternode) + "\"}]},\n\"TrueVal\":{\"role\":\"number\",\"inputs\":[{\"name\":\"Number\",\"type\":\"number\",\"value\":1}]}},\n\"contract\":{\"MasternodePing\":{\"actions\":[{\"type\":\"basic\",\"name\":\"SigVerify\"},{\"type\":\"basic\",\"name\":\"TrueVal\"}]}},\n\"active_contract\":\"MasternodePing\"}";
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    if (!errorStr.empty()) {
        LogPrintf("GetMasternodePingScript: MESCAL compilation failed for key %s: %s\n", pubKeyMasternode.GetID().ToString(), errorStr);
        script.clear();
        script << ToByteVector(pubKeyMasternode) << OP_CHECKSIGVERIFY << OP_1;
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

bool MatchCoinLockRegistration(const CScript& script, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut) {
    CScript::const_iterator pc = script.begin();
    opcodetype op;
    std::vector<unsigned char> vchPubKey;
    std::vector<unsigned char> vchLockTime;
    std::vector<unsigned char> vchHash;

    // 1. <pubkey>
    if (!script.GetOp(pc, op, vchPubKey) || vchPubKey.size() != 33) return false;
    // 2. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 3. <locktime>
    if (!script.GetOp(pc, op, vchLockTime)) return false;
    // 4. OP_CHECKLOCKTIMEVERIFY
    if (!script.GetOp(pc, op) || op != OP_CHECKLOCKTIMEVERIFY) return false;
    // 5. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 6. OP_DUP
    if (!script.GetOp(pc, op) || op != OP_DUP) return false;
    // 7. OP_HASH160
    if (!script.GetOp(pc, op) || op != OP_HASH160) return false;
    // 8. <pubkeyhash>
    if (!script.GetOp(pc, op, vchHash) || vchHash.size() != 20) return false;
    // 9. OP_EQUALVERIFY
    if (!script.GetOp(pc, op) || op != OP_EQUALVERIFY) return false;
    // 10. OP_CHECKSIG
    if (!script.GetOp(pc, op) || op != OP_CHECKSIG) return false;
    // Ensure we reached the end of the script
    if (pc != script.end()) return false;

    pubkeyOut = CPubKey(vchPubKey);
    if (!pubkeyOut.IsValid()) return false;

    try {
        lockTimeOut = CScriptNum(vchLockTime, true).getint64();
    } catch (...) {
        return false;
    }

    pubkeyHashOut = CKeyID(uint160(vchHash));
    return true;
}

bool MatchPoWLockRegistration(const CScript& script, std::vector<unsigned char>& nonceOut, uint256& challengeOut, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut) {
    CScript::const_iterator pc = script.begin();
    opcodetype op;
    std::vector<unsigned char> vchNonce;
    std::vector<unsigned char> vchChallenge;
    std::vector<unsigned char> vchPubKey;
    std::vector<unsigned char> vchLockTime;
    std::vector<unsigned char> vchHash;

    // 1. <nonce>
    if (!script.GetOp(pc, op, vchNonce) || vchNonce.empty()) return false;
    // 2. <challenge>
    if (!script.GetOp(pc, op, vchChallenge) || vchChallenge.size() != 32) return false;
    // 3. <pubkey>
    if (!script.GetOp(pc, op, vchPubKey) || vchPubKey.size() != 33) return false;
    // 4. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 5. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 6. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 7. <locktime>
    if (!script.GetOp(pc, op, vchLockTime)) return false;
    // 8. OP_CHECKLOCKTIMEVERIFY
    if (!script.GetOp(pc, op) || op != OP_CHECKLOCKTIMEVERIFY) return false;
    // 9. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 10. OP_DUP
    if (!script.GetOp(pc, op) || op != OP_DUP) return false;
    // 11. OP_HASH160
    if (!script.GetOp(pc, op) || op != OP_HASH160) return false;
    // 12. <pubkeyhash>
    if (!script.GetOp(pc, op, vchHash) || vchHash.size() != 20) return false;
    // 13. OP_EQUALVERIFY
    if (!script.GetOp(pc, op) || op != OP_EQUALVERIFY) return false;
    // 14. OP_CHECKSIG
    if (!script.GetOp(pc, op) || op != OP_CHECKSIG) return false;
    // Ensure end of script
    if (pc != script.end()) return false;

    nonceOut = vchNonce;
    challengeOut = uint256(vchChallenge);
    pubkeyOut = CPubKey(vchPubKey);
    if (!pubkeyOut.IsValid()) return false;

    try {
        lockTimeOut = CScriptNum(vchLockTime, true).getint64();
    } catch (...) {
        return false;
    }

    pubkeyHashOut = CKeyID(uint160(vchHash));
    return true;
}

std::vector<CPubKey> GetAdamMinerPool(int nHeight) {
    static RecursiveMutex cs_miner_pool_cache;
    static std::map<uint256, std::vector<CPubKey>> mapMinerPoolCache;

    LOCK(cs_main);
    LOCK(cs_miner_pool_cache);
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

    if (Params().NetworkIDString() == "regtest") {
        for (int i = 0; i < 15; ++i) {
            uniqueKeys.insert(GetAdamDeterministicPubKey(i));
        }
    }

    // Automatically register bootstrap miners from blocks 1 to 199
    int nBootstrapLimit = Params().GetConsensus().nAdamBootstrapLimit;
    if ((Params().NetworkIDString() == "main" || Params().NetworkIDString() == "test" || Params().NetworkIDString() == "regtest") && (!pindexTip || pindexTip->nHeight < nBootstrapLimit)) {
        int nScanLimit = std::min(199, pindexTip ? pindexTip->nHeight : 0);
        for (int h = 1; h <= nScanLimit; ++h) {
            CBlockIndex* pindex = chainActive[h];
            if (!pindex) continue;
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
                                    uniqueKeys.insert(pubkey);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    std::vector<CMasternode> vMns = mnodeman.GetFullMasternodeVector();
    for (auto& mn : vMns) {
        if (mn.IsEnabled() && mn.pubKeyMasternode.IsValid()) {
            uniqueKeys.insert(mn.pubKeyMasternode);
        }
    }

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

        for (int h = nTipHeight; h > nLimit; --h) {
            CBlockIndex* pindex = chainActive[h];
            if (!pindex) continue;

            CBlock block;
            if (ReadBlockFromDisk(block, pindex)) {
                for (const auto& tx : block.vtx) {
                    uint256 txid = tx.GetHash();
                    for (size_t i = 0; i < tx.vout.size(); ++i) {
                        const auto& vout = tx.vout[i];
                        CPubKey pubkey;
                        int64_t lockTime = 0;
                        CKeyID pubkeyHash;

                        if (MatchCoinLockRegistration(vout.scriptPubKey, pubkey, lockTime, pubkeyHash)) {
                            if (pubkey.GetID() != pubkeyHash) continue;
                            if (lockTime < pindex->nHeight + nRegPeriod) continue;
                            if (vout.nValue < MINER_REGISTRATION_LOCK_AMOUNT) continue;

                            COutPoint outpoint(txid, i);
                            bool unspent = pcoinsTip->HaveCoin(outpoint);
                            if (!unspent) continue;

                            uniqueKeys.insert(pubkey);

                            if (pindexTip) {
                                int64_t remaining = lockTime - pindexTip->nHeight;
                                if (remaining > 0 && remaining <= 240) {
                                    LogPrintf("ADAM WARNING: Coin-Lock miner registration for key %s is expiring in %d blocks (~%d minutes). Please renew!\n",
                                              pubkey.GetID().ToString(), remaining, remaining * 30 / 60);
                                }
                            }
                        } else {
                            std::vector<unsigned char> nonce;
                            uint256 challenge;
                            if (MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash)) {
                                if (pubkey.GetID() != pubkeyHash) continue;
                                if (lockTime < pindex->nHeight + nRegPeriod) continue;

                                bool challengeValid = false;
                                if (mapBlockIndex.count(challenge)) {
                                    CBlockIndex* pindexChallenge = mapBlockIndex[challenge];
                                    if (pindexChallenge && chainActive[pindexChallenge->nHeight]->GetBlockHash() == challenge) {
                                        if (pindexChallenge->nHeight >= pindex->nHeight - 100 && pindexChallenge->nHeight < pindex->nHeight) {
                                            challengeValid = true;
                                        }
                                    }
                                }
                                if (!challengeValid) continue;

                                if (nonce.size() != 4) continue;
                                uint32_t nNonce = nonce[0] | (nonce[1] << 8) | (nonce[2] << 16) | (nonce[3] << 24);
                                CHashWriter ss(SER_GETHASH, 0);
                                ss << nNonce;
                                ss << challenge;
                                ss << pubkey;
                                uint256 puzzleHash = ss.GetHash();

                                if (puzzleHash > powLimitTarget) continue;

                                COutPoint outpoint(txid, i);
                                bool unspent = pcoinsTip->HaveCoin(outpoint);
                                if (!unspent) continue;

                                uniqueKeys.insert(pubkey);

                                if (pindexTip) {
                                    int64_t remaining = lockTime - pindexTip->nHeight;
                                    if (remaining > 0 && remaining <= 240) {
                                        LogPrintf("ADAM WARNING: PoW-Lock miner registration for key %s is expiring in %d blocks (~%d minutes). Please renew!\n",
                                                  pubkey.GetID().ToString(), remaining, remaining * 30 / 60);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // On testnet when the dynamic miner pool is too small, fall back to deterministic keys
    int height = pindexTip ? pindexTip->nHeight : 0;
    if (Params().NetworkIDString() == "test" && uniqueKeys.size() < (size_t)Params().GetConsensus().GetAdamThreshold(height)) {
        for (int i = 0; i < 15; ++i) {
            uniqueKeys.insert(GetAdamDeterministicPubKey(i));
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

        CHashWriter ss(SER_GETHASH, 0);
        ss << seed;
        ss << block.vAdamVRFProof;
        seed = ss.GetHash();
        
        {
            LOCK(cs_adam_seeds);
            mapAdamSeeds[pindexCurr->GetBlockHash()] = seed;
            mapSeedToBlockHash[seed] = pindexCurr->GetBlockHash();
        }
    }
    
    return seed;
}


bool SelectAdamNodes(const uint256& hashAdamSeed, const Consensus::Params& params, std::vector<CPubKey>& vSelectedMinersOut, CPubKey& coordinatorOut) {
    int nHeight = -1;
    {
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
    if (pindexTip) {
        int targetHeight = pindexTip->nHeight + 1;
        if (!params.NetworkUpgradeActive(targetHeight, Consensus::UPGRADE_POMBL)) {
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
        int currentHeight = (chainActive.Tip() ? chainActive.Tip()->nHeight : 0);
        std::vector<CMasternode> vFullMns = mnodeman.GetFullMasternodeVector();
        for (auto& mn : vFullMns) {
            if (mn.pubKeyMasternode.IsValid() && pcoinsTip->HaveCoin(mn.vin.prevout)) {
                auto it = mapMasternodeLastActiveHeight.find(mn.pubKeyMasternode);
                if (it != mapMasternodeLastActiveHeight.end() && (currentHeight - it->second <= 50)) {
                    vMns.push_back(mn.pubKeyMasternode);
                }
            }
        }
    }
    
    // Rank all keys in the combined pool by hashing seed + key
    std::vector<MinerRank> rankedPool;
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


bool VerifyAdamSolution(const uint256& hashPrevBlock, const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion, int nHeight) {
    if (vchSolution.empty()) {
        return false;
    }
    try {
        CDataStream ss(vchSolution, SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nNonce;
        std::vector<unsigned char> vchSig;
        ss >> nNonce >> vchSig;
        
        CDataStream ssInput(SER_GETHASH, 0);
        ssInput << hashAdamSeed;
        ssInput << minerKey;
        ssInput << nNonce;
        
        uint256 puzzleHash;
        if (nVersion == 11) {
            int algoIndex = GetAdamPuzzleAlgo(hashAdamSeed, minerKey, true);
            puzzleHash = CalculateAdamPuzzleHash(algoIndex, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
        } else if (nVersion >= 12) {
            int minerIdx = -1;
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            if (SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator)) {
                for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                    if (vExpectedMiners[i] == minerKey) {
                        minerIdx = i;
                        break;
                    }
                }
            }
            if (minerIdx < 0) {
                return false;
            }
            int algo1 = -1, algo2 = -1, algo3 = -1;
            GetAdam3PermutationAlgos(hashPrevBlock, minerKey, algo1, algo2, algo3);
            
            uint256 hash3 = CalculateAdamPuzzleHash(algo3, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
            int i_factor = minerIdx + 1;
            arith_uint256 val1 = UintToArith256(hash3) * i_factor;
            uint256 multiplied1 = ArithToUint256(val1);
            
            uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
            arith_uint256 val2 = UintToArith256(hash2) * i_factor;
            uint256 multiplied2 = ArithToUint256(val2);
            
            puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
        } else {
            puzzleHash = CalculateAdamPuzzleHash(12, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
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

bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey) {
    if (block.vAdamCoordinatorSig.empty()) {
        LogPrintf("VerifyAdamCoordinatorSig: Signature is empty!\n");
        return false;
    }
    bool result = VerifyBLSWithECDSAFallback(block.GetHash(), coordinatorKey, block.vAdamCoordinatorSig);
    LogPrintf("VerifyAdamCoordinatorSig: key: %s, hash: %s, sig_size: %d, result: %d\n",
        coordinatorKey.GetID().ToString(), block.GetHash().ToString(), block.vAdamCoordinatorSig.size(), result);
    LogPrintf("VerifyAdamCoordinatorSig details: ver=%d, prev=%s, merkle=%s, time=%u, bits=%08x, nonce=%u, miners=%d, solutions=%d, vrf=%d, sig=%d\n",
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
    if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
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


