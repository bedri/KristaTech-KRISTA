// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTA_LLMQ_H
#define KRISTA_LLMQ_H

#include "main.h"
#include "masternode.h"
#include "uint256.h"
#include "key.h"
#include "crypto/bls.h"
#include "serialize.h"

#include <vector>
#include <map>

namespace llmq {

class CQuorumMember {
public:
    COutPoint collateralOutpoint;
    CPubKey pubKeyMasternode;

    CQuorumMember() = default;
    CQuorumMember(const COutPoint& out, const CPubKey& pub) : collateralOutpoint(out), pubKeyMasternode(pub) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(collateralOutpoint);
        READWRITE(pubKeyMasternode);
    }
};

class CQuorum {
public:
    int nHeight{0};
    uint256 quorumHash;
    std::vector<CQuorumMember> members;
    CPubKey quorumPubKey;

    CQuorum() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nHeight);
        READWRITE(quorumHash);
        READWRITE(members);
        READWRITE(quorumPubKey);
    }
};

class CQuorumSignature {
public:
    uint256 blockHash;
    std::vector<std::pair<COutPoint, std::vector<unsigned char>>> signatures; // member outpoint -> signature bytes

    CQuorumSignature() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(signatures);
    }

    bool Verify(const CQuorum& quorum, const CBlockIndex* pindexPrev = nullptr, BLSDomain domain = BLSDomain::LLMQ_BLOCK, int nVerifyHeight = -1) const;
    bool AggregateBLSSignatures(const CQuorum& quorum, CBLSPubKey& aggPubKeyOut, CBLSSignature& aggSigOut, const CBlockIndex* pindexPrev = nullptr, BLSDomain domain = BLSDomain::LLMQ_BLOCK, int nVerifyHeight = -1) const;
};

// Compute target LLMQ quorum size from verified on-chain active Masternode count (5 -> 9 -> 11 -> 15)
int GetTargetQuorumSize(int nHeight, size_t nActiveOnChainMNs);

// Select M members deterministically based on seed (if nQuorumSize <= 0, scales dynamically via GetTargetQuorumSize)
std::vector<CQuorumMember> ElectQuorumMembers(int nHeight, int nQuorumSize = 0, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);

// DKG session simulation: create a new quorum at height
CQuorum RunDKG(int nHeight, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);

// Get BFT signature threshold for a quorum of nQuorumSize (floor(2N/3)+1)
size_t GetQuorumThreshold(size_t nQuorumSize);

// Get the active quorum for a given block height
CQuorum GetActiveQuorum(int nHeight, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);
void ClearQuorumCache();

// Retrieve private key for a masternode (local or deterministic regtest)
bool GetMasternodePrivKey(const CPubKey& pubKey, CKey& key);

/** A quorum member signs at most one block per parent within the active proposal slot window,
 *  using a local guard strictly shorter than the 30s PoBLS fallback slice so modest clock skew
 *  never spills across fallback rotations. */
static const int64_t QUORUM_EQUIVOCATION_GUARD_SECS = 25;

/** Minimum local dwell time before a quorum member switches to a strictly higher proposal slot. */
static const int64_t QUORUM_MIN_SLOT_SETTLE_SECS = 15;

/** Compute the deterministic PoBLS proposal slot index for a candidate block timestamp on top of prevTime.
 *  Returns 0 during the primary coordinator window (< ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS),
 *  and 1 + (elapsed - 45) / 30 for subsequent PoBLS fallback rotation slices. */
int64_t ComputeQuorumProposalSlot(int64_t nParentBlockTime, int64_t nProposalBlockTime);

/** Anti-equivocation and slot-aligned view-change guard for local quorum signing. Returns true (and records
 *  the decision) if this node may sign blockHash proposed by coordinator on top of prevHash; refuses same-slot
 *  equivocation, displaced hashes, locked proposals, and rapid sub-settle slot switches. */
bool AllowQuorumSignature(const uint256& prevHash, const CPubKey& coordinator, const uint256& blockHash, int64_t nProposalSlot = -1, int64_t nNowOverride = -1);

} // namespace llmq

#endif // KRISTA_LLMQ_H
