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
#include <vector>
#include <map>

class CBlockHeader;

extern RecursiveMutex cs_adam_seeds;
extern std::map<uint256, uint256> mapAdamSeeds;

// Helper functions for ADAM (A Decentralized Approach Model) cooperative consensus

/**
 * Check if ADAM consensus is active at the given height.
 */
inline bool IsAdamActive(int nHeight, const Consensus::Params& params) {
    return nHeight >= params.nAdamHeight;
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
std::vector<CPubKey> GetAdamMinerPool();

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
int GetAdamPuzzleAlgo(const uint256& hashAdamSeed, const CPubKey& minerKey);

/**
 * Get the name of a puzzle hash algorithm.
 */
std::string GetAdamPuzzleAlgoName(int algoIndex);

/**
 * Calculate the puzzle hash using the chosen algorithm.
 */
uint256 CalculateAdamPuzzleHash(int algoIndex, const unsigned char* pbegin, const unsigned char* pend);

/**
 * Verify a partial PoW solution submitted by a miner.
 */
bool VerifyAdamSolution(const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion = 0);

/**
 * Verify the Coordinator's VRF proof signature.
 */
bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey);

/**
 * Verify the Coordinator's signature on the block header hash.
 */
bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey);

#endif // BITCOIN_ADAM_H
