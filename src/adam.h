// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADAM_H
#define BITCOIN_ADAM_H

#include "pubkey.h"
#include "key.h"
#include "uint256.h"
#include "consensus/params.h"
#include "sync.h"
#include "script/script.h"
#include <vector>
#include <map>

class CBlockHeader;

extern RecursiveMutex cs_adam_seeds;
extern std::map<uint256, uint256> mapAdamSeeds;

extern RecursiveMutex cs_recent_vrf_proofs;
extern std::map<uint256, std::vector<unsigned char>> mapRecentVRFProofs;

extern std::map<CPubKey, int> mapMasternodeLastActiveHeight;
CScript GetMasternodePingScript(const CPubKey& pubKeyMasternode);

// Helper functions for ADAM (A Decentralized Approach Model) cooperative consensus

static const CAmount MINER_REGISTRATION_LOCK_AMOUNT = 1000 * COIN;

/**
 * Check if ADAM consensus is active at the given height.
 */
inline bool IsAdamActive(int nHeight, const Consensus::Params& params) {
    return params.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ADAM);
}

/**
 * Check if Model D reward split is active at the given height.
 */
bool IsModelDActive(int nHeight);

/**
 * Generate a deterministic private key from an index.
 */
CKey GetAdamDeterministicKey(int index);

/**
 * Generate a deterministic public key from an index.
 */
CPubKey GetAdamDeterministicPubKey(int index);

/**
 * Get the pool of potential miners/coordinators.
 */
std::vector<CPubKey> GetAdamMinerPool(int nHeight = -1);

class CBlockIndex;

/**
 * Get the ADAM rolling seed for a block index.
 */
uint256 GetAdamSeed(const CBlockIndex* pindex);

/**
 * Select n miners and 1 coordinator deterministically using the rolling seed.
 */
bool SelectAdamNodes(const uint256& hashAdamSeed, const Consensus::Params& params, std::vector<CPubKey>& vSelectedMinersOut, CPubKey& coordinatorOut);

/**
 * Get the deterministic puzzle hash algorithm index for an elected miner.
 */
int GetAdamPuzzleAlgo(const uint256& hashAdamSeed, const CPubKey& minerKey, bool fFallbackMode = false);

/**
 * Get the name of a puzzle hash algorithm.
 */
std::string GetAdamPuzzleAlgoName(int algoIndex);

/**
 * Get the deterministic 3-permutation algorithm indices for an elected miner based on hashPrevBlock.
 */
void GetAdam3PermutationAlgos(const uint256& hashPrevBlock, const CPubKey& minerKey, int& algo1, int& algo2, int& algo3);

/**
 * Calculate the puzzle hash using the chosen algorithm.
 */
uint256 CalculateAdamPuzzleHash(int algoIndex, const unsigned char* pbegin, const unsigned char* pend);

/**
 * Verify a partial PoW solution submitted by a miner.
 */
bool VerifyAdamSolution(const uint256& hashPrevBlock, const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion = 0, int nHeight = -1);

/**
 * Verify the Coordinator's VRF proof signature.
 */
bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey);

/**
 * Verify the Coordinator's signature on the block header hash.
 */
bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey);

struct CAdamSolutionMsg {
    uint256 hashPrevBlock;
    CPubKey minerKey;
    std::vector<unsigned char> vchSolution;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashPrevBlock);
        READWRITE(minerKey);
        READWRITE(vchSolution);
    }
};

extern RecursiveMutex cs_adam_solutions;
extern std::map<uint256, std::map<CPubKey, std::vector<unsigned char>>> mapAdamSolutionsCache;
extern std::map<uint256, std::vector<CAdamSolutionMsg>> mapOrphanAdamSolutions;

void ProcessOrphanAdamSolutions(const uint256& hash);

uint256 GetMinerPoWLimit(const std::string& networkId);

#endif // BITCOIN_ADAM_H
