// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTATECH_NET_PROCESSING_H
#define KRISTATECH_NET_PROCESSING_H

#include "net.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "sync.h"
#include "validationinterface.h"

class CBlock;
class CBlockHeader;
class CBlockIndex;
class CValidationState;

struct CFutureBlock {
    CBlock block;
    std::string peerAddr;
};

struct COrphanTx {
    CTransaction tx;
    NodeId fromPeer;
};

enum FlushStateMode {
    FLUSH_STATE_IF_NEEDED,
    FLUSH_STATE_PERIODIC,
    FLUSH_STATE_ALWAYS
};

extern RecursiveMutex cs_future_blocks;
extern std::map<uint256, CFutureBlock> mapFutureBlocks;
extern std::map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_main);
extern std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);
extern int nSyncStarted;

bool AddOrphanTx(const CTransaction& tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
void EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
void EraseOrphansFor(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) EXCLUSIVE_LOCKS_REQUIRED(cs_main);
bool FlushStateToDisk(CValidationState& state, FlushStateMode mode);
void CheckBlockIndex();
bool CheckBlockTime(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev);
bool AcceptBlockHeader(const CBlock& block, CValidationState& state, CBlockIndex** ppindex = NULL);

/** Notify P2P block-in-flight tracker that a block arrived from a peer. */
void NotifyBlockReceivedFromPeer(const uint256& hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Record the peer that supplied a block and update its last sync request timestamp. */
void UpdatePeerBlockSourceAndTime(NodeId nodeId, const uint256& blockHash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Update per-peer block-spam state after a failed AcceptBlock; returns false if peer is spamming. */
bool CheckPeerBlockSpam(NodeId nodeId, int nHeight, CValidationState& state) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Record rejection details and DoS score against the peer that supplied an invalid block. */
void PenalizeInvalidBlockSource(const uint256& blockHash, unsigned char rejectCode, const std::string& rejectReason, int nDoS) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Remove a connected block from the peer block-source map. */
void ErasePeerBlockSource(const uint256& blockHash) EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Clear P2P net-processing state (called during UnloadBlockIndex). */
void ClearNetProcessingState() EXCLUSIVE_LOCKS_REQUIRED(cs_main);

/** Reset the recent-rejects rolling bloom filter (called during InitBlockIndex). */
void ResetRecentRejects();

#endif // KRISTATECH_NET_PROCESSING_H
