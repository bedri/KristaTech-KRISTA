// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "net_processing.h"
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include <fstream>
#include <unistd.h>
#include "adam.h"

#include "addrman.h"
#include "amount.h"
#include "blocksignature.h"
#include "chainparams.h"
#include "base58.h"
#include "checkpoints.h"
#include "checkqueue.h"
#include "consensus/consensus.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "fs.h"
#include "init.h"
#include "kernel.h"
#include "llmq.h"
#include "llmq_messages.h"
#include "masternode-payments.h"
#include "masternodeman.h"
#include "merkleblock.h"
#include "miner.h"
#include "blockencodings.h"
#include "messagesigner.h"
#include "net.h"
#include "netmessagemaker.h"
#include "netbase.h"
#include "policy/policy.h"
#include "pow.h"
#include "spork.h"
#include "sporkdb.h"
#include "txdb.h"
#include "txmempool.h"
#include "guiinterface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"

#include "masternode-sync.h"
#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <atomic>
#include <queue>

struct PartiallyDownloadedBlock {
    CBlockHeaderAndShortTxIDs cmpct;
    std::vector<CTransaction> vtx;
    std::vector<uint16_t> missing_indexes;
};
static std::map<std::pair<NodeId, uint256>, PartiallyDownloadedBlock> mapPartiallyDownloadedBlocks;
static RecursiveMutex cs_partiallyDownloadedBlocks;

struct HeadersSyncState {
    std::vector<CBlockHeader> vHeadersBuffer;
    arith_uint256 nBaseWork;
    arith_uint256 nAccumulatedWork;
    uint256 hashLast;
};
static std::map<NodeId, HeadersSyncState> mapHeadersSyncState GUARDED_BY(cs_main);


static const uint64_t RANDOMIZER_ID_ADDRESS_RELAY = 0x3cac0035b5866b90ULL; // SHA256("main address relay")[0:8]

namespace {
/**
     * Sources of received blocks, to be able to send them reject messages or ban
     * them, if processing happens afterwards. Protected by cs_main.
     */
std::map<uint256, NodeId> mapBlockSource;

/**
     * Filter for transactions that were recently rejected by
     * AcceptToMemoryPool. These are not rerequested until the chain tip
     * changes, at which point the entire filter is reset. Protected by
     * cs_main.
     *
     * Without this filter we'd be re-requesting txs from each of our peers,
     * increasing bandwidth consumption considerably. For instance, with 100
     * peers, half of which relay a tx we don't accept, that might be a 50x
     * bandwidth increase. A flooding attacker attempting to roll-over the
     * filter using minimum-sized, 60byte, transactions might manage to send
     * 1000/sec if we have fast peers, so we pick 120,000 to give our peers a
     * two minute window to send invs to us.
     *
     * Decreasing the false positive rate is fairly cheap, so we pick one in a
     * million to make it highly unlikely for users to have issues with this
     * filter.
     *
     * Memory used: 1.7MB
     */
boost::scoped_ptr<CRollingBloomFilter> recentRejects;
uint256 hashRecentRejectsChainTip;

/** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
struct QueuedBlock {
    uint256 hash;
    CBlockIndex* pindex;        //! Optional.
    int64_t nTime;              //! Time of "getdata" request in microseconds.
    int nValidatedQueuedBefore; //! Number of blocks queued with validated headers (globally) at the time this one is requested.
    bool fValidatedHeaders;     //! Whether this block has validated headers at the time of request.
};
std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> > mapBlocksInFlight;

/** Number of blocks in flight with validated headers. */
int nQueuedValidatedHeaders = 0;

/** Number of preferable block download peers. */
int nPreferredDownload = 0;

} // anon namespace

//////////////////////////////////////////////////////////////////////////////
//
// Registration of network node signals.
//

namespace
{
struct CBlockReject {
    unsigned char chRejectCode;
    std::string strRejectReason;
    uint256 hashBlock;
};


class CNodeBlocks
{
public:
    CNodeBlocks():
            maxSize(0),
            maxAvg(0)
    {
        maxSize = GetArg("-blockspamfiltermaxsize", DEFAULT_BLOCK_SPAM_FILTER_MAX_SIZE);
        maxAvg = GetArg("-blockspamfiltermaxavg", DEFAULT_BLOCK_SPAM_FILTER_MAX_AVG);
    }

    bool onBlockReceived(int nHeight) {
        if(nHeight > 0 && maxSize && maxAvg) {
            addPoint(nHeight);
            return true;
        }
        return false;
    }

    bool updateState(CValidationState& state, bool ret)
    {
        // No Blocks
        size_t size = points.size();
        if(size == 0)
            return ret;

        // Compute the number of the received blocks
        size_t nBlocks = 0;
        for(auto point : points)
        {
            nBlocks += point.second;
        }

        // Compute the average value per height
        double nAvgValue = (double)nBlocks / size;

        // Ban the node if try to spam
        bool banNode = (nAvgValue >= 1.5 * maxAvg && size >= maxAvg) ||
                       (nAvgValue >= maxAvg && nBlocks >= maxSize) ||
                       (nBlocks >= maxSize * 3);
        if(banNode)
        {
            // Clear the points and ban the node
            points.clear();
            return state.DoS(100, error("block-spam ban node for sending spam"));
        }

        return ret;
    }

private:
    void addPoint(int height)
    {
        // Remove the last element in the list
        if(points.size() == maxSize)
        {
            points.erase(points.begin());
        }

        // Add the point to the list
        int occurrence = 0;
        auto mi = points.find(height);
        if (mi != points.end())
            occurrence = (*mi).second;
        occurrence++;
        points[height] = occurrence;
    }

private:
    std::map<int,int> points;
    size_t maxSize;
    size_t maxAvg;
};



 /**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    const CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    const std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex* pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex* pindexLastCommonBlock;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    int64_t nLastSyncReqTime;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    std::list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeBlocks nodeBlocks;

    CNodeState(CAddress addrIn, std::string addrNameIn) : address(addrIn), name(addrNameIn) {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = NULL;
        hashLastUnknownBlock.SetNull();
        pindexLastCommonBlock = NULL;
        fSyncStarted = false;
        nLastSyncReqTime = 0;
        nStallingSince = 0;
        nBlocksInFlight = 0;
        fPreferredDownload = false;
    }
};

/** Map maintaining per-node state. Requires cs_main. */
std::map<NodeId, CNodeState> mapNodeState;

// Requires cs_main.
CNodeState* State(NodeId pnode)
{
    std::map<NodeId, CNodeState>::iterator it = mapNodeState.find(pnode);
    if (it == mapNodeState.end())
        return NULL;
    return &it->second;
}

void UpdatePreferredDownload(CNode* node, CNodeState* state)
{
    nPreferredDownload -= state->fPreferredDownload;

    // Whether this node should be marked as a preferred download node.
    state->fPreferredDownload = (!node->fInbound || node->fWhitelisted) && !node->fOneShot && !node->fClient;

    nPreferredDownload += state->fPreferredDownload;
}

void PushNodeVersion(CNode* pnode, CConnman& connman, int64_t nTime)
{
    ServiceFlags nLocalNodeServices = pnode->GetLocalServices();
    uint64_t nonce = pnode->GetLocalNonce();
    int nNodeStartingHeight = pnode->GetMyStartingHeight();
    NodeId nodeid = pnode->GetId();
    CAddress addr = pnode->addr;

    CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService(), addr.nServices));
    CAddress addrMe = CAddress(CService(), nLocalNodeServices);

    connman.PushMessage(pnode, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERSION, PROTOCOL_VERSION, (uint64_t)nLocalNodeServices, nTime, addrYou, addrMe,
            nonce, strSubVersion, nNodeStartingHeight, true));

    if (fLogIPs)
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), addrYou.ToString(), nodeid);
    else
        LogPrint(BCLog::NET, "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nNodeStartingHeight, addrMe.ToString(), nodeid);
}

void InitializeNode(CNode *pnode, CConnman& connman) {
    CAddress addr = pnode->addr;
    std::string addrName = pnode->GetAddrName();
    NodeId nodeid = pnode->GetId();
    {
        LOCK(cs_main);
        mapNodeState.emplace_hint(mapNodeState.end(), std::piecewise_construct, std::forward_as_tuple(nodeid), std::forward_as_tuple(addr, std::move(addrName)));
    }
    if(!pnode->fInbound)
        PushNodeVersion(pnode, connman, GetTime());
}

void FinalizeNode(NodeId nodeid, bool& fUpdateConnectionTime)
{
    fUpdateConnectionTime = false;
    LOCK(cs_main);
    CNodeState* state = State(nodeid);

    if (state->fSyncStarted)
        nSyncStarted--;

    if (state->nMisbehavior == 0 && state->fCurrentlyConnected) {
        fUpdateConnectionTime = true;
    }

    for (const QueuedBlock& entry : state->vBlocksInFlight)
        mapBlocksInFlight.erase(entry.hash);
    EraseOrphansFor(nodeid);
    mapHeadersSyncState.erase(nodeid);
    {
        LOCK(cs_partiallyDownloadedBlocks);
        for (auto it = mapPartiallyDownloadedBlocks.begin(); it != mapPartiallyDownloadedBlocks.end(); ) {
            if (it->first.first == nodeid) {
                it = mapPartiallyDownloadedBlocks.erase(it);
            } else {
                ++it;
            }
        }
    }
    nPreferredDownload -= state->fPreferredDownload;

    mapNodeState.erase(nodeid);
}

// Requires cs_main.
void MarkBlockAsReceived(const uint256& hash)
{
    std::map<uint256, std::pair<NodeId, std::list<QueuedBlock>::iterator> >::iterator itInFlight = mapBlocksInFlight.find(hash);
    if (itInFlight != mapBlocksInFlight.end()) {
        CNodeState* state = State(itInFlight->second.first);
        nQueuedValidatedHeaders -= itInFlight->second.second->fValidatedHeaders;
        state->vBlocksInFlight.erase(itInFlight->second.second);
        state->nBlocksInFlight--;
        state->nStallingSince = 0;
        mapBlocksInFlight.erase(itInFlight);
    }
}

// Requires cs_main.
void MarkBlockAsInFlight(NodeId nodeid, const uint256& hash, CBlockIndex* pindex = NULL)
{
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    // Make sure it's not listed somewhere already.
    MarkBlockAsReceived(hash);

    QueuedBlock newentry = {hash, pindex, GetTimeMicros(), nQueuedValidatedHeaders, pindex != NULL};
    nQueuedValidatedHeaders += newentry.fValidatedHeaders;
    std::list<QueuedBlock>::iterator it = state->vBlocksInFlight.insert(state->vBlocksInFlight.end(), newentry);
    state->nBlocksInFlight++;
    mapBlocksInFlight[hash] = std::make_pair(nodeid, it);
}

/** Check whether the last unknown block a peer advertised is not yet known. */
void ProcessBlockAvailability(NodeId nodeid)
{
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    if (!state->hashLastUnknownBlock.IsNull()) {
        BlockMap::iterator itOld = mapBlockIndex.find(state->hashLastUnknownBlock);
        if (itOld != mapBlockIndex.end() && itOld->second->nChainWork > 0) {
            if (state->pindexBestKnownBlock == NULL || itOld->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
                state->pindexBestKnownBlock = itOld->second;
            state->hashLastUnknownBlock.SetNull();
        }
    }
}

/** Update tracking information about which blocks a peer is assumed to have. */
void UpdateBlockAvailability(NodeId nodeid, const uint256& hash)
{
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    ProcessBlockAvailability(nodeid);

    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end() && it->second->nChainWork > 0) {
        // An actually better block was announced.
        if (state->pindexBestKnownBlock == NULL || it->second->nChainWork >= state->pindexBestKnownBlock->nChainWork)
            state->pindexBestKnownBlock = it->second;
    } else {
        // An unknown block was announced; just assume that the latest one is the best one.
        state->hashLastUnknownBlock = hash;
    }
}

/** Find the last common ancestor two blocks have.
 *  Both pa and pb must be non-NULL. */
CBlockIndex* LastCommonAncestor(CBlockIndex* pa, CBlockIndex* pb)
{
    if (pa->nHeight > pb->nHeight) {
        pa = pa->GetAncestor(pb->nHeight);
    } else if (pb->nHeight > pa->nHeight) {
        pb = pb->GetAncestor(pa->nHeight);
    }

    while (pa != pb && pa && pb) {
        pa = pa->pprev;
        pb = pb->pprev;
    }

    // Eventually all chain branches meet at the genesis block.
    assert(pa == pb);
    return pa;
}

/** Update pindexLastCommonBlock and add not-in-flight missing successors to vBlocks, until it has
 *  at most count entries. */
void FindNextBlocksToDownload(NodeId nodeid, unsigned int count, std::vector<CBlockIndex*>& vBlocks, NodeId& nodeStaller)
{
    if (count == 0)
        return;

    vBlocks.reserve(vBlocks.size() + count);
    CNodeState* state = State(nodeid);
    assert(state != NULL);

    // Make sure pindexBestKnownBlock is up to date, we'll need it.
    ProcessBlockAvailability(nodeid);

    if (state->pindexBestKnownBlock == NULL || state->pindexBestKnownBlock->nChainWork < chainActive.Tip()->nChainWork) {
        // This peer has nothing interesting.
        return;
    }

    if (state->pindexLastCommonBlock == NULL) {
        // Bootstrap quickly by guessing a parent of our best tip is the forking point.
        // Guessing wrong in either direction is not a problem.
        state->pindexLastCommonBlock = chainActive[std::min(state->pindexBestKnownBlock->nHeight, chainActive.Height())];
    }

    // If the peer reorganized, our previous pindexLastCommonBlock may not be an ancestor
    // of their current tip anymore. Go back enough to fix that.
    state->pindexLastCommonBlock = LastCommonAncestor(state->pindexLastCommonBlock, state->pindexBestKnownBlock);
    if (state->pindexLastCommonBlock == state->pindexBestKnownBlock)
        return;

    std::vector<CBlockIndex*> vToFetch;
    CBlockIndex* pindexWalk = state->pindexLastCommonBlock;
    // Never fetch further than the best block we know the peer has, or more than BLOCK_DOWNLOAD_WINDOW + 1 beyond the last
    // linked block we have in common with this peer. The +1 is so we can detect stalling, namely if we would be able to
    // download that next block if the window were 1 larger.
    int nWindowEnd = state->pindexLastCommonBlock->nHeight + BLOCK_DOWNLOAD_WINDOW;
    int nMaxHeight = std::min<int>(state->pindexBestKnownBlock->nHeight, nWindowEnd + 1);
    NodeId waitingfor = -1;
    while (pindexWalk->nHeight < nMaxHeight) {
        // Read up to 128 (or more, if more blocks than that are needed) successors of pindexWalk (towards
        // pindexBestKnownBlock) into vToFetch. We fetch 128, because CBlockIndex::GetAncestor may be as expensive
        // as iterating over ~100 CBlockIndex* entries anyway.
        int nToFetch = std::min(nMaxHeight - pindexWalk->nHeight, std::max<int>(count - vBlocks.size(), 128));
        vToFetch.resize(nToFetch);
        pindexWalk = state->pindexBestKnownBlock->GetAncestor(pindexWalk->nHeight + nToFetch);
        vToFetch[nToFetch - 1] = pindexWalk;
        for (unsigned int i = nToFetch - 1; i > 0; i--) {
            vToFetch[i - 1] = vToFetch[i]->pprev;
        }

        // Iterate over those blocks in vToFetch (in forward direction), adding the ones that
        // are not yet downloaded and not in flight to vBlocks. In the mean time, update
        // pindexLastCommonBlock as long as all ancestors are already downloaded.
        for (CBlockIndex* pindex : vToFetch) {
            if (!pindex->IsValid(BLOCK_VALID_TREE)) {
                // We consider the chain that this peer is on invalid.
                return;
            }
            if (pindex->nStatus & BLOCK_HAVE_DATA) {
                if (pindex->nChainTx)
                    state->pindexLastCommonBlock = pindex;
            } else if (mapBlocksInFlight.count(pindex->GetBlockHash()) == 0) {
                // The block is not already downloaded, and not yet in flight.
                if (pindex->nHeight > nWindowEnd) {
                    // We reached the end of the window.
                    if (vBlocks.size() == 0 && waitingfor != nodeid) {
                        // We aren't able to fetch anything, but we would be if the download window was one larger.
                        nodeStaller = waitingfor;
                    }
                    return;
                }
                vBlocks.push_back(pindex);
                if (vBlocks.size() == count) {
                    return;
                }
            } else if (waitingfor == -1) {
                // This is the first already-in-flight block.
                waitingfor = mapBlocksInFlight[pindex->GetBlockHash()].first;
            }
        }
    }
}

} // anon namespace

bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats)
{
    LOCK(cs_main);
    CNodeState* state = State(nodeid);
    if (state == NULL)
        return false;
    stats.nMisbehavior = state->nMisbehavior;
    stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
    stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
    for (const QueuedBlock& queue : state->vBlocksInFlight) {
        if (queue.pindex)
            stats.vHeightInFlight.push_back(queue.pindex->nHeight);
    }
    return true;
}

void RegisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.ProcessMessages.connect(&ProcessMessages);
    nodeSignals.SendMessages.connect(&SendMessages);
    nodeSignals.InitializeNode.connect(&InitializeNode);
    nodeSignals.FinalizeNode.connect(&FinalizeNode);
}

void UnregisterNodeSignals(CNodeSignals& nodeSignals)
{
    nodeSignals.ProcessMessages.disconnect(&ProcessMessages);
    nodeSignals.SendMessages.disconnect(&SendMessages);
    nodeSignals.InitializeNode.disconnect(&InitializeNode);
    nodeSignals.FinalizeNode.disconnect(&FinalizeNode);
}


// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    if (howmuch == 0)
        return;

    CNodeState* state = State(pnode);
    if (state == NULL)
        return;

    state->nMisbehavior += howmuch;
    int banscore = GetArg("-banscore", DEFAULT_BANSCORE_THRESHOLD);
    if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore) {
        LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
        state->fShouldBan = true;
    } else
        LogPrintf("Misbehaving: %s (%d -> %d)\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
}


int nSyncStarted = 0;

void NotifyBlockReceivedFromPeer(const uint256& hash)
{
    MarkBlockAsReceived(hash);
}

void UpdatePeerBlockSourceAndTime(NodeId nodeId, const uint256& blockHash)
{
    mapBlockSource[blockHash] = nodeId;
    CNodeState* nodestate = State(nodeId);
    if (nodestate) nodestate->nLastSyncReqTime = GetTime();
}

bool CheckPeerBlockSpam(NodeId nodeId, int nHeight, CValidationState& state)
{
    CNodeState* nodestate = State(nodeId);
    if (nodestate == nullptr) return true;
    nodestate->nodeBlocks.onBlockReceived(nHeight);
    bool nodeStatus = nodestate->nodeBlocks.updateState(state, true);
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        if (nDoS > 0)
            Misbehaving(nodeId, nDoS);
        nodeStatus = false;
    }
    return nodeStatus;
}

void PenalizeInvalidBlockSource(const uint256& blockHash, unsigned char rejectCode, const std::string& rejectReason, int nDoS)
{
    std::map<uint256, NodeId>::iterator it = mapBlockSource.find(blockHash);
    if (it != mapBlockSource.end() && State(it->second)) {
        CBlockReject reject = {rejectCode, rejectReason.substr(0, MAX_REJECT_MESSAGE_LENGTH), blockHash};
        State(it->second)->rejects.push_back(reject);
        if (nDoS > 0) {
            Misbehaving(it->second, nDoS);
        }
    }
}

void ErasePeerBlockSource(const uint256& blockHash)
{
    mapBlockSource.erase(blockHash);
}

void ClearNetProcessingState()
{
    mapBlockSource.clear();
    mapBlocksInFlight.clear();
    nQueuedValidatedHeaders = 0;
    nPreferredDownload = 0;
    mapNodeState.clear();
    mapHeadersSyncState.clear();
    recentRejects.reset(nullptr);
}

void ResetRecentRejects()
{
    recentRejects.reset(new CRollingBloomFilter(120000, 0.000001));
}

bool static AlreadyHave(const CInv& inv) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    switch (inv.type) {
    case MSG_TX: {
        assert(recentRejects);
        if (chainActive.Tip()->GetBlockHash() != hashRecentRejectsChainTip) {
            // If the chain tip has changed previously rejected transactions
            // might be now valid, e.g. due to a nLockTime'd tx becoming valid,
            // or a double-spend. Reset the rejects filter and give those
            // txs a second chance.
            hashRecentRejectsChainTip = chainActive.Tip()->GetBlockHash();
            recentRejects->reset();
        }


        return recentRejects->contains(inv.hash) ||
               mempool.exists(inv.hash) ||
               mapOrphanTransactions.count(inv.hash) ||
               pcoinsTip->HaveCoinInCache(COutPoint(inv.hash, 0)) || // Best effort: only try output 0 and 1
               pcoinsTip->HaveCoinInCache(COutPoint(inv.hash, 1));
    }

    case MSG_BLOCK: {
        if (mapBlockIndex.count(inv.hash)) {
            return true;
        }
        LOCK(cs_future_blocks);
        auto itFuture = mapFutureBlocks.find(inv.hash);
        return itFuture != mapFutureBlocks.end() && !itFuture->second.block.vtx.empty();
    }
    case MSG_SPORK:
        return mapSporks.count(inv.hash);
    case MSG_MASTERNODE_WINNER:
        if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
            masternodeSync.AddedMasternodeWinner(inv.hash);
            return true;
        }
        return false;
    case MSG_MASTERNODE_ANNOUNCE:
        if (mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) {
            masternodeSync.AddedMasternodeList(inv.hash);
            return true;
        }
        return false;
    case MSG_MASTERNODE_PING:
        return mnodeman.mapSeenMasternodePing.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}

static void RelayTransaction(const CTransaction& tx, CConnman& connman)
{
    CInv inv(MSG_TX, tx.GetHash());
    connman.ForEachNode([&inv](CNode* pnode)
    {
        pnode->PushInventory(inv);
    });
}

static void RelayAddress(const CAddress& addr, bool fReachable, CConnman& connman)
{
    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)

    // Relay to a limited number of other nodes
    // Use deterministic randomness to send to the same nodes for 24 hours
    // at a time so the addrKnowns of the chosen nodes prevent repeats
    uint64_t hashAddr = addr.GetHash();
    std::multimap<uint64_t, CNode*> mapMix;
    const CSipHasher hasher = connman.GetDeterministicRandomizer(RANDOMIZER_ID_ADDRESS_RELAY).Write(hashAddr << 32).Write((GetTime() + hashAddr) / (24*60*60));

    auto sortfunc = [&mapMix, &hasher](CNode* pnode) {
        if (pnode->nVersion >= CADDR_TIME_VERSION) {
            uint64_t hashKey = CSipHasher(hasher).Write(pnode->id).Finalize();
            mapMix.emplace(hashKey, pnode);
        }
    };

    auto pushfunc = [&addr, &mapMix, &nRelayNodes] {
        FastRandomContext insecure_rand;
        for (auto mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
            mi->second->PushAddress(addr, insecure_rand);
    };

    connman.ForEachNodeThen(std::move(sortfunc), std::move(pushfunc));
}

void static ProcessGetData(CNode* pfrom, CConnman& connman, std::atomic<bool>& interruptMsgProc)
{
    AssertLockNotHeld(cs_main);

    std::deque<CInv>::iterator it = pfrom->vRecvGetData.begin();
    std::vector<CInv> vNotFound;
    CNetMsgMaker msgMaker(pfrom->GetSendVersion());
    LOCK(cs_main);

    while (it != pfrom->vRecvGetData.end()) {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->fPauseSend)
            break;

        const CInv& inv = *it;
        {
            if (interruptMsgProc)
                return;
            it++;

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK || inv.type == MSG_CMPCT_BLOCK) {
                bool send = false;
                BlockMap::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end()) {
                    if (chainActive.Contains(mi->second)) {
                        send = true;
                    } else {
                        // To prevent fingerprinting attacks, only send blocks outside of the active
                        // chain if they are valid, and no more than a max reorg depth than the best header
                        // chain we know about.
                        send = mi->second->IsValid(BLOCK_VALID_SCRIPTS) && (pindexBestHeader != NULL) &&
                               (chainActive.Height() - mi->second->nHeight < GetArg("-maxreorg", DEFAULT_MAX_REORG_DEPTH));
                        if (!send) {
                            LogPrintf("ProcessGetData(): ignoring request from peer=%i for old block that isn't in the main chain\n", pfrom->GetId());
                        }
                    }
                }
                // Don't send not-validated blocks
                if (send && (mi->second->nStatus & BLOCK_HAVE_DATA)) {
                    // Send block from disk
                    CBlock block;
                    if (!ReadBlockFromDisk(block, (*mi).second))
                        assert(!"cannot load block from disk");
                    if (inv.type == MSG_BLOCK)
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::BLOCK, block));
                    else if (inv.type == MSG_CMPCT_BLOCK)
                    {
                        CBlockHeaderAndShortTxIDs cmpct(block);
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::CMPCTBLOCK, cmpct));
                    }
                    else // MSG_FILTERED_BLOCK)
                    {
                        bool send = false;
                        CMerkleBlock merkleBlock;
                        {
                            LOCK(pfrom->cs_filter);
                            if (pfrom->pfilter) {
                                send = true;
                                merkleBlock = CMerkleBlock(block, *pfrom->pfilter);
                            }
                        }
                        if (send) {
                            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::MERKLEBLOCK, merkleBlock));
                            // CMerkleBlock just contains hashes, so also push any transactions in the block the client did not see
                            // This avoids hurting performance by pointlessly requiring a round-trip
                            // Note that there is currently no way for a node to request any single transactions we didnt send here -
                            // they must either disconnect and retry or request the full block.
                            // Thus, the protocol spec specified allows for us to provide duplicate txn here,
                            // however we MUST always provide at least what the remote peer needs
                            typedef std::pair<unsigned int, uint256> PairType;
                            for (PairType& pair : merkleBlock.vMatchedTxn)
                                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::TX, block.vtx[pair.first]));
                        }
                        // else
                        // no response
                    }

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue) {
                        // Bypass PushInventory, this must send even if redundant,
                        // and we want it right after the last block so they don't
                        // wait for other stuff first.
                        std::vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, chainActive.Tip()->GetBlockHash()));
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::INV, vInv));
                        pfrom->hashContinue.SetNull();
                    }
                }
            } else if (inv.IsKnownType()) {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    std::map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        connman.PushMessage(pfrom, msgMaker.Make(inv.GetCommand(), (*mi).second));
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_TX) {
                    CTransaction tx;
                    if (mempool.lookup(inv.hash, tx)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::TX, ss));
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_SPORK) {
                    if (mapSporks.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mapSporks[inv.hash];
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::SPORK, ss));
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_MASTERNODE_WINNER) {
                    if (masternodePayments.mapMasternodePayeeVotes.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << masternodePayments.mapMasternodePayeeVotes[inv.hash];
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::MNWINNER, ss));
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_ANNOUNCE) {
                    if (mnodeman.mapSeenMasternodeBroadcast.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnodeman.mapSeenMasternodeBroadcast[inv.hash];
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::MNBROADCAST, ss));
                        pushed = true;
                    }
                }

                if (!pushed && inv.type == MSG_MASTERNODE_PING) {
                    if (mnodeman.mapSeenMasternodePing.count(inv.hash)) {
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << mnodeman.mapSeenMasternodePing[inv.hash];
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::MNPING, ss));
                        pushed = true;
                    }
                }

                if (!pushed) {
                    vNotFound.push_back(inv);
                }
            }

            if (inv.type == MSG_BLOCK || inv.type == MSG_FILTERED_BLOCK)
                break;
        }
    }

    pfrom->vRecvGetData.erase(pfrom->vRecvGetData.begin(), it);

    if (!vNotFound.empty()) {
        // Let the peer know that we didn't find what it asked for, so it doesn't
        // have to wait around forever. Currently only SPV clients actually care
        // about this message: it's needed when they are recursively walking the
        // dependencies of relevant unconfirmed transactions. SPV clients want to
        // do that because they want to know about (and store and rebroadcast and
        // risk analyze) the dependencies of transactions relevant to them, without
        // having to download the entire memory pool.
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::NOTFOUND, vNotFound));
    }
}

bool fRequestedSporksIDB = false;
bool static ProcessMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived, CConnman& connman, std::atomic<bool>& interruptMsgProc)
{
    LogPrint(BCLog::NET, "received: %s (%u bytes) peer=%d\n", SanitizeString(strCommand), vRecv.size(), pfrom->id);
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0) {
        LogPrintf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == NetMsgType::VERSION) {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0) {
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_DUPLICATE, std::string("Duplicate version message")));
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        uint64_t nServiceInt;
        ServiceFlags nServices;
        int nVersion;
        int nSendVersion;
        std::string strSubVer;
        std::string cleanSubVer;
        int nStartingHeight = -1;
        bool fRelay = true;
        vRecv >> nVersion >> nServiceInt >> nTime >> addrMe;
        nSendVersion = std::min(nVersion, PROTOCOL_VERSION);
        nServices = ServiceFlags(nServiceInt);
        if (!pfrom->fInbound) {
            connman.SetServices(pfrom->addr, nServices);
        }
        if (pfrom->nServicesExpected & ~nServices) {
            LogPrint(BCLog::NET, "peer=%d does not offer the expected services (%08x offered, %08x expected); disconnecting\n", pfrom->id, nServices, pfrom->nServicesExpected);
            connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_NONSTANDARD, strprintf("Expected to offer services %08x", pfrom->nServicesExpected)));
            pfrom->fDisconnect = true;
            return false;
        }

        if (nServices == NODE_NONE && sporkManager.IsSporkActive(SPORK_101_SERVICES_ENFORCEMENT)) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return error("No services on version message");
        }

        if (pfrom->DisconnectOldProtocol(nVersion, ActiveProtocol(), strCommand))
            return false;

        if (nVersion == 10300)
            nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(strSubVer, MAX_SUBVERSION_LENGTH);
            cleanSubVer = SanitizeString(strSubVer);
        }
        uint64_t peerGUID = 0;
        size_t guid_pos = cleanSubVer.find("(guid:");
        if (guid_pos != std::string::npos && guid_pos + 22 <= cleanSubVer.size()) {
            try {
                std::string guid_str = cleanSubVer.substr(guid_pos + 6, 16);
                peerGUID = std::stoull(guid_str, nullptr, 16);
            } catch (...) {
                peerGUID = 0;
            }
        }
        pfrom->nPeerGUID = peerGUID;

        if (!vRecv.empty())
            vRecv >> nStartingHeight;
        if (!vRecv.empty())
            vRecv >> fRelay;

        // Disconnect if we connected to ourself
        if (pfrom->fInbound && !connman.CheckIncomingNonce(nNonce)) {
            LogPrintf("connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        if (peerGUID != 0) {
            bool fDuplicate = false;
            connman.ForEachNode([&fDuplicate, pfrom, peerGUID](CNode* pnode) {
                if (pnode != pfrom && pnode->nPeerGUID == peerGUID && (const CNetAddr&)pnode->addr == (const CNetAddr&)pfrom->addr && !pnode->fDisconnect) {
                    fDuplicate = true;
                }
            });
            if (fDuplicate) {
                uint64_t myGUID = connman.GetLocalNodeGUID();
                CNode* pnodeToDisconnect = nullptr;
                if (myGUID > peerGUID) {
                    if (pfrom->fInbound) {
                        pnodeToDisconnect = pfrom;
                    } else {
                        connman.ForEachNode([&pnodeToDisconnect, pfrom, peerGUID](CNode* pnode) {
                            if (pnode != pfrom && pnode->nPeerGUID == peerGUID && (const CNetAddr&)pnode->addr == (const CNetAddr&)pfrom->addr && pnode->fInbound && !pnode->fDisconnect) {
                                pnodeToDisconnect = pnode;
                            }
                        });
                    }
                } else if (myGUID < peerGUID) {
                    if (!pfrom->fInbound) {
                        pnodeToDisconnect = pfrom;
                    } else {
                        connman.ForEachNode([&pnodeToDisconnect, pfrom, peerGUID](CNode* pnode) {
                            if (pnode != pfrom && pnode->nPeerGUID == peerGUID && (const CNetAddr&)pnode->addr == (const CNetAddr&)pfrom->addr && !pnode->fInbound && !pnode->fDisconnect) {
                                pnodeToDisconnect = pnode;
                            }
                        });
                    }
                }
                if (pnodeToDisconnect != nullptr) {
                    LogPrintf("Duplicate connection detected by GUID %016llx (my=%016llx), disconnecting %s connection to %s\n",
                        peerGUID, myGUID, pnodeToDisconnect->fInbound ? "inbound" : "outbound", pnodeToDisconnect->addr.ToString());
                    pnodeToDisconnect->fDisconnect = true;
                    if (pnodeToDisconnect == pfrom) {
                        return true;
                    }
                }
            }
        }

        if (pfrom->fInbound && addrMe.IsRoutable()) {
            SeenLocal(addrMe);
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            PushNodeVersion(pfrom, connman, GetAdjustedTime());

        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::VERACK));

        pfrom->nServices = nServices;
        pfrom->SetAddrLocal(addrMe);
        {
            LOCK(pfrom->cs_SubVer);
            pfrom->strSubVer = strSubVer;
            pfrom->cleanSubVer = cleanSubVer;
        }

        auto shortFromName = pfrom->cleanSubVer.substr(0, pfrom->cleanSubVer.find(':')).substr(0, pfrom->cleanSubVer.find(' '));
        auto shortName = CLIENT_NAME.substr(0, CLIENT_NAME.find(' '));

        for (auto & c: shortFromName) c = toupper(c);
        for (auto & c: shortName) c = toupper(c);

        shortFromName.erase(
            std::remove_if(
                shortFromName.begin(), 
                shortFromName.end(), 
                []( char const& c ) -> bool { return !std::isalnum(c); }), 
            shortFromName.end()
        );

        if (shortName.find(shortFromName) == std::string::npos) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            pfrom->fDisconnect = true;
            return error("Wrong user agent %s", pfrom->cleanSubVer);
        }

        pfrom->nStartingHeight = nStartingHeight;
        pfrom->fClient = !(nServices & NODE_NETWORK);
        {
            LOCK(pfrom->cs_filter);
            pfrom->fRelayTxes = fRelay; // set to true after we get the first filter* message
        }

        // Change version
        pfrom->SetSendVersion(nSendVersion);
        pfrom->nVersion = nVersion;

        {
            LOCK(cs_main);
            // Potentially mark this peer as a preferred download peer.
            UpdatePreferredDownload(pfrom, State(pfrom->GetId()));
        }

        if (!pfrom->fInbound) {
            // Advertise our address
            if (fListen && !IsInitialBlockDownload()) {
                CAddress addr = GetLocalAddress(&pfrom->addr, pfrom->GetLocalServices());
                FastRandomContext insecure_rand;
                if (addr.IsRoutable()) {
                    LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(addrMe);
                    LogPrintf("ProcessMessages: advertising address %s\n", addr.ToString());
                    pfrom->PushAddress(addr, insecure_rand);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || connman.GetAddressCount() < 1000) {
                connman.PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make(NetMsgType::GETADDR));
                pfrom->fGetAddr = true;
            }
            connman.MarkAddressGood(pfrom->addr);
        }

        std::string remoteAddr;
        if (fLogIPs)
            remoteAddr = ", peeraddr=" + pfrom->addr.ToString();

        LogPrintf("receive version message: %s: version %d, blocks=%d, us=%s, peer=%d%s\n",
            cleanSubVer, pfrom->nVersion,
            pfrom->nStartingHeight, addrMe.ToString(), pfrom->id,
            remoteAddr);

        int64_t nTimeOffset = nTime - GetTime();
        pfrom->nTimeOffset = nTimeOffset;
        const int nTimeSlotLength = Params().GetConsensus().nTimeSlotLength;
        if (abs64(nTimeOffset) < 16 * nTimeSlotLength) {
            AddTimeData(pfrom->addr, nTimeOffset, nTimeSlotLength);
        } else {
            LogPrintf("timeOffset (%d seconds) too large. Disconnecting node %s\n",
                nTimeOffset, pfrom->addr.ToString().c_str());
            pfrom->fDisconnect = true;
            CheckOffsetDisconnectedPeers(pfrom->addr);
        }

        // Feeler connections exist only to verify if address is online.
        if (pfrom->fFeeler) {
            assert(pfrom->fInbound == false);
            pfrom->fDisconnect = true;
        }

        // KristaTech: Request active sporks once from the first connected peer on startup.
        if (!fRequestedSporksIDB) {
            LogPrintf("asking peer for sporks\n");
            connman.PushMessage(pfrom, CNetMsgMaker(nSendVersion).Make(NetMsgType::GETSPORKS));
            fRequestedSporksIDB = true;
        }

        return true;
    }


    else if (pfrom->nVersion == 0) {
        // Must have a version message before anything else
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 1);
        return false;
    }

    // At this point, the outgoing message serialization version can't change.
    CNetMsgMaker msgMaker(pfrom->GetSendVersion());

    if (strCommand == NetMsgType::VERACK) {
        pfrom->SetRecvVersion(std::min(pfrom->nVersion.load(), PROTOCOL_VERSION));

        // Mark this node as currently connected, so we update its timestamp later.
        if (pfrom->fNetworkNode) {
            LOCK(cs_main);
            State(pfrom->GetId())->fCurrentlyConnected = true;
        }
        pfrom->fSuccessfullyConnected = true;
    }


    else if (strCommand == NetMsgType::ADDR) {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && connman.GetAddressCount() > 1000)
            return true;
        if (vAddr.size() > 1000) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message addr size() = %u", vAddr.size());
        }

        // Token-bucket rate limiting for ADDR messages (CVE-2021-3401 mitigation)
        const int64_t nAddrNow = GetTime();
        if (pfrom->nLastAddrTokenUpdate == 0) {
            pfrom->nLastAddrTokenUpdate = nAddrNow;
        } else if (nAddrNow > pfrom->nLastAddrTokenUpdate) {
            pfrom->nAddrTokenBucket = std::min(1000.0, pfrom->nAddrTokenBucket + 0.1 * (double)(nAddrNow - pfrom->nLastAddrTokenUpdate));
            pfrom->nLastAddrTokenUpdate = nAddrNow;
        }
        if (pfrom->nAddrTokenBucket < (double)vAddr.size() && !pfrom->fWhitelisted) {
            LogPrint(BCLog::NET, "addr rate limit exceeded from peer=%d (bucket=%.1f, count=%u)\n", pfrom->id, pfrom->nAddrTokenBucket, vAddr.size());
            return true;
        }
        pfrom->nAddrTokenBucket = std::max(0.0, pfrom->nAddrTokenBucket - (double)vAddr.size());

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        for (CAddress& addr : vAddr) {
            if (interruptMsgProc)
                return true;

            if ((addr.nServices & REQUIRED_SERVICES) != REQUIRED_SERVICES)
                continue;

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable()) {
                // Relay to a limited number of other nodes
                RelayAddress(addr, fReachable, connman);
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        connman.AddNewAddresses(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }


    else if (strCommand == NetMsgType::INV) {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message inv size() = %u", vInv.size());
        }

        LOCK(cs_main);

        std::vector<CInv> vToFetch;

        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            const CInv& inv = vInv[nInv];

            if (interruptMsgProc)
                return true;

            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(inv);
            LogPrint(BCLog::NET, "got inv: %s  %s peer=%d\n", inv.ToString(), fAlreadyHave ? "have" : "new", pfrom->id);

            bool fBlocksOnly = GetBoolArg("-blocksonly", false);
            if (fBlocksOnly && inv.type == MSG_TX) {
                continue;
            }
            if (!fAlreadyHave && !fImporting && !fReindex && inv.type != MSG_BLOCK)
                pfrom->AskFor(inv);


            if (inv.type == MSG_BLOCK) {
                UpdateBlockAvailability(pfrom->GetId(), inv.hash);
                if (!fAlreadyHave && !fImporting && !fReindex && !mapBlocksInFlight.count(inv.hash)) {
                    // Add this to the list of blocks to request
                    vToFetch.push_back(inv);
                    LogPrint(BCLog::NET, "getblocks (%d) %s to peer=%d\n", pindexBestHeader->nHeight, inv.hash.ToString(), pfrom->id);
                }
            }
        }

        if (!vToFetch.empty())
            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETDATA, vToFetch));
    }


    else if (strCommand == NetMsgType::GETDATA) {
        std::vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata size() = %u", vInv.size());
        }

        if (vInv.size() != 1)
            LogPrint(BCLog::NET, "received getdata (%u invsz) peer=%d\n", vInv.size(), pfrom->id);

        if (vInv.size() > 0)
            LogPrint(BCLog::NET, "received getdata for: %s peer=%d\n", vInv[0].ToString(), pfrom->id);

        if (pfrom->vRecvGetData.size() + vInv.size() > MAX_INV_SZ) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("message getdata queue overflow (%u + %u)", pfrom->vRecvGetData.size(), vInv.size());
        }
        pfrom->vRecvGetData.insert(pfrom->vRecvGetData.end(), vInv.begin(), vInv.end());
        ProcessGetData(pfrom, connman, interruptMsgProc);
    }


    else if (strCommand == NetMsgType::GETBLOCKS || strCommand == NetMsgType::GETHEADERS) {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        if (locator.vHave.size() > MAX_LOCATOR_SZ) {
            LogPrint(BCLog::NET, "getblocks locator size %lld > %d, disconnect peer=%d\n", locator.vHave.size(), MAX_LOCATOR_SZ, pfrom->GetId());
            pfrom->fDisconnect = true;
            return true;
        }

        LOCK(cs_main);

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = FindForkInGlobalIndex(chainActive, locator);

        // Send the rest of the chain
        if (pindex)
            pindex = chainActive.Next(pindex);
        int nLimit = 500;
        LogPrint(BCLog::NET, "getblocks %d to %s limit %d from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.IsNull() ? "end" : hashStop.ToString(), nLimit, pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex)) {
            if (pindex->GetBlockHash() == hashStop) {
                LogPrint(BCLog::NET, "  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0) {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                LogPrint(BCLog::NET, "  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }


    else if (strCommand == NetMsgType::HEADERS && Params().HeadersFirstSyncingActive()) {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        if (locator.vHave.size() > MAX_LOCATOR_SZ) {
            LogPrint(BCLog::NET, "getblocks locator size %lld > %d, disconnect peer=%d\n", locator.vHave.size(), MAX_LOCATOR_SZ, pfrom->GetId());
            pfrom->fDisconnect = true;
            return true;
        }

        LOCK(cs_main);

        if (IsInitialBlockDownload())
            return true;

        CBlockIndex* pindex = NULL;
        if (locator.IsNull()) {
            // If locator is null, return the hashStop block
            BlockMap::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        } else {
            // Find the last block the caller has in the main chain
            pindex = FindForkInGlobalIndex(chainActive, locator);
            if (pindex)
                pindex = chainActive.Next(pindex);
        }

        // we must use CBlocks, as CBlockHeaders won't include the 0x00 nTx count at the end
        std::vector<CBlock> vHeaders;
        int nLimit = MAX_HEADERS_RESULTS;
        LogPrintf("getheaders %d to %s from peer=%d\n", (pindex ? pindex->nHeight : -1), hashStop.ToString(), pfrom->id);
        for (; pindex; pindex = chainActive.Next(pindex)) {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::HEADERS, vHeaders));
    }


    else if (strCommand == NetMsgType::TX) {
        if (GetBoolArg("-blocksonly", false) && !pfrom->fWhitelisted) {
            return true;
        }
        std::vector<uint256> vWorkQueue;
        std::vector<uint256> vEraseQueue;
        CTransaction tx;

        //masternode signed transaction
        bool ignoreFees = false;
        CTxIn vin;
        std::vector<unsigned char> vchSig;

        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        LOCK(cs_main);

        bool fMissingInputs = false;
        CValidationState state;

        mapAlreadyAskedFor.erase(inv);

        if (AcceptToMemoryPool(mempool, state, tx, true, &fMissingInputs, false, ignoreFees)) {
            mempool.check(pcoinsTip);
            RelayTransaction(tx, connman);
            vWorkQueue.push_back(inv.hash);

            LogPrint(BCLog::MEMPOOL, "%s : peer=%d %s : accepted %s (poolsz %u txn, %u kB)\n",
                __func__, pfrom->id, pfrom->cleanSubVer, tx.GetHash().ToString(),
                mempool.size(), mempool.DynamicMemoryUsage() / 1000);

            // Recursively process any orphan transactions that depended on this one
            std::set<NodeId> setMisbehaving;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++) {
                std::map<uint256, std::set<uint256> >::iterator itByPrev = mapOrphanTransactionsByPrev.find(vWorkQueue[i]);
                if (itByPrev == mapOrphanTransactionsByPrev.end())
                    continue;
                for (std::set<uint256>::iterator mi = itByPrev->second.begin();
                     mi != itByPrev->second.end();
                     ++mi) {
                    const uint256& orphanHash = *mi;
                    const CTransaction& orphanTx = mapOrphanTransactions[orphanHash].tx;
                    NodeId fromPeer = mapOrphanTransactions[orphanHash].fromPeer;
                    bool fMissingInputs2 = false;
                    // Use a dummy CValidationState so someone can't setup nodes to counter-DoS based on orphan
                    // resolution (that is, feeding people an invalid transaction based on LegitTxX in order to get
                    // anyone relaying LegitTxX banned)
                    CValidationState stateDummy;


                    if (setMisbehaving.count(fromPeer))
                        continue;
                    if (AcceptToMemoryPool(mempool, stateDummy, orphanTx, true, &fMissingInputs2)) {
                        LogPrint(BCLog::MEMPOOL, "   accepted orphan tx %s\n", orphanHash.ToString());
                        RelayTransaction(orphanTx, connman);
                        vWorkQueue.push_back(orphanHash);
                        vEraseQueue.push_back(orphanHash);
                    } else if (!fMissingInputs2) {
                        int nDos = 0;
                        if (stateDummy.IsInvalid(nDos) && nDos > 0) {
                            // Punish peer that gave us an invalid orphan tx
                            Misbehaving(fromPeer, nDos);
                            setMisbehaving.insert(fromPeer);
                            LogPrint(BCLog::MEMPOOL, "   invalid orphan tx %s\n", orphanHash.ToString());
                        }
                        // Has inputs but not accepted to mempool
                        // Probably non-standard or insufficient fee/priority
                        LogPrint(BCLog::MEMPOOL, "   removed orphan tx %s\n", orphanHash.ToString());
                        vEraseQueue.push_back(orphanHash);
                        assert(recentRejects);
                        recentRejects->insert(orphanHash);
                    }
                    mempool.check(pcoinsTip);
                }
            }

            for (uint256 hash : vEraseQueue)
                EraseOrphanTx(hash);
        } else if (fMissingInputs) {
            AddOrphanTx(tx, pfrom->GetId());

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nMaxOrphanTx = (unsigned int)std::max((int64_t)0, GetArg("-maxorphantx", DEFAULT_MAX_ORPHAN_TRANSACTIONS));
            unsigned int nEvicted = LimitOrphanTxSize(nMaxOrphanTx);
            if (nEvicted > 0)
                LogPrint(BCLog::MEMPOOL, "mapOrphan overflow, removed %u tx\n", nEvicted);
        } else {
            // AcceptToMemoryPool() returned false, possibly because the tx is
            // already in the mempool; if the tx isn't in the mempool that
            // means it was rejected and we shouldn't ask for it again.
            if (!mempool.exists(tx.GetHash())) {
                assert(recentRejects);
                recentRejects->insert(tx.GetHash());
            }
            if (pfrom->fWhitelisted) {
                // Always relay transactions received from whitelisted peers, even
                // if they were rejected from the mempool, allowing the node to
                // function as a gateway for nodes hidden behind it.
                //
                // FIXME: This includes invalid transactions, which means a
                // whitelisted peer could get us banned! We may want to change
                // that.
                RelayTransaction(tx, connman);
            }
        }

        int nDoS = 0;
        if (state.IsInvalid(nDoS)) {
            LogPrint(BCLog::MEMPOOLREJ, "%s from peer=%d %s was not accepted into the memory pool: %s\n", tx.GetHash().ToString(),
                pfrom->id, pfrom->cleanSubVer,
                FormatStateMessage(state));
            if (state.GetRejectCode() < REJECT_INTERNAL) // Never send AcceptToMemoryPool's internal codes over P2P
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                                               state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash));
            if (nDoS > 0)
                Misbehaving(pfrom->GetId(), nDoS);
        }
        FlushStateToDisk(state, FLUSH_STATE_PERIODIC);
    }


    else if (strCommand == NetMsgType::HEADERS && Params().HeadersFirstSyncingActive() && !fImporting && !fReindex) // Ignore headers received while importing
    {
        std::vector<CBlockHeader> headers;

        // Bypass the normal CBlock deserialization, as we don't want to risk deserializing 2000 full blocks.
        unsigned int nCount = ReadCompactSize(vRecv);
        if (nCount > MAX_HEADERS_RESULTS) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return error("headers message size = %u", nCount);
        }
        headers.resize(nCount);
        for (unsigned int n = 0; n < nCount; n++) {
            vRecv >> headers[n];
            ReadCompactSize(vRecv); // ignore tx count; assume it is 0.
        }

        LOCK(cs_main);

        if (nCount == 0) {
            // Nothing interesting. Stop asking this peers for more headers.
            return true;
        }

        // HeadersSyncState low-work pre-sync protection: avoid polluting mapBlockIndex
        // with large low-work header chains before cumulative work reaches threshold.
        {
            HeadersSyncState& syncState = mapHeadersSyncState[pfrom->GetId()];
            if (!headers.empty()) {
                const arith_uint256 nThresholdWork = chainActive.Tip() ? (UintToArith256(chainActive.Tip()->nChainWork) / 2) : arith_uint256(0);
                CBlockIndex* pindexPrevFirst = nullptr;
                auto miFirst = mapBlockIndex.find(headers[0].hashPrevBlock);
                if (miFirst != mapBlockIndex.end()) {
                    pindexPrevFirst = miFirst->second;
                }

                const bool fInPresync = !syncState.vHeadersBuffer.empty();
                const bool fNeedPresync = fInPresync || (pindexPrevFirst != nullptr && nThresholdWork > 0 && UintToArith256(pindexPrevFirst->nChainWork) < nThresholdWork);

                if (fNeedPresync) {
                    if (!fInPresync) {
                        syncState.nBaseWork = UintToArith256(pindexPrevFirst->nChainWork);
                        syncState.nAccumulatedWork = arith_uint256(0);
                    }
                    const size_t maxPresync = std::max<size_t>(16384, static_cast<size_t>(chainActive.Height()) + 2000);
                    arith_uint256 maxPerHeaderWork = (chainActive.Tip() ? UintToArith256(GetBlockProof(*chainActive.Tip())) : arith_uint256(1)) * 4;
                    if (maxPerHeaderWork == 0) maxPerHeaderWork = arith_uint256(1);

                    for (const CBlockHeader& hdr : headers) {
                        if (!syncState.vHeadersBuffer.empty() && hdr.hashPrevBlock != syncState.hashLast) {
                            Misbehaving(pfrom->GetId(), 20);
                            mapHeadersSyncState.erase(pfrom->GetId());
                            return error("non-continuous presync headers sequence");
                        }
                        if (syncState.vHeadersBuffer.size() >= maxPresync) {
                            Misbehaving(pfrom->GetId(), 20);
                            mapHeadersSyncState.erase(pfrom->GetId());
                            return error("headers presync buffer exceeded without sufficient work");
                        }
                        syncState.vHeadersBuffer.push_back(hdr);
                        syncState.hashLast = hdr.GetHash();
                        CBlockIndex dummy;
                        dummy.nBits = hdr.nBits;
                        syncState.nAccumulatedWork += std::min(UintToArith256(GetBlockProof(dummy)), maxPerHeaderWork);
                    }
                    if (syncState.nBaseWork + syncState.nAccumulatedWork < nThresholdWork) {
                        if (nCount == MAX_HEADERS_RESULTS) {
                            CBlockLocator loc(std::vector<uint256>{syncState.hashLast});
                            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETHEADERS, loc, UINT256_ZERO));
                        }
                        return true;
                    }
                    // Sufficient work reached: flush pre-synced headers into AcceptBlockHeader below
                    headers.swap(syncState.vHeadersBuffer);
                    syncState.vHeadersBuffer.clear();
                    syncState.nAccumulatedWork = arith_uint256(0);
                }
            }
        }

        CBlockIndex* pindexLast = NULL;
        for (const CBlockHeader& header : headers) {
            CValidationState state;
            if (pindexLast != NULL && header.hashPrevBlock != pindexLast->GetBlockHash()) {
                Misbehaving(pfrom->GetId(), 20);
                return error("non-continuous headers sequence");
            }

            /*TODO: this has a CBlock cast on it so that it will compile. There should be a solution for this
             * before headers are reimplemented on mainnet
             */
            if (!AcceptBlockHeader((CBlock)header, state, &pindexLast)) {
                int nDoS;
                if (state.IsInvalid(nDoS)) {
                    if (state.GetRejectReason() == "time-too-new") {
                        CBlockIndex* pindexPrev = nullptr;
                        auto miPrev = mapBlockIndex.find(header.hashPrevBlock);
                        if (miPrev != mapBlockIndex.end() && miPrev->second) {
                            pindexPrev = miPrev->second;
                        }
                        if (IsFutureBlockPreVerified(header, pindexPrev) && header.GetBlockTime() <= GetAdjustedTime() + 300) {
                            const uint256 hashHeader = header.GetHash();
                            LOCK(cs_future_blocks);
                            auto itExisting = mapFutureBlocks.find(hashHeader);
                            if (itExisting != mapFutureBlocks.end() && !itExisting->second.block.vtx.empty()) {
                                continue;
                            }
                            if (itExisting == mapFutureBlocks.end() && mapFutureBlocks.size() >= 64) {
                                auto itMax = mapFutureBlocks.begin();
                                for (auto it = mapFutureBlocks.begin(); it != mapFutureBlocks.end(); ++it) {
                                    if (it->second.block.GetBlockTime() > itMax->second.block.GetBlockTime()) {
                                        itMax = it;
                                    }
                                }
                                if (header.GetBlockTime() < itMax->second.block.GetBlockTime()) {
                                    mapFutureBlocks.erase(itMax);
                                }
                            }
                            if (itExisting != mapFutureBlocks.end() || mapFutureBlocks.size() < 64) {
                                mapFutureBlocks[hashHeader] = {(CBlock)header, pfrom->GetAddrName()};
                                LogPrintf("Deferred future header %s from peer %s until valid\n", hashHeader.ToString(), pfrom->GetAddrName());
                            }
                        }
                        continue;
                    }
                    if (nDoS > 0)
                        Misbehaving(pfrom->GetId(), nDoS);
                    std::string strError = "invalid header received " + header.GetHash().ToString();
                    return error(strError.c_str());
                }
            }
        }

        if (pindexLast)
            UpdateBlockAvailability(pfrom->GetId(), pindexLast->GetBlockHash());

        if (nCount == MAX_HEADERS_RESULTS && pindexLast) {
            // Headers message had its maximum size; the peer may have more headers.
            // TODO: optimize: if pindexLast is an ancestor of chainActive.Tip or pindexBestHeader, continue
            // from there instead.
            LogPrintf("more getheaders (%d) to end to peer=%d (startheight:%d)\n", pindexLast->nHeight, pfrom->id, pfrom->nStartingHeight);
            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexLast), UINT256_ZERO));
        }

        CheckBlockIndex();
    }

    else if (strCommand == NetMsgType::BLOCK && !fImporting && !fReindex) // Ignore blocks received while importing
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();
        CInv inv(MSG_BLOCK, hashBlock);
        LogPrint(BCLog::NET, "received block %s peer=%d\n", inv.hash.ToString(), pfrom->id);

        //sometimes we will be sent their most recent block and its not the one we want, in that case tell where we are
        if (!mapBlockIndex.count(block.hashPrevBlock)) {
            if (find(pfrom->vBlockRequested.begin(), pfrom->vBlockRequested.end(), hashBlock) != pfrom->vBlockRequested.end()) {
                //we already asked for this block, so lets work backwards and ask for the previous block
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETBLOCKS, chainActive.GetLocator(), block.hashPrevBlock));
                pfrom->vBlockRequested.push_back(block.hashPrevBlock);
            } else {
                //ask to sync to this block
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETBLOCKS, chainActive.GetLocator(), hashBlock));
                pfrom->vBlockRequested.push_back(hashBlock);
            }
        } else {
            CValidationState state;
            if (!mapBlockIndex.count(block.GetHash())) {
                ProcessNewBlock(state, pfrom, &block, nullptr, &connman);
                int nDoS;
                if (state.IsInvalid(nDoS)) {
                    if (state.GetRejectReason() == "time-too-new") {
                        bool fPreVerified = false;
                        {
                            LOCK(cs_main);
                            CBlockIndex* pindexPrev = nullptr;
                            auto miPrev = mapBlockIndex.find(block.hashPrevBlock);
                            if (miPrev != mapBlockIndex.end() && miPrev->second) {
                                pindexPrev = miPrev->second;
                            }
                            fPreVerified = (IsFutureBlockPreVerified(block, pindexPrev) && block.GetBlockTime() <= GetAdjustedTime() + 300);
                        }
                        if (fPreVerified) {
                            LOCK(cs_future_blocks);
                            auto itExisting = mapFutureBlocks.find(hashBlock);
                            if (itExisting != mapFutureBlocks.end()) {
                                if (itExisting->second.block.vtx.empty()) {
                                    itExisting->second = {block, pfrom->GetAddrName()};
                                    LogPrintf("Upgraded deferred future header %s to full block from peer %s\n", hashBlock.ToString(), pfrom->GetAddrName());
                                }
                            } else {
                                if (mapFutureBlocks.size() >= 64) {
                                    auto itMax = mapFutureBlocks.begin();
                                    for (auto it = mapFutureBlocks.begin(); it != mapFutureBlocks.end(); ++it) {
                                        if (it->second.block.GetBlockTime() > itMax->second.block.GetBlockTime()) {
                                            itMax = it;
                                        }
                                    }
                                    if (block.GetBlockTime() < itMax->second.block.GetBlockTime()) {
                                        mapFutureBlocks.erase(itMax);
                                    }
                                }
                                if (mapFutureBlocks.size() < 64) {
                                    mapFutureBlocks[hashBlock] = {block, pfrom->GetAddrName()};
                                    LogPrintf("Deferred future block %s from peer %s until valid\n", hashBlock.ToString(), pfrom->GetAddrName());
                                }
                            }
                        }
                    } else {
                        pfrom->AddInventoryKnown(inv);
                        assert(state.GetRejectCode() < REJECT_INTERNAL); // Blocks are never rejected with internal reject codes
                        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::REJECT, strCommand, (unsigned char)state.GetRejectCode(),
                                                       state.GetRejectReason().substr(0, MAX_REJECT_MESSAGE_LENGTH), inv.hash));
                        if (nDoS > 0) {
                            TRY_LOCK(cs_main, lockMain);
                            if (lockMain) Misbehaving(pfrom->GetId(), nDoS);
                        }
                    }
                } else {
                    pfrom->AddInventoryKnown(inv);
                }
                //disconnect this node if its old protocol version
                pfrom->DisconnectOldProtocol(pfrom->nVersion, ActiveProtocol(), strCommand);
            } else {
                pfrom->AddInventoryKnown(inv);
                LogPrint(BCLog::NET, "%s : Already processed block %s, skipping ProcessNewBlock()\n", __func__, block.GetHash().GetHex());
            }
        }
    }

    else if (strCommand == NetMsgType::FEEFILTER) {
        CAmount newFeeFilter = 0;
        vRecv >> newFeeFilter;
        if (Params().GetConsensus().MoneyRange(newFeeFilter)) {
            pfrom->minFeeFilter = newFeeFilter;
            LogPrint(BCLog::NET, "received: feefilter of %s from peer=%d\n", CFeeRate(newFeeFilter).ToString(), pfrom->id);
        }
        return true;
    }

    else if (strCommand == NetMsgType::SENDCMPCT)
    {
        bool fHighBandwidth = false;
        uint64_t nVersion = 0;
        vRecv >> fHighBandwidth >> nVersion;
        if (nVersion == 1) {
            pfrom->fSupportsCompactBlocks = true;
            pfrom->fHighBandwidthCompactBlocks = fHighBandwidth;
            LogPrint(BCLog::CMPCTBLOCK, "sendcmpct: peer=%d, version=1, high_bandwidth=%d\n", pfrom->id, fHighBandwidth);
        }
        return true;
    }

    else if (strCommand == NetMsgType::CMPCTBLOCK && !fImporting && !fReindex)
    {
        CBlockHeaderAndShortTxIDs cmpct;
        vRecv >> cmpct;
        uint256 hashBlock = cmpct.header.GetHash();
        
        {
            CValidationState hdrState;
            if (!CheckBlockHeader(cmpct.header, hdrState, false)) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
                return error("cmpctblock: invalid block header %s", hashBlock.ToString());
            }
            LOCK(cs_main);
            if (mapBlockIndex.count(hashBlock)) {
                // Already have block, ignore
                return true;
            }

            if (!mapBlockIndex.count(cmpct.header.hashPrevBlock)) {
                // Missing parent, request headers
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETHEADERS, chainActive.GetLocator(), hashBlock));
                return true;
            }
        }

        if (cmpct.prefilledtxn.size() + cmpct.shorttxids.size() > std::numeric_limits<uint16_t>::max()) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return error("cmpctblock: too many transactions in compact block");
        }

        CBlock block(cmpct.header);
        block.vtx.resize(cmpct.prefilledtxn.size() + cmpct.shorttxids.size());
        
        // Decode prefilled transactions
        uint32_t abs_index = 0;
        for (size_t i = 0; i < cmpct.prefilledtxn.size(); i++) {
            if (i == 0) {
                abs_index = cmpct.prefilledtxn[i].index;
            } else {
                abs_index += cmpct.prefilledtxn[i].index + 1;
            }
            if (abs_index >= block.vtx.size()) {
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 100);
                return error("cmpctblock: invalid prefilled transaction index");
            }
            block.vtx[abs_index] = cmpct.prefilledtxn[i].tx;
        }
        
        // Map mempool tx by short IDs
        std::map<uint64_t, CTransaction> mempool_shortids;
        std::set<uint64_t> collided_shortids;
        {
            LOCK(mempool.cs);
            for (CTxMemPool::indexed_transaction_set::const_iterator mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi) {
                const CTransaction& tx = mi->GetTx();
                uint64_t sid = cmpct.GetShortID(tx.GetHash());
                if (mempool_shortids.count(sid)) {
                    collided_shortids.insert(sid);
                } else {
                    mempool_shortids[sid] = tx;
                }
            }
        }
        
        // Match short IDs to mempool transactions
        std::vector<uint16_t> missing_indexes;
        uint32_t shorttxid_index = 0;
        for (size_t i = 0; i < block.vtx.size(); i++) {
            if (block.vtx[i].IsNull()) {
                if (shorttxid_index >= cmpct.shorttxids.size()) {
                    LOCK(cs_main);
                    Misbehaving(pfrom->GetId(), 100);
                    return error("cmpctblock: compact block size mismatch");
                }
                uint64_t sid = cmpct.shorttxids[shorttxid_index++];
                if (mempool_shortids.count(sid) && !collided_shortids.count(sid)) {
                    block.vtx[i] = mempool_shortids[sid];
                } else {
                    missing_indexes.push_back(i);
                }
            }
        }

        // Fill PoS block signature if PoS
        if (cmpct.prefilledtxn.size() > 1 && cmpct.prefilledtxn[1].tx.IsCoinStake()) {
            block.vchBlockSig = cmpct.vchBlockSig;
        }
        
        if (!missing_indexes.empty()) {
            // Missing transactions, save per-peer state and send getblocktxn
            {
                LOCK(cs_partiallyDownloadedBlocks);
                size_t peerCount = 0;
                auto firstPeerIt = mapPartiallyDownloadedBlocks.end();
                for (auto it = mapPartiallyDownloadedBlocks.begin(); it != mapPartiallyDownloadedBlocks.end(); ++it) {
                    if (it->first.first == pfrom->GetId()) {
                        if (firstPeerIt == mapPartiallyDownloadedBlocks.end()) firstPeerIt = it;
                        ++peerCount;
                    }
                }
                if (peerCount >= 4 && firstPeerIt != mapPartiallyDownloadedBlocks.end()) {
                    mapPartiallyDownloadedBlocks.erase(firstPeerIt);
                }
                PartiallyDownloadedBlock& pdb = mapPartiallyDownloadedBlocks[std::make_pair(pfrom->GetId(), hashBlock)];
                pdb.cmpct = cmpct;
                pdb.vtx = block.vtx;
                pdb.missing_indexes = missing_indexes;
            }
            BlockTransactionsRequest req;
            req.blockhash = hashBlock;
            req.indexes = missing_indexes;
            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::GETBLOCKTXN, req));
            LogPrint(BCLog::CMPCTBLOCK, "cmpctblock: requesting %d missing transactions for block %s\n", missing_indexes.size(), hashBlock.ToString());
        } else {
            // Fully reconstructed block
            CValidationState state;
            ProcessNewBlock(state, pfrom, &block, nullptr, &connman);
            LogPrint(BCLog::CMPCTBLOCK, "cmpctblock: successfully reconstructed block %s\n", hashBlock.ToString());
        }
        return true;
    }

    else if (strCommand == NetMsgType::GETBLOCKTXN)
    {
        BlockTransactionsRequest req;
        vRecv >> req;
        
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(req.blockhash);
        if (mi == mapBlockIndex.end() || !(mi->second->nStatus & BLOCK_HAVE_DATA)) {
            return error("getblocktxn: do not have block data for %s", req.blockhash.ToString());
        }
        
        CBlock block;
        if (!ReadBlockFromDisk(block, mi->second)) {
            return error("getblocktxn: failed to read block %s from disk", req.blockhash.ToString());
        }
        
        BlockTransactions resp(req);
        resp.txn.reserve(req.indexes.size());
        for (uint16_t idx : req.indexes) {
            if (idx >= block.vtx.size()) {
                Misbehaving(pfrom->GetId(), 100);
                return error("getblocktxn: index %d out of bounds for block %s", idx, req.blockhash.ToString());
            }
            resp.txn.push_back(block.vtx[idx]);
        }
        
        connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::BLOCKTXN, resp));
        LogPrint(BCLog::CMPCTBLOCK, "getblocktxn: sent %d requested transactions for block %s\n", resp.txn.size(), req.blockhash.ToString());
        return true;
    }

    else if (strCommand == NetMsgType::BLOCKTXN && !fImporting && !fReindex)
    {
        BlockTransactions resp;
        vRecv >> resp;
        
        PartiallyDownloadedBlock pdb;
        {
            LOCK(cs_partiallyDownloadedBlocks);
            auto it = mapPartiallyDownloadedBlocks.find(std::make_pair(pfrom->GetId(), resp.blockhash));
            if (it == mapPartiallyDownloadedBlocks.end()) {
                // Unexpected blocktxn
                return true;
            }
            pdb = it->second;
            mapPartiallyDownloadedBlocks.erase(it);
        }
        
        if (resp.txn.size() != pdb.missing_indexes.size()) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
            return error("blocktxn: transaction count mismatch");
        }
        
        CBlock block(pdb.cmpct.header);
        block.vtx = pdb.vtx;
        if (pdb.cmpct.prefilledtxn.size() > 1 && pdb.cmpct.prefilledtxn[1].tx.IsCoinStake()) {
            block.vchBlockSig = pdb.cmpct.vchBlockSig;
        }

        for (size_t i = 0; i < resp.txn.size(); i++) {
            uint16_t idx = pdb.missing_indexes[i];
            if (idx >= block.vtx.size()) {
                return error("blocktxn: invalid index in missing_indexes");
            }
            block.vtx[idx] = resp.txn[i];
        }
        
        CValidationState state;
        ProcessNewBlock(state, pfrom, &block, nullptr, &connman);
        LogPrint(BCLog::CMPCTBLOCK, "blocktxn: successfully reconstructed and processed block %s\n", resp.blockhash.ToString());
        return true;
    }

    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making users (which are behind NAT and can only make outgoing connections) ignore
    // getaddr message mitigates the attack.
    else if ((strCommand == NetMsgType::GETADDR) && (pfrom->fInbound)) {
        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vAddr = connman.GetAddresses();
        FastRandomContext insecure_rand;
        for (const CAddress& addr : vAddr)
            pfrom->PushAddress(addr, insecure_rand);
    }


    else if (strCommand == NetMsgType::MEMPOOL) {
        if (GetBoolArg("-blocksonly", false) && !pfrom->fWhitelisted) {
            return true;
        }
        LOCK2(cs_main, pfrom->cs_filter);

        std::vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        std::vector<CInv> vInv;
        for (uint256& hash : vtxid) {
            CInv inv(MSG_TX, hash);
            CTransaction tx;
            bool fInMemPool = mempool.lookup(hash, tx);
            if (!fInMemPool) continue; // another thread removed since queryHashes, maybe...
            if ((pfrom->pfilter && pfrom->pfilter->IsRelevantAndUpdate(tx)) ||
                (!pfrom->pfilter))
                vInv.push_back(inv);
            if (vInv.size() == MAX_INV_SZ) {
                connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::INV, vInv));
                vInv.clear();
            }
        }
        if (vInv.size() > 0)
            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::INV, vInv));
    }


    else if (strCommand == NetMsgType::PING) {
        if (pfrom->nVersion > BIP0031_VERSION) {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            connman.PushMessage(pfrom, msgMaker.Make(NetMsgType::PONG, nonce));
        }
    }


    else if (strCommand == NetMsgType::PONG) {
        int64_t pingUsecEnd = nTimeReceived;
        uint64_t nonce = 0;
        size_t nAvail = vRecv.in_avail();
        bool bPingFinished = false;
        std::string sProblem;

        if (nAvail >= sizeof(nonce)) {
            vRecv >> nonce;

            // Only process pong message if there is an outstanding ping (old ping without nonce should never pong)
            if (pfrom->nPingNonceSent != 0) {
                if (nonce == pfrom->nPingNonceSent) {
                    // Matching pong received, this ping is no longer outstanding
                    bPingFinished = true;
                    int64_t pingUsecTime = pingUsecEnd - pfrom->nPingUsecStart;
                    if (pingUsecTime > 0) {
                        // Successful ping time measurement, replace previous
                        pfrom->nPingUsecTime = pingUsecTime;
                        pfrom->nMinPingUsecTime = std::min(pfrom->nMinPingUsecTime.load(), pingUsecTime);
                    } else {
                        // This should never happen
                        sProblem = "Timing mishap";
                    }
                } else {
                    // Nonce mismatches are normal when pings are overlapping
                    sProblem = "Nonce mismatch";
                    if (nonce == 0) {
                        // This is most likely a bug in another implementation somewhere, cancel this ping
                        bPingFinished = true;
                        sProblem = "Nonce zero";
                    }
                }
            } else {
                sProblem = "Unsolicited pong without ping";
            }
        } else {
            // This is most likely a bug in another implementation somewhere, cancel this ping
            bPingFinished = true;
            sProblem = "Short payload";
        }

        if (!(sProblem.empty())) {
            LogPrint(BCLog::NET, "pong peer=%d %s: %s, %x expected, %x received, %u bytes\n",
                pfrom->id,
                pfrom->cleanSubVer,
                sProblem,
                pfrom->nPingNonceSent,
                nonce,
                nAvail);
        }
        if (bPingFinished) {
            pfrom->nPingNonceSent = 0;
        }
    }

    else if (!(pfrom->GetLocalServices() & NODE_BLOOM) &&
             (strCommand == NetMsgType::FILTERLOAD ||
                 strCommand == NetMsgType::FILTERADD ||
                 strCommand == NetMsgType::FILTERCLEAR)) {
        LogPrintf("bloom message=%s\n", strCommand);
        LOCK(cs_main);
        Misbehaving(pfrom->GetId(), 100);
    }

    else if (strCommand == NetMsgType::FILTERLOAD) {
        CBloomFilter filter;
        vRecv >> filter;

        if (!filter.IsWithinSizeConstraints()) {
            // There is no excuse for sending a too-large filter
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
        } else {
            LOCK(pfrom->cs_filter);
            delete pfrom->pfilter;
            pfrom->pfilter = new CBloomFilter(filter);
            pfrom->pfilter->UpdateEmptyFull();
            pfrom->fRelayTxes = true;
        }
    }


    else if (strCommand == NetMsgType::FILTERADD) {
        std::vector<unsigned char> vData;
        vRecv >> vData;

        // Nodes must NEVER send a data item > 520 bytes (the max size for a script data object,
        // and thus, the maximum size any matched object can have) in a filteradd message
        bool bad = false;
        if (vData.size() > MAX_SCRIPT_ELEMENT_SIZE) {
            bad = true;
        } else {
            LOCK(pfrom->cs_filter);
            if (pfrom->pfilter) {
                pfrom->pfilter->insert(vData);
            } else {
                bad = true;
            }
        }
        if (bad) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 100);
        }
    }


    else if (strCommand == NetMsgType::FILTERCLEAR) {
        LOCK(pfrom->cs_filter);
        delete pfrom->pfilter;
        pfrom->pfilter = new CBloomFilter();
        pfrom->fRelayTxes = true;
    }


    else if (strCommand == NetMsgType::ADAMSOL) {
        CAdamSolutionMsg msg;
        try {
            vRecv >> msg;
        } catch (const std::exception& e) {
            LogPrintf("ProcessMessage: adamsol: Failed to deserialize solution message: %s\n", e.what());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        if (!msg.minerKey.IsValid() || msg.vchSolution.empty() || msg.vchSolution.size() > 512) {
            LogPrintf("ProcessMessage: adamsol: Invalid miner public key or solution size (%u) from peer %d\n", (unsigned int)msg.vchSolution.size(), pfrom->GetId());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        uint256 prevBlockHash = msg.hashPrevBlock;
        {
            LOCK(cs_adam_solutions);
            auto itBlock = mapAdamSolutionsCache.find(prevBlockHash);
            if (itBlock != mapAdamSolutionsCache.end()) {
                auto itSol = itBlock->second.find(msg.minerKey);
                if (itSol != itBlock->second.end() && itSol->second == msg.vchSolution) {
                    return true;
                }
            }
        }
        CBlockIndex* pindexPrev = nullptr;
        {
            LOCK(cs_main);
            if (mapBlockIndex.count(prevBlockHash)) {
                pindexPrev = mapBlockIndex[prevBlockHash];
            }
        }

        if (!pindexPrev) {
            LogPrint(BCLog::MASTERNODE, "ProcessMessage: adamsol: Predecessor index not found for hash %s, caching as orphan.\n", prevBlockHash.ToString());
            LOCK(cs_adam_solutions);
            if (mapOrphanAdamSolutions.size() >= 20) {
                mapOrphanAdamSolutions.erase(mapOrphanAdamSolutions.begin());
            }
            auto& orphans = mapOrphanAdamSolutions[prevBlockHash];
            if (orphans.size() < 20) {
                orphans.push_back(msg);
            }
            return true;
        }

        {
            LOCK(cs_main);
            if (chainActive.Tip() &&
                (pindexPrev->nHeight + 2 < chainActive.Height() ||
                 pindexPrev->nHeight > chainActive.Height() + 2)) {
                return true;
            }
        }

        uint256 adamSeed = GetAdamSeed(pindexPrev);
        const Consensus::Params& consensus = Params().GetConsensus();
        int nNextHeight = pindexPrev->nHeight + 1;

        std::vector<CPubKey> vExpectedMiners;
        std::vector<CPubKey> vStandbyMiners;
        CPubKey expectedCoordinator;
        if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nNextHeight, nullptr, &vStandbyMiners)) {
            LogPrintf("ProcessMessage: adamsol: SelectAdamNodes failed for seed %s\n", adamSeed.ToString());
            return true;
        }

        int electedIdx = -1;
        for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
            if (vExpectedMiners[i] == msg.minerKey) {
                electedIdx = (int)i;
                break;
            }
        }
        if (electedIdx < 0) {
            for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
                if (vStandbyMiners[s] == msg.minerKey) {
                    electedIdx = (int)vExpectedMiners.size() + (int)s;
                    break;
                }
            }
        }

        if (electedIdx < 0) {
            LogPrintf("ProcessMessage: adamsol: miner key %s is not elected for tip %s\n",
                msg.minerKey.GetID().ToString(), prevBlockHash.ToString());
            return true;
        }

        CBlockHeader dummyHeader;
        if (consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
            dummyHeader.nVersion = 12;
        } else {
            dummyHeader.nVersion = 11;
        }
        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);

        if (!VerifyAdamSolution(prevBlockHash, adamSeed, msg.minerKey, msg.vchSolution, nBits, dummyHeader.nVersion, nNextHeight, electedIdx)) {
            LogPrintf("ProcessMessage: adamsol: VerifyAdamSolution failed for miner %s and tip %s\n",
                msg.minerKey.GetID().ToString(), prevBlockHash.ToString());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 10);
            return true;
        }

        bool isNew = false;
        bool shouldVerifyOld = false;
        std::vector<unsigned char> vchOldSol;
        {
            LOCK(cs_adam_solutions);
            if (mapAdamSolutionsCache.size() > 1000 && !mapAdamSolutionsCache.count(prevBlockHash)) {
                auto itEvict = mapAdamSolutionsCache.begin();
                while (itEvict != mapAdamSolutionsCache.end() &&
                       (itEvict->first == prevBlockHash || (chainActive.Tip() && itEvict->first == chainActive.Tip()->GetBlockHash()))) {
                    ++itEvict;
                }
                if (itEvict != mapAdamSolutionsCache.end()) {
                    mapAdamSolutionsCache.erase(itEvict);
                }
            }
            auto& solutionsForBlock = mapAdamSolutionsCache[prevBlockHash];
            auto solIt = solutionsForBlock.find(msg.minerKey);
            if (solIt == solutionsForBlock.end()) {
                solutionsForBlock[msg.minerKey] = msg.vchSolution;
                isNew = true;
            } else {
                vchOldSol = solIt->second;
                shouldVerifyOld = true;
            }
        }

        if (shouldVerifyOld) {
            if (!VerifyAdamSolution(prevBlockHash, adamSeed, msg.minerKey, vchOldSol, nBits, dummyHeader.nVersion, nNextHeight, electedIdx)) {
                LOCK(cs_adam_solutions);
                mapAdamSolutionsCache[prevBlockHash][msg.minerKey] = msg.vchSolution;
                isNew = true;
                LogPrintf("ProcessMessage: Overwriting invalid/old-version cached solution for miner %s\n", msg.minerKey.GetID().ToString());
            }
        }

        if (isNew) {
            LogPrintf("ProcessMessage: Received and cached new adamsol for miner key %s and tip %s\n",
                msg.minerKey.GetID().ToString(), prevBlockHash.ToString());
            
            if (g_connman) {
                g_connman->ForEachNode([&msg, pfrom](CNode* pnode) {
                    if (pnode->id != pfrom->id) {
                        g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::ADAMSOL, msg));
                    }
                });
            }
            if (!fGenerateBitcoins) {
                TryAssembleCoordinatorBlockAsync(prevBlockHash);
            }
        }
    }

    else if (strCommand == NetMsgType::PROPOSEBLOCK) {
        CBlockProposeMsg msg;
        try {
            vRecv >> msg;
        } catch (const std::exception& e) {
            LogPrintf("ProcessMessage: qblockprop: Failed to deserialize propose message: %s\n", e.what());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        uint256 blockHash = msg.block.GetHash();

        // Track seen proposed blocks to avoid infinite loop / double relay.
        // On a 4s coordinator re-broadcast, reply to pfrom with any cached quorum signature shares for this block.
        bool fAlreadySeen = false;
        {
            LOCK(cs_seen_prop);
            fAlreadySeen = (setSeenProposedBlocks.count(blockHash) > 0);
        }
        if (fAlreadySeen) {
            std::vector<CQuorumSigShareMsg> vCachedShares;
            {
                LOCK(cs_quorum_sigs);
                auto itBlock = mapQuorumBlockSigs.find(blockHash);
                if (itBlock != mapQuorumBlockSigs.end()) {
                    for (const auto& pair : itBlock->second) {
                        CQuorumSigShareMsg sigShare;
                        sigShare.blockHash = blockHash;
                        sigShare.collateralOutpoint = pair.first;
                        sigShare.vchSig = pair.second;
                        vCachedShares.push_back(std::move(sigShare));
                    }
                }
            }
            for (const auto& sigShare : vCachedShares) {
                connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::QUORUMSIGSHARE, sigShare));
            }
            return true;
        }

        CBlockIndex* pindexPrev = nullptr;
        {
            LOCK(cs_main);
            auto mi = mapBlockIndex.find(msg.block.hashPrevBlock);
            if (mi != mapBlockIndex.end()) {
                pindexPrev = mi->second;
            }
        }
        if (!pindexPrev) {
            LogPrint(BCLog::MASTERNODE, "ProcessMessage: qblockprop: Predecessor index not found for proposed block %s\n", blockHash.ToString());
            return true;
        }

        if (msg.block.nVersion != 12) {
            LogPrintf("ProcessMessage: qblockprop: Rejecting non-v12 proposed block %s (version=%d)\n", blockHash.ToString(), msg.block.nVersion);
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        CValidationState state;
        if (!CheckBlockHeader(msg.block, state, !msg.block.IsProofOfStake())) {
            LogPrintf("ProcessMessage: qblockprop: Invalid block header for proposed block %s: %s\n", blockHash.ToString(), FormatStateMessage(state));
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        const Consensus::Params& consensus = Params().GetConsensus();
        const int nProposalHeight = pindexPrev->nHeight + 1;
        const bool fFallbackMode = (msg.block.nVersion == 11);
        if (msg.block.GetBlockTime() <= pindexPrev->GetBlockTime()) {
            LogPrintf("ProcessMessage: qblockprop: Proposed block %s has timestamp <= parent timestamp\n", blockHash.ToString());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }
        int64_t nBlockTimeDiff = std::max<int64_t>(0, msg.block.GetBlockTime() - pindexPrev->GetBlockTime());
        uint256 adamSeed = GetAdamSeed(pindexPrev);
        std::vector<CPubKey> vExpectedMiners;
        std::vector<CPubKey> vStandbyMiners;
        CPubKey expectedCoordinator;

        const CCoinsViewCache* pProposalView = nullptr;
        {
            LOCK(cs_main);
            if (pcoinsTip && pcoinsTip->GetBestBlock() == msg.block.hashPrevBlock) {
                pProposalView = pcoinsTip;
            }
        }
        if (!pProposalView) {
            LogPrint(BCLog::MASTERNODE, "ProcessMessage: qblockprop: Deferring proposal %s because parent %s is not current tip\n",
                     blockHash.ToString(), msg.block.hashPrevBlock.ToString());
            return true;
        }

        if (fFallbackMode) {
            int nActiveMns = GetActiveMasternodePoolCount(nProposalHeight - 1, pProposalView);
            int threshold = consensus.GetAdamEffectiveThreshold(nProposalHeight, nBlockTimeDiff, nActiveMns);
            if (msg.block.vAdamMiners.size() < (size_t)threshold ||
                msg.block.vAdamMiners.size() > (size_t)(consensus.nAdamMinersCount + 3) ||
                (msg.block.vAdamSolutions.size() != msg.block.vAdamMiners.size() - 1 &&
                 msg.block.vAdamSolutions.size() != msg.block.vAdamMiners.size())) {
                LogPrintf("ProcessMessage: qblockprop: Fallback miners or solutions size mismatch for proposed block %s\n", blockHash.ToString());
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
                return true;
            }
            expectedCoordinator = msg.block.vAdamMiners.back();
        } else {
            if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nProposalHeight, pProposalView, &vStandbyMiners)) {
                LogPrintf("ProcessMessage: qblockprop: SelectAdamNodes failed for proposed block %s\n", blockHash.ToString());
                return true;
            }

            if (!ValidateAdamCommitteeForTime(
                    msg.block.hashPrevBlock, adamSeed, nProposalHeight, nBlockTimeDiff,
                    msg.block.nBits, msg.block.nVersion, msg.block.vAdamMiners, msg.block.vAdamSolutions,
                    vExpectedMiners, vStandbyMiners, nullptr, nullptr, pProposalView)) {
                LogPrintf("ProcessMessage: qblockprop: Elected miners or solutions size mismatch for proposed block %s\n", blockHash.ToString());
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
                return true;
            }
        }

        CPubKey signingCoordinator = expectedCoordinator;
        bool fCoordSigValid = false;
        if (!msg.block.vAdamCoordinatorSig.empty() && !msg.block.vAdamVRFProof.empty()) {
            fCoordSigValid = VerifyAdamCoordinatorSig(msg.block, signingCoordinator, &blockHash);
            if (!fCoordSigValid && !fFallbackMode && nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                std::map<CPubKey, std::vector<unsigned char>> mapBlockSolutions;
                for (size_t i = 0; i < msg.block.vAdamMiners.size() && i < msg.block.vAdamSolutions.size(); ++i) {
                    if (!msg.block.vAdamSolutions[i].empty() &&
                        VerifyAdamSolution(msg.block.hashPrevBlock, adamSeed, msg.block.vAdamMiners[i], msg.block.vAdamSolutions[i], msg.block.nBits, msg.block.nVersion, nProposalHeight, (int)i)) {
                        mapBlockSolutions[msg.block.vAdamMiners[i]] = msg.block.vAdamSolutions[i];
                    }
                }
                CPubKey activeFallback, prevFallback;
                int rotationIndex = 0;
                if (GetAdamFallbackCoordinatorForSlot(adamSeed, nProposalHeight, nBlockTimeDiff, msg.block.vAdamMiners, mapBlockSolutions, activeFallback, prevFallback, rotationIndex)) {
                    if (VerifyAdamCoordinatorSig(msg.block, activeFallback, &blockHash)) {
                        signingCoordinator = activeFallback;
                        fCoordSigValid = true;
                    } else if (VerifyAdamCoordinatorSig(msg.block, prevFallback, &blockHash)) {
                        signingCoordinator = prevFallback;
                        fCoordSigValid = true;
                    }
                }
            }
        }

        if (!fCoordSigValid ||
            !VerifyAdamVRFProof(adamSeed, msg.block.vAdamVRFProof, signingCoordinator)) {
            LogPrintf("ProcessMessage: qblockprop: Invalid coordinator signature or VRF proof for proposed block %s\n", blockHash.ToString());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        // Validate block structure, transactions, solutions, PoS signature/kernel, and UTXO/reward rules before relaying or signing
        bool fProposalValid = CheckBlock(msg.block, state, true, true, false, pcoinsTip);
        if (fProposalValid) {
            LOCK(cs_main);
            const bool enableP2PKH = consensus.NetworkUpgradeActive(nProposalHeight, Consensus::UPGRADE_P2PKH_BLOCK_SIGNATURES);
            if (!CheckWork(msg.block, pindexPrev) ||
                !ContextualCheckBlockHeader(msg.block, state, pindexPrev) ||
                !ContextualCheckBlock(msg.block, state, pindexPrev)) {
                fProposalValid = false;
            }
            if (fProposalValid && msg.block.IsProofOfStake()) {
                std::string strPoSError;
                if (!CheckBlockSignature(msg.block, enableP2PKH) ||
                    !CheckProofOfStake(msg.block, strPoSError, pindexPrev)) {
                    fProposalValid = false;
                }
            }
            if (fProposalValid && pindexPrev == chainActive.Tip() && pcoinsTip && pcoinsTip->GetBestBlock() == pindexPrev->GetBlockHash()) {
                CCoinsViewCache viewNew(pcoinsTip);
                CBlockIndex indexDummy(msg.block);
                indexDummy.pprev = pindexPrev;
                indexDummy.nHeight = nProposalHeight;
                if (!ConnectBlock(msg.block, state, &indexDummy, viewNew, true, true)) {
                    fProposalValid = false;
                }
            }
        }
        if (!fProposalValid) {
            int nDoS = 0;
            LOCK(cs_main);
            if (state.IsInvalid(nDoS) && nDoS > 0) {
                Misbehaving(pfrom->GetId(), nDoS);
            } else {
                Misbehaving(pfrom->GetId(), 10);
            }
            LogPrintf("ProcessMessage: qblockprop: Proposed block %s validation failed: %s\n", blockHash.ToString(), FormatStateMessage(state));
            return true;
        }

        {
            LOCK(cs_seen_prop);
            if (!setSeenProposedBlocks.insert(blockHash).second) {
                return true;
            }
            if (setSeenProposedBlocks.size() > 1000) {
                auto itEvict = setSeenProposedBlocks.begin();
                if (itEvict != setSeenProposedBlocks.end() && *itEvict == blockHash) {
                    ++itEvict;
                }
                if (itEvict != setSeenProposedBlocks.end()) {
                    setSeenProposedBlocks.erase(itEvict);
                }
            }
        }

        // Relay the verified PROPOSEBLOCK message to all other peers
        connman.ForEachNode([&msg, pfrom, &connman](CNode* pnode) {
            if (pnode != pfrom) {
                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::PROPOSEBLOCK, msg));
            }
        });

        int nHeight = pindexPrev->nHeight + 1;
        
        // Quorum listesini al
        llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev, pcoinsTip);
        
        // Find all quorum members for which we hold the private keys (including wallet, activeMasternode, masternode.conf, and Regtest)
        struct QuorumSigner {
            llmq::CQuorumMember member;
            CKey key;
        };
        std::vector<QuorumSigner> vSigners;

        for (const auto& member : quorum.members) {
            CKey keyMasternode;
            if (llmq::GetMasternodePrivKey(member.pubKeyMasternode, keyMasternode)) {
                vSigners.push_back({member, keyMasternode});
            }
        }

        if (vSigners.empty()) {
            LogPrint(BCLog::MASTERNODE, "ProcessMessage: qblockprop: Not an active quorum member for height %d\n", nHeight);
            return true;
        }

        // LLMQ Primary-Solution Anti-Substitution Veto (R35-1):
        // Evaluated AFTER setSeenProposedBlocks insertion and P2P relay so non-LLMQ nodes still relay
        // valid proposals to LLMQ members and vetoed proposals are not re-validated repeatedly.
        if (!fFallbackMode && nBlockTimeDiff >= ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS) {
            std::map<CPubKey, std::vector<unsigned char>> localSolutions;
            {
                LOCK(cs_adam_solutions);
                auto itSol = mapAdamSolutionsCache.find(msg.block.hashPrevBlock);
                if (itSol != mapAdamSolutionsCache.end()) {
                    localSolutions = itSol->second;
                }
            }
            if (!localSolutions.empty()) {
                for (size_t i = 0; i < vExpectedMiners.size() && i < msg.block.vAdamMiners.size() && i < msg.block.vAdamSolutions.size(); ++i) {
                    if (msg.block.vAdamMiners[i] != vExpectedMiners[i]) {
                        auto itPrimarySol = localSolutions.find(vExpectedMiners[i]);
                        if (itPrimarySol != localSolutions.end() && !itPrimarySol->second.empty() &&
                            VerifyAdamSolution(msg.block.hashPrevBlock, adamSeed, vExpectedMiners[i], itPrimarySol->second, msg.block.nBits, msg.block.nVersion, nProposalHeight, (int)i)) {
                            LogPrintf("ProcessMessage: qblockprop: Refusing to sign proposal %s that substitutes primary validator seat %d (%s) despite valid local solution\n",
                                      blockHash.ToString(), (int)i, vExpectedMiners[i].GetID().ToString());
                            return true;
                        }
                    }
                }
            }
        }

        // Anti-equivocation: never let our quorum keys sign two competing equal-height blocks from different coordinators
        if (!llmq::AllowQuorumSignature(msg.block.hashPrevBlock, signingCoordinator, blockHash)) {
            return true;
        }

        // Sign, cache locally, and broadcast signature shares to all peers (using deterministic BLS12-381 + secp256k1 authorization)
        for (const auto& signer : vSigners) {
            std::vector<unsigned char> vchSig;
            CBLSSecretKey quorumBlsKey = DeriveBLSFromCKey(signer.key);
            if (!(quorumBlsKey.IsValid() && SignBLSWithECDSAFallback(blockHash, signer.key, quorumBlsKey, vchSig))) {
                LogPrintf("ProcessMessage: qblockprop: Failed to sign proposed block hash %s\n", blockHash.ToString());
                continue;
            }

            std::set<uint256> seenPropsSnapshot;
            {
                LOCK(cs_seen_prop);
                seenPropsSnapshot = setSeenProposedBlocks;
            }
            {
                LOCK(cs_quorum_sigs);
                if (mapQuorumBlockSigs.size() > 1000 && !mapQuorumBlockSigs.count(blockHash)) {
                    auto itEvict = mapQuorumBlockSigs.begin();
                    while (itEvict != mapQuorumBlockSigs.end() &&
                           (itEvict->first == blockHash || seenPropsSnapshot.count(itEvict->first) > 0)) {
                        ++itEvict;
                    }
                    if (itEvict == mapQuorumBlockSigs.end()) {
                        itEvict = mapQuorumBlockSigs.begin();
                        if (itEvict != mapQuorumBlockSigs.end() && itEvict->first == blockHash) {
                            ++itEvict;
                        }
                    }
                    if (itEvict != mapQuorumBlockSigs.end()) {
                        mapQuorumBlockSigs.erase(itEvict);
                    }
                }
                mapQuorumBlockSigs[blockHash][signer.member.collateralOutpoint] = vchSig;
            }

            // Signature share mesajını oluştur ve tüm bağlı peer'lara yayınla
            CQuorumSigShareMsg sigShare;
            sigShare.blockHash = blockHash;
            sigShare.collateralOutpoint = signer.member.collateralOutpoint;
            sigShare.vchSig = vchSig;

            LogPrintf("ProcessMessage: qblockprop: Successfully signed block %s for height %d. Broadcasting signature share to peers\n",
                blockHash.ToString(), nHeight);

            connman.ForEachNode([&sigShare, &connman](CNode* pnode) {
                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::QUORUMSIGSHARE, sigShare));
            });
        }
    }

    else if (strCommand == NetMsgType::QUORUMSIGSHARE) {
        CQuorumSigShareMsg msg;
        try {
            vRecv >> msg;
        } catch (const std::exception& e) {
            LogPrintf("ProcessMessage: qsigshare: Failed to deserialize signature share: %s\n", e.what());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        if (msg.vchSig.empty() || msg.vchSig.size() > 512) {
            LogPrintf("ProcessMessage: qsigshare: Invalid signature share size (%u) from peer %d\n", (unsigned int)msg.vchSig.size(), pfrom->GetId());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        {
            LOCK(cs_seen_prop);
            if (!setSeenProposedBlocks.count(msg.blockHash)) {
                LogPrint(BCLog::MASTERNODE, "ProcessMessage: qsigshare: Unknown proposed block %s from peer %d\n", msg.blockHash.ToString(), pfrom->GetId());
                return true;
            }
        }
        {
            LOCK(cs_quorum_sigs);
            auto itBlock = mapQuorumBlockSigs.find(msg.blockHash);
            if (itBlock != mapQuorumBlockSigs.end() && itBlock->second.count(msg.collateralOutpoint)) {
                return true;
            }
        }

        // Aktif zincirin bir üst yüksekliği için quorum'u al
        int nHeight = 0;
        const CBlockIndex* pindexTip = nullptr;
        llmq::CQuorum quorum;
        {
            LOCK(cs_main);
            pindexTip = chainActive.Tip();
            nHeight = pindexTip ? (pindexTip->nHeight + 1) : 1;
            quorum = llmq::GetActiveQuorum(nHeight, pindexTip, pcoinsTip);
        }
        bool found = false;
        CPubKey memberPubKey;
        for (const auto& member : quorum.members) {
            if (member.collateralOutpoint == msg.collateralOutpoint) {
                found = true;
                memberPubKey = member.pubKeyMasternode;
                break;
            }
        }

        if (!found) {
            LogPrint(BCLog::MASTERNODE, "QUORUMSIGSHARE: Signer %s is not a member of active quorum for block %s\n", msg.collateralOutpoint.ToString(), msg.blockHash.ToString());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        // Verify signature share using hybrid BLS12-381 + secp256k1 authorization
        if (!VerifyBLSWithECDSAFallback(msg.blockHash, memberPubKey, msg.vchSig)) {
            LogPrintf("ProcessMessage: qsigshare: Invalid signature verification from outpoint %s for block %s\n",
                msg.collateralOutpoint.ToString(), msg.blockHash.ToString());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 10);
            return true;
        }

        // Geçerli imzayı önbelleğe kaydet
        bool isNewSignature = false;
        size_t totalSigs = 0;
        std::set<uint256> seenPropsSnapshot;
        {
            LOCK(cs_seen_prop);
            seenPropsSnapshot = setSeenProposedBlocks;
        }
        {
            LOCK(cs_quorum_sigs);
            if (mapQuorumBlockSigs.size() > 1000 && !mapQuorumBlockSigs.count(msg.blockHash)) {
                auto itEvict = mapQuorumBlockSigs.begin();
                while (itEvict != mapQuorumBlockSigs.end() &&
                       (itEvict->first == msg.blockHash || seenPropsSnapshot.count(itEvict->first) > 0)) {
                    ++itEvict;
                }
                if (itEvict == mapQuorumBlockSigs.end()) {
                    return true;
                }
                mapQuorumBlockSigs.erase(itEvict);
            }
            if (!mapQuorumBlockSigs.count(msg.blockHash) || !mapQuorumBlockSigs[msg.blockHash].count(msg.collateralOutpoint)) {
                mapQuorumBlockSigs[msg.blockHash][msg.collateralOutpoint] = msg.vchSig;
                isNewSignature = true;
            }
            totalSigs = mapQuorumBlockSigs[msg.blockHash].size();
        }

        if (isNewSignature) {
            LogPrintf("ProcessMessage: qsigshare: Valid signature share accepted from %s for block %s. Total signatures for this block: %d\n",
                msg.collateralOutpoint.ToString(), msg.blockHash.ToString(), totalSigs);

            // Relay the QUORUMSIGSHARE message to all other peers
            connman.ForEachNode([&msg, pfrom, &connman](CNode* pnode) {
                if (pnode != pfrom) {
                    connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::QUORUMSIGSHARE, msg));
                }
            });
        }
    }

    else if (strCommand == NetMsgType::POSCOORDREQ) {
        CPoSCoordSignRequestMsg msg;
        try {
            vRecv >> msg;
        } catch (const std::exception& e) {
            LogPrintf("ProcessMessage: poscoordreq: Failed to deserialize request: %s\n", e.what());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        if (!msg.block.IsProofOfStake() || msg.block.nVersion < 11 || !msg.targetCoordinator.IsValid()) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        const uint256 reqHash = ComputePoSCoordRequestHash(msg.block, msg.targetCoordinator);

        bool fAlreadySeen = false;
        CPoSCoordSigEntry cachedResp;
        bool fHaveCachedResp = false;
        {
            LOCK(cs_pos_coord_sigs);
            fAlreadySeen = (mapSeenPoSCoordReqs.count(reqHash) > 0);
            auto itResp = mapPoSCoordSigs.find(reqHash);
            if (itResp != mapPoSCoordSigs.end()) {
                cachedResp = itResp->second;
                fHaveCachedResp = true;
            }
        }
        if (fAlreadySeen) {
            if (fHaveCachedResp) {
                CPoSCoordSignResponseMsg respMsg;
                respMsg.reqHash = reqHash;
                respMsg.coordinatorPubKey = cachedResp.coordinatorPubKey;
                respMsg.vAdamVRFProof = cachedResp.vAdamVRFProof;
                respMsg.vAdamCoordinatorSig = cachedResp.vAdamCoordinatorSig;
                connman.PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::POSCOORDSIG, respMsg));
            }
            return true;
        }

        const Consensus::Params& consensus = Params().GetConsensus();
        CBlockIndex* pindexPrev = nullptr;
        int nProposalHeight = 0;
        bool fDraftValid = false;
        uint256 adamSeed;
        {
            LOCK(cs_main);
            auto mi = mapBlockIndex.find(msg.block.hashPrevBlock);
            if (mi != mapBlockIndex.end()) {
                pindexPrev = mi->second;
            }
            if (!pindexPrev || pindexPrev != chainActive.Tip() || !pcoinsTip || pcoinsTip->GetBestBlock() != msg.block.hashPrevBlock) {
                return true;
            }
            nProposalHeight = pindexPrev->nHeight + 1;
            const bool enableP2PKH = consensus.NetworkUpgradeActive(nProposalHeight, Consensus::UPGRADE_P2PKH_BLOCK_SIGNATURES);
            if (!VerifyPoSCoordRequestAuth(msg.block, reqHash, msg.vchStakerAuthSig, enableP2PKH)) {
                LogPrintf("ProcessMessage: poscoordreq: Invalid staker auth signature on req %s\n", reqHash.ToString());
                Misbehaving(pfrom->GetId(), 20);
                return true;
            }

            CValidationState stateTime;
            if (!CheckBlockTime(msg.block, stateTime, pindexPrev) ||
                (!Params().IsRegTestNet() && msg.block.GetBlockTime() < pindexPrev->GetBlockTime() + consensus.nTargetSpacing)) {
                int nTimeDoS = 0;
                if (stateTime.IsInvalid(nTimeDoS) && nTimeDoS > 0) {
                    Misbehaving(pfrom->GetId(), 20);
                }
                return true;
            }

            adamSeed = GetAdamSeed(pindexPrev);
            const int64_t nBlockTimeDiff = std::max<int64_t>(0, msg.block.GetBlockTime() - pindexPrev->GetBlockTime());
            const bool fFallbackMode = (msg.block.nVersion == 11);
            std::vector<CPubKey> vExpectedMiners;
            std::vector<CPubKey> vStandbyMiners;
            CPubKey expectedCoordinator;
            bool fValidTargetCoordinator = false;
            if (fFallbackMode) {
                if (!msg.block.vAdamMiners.empty() && msg.block.vAdamMiners.back() == msg.targetCoordinator) {
                    fValidTargetCoordinator = true;
                }
            } else if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nProposalHeight, pcoinsTip, &vStandbyMiners)) {
                if (msg.targetCoordinator == expectedCoordinator) {
                    fValidTargetCoordinator = true;
                } else if (nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                    std::map<CPubKey, std::vector<unsigned char>> mapBlockSolutions;
                    for (size_t i = 0; i < msg.block.vAdamMiners.size() && i < msg.block.vAdamSolutions.size(); ++i) {
                        if (!msg.block.vAdamSolutions[i].empty() &&
                            VerifyAdamSolution(msg.block.hashPrevBlock, adamSeed, msg.block.vAdamMiners[i], msg.block.vAdamSolutions[i], msg.block.nBits, msg.block.nVersion, nProposalHeight, (int)i)) {
                            mapBlockSolutions[msg.block.vAdamMiners[i]] = msg.block.vAdamSolutions[i];
                        }
                    }
                    CPubKey activeFallback, prevFallback;
                    int rotationIndex = 0;
                    if (GetAdamFallbackCoordinatorForSlot(adamSeed, nProposalHeight, nBlockTimeDiff, msg.block.vAdamMiners, mapBlockSolutions, activeFallback, prevFallback, rotationIndex) &&
                        (msg.targetCoordinator == activeFallback || msg.targetCoordinator == prevFallback)) {
                        fValidTargetCoordinator = true;
                    }
                }
            }
            if (!fValidTargetCoordinator) {
                LogPrintf("ProcessMessage: poscoordreq: Target coordinator %s is not valid for height %d (t+%ds)\n",
                    msg.targetCoordinator.GetID().ToString(), nProposalHeight, (int)nBlockTimeDiff);
                Misbehaving(pfrom->GetId(), 10);
                return true;
            }

            CValidationState state;
            std::string strPoSError;
            if (CheckBlock(msg.block, state, false, true, false, pcoinsTip) &&
                ContextualCheckBlock(msg.block, state, pindexPrev) &&
                CheckProofOfStake(msg.block, strPoSError, pindexPrev)) {
                CCoinsViewCache viewNew(pcoinsTip);
                CBlockIndex indexDummy(msg.block);
                indexDummy.pprev = pindexPrev;
                indexDummy.nHeight = nProposalHeight;
                if (ConnectBlock(msg.block, state, &indexDummy, viewNew, true, true)) {
                    fDraftValid = true;
                }
            }
            if (!fDraftValid) {
                int nDoS = 0;
                if (state.IsInvalid(nDoS) && nDoS > 0) {
                    Misbehaving(pfrom->GetId(), nDoS);
                } else {
                    Misbehaving(pfrom->GetId(), 10);
                }
                LogPrintf("ProcessMessage: poscoordreq: Draft PoS block validation failed on height %d (%s / %s)\n",
                    nProposalHeight, FormatStateMessage(state), strPoSError);
                return true;
            }
        }

        if (!RecordSeenPoSCoordReq(reqHash, msg.targetCoordinator, msg.block, adamSeed)) {
            return true;
        }

        // Relay verified POSCOORDREQ across P2P so remote coordinators receive it over multi-hop topologies
        connman.ForEachNode([&msg, pfrom, &connman](CNode* pnode) {
            if (pnode != pfrom) {
                connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::POSCOORDREQ, msg));
            }
        });

        CKey coordKey;
        if (!HasCoordinatorKey(msg.targetCoordinator, nullptr, coordKey) || !coordKey.IsValid()) {
            return true;
        }
        if (!AcquireCoordinatorProposalSlot(msg.block.hashPrevBlock, msg.targetCoordinator, reqHash, false)) {
            LogPrintf("ProcessMessage: poscoordreq: Declining req %s for coordinator %s (slot already committed on parent %s)\n",
                reqHash.ToString(), msg.targetCoordinator.GetID().ToString(), msg.block.hashPrevBlock.ToString());
            return true;
        }

        CBLSSecretKey blsKey = DeriveBLSFromCKey(coordKey);
        if (!blsKey.IsValid()) {
            return true;
        }

        CBlock signedDraft = msg.block;
        signedDraft.vAdamVRFProof.clear();
        signedDraft.vAdamCoordinatorSig.clear();
        if (!SignBLSWithECDSAFallback(adamSeed, coordKey, blsKey, signedDraft.vAdamVRFProof) ||
            !SignBLSWithECDSAFallback(signedDraft.GetHash(), coordKey, blsKey, signedDraft.vAdamCoordinatorSig)) {
            return true;
        }

        {
            LOCK(cs_main);
            if (chainActive.Tip() != pindexPrev) {
                return true;
            }
            CValidationState state;
            if (!TestBlockValidity(state, signedDraft, pindexPrev, false, true)) {
                LogPrintf("ProcessMessage: poscoordreq: TestBlockValidity failed after attaching coordinator proofs (%s)\n",
                    FormatStateMessage(state));
                return true;
            }
        }

        if (!AcquireCoordinatorProposalSlot(msg.block.hashPrevBlock, msg.targetCoordinator, reqHash, true)) {
            return true;
        }

        {
            LOCK(cs_pos_coord_sigs);
            if (mapPoSCoordSigs.size() > 1000 && !mapPoSCoordSigs.count(reqHash)) {
                auto itEvict = mapPoSCoordSigs.begin();
                if (itEvict != mapPoSCoordSigs.end() && itEvict->first == reqHash) {
                    ++itEvict;
                }
                if (itEvict != mapPoSCoordSigs.end()) {
                    mapPoSCoordSigs.erase(itEvict);
                }
            }
            mapPoSCoordSigs[reqHash] = {msg.targetCoordinator, signedDraft.vAdamVRFProof, signedDraft.vAdamCoordinatorSig};
        }

        CPoSCoordSignResponseMsg respMsg;
        respMsg.reqHash = reqHash;
        respMsg.coordinatorPubKey = msg.targetCoordinator;
        respMsg.vAdamVRFProof = std::move(signedDraft.vAdamVRFProof);
        respMsg.vAdamCoordinatorSig = std::move(signedDraft.vAdamCoordinatorSig);

        LogPrintf("ProcessMessage: poscoordreq: Co-signed cooperative PoS block request %s for height %d as coordinator %s\n",
            reqHash.ToString(), nProposalHeight, msg.targetCoordinator.GetID().ToString());

        connman.ForEachNode([&respMsg, &connman](CNode* pnode) {
            connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::POSCOORDSIG, respMsg));
        });
    }

    else if (strCommand == NetMsgType::POSCOORDSIG) {
        CPoSCoordSignResponseMsg msg;
        try {
            vRecv >> msg;
        } catch (const std::exception& e) {
            LogPrintf("ProcessMessage: poscoordsig: Failed to deserialize response: %s\n", e.what());
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        if (msg.reqHash.IsNull() || !msg.coordinatorPubKey.IsValid() ||
            msg.vAdamVRFProof.empty() || msg.vAdamVRFProof.size() > 512 ||
            msg.vAdamCoordinatorSig.empty() || msg.vAdamCoordinatorSig.size() > 512) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        CSeenPoSCoordReqEntry reqEntry;
        {
            LOCK(cs_pos_coord_sigs);
            if (mapPoSCoordSigs.count(msg.reqHash) > 0) {
                return true;
            }
            auto itReq = mapSeenPoSCoordReqs.find(msg.reqHash);
            if (itReq == mapSeenPoSCoordReqs.end()) {
                return true;
            }
            reqEntry = itReq->second;
        }

        if (msg.coordinatorPubKey != reqEntry.targetCoordinator || reqEntry.adamSeed.IsNull()) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        CBlockHeader signedHeader = reqEntry.draftHeader;
        signedHeader.vAdamVRFProof = msg.vAdamVRFProof;
        signedHeader.vAdamCoordinatorSig = msg.vAdamCoordinatorSig;
        const uint256 signedHeaderHash = signedHeader.GetHash();

        CBLSPubKey vrfBlsPk, coordBlsPk;
        if (!VerifyAdamVRFProof(reqEntry.adamSeed, msg.vAdamVRFProof, reqEntry.targetCoordinator) ||
            !VerifyAdamCoordinatorSig(signedHeader, reqEntry.targetCoordinator, &signedHeaderHash) ||
            !ExtractBLSPubKeyFromSignedData(msg.vAdamVRFProof, vrfBlsPk) ||
            !ExtractBLSPubKeyFromSignedData(msg.vAdamCoordinatorSig, coordBlsPk) ||
            vrfBlsPk != coordBlsPk) {
            LOCK(cs_main);
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        bool fInserted = false;
        {
            LOCK(cs_pos_coord_sigs);
            if (mapPoSCoordSigs.count(msg.reqHash) == 0 && mapSeenPoSCoordReqs.count(msg.reqHash) > 0) {
                if (mapPoSCoordSigs.size() > 1000) {
                    auto itEvict = mapPoSCoordSigs.begin();
                    if (itEvict != mapPoSCoordSigs.end()) {
                        mapPoSCoordSigs.erase(itEvict);
                    }
                }
                mapPoSCoordSigs[msg.reqHash] = {msg.coordinatorPubKey, msg.vAdamVRFProof, msg.vAdamCoordinatorSig};
                fInserted = true;
            }
        }

        if (fInserted) {
            connman.ForEachNode([&msg, pfrom, &connman](CNode* pnode) {
                if (pnode != pfrom) {
                    connman.PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::POSCOORDSIG, msg));
                }
            });
        }
    }

    else if (strCommand == NetMsgType::REJECT) {
        try {
            std::string strMsg;
            unsigned char ccode;
            std::string strReason;
            vRecv >> LIMITED_STRING(strMsg, CMessageHeader::COMMAND_SIZE) >> ccode >> LIMITED_STRING(strReason, MAX_REJECT_MESSAGE_LENGTH);

            std::ostringstream ss;
            ss << strMsg << " code " << itostr(ccode) << ": " << strReason;

            if (strMsg == NetMsgType::BLOCK || strMsg == NetMsgType::TX) {
                uint256 hash;
                vRecv >> hash;
                ss << ": hash " << hash.ToString();
            }
            LogPrint(BCLog::NET, "Reject %s\n", SanitizeString(ss.str()));
        } catch (const std::ios_base::failure& e) {
            // Avoid feedback loops by preventing reject messages from triggering a new reject message.
            LogPrint(BCLog::NET, "Unparseable reject message received\n");
        }
    } else {
        bool found = false;
        const std::vector<std::string>& allMessages = getAllNetMessageTypes();
        for (const std::string& msg : allMessages) {
            if (msg == strCommand) {
                found = true;
                break;
            }
        }

        if (found) {
            //probably one the extensions
            mnodeman.ProcessMessage(pfrom, strCommand, vRecv);
            masternodePayments.ProcessMessageMasternodePayments(pfrom, strCommand, vRecv);
            sporkManager.ProcessSpork(pfrom, strCommand, vRecv);
            masternodeSync.ProcessMessage(pfrom, strCommand, vRecv);
        } else {
            // Ignore unknown commands for extensibility
            LogPrint(BCLog::NET, "Unknown command \"%s\" from peer=%d\n", SanitizeString(strCommand), pfrom->id);
        }
    }


    return true;
}

// Note: whenever a protocol update is needed toggle between both implementations (comment out the formerly active one)
//       so we can leave the existing clients untouched (old SPORK will stay on so they don't see even older clients).
//       Those old clients won't react to the changes of the other (new) SPORK because at the time of their implementation
//       it was the one which was commented out
int ActiveProtocol()
{
    const int64_t nSporkProto = sporkManager.GetSporkValue(SPORK_14_MIN_PROTOCOL_ACCEPTED);
    if (nSporkProto <= 0 || nSporkProto >= 4070908800LL) {
        return MIN_PEER_PROTO_VERSION;
    }
    const int64_t nClamped = std::min<int64_t>(PROTOCOL_VERSION, std::max<int64_t>(MIN_PEER_PROTO_VERSION, nSporkProto));
    return (int)nClamped;
}

void ProcessFutureBlocks(CConnman& connman) {
    static std::atomic<int64_t> nLastFutureProcess{0};
    int64_t nNow = GetTime();
    if (nNow == nLastFutureProcess.exchange(nNow)) {
        return;
    }

    std::vector<CFutureBlock> toProcess;
    {
        LOCK(cs_main);
        LOCK(cs_future_blocks);
        if (mapFutureBlocks.empty()) {
            return;
        }
        for (auto it = mapFutureBlocks.begin(); it != mapFutureBlocks.end(); ) {
            BlockMap::iterator mi = mapBlockIndex.find(it->second.block.hashPrevBlock);
            if (mi == mapBlockIndex.end() || !(*mi).second) {
                if (GetAdjustedTime() - it->second.block.nTime > 3600) {
                    it = mapFutureBlocks.erase(it);
                } else {
                    ++it;
                }
                continue;
            }
            CBlockIndex* pindexPrev = (*mi).second;
            if (it->second.block.nTime <= pindexPrev->MaxFutureBlockTime()) {
                toProcess.push_back(it->second);
                it = mapFutureBlocks.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    for (const auto& fb : toProcess) {
        CValidationState state;
        if (fb.block.vtx.empty()) {
            LOCK(cs_main);
            CBlockIndex* pindexLast = nullptr;
            if (AcceptBlockHeader(fb.block, state, &pindexLast)) {
                LogPrintf("ProcessFutureBlocks: Successfully accepted deferred future header %s\n", fb.block.GetHash().ToString());
                if (pindexLast) {
                    connman.ForEachNode([&fb, &pindexLast, &connman](CNode* pnode) {
                        if (pnode->GetAddrName() == fb.peerAddr) {
                            UpdateBlockAvailability(pnode->GetId(), pindexLast->GetBlockHash());
                            CNetMsgMaker msgMaker(pnode->GetSendVersion());
                            std::vector<CInv> vInv = {CInv(MSG_BLOCK, pindexLast->GetBlockHash())};
                            connman.PushMessage(pnode, msgMaker.Make(NetMsgType::GETDATA, vInv));
                        }
                    });
                }
            }
        } else {
            bool fAlreadyHave = false;
            {
                LOCK(cs_main);
                fAlreadyHave = (mapBlockIndex.count(fb.block.GetHash()) > 0);
            }
            if (!fAlreadyHave) {
                CNode* pfrom = nullptr;
                connman.ForEachNode([&fb, &pfrom](CNode* pnode) {
                    if (!pfrom && pnode->GetAddrName() == fb.peerAddr) {
                        pnode->AddRef();
                        pfrom = pnode;
                    }
                });
                ProcessNewBlock(state, pfrom, &fb.block, nullptr, &connman);
                if (pfrom) {
                    pfrom->Release();
                }
                if (state.GetRejectReason() != "time-too-new") {
                    const CInv invProcessed(MSG_BLOCK, fb.block.GetHash());
                    connman.ForEachNode([&fb, &invProcessed](CNode* pnode) {
                        if (pnode->GetAddrName() == fb.peerAddr) {
                            pnode->AddInventoryKnown(invProcessed);
                        }
                    });
                }
                LogPrintf("ProcessFutureBlocks: Processed deferred future block %s (result: %d)\n", fb.block.GetHash().ToString(), state.IsValid());
            }
        }
    }
}

bool ProcessMessages(CNode* pfrom, CConnman& connman, std::atomic<bool>& interruptMsgProc)
{
    ProcessFutureBlocks(connman);

    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //
    bool fMoreWork = false;

    if (!pfrom->vRecvGetData.empty())
        ProcessGetData(pfrom, connman, interruptMsgProc);

    if (pfrom->fDisconnect)
        return false;

    // this maintains the order of responses
    if (!pfrom->vRecvGetData.empty()) return true;

    // Don't bother if send buffer is too full to respond anyway
    if (pfrom->fPauseSend)
        return false;

    std::list<CNetMessage> msgs;
    {
        LOCK(pfrom->cs_vProcessMsg);
        if (pfrom->vProcessMsg.empty())
            return false;
        // Just take one message
        msgs.splice(msgs.begin(), pfrom->vProcessMsg, pfrom->vProcessMsg.begin());
        pfrom->nProcessQueueSize -= msgs.front().vRecv.size() + CMessageHeader::HEADER_SIZE;
        pfrom->fPauseRecv = pfrom->nProcessQueueSize > connman.GetReceiveFloodSize();
        fMoreWork = !pfrom->vProcessMsg.empty();
    }
    CNetMessage& msg(msgs.front());

    msg.SetVersion(pfrom->GetRecvVersion());
    // Scan for message start
    if (memcmp(msg.hdr.pchMessageStart, Params().MessageStart(), MESSAGE_START_SIZE) != 0) {
        LogPrintf("PROCESSMESSAGE: INVALID MESSAGESTART %s peer=%d\n", SanitizeString(msg.hdr.GetCommand()), pfrom->id);
        pfrom->fDisconnect = true;
        return false;
    }

    // Read header
    CMessageHeader& hdr = msg.hdr;
    if (!hdr.IsValid(Params().MessageStart())) {
        LogPrintf("PROCESSMESSAGE: ERRORS IN HEADER %s peer=%d\n", SanitizeString(hdr.GetCommand()), pfrom->id);
        return fMoreWork;
    }
    std::string strCommand = hdr.GetCommand();

    // Message size
    unsigned int nMessageSize = hdr.nMessageSize;

        // Checksum (skipped for BIP324 AEAD-authenticated v2 packets)
        CDataStream& vRecv = msg.vRecv;
        if (!msg.fValidatedChecksum) {
            uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
            if (memcmp(hash.begin(), hdr.pchChecksum, CMessageHeader::CHECKSUM_SIZE) != 0)
            {
                LogPrintf("%s(%s, %u bytes): CHECKSUM ERROR expected %s was %s\n", __func__,
                   SanitizeString(strCommand), nMessageSize,
                   HexStr(hash.begin(), hash.begin()+CMessageHeader::CHECKSUM_SIZE),
                   HexStr(hdr.pchChecksum, hdr.pchChecksum+CMessageHeader::CHECKSUM_SIZE));
                return fMoreWork;
            }
        }

    // Process message
    bool fRet = false;
    try {
        fRet = ProcessMessage(pfrom, strCommand, vRecv, msg.nTime, connman, interruptMsgProc);
        if (interruptMsgProc)
            return false;
        if (!pfrom->vRecvGetData.empty())
            fMoreWork = true;
    } catch (const std::ios_base::failure& e) {
        connman.PushMessage(pfrom, CNetMsgMaker(INIT_PROTO_VERSION).Make(NetMsgType::REJECT, strCommand, REJECT_MALFORMED, std::string("error parsing message")));
        if (strstr(e.what(), "end of data")) {
            // Allow exceptions from under-length message on vRecv
            LogPrintf("ProcessMessages(%s, %u bytes): Exception '%s' caught, normally caused by a message being shorter than its stated length\n", SanitizeString(strCommand), nMessageSize, e.what());
        } else if (strstr(e.what(), "size too large")) {
            // Allow exceptions from over-long size
            LogPrintf("ProcessMessages(%s, %u bytes): Exception '%s' caught\n", SanitizeString(strCommand), nMessageSize, e.what());
        } else {
            PrintExceptionContinue(&e, "ProcessMessages()");
        }
    } catch (const std::exception& e) {
        PrintExceptionContinue(&e, "ProcessMessages()");
    } catch (...) {
        PrintExceptionContinue(NULL, "ProcessMessages()");
    }

    if (!fRet)
        LogPrintf("ProcessMessage(%s, %u bytes) FAILED peer=%d\n", SanitizeString(strCommand), nMessageSize, pfrom->id);

    return fMoreWork;
}


bool SendMessages(CNode* pto, CConnman& connman, std::atomic<bool>& interruptMsgProc)
{
    {
        // Don't send anything until the version handshake is complete
        if (!pto->fSuccessfullyConnected || pto->fDisconnect)
            return true;

        // If we get here, the outgoing message serialization version is set and can't change.
        CNetMsgMaker msgMaker(pto->GetSendVersion());

        if (!pto->fSentCmpct) {
            bool fHighBandwidth = true;
            uint64_t nVersion = 1;
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::SENDCMPCT, fHighBandwidth, nVersion));
            pto->fSentCmpct = true;
        }

        //
        // Message: ping
        //
        bool pingSend = false;
        if (pto->fPingQueued) {
            // RPC ping request by user
            pingSend = true;
        }
        if (pto->nPingNonceSent == 0 && pto->nPingUsecStart + PING_INTERVAL * 1000000 < GetTimeMicros()) {
            // Ping automatically sent as a latency probe & keepalive.
            pingSend = true;
        }
        if (pingSend) {
            uint64_t nonce = 0;
            while (nonce == 0) {
                GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
            }
            pto->fPingQueued = false;
            pto->nPingUsecStart = GetTimeMicros();
            if (pto->nVersion > BIP0031_VERSION) {
                pto->nPingNonceSent = nonce;
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::PING, nonce));
            } else {
                // Peer is too old to support ping command with nonce, pong will never arrive.
                pto->nPingNonceSent = 0;
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::PING));
            }
        }

        TRY_LOCK(cs_main, lockMain); // Acquire cs_main for IsInitialBlockDownload() and CNodeState()
        if (!lockMain)
            return true;

        CNodeState& state = *State(pto->GetId());

        for (const CBlockReject& reject : state.rejects)
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::REJECT, (std::string)NetMsgType::BLOCK, reject.chRejectCode, reject.strRejectReason, reject.hashBlock));
        state.rejects.clear();

        if (state.fShouldBan) {
            state.fShouldBan = false;
            if (pto->fWhitelisted)
                LogPrintf("Warning: not punishing whitelisted peer %s!\n", pto->addr.ToString());
            else {
                pto->fDisconnect = true;
                if (pto->addr.IsLocal())
                    LogPrintf("Warning: not banning local peer %s!\n", pto->addr.ToString());
                else {
                    connman.Ban(pto->addr, BanReasonNodeMisbehaving);
                }
                return true;
            }
        }

        // Address refresh broadcast
        int64_t nNow = GetTimeMicros();
        if (!IsInitialBlockDownload() && pto->nNextLocalAddrSend < nNow) {
            AdvertiseLocal(pto);
            pto->nNextLocalAddrSend = PoissonNextSend(nNow, AVG_LOCAL_ADDRESS_BROADCAST_INTERVAL);
        }

        //
        // Message: addr
        //
        if (pto->nNextAddrSend < nNow) {
            pto->nNextAddrSend = PoissonNextSend(nNow, AVG_ADDRESS_BROADCAST_INTERVAL);
            std::vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            for (const CAddress& addr : pto->vAddrToSend) {
                if (!pto->addrKnown.contains(addr.GetKey())) {
                    pto->addrKnown.insert(addr.GetKey());
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000) {
                        connman.PushMessage(pto, msgMaker.Make(NetMsgType::ADDR, vAddr));
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::ADDR, vAddr));
        }

        // Start block sync
        if (pindexBestHeader == NULL)
            pindexBestHeader = chainActive.Tip();
        bool fFetch = state.fPreferredDownload || (nPreferredDownload == 0 && !pto->fClient && !pto->fOneShot); // Download if this is a nice peer, or we have no nice peers and this one might do.
        if (fFetch && !pto->fClient && !fImporting && !fReindex && pto->nStartingHeight >= chainActive.Height()) {
            if (!state.fSyncStarted || (state.nLastSyncReqTime > 0 && GetTime() - state.nLastSyncReqTime > 60)) {
                state.fSyncStarted = true;
                state.nLastSyncReqTime = GetTime();
                CBlockIndex* pindexSync = pindexBestHeader ? pindexBestHeader : chainActive.Tip();
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETHEADERS, chainActive.GetLocator(pindexSync), UINT256_ZERO));
                connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETBLOCKS, chainActive.GetLocator(pindexSync), UINT256_ZERO));
            }
        }

        // Resend wallet transactions that haven't gotten in a block yet
        // Except during reindex, importing and IBD, when old wallet
        // transactions become unconfirmed and spams other nodes.
        if (!fReindex && !fImporting && !IsInitialBlockDownload()) {
            GetMainSignals().Broadcast(&connman);
        }

        //
        // Message: inventory
        //
        std::vector<CInv> vInv;
        std::vector<CInv> vInvWait;
        {
            bool fSendTrickle = pto->fWhitelisted;
            if (pto->nNextInvSend < nNow) {
                fSendTrickle = true;
                pto->nNextInvSend = PoissonNextSend(nNow, AVG_INVENTORY_BROADCAST_INTERVAL);
            }
            LOCK(mempool.cs);
            LOCK(pto->cs_vSend);
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            const bool fBlocksOnly = GetBoolArg("-blocksonly", false);
            for (const CInv& inv : pto->vInventoryToSend) {
                if (inv.type == MSG_TX && fBlocksOnly)
                    continue;
                if (inv.type == MSG_TX && pto->filterInventoryKnown.contains(inv.hash))
                    continue;
                if (inv.type == MSG_TX && pto->minFeeFilter > 0) {
                    auto itTx = mempool.mapTx.find(inv.hash);
                    if (itTx != mempool.mapTx.end()) {
                        const CFeeRate txFeeRate(itTx->GetFee(), itTx->GetTxSize());
                        if (txFeeRate.GetFeePerK() < pto->minFeeFilter) {
                            continue;
                        }
                    }
                }

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle) {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint256 hashRand = inv.hash ^ hashSalt;
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0);

                    if (fTrickleWait) {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                pto->filterInventoryKnown.insert(inv.hash);

                vInv.push_back(inv);
                if (vInv.size() >= 1000) {
                    connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));
                    vInv.clear();
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::INV, vInv));

        // Periodically send BIP133 feefilter
        if (pto->nNextSendFeefilter < nNow && pto->fSuccessfullyConnected) {
            CAmount currentMinFee = GetBoolArg("-blocksonly", false) ? Params().GetConsensus().nMaxMoneyOut : ::minRelayTxFee.GetFeePerK();
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::FEEFILTER, currentMinFee));
            pto->nNextSendFeefilter = nNow + 600LL * 1000000LL;
        }

        // Detect whether we're stalling
        nNow = GetTimeMicros();
        if (state.nStallingSince && state.nStallingSince < nNow - 1000000 * BLOCK_STALLING_TIMEOUT) {
            // Stalling only triggers when the block download window cannot move. During normal steady state,
            // the download window should be much larger than the to-be-downloaded set of blocks, so disconnection
            // should only happen during initial block download.
            LogPrintf("Peer=%d is stalling block download, disconnecting\n", pto->id);
            pto->fDisconnect = true;
            return true;
        }
        // In case there is a block that has been in flight from this peer for (2 + 0.5 * N) times the block interval
        // (with N the number of validated blocks that were in flight at the time it was requested), disconnect due to
        // timeout. We compensate for in-flight blocks to prevent killing off peers due to our own downstream link
        // being saturated. We only count validated in-flight blocks so peers can't advertise nonexisting block hashes
        // to unreasonably increase our timeout.
        if (state.vBlocksInFlight.size() > 0 && state.vBlocksInFlight.front().nTime < nNow - 500000 * Params().GetConsensus().nTargetSpacing * (4 + state.vBlocksInFlight.front().nValidatedQueuedBefore)) {
            LogPrintf("Timeout downloading block %s from peer=%d, disconnecting\n", state.vBlocksInFlight.front().hash.ToString(), pto->id);
            pto->fDisconnect = true;
            return true;
        }

        //
        // Message: getdata (blocks)
        //
        std::vector<CInv> vGetData;
        if (!pto->fClient && fFetch && state.nBlocksInFlight < MAX_BLOCKS_IN_TRANSIT_PER_PEER) {
            std::vector<CBlockIndex*> vToDownload;
            NodeId staller = -1;
            FindNextBlocksToDownload(pto->GetId(), MAX_BLOCKS_IN_TRANSIT_PER_PEER - state.nBlocksInFlight, vToDownload, staller);
            for (CBlockIndex* pindex : vToDownload) {
                vGetData.push_back(CInv(MSG_BLOCK, pindex->GetBlockHash()));
                MarkBlockAsInFlight(pto->GetId(), pindex->GetBlockHash(), pindex);
                LogPrintf("Requesting block %s (%d) peer=%d\n", pindex->GetBlockHash().ToString(),
                    pindex->nHeight, pto->id);
            }
            if (state.nBlocksInFlight == 0 && staller != -1) {
                if (State(staller)->nStallingSince == 0) {
                    State(staller)->nStallingSince = nNow;
                    LogPrint(BCLog::NET, "Stall started peer=%d\n", staller);
                }
            }
        }

        //
        // Message: getdata (non-blocks)
        //
        while (!pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow) {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(inv)) {
                LogPrint(BCLog::NET, "Requesting %s peer=%d\n", inv.ToString(), pto->id);
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000) {
                    connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
                    vGetData.clear();
                }
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            connman.PushMessage(pto, msgMaker.Make(NetMsgType::GETDATA, vGetData));
    }
    return true;
}

std::string CBlockFileInfo::ToString() const
{
    return strprintf("CBlockFileInfo(blocks=%u, size=%u, heights=%u...%u, time=%s...%s)", nBlocks, nSize, nHeightFirst, nHeightLast, DateTimeStrFormat("%Y-%m-%d", nTimeFirst), DateTimeStrFormat("%Y-%m-%d", nTimeLast));
}
