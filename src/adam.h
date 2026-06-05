// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADAM_H
#define BITCOIN_ADAM_H

#include "pubkey.h"
#include "key.h"
#include "uint256.h"
#include "consensus/params.h"
#include <vector>

class CBlockHeader;

// Helper functions for ADAM (A Decentralized Approach Model) cooperative consensus

/**
 * Check if ADAM consensus is active at the given height.
 */
inline bool IsAdamActive(int nHeight, const Consensus::Params& params) {
    return nHeight >= params.nAdamHeight;
}

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
 * Verify a partial PoW solution submitted by a miner.
 */
bool VerifyAdamSolution(const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits);

/**
 * Verify the Coordinator's VRF proof signature.
 */
bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey);

/**
 * Verify the Coordinator's signature on the block header hash.
 */
bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey);

#endif // BITCOIN_ADAM_H
