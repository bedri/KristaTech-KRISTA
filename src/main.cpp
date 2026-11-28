// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include <fstream>
#include <limits>
#include <unistd.h>
#include "adam.h"
#include "evo/deterministicmns.h"

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
#include "masternode.h"
#include "merkleblock.h"
#include "miner.h"
#include "blockencodings.h"
#include "messagesigner.h"
#include "net.h"
#include "netmessagemaker.h"
#include "netbase.h"
#include "policy/policy.h"
#include "pow.h"
#include "txdb.h"
#include "txmempool.h"
#include "guiinterface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include "masternode-sync.h"
#include <sstream>

#include <boost/algorithm/string/replace.hpp>
#include <boost/thread.hpp>
#include <boost/foreach.hpp>
#include <atomic>
#include <queue>
#include "net_processing.h"

static CKeyID GetCompressedKeyID(const CPubKey& pubkey) {
    if (pubkey.IsCompressed()) return pubkey.GetID();
    if (pubkey.size() == 65) {
        unsigned char comp_vch[33];
        comp_vch[0] = (pubkey[64] % 2 == 0) ? 0x02 : 0x03;
        memcpy(comp_vch + 1, pubkey.begin() + 1, 32);
        return CPubKey(comp_vch, comp_vch + 33).GetID();
    }
    return pubkey.GetID();
}

static bool ComparePubKeys(const CPubKey& pk1, const CPubKey& pk2) {
    if (pk1 == pk2) return true;
    if (!pk1.IsValid() || !pk2.IsValid()) return false;
    return CXOnlyPubKey(pk1) == CXOnlyPubKey(pk2);
}


#if defined(NDEBUG)
#error "KristaTech cannot be compiled without assertions."
#endif

/**
 * Global state
 */


/**
 * Mutex to guard access to validation specific variables, such as reading
 * or changing the chainstate.
 *
 * This may also need to be locked when updating the transaction pool, e.g. on
 * AcceptToMemoryPool. See CTxMemPool::cs comment for details.
 *
 * The transaction pool has a separate lock to allow reading from it and the
 * chainstate at the same time.
 */
RecursiveMutex cs_main;

BlockMap mapBlockIndex;

RecursiveMutex cs_future_blocks;
std::map<uint256, CFutureBlock> mapFutureBlocks;

CChain chainActive;
CBlockIndex* pindexBestHeader = NULL;
int64_t nTimeBestReceived = 0;
CAmount nMoneySupply;
// Best block section
Mutex g_best_block_mutex;
std::condition_variable g_best_block_cv;
uint256 g_best_block;

int nScriptCheckThreads = 0;
std::atomic<bool> fImporting{false};
std::atomic<bool> fReindex{false};
bool fTxIndex = true;
bool fCheckBlockIndex = false;
bool fVerifyingBlocks = false;
size_t nCoinCacheUsage = 5000 * 300;

/* If the tip is older than this (in seconds), the node is considered to be in initial block download. */
int64_t nMaxTipAge = DEFAULT_MAX_TIP_AGE;

/** Fees smaller than this (in uKRISTA) are considered zero fee (for relaying, mining and transaction creation)
 * We are ~100 times smaller then bitcoin now (2015-06-23), set minRelayTxFee only 10 times higher
 * so it's still 10 times lower comparing to bitcoin.
 */
CFeeRate minRelayTxFee = CFeeRate(10000);

CTxMemPool mempool(::minRelayTxFee);

static bool CheckSequenceLocksImpl(const CTransaction& tx, const CCoinsViewCache& view, const CBlockIndex* pindexPrev)
{
    if (tx.nVersion < 2 || tx.IsCoinBase() || !pindexPrev) {
        return true;
    }
    int nMinHeight = -1;
    int64_t nMinTime = -1;
    for (const CTxIn& txin : tx.vin) {
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG) {
            continue;
        }
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }
        int nCoinHeight = coin.nHeight;
        if (nCoinHeight == MEMPOOL_HEIGHT) {
            nCoinHeight = pindexPrev->nHeight + 1;
        }
        if (txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_TYPE_FLAG) {
            const CBlockIndex* pindexCoinPrev = pindexPrev->GetAncestor(std::max(nCoinHeight - 1, 0));
            const int64_t nCoinTime = pindexCoinPrev ? pindexCoinPrev->GetMedianTimePast() : 0;
            const int64_t nTimeOffset = ((int64_t)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) << CTxIn::SEQUENCE_LOCKTIME_GRANULARITY) - 1;
            nMinTime = std::max(nMinTime, nCoinTime + nTimeOffset);
        } else {
            const int nHeightOffset = (int)(txin.nSequence & CTxIn::SEQUENCE_LOCKTIME_MASK) - 1;
            nMinHeight = std::max(nMinHeight, nCoinHeight + nHeightOffset);
        }
    }
    if (nMinHeight > pindexPrev->nHeight) {
        return false;
    }
    if (nMinTime >= pindexPrev->GetMedianTimePast()) {
        return false;
    }
    return true;
}


bool CheckSequenceLocks(const CTransaction& tx, const CCoinsViewCache& view, const CBlockIndex* pindexPrev)
{
    return CheckSequenceLocksImpl(tx, view, pindexPrev);
}

std::map<uint256, COrphanTx> mapOrphanTransactions GUARDED_BY(cs_main);
std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev GUARDED_BY(cs_main);
std::map<uint256, int64_t> mapRejectedBlocks;
RecursiveMutex cs_quorum_sigs;
std::map<uint256, std::map<COutPoint, std::vector<unsigned char>>> mapQuorumBlockSigs;
std::set<uint256> setSeenProposedBlocks;
std::map<uint256, uint256> mapProposedBlockPrev;
RecursiveMutex cs_seen_prop;

/** Constant stuff for coinbase transactions we create: */
CScript COINBASE_FLAGS;

const std::string strMessageMagic = "DarkNet Signed Message:\n";


// Internal stuff
namespace
{
struct CBlockIndexWorkComparator {
    bool operator()(CBlockIndex* pa, CBlockIndex* pb) const
    {
        // First sort by most total work, ...
        if (pa->nChainWork > pb->nChainWork) return false;
        if (pa->nChainWork < pb->nChainWork) return true;

        // ... then deterministically by the lowest block hash.
        // KristaTech: ADAM block production requires miner solutions and LLMQ signatures on one common tip,
        // so an equal-work fork resolved by "first seen" would split the network and halt both sides forever.
        // A network-wide deterministic tie-breaker makes every node converge on the same equal-work tip.
        if (pa->phashBlock && pb->phashBlock && *pa->phashBlock != *pb->phashBlock) {
            const arith_uint256 ha = UintToArith256(*pa->phashBlock);
            const arith_uint256 hb = UintToArith256(*pb->phashBlock);
            if (ha < hb) return false;
            if (ha > hb) return true;
        }

        // Use pointer address as tie breaker (only reachable without block hashes).
        if (pa < pb) return false;
        if (pa > pb) return true;

        // Identical blocks.
        return false;
    }
};

CBlockIndex* pindexBestInvalid;

/**
     * The set of all CBlockIndex entries with BLOCK_VALID_TRANSACTIONS (for itself and all ancestors) and
     * as good as our current tip or better. Entries may be failed, though.
     */
std::set<CBlockIndex*, CBlockIndexWorkComparator> setBlockIndexCandidates;
/** Number of nodes with fSyncStarted. */

/** All pairs A->B, where A (or one if its ancestors) misses transactions, but B has transactions. */
std::multimap<CBlockIndex*, CBlockIndex*> mapBlocksUnlinked;

RecursiveMutex cs_LastBlockFile;
std::vector<CBlockFileInfo> vinfoBlockFile;
int nLastBlockFile = 0;

/**
     * Every received block is assigned a unique and increasing identifier, so we
     * know which one to give priority in case of a fork.
     */
RecursiveMutex cs_nBlockSequenceId;
/** Blocks loaded from disk are assigned id 0, so start the counter at 1. */
uint32_t nBlockSequenceId = 1;

/** Dirty block index entries. */
std::set<CBlockIndex*> setDirtyBlockIndex;

/** Dirty block file entries. */
std::set<int> setDirtyFileInfo;
} // anon namespace

CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator)
{
    // Find the first block the caller has in the main chain
    for (const uint256& hash : locator.vHave) {
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = (*mi).second;
            if (chain.Contains(pindex))
                return pindex;
        }
    }
    return chain.Genesis();
}

CBlockIndex* GetChainTip()
{
    return chainActive.AtomicTip();
}

CCoinsViewCache* pcoinsTip = NULL;
CBlockTreeDB* pblocktree = NULL;

//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx, NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:
    unsigned int sz = GetSerializeSize(tx, SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz > 5000) {
        LogPrint(BCLog::MEMPOOL, "ignoring large orphan tx (size: %u, hash: %s)\n", sz, hash.ToString());
        return false;
    }

    mapOrphanTransactions[hash].tx = tx;
    mapOrphanTransactions[hash].fromPeer = peer;
    for (const CTxIn& txin : tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    LogPrint(BCLog::MEMPOOL, "stored orphan tx %s (mapsz %u prevsz %u)\n", hash.ToString(),
        mapOrphanTransactions.size(), mapOrphanTransactionsByPrev.size());
    return true;
}


void EraseOrphanTx(uint256 hash) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.find(hash);
    if (it == mapOrphanTransactions.end())
        return;
    for (const CTxIn& txin : it->second.tx.vin) {
        std::map<uint256, std::set<uint256> >::iterator itPrev = mapOrphanTransactionsByPrev.find(txin.prevout.hash);
        if (itPrev == mapOrphanTransactionsByPrev.end())
            continue;
        itPrev->second.erase(hash);
        if (itPrev->second.empty())
            mapOrphanTransactionsByPrev.erase(itPrev);
    }
    mapOrphanTransactions.erase(it);
}

void EraseOrphansFor(NodeId peer) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    int nErased = 0;
    std::map<uint256, COrphanTx>::iterator iter = mapOrphanTransactions.begin();
    while (iter != mapOrphanTransactions.end()) {
        std::map<uint256, COrphanTx>::iterator maybeErase = iter++; // increment to avoid iterator becoming invalid
        if (maybeErase->second.fromPeer == peer) {
            EraseOrphanTx(maybeErase->second.tx.GetHash());
            ++nErased;
        }
    }
    if (nErased > 0) LogPrint(BCLog::MEMPOOL, "Erased %d orphan tx from peer %d\n", nErased, peer);
}


unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans) EXCLUSIVE_LOCKS_REQUIRED(cs_main)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans) {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        std::map<uint256, COrphanTx>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}

bool CheckFinalTx(const CTransaction& tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = std::max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int nBlockHeight = chainActive.Height() + 1;

    // BIP113 will require that time-locked transactions have nLockTime set to
    // less than the median time of the previous block they're contained in.
    // When the next block is created its previous block will be the current
    // chain tip, so we use that to calculate the median time passed to
    // IsFinalTx() if LOCKTIME_MEDIAN_TIME_PAST is set.
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST) ? chainActive.Tip()->GetMedianTimePast() : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

void LimitMempoolSize(CTxMemPool& pool, size_t limit, unsigned long age) {
    int expired = pool.Expire(GetTime() - age);
    if (expired != 0)
        LogPrint(BCLog::MEMPOOL, "Expired %i transactions from the memory pool\n", expired);

    std::vector<COutPoint> vNoSpendsRemaining;
    pool.TrimToSize(limit, &vNoSpendsRemaining);
    for (const COutPoint& removed: vNoSpendsRemaining)
        pcoinsTip->Uncache(removed);
}

CAmount GetMinRelayFee(const CTransaction& tx, const CTxMemPool& pool, unsigned int nBytes, bool fAllowFree)
{
    uint256 hash = tx.GetHash();
    double dPriorityDelta = 0;
    CAmount nFeeDelta = 0;
    pool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
    if (dPriorityDelta > 0 || nFeeDelta > 0)
        return 0;

    bool hasOpReturn = false;
    for (const CTxOut& txout : tx.vout) {
        if (txout.scriptPubKey.size() > 0 && txout.scriptPubKey[0] == OP_RETURN) {
            hasOpReturn = true;
            break;
        }
    }

    CAmount nMinFee = ::minRelayTxFee.GetFee(nBytes);

    if (fAllowFree && !hasOpReturn) {
        // There is a free transaction area in blocks created by most miners,
        // * If we are relaying we allow transactions up to DEFAULT_BLOCK_PRIORITY_SIZE - 1000
        //   to be considered to fall into this category. We don't want to encourage sending
        //   multiple transactions instead of one big transaction to avoid fees.
        if (nBytes < (DEFAULT_BLOCK_PRIORITY_SIZE - 1000))
            nMinFee = 0;
    }

    if (hasOpReturn) {
        // Authenticated on-chain masternode keep-alive pings from real collateralized masternodes carry a small
        // OP_RETURN authorization payload by design; exempt them from the anti-spam OP_RETURN fee multiplier
        // strictly when shape, trailing-byte, 360-block per-MN cooldown, and mempool-uniqueness guards pass.
        CPubKey pingPubKey;
        COutPoint pingCollateral;
        COnChainMasternodeDelegation inlineDel;
        bool fValidMasternodePing = false;
        if (nBytes <= 850 &&
            ExtractOnChainMasternodePing(tx, pingPubKey, pingCollateral, false, &inlineDel, nullptr, -1, true)) {
            LOCK2(cs_main, pool.cs);
            const int nCurHeight = chainActive.Height();
            if (!IsMasternodeActiveAtHeight(pingPubKey, nCurHeight, MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS)) {
                bool fConflictingMempoolPing = false;
                for (const auto& memEntry : pool.mapTx) {
                    const CTransaction& memTx = memEntry.GetTx();
                    if (memTx.GetHash() == hash) continue;
                    if (memTx.vout.size() < 2 || memTx.vout.size() > 3) continue;
                    CPubKey memPingKey;
                    COutPoint memPingCol;
                    if (ExtractOnChainMasternodePing(memTx, memPingKey, memPingCol, false, nullptr, nullptr, -1, false)) {
                        if (memPingKey == pingPubKey || memPingCol == pingCollateral) {
                            fConflictingMempoolPing = true;
                            break;
                        }
                    }
                }
                if (!fConflictingMempoolPing) {
                    const bool fHadCollateralCached = pcoinsTip && pcoinsTip->HaveCoinInCache(pingCollateral);
                    fValidMasternodePing = IsValidMasternodeCollateralAndAuth(pingPubKey, pingCollateral, nCurHeight, &inlineDel);
                    if (!fHadCollateralCached && !fValidMasternodePing && pcoinsTip) {
                        pcoinsTip->Uncache(pingCollateral);
                    }
                }
            }
        }
        if (!fValidMasternodePing) {
            nMinFee *= 1000;
        }
    }

    if (!Params().GetConsensus().MoneyRange(nMinFee))
        nMinFee = Params().GetConsensus().nMaxMoneyOut;
    return nMinFee;
}

/** Convert CValidationState to a human-readable message for logging */
std::string FormatStateMessage(const CValidationState &state)
{
    return strprintf("%s%s (code %i)",
        state.GetRejectReason(),
        (state.GetDebugMessage().empty() ? "" : ", " + state.GetDebugMessage()),
        state.GetRejectCode());
}

bool AcceptToMemoryPoolWorker(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                              bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee, bool ignoreFees,
                              std::vector<COutPoint>& coins_to_uncache)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    const Consensus::Params& consensus = Params().GetConsensus();

    // Check transaction
    int chainHeight = chainActive.Height();
    if (!CheckTransaction(tx, state))
        return error("%s : transaction checks for %s failed with %s", __func__, tx.GetHash().ToString(), FormatStateMessage(state));

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "coinbase");

    //Coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return state.DoS(100, false, REJECT_INVALID, "coinstake");

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
        return state.DoS(0, false, REJECT_NONSTANDARD, "non-final");

    // Rather not work on nonstandard transactions (unless regtest)
    std::string reason;
    if (!Params().IsRegTestNet() && !IsStandardTx(tx, reason))
        return state.DoS(0, false, REJECT_NONSTANDARD, reason);
    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash)) {
        return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-in-mempool");
    }

    // Check for conflicts with in-memory transactions and validate against pool view
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (const auto &in : tx.vin) {
            COutPoint outpoint = in.prevout;
            if (pool.mapNextTx.count(outpoint)) {
                // Disable replacement feature for now
                return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-conflict");
            }
        }

        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;

        CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
        view.SetBackend(viewMemPool);

        // do we already have it?
        for (size_t out = 0; out < tx.vout.size(); out++) {
            COutPoint outpoint(hash, out);
            bool had_coin_in_cache = pcoinsTip->HaveCoinInCache(outpoint);
            if (view.HaveCoin(outpoint)) {
                if (!had_coin_in_cache) {
                    coins_to_uncache.push_back(outpoint);
                }
                return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-known");
            }
        }

        // do all inputs exist?
        bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);
        if (fZeroCoinPoW && chainActive.Tip() != nullptr) {
            std::vector<unsigned char> zNonce;
            uint256 zChallenge;
            CPubKey zPubKey;
            int64_t zLockTime = 0;
            CKeyID zPubKeyHash;
            CBLSPubKey zBlsPk;
            CBLSSignature zBlsPoP;
            if (tx.vout.size() != 1 ||
                !MatchPoWLockRegistration(tx.vout[0].scriptPubKey, zNonce, zChallenge, zPubKey, zLockTime, zPubKeyHash, &zBlsPk, &zBlsPoP) ||
                zPubKey.GetID() != zPubKeyHash) {
                return state.DoS(100, false, REJECT_INVALID, "bad-zerocoin-pow-format");
            }
            const int nNextHeight = chainActive.Height() + 1;
            if (!zPubKey.IsCompressed()) {
                return state.DoS(100, false, REJECT_INVALID, "bad-zerocoin-pow-uncompressed-pubkey");
            }
            std::string strBlsErr;
            if (!CheckRegistrationBlsBinding(zPubKey, zBlsPk, zBlsPoP, nNextHeight, chainActive.Tip(), &view, true, strBlsErr)) {
                return state.DoS(100, false, REJECT_INVALID, strBlsErr);
            }
            const size_t maxBootstrapPoolSize = (size_t)(consensus.GetAdamMinersCount(nNextHeight) + 2);
            const size_t nActiveGateCount = GetFundedAdamMinerPoolCount(chainHeight);
            if (!consensus.IsAdamOpenEntryActive(nNextHeight) && nActiveGateCount >= maxBootstrapPoolSize) {
                return state.Invalid(false, REJECT_NONSTANDARD, "bad-zerocoin-pow-pool-full",
                    strprintf("Zero-Coin bootstrap pool is closed because %u+ funded validators are active", (unsigned int)maxBootstrapPoolSize));
            }
            {
                const std::vector<CPubKey> activePool = GetAdamMinerPool(chainHeight);
                if (std::find(activePool.begin(), activePool.end(), zPubKey) != activePool.end()) {
                    return state.Invalid(false, REJECT_NONSTANDARD, "bad-zerocoin-pow-already-active",
                        "Public key is already active in the ADAM validator pool");
                }
            }
            const int nMinZeroPeriod = GetZeroCoinRegPeriod(nNextHeight);
            if (zLockTime >= LOCKTIME_THRESHOLD || zLockTime < nNextHeight + nMinZeroPeriod || zLockTime > nNextHeight + 5760) {
                return state.DoS(20, false, REJECT_INVALID, "bad-zerocoin-pow-locktime");
            }
            const int nMempoolChallengeWindow = ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 - 2;
            bool challengeValid = false;
            auto miChallenge = mapBlockIndex.find(zChallenge);
            if (miChallenge != mapBlockIndex.end() && miChallenge->second) {
                CBlockIndex* pindexChallenge = miChallenge->second;
                if (pindexChallenge->nHeight >= 0 && pindexChallenge->nHeight <= chainActive.Height() &&
                    chainActive[pindexChallenge->nHeight] &&
                    chainActive[pindexChallenge->nHeight]->GetBlockHash() == zChallenge &&
                    pindexChallenge->nHeight >= nNextHeight - nMempoolChallengeWindow && pindexChallenge->nHeight < nNextHeight) {
                    challengeValid = true;
                }
            }
            if (!challengeValid) {
                return state.DoS(20, false, REJECT_INVALID, "bad-zerocoin-pow-challenge");
            }
        }
        for (const CTxIn& txin : tx.vin) {
            if (fZeroCoinPoW) {
                continue;
            }
            if (!pcoinsTip->HaveCoinInCache(txin.prevout)) {
                coins_to_uncache.push_back(txin.prevout);
            }
            if (!view.HaveCoin(txin.prevout)) {
                if (pfMissingInputs) {
                    *pfMissingInputs = true;
                }
                return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
            }
        }

        // Reject transactions spending outputs locked to designated burn addresses (from in-memory view, zero disk I/O)
        if (!fZeroCoinPoW && !consensus.mBurnAddresses.empty()) {
            for (const CTxIn& txin : tx.vin) {
                const Coin& coin = view.AccessCoin(txin.prevout);
                if (!coin.IsSpent()) {
                    txnouttype type;
                    std::vector<CTxDestination> addresses;
                    int nRequired;
                    if (ExtractDestinations(coin.out.scriptPubKey, type, addresses, nRequired)) {
                        for (const auto& addr : addresses) {
                            if (consensus.IsBurnAddress(EncodeDestination(addr), chainHeight)) {
                                return state.DoS(0, false, REJECT_INVALID, "bad-txns-invalid-outputs");
                            }
                        }
                    }
                }
            }
        }

        // Bring the best block into scope
        view.GetBestBlock();

        nValueIn = fZeroCoinPoW ? 0 : view.GetValueIn(tx);

        // Enforce Coin-Lock / PoW-Lock locktime bounds, output limits, PoW proof validity, V4 BLS+PoP pinning, and 100-block input UTXO maturity
        std::vector<uint256> vProTxToEvict;
        if (!fZeroCoinPoW && chainActive.Tip() != nullptr) {
            const int nNextHeight = chainActive.Height() + 1;
            const bool fDualTrackNext = IsAdamDualTrackActive(nNextHeight);
            const uint256 powLimitTarget = GetMinerPoWLimit(Params().NetworkIDString());
            int nRegPeriod = 2880;
            if ((Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) &&
                IsModelDActive(nNextHeight)) {
                nRegPeriod = 100;
            }
            int nCoinLockOutputs = 0;
            int nPoWLockOutputs = 0;
            std::map<CBLSPubKey, CPubKey> seenTxRegBls;
            for (const CTxOut& txout : tx.vout) {
                std::vector<unsigned char> vchNonce;
                uint256 hashChallenge;
                CPubKey regPubKey;
                int64_t nRegLockTime = 0;
                CKeyID regKeyId;
                CBLSPubKey regBlsPk;
                CBLSSignature regBlsPoP;
                if (MatchPoWLockRegistration(txout.scriptPubKey, vchNonce, hashChallenge, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                    if (nRegLockTime >= LOCKTIME_THRESHOLD || nRegLockTime < nNextHeight + nRegPeriod || nRegLockTime > nNextHeight + nRegPeriod + 2880) {
                        return state.DoS(20, false, REJECT_INVALID, "bad-txns-minerlock-locktime");
                    }
                    if (fDualTrackNext && txout.nValue < POW_LOCK_REGISTRATION_AMOUNT) {
                        return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-powlock-amount-too-small",
                            strprintf("Funded PoW-Lock registration requires at least %d KRISTA", POW_LOCK_REGISTRATION_AMOUNT / COIN));
                    }
                    std::string strBlsErr;
                    if (!CheckRegistrationBlsBinding(regPubKey, regBlsPk, regBlsPoP, nNextHeight, chainActive.Tip(), &view, true, strBlsErr)) {
                        return state.DoS(100, false, REJECT_INVALID, strBlsErr);
                    }
                    if (regBlsPk.IsValid()) {
                        auto itSeen = seenTxRegBls.find(regBlsPk);
                        if (itSeen != seenTxRegBls.end() && itSeen->second != regPubKey) {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-minerreg-dup-bls");
                        }
                        seenTxRegBls.emplace(regBlsPk, regPubKey);
                    }
                    if (!regPubKey.IsValid() || !regPubKey.IsCompressed() || regPubKey.GetID() != regKeyId || vchNonce.size() != 4) {
                        return state.DoS(20, false, REJECT_INVALID, "bad-txns-powlock-invalid-key");
                    }
                    bool fChallengeFresh = false;
                    auto miChal = mapBlockIndex.find(hashChallenge);
                    if (miChal != mapBlockIndex.end() && miChal->second &&
                        miChal->second->nHeight >= 0 && miChal->second->nHeight <= chainActive.Height() &&
                        chainActive[miChal->second->nHeight] == miChal->second &&
                        miChal->second->nHeight >= nNextHeight - 100 && miChal->second->nHeight < nNextHeight) {
                        fChallengeFresh = true;
                    }
                    if (!fChallengeFresh) {
                        return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-powlock-stale-challenge",
                            "Funded PoW-Lock challenge block must be on active chain within the last 100 blocks");
                    }
                    const uint32_t nRegNonce = (uint32_t)vchNonce[0] | ((uint32_t)vchNonce[1] << 8) |
                                               ((uint32_t)vchNonce[2] << 16) | ((uint32_t)vchNonce[3] << 24);
                    if (ComputeMinerPoWRegistrationHash(nRegNonce, hashChallenge, regPubKey, regBlsPk) > powLimitTarget) {
                        return state.DoS(20, false, REJECT_INVALID, "bad-txns-powlock-invalid");
                    }
                    nPoWLockOutputs++;
                } else if (MatchCoinLockRegistration(txout.scriptPubKey, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                    if (nRegLockTime >= LOCKTIME_THRESHOLD || nRegLockTime < nNextHeight + nRegPeriod || nRegLockTime > nNextHeight + nRegPeriod + 2880) {
                        return state.DoS(20, false, REJECT_INVALID, "bad-txns-minerlock-locktime");
                    }
                    if (fDualTrackNext && txout.nValue < MINER_REGISTRATION_LOCK_AMOUNT) {
                        return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-coinlock-amount-too-small",
                            strprintf("Coin-Lock registration requires at least %d KRISTA", MINER_REGISTRATION_LOCK_AMOUNT / COIN));
                    }
                    if (!regPubKey.IsValid() || !regPubKey.IsCompressed() || regPubKey.GetID() != regKeyId) {
                        return state.DoS(20, false, REJECT_INVALID, "bad-txns-coinlock-invalid-key");
                    }
                    std::string strBlsErr;
                    if (!CheckRegistrationBlsBinding(regPubKey, regBlsPk, regBlsPoP, nNextHeight, chainActive.Tip(), &view, true, strBlsErr)) {
                        return state.DoS(100, false, REJECT_INVALID, strBlsErr);
                    }
                    if (regBlsPk.IsValid()) {
                        auto itSeen = seenTxRegBls.find(regBlsPk);
                        if (itSeen != seenTxRegBls.end() && itSeen->second != regPubKey) {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-minerreg-dup-bls");
                        }
                        seenTxRegBls.emplace(regBlsPk, regPubKey);
                    }
                    nCoinLockOutputs++;
                }
            }
            if (fDualTrackNext) {
                if (nCoinLockOutputs > 1) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-too-many-coinlock");
                }
                if (nCoinLockOutputs > 0) {
                    for (const CTxIn& txin : tx.vin) {
                        const Coin& coin = view.AccessCoin(txin.prevout);
                        if (coin.IsSpent() || (int)coin.nHeight > chainActive.Height() || nNextHeight - (int)coin.nHeight < ADAM_COIN_LOCK_MIN_INPUT_DEPTH) {
                            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-coinlock-input-immature",
                                strprintf("Coin-Lock registration input UTXO requires at least %d confirmations", ADAM_COIN_LOCK_MIN_INPUT_DEPTH));
                        }
                    }
                }
                if (nPoWLockOutputs > 1) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-too-many-powlock");
                }
                if (nPoWLockOutputs > 0) {
                    for (const CTxIn& txin : tx.vin) {
                        const Coin& coin = view.AccessCoin(txin.prevout);
                        if (coin.IsSpent() || (int)coin.nHeight > chainActive.Height() || nNextHeight - (int)coin.nHeight < ADAM_POW_LOCK_MIN_INPUT_DEPTH) {
                            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-powlock-input-immature",
                                strprintf("PoW-Lock registration input UTXO requires at least %d confirmations", ADAM_POW_LOCK_MIN_INPUT_DEPTH));
                        }
                    }
                }
            }

        if (deterministicMNManager) {
            if (!deterministicMNManager->CheckSpecialTx(tx, chainActive.Tip(), state, view)) {
                return false;
            }
            std::vector<unsigned char> vchProPayload;
            bool fProMalformed = false;
            const dip3::ProTxType proType = dip3::ExtractProTxPayload(tx, vchProPayload, fProMalformed);
            if (fProMalformed) {
                return state.DoS(100, false, REJECT_INVALID, "bad-protx-malformed");
            }

            if (proType != dip3::PROTX_NONE || !pool.setProTxIds.empty()) {
                dip3::CProRegTx regPtx;
                uint256 incomingTargetHash;
                CBLSPubKey incomingUpRegBls;
                const bool fNeedTipList = (proType == dip3::PROTX_UPSERV || proType == dip3::PROTX_UPREG ||
                                           proType == dip3::PROTX_UPREV || !pool.mapProUpByTarget.empty());
                dip3::CDeterministicMNList tipMnList;
                if (fNeedTipList) {
                    tipMnList = deterministicMNManager->GetListForBlock(chainActive.Tip());
                }

                // Collect any unconfirmed ProRegTx or pending ProUp*Tx whose collateral is spent by this valid incoming tx
                // so a rogue operator cannot censor the owner's collateral withdrawal.
                for (const CTxIn& txin : tx.vin) {
                    auto itRegCol = pool.mapProRegByCollateral.find(txin.prevout);
                    if (itRegCol != pool.mapProRegByCollateral.end()) {
                        for (const uint256& evictHash : itRegCol->second) {
                            vProTxToEvict.push_back(evictHash);
                        }
                    }
                    if (fNeedTipList) {
                        const dip3::CDeterministicMN* pOtherMn = tipMnList.GetMNByCollateral(txin.prevout);
                        if (pOtherMn) {
                            auto itUp = pool.mapProUpByTarget.find(pOtherMn->proTxHash);
                            if (itUp != pool.mapProUpByTarget.end()) {
                                for (const uint256& evictHash : itUp->second) {
                                    vProTxToEvict.push_back(evictHash);
                                }
                            }
                        }
                    }
                }
                if (!vProTxToEvict.empty()) {
                    std::sort(vProTxToEvict.begin(), vProTxToEvict.end());
                    vProTxToEvict.erase(std::unique(vProTxToEvict.begin(), vProTxToEvict.end()), vProTxToEvict.end());
                }

                if (proType == dip3::PROTX_REG) {
                    try {
                        CDataStream ds(vchProPayload, SER_NETWORK, PROTOCOL_VERSION);
                        ds >> regPtx;
                    } catch (...) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-protx-malformed");
                    }
                    if (!regPtx.collateralOutpoint.hash.IsNull() && pool.mapNextTx.count(regPtx.collateralOutpoint) > 0) {
                        return state.DoS(0, error("%s: ProRegTx collateral already spent in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-collateral-spent-mempool");
                    }
                    if (!regPtx.collateralOutpoint.hash.IsNull() && pool.mapProRegByCollateral.count(regPtx.collateralOutpoint) > 0) {
                        return state.DoS(0, error("%s: conflicting ProRegTx already in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-mempool-conflict");
                    }
                    const std::vector<unsigned char> ownerBytes(regPtx.pubKeyOwner.begin(), regPtx.pubKeyOwner.end());
                    if (pool.mapProRegByOwner.count(ownerBytes) > 0) {
                        return state.DoS(0, error("%s: conflicting ProRegTx owner already in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-mempool-conflict");
                    }
                    const std::vector<unsigned char> opBytes(regPtx.blsPubKeyOperator.begin(), regPtx.blsPubKeyOperator.end());
                    if (pool.mapProByOperator.count(opBytes) > 0) {
                        return state.DoS(0, error("%s: ProRegTx operator BLS conflicts with ProTx in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-mempool-conflict");
                    }
                } else if (proType == dip3::PROTX_UPSERV || proType == dip3::PROTX_UPREG || proType == dip3::PROTX_UPREV) {
                    try {
                        CDataStream ds(vchProPayload, SER_NETWORK, PROTOCOL_VERSION);
                        if (proType == dip3::PROTX_UPREG) {
                            dip3::CProUpRegTx upRegPtx;
                            ds >> upRegPtx;
                            incomingTargetHash = upRegPtx.proTxHash;
                            incomingUpRegBls = upRegPtx.blsPubKeyOperator;
                        } else {
                            uint16_t nVer = 0;
                            ds >> nVer >> incomingTargetHash;
                        }
                    } catch (...) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-protx-malformed");
                    }
                    const dip3::CDeterministicMN* pIncomingMn = tipMnList.GetMN(incomingTargetHash);
                    if (pIncomingMn && pool.mapNextTx.count(pIncomingMn->collateralOutpoint) > 0) {
                        return state.DoS(0, error("%s: target MN collateral already spent in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-collateral-spent-mempool");
                    }
                    if (pool.mapProUpByTarget.count(incomingTargetHash) > 0) {
                        return state.DoS(0, error("%s: conflicting ProUp*Tx already in mempool", __func__),
                                         REJECT_DUPLICATE, "bad-protx-mempool-conflict");
                    }
                    if (proType == dip3::PROTX_UPREG) {
                        const std::vector<unsigned char> opBytes(incomingUpRegBls.begin(), incomingUpRegBls.end());
                        if (pool.mapProByOperator.count(opBytes) > 0) {
                            return state.DoS(0, error("%s: ProUpRegTx operator BLS conflicts with ProTx in mempool", __func__),
                                             REJECT_DUPLICATE, "bad-protx-mempool-conflict");
                        }
                    }
                }
            }
        }

            CPubKey pingPubKey;
            COutPoint pingCollateral;
            if (ExtractOnChainMasternodePing(tx, pingPubKey, pingCollateral, false, nullptr, nullptr, nNextHeight, true)) {
                if (pcoinsTip && !pcoinsTip->HaveCoinInCache(pingCollateral)) {
                    coins_to_uncache.push_back(pingCollateral);
                }
            }
        }

        // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
        view.SetBackend(dummy);

        // Check for non-standard pay-to-script-hash in inputs
        if (!Params().IsRegTestNet() && !fZeroCoinPoW && !AreInputsStandard(tx, view))
            return state.Invalid(false, REJECT_NONSTANDARD, "bad-txns-nonstandard-inputs");

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = 0;
        nSigOps = GetLegacySigOpCount(tx);
        unsigned int nMaxSigOps = MAX_TX_SIGOPS_LEGACY;
        nSigOps += fZeroCoinPoW ? 0 : GetP2SHSigOpCount(tx, view);
        if(nSigOps > nMaxSigOps)
            return state.DoS(0, false, REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false,
                strprintf("%d > %d", nSigOps, nMaxSigOps));

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        CAmount inChainInputValue = 0;
        double dPriority = 0;
        bool fSpendsCoinbaseOrCoinstake = false;
        dPriority = fZeroCoinPoW ? 0 : view.GetPriority(tx, chainHeight, inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        for (const CTxIn &txin : tx.vin) {
            if (fZeroCoinPoW) continue;
            const Coin &coin = view.AccessCoin(txin.prevout);
            if (coin.IsCoinBase() || coin.IsCoinStake()) {
                fSpendsCoinbaseOrCoinstake = true;
                break;
            }
        }

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainHeight, pool.HasNoInputsOf(tx), inChainInputValue, fSpendsCoinbaseOrCoinstake, nSigOps);
        unsigned int nSize = entry.GetTxSize();

        // Don't accept it if it can't get into a block
        if (!ignoreFees && !fZeroCoinPoW) {
            CAmount txMinFee = GetMinRelayFee(tx, pool, nSize, true);
            if (nFees < txMinFee)
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient fee", false,
                    strprintf("%d < %d", nFees, txMinFee));

            // Require that free transactions have sufficient priority to be mined in the next block.
            if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(entry.GetPriority(chainHeight + 1))) {
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
            }

            // Continuously rate-limit free (really, very-low-fee) transactions
            // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
            // be annoying or make others' transactions take longer to confirm.
            if (fLimitFree && !fZeroCoinPoW && nFees < ::minRelayTxFee.GetFee(nSize)) {
                static RecursiveMutex csFreeLimiter;
                static double dFreeCount;
                static int64_t nLastTime;
                int64_t nNow = GetTime();

                LOCK(csFreeLimiter);

                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount >= GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 * 1000)
                    return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "rate limited free transaction");
                LogPrint(BCLog::MEMPOOL, "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
                dFreeCount += nSize;
            }
        }

        if (fRejectAbsurdFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
            return state.Invalid(false,
                REJECT_HIGHFEE, "absurdly-high-fee",
                strprintf("%d > %d", nFees, ::minRelayTxFee.GetFee(nSize) * 10000));

        // As zero fee transactions are not going to be accepted in the near future (4.0) and the code will be fully refactored soon.
        // This is just a quick inline towards that goal, the mempool by default will not accept them. Blocking
        // any subsequent network relay.
        if (!Params().IsRegTestNet() && !fZeroCoinPoW && nFees == 0) {
            return error("%s : zero fees not accepted %s, %d > %d",
                    __func__, hash.ToString(), nFees, ::minRelayTxFee.GetFee(nSize) * 10000);
        }

        // Calculate in-mempool ancestors, up to a limit.
        CTxMemPool::setEntries setAncestors;
        size_t nLimitAncestors = GetArg("-limitancestorcount", DEFAULT_ANCESTOR_LIMIT);
        size_t nLimitAncestorSize = GetArg("-limitancestorsize", DEFAULT_ANCESTOR_SIZE_LIMIT)*1000;
        size_t nLimitDescendants = GetArg("-limitdescendantcount", DEFAULT_DESCENDANT_LIMIT);
        size_t nLimitDescendantSize = GetArg("-limitdescendantsize", DEFAULT_DESCENDANT_SIZE_LIMIT)*1000;
        std::string errString;
        if (!pool.CalculateMemPoolAncestors(entry, setAncestors, nLimitAncestors, nLimitAncestorSize, nLimitDescendants, nLimitDescendantSize, errString)) {
            return state.DoS(0, error("%s : %s", __func__, errString), REJECT_NONSTANDARD, "too-long-mempool-chain", false);
        }
        for (const uint256& evictHash : vProTxToEvict) {
            CTxMemPool::txiter itEvict = pool.mapTx.find(evictHash);
            if (itEvict != pool.mapTx.end() && setAncestors.count(itEvict) > 0) {
                return state.Invalid(false, REJECT_CONFLICT, "txn-mempool-protx-collateral-conflict");
            }
        }

        bool fCLTVIsActivated = consensus.NetworkUpgradeActive(chainHeight, Consensus::UPGRADE_BIP65);

        if (!CheckSequenceLocks(tx, view, chainActive.Tip())) {
            return state.DoS(0, false, REJECT_NONSTANDARD, "non-BIP68-final");
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if (fCLTVIsActivated)
            flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

        PrecomputedTransactionData precomTxData(tx);
        if (!CheckInputs(tx, state, view, true, flags, true, precomTxData)) {
            return false;
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        flags = MANDATORY_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY;
        if (fCLTVIsActivated)
            flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        if (!CheckInputs(tx, state, view, true, flags, true, precomTxData)) {
            return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s, %s",
                    __func__, hash.ToString(), FormatStateMessage(state));
        }

        // Evict any conflicting mempool ProRegTx / ProUp*Tx after all signature and policy checks succeed
        for (const uint256& evictHash : vProTxToEvict) {
            CTxMemPool::txiter itEvict = pool.mapTx.find(evictHash);
            if (itEvict != pool.mapTx.end()) {
                std::list<CTransaction> dummyRemoved;
                pool.remove(itEvict->GetTx(), dummyRemoved, true);
            }
        }

        // Store transaction in memory
        pool.addUnchecked(hash, entry, setAncestors, !IsInitialBlockDownload());

        // trim mempool and check if tx was trimmed
        if (!fOverrideMempoolLimit) {
            LimitMempoolSize(pool, std::max<int64_t>(0, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE)) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
            if (!pool.exists(hash))
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "mempool full");
        }
    }

    GetMainSignals().SyncTransaction(tx, nullptr, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);

    return true;
}


bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState &state, const CTransaction &tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fOverrideMempoolLimit, bool fRejectAbsurdFee, bool fIgnoreFees)
{
    std::vector<COutPoint> coins_to_uncache;
    bool res = AcceptToMemoryPoolWorker(pool, state, tx, fLimitFree, pfMissingInputs, fOverrideMempoolLimit, fRejectAbsurdFee, fIgnoreFees, coins_to_uncache);
    if (!res) {
        for (const COutPoint& outpoint: coins_to_uncache)
            pcoinsTip->Uncache(outpoint);
    }
    return res;
}

bool AcceptableInputs(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee, bool isDSTX)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    const int chainHeight = chainActive.Height();
    const Consensus::Params& consensus = Params().GetConsensus();

    if (!CheckTransaction(tx, state))
        return error("%s : transaction checks for %s failed with %s", __func__, tx.GetHash().ToString(), FormatStateMessage(state));

    // Coinbase and coinstake are only valid in a block, not as loose transactions
    if (tx.IsCoinBase())
        return state.DoS(100, error("AcceptableInputs: : coinbase as individual tx"),
            REJECT_INVALID, "coinbase");
    if (tx.IsCoinStake())
        return state.DoS(100, error("AcceptableInputs: : coinstake as individual tx"),
            REJECT_INVALID, "coinstake");

    // is it already in the memory pool?
    uint256 hash = tx.GetHash();
    if (pool.exists(hash))
        return false;

    // Check for conflicts with in-memory transactions
    LOCK(pool.cs); // protect pool.mapNextTx
    for (const auto &in : tx.vin) {
        COutPoint outpoint = in.prevout;
        if (pool.mapNextTx.count(outpoint)) {
            // Disable replacement feature for now
            return false;
        }
    }


    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(pcoinsTip, pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            for (size_t out = 0; out < tx.vout.size(); out++) {
                COutPoint outpoint(hash, out);
                if (view.HaveCoin(outpoint)) {
                    return state.Invalid(false, REJECT_ALREADY_KNOWN, "txn-already-known");
                }
            }

            // do all inputs exist?
            for (const CTxIn& txin : tx.vin) {
                if (fZeroCoinPoW) {
                    continue;
                }
                if (!view.HaveCoin(txin.prevout)) {
                    if (pfMissingInputs) {
                        *pfMissingInputs = true;
                    }
                    return false; // fMissingInputs and !state.IsInvalid() is used to detect this condition, don't set state.Invalid()
                }
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = fZeroCoinPoW ? 0 : view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        }

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        unsigned int nMaxSigOps = MAX_TX_SIGOPS_LEGACY;
        nSigOps += fZeroCoinPoW ? 0 : GetP2SHSigOpCount(tx, view);
        if (nSigOps > nMaxSigOps)
            return state.DoS(0,
                error("AcceptableInputs : too many sigops %s, %d > %d",
                    hash.ToString(), nSigOps, nMaxSigOps),
                REJECT_NONSTANDARD, "bad-txns-too-many-sigops");

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn - nValueOut;
        CAmount inChainInputValue;
        double dPriority = fZeroCoinPoW ? 0 : view.GetPriority(tx, chainHeight, inChainInputValue);

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbaseOrCoinstake = false;
        for (const CTxIn &txin : tx.vin) {
            if (fZeroCoinPoW) continue;
            const Coin& coin = view.AccessCoin(txin.prevout);
            if (coin.IsCoinBase() || coin.IsCoinStake()) {
                fSpendsCoinbaseOrCoinstake = true;
                break;
            }
        }
        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, chainHeight, mempool.HasNoInputsOf(tx), inChainInputValue, fSpendsCoinbaseOrCoinstake, nSigOps);
        unsigned int nSize = entry.GetTxSize();

        // Don't accept it if it can't get into a block
        // but prioritise dstx and don't check fees for it
        if (isDSTX) {
            mempool.PrioritiseTransaction(hash, hash.ToString(), 1000, 0.1 * COIN);
        } else if (!fZeroCoinPoW) { // same as !ignoreFees for AcceptToMemoryPool
            CAmount txMinFee = GetMinRelayFee(tx, pool, nSize, true);
            if (nFees < txMinFee)
                return state.DoS(0, error("AcceptableInputs : not enough fees %s, %d < %d", hash.ToString(), nFees, txMinFee),
                    REJECT_INSUFFICIENTFEE, "insufficient fee");

            // Require that free transactions have sufficient priority to be mined in the next block.
            CAmount mempoolRejectFee = pool.GetMinFee(GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000).GetFee(nSize);
            if (mempoolRejectFee > 0 && nFees < mempoolRejectFee) {
                return state.DoS(0, error("%s : insuffiecient fee: %d < %d",
                        __func__, nFees, mempoolRejectFee), REJECT_INSUFFICIENTFEE, "mempool min fee not met", false);
            } else if (GetBoolArg("-relaypriority", DEFAULT_RELAYPRIORITY) && nFees < ::minRelayTxFee.GetFee(nSize) && !AllowFree(entry.GetPriority(chainActive.Height() + 1))) {
                // Require that free transactions have sufficient priority to be mined in the next block.
                return state.DoS(0, false, REJECT_INSUFFICIENTFEE, "insufficient priority");
            }

            // Continuously rate-limit free (really, very-low-fee) transactions
            // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
            // be annoying or make others' transactions take longer to confirm.
            if (fLimitFree && nFees < ::minRelayTxFee.GetFee(nSize)) {
                static RecursiveMutex csFreeLimiter;
                static double dFreeCount;
                static int64_t nLastTime;
                int64_t nNow = GetTime();

                LOCK(csFreeLimiter);

                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0 / 600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount >= GetArg("-limitfreerelay", DEFAULT_LIMITFREERELAY) * 10 * 1000)
                    return state.DoS(0, error("AcceptableInputs : free transaction rejected by rate limiter"),
                        REJECT_INSUFFICIENTFEE, "rate limited free transaction");
                LogPrint(BCLog::MEMPOOL, "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount + nSize);
                dFreeCount += nSize;
            }
        }

        if (fRejectInsaneFee && nFees > ::minRelayTxFee.GetFee(nSize) * 10000)
            return error("AcceptableInputs: : insane fees %s, %d > %d",
                hash.ToString(),
                nFees, ::minRelayTxFee.GetFee(nSize) * 10000);

        bool fCLTVIsActivated = consensus.NetworkUpgradeActive(chainActive.Tip()->nHeight, Consensus::UPGRADE_BIP65);

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
        if (fCLTVIsActivated)
            flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

        PrecomputedTransactionData precomTxData(tx);
        if (!CheckInputs(tx, state, view, false, flags, true, precomTxData)) {
            return error("AcceptableInputs: : ConnectInputs failed %s", hash.ToString());
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        // for any real tx this will be checked on AcceptToMemoryPool anyway
        //        if (!CheckInputs(tx, state, view, false, MANDATORY_SCRIPT_VERIFY_FLAGS, true, precomTxData))
        //        {
        //            return error("AcceptableInputs: : BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags %s", hash.ToString());
        //        }

        // Store transaction in memory
        // pool.addUnchecked(hash, entry);
    }

    // SyncWithWallets(tx, NULL);

    return true;
}


bool GetOutput(const uint256& hash, unsigned int index, CValidationState& state, CTxOut& out)
{
    CTransaction txPrev;
    uint256 hashBlock;
    if (!GetTransaction(hash, txPrev, hashBlock, true)) {
        return state.DoS(100, error("Output not found"));
    }
    if (index > txPrev.vout.size()) {
        return state.DoS(100, error("Output not found, invalid index %d for %s",index, hash.GetHex()));
    }
    out = txPrev.vout[index];
    return true;
}


/** Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock */
bool GetTransaction(const uint256& hash, CTransaction& txOut, uint256& hashBlock, bool fAllowSlow, CBlockIndex* blockIndex)
{
    CBlockIndex* pindexSlow = blockIndex;

    LOCK(cs_main);

    if (!blockIndex) {
        if (mempool.lookup(hash, txOut)) {
            return true;
        }

        if (fTxIndex && pblocktree) {
            CDiskTxPos postx;
            if (pblocktree->ReadTxIndex(hash, postx)) {
                CAutoFile file(OpenBlockFile(postx, true), SER_DISK, CLIENT_VERSION);
                if (file.IsNull())
                    return error("%s: OpenBlockFile failed", __func__);
                CBlockHeader header;
                try {
                    file >> header;
                    fseek(file.Get(), postx.nTxOffset, SEEK_CUR);
                    file >> txOut;
                } catch (const std::exception& e) {
                    return error("%s : Deserialize or I/O error - %s", __func__, e.what());
                }
                hashBlock = header.GetHash();
                if (txOut.GetHash() != hash)
                    return error("%s : txid mismatch", __func__);
                return true;
            }

            // transaction not found in the index, nothing more can be done
            return false;
        }

        if (fAllowSlow) { // use coin database to locate block that contains transaction, and scan it
            const Coin& coin = AccessByTxid(*pcoinsTip, hash);
            if (!coin.IsSpent()) pindexSlow = chainActive[coin.nHeight];
        }
    }

    if (pindexSlow) {
        CBlock block;
        if (ReadBlockFromDisk(block, pindexSlow)) {
            for (const CTransaction& tx : block.vtx) {
                if (tx.GetHash() == hash) {
                    txOut = tx;
                    hashBlock = pindexSlow->GetBlockHash();
                    return true;
                }
            }
        }
    }

    return false;
}


//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(const CBlock& block, CDiskBlockPos& pos)
{
    // Open history file to append
    CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("WriteBlockToDisk : OpenBlockFile failed");

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, block);
    fileout << FLATDATA(Params().MessageStart()) << nSize;

    // Write block
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("WriteBlockToDisk : ftell failed");
    pos.nPos = (unsigned int)fileOutPos;
    fileout << block;

    return true;
}


bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos)
{
    block.SetNull();

    if (pos.IsNull())
        return false;

    // Open history file to read
    CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("ReadBlockFromDisk : OpenBlockFile failed");

    // Read block
    try {
        filein >> block;
    } catch (const std::exception& e) {
        return error("%s : Deserialize or I/O error - %s", __func__, e.what());
    }

    // Check the header
    if (block.IsProofOfWork() && block.nVersion < 11) {
        if (!CheckProofOfWork(block.GetHash(), block.nBits))
            return error("ReadBlockFromDisk : Errors in block header");
    }

    return true;
}


bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex)
{
    if (!pindex || !(pindex->nStatus & BLOCK_HAVE_DATA))
        return false;
    if (!ReadBlockFromDisk(block, pindex->GetBlockPos()))
        return false;
    if (block.GetHash() != pindex->GetBlockHash()) {
        LogPrintf("%s : block=%s index=%s\n", __func__, block.GetHash().GetHex(), pindex->GetBlockHash().GetHex());
        return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
    }
    return true;
}



double ConvertBitsToDouble(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29) {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

bool IsInitialBlockDownload()
{
    static int64_t nIBDStartTime = GetTime();
    int maxPeerHeight = 0;
    int maxRawPeerHeight = -1;
    int nPeers = 0;
    int nHandshakedPeers = 0;
    if (g_connman) {
        nPeers = g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
    }

    LOCK(cs_main);
    const int chainHeight = chainActive.Height();
    if (fImporting || fReindex || fVerifyingBlocks || chainHeight < Checkpoints::GetTotalBlocksEstimate())
        return true;

    if (chainHeight < 0 || !chainActive.Tip())
        return true;

    // Use best valid header
    CBlockIndex* pindexHeader = pindexBestHeader;
    if (pindexHeader && (!pindexHeader->IsValid(BLOCK_VALID_TREE) || (pindexHeader->nStatus & BLOCK_FAILED_MASK))) {
        pindexHeader = chainActive.Tip();
    }

    if (!pindexHeader || chainHeight < pindexHeader->nHeight - 24 * 6)
        return true;

    if (nPeers == 0 && !Params().IsRegTestNet())
        return true;

    // When our active chain tip is past genesis, recent (within 30 minutes), and caught up with validated headers,
    // never allow unverified peer nStartingHeight claims to force the node into IBD.
    if (chainHeight > 0 &&
        chainActive.Tip()->GetBlockTime() >= GetTime() - 30 * 60 &&
        chainHeight >= pindexHeader->nHeight - 6) {
        return false;
    }

    if (g_connman) {
        int64_t nElapsedSecs = std::max<int64_t>(0, GetTime() - chainActive.Tip()->GetBlockTime());
        int nMaxPlausibleGain = (int)(nElapsedSecs / 10) + 120;
        const bool fStartupGrace = (GetTime() - nIBDStartTime < 90);
        const int nHeaderHeight = pindexHeader ? pindexHeader->nHeight : chainHeight;
        int nOutboundSyncedPeers = 0;
        std::vector<int> vInboundHeights;
        g_connman->ForEachNode([&maxPeerHeight, &maxRawPeerHeight, &nHandshakedPeers, &nOutboundSyncedPeers, &vInboundHeights, chainHeight, nHeaderHeight, nMaxPlausibleGain, fStartupGrace](CNode* pnode) {
            if (!pnode->fSuccessfullyConnected || pnode->fFeeler || pnode->fDisconnect) {
                return;
            }
            nHandshakedPeers++;
            int nPeerH = pnode->nStartingHeight;
            if (nPeerH > maxRawPeerHeight) {
                maxRawPeerHeight = nPeerH;
            }
            if (!pnode->fInbound) {
                if (nPeerH > 0 && chainHeight >= nPeerH) {
                    nOutboundSyncedPeers++;
                }
                const int nMaxHeaderBackedHeight = std::max(nHeaderHeight, chainHeight) + nMaxPlausibleGain;
                if (chainHeight > 0 && !fStartupGrace && nPeerH > nMaxHeaderBackedHeight) {
                    nPeerH = nMaxHeaderBackedHeight;
                }
                if (nPeerH > maxPeerHeight) {
                    maxPeerHeight = nPeerH;
                }
                return;
            }
            vInboundHeights.push_back(nPeerH);
        });
        const bool fClampInbound = (chainHeight > 0 && !fStartupGrace) || (nOutboundSyncedPeers > 0);
        for (int nPeerH : vInboundHeights) {
            if (fClampInbound && nPeerH > nHeaderHeight) {
                nPeerH = nHeaderHeight;
            }
            if (nPeerH > maxPeerHeight && (chainHeight == 0 || nPeerH <= chainHeight + nMaxPlausibleGain)) {
                maxPeerHeight = nPeerH;
            }
        }
    }

    if (maxPeerHeight > 0 && chainHeight < maxPeerHeight)
        return true;

    if (chainHeight < pindexHeader->nHeight)
        return true;

    if (chainHeight == 0) {
        if (nHandshakedPeers > 0 && maxRawPeerHeight == 0 && chainActive.Tip() == pindexHeader) {
            return false;
        }
        return true;
    }

    if (nHandshakedPeers > 0 && maxPeerHeight > 0 && chainHeight >= maxPeerHeight && chainActive.Tip() == pindexHeader) {
        return false;
    }

    if (pindexHeader->GetBlockTime() < GetTime() - nMaxTipAge)
        return true;

    return false;
}

bool fLargeWorkForkFound = false;
bool fLargeWorkInvalidChainFound = false;
CBlockIndex *pindexBestForkTip = NULL, *pindexBestForkBase = NULL;

static void AlertNotify(const std::string& strMessage, bool fThread)
{
    uiInterface.NotifyAlertChanged();
    std::string strCmd = GetArg("-alertnotify", "");
    if (strCmd.empty()) return;

    // Alert text should be plain ascii coming from a trusted source, but to
    // be safe we first strip anything not in safeChars, then add single quotes around
    // the whole string before passing it to the shell:
    std::string singleQuote("'");
    std::string safeStatus = SanitizeString(strMessage);
    safeStatus = singleQuote+safeStatus+singleQuote;
    boost::replace_all(strCmd, "%s", safeStatus);

    if (fThread)
        boost::thread t(runCommand, strCmd); // thread runs free
    else
        runCommand(strCmd);
}

void CheckForkWarningConditions()
{
    AssertLockHeld(cs_main);
    // Before we get past initial download, we cannot reliably alert about forks
    // (we assume we don't get stuck on a fork before the last checkpoint)
    if (IsInitialBlockDownload())
        return;

    const CBlockIndex* pChainTip = chainActive.Tip();
    if (!pChainTip)
        return;

    // If our best fork is no longer within 72 blocks (+/- 3 hours if no one mines it)
    // of our head, drop it
    if (pindexBestForkTip && pChainTip->nHeight - pindexBestForkTip->nHeight >= 72)
        pindexBestForkTip = nullptr;

    if (pindexBestForkTip || (pindexBestInvalid && pindexBestInvalid->nChainWork > pChainTip->nChainWork + (GetBlockProof(*pChainTip) * 6))) {
        if (!fLargeWorkForkFound && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                std::string warning = std::string("'Warning: Large-work fork detected, forking after block ") +
                                      pindexBestForkBase->phashBlock->ToString() + std::string("'");
                AlertNotify(warning, true);
            }
        }
        if (pindexBestForkTip && pindexBestForkBase) {
            if (pindexBestForkBase->phashBlock) {
                LogPrintf("CheckForkWarningConditions: Warning: Large valid fork found\n  forking the chain at height %d (%s)\n  lasting to height %d (%s).\nChain state database corruption likely.\n",
                    pindexBestForkBase->nHeight, pindexBestForkBase->phashBlock->ToString(),
                    pindexBestForkTip->nHeight, pindexBestForkTip->phashBlock->ToString());
                fLargeWorkForkFound = true;
            }
        } else {
            LogPrintf("CheckForkWarningConditions: Warning: Found invalid chain at least ~6 blocks longer than our best chain.\nChain state database corruption likely.\n");
            fLargeWorkInvalidChainFound = true;
        }
    } else {
        fLargeWorkForkFound = false;
        fLargeWorkInvalidChainFound = false;
    }
}

void CheckForkWarningConditionsOnNewFork(CBlockIndex* pindexNewForkTip)
{
    AssertLockHeld(cs_main);
    // If we are on a fork that is sufficiently large, set a warning flag
    CBlockIndex* pfork = pindexNewForkTip;
    CBlockIndex* plonger = chainActive.Tip();
    while (pfork && pfork != plonger) {
        while (plonger && plonger->nHeight > pfork->nHeight)
            plonger = plonger->pprev;
        if (pfork == plonger)
            break;
        pfork = pfork->pprev;
    }

    // We define a condition which we should warn the user about as a fork of at least 7 blocks
    // who's tip is within 72 blocks (+/- 3 hours if no one mines it) of ours
    // or a chain that is entirely longer than ours and invalid (note that this should be detected by both)
    // We use 7 blocks rather arbitrarily as it represents just under 10% of sustained network
    // hash rate operating on the fork.
    // We define it this way because it allows us to only store the highest fork tip (+ base) which meets
    // the 7-block condition and from this always have the most-likely-to-cause-warning fork
    if (pfork && (!pindexBestForkTip || (pindexBestForkTip && pindexNewForkTip->nHeight > pindexBestForkTip->nHeight)) &&
        pindexNewForkTip->nChainWork - pfork->nChainWork > (GetBlockProof(*pfork) * 7) &&
        chainActive.Height() - pindexNewForkTip->nHeight < 72) {
        pindexBestForkTip = pindexNewForkTip;
        pindexBestForkBase = pfork;
    }

    CheckForkWarningConditions();
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (!pindexBestInvalid || pindexNew->nChainWork > pindexBestInvalid->nChainWork)
        pindexBestInvalid = pindexNew;

    LogPrintf("InvalidChainFound: invalid block=%s  height=%d  log2_work=%.16f  date=%s\n",
        pindexNew->GetBlockHash().ToString(), pindexNew->nHeight,
        log(pindexNew->nChainWork.getdouble()) / log(2.0), DateTimeStrFormat("%Y-%m-%d %H:%M:%S",
                                                               pindexNew->GetBlockTime()));

    const CBlockIndex* pChainTip = chainActive.Tip();
    assert(pChainTip);
    LogPrintf("InvalidChainFound:  current best=%s  height=%d  log2_work=%.16f  date=%s\n",
            pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight, log(pChainTip->nChainWork.getdouble()) / log(2.0),
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()));

    CheckForkWarningConditions();
}

void static InvalidBlockFound(CBlockIndex* pindex, const CValidationState& state)
{
    int nDoS = 0;
    if (state.IsInvalid(nDoS)) {
        assert (state.GetRejectCode() < REJECT_INTERNAL); // Blocks are never rejected with internal reject codes
        PenalizeInvalidBlockSource(pindex->GetBlockHash(), (unsigned char) state.GetRejectCode(), state.GetRejectReason(), nDoS);
    }
    if (!state.CorruptionPossible()) {
        pindex->nStatus |= BLOCK_FAILED_VALID;
        setDirtyBlockIndex.insert(pindex);
        setBlockIndexCandidates.erase(pindex);
        InvalidChainFound(pindex);
    }
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight)
{
    // mark inputs spent
    if (!tx.IsCoinBase()) {
        const bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);
        txundo.vprevout.reserve(tx.vin.size());
        for (const CTxIn& txin : tx.vin) {
            txundo.vprevout.emplace_back();
            if (!fZeroCoinPoW && !txin.prevout.IsNull()) {
                inputs.SpendCoin(txin.prevout, &txundo.vprevout.back());
            }
        }
    }
    // add outputs
    AddCoins(inputs, tx, nHeight);
}

void UpdateCoins(const CTransaction& tx, CCoinsViewCache &inputs, int nHeight)
{
    CTxUndo txundo;
    UpdateCoins(tx, inputs, txundo, nHeight);
}

bool CScriptCheck::operator()()
{
    const CScript& scriptSig = scriptSigOverride.empty() ? ptxTo->vin[nIn].scriptSig : scriptSigOverride;
    return VerifyScript(scriptSig, scriptPubKey, nFlags, CachingTransactionSignatureChecker(ptxTo, nIn, amount, cacheStore, *precomTxData), &error);
}

int GetSpendHeight(const CCoinsViewCache& inputs)
{
    LOCK(cs_main);
    auto it = mapBlockIndex.find(inputs.GetBestBlock());
    if (it == mapBlockIndex.end() || !it->second)
        return 0;
    CBlockIndex* pindexPrev = it->second;
    return pindexPrev->nHeight + 1;
}

namespace Consensus {
bool CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& inputs, int nSpendHeight, bool fAllowCoinStake = false)
{
    // if we have already a checkpoint newer than this block 
    // then it is OK
    if (nSpendHeight <= Checkpoints::GetTotalBlocksEstimate())
        return true;

    if (IsZeroCoinPoWTransaction(tx)) {
        if (tx.vin.size() != 1 || tx.vout.size() != 1 || tx.GetValueOut() != 0 || tx.vout[0].nValue != 0) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-zerocoin-nonzero-out");
        }
        bool puzzleValid = false;
        const uint256 zeroCoinTarget = GetZeroCoinPoWLimit(::Params().NetworkIDString());
        std::vector<unsigned char> nonce;
        uint256 challenge;
        CPubKey pubkey;
        int64_t lockTime = 0;
        CKeyID pubkeyHash;
        CBLSPubKey blsPk;
        CBLSSignature blsPoP;
        if (MatchPoWLockRegistration(tx.vout[0].scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash, &blsPk, &blsPoP)) {
            const CBlockIndex* pindexInputsTip = nullptr;
            if (!inputs.GetBestBlock().IsNull()) {
                LOCK(cs_main);
                auto miTip = mapBlockIndex.find(inputs.GetBestBlock());
                if (miTip != mapBlockIndex.end()) {
                    pindexInputsTip = miTip->second;
                }
            }
            std::string strBlsErr;
            if (!CheckRegistrationBlsBinding(pubkey, blsPk, blsPoP, std::max(0, nSpendHeight), pindexInputsTip, &inputs, false, strBlsErr)) {
                return state.DoS(100, false, REJECT_INVALID, strBlsErr);
            }
            if (pubkey.IsValid() && pubkey.IsCompressed() && pubkey.GetID() == pubkeyHash && nonce.size() == 4) {
                uint32_t nNonce = nonce[0] | (nonce[1] << 8) | (nonce[2] << 16) | (nonce[3] << 24);
                uint256 puzzleHash = ComputeZeroCoinRegistrationHash(nNonce, challenge, pubkey, blsPk);
                if (puzzleHash <= zeroCoinTarget) {
                    puzzleValid = true;
                }
            }
        }
        if (!puzzleValid) {
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-zerocoin-pow-invalid");
        }
        return true;
    }

    // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
    // for an attacker to attempt to split the network.
    if (!inputs.HaveInputs(tx))
        return state.Invalid(false, 0, "", "Inputs unavailable");

    const Consensus::Params& consensus = ::Params().GetConsensus();
    CAmount nValueIn = 0;
    CAmount nFees = 0;
    for (unsigned int i = 0; i < tx.vin.size(); i++) {
        const COutPoint &prevout = tx.vin[i].prevout;
        const Coin& coin = inputs.AccessCoin(prevout);
        assert(!coin.IsSpent());

        // If prev is coinbase, check that it's matured
        if (coin.IsCoinBase() || coin.IsCoinStake()) {
            if ((signed long)nSpendHeight - coin.nHeight < consensus.nCoinbaseMaturity)
                return state.Invalid(false, REJECT_INVALID, "bad-txns-premature-spend-of-coinbase-coinstake",
                        strprintf("tried to spend coinbase/coinstake at depth %d", nSpendHeight - coin.nHeight));
        }

        // Check for negative or overflow input values
        if (!consensus.MoneyRange(coin.out.nValue) || coin.out.nValue > consensus.nMaxMoneyOut - nValueIn)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-inputvalues-outofrange");
        nValueIn += coin.out.nValue;
    }

    if (!tx.IsCoinStake() || !fAllowCoinStake) {
        if (nValueIn < tx.GetValueOut())
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-in-belowout", false,
                    strprintf("value in (%s) < value out (%s)", FormatMoney(nValueIn), FormatMoney(tx.GetValueOut())));

        // Tally transaction fees
        CAmount nTxFee = nValueIn - tx.GetValueOut();
        if (nTxFee < 0)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-negative");
        if (nTxFee > consensus.nMaxMoneyOut - nFees)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-fee-outofrange");
        nFees += nTxFee;
    }
    return true;
}

}// namespace Consensus

static bool IsMinimalDevFundPush(const std::vector<unsigned char>& data, opcodetype opcode)
{
    if (data.empty()) {
        return opcode == OP_0;
    }
    if (data.size() == 1 && data[0] >= 1 && data[0] <= 16) {
        return opcode == OP_1 + (data[0] - 1);
    }
    if (data.size() == 1 && data[0] == 0x81) {
        return opcode == OP_1NEGATE;
    }
    if (data.size() <= 75) {
        return opcode == data.size();
    }
    if (data.size() <= 255) {
        return opcode == OP_PUSHDATA1;
    }
    if (data.size() <= 65535) {
        return opcode == OP_PUSHDATA2;
    }
    return false;
}

static bool ParseStrictDevFundScriptSig(const CScript& scriptSig, std::vector<std::vector<unsigned char>>& vPushesOut)
{
    vPushesOut.clear();
    CScript::const_iterator pc = scriptSig.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;
    while (pc < scriptSig.end()) {
        if (!scriptSig.GetOp(pc, opcode, vch)) {
            return false;
        }
        if (opcode > OP_PUSHDATA4 || opcode < 0) {
            return false;
        }
        if (!IsMinimalDevFundPush(vch, opcode)) {
            return false;
        }
        vPushesOut.push_back(std::move(vch));
    }
    return pc == scriptSig.end();
}

static bool VerifyDeveloperFundInput(const CTransaction& tx,
                                     unsigned int nIn,
                                     const CScript& prevScriptPubKey,
                                     int nSpendHeight,
                                     const CBlockIndex* pindexPrev,
                                     const CCoinsViewCache& view,
                                     CValidationState& state,
                                     CScript* pScriptPubKeyOverride = nullptr,
                                     CScript* pScriptSigOverride = nullptr)
{
    const CTxDestination devFundDest = DecodeDestination(Params().DeveloperFundAddress());
    const CScript devFundCanonicalScript = GetScriptForDestination(devFundDest);
    if (prevScriptPubKey != devFundCanonicalScript) {
        txnouttype type;
        std::vector<CTxDestination> addresses;
        int nRequired;
        if (ExtractDestinations(prevScriptPubKey, type, addresses, nRequired)) {
            for (const auto& addr : addresses) {
                if (EncodeDestination(addr) == Params().DeveloperFundAddress()) {
                    return state.DoS(100, error("VerifyDeveloperFundInput() : non-canonical developer fund script"),
                        REJECT_INVALID, "bad-txns-malformed-quorum-sig");
                }
            }
        }
        return true;
    }

    if (nSpendHeight < Params().GetConsensus().nTreasuryGovernanceStartHeight) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : spending from developer fund before start height is prohibited"),
            REJECT_INVALID, "bad-txns-spend-devfund-early");
    }

    std::vector<std::vector<unsigned char>> vPushes;
    if (!ParseStrictDevFundScriptSig(tx.vin[nIn].scriptSig, vPushes)) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : malformed scriptSig for developer fund spend"),
            REJECT_INVALID, "bad-txns-malformed-quorum-sig");
    }
    if (vPushes.size() < 3) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : missing quorum signature for developer fund spend"),
            REJECT_INVALID, "bad-txns-missing-quorum-sig");
    }
    if (vPushes.size() != 3) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : unexpected push count for developer fund spend"),
            REJECT_INVALID, "bad-txns-malformed-quorum-sig");
    }

    llmq::CQuorumSignature qsig;
    try {
        CDataStream ss(vPushes[2], SER_NETWORK, PROTOCOL_VERSION);
        ss >> qsig;
        if (!ss.empty()) {
            return state.DoS(100, error("VerifyDeveloperFundInput() : trailing bytes in quorum signature"),
                REJECT_INVALID, "bad-txns-malformed-quorum-sig");
        }
    } catch (const std::exception& e) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : failed to deserialize quorum signature: %s", e.what()),
            REJECT_INVALID, "bad-txns-malformed-quorum-sig");
    }

    const CBlockIndex* pindexPrevSpend = pindexPrev;
    if (!pindexPrevSpend && !view.GetBestBlock().IsNull()) {
        auto itPrevSpend = mapBlockIndex.find(view.GetBestBlock());
        if (itPrevSpend != mapBlockIndex.end()) {
            pindexPrevSpend = itPrevSpend->second;
        }
    }
    llmq::CQuorum quorum = llmq::GetActiveQuorum(nSpendHeight, pindexPrevSpend, &view);

    CMutableTransaction txTmp(tx);
    for (unsigned int idx = 0; idx < txTmp.vin.size(); ++idx) {
        if (txTmp.vin[idx].prevout == tx.vin[nIn].prevout ||
            view.AccessCoin(txTmp.vin[idx].prevout).out.scriptPubKey == devFundCanonicalScript) {
            std::vector<std::vector<unsigned char>> vPushesTmp;
            if (!ParseStrictDevFundScriptSig(txTmp.vin[idx].scriptSig, vPushesTmp) || vPushesTmp.size() != 3) {
                return state.DoS(100, error("VerifyDeveloperFundInput() : malformed co-input scriptSig for developer fund spend"),
                    REJECT_INVALID, "bad-txns-malformed-quorum-sig");
            }
            CScript scriptSigTmp;
            scriptSigTmp << vPushesTmp[0] << vPushesTmp[1];
            txTmp.vin[idx].scriptSig = scriptSigTmp;
        }
    }
    qsig.blockHash = txTmp.GetHash();
    if (!qsig.Verify(quorum, pindexPrevSpend, BLSDomain::LLMQ_TREASURY, nSpendHeight)) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : invalid quorum signature for developer fund spend"),
            REJECT_INVALID, "bad-txns-invalid-quorum-sig");
    }

    if (!CheckSignatureEncoding(vPushes[0], SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC, nullptr)) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : non-canonical treasury ECDSA signature for developer fund spend"),
            REJECT_INVALID, "bad-txns-invalid-treasury-sig");
    }

    CPubKey treasuryKey(ParseHex(Params().GetConsensus().strTreasuryPubKey));
    if (!treasuryKey.IsValid()) {
        return state.DoS(100, error("VerifyDeveloperFundInput() : invalid treasury public key"),
            REJECT_INVALID, "bad-txns-invalid-treasury-key");
    }
    if (pScriptPubKeyOverride) {
        *pScriptPubKeyOverride = GetScriptForDestination(treasuryKey.GetID());
    }
    if (pScriptSigOverride) {
        *pScriptSigOverride = (CScript() << vPushes[0] << vPushes[1]);
    }
    return true;
}

bool CheckInputs(const CTransaction& tx, CValidationState &state, const CCoinsViewCache &inputs, bool fScriptChecks, unsigned int flags, bool cacheStore, PrecomputedTransactionData& precomTxData, std::vector<CScriptCheck> *pvChecks, bool fAllowCoinStake)
{
    if (!tx.IsCoinBase()) {

        if (!Consensus::CheckTxInputs(tx, state, inputs, GetSpendHeight(inputs), fAllowCoinStake))
            return false;

        if (IsZeroCoinPoWTransaction(tx))
            return true;

        if (pvChecks)
            pvChecks->reserve(tx.vin.size());

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.

        // Skip ECDSA signature verification when connecting blocks
        // before the last block chain checkpoint. This is safe because block merkle hashes are
        // still computed and checked, and any change will be caught at the next checkpoint.
        if (fScriptChecks) {
            if (!precomTxData.m_spent_outputs_ready) {
                std::vector<CTxOut> spentOutputs(tx.vin.size());
                for (unsigned int j = 0; j < tx.vin.size(); j++) {
                    const Coin& c = inputs.AccessCoin(tx.vin[j].prevout);
                    assert(!c.IsSpent());
                    spentOutputs[j] = c.out;
                }
                precomTxData.Init(tx, std::move(spentOutputs));
            }
            const int nSpendHeight = GetSpendHeight(inputs);
            for (unsigned int i = 0; i < tx.vin.size(); i++) {
                const COutPoint& prevout = tx.vin[i].prevout;
                const Coin& coin = inputs.AccessCoin(prevout);
                assert(!coin.IsSpent());

                // We very carefully only pass in things to CScriptCheck which
                // are clearly committed to by tx' witness hash. This provides
                // a sanity check that our caching is not introducing consensus
                // failures through additional data in, eg, the coins being
                // spent being checked as a part of CScriptCheck.
                CScript scriptPubKey = coin.out.scriptPubKey;
                const CAmount amount = coin.out.nValue;
                CScript scriptSigOverride;

                if (!VerifyDeveloperFundInput(tx, i, scriptPubKey, nSpendHeight, nullptr, inputs, state, &scriptPubKey, &scriptSigOverride)) {
                    return false;
                }

                // Verify signature
                unsigned int inputFlags = flags;
                if (fAllowCoinStake && IsValidPoLSelfRenewal(tx, i, coin.out, nSpendHeight)) {
                    inputFlags &= ~(SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS);
                }
                CScriptCheck check(scriptPubKey, amount, tx, i, inputFlags, cacheStore, &precomTxData, scriptSigOverride);
                if (pvChecks) {
                    pvChecks->push_back(CScriptCheck());
                    check.swap(pvChecks->back());
                } else if (!check()) {
                    if (inputFlags & STANDARD_NOT_MANDATORY_VERIFY_FLAGS) {
                        // Check whether the failure was caused by a
                        // non-mandatory script verification check, such as
                        // non-standard DER encodings or non-null dummy
                        // arguments; if so, don't trigger DoS protection to
                        // avoid splitting the network between upgraded and
                        // non-upgraded nodes.
                        CScriptCheck check2(scriptPubKey, amount, tx, i,
                            inputFlags & ~STANDARD_NOT_MANDATORY_VERIFY_FLAGS, cacheStore, &precomTxData, scriptSigOverride);
                        if (check2())
                            return state.Invalid(false, REJECT_NONSTANDARD, strprintf("non-mandatory-script-verify-flag (%s)", ScriptErrorString(check.GetScriptError())));
                    }
                    // Failures of other flags indicate a transaction that is
                    // invalid in new blocks, e.g. a invalid P2SH. We DoS ban
                    // such nodes as they are not following the protocol. That
                    // said during an upgrade careful thought should be taken
                    // as to the correct behavior - we may want to continue
                    // peering with non-upgraded nodes even after a soft-fork
                    // super-majority vote has passed.
                    return state.DoS(100, false, REJECT_INVALID, strprintf("mandatory-script-verify-flag-failed (%s)", ScriptErrorString(check.GetScriptError())));
                }
            }
        }
    }

    return true;
}


/** Abort with a message */
static bool AbortNode(const std::string& strMessage, const std::string& userMessage="")
{
    strMiscWarning = strMessage;
    LogPrintf("*** %s\n", strMessage);
    uiInterface.ThreadSafeMessageBox(
        userMessage.empty() ? _("Error: A fatal internal error occured, see debug.log for details") : userMessage,
        "", CClientUIInterface::MSG_ERROR);
    StartShutdown();
    return false;
}

static bool AbortNode(CValidationState& state, const std::string& strMessage, const std::string& userMessage="")
{
    AbortNode(strMessage, userMessage);
    return state.Error(strMessage);
}

namespace {

bool UndoWriteToDisk(const CBlockUndo& blockundo, CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to append
    CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : OpenUndoFile failed", __func__);

    // Write index header
    unsigned int nSize = GetSerializeSize(fileout, blockundo);
    fileout << FLATDATA(Params().MessageStart()) << nSize;

    // Write undo data
    long fileOutPos = ftell(fileout.Get());
    if (fileOutPos < 0)
        return error("%s : ftell failed", __func__);
    pos.nPos = (unsigned int)fileOutPos;
    fileout << blockundo;

    // calculate & write checksum
    CHashWriter hasher(SER_GETHASH, PROTOCOL_VERSION);
    hasher << hashBlock;
    hasher << blockundo;
    fileout << hasher.GetHash();

    return true;
}

} // anon namespace

bool UndoReadFromDisk(CBlockUndo& blockundo, const CDiskBlockPos& pos, const uint256& hashBlock)
{
    // Open history file to read
    CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
    if (filein.IsNull())
        return error("%s : OpenBlockFile failed", __func__);

    // Read block
    uint256 hashChecksum;
    CHashVerifier<CAutoFile> verifier(&filein); // We need a CHashVerifier as reserializing may lose data
    try {
        verifier << hashBlock;
        verifier >> blockundo;
        filein >> hashChecksum;
    } catch (const std::exception& e) {
        return error("%s : Deserialize or I/O error - %s", __func__, e.what());
    }

    // Verify checksum
    if (hashChecksum != verifier.GetHash())
        return error("%s : Checksum mismatch", __func__);

    return true;
}

enum DisconnectResult
{
    DISCONNECT_OK,      // All good.
    DISCONNECT_UNCLEAN, // Rolled back, but UTXO set was inconsistent with block.
    DISCONNECT_FAILED   // Something else went wrong.
};

/**
 * Restore the UTXO in a Coin at a given COutPoint
 * @param undo The Coin to be restored.
 * @param view The coins view to which to apply the changes.
 * @param out The out point that corresponds to the tx input.
 * @return A DisconnectResult as an int
 */
int ApplyTxInUndo(Coin&& undo, CCoinsViewCache& view, const COutPoint& out)
{
    bool fClean = true;

    if (view.HaveCoin(out)) fClean = false; // overwriting transaction output

    if (undo.nHeight == 0) {
        // Missing undo metadata (height and coinbase/coinstake). Older versions included this
        // information only in undo records for the last spend of a transactions'
        // outputs. This implies that it must be present for some other output of the same tx.
        const Coin& alternate = AccessByTxid(view, out.hash);
        if (!alternate.IsSpent()) {
            undo.nHeight = alternate.nHeight;
            undo.fCoinBase = alternate.fCoinBase;
            undo.fCoinStake = alternate.fCoinStake;
        } else {
            return DISCONNECT_FAILED; // adding output for transaction without known metadata
        }
    }
    view.AddCoin(out, std::move(undo), false);

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}


/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  When UNCLEAN or FAILED is returned, view is left in an indeterminate state. */
DisconnectResult DisconnectBlock(CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view)
{
    AssertLockHeld(cs_main);

    if (pindex->GetBlockHash() != view.GetBestBlock())
        LogPrintf("%s : pindex=%s view=%s\n", __func__, pindex->GetBlockHash().GetHex(), view.GetBestBlock().GetHex());
    assert(pindex->GetBlockHash() == view.GetBestBlock());

    bool fClean = true;

    CBlockUndo blockUndo;
    __int128 nValueIn128 = 0;
    __int128 nBurnedInBlock128 = 0;
    CDiskBlockPos pos = pindex->GetUndoPos();
    if (pos.IsNull()) {
        error("%s: no undo data available", __func__);
        return DISCONNECT_FAILED;
    }
    if (!UndoReadFromDisk(blockUndo, pos, pindex->pprev->GetBlockHash())) {
        error("%s: failure reading undo data", __func__);
        return DISCONNECT_FAILED;
    }

    if (blockUndo.vtxundo.size() + 1 != block.vtx.size()) {
        error("%s: block and undo data inconsistent", __func__);
        return DISCONNECT_FAILED;
    }

    // undo transactions in reverse order
    bool fNeedBurnFlush = false;
    const uint256 hashBlockDisconnect = pindex->GetBlockHash();
    __int128 nSpendableValueOut128 = 0;
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const CTransaction& tx = block.vtx[i];

        for (const auto& out : tx.vout) {
            if (!out.scriptPubKey.IsUnspendable()) {
                nSpendableValueOut128 += out.nValue;
            }
        }
        uint256 hash = tx.GetHash();


        // Check that all outputs are available and match the outputs in the block itself
        // exactly.
        for (size_t o = 0; o < tx.vout.size(); o++) {
            if (!tx.vout[o].scriptPubKey.IsUnspendable()) {
                COutPoint out(hash, o);
                Coin coin;
                view.SpendCoin(out, &coin);
                if (tx.vout[o] != coin.out) {
                    fClean = false; // transaction output mismatch
                }
            }
        }

        // not coinbases because they dont have traditional inputs
        if (tx.IsCoinBase() || IsZeroCoinPoWTransaction(tx)) {
            if (tx.IsCoinBase()) {
                for (const auto& out : tx.vout) {
                    txnouttype type;
                    std::vector<CTxDestination> addresses;
                    int nRequired;
                    if (ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
                        for (const auto& addr : addresses) {
                            std::string strAddr = EncodeDestination(addr);
                            if (Params().GetConsensus().IsBurnAddress(strAddr, pindex->nHeight)) {
                                nBurnedInBlock128 += out.nValue;
                            }
                        }
                    }
                }
            }
            continue;
        }

        // restore inputs
        CTxUndo& txundo = blockUndo.vtxundo[i - 1];
        if (txundo.vprevout.size() != tx.vin.size()) {
            error("%s: transaction and undo data inconsistent - txundo.vprevout.siz=%d tx.vin.siz=%d",
                    __func__, txundo.vprevout.size(), tx.vin.size());
            return DISCONNECT_FAILED;
        }
        for (unsigned int j = tx.vin.size(); j-- > 0;) {
            const COutPoint& out = tx.vin[j].prevout;
            if (out.IsNull()) continue;
            int res = ApplyTxInUndo(std::move(txundo.vprevout[j]), view, out);
            if (res == DISCONNECT_FAILED) return DISCONNECT_FAILED;
            fClean = fClean && res != DISCONNECT_UNCLEAN;
        }
        // At this point, all of txundo.vprevout should have been moved out.

        // Remove burn from cache after inputs are restored into view (works without -txindex)
        {
            CAmount nBurnAmount = 0;
            const bool hasBurn = GetTransactionBurnAmount(tx, pindex->nHeight, nBurnAmount);
            if (hasBurn) {
                nBurnedInBlock128 += nBurnAmount;
            }
            if (hasBurn && !tx.IsCoinStake() && !tx.vin.empty()) {
                const CTxIn& txin = tx.vin[0];
                CScript prevScript;
                const Coin& restoredCoin = view.AccessCoin(txin.prevout);
                if (!restoredCoin.IsSpent()) {
                    prevScript = restoredCoin.out.scriptPubKey;
                } else {
                    CTransaction txPrev;
                    uint256 hashBlock;
                    if (GetTransaction(txin.prevout.hash, txPrev, hashBlock, true) && txin.prevout.n < txPrev.vout.size()) {
                        prevScript = txPrev.vout[txin.prevout.n].scriptPubKey;
                    }
                }
                if (!prevScript.empty()) {
                    COutPoint targetOut;
                    if (ResolveVoluntaryBurnTargetOutpoint(tx, pindex->nHeight, prevScript, &view, targetOut)) {
                        RemoveOutpointBurnFromCache(targetOut, nBurnAmount, pindex->nHeight);
                        fNeedBurnFlush = true;
                    }
                }
            }
            if (block.IsProofOfStake() && i == 1 && !tx.vin.empty()) {
                CScript prevStakeScript;
                CAmount nPrevStakeAmount = 0;
                const Coin& restoredStakeCoin = view.AccessCoin(tx.vin[0].prevout);
                if (!restoredStakeCoin.IsSpent()) {
                    prevStakeScript = restoredStakeCoin.out.scriptPubKey;
                    nPrevStakeAmount = restoredStakeCoin.out.nValue;
                } else {
                    CTransaction txPrev;
                    uint256 hashBlk;
                    if (GetTransaction(tx.vin[0].prevout.hash, txPrev, hashBlk, true) && tx.vin[0].prevout.n < txPrev.vout.size()) {
                        prevStakeScript = txPrev.vout[tx.vin[0].prevout.n].scriptPubKey;
                        nPrevStakeAmount = txPrev.vout[tx.vin[0].prevout.n].nValue;
                    }
                }
                if (UndoOutpointBurnTransferForBlock(block, pindex->nHeight, hashBlockDisconnect, prevStakeScript, nPrevStakeAmount)) {
                    fNeedBurnFlush = true;
                }
            }
        }

        if (view.HaveInputs(tx))
            nValueIn128 += view.GetValueIn(tx);
    }

    if (fClean) {
        if (fNeedBurnFlush && g_fMasternodePingStateInitialized && !fVerifyingBlocks) {
            FlushBurnCacheToDisk(pindex->pprev ? pindex->pprev->GetBlockHash() : UINT256_ZERO);
        }

        // track money
        const __int128 nSupplyDelta128 = nSpendableValueOut128 - nValueIn128 - nBurnedInBlock128;
        nMoneySupply = (CAmount)((__int128)nMoneySupply - nSupplyDelta128);
    }

    // move best block pointer to prevout block
    view.SetBestBlock(pindex->pprev->GetBlockHash());
    if (fClean && !fVerifyingBlocks) {
        UnpinBLSPubKeysAtHeight(pindex->nHeight, pindex->GetBlockHash());
        if (deterministicMNManager && pindex->nHeight > 0) {
            if (!deterministicMNManager->UndoBlock(block, pindex)) {
                error("%s: evodb UndoBlock failed for %s", __func__, pindex->GetBlockHash().ToString());
                fClean = false;
            }
        }

        // Revert masternode active pings (both explicit tx and implicit block participation) on disconnect
        std::set<CPubKey> setEmptiedMasternodes;
        std::map<CPubKey, COutPoint> mapEmptiedCollaterals;
        for (auto itSet = mapMasternodeActiveHeights.begin(); itSet != mapMasternodeActiveHeights.end(); ) {
            if (itSet->second.erase(pindex->nHeight) > 0) {
                if (!itSet->second.empty()) {
                    mapMasternodeLastActiveHeight[itSet->first] = *itSet->second.rbegin();
                    ++itSet;
                } else {
                    CPubKey pubKeyMasternode = itSet->first;
                    itSet = mapMasternodeActiveHeights.erase(itSet);
                    mapMasternodeLastActiveHeight.erase(pubKeyMasternode);
                    auto itColl = mapMasternodeCollateralOutpoints.find(pubKeyMasternode);
                    if (itColl != mapMasternodeCollateralOutpoints.end()) {
                        mapEmptiedCollaterals[pubKeyMasternode] = itColl->second;
                    }
                    setEmptiedMasternodes.insert(pubKeyMasternode);
                }
            } else {
                ++itSet;
            }
        }

        for (const CTransaction& tx : block.vtx) {
            CPubKey pubKeyMasternode;
            COutPoint collateralOutpoint;
            COnChainMasternodeDelegation inlineDel;
            if (ExtractOnChainMasternodePing(tx, pubKeyMasternode, collateralOutpoint, false, &inlineDel, &view)) {
                if (!collateralOutpoint.IsNull() && inlineDel.pubKeyCollateralAddress.IsValid()) {
                    CPubKey restoredOldKey;
                    RevertOnChainCollateralDelegation(collateralOutpoint, pubKeyMasternode, pindex->nHeight, &restoredOldKey);
                    if (restoredOldKey.IsValid() && mapMasternodeActiveHeights.find(restoredOldKey) == mapMasternodeActiveHeights.end()) {
                        mapEmptiedCollaterals[restoredOldKey] = collateralOutpoint;
                        setEmptiedMasternodes.insert(restoredOldKey);
                    }
                }
                if (mapMasternodeActiveHeights.find(pubKeyMasternode) == mapMasternodeActiveHeights.end()) {
                    if (!collateralOutpoint.IsNull()) {
                        mapEmptiedCollaterals[pubKeyMasternode] = collateralOutpoint;
                    }
                    setEmptiedMasternodes.insert(pubKeyMasternode);
                }
            }
        }

        for (const CPubKey& pubKeyMasternode : setEmptiedMasternodes) {
            COutPoint prevCollateral;
            auto itSavedColl = mapEmptiedCollaterals.find(pubKeyMasternode);
            if (itSavedColl != mapEmptiedCollaterals.end()) {
                prevCollateral = itSavedColl->second;
            } else {
                auto itColl = mapMasternodeCollateralOutpoints.find(pubKeyMasternode);
                if (itColl != mapMasternodeCollateralOutpoints.end()) {
                    prevCollateral = itColl->second;
                }
            }
            mapMasternodeLastActiveHeight.erase(pubKeyMasternode);
            mapMasternodeCollateralOutpoints.erase(pubKeyMasternode);
            // Scan back up to 2880 blocks (full consensus activity window) to find previous valid ping or ADAM/LLMQ block participation heartbeat
            const CBlockIndex* pindexScan = pindex->pprev;
            while (pindexScan && pindexScan->nHeight > 0 && pindex->nHeight - pindexScan->nHeight <= 2880) {
                CBlock prevBlock;
                if (ReadBlockFromDisk(prevBlock, pindexScan)) {
                    CPubKey restoredKey;
                    if (RestoreMasternodeActivityFromBlock(prevBlock, pindexScan->nHeight, pubKeyMasternode, prevCollateral, &view, &restoredKey)) {
                        if (restoredKey.IsValid() && restoredKey != pubKeyMasternode) {
                            const CBlockIndex* pindexRescan = pindex->pprev;
                            while (pindexRescan && pindexRescan->nHeight > pindexScan->nHeight) {
                                CBlock rescanBlock;
                                if (ReadBlockFromDisk(rescanBlock, pindexRescan) &&
                                    RestoreMasternodeActivityFromBlock(rescanBlock, pindexRescan->nHeight, restoredKey, prevCollateral, &view)) {
                                    break;
                                }
                                pindexRescan = pindexRescan->pprev;
                            }
                        }
                        break;
                    }
                }
                pindexScan = pindexScan->pprev;
            }
        }

        BumpMasternodePingStateGen();
        llmq::ClearQuorumCache();
        ClearPrevLlmqCache();

        if (fClean && g_fMasternodePingStateInitialized) {
            FlushMasternodePingStateToDisk(pindex->pprev ? pindex->pprev->GetBlockHash() : UINT256_ZERO);
        }
    }

    return fClean ? DISCONNECT_OK : DISCONNECT_UNCLEAN;
}

void static FlushBlockFile(bool fFinalize = false)
{
    LOCK(cs_LastBlockFile);

    CDiskBlockPos posOld(nLastBlockFile, 0);

    FILE* fileOld = OpenBlockFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }

    fileOld = OpenUndoFile(posOld);
    if (fileOld) {
        if (fFinalize)
            TruncateFile(fileOld, vinfoBlockFile[nLastBlockFile].nUndoSize);
        FileCommit(fileOld);
        fclose(fileOld);
    }
}

bool FindUndoPos(CValidationState& state, int nFile, CDiskBlockPos& pos, unsigned int nAddSize);

static CCheckQueue<CScriptCheck> scriptcheckqueue(128);

void ThreadScriptCheck()
{
    util::ThreadRename("kristatech-scriptch");
    scriptcheckqueue.Thread();
}

static int64_t nTimeVerify = 0;
static int64_t nTimeConnect = 0;
static int64_t nTimeIndex = 0;
static int64_t nTimeCallbacks = 0;
static int64_t nTimeTotal = 0;

static uint256 GetV2StakeModifierPrevoutId(const CBlock& block)
{
    if (block.IsProofOfStake() && block.vtx.size() >= 2 && !block.vtx[1].vin.empty()) {
        return SerializeHash(block.vtx[1].vin[0].prevout);
    }
    return block.GetHash();
}

static bool CanComputeV2StakeModifier(const CBlockIndex* pindex)
{
    if (!pindex || !pindex->pprev) {
        return true;
    }
    if (pindex->pprev->nHeight == 0) {
        return true;
    }
    return pindex->pprev->GeneratedStakeModifier() && !pindex->pprev->GetStakeModifierV2().IsNull();
}

bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view, bool fJustCheck, bool fAlreadyChecked)
{
    AssertLockHeld(cs_main);
    // Check it again in case a previous version let a bad block in, or if CheckBlock previously ran in offline-sync mode (!block.fChecked)
    if ((!fAlreadyChecked || (!block.fChecked && !fJustCheck)) && !CheckBlock(block, state, !fJustCheck, !fJustCheck, !fJustCheck, &view)) {
        if (state.CorruptionPossible()) {
            // We don't write down blocks to disk if they may have been
            // corrupted, so this should be impossible unless we're having hardware
            // problems.
            return AbortNode(state, "Corrupt block found indicating potential hardware failure; shutting down");
        }
        return error("%s: CheckBlock failed for %s: %s", __func__, block.GetHash().ToString(), FormatStateMessage(state));
    }

    // verify that the view's current state corresponds to the previous block
    uint256 hashPrevBlock = pindex->pprev == NULL ? UINT256_ZERO : pindex->pprev->GetBlockHash();
    if (hashPrevBlock != view.GetBestBlock())
        LogPrintf("%s: hashPrev=%s view=%s\n", __func__, hashPrevBlock.GetHex(), view.GetBestBlock().GetHex());
    assert(hashPrevBlock == view.GetBestBlock());

    const Consensus::Params& consensus = Params().GetConsensus();

    // Special case for the genesis block, skipping connection of its transactions
    // (its coinbase is unspendable)
    if (block.GetHash() == consensus.hashGenesisBlock) {
        if (!fJustCheck)
            view.SetBestBlock(pindex->GetBlockHash());
        return true;
    }

    const bool isPoSActive = consensus.NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_POS);
    if (!isPoSActive && block.IsProofOfStake())
        return state.DoS(100, error("ConnectBlock() : PoS period not active"),
            REJECT_INVALID, "PoS-early");

    if (isPoSActive && block.IsProofOfWork() && !IsAdamActive(pindex->nHeight, consensus))
        return state.DoS(100, error("ConnectBlock() : Legacy PoW period ended"),
            REJECT_INVALID, "legacy-pow-ended");

    bool fScriptChecks = pindex->nHeight >= Checkpoints::GetTotalBlocksEstimate();

    // If scripts won't be checked anyways, don't bother seeing if CLTV is activated
    bool fCLTVIsActivated = false;
    if (fScriptChecks && pindex->pprev) {
        fCLTVIsActivated = consensus.NetworkUpgradeActive(pindex->pprev->nHeight, Consensus::UPGRADE_BIP65);
    }

    CCheckQueueControl<CScriptCheck> control(fScriptChecks && nScriptCheckThreads ? &scriptcheckqueue : nullptr);

    int64_t nTimeStart = GetTimeMicros();
    __int128 nFees128 = 0;
    int nInputs = 0;
    unsigned int nSigOps = 0;
    CDiskTxPos pos(pindex->GetBlockPos(), GetSizeOfCompactSize(block.vtx.size()));
    std::vector<std::pair<uint256, CDiskTxPos> > vPos;
    vPos.reserve(block.vtx.size());
    CBlockUndo blockundo;
    blockundo.vtxundo.reserve(block.vtx.size() - 1);
    __int128 nValueOut128 = 0;
    __int128 nSpendableValueOut128 = 0;
    __int128 nValueIn128 = 0;
    __int128 nBurnedInBlock128 = 0;
    std::vector<std::pair<COutPoint, CAmount>> vPendingOutpointBurns;
    std::pair<COutPoint, COutPoint> pendingCoinstakeTransfer;
    bool fHasPendingCoinstakeTransfer = false;
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
    std::vector<uint256> vSpendsInBlock;
    uint256 hashBlock = block.GetHash();

    if (!fJustCheck && pindex->nHeight > 0 && !block.vtx.empty() && CanComputeV2StakeModifier(pindex)) {
        const uint256 expectedPrevoutId = GetV2StakeModifierPrevoutId(block);
        uint256 prevMod = UINT256_ZERO;
        if (pindex->pprev) {
            if (pindex->pprev->nHeight > 0) {
                prevMod = pindex->pprev->GetStakeModifierV2();
            } else {
                CHashWriter ssSeed(SER_GETHASH, 0);
                ssSeed << pindex->pprev->GetBlockHash() << pindex->pprev->GetStakeModifierV1();
                prevMod = ssSeed.GetHash();
            }
        }
        CHashWriter ssExpectedMod(SER_GETHASH, 0);
        ssExpectedMod << expectedPrevoutId << prevMod;
        if (!pindex->GeneratedStakeModifier() || pindex->GetStakeModifierV2() != ssExpectedMod.GetHash()) {
            pindex->SetNewStakeModifier(expectedPrevoutId);
            setDirtyBlockIndex.insert(pindex);
        }
    }

    if (block.IsProofOfStake() && pindex->pprev) {
        std::string strPoSError;
        if (!CheckProofOfStake(block, strPoSError, pindex->pprev, &view)) {
            return state.DoS(100, error("ConnectBlock() : proof-of-stake validation failed (%s)", strPoSError),
                REJECT_INVALID, "bad-pos-kernel");
        }
    }

    dip3::CDeterministicMNList preparedDip3List;
    bool fHasPreparedDip3List = false;
    if (deterministicMNManager && pindex->nHeight > 0) {
        const bool fParentInEvodb = !pindex->pprev || pindex->pprev->nHeight == 0 ||
            !pindex->pprev->phashBlock || deterministicMNManager->HasListForBlock(pindex->pprev->GetBlockHash());
        if (!fVerifyingBlocks || fParentInEvodb) {
            if (!deterministicMNManager->ProcessBlock(block, pindex, state, view, true, &preparedDip3List, nullptr)) {
                return false;
            }
            fHasPreparedDip3List = true;
        }
    }

    std::vector<PrecomputedTransactionData> precomTxData;
    precomTxData.reserve(block.vtx.size()); // Required so that pointers to individual precomTxData don't get invalidated
    const bool fDualTrackBlock = IsAdamDualTrackActive(pindex->nHeight);
    const uint256 minerPowLimit = GetMinerPoWLimit(Params().NetworkIDString());
    std::map<CBLSPubKey, CPubKey> seenBlockRegBls;
    auto checkBlockRegBls = [&](const CPubKey& regPubKey, const CBLSPubKey& regBlsPk, const CBLSSignature& regBlsPoP) -> bool {
        std::string strBlsErr;
        if (!CheckRegistrationBlsBinding(regPubKey, regBlsPk, regBlsPoP, pindex->nHeight, pindex->pprev, &view, true, strBlsErr)) {
            return state.DoS(100, error("ConnectBlock() : registration BLS binding failed: %s", strBlsErr),
                REJECT_INVALID, strBlsErr);
        }
        if (regBlsPk.IsValid()) {
            auto itSeen = seenBlockRegBls.find(regBlsPk);
            if (itSeen != seenBlockRegBls.end() && itSeen->second != regPubKey) {
                return state.DoS(100, error("ConnectBlock() : duplicate BLS pubkey across block registrations"),
                    REJECT_INVALID, "bad-txns-minerreg-dup-bls");
            }
            seenBlockRegBls.emplace(regBlsPk, regPubKey);
        }
        return true;
    };

    for (unsigned int i = 0; i < block.vtx.size(); i++) {
        const CTransaction& tx = block.vtx[i];
        const bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);

        nInputs += tx.vin.size();
        nSigOps += GetLegacySigOpCount(tx);
        if (nSigOps > nMaxBlockSigOps)
            return state.DoS(100, error("ConnectBlock() : too many sigops"), REJECT_INVALID, "bad-blk-sigops");

        if (fZeroCoinPoW && !tx.vout.empty()) {
            std::vector<unsigned char> zNonce;
            uint256 zChallenge;
            CPubKey zPubKey;
            int64_t zLockTime = 0;
            CKeyID zPubKeyHash;
            CBLSPubKey zBlsPk;
            CBLSSignature zBlsPoP;
            if (MatchPoWLockRegistration(tx.vout[0].scriptPubKey, zNonce, zChallenge, zPubKey, zLockTime, zPubKeyHash, &zBlsPk, &zBlsPoP)) {
                if (!checkBlockRegBls(zPubKey, zBlsPk, zBlsPoP)) {
                    return false;
                }
            }
        }

        if (!tx.IsCoinBase() && !fZeroCoinPoW) {
            if (!view.HaveInputs(tx))
                return state.DoS(100, error("ConnectBlock() : inputs missing/spent"),
                    REJECT_INVALID, "bad-txns-inputs-missingorspent");

            // Prevent spending from a designated burn address
            for (const CTxIn& txin : tx.vin) {
                const Coin& coin = view.AccessCoin(txin.prevout);
                if (!coin.IsSpent()) {
                    txnouttype type;
                    std::vector<CTxDestination> addresses;
                    int nRequired;
                    if (ExtractDestinations(coin.out.scriptPubKey, type, addresses, nRequired)) {
                        for (const auto& addr : addresses) {
                            std::string strAddr = EncodeDestination(addr);
                            if (Params().GetConsensus().IsBurnAddress(strAddr, pindex->nHeight)) {
                                return state.DoS(100, error("ConnectBlock() : spending from a burn address is prohibited"),
                                    REJECT_INVALID, "bad-txns-spend-burn-address");
                            }
                        }
                    }
                }
            }

            // Enforce Coin-Lock & PoW-Lock registration output limits, BLS binding, and input UTXO maturity
            {
                int nCoinLockOutputs = 0;
                int nPoWLockOutputs = 0;
                for (const CTxOut& txout : tx.vout) {
                    std::vector<unsigned char> vchNonce;
                    uint256 hashChallenge;
                    CPubKey regPubKey;
                    int64_t nRegLockTime = 0;
                    CKeyID regKeyId;
                    CBLSPubKey regBlsPk;
                    CBLSSignature regBlsPoP;
                    if (MatchCoinLockRegistration(txout.scriptPubKey, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                        if (!checkBlockRegBls(regPubKey, regBlsPk, regBlsPoP)) {
                            return false;
                        }
                        nCoinLockOutputs++;
                    } else if (MatchPoWLockRegistration(txout.scriptPubKey, vchNonce, hashChallenge, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                        if (!checkBlockRegBls(regPubKey, regBlsPk, regBlsPoP)) {
                            return false;
                        }
                        if (!regPubKey.IsValid() || !regPubKey.IsCompressed() || regPubKey.GetID() != regKeyId || vchNonce.size() != 4) {
                            return state.DoS(100, error("ConnectBlock() : funded PoW-Lock registration invalid key or nonce"),
                                REJECT_INVALID, "bad-txns-powlock-invalid-key");
                        }
                        bool fChallengeFresh = false;
                        auto miChal = mapBlockIndex.find(hashChallenge);
                        if (miChal != mapBlockIndex.end() && miChal->second && pindex->pprev &&
                            miChal->second->nHeight >= 0 && miChal->second->nHeight <= pindex->pprev->nHeight &&
                            pindex->pprev->GetAncestor(miChal->second->nHeight) == miChal->second &&
                            miChal->second->nHeight >= pindex->nHeight - 100 && miChal->second->nHeight < pindex->nHeight) {
                            fChallengeFresh = true;
                        }
                        if (!fChallengeFresh) {
                            return state.DoS(100, error("ConnectBlock() : funded PoW-Lock challenge block stale or not in ancestor chain"),
                                REJECT_INVALID, "bad-txns-powlock-stale-challenge");
                        }
                        const uint32_t nRegNonce = (uint32_t)vchNonce[0] | ((uint32_t)vchNonce[1] << 8) |
                                                   ((uint32_t)vchNonce[2] << 16) | ((uint32_t)vchNonce[3] << 24);
                        if (ComputeMinerPoWRegistrationHash(nRegNonce, hashChallenge, regPubKey, regBlsPk) > minerPowLimit) {
                            return state.DoS(100, error("ConnectBlock() : funded PoW-Lock registration puzzle hash failed"),
                                REJECT_INVALID, "bad-txns-powlock-invalid");
                        }
                        nPoWLockOutputs++;
                    }
                }
                if (fDualTrackBlock) {
                    if (nCoinLockOutputs > 1) {
                        return state.DoS(100, error("ConnectBlock() : more than one Coin-Lock registration output in a single transaction"),
                            REJECT_INVALID, "bad-txns-too-many-coinlock");
                    }
                    if (nCoinLockOutputs > 0) {
                        for (const CTxIn& txin : tx.vin) {
                            const Coin& coin = view.AccessCoin(txin.prevout);
                            if (coin.IsSpent() || pindex->nHeight - (int)coin.nHeight < ADAM_COIN_LOCK_MIN_INPUT_DEPTH) {
                                return state.DoS(100, error("ConnectBlock() : Coin-Lock registration input UTXO requires at least %d confirmations (got %d)",
                                    ADAM_COIN_LOCK_MIN_INPUT_DEPTH, pindex->nHeight - (int)coin.nHeight),
                                    REJECT_INVALID, "bad-txns-coinlock-input-immature");
                            }
                        }
                    }
                    if (nPoWLockOutputs > 1) {
                        return state.DoS(100, error("ConnectBlock() : more than one PoW-Lock registration output in a single transaction"),
                            REJECT_INVALID, "bad-txns-too-many-powlock");
                    }
                    if (nPoWLockOutputs > 0) {
                        for (const CTxIn& txin : tx.vin) {
                            const Coin& coin = view.AccessCoin(txin.prevout);
                            if (coin.IsSpent() || pindex->nHeight - (int)coin.nHeight < ADAM_POW_LOCK_MIN_INPUT_DEPTH) {
                                return state.DoS(100, error("ConnectBlock() : PoW-Lock registration input UTXO requires at least %d confirmations (got %d)",
                                    ADAM_POW_LOCK_MIN_INPUT_DEPTH, pindex->nHeight - (int)coin.nHeight),
                                    REJECT_INVALID, "bad-txns-powlock-input-immature");
                            }
                        }
                    }
                }
            }

            // Enforce Developer Treasury Governance rules (Model B + C)
            for (unsigned int inIdx = 0; inIdx < tx.vin.size(); ++inIdx) {
                const Coin& coin = view.AccessCoin(tx.vin[inIdx].prevout);
                if (!coin.IsSpent()) {
                    if (!VerifyDeveloperFundInput(tx, inIdx, coin.out.scriptPubKey, pindex->nHeight, pindex->pprev, view, state)) {
                        return false;
                    }
                }
            }

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += GetP2SHSigOpCount(tx, view);
            if (nSigOps > nMaxBlockSigOps)
                return state.DoS(100, error("ConnectBlock() : too many sigops"), REJECT_INVALID, "bad-blk-sigops");

        }

        // Cache the sig ser hashes
        precomTxData.emplace_back(tx);

        if (!tx.IsCoinBase()) {
            if (!fZeroCoinPoW) {
                const CAmount txValIn = view.GetValueIn(tx);
                if (!tx.IsCoinStake() || !block.IsProofOfStake() || i != 1)
                    nFees128 += (__int128)txValIn - (__int128)tx.GetValueOut();
                nValueIn128 += txValIn;
            }

            if (!CheckSequenceLocks(tx, view, pindex->pprev)) {
                return state.DoS(100, error("%s: transaction %s is not BIP68-final", __func__, tx.GetHash().ToString()), REJECT_INVALID, "bad-txns-nonfinal");
            }

            std::vector<CScriptCheck> vChecks;
            unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_TAPROOT_BIP341;
            if (fCLTVIsActivated)
                flags |= SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

            const bool fAllowCoinStake = block.IsProofOfStake() && i == 1;
            bool fCacheResults = fJustCheck; /* Don't cache results if we're actually connecting blocks (still consult the cache, though) */
            if (!CheckInputs(tx, state, view, fScriptChecks, flags, fCacheResults, precomTxData[i], nScriptCheckThreads ? &vChecks : NULL, fAllowCoinStake))
                return error("%s: Check inputs on %s failed with %s", __func__, tx.GetHash().ToString(), FormatStateMessage(state));
            control.Add(vChecks);
        }
        nValueOut128 += tx.GetValueOut();
        for (const auto& out : tx.vout) {
            if (!out.scriptPubKey.IsUnspendable()) {
                nSpendableValueOut128 += out.nValue;
            }
        }

        // Extract burn information & coinstake PoB lineage before UpdateCoins is called
        if (!fJustCheck && !fZeroCoinPoW) {
            if (block.IsProofOfStake() && i == 1 && !tx.vin.empty() && tx.vout.size() >= 2) {
                const COutPoint& fromOut = tx.vin[0].prevout;
                const Coin& stakeCoin = view.AccessCoin(fromOut);
                if (!stakeCoin.IsSpent() &&
                    tx.vout[1].scriptPubKey == stakeCoin.out.scriptPubKey &&
                    tx.vout[1].nValue >= stakeCoin.out.nValue &&
                    HasActiveOutpointBurn(fromOut, pindex->nHeight)) {
                    pendingCoinstakeTransfer = std::make_pair(fromOut, COutPoint(tx.GetHash(), 1));
                    fHasPendingCoinstakeTransfer = true;
                }
            }

            CAmount nBurnAmount = 0;
            const bool hasBurn = GetTransactionBurnAmount(tx, pindex->nHeight, nBurnAmount);
            if (hasBurn) {
                nBurnedInBlock128 += nBurnAmount;
            }
            // Only voluntary user Proof-of-Burn transactions (!IsCoinBase && !IsCoinStake) grant PoB staking weight.
            // Protocol unclaimed burns in CoinBase/CoinStake reduce nMoneySupply without granting free PoB weight to block producers.
            if (!fJustCheck && hasBurn && !tx.IsCoinBase() && !tx.IsCoinStake() && !tx.vin.empty()) {
                const CTxIn& txin = tx.vin[0];
                const Coin& coin = view.AccessCoin(txin.prevout);
                if (!coin.IsSpent()) {
                    COutPoint targetOut;
                    if (ResolveVoluntaryBurnTargetOutpoint(tx, pindex->nHeight, coin.out.scriptPubKey, &view, targetOut)) {
                        vPendingOutpointBurns.emplace_back(targetOut, nBurnAmount);
                    }
                }
            }
        }

        CTxUndo undoDummy;
        if (i > 0) {
            blockundo.vtxundo.emplace_back();
        }
        UpdateCoins(tx, view, i == 0 ? undoDummy : blockundo.vtxundo.back(), pindex->nHeight);

        vPos.emplace_back(tx.GetHash(), pos);
        pos.nTxOffset += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
    }

    if (nFees128 < 0 || nFees128 > consensus.nMaxMoneyOut) {
        return state.DoS(100, error("ConnectBlock() : block fees out of range"), REJECT_INVALID, "bad-blk-fees-outofrange");
    }
    const CAmount nFees = (CAmount)nFees128;

    // track mint amount info in 128-bit arithmetic to avoid intra-block signed int64 overflow
    const __int128 nMint128 = (nValueOut128 - nValueIn128) + nFees128;

    int64_t nTime1 = GetTimeMicros();
    nTimeConnect += nTime1 - nTimeStart;
    LogPrint(BCLog::BENCH, "      - Connect %u transactions: %.2fms (%.3fms/tx, %.3fms/txin) [%.2fs]\n", (unsigned)block.vtx.size(), 0.001 * (nTime1 - nTimeStart), 0.001 * (nTime1 - nTimeStart) / block.vtx.size(), nInputs <= 1 ? 0 : 0.001 * (nTime1 - nTimeStart) / (nInputs - 1), nTimeConnect * 0.000001);

    // Fee redistribution applies symmetrically to PoW and PoS producers.
    CAmount nExpectedMint = CMasternode::GetBlockValue(pindex->pprev->nHeight + 1);
    nExpectedMint += nFees;

    //Check that the block does not overmint
    const bool fMintFitsCAmount = (nMint128 >= std::numeric_limits<CAmount>::min() && nMint128 <= std::numeric_limits<CAmount>::max());
    if (nMint128 > (__int128)nExpectedMint || (fMintFitsCAmount && !IsBlockValueValid(pindex->nHeight, nExpectedMint, (CAmount)nMint128))) {
        return state.DoS(100, error("ConnectBlock() : reward pays too much (actual=%s vs limit=%s)",
                                    FormatMoney(fMintFitsCAmount ? (CAmount)nMint128 : std::numeric_limits<CAmount>::max()), FormatMoney(nExpectedMint)),
                         REJECT_INVALID, "bad-cb-amount");
    }

    if (!control.Wait())
        return state.DoS(100, error("%s: CheckQueue failed", __func__), REJECT_INVALID, "block-validation-failed");
    int64_t nTime2 = GetTimeMicros();
    nTimeVerify += nTime2 - nTimeStart;
    LogPrint(BCLog::BENCH, "    - Verify %u txins: %.2fms (%.3fms/txin) [%.2fs]\n", nInputs - 1, 0.001 * (nTime2 - nTimeStart), nInputs <= 1 ? 0 : 0.001 * (nTime2 - nTimeStart) / (nInputs - 1), nTimeVerify * 0.000001);

    //IMPORTANT NOTE: Nothing before this point should actually store to disk (or even memory)
    if (fJustCheck)
        return true;

    // Write undo information to disk
    if (pindex->GetUndoPos().IsNull() || !pindex->IsValid(BLOCK_VALID_SCRIPTS)) {
        if (pindex->GetUndoPos().IsNull()) {
            CDiskBlockPos diskPosBlock;
            if (!FindUndoPos(state, pindex->nFile, diskPosBlock, ::GetSerializeSize(blockundo, SER_DISK, CLIENT_VERSION) + 40))
                return error("ConnectBlock() : FindUndoPos failed");
            if (!UndoWriteToDisk(blockundo, diskPosBlock, pindex->pprev->GetBlockHash()))
                return AbortNode(state, "Failed to write undo data");

            // update nUndoPos in block index
            pindex->nUndoPos = diskPosBlock.nPos;
            pindex->nStatus |= BLOCK_HAVE_UNDO;
        }

        pindex->RaiseValidity(BLOCK_VALID_SCRIPTS);
        setDirtyBlockIndex.insert(pindex);
    }


    if (fTxIndex)
        if (!pblocktree->WriteTxIndex(vPos))
            return AbortNode(state, "Failed to write transaction index");

    // Persist deterministic MN list to evodb before mutating global supply / view / caches
    if (!fJustCheck && !fVerifyingBlocks && fHasPreparedDip3List && deterministicMNManager) {
        if (!deterministicMNManager->CommitBlockList(pindex->GetBlockHash(), preparedDip3List, uint256())) {
            return AbortNode(state, "Failed to commit deterministic MN list to evodb");
        }
    }

    // add this block to the view's block chain
    view.SetBestBlock(pindex->GetBlockHash());

    // Update KRISTA money supply (L-1: exclude IsUnspendable outputs)
    nMoneySupply = (CAmount)((__int128)nMoneySupply + (nSpendableValueOut128 - nValueIn128 - nBurnedInBlock128));

    int64_t nTime3 = GetTimeMicros();
    nTimeIndex += nTime3 - nTime2;
    LogPrint(BCLog::BENCH, "    - Index writing: %.2fms [%.2fs]\n", 0.001 * (nTime3 - nTime2), nTimeIndex * 0.000001);

    // Watch for changes to the previous coinbase transaction.
    static uint256 hashPrevBestCoinBase;
    GetMainSignals().UpdatedTransaction(hashPrevBestCoinBase);
    hashPrevBestCoinBase = block.vtx[0].GetHash();

    int64_t nTime4 = GetTimeMicros();
    nTimeCallbacks += nTime4 - nTime3;
    LogPrint(BCLog::BENCH, "    - Callbacks: %.2fms [%.2fs]\n", 0.001 * (nTime4 - nTime3), nTimeCallbacks * 0.000001);

    if (!fJustCheck && block.nVersion >= 11) {
        uint256 prevSeed = GetAdamSeed(pindex->pprev);
        uint256 newSeed = ComputeNextAdamSeed(prevSeed, block.vAdamVRFProof);
        {
            LOCK(cs_adam_seeds);
            mapAdamSeeds[pindex->GetBlockHash()] = newSeed;
        }
    }

    if (!fJustCheck &&
        (fHasPendingCoinstakeTransfer || !vPendingOutpointBurns.empty())) {
        if (fHasPendingCoinstakeTransfer) {
            TransferOutpointBurnOnCoinstake(pendingCoinstakeTransfer.first, pendingCoinstakeTransfer.second, pindex->nHeight, hashBlock);
        }
        for (const auto& oburn : vPendingOutpointBurns) {
            AddOutpointBurnToCache(oburn.first, oburn.second, pindex->nHeight);
        }
        if (g_fMasternodePingStateInitialized && !fVerifyingBlocks) {
            FlushBurnCacheToDisk(pindex->GetBlockHash());
        }
    }

    if (!fJustCheck && !fVerifyingBlocks) {
        RegisterBlockMasternodeActivity(block, pindex->nHeight, &view);
    }

    return true;
}


bool FlushStateToDisk(CValidationState& state, FlushStateMode mode)
{
    int64_t nMempoolUsage = mempool.DynamicMemoryUsage();
    LOCK(cs_main);
    static int64_t nLastWrite = 0;
    static int64_t nLastFlush = 0;
    static int64_t nLastSetChain = 0;
    try {
        int64_t nNow = GetTimeMicros();
        // Avoid writing/flushing immediately after startup.
        if (nLastWrite == 0) {
            nLastWrite = nNow;
        }
        if (nLastFlush == 0) {
            nLastFlush = nNow;
        }
        if (nLastSetChain == 0) {
            nLastSetChain = nNow;
        }
        int64_t nMempoolSizeMax = GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000;
        int64_t cacheSize = pcoinsTip->DynamicMemoryUsage() * DB_PEAK_USAGE_FACTOR;
        int64_t nTotalSpace = nCoinCacheUsage + std::max<int64_t>(nMempoolSizeMax - nMempoolUsage, 0);
        // The cache is large and we're within 10% and 10 MiB of the limit, but we have time now
        // (not in the middle of a block processing).
        bool fCacheLarge = mode == FLUSH_STATE_PERIODIC &&
                cacheSize > std::max((9 * nTotalSpace) / 10, nTotalSpace - MAX_BLOCK_COINSDB_USAGE * 1024 * 1024);
        // The cache is over the limit, we have to write now.
        bool fCacheCritical = mode == FLUSH_STATE_IF_NEEDED && (unsigned) cacheSize > nCoinCacheUsage;
        // It's been a while since we wrote the block index to disk.
        // Do this frequently, so we don't need to redownload after a crash.
        bool fPeriodicWrite = mode == FLUSH_STATE_PERIODIC && nNow > nLastWrite + (int64_t)DATABASE_WRITE_INTERVAL * 1000000;
        // It's been very long since we flushed the cache. Do this infrequently, to optimize cache usage.
        bool fPeriodicFlush = mode == FLUSH_STATE_PERIODIC && nNow > nLastFlush + (int64_t)DATABASE_FLUSH_INTERVAL * 1000000;
        // Combine all conditions that result in a full cache flush.
        bool fDoFullFlush = (mode == FLUSH_STATE_ALWAYS) || fCacheLarge || fCacheCritical || fPeriodicFlush;
        // Write blocks and block index to disk.
        if (fDoFullFlush || fPeriodicWrite) {
            // Depend on nMinDiskSpace to ensure we can write block index
            if (!CheckDiskSpace(0))
                return state.Error("out of disk space");
            // First make sure all block and undo data is flushed to disk.
            FlushBlockFile();
            // Then update all block file information (which may refer to block and undo files).
            {
                std::vector<std::pair<int, const CBlockFileInfo*> > vFiles;
                vFiles.reserve(setDirtyFileInfo.size());
                for (std::set<int>::iterator it = setDirtyFileInfo.begin(); it != setDirtyFileInfo.end(); ) {
                    vFiles.push_back(std::make_pair(*it, &vinfoBlockFile[*it]));
                    setDirtyFileInfo.erase(it++);
                }
                std::vector<const CBlockIndex*> vBlocks;
                vBlocks.reserve(setDirtyBlockIndex.size());
                for (std::set<CBlockIndex*>::iterator it = setDirtyBlockIndex.begin(); it != setDirtyBlockIndex.end(); ) {
                    vBlocks.push_back(*it);
                    setDirtyBlockIndex.erase(it++);
                }
                if (!pblocktree->WriteBatchSync(vFiles, nLastBlockFile, vBlocks)) {
                    return AbortNode(state, "Files to write to block index database");
                }
            }
            nLastWrite = nNow;
        }

        // Flush best chain related state. This can only be done if the blocks / block index write was also done.
        if (fDoFullFlush) {
            // Typical Coin structures on disk are around 48 bytes in size.
            // Pushing a new one to the database can cause it to be written
            // twice (once in the log, and once in the tables). This is already
            // an overestimation, as most will delete an existing entry or
            // overwrite one. Still, use a conservative safety factor of 2.
            if (!CheckDiskSpace(48 * 2 * 2 * pcoinsTip->GetCacheSize()))
                return state.Error("out of disk space");
            // Flush the chainstate (which may refer to block index entries).
            if (!pcoinsTip->Flush())
                return AbortNode(state, "Failed to write to coin database");
            if (deterministicMNManager && chainActive.Tip() && !fVerifyingBlocks) {
                static int nLastEvodbPrunedHeight = 0;
                const int nRetain = std::max(2000, (int)std::min<int64_t>(1000000, GetArg("-checkblocks", DEFAULT_CHECKBLOCKS)) + 100);
                const int nTipH = chainActive.Height();
                if (nTipH > nRetain) {
                    const int nPruneUpTo = nTipH - nRetain;
                    if (nLastEvodbPrunedHeight <= 0) {
                        nLastEvodbPrunedHeight = std::max(0, nPruneUpTo - 500);
                    }
                    if (nPruneUpTo > nLastEvodbPrunedHeight) {
                        std::vector<uint256> vPruneHashes;
                        vPruneHashes.reserve(nPruneUpTo - nLastEvodbPrunedHeight);
                        for (int h = nLastEvodbPrunedHeight + 1; h <= nPruneUpTo; ++h) {
                            if (chainActive[h] && chainActive[h]->phashBlock) {
                                vPruneHashes.push_back(chainActive[h]->GetBlockHash());
                            }
                        }
                        if (deterministicMNManager->PruneLists(vPruneHashes)) {
                            nLastEvodbPrunedHeight = nPruneUpTo;
                        }
                    }
                }
            }
            if (chainActive.Tip() && g_fMasternodePingStateInitialized && !fVerifyingBlocks) {
                FlushBurnCacheToDisk(chainActive.Tip()->GetBlockHash());
                FlushMasternodePingStateToDisk(chainActive.Tip()->GetBlockHash());
            }
            nLastFlush = nNow;
        }
        if ((mode == FLUSH_STATE_ALWAYS || mode == FLUSH_STATE_PERIODIC) && nNow > nLastSetChain + (int64_t)DATABASE_WRITE_INTERVAL * 1000000) {
            // Update best block in wallet (so we can detect restored wallets).
            GetMainSignals().SetBestChain(chainActive.GetLocator());
            nLastSetChain = nNow;
        }

    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error while flushing: ") + e.what());
    }
    return true;
}


void FlushStateToDisk()
{
    CValidationState state;
    FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
}

/** Update chainActive and related internal data structures. */
void static UpdateTip(CBlockIndex* pindexNew)
{
    chainActive.SetTip(pindexNew);

    // New best block
    nTimeBestReceived = GetTime();
    mempool.AddTransactionsUpdated(1);

    {
        LOCK(g_best_block_mutex);
        g_best_block = pindexNew->GetBlockHash();
        g_best_block_cv.notify_all();
    }

    const CBlockIndex* pChainTip = chainActive.Tip();
    LogPrintf("%s: new best=%s  height=%d version=%d  log2_work=%.16f  tx=%lu  date=%s progress=%f  cache=%.1fMiB(%utxo)\n",
              __func__,
              pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight, pChainTip->nVersion, log(pChainTip->nChainWork.getdouble()) / log(2.0), (unsigned long)pChainTip->nChainTx,
              DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()),
              Checkpoints::GuessVerificationProgress(pChainTip), pcoinsTip->DynamicMemoryUsage() * (1.0 / (1<<20)), pcoinsTip->GetCacheSize());

    // Check the version of the last 100 blocks to see if we need to upgrade:
    static bool fWarned = false;
    if (!IsInitialBlockDownload() && !fWarned) {
        int nUpgraded = 0;
        const CBlockIndex* pindex = pChainTip;
        for (int i = 0; i < 100 && pindex != NULL; i++) {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
                ++nUpgraded;
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
            LogPrintf("SetBestChain: %d of last 100 blocks above version %d\n", nUpgraded, (int)CBlock::CURRENT_VERSION);
        if (nUpgraded > 100 / 2) {
            // strMiscWarning is read by GetWarnings(), called by Qt and the JSON-RPC code to warn the user:
            strMiscWarning = _("Warning: This version is obsolete, upgrade required!");
            AlertNotify(strMiscWarning, true);
            fWarned = true;
        }
    }
}

/** Disconnect chainActive's tip. You probably want to call mempool.removeForReorg and manually re-limit mempool size after this, with cs_main held. */
bool static DisconnectTip(CValidationState& state)
{
    CBlockIndex* pindexDelete = chainActive.Tip();
    assert(pindexDelete);
    // Read block from disk.
    CBlock block;
    if (!ReadBlockFromDisk(block, pindexDelete))
        return AbortNode(state, "Failed to read block");
    // Apply the block atomically to the chain state.
    int64_t nStart = GetTimeMicros();
    {
        CCoinsViewCache view(pcoinsTip);
        if (DisconnectBlock(block, pindexDelete, view) != DISCONNECT_OK)
            return error("DisconnectTip() : DisconnectBlock %s failed", pindexDelete->GetBlockHash().ToString());
        assert(view.Flush());
    }
    LogPrint(BCLog::BENCH, "- Disconnect block: %.2fms\n", (GetTimeMicros() - nStart) * 0.001);
    // Write the chain state to disk, if necessary.
    if (!FlushStateToDisk(state, FLUSH_STATE_ALWAYS))
        return false;
    // Resurrect mempool transactions from the disconnected block.
    std::vector<uint256> vHashUpdate;
    for (const CTransaction& tx : block.vtx) {
        // ignore validation errors in resurrected transactions
        std::list<CTransaction> removed;
        CValidationState stateDummy;
        if (tx.IsCoinBase() || tx.IsCoinStake() || !AcceptToMemoryPool(mempool, stateDummy, tx, false, nullptr, true)) {
            mempool.remove(tx, removed, true);
        } else if (mempool.exists(tx.GetHash())) {
            vHashUpdate.push_back(tx.GetHash());
        }
    }
    // AcceptToMemoryPool/addUnchecked all assume that new mempool entries have
    // no in-mempool children, which is generally not true when adding
    // previously-confirmed transactions back to the mempool.
    // UpdateTransactionsFromBlock finds descendants of any transactions in this
    // block that were added back and cleans up the mempool state.
    mempool.UpdateTransactionsFromBlock(vHashUpdate);
    // Update chainActive and related variables.
    UpdateTip(pindexDelete->pprev);
    // Let wallets know transactions went from 1-confirmed to
    // 0-confirmed or conflicted:
    for (const CTransaction& tx : block.vtx) {
        GetMainSignals().SyncTransaction(tx, pindexDelete->pprev, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);
    }
    return true;
}


static int64_t nTimeReadFromDisk = 0;
static int64_t nTimeConnectTotal = 0;
static int64_t nTimeFlush = 0;
static int64_t nTimeChainState = 0;
static int64_t nTimePostConnect = 0;

/**
 * Connect a new block to chainActive. pblock is either NULL or a pointer to a CBlock
 * corresponding to pindexNew, to bypass loading it again from disk.
 */
bool static ConnectTip(CValidationState& state, CBlockIndex* pindexNew, const CBlock* pblock, bool fAlreadyChecked, std::list<CTransaction> &txConflicted, std::vector<std::tuple<CTransaction,CBlockIndex*,int>> &txChanged)
{
    assert(pindexNew->pprev == chainActive.Tip());

    if (pblock == NULL)
        fAlreadyChecked = false;

    // Read block from disk.
    int64_t nTime1 = GetTimeMicros();
    CBlock block;
    if (!pblock) {
        if (!ReadBlockFromDisk(block, pindexNew))
            return AbortNode(state, "Failed to read block");
        pblock = &block;
    }
    // Apply the block atomically to the chain state.
    int64_t nTime2 = GetTimeMicros();
    nTimeReadFromDisk += nTime2 - nTime1;
    int64_t nTime3;
    LogPrint(BCLog::BENCH, "  - Load block from disk: %.2fms [%.2fs]\n", (nTime2 - nTime1) * 0.001, nTimeReadFromDisk * 0.000001);
    {
        CCoinsViewCache view(pcoinsTip);
        bool rv = ConnectBlock(*pblock, state, pindexNew, view, false, fAlreadyChecked);
        GetMainSignals().BlockChecked(*pblock, state);
        if (!rv) {
            if (state.IsInvalid())
                InvalidBlockFound(pindexNew, state);
            return error("ConnectTip() : ConnectBlock %s failed", pindexNew->GetBlockHash().ToString());
        }
        ErasePeerBlockSource(pindexNew->GetBlockHash());
        nTime3 = GetTimeMicros();
        nTimeConnectTotal += nTime3 - nTime2;
        LogPrint(BCLog::BENCH, "  - Connect total: %.2fms [%.2fs]\n", (nTime3 - nTime2) * 0.001, nTimeConnectTotal * 0.000001);
        assert(view.Flush());
    }
    int64_t nTime4 = GetTimeMicros();
    nTimeFlush += nTime4 - nTime3;
    LogPrint(BCLog::BENCH, "  - Flush: %.2fms [%.2fs]\n", (nTime4 - nTime3) * 0.001, nTimeFlush * 0.000001);

    // Write the chain state to disk, if necessary. Always write to disk if this is the first of a new file.
    FlushStateMode flushMode = FLUSH_STATE_IF_NEEDED;
    if (pindexNew->pprev && (pindexNew->GetBlockPos().nFile != pindexNew->pprev->GetBlockPos().nFile))
        flushMode = FLUSH_STATE_ALWAYS;
    if (!FlushStateToDisk(state, flushMode))
        return false;
    int64_t nTime5 = GetTimeMicros();
    nTimeChainState += nTime5 - nTime4;
    LogPrint(BCLog::BENCH, "  - Writing chainstate: %.2fms [%.2fs]\n", (nTime5 - nTime4) * 0.001, nTimeChainState * 0.000001);

    // Remove conflicting transactions from the mempool.
    mempool.removeForBlock(pblock->vtx, pindexNew->nHeight, txConflicted, !IsInitialBlockDownload());
    // Update chainActive & related variables.
    UpdateTip(pindexNew);

    // Evict any stale or superseded Zero-Coin PoW registration from mempool so sentinel 0x01:0 never clogs
    {
        static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
        CTransaction staleZeroTx;
        bool fHasZeroTx = false;
        {
            LOCK(mempool.cs);
            auto itSentinel = mempool.mapNextTx.find(zeroCoinSentinel);
            if (itSentinel != mempool.mapNextTx.end() && itSentinel->second.ptx != nullptr) {
                staleZeroTx = *(itSentinel->second.ptx);
                fHasZeroTx = true;
            }
        }
        if (fHasZeroTx) {
            const Consensus::Params& consensus = Params().GetConsensus();
            const int nNextHeight = pindexNew->nHeight + 1;
            const size_t maxBootstrapPoolSize = (size_t)(consensus.GetAdamMinersCount(nNextHeight) + 2);
            const size_t nGateCount = GetFundedAdamMinerPoolCount(pindexNew->nHeight);
            bool fShouldEvict = (!consensus.IsAdamOpenEntryActive(nNextHeight) && nGateCount >= maxBootstrapPoolSize);
            if (!fShouldEvict) {
                std::vector<unsigned char> zNonce;
                uint256 zChallenge;
                CPubKey zPubKey;
                int64_t zLockTime = 0;
                CKeyID zPubKeyHash;
                CBLSPubKey zBlsPk;
                CBLSSignature zBlsPoP;
                const int nMinZeroPeriod = GetZeroCoinRegPeriod(nNextHeight);
                const int nMaxChallengeAge = ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2;
                std::string strBlsErr;
                if (staleZeroTx.vout.size() != 1 ||
                    !MatchPoWLockRegistration(staleZeroTx.vout[0].scriptPubKey, zNonce, zChallenge, zPubKey, zLockTime, zPubKeyHash, &zBlsPk, &zBlsPoP) ||
                    zLockTime < nNextHeight + nMinZeroPeriod ||
                    !CheckRegistrationBlsBinding(zPubKey, zBlsPk, zBlsPoP, nNextHeight, pindexNew, pcoinsTip, true, strBlsErr)) {
                    fShouldEvict = true;
                } else {
                    auto miChallenge = mapBlockIndex.find(zChallenge);
                    if (miChallenge == mapBlockIndex.end() || !miChallenge->second ||
                        !chainActive.Contains(miChallenge->second) ||
                        miChallenge->second->nHeight < nNextHeight - nMaxChallengeAge ||
                        miChallenge->second->nHeight >= nNextHeight) {
                        fShouldEvict = true;
                    } else {
                        const std::vector<CPubKey> activePool = GetAdamMinerPool(pindexNew->nHeight);
                        if (std::find(activePool.begin(), activePool.end(), zPubKey) != activePool.end()) {
                            fShouldEvict = true;
                        }
                    }
                }
            }
            if (fShouldEvict) {
                std::list<CTransaction> dummyRemoved;
                mempool.remove(staleZeroTx, dummyRemoved, true);
            }
        }
    }

    // Prune ADAM solutions cache and LLMQ quorum signatures cache (keep max 50 blocks or 500 items)
    int nPruneHeight = pindexNew->nHeight - 50;
    {
        LOCK(cs_adam_solutions);
        if (mapAdamSolutionsCache.size() > 50) {
            for (auto it = mapAdamSolutionsCache.begin(); it != mapAdamSolutionsCache.end();) {
                auto mi = mapBlockIndex.find(it->first);
                if (mi != mapBlockIndex.end() && mi->second && mi->second->nHeight < nPruneHeight) {
                    mapAdamSolutionFirstSeen.erase(it->first);
                    it = mapAdamSolutionsCache.erase(it);
                } else {
                    ++it;
                }
            }
            while (mapAdamSolutionsCache.size() > 500) {
                auto itEvict = mapAdamSolutionsCache.begin();
                if (itEvict != mapAdamSolutionsCache.end() && itEvict->first == pindexNew->GetBlockHash()) {
                    ++itEvict;
                }
                if (itEvict == mapAdamSolutionsCache.end()) break;
                mapAdamSolutionFirstSeen.erase(itEvict->first);
                mapAdamSolutionsCache.erase(itEvict);
            }
        }
        if (mapAdamSolutionFirstSeen.size() > 50) {
            for (auto it = mapAdamSolutionFirstSeen.begin(); it != mapAdamSolutionFirstSeen.end();) {
                auto mi = mapBlockIndex.find(it->first);
                if ((mi != mapBlockIndex.end() && mi->second && mi->second->nHeight < nPruneHeight) ||
                    (mapAdamSolutionsCache.find(it->first) == mapAdamSolutionsCache.end() && it->first != pindexNew->GetBlockHash())) {
                    it = mapAdamSolutionFirstSeen.erase(it);
                } else {
                    ++it;
                }
            }
            while (mapAdamSolutionFirstSeen.size() > 500) {
                auto itEvict = mapAdamSolutionFirstSeen.begin();
                if (itEvict != mapAdamSolutionFirstSeen.end() && itEvict->first == pindexNew->GetBlockHash()) {
                    ++itEvict;
                }
                if (itEvict == mapAdamSolutionFirstSeen.end()) break;
                mapAdamSolutionFirstSeen.erase(itEvict);
            }
        }
    }

    {
        LOCK(cs_quorum_sigs);
        if (mapQuorumBlockSigs.size() > 50) {
            for (auto it = mapQuorumBlockSigs.begin(); it != mapQuorumBlockSigs.end();) {
                auto mi = mapBlockIndex.find(it->first);
                if (mi != mapBlockIndex.end() && mi->second && mi->second->nHeight < nPruneHeight) {
                    it = mapQuorumBlockSigs.erase(it);
                } else {
                    ++it;
                }
            }
            while (mapQuorumBlockSigs.size() > 500) {
                mapQuorumBlockSigs.erase(mapQuorumBlockSigs.begin());
            }
        }
    }

    for(unsigned int i=0; i < pblock->vtx.size(); i++) {
        txChanged.emplace_back(pblock->vtx[i], pindexNew, i);
    }

    int64_t nTime6 = GetTimeMicros();
    nTimePostConnect += nTime6 - nTime5;
    nTimeTotal += nTime6 - nTime1;
    LogPrint(BCLog::BENCH, "  - Connect postprocess: %.2fms [%.2fs]\n", (nTime6 - nTime5) * 0.001, nTimePostConnect * 0.000001);
    LogPrint(BCLog::BENCH, "- Connect block: %.2fms [%.2fs]\n", (nTime6 - nTime1) * 0.001, nTimeTotal * 0.000001);
    return true;
}


bool DisconnectBlocks(int nBlocks)
{
    LOCK(cs_main);

    CValidationState state;

    LogPrintf("%s: Got command to replay %d blocks\n", __func__, nBlocks);
    for (int i = 0; i <= nBlocks; i++)
        DisconnectTip(state);

    return true;
}


void ReprocessBlocks(int nBlocks)
{
    {
        LOCK(cs_main);
        const int64_t nCutoff = GetTime() - (nBlocks * Params().GetConsensus().nTargetSpacing * 2);
        std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
        while (it != mapRejectedBlocks.end()) {
            //use a window twice as large as is usual for the nBlocks we want to reset
            if ((*it).second > nCutoff) {
                BlockMap::iterator mi = mapBlockIndex.find((*it).first);
                if (mi != mapBlockIndex.end() && (*mi).second) {
                    CBlockIndex* pindex = (*mi).second;
                    LogPrintf("%s - %s\n", __func__, (*it).first.ToString());

                    CValidationState state;
                    ReconsiderBlock(state, pindex);
                }
            }
            it = mapRejectedBlocks.erase(it);
        }
    }

    CValidationState state;
    {
        LOCK(cs_main);
        DisconnectBlocks(nBlocks);
    }

    if (state.IsValid()) {
        ActivateBestChain(state);
    }
}

/**
 * Return the tip of the chain with the most work in it, that isn't
 * known to be invalid (it's however far from certain to be valid).
 */
static CBlockIndex* FindMostWorkChain()
{
    do {
        CBlockIndex* pindexNew = NULL;

        // Find the best candidate header.
        {
            std::set<CBlockIndex*, CBlockIndexWorkComparator>::reverse_iterator it = setBlockIndexCandidates.rbegin();
            if (it == setBlockIndexCandidates.rend())
                return NULL;
            pindexNew = *it;
        }

        // Check whether all blocks on the path between the currently active chain and the candidate are valid.
        // Just going until the active chain is an optimization, as we know all blocks in it are valid already.
        CBlockIndex* pindexTest = pindexNew;
        bool fInvalidAncestor = false;
        while (pindexTest && !chainActive.Contains(pindexTest)) {
            assert(pindexTest->nChainTx || pindexTest->nHeight == 0);

            // Pruned nodes may have entries in setBlockIndexCandidates for
            // which block files have been deleted.  Remove those as candidates
            // for the most work chain if we come across them; we can't switch
            // to a chain unless we have all the non-active-chain parent blocks.
            bool fFailedChain = pindexTest->nStatus & BLOCK_FAILED_MASK;
            bool fMissingData = !(pindexTest->nStatus & BLOCK_HAVE_DATA);
            if (fFailedChain || fMissingData) {
                // Candidate chain is not usable (either invalid or missing data)
                if (fFailedChain && (pindexBestInvalid == NULL || pindexNew->nChainWork > pindexBestInvalid->nChainWork))
                    pindexBestInvalid = pindexNew;
                CBlockIndex* pindexFailed = pindexNew;
                // Remove the entire chain from the set.
                while (pindexTest != pindexFailed) {
                    if (fFailedChain) {
                        pindexFailed->nStatus |= BLOCK_FAILED_CHILD;
                    } else if (fMissingData) {
                        // If we're missing data, then add back to mapBlocksUnlinked,
                        // so that if the block arrives in the future we can try adding
                        // to setBlockIndexCandidates again.
                        mapBlocksUnlinked.insert(std::make_pair(pindexFailed->pprev, pindexFailed));
                    }
                    setBlockIndexCandidates.erase(pindexFailed);
                    pindexFailed = pindexFailed->pprev;
                }
                setBlockIndexCandidates.erase(pindexTest);
                fInvalidAncestor = true;
                break;
            }
            pindexTest = pindexTest->pprev;
        }
        if (!fInvalidAncestor)
            return pindexNew;
    } while (true);
}

/** Delete all entries in setBlockIndexCandidates that are worse than the current tip. */
static void PruneBlockIndexCandidates()
{
    // Note that we can't delete the current block itself, as we may need to return to it later in case a
    // reorganization to a better block fails.
    std::set<CBlockIndex*, CBlockIndexWorkComparator>::iterator it = setBlockIndexCandidates.begin();
    while (it != setBlockIndexCandidates.end() && setBlockIndexCandidates.value_comp()(*it, chainActive.Tip())) {
        setBlockIndexCandidates.erase(it++);
    }
    // Either the current tip or a successor of it we're working towards is left in setBlockIndexCandidates.
    assert(!setBlockIndexCandidates.empty());
}

/**
 * Try to make some progress towards making pindexMostWork the active block.
 * pblock is either NULL or a pointer to a CBlock corresponding to pindexMostWork.
 */
static bool ActivateBestChainStep(CValidationState& state, CBlockIndex* pindexMostWork, const CBlock* pblock, bool fAlreadyChecked, std::list<CTransaction>& txConflicted, std::vector<std::tuple<CTransaction,CBlockIndex*,int>>& txChanged)
{
    AssertLockHeld(cs_main);
    if (pblock == NULL)
        fAlreadyChecked = false;
    bool fInvalidFound = false;
    const CBlockIndex* pindexOldTip = chainActive.Tip();
    const CBlockIndex* pindexFork = chainActive.FindFork(pindexMostWork);

    // Disconnect active blocks which are no longer in the best chain.
    bool fBlocksDisconnected = false;
    while (chainActive.Tip() && chainActive.Tip() != pindexFork) {
        if (!DisconnectTip(state))
            return false;
        fBlocksDisconnected = true;
    }

    // Build list of new blocks to connect.
    std::vector<CBlockIndex*> vpindexToConnect;
    bool fContinue = true;
    int nHeight = pindexFork ? pindexFork->nHeight : -1;
    while (fContinue && nHeight != pindexMostWork->nHeight) {
        // Don't iterate the entire list of potential improvements toward the best tip, as we likely only need
        // a few blocks along the way.
        int nTargetHeight = std::min(nHeight + 32, pindexMostWork->nHeight);
        vpindexToConnect.clear();
        vpindexToConnect.reserve(nTargetHeight - nHeight);
        CBlockIndex* pindexIter = pindexMostWork->GetAncestor(nTargetHeight);
        while (pindexIter && pindexIter->nHeight != nHeight) {
            vpindexToConnect.push_back(pindexIter);
            pindexIter = pindexIter->pprev;
        }
        nHeight = nTargetHeight;

        // Connect new blocks.
        BOOST_REVERSE_FOREACH (CBlockIndex* pindexConnect, vpindexToConnect) {
            if (!ConnectTip(state, pindexConnect, pindexConnect == pindexMostWork ? pblock : NULL, pindexConnect == pindexMostWork ? fAlreadyChecked : false, txConflicted, txChanged)) {
                if (state.IsInvalid()) {
                    // The block violates a consensus rule.
                    if (!state.CorruptionPossible())
                        InvalidChainFound(vpindexToConnect.back());
                    state = CValidationState();
                    fInvalidFound = true;
                    fContinue = false;
                    break;
                } else {
                    // A system error occurred (disk space, database error, ...).
                    return false;
                }
            } else {
                PruneBlockIndexCandidates();
                if (!pindexOldTip || chainActive.Tip()->nChainWork > pindexOldTip->nChainWork) {
                    // We're in a better position than we were. Return temporarily to release the lock.
                    fContinue = false;
                    break;
                }
            }
        }
    }

    if (fBlocksDisconnected) {
        mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
        LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);
    }
    mempool.check(pcoinsTip);

    // Callbacks/notifications for a new best chain.
    if (fInvalidFound)
        CheckForkWarningConditionsOnNewFork(vpindexToConnect.back());
    else
        CheckForkWarningConditions();

    return true;
}


/**
 * Make the best chain active, in multiple steps. The result is either failure
 * or an activated best chain. pblock is either NULL or a pointer to a block
 * that is already loaded (to avoid loading it again from disk).
 */
bool ActivateBestChain(CValidationState& state, const CBlock* pblock, bool fAlreadyChecked, CConnman* connman)
{
    // Note that while we're often called here from ProcessNewBlock, this is
    // far from a guarantee. Things in the P2P/RPC will often end up calling
    // us in the middle of ProcessNewBlock - do not assume pblock is set
    // sanely for performance or correctness!
    AssertLockNotHeld(cs_main);

    CBlockIndex* pindexNewTip = nullptr;
    CBlockIndex* pindexMostWork = nullptr;
    std::vector<std::tuple<CTransaction,CBlockIndex*,int>> txChanged;
    if (pblock)
        txChanged.reserve(pblock->vtx.size());
    do {
        txChanged.clear();
        boost::this_thread::interruption_point();

        const CBlockIndex *pindexFork;
        std::list<CTransaction> txConflicted;
        bool fInitialDownload;
        while (true) {
            TRY_LOCK(cs_main, lockMain);
            if (!lockMain) {
                MilliSleep(50);
                continue;
            }

            CBlockIndex *pindexOldTip = chainActive.Tip();
            pindexMostWork = FindMostWorkChain();

            // Whether we have anything to do at all.
            if (pindexMostWork == NULL || pindexMostWork == chainActive.Tip())
                return true;

            if (!ActivateBestChainStep(state, pindexMostWork, pblock && pblock->GetHash() == pindexMostWork->GetBlockHash() ? pblock : NULL, fAlreadyChecked, txConflicted, txChanged))
                return false;

            pindexNewTip = chainActive.Tip();
            pindexFork = chainActive.FindFork(pindexOldTip);
            fInitialDownload = IsInitialBlockDownload();

            // throw all transactions though the signal-interface
            for (const CTransaction &tx : txConflicted) {
                GetMainSignals().SyncTransaction(tx, pindexNewTip, CMainSignals::SYNC_TRANSACTION_NOT_IN_BLOCK);
            }
            // ... and about transactions that got confirmed:
            for(unsigned int i = 0; i < txChanged.size(); i++) {
                GetMainSignals().SyncTransaction(std::get<0>(txChanged[i]), std::get<1>(txChanged[i]), std::get<2>(txChanged[i]));
            }

            break;
        }

        // When we reach this point, we switched to a new tip (stored in pindexNewTip).
        // Notifications/callbacks that can run without cs_main
        if(connman)
            connman->SetBestHeight(pindexNewTip->nHeight);

        // Always notify the UI if a new block tip was connected
        if (pindexFork != pindexNewTip) {

            // Notify the UI
            uiInterface.NotifyBlockTip(fInitialDownload, pindexNewTip);

            // Notifications/callbacks that can run without cs_main
            if (!fInitialDownload) {
                uint256 hashNewTip = pindexNewTip->GetBlockHash();
                // Relay inventory, but don't relay old inventory during initial block download.
                int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
                {
                    if (connman) {
                        connman->ForEachNode([pindexNewTip, nBlockEstimate, hashNewTip](CNode* pnode) {
                            if (pindexNewTip->nHeight > (pnode->nStartingHeight != -1 ? pnode->nStartingHeight - 2000 : nBlockEstimate)) {
                                pnode->PushInventory(CInv(MSG_BLOCK, hashNewTip));
                            }
                        });
                    }
                }
                // Notify external listeners about the new tip.
                GetMainSignals().UpdatedBlockTip(pindexNewTip);

                unsigned size = 0;
                if (pblock)
                    size = GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION);
                // If the size is over 1 MB notify external listeners, and it is within the last 5 minutes
                if (size > MAX_BLOCK_SIZE_LEGACY && pblock->GetBlockTime() > GetAdjustedTime() - 300) {
                    uiInterface.NotifyBlockSize(static_cast<int>(size), hashNewTip);
                }
            }

        }
    } while (pindexMostWork != chainActive.Tip());
    CheckBlockIndex();

    // Write changes periodically to disk, after relay.
    if (!FlushStateToDisk(state, FLUSH_STATE_PERIODIC)) {
        return false;
    }

    return true;
}


bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    // Mark the block itself as invalid.
    pindex->nStatus |= BLOCK_FAILED_VALID;
    setDirtyBlockIndex.insert(pindex);
    setBlockIndexCandidates.erase(pindex);

    while (chainActive.Contains(pindex)) {
        CBlockIndex* pindexWalk = chainActive.Tip();
        pindexWalk->nStatus |= BLOCK_FAILED_CHILD;
        setDirtyBlockIndex.insert(pindexWalk);
        setBlockIndexCandidates.erase(pindexWalk);
        // ActivateBestChain considers blocks already in chainActive
        // unconditionally valid already, so force disconnect away from it.
        if (!DisconnectTip(state)) {
            mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);
            return false;
        }
    }

    LimitMempoolSize(mempool, GetArg("-maxmempool", DEFAULT_MAX_MEMPOOL_SIZE) * 1000000, GetArg("-mempoolexpiry", DEFAULT_MEMPOOL_EXPIRY) * 60 * 60);

    // The resulting new best tip may not be in setBlockIndexCandidates anymore, so
    // add it again.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && !setBlockIndexCandidates.value_comp()(it->second, chainActive.Tip())) {
            setBlockIndexCandidates.insert(it->second);
        }
        it++;
    }

    InvalidChainFound(pindex);
    mempool.removeForReorg(pcoinsTip, chainActive.Tip()->nHeight + 1, STANDARD_LOCKTIME_VERIFY_FLAGS);

    // If pindexBestHeader was on the invalidated chain, recalculate it
    if (pindexBestHeader && (!pindexBestHeader->IsValid(BLOCK_VALID_TREE) || (pindexBestHeader->nStatus & BLOCK_FAILED_MASK))) {
        pindexBestHeader = chainActive.Tip();
        for (const auto& item : mapBlockIndex) {
            CBlockIndex* pindexH = item.second;
            if (pindexH && pindexH->IsValid(BLOCK_VALID_TREE) && !(pindexH->nStatus & BLOCK_FAILED_MASK)) {
                if (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindexH)) {
                    pindexBestHeader = pindexH;
                }
            }
        }
    }
    return true;
}


bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex)
{
    AssertLockHeld(cs_main);

    int nHeight = pindex->nHeight;

    // Remove the invalidity flag from this block and all its descendants.
    BlockMap::iterator it = mapBlockIndex.begin();
    while (it != mapBlockIndex.end()) {
        if (!it->second->IsValid() && it->second->GetAncestor(nHeight) == pindex) {
            it->second->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(it->second);
            if (it->second->IsValid(BLOCK_VALID_TRANSACTIONS) && it->second->nChainTx && setBlockIndexCandidates.value_comp()(chainActive.Tip(), it->second)) {
                setBlockIndexCandidates.insert(it->second);
            }
            if (it->second == pindexBestInvalid) {
                // Reset invalid block marker if it was pointing to one of those.
                pindexBestInvalid = NULL;
            }
        }
        it++;
    }

    // Remove the invalidity flag from all ancestors too.
    while (pindex != NULL) {
        if (pindex->nStatus & BLOCK_FAILED_MASK) {
            pindex->nStatus &= ~BLOCK_FAILED_MASK;
            setDirtyBlockIndex.insert(pindex);
        }
        pindex = pindex->pprev;
    }
    return true;
}


CBlockIndex* AddToBlockIndex(const CBlock& block)
{
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator it = mapBlockIndex.find(hash);
    if (it != mapBlockIndex.end())
        return it->second;

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(block);
    assert(pindexNew);
    // We assign the sequence id to blocks only when the full data is available,
    // to avoid miners withholding blocks but broadcasting headers, to get a
    // competitive advantage.
    pindexNew->nSequenceId = 0;
    BlockMap::iterator mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);
    BlockMap::iterator miPrev = mapBlockIndex.find(block.hashPrevBlock);
    if (miPrev != mapBlockIndex.end()) {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
        pindexNew->BuildSkip();

        // Compute and set V2 stake modifier when parent modifier is ready.
        if (pindexNew->nHeight > 0 && !block.vtx.empty()) {
            pindexNew->hashStakeModifierPrevout = GetV2StakeModifierPrevoutId(block);
            if (CanComputeV2StakeModifier(pindexNew)) {
                pindexNew->SetNewStakeModifier(pindexNew->hashStakeModifierPrevout);
            }
        }
    }
    pindexNew->nChainWork = (pindexNew->pprev ? pindexNew->pprev->nChainWork : 0) + GetBlockProof(*pindexNew);
    pindexNew->RaiseValidity(BLOCK_VALID_TREE);
    if ((pindexBestHeader == NULL || pindexBestHeader->nChainWork < pindexNew->nChainWork) &&
        pindexNew->IsValid(BLOCK_VALID_TREE) && !(pindexNew->nStatus & BLOCK_FAILED_MASK))
        pindexBestHeader = pindexNew;

    setDirtyBlockIndex.insert(pindexNew);
    ProcessOrphanAdamSolutions(hash);

    return pindexNew;
}

/** Mark a block as having its data received and checked (up to BLOCK_VALID_TRANSACTIONS). */
bool ReceivedBlockTransactions(const CBlock& block, CValidationState& state, CBlockIndex* pindexNew, const CDiskBlockPos& pos)
{
    if (block.IsProofOfStake())
        pindexNew->SetProofOfStake();
    pindexNew->nTx = block.vtx.size();
    pindexNew->nChainTx = 0;
    pindexNew->nFile = pos.nFile;
    pindexNew->nDataPos = pos.nPos;
    pindexNew->nUndoPos = 0;
    pindexNew->nStatus |= BLOCK_HAVE_DATA;
    if (pindexNew->nHeight > 0 && !block.vtx.empty()) {
        pindexNew->hashStakeModifierPrevout = GetV2StakeModifierPrevoutId(block);
        if (CanComputeV2StakeModifier(pindexNew)) {
            pindexNew->SetNewStakeModifier(pindexNew->hashStakeModifierPrevout);
            setDirtyBlockIndex.insert(pindexNew);
        }
    }
    pindexNew->RaiseValidity(BLOCK_VALID_TRANSACTIONS);
    setDirtyBlockIndex.insert(pindexNew);

    if (pindexNew->pprev == NULL || pindexNew->pprev->nChainTx) {
        // If pindexNew is the genesis block or all parents are BLOCK_VALID_TRANSACTIONS.
        std::deque<CBlockIndex*> queue;
        queue.push_back(pindexNew);

        // Recursively process any descendant blocks that now may be eligible to be connected.
        while (!queue.empty()) {
            CBlockIndex* pindex = queue.front();
            queue.pop_front();
            if (pindex->nHeight > 0 && CanComputeV2StakeModifier(pindex) &&
                (!pindex->GeneratedStakeModifier() || pindex->GetStakeModifierV2().IsNull())) {
                uint256 prevoutId = pindex->hashStakeModifierPrevout;
                if (prevoutId.IsNull()) {
                    CBlock childBlock;
                    if (ReadBlockFromDisk(childBlock, pindex) && !childBlock.vtx.empty()) {
                        prevoutId = GetV2StakeModifierPrevoutId(childBlock);
                        pindex->hashStakeModifierPrevout = prevoutId;
                    }
                }
                if (!prevoutId.IsNull()) {
                    pindex->SetNewStakeModifier(prevoutId);
                    setDirtyBlockIndex.insert(pindex);
                }
            }
            pindex->nChainTx = (pindex->pprev ? pindex->pprev->nChainTx : 0) + pindex->nTx;
            {
                LOCK(cs_nBlockSequenceId);
                pindex->nSequenceId = nBlockSequenceId++;
            }
            if (chainActive.Tip() == NULL || !setBlockIndexCandidates.value_comp()(pindex, chainActive.Tip())) {
                setBlockIndexCandidates.insert(pindex);
            }
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = mapBlocksUnlinked.equal_range(pindex);
            while (range.first != range.second) {
                std::multimap<CBlockIndex*, CBlockIndex*>::iterator it = range.first;
                queue.push_back(it->second);
                range.first++;
                mapBlocksUnlinked.erase(it);
            }
        }
    } else {
        if (pindexNew->pprev && pindexNew->pprev->IsValid(BLOCK_VALID_TREE)) {
            mapBlocksUnlinked.insert(std::make_pair(pindexNew->pprev, pindexNew));
        }
    }

    return true;
}


bool FindBlockPos(CValidationState& state, CDiskBlockPos& pos, unsigned int nAddSize, unsigned int nHeight, uint64_t nTime, bool fKnown = false)
{
    LOCK(cs_LastBlockFile);

    unsigned int nFile = fKnown ? pos.nFile : nLastBlockFile;
    if (vinfoBlockFile.size() <= nFile) {
        vinfoBlockFile.resize(nFile + 1);
    }

    if (!fKnown) {
        while (vinfoBlockFile[nFile].nSize + nAddSize >= MAX_BLOCKFILE_SIZE) {
            LogPrintf("Leaving block file %i: %s\n", nFile, vinfoBlockFile[nFile].ToString());
            FlushBlockFile(true);
            nFile++;
            if (vinfoBlockFile.size() <= nFile) {
                vinfoBlockFile.resize(nFile + 1);
            }
        }
        pos.nFile = nFile;
        pos.nPos = vinfoBlockFile[nFile].nSize;
    }

    nLastBlockFile = nFile;
    vinfoBlockFile[nFile].AddBlock(nHeight, nTime);
    if (fKnown)
        vinfoBlockFile[nFile].nSize = std::max(pos.nPos + nAddSize, vinfoBlockFile[nFile].nSize);
    else
        vinfoBlockFile[nFile].nSize += nAddSize;

    if (!fKnown) {
        unsigned int nOldChunks = (pos.nPos + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        unsigned int nNewChunks = (vinfoBlockFile[nFile].nSize + BLOCKFILE_CHUNK_SIZE - 1) / BLOCKFILE_CHUNK_SIZE;
        if (nNewChunks > nOldChunks) {
            if (CheckDiskSpace(nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos)) {
                FILE* file = OpenBlockFile(pos);
                if (file) {
                    LogPrintf("Pre-allocating up to position 0x%x in blk%05u.dat\n", nNewChunks * BLOCKFILE_CHUNK_SIZE, pos.nFile);
                    AllocateFileRange(file, pos.nPos, nNewChunks * BLOCKFILE_CHUNK_SIZE - pos.nPos);
                    fclose(file);
                }
            } else
                return state.Error("out of disk space");
        }
    }

    setDirtyFileInfo.insert(nFile);
    return true;
}


bool FindUndoPos(CValidationState& state, int nFile, CDiskBlockPos& pos, unsigned int nAddSize)
{
    pos.nFile = nFile;

    LOCK(cs_LastBlockFile);

    unsigned int nNewSize;
    pos.nPos = vinfoBlockFile[nFile].nUndoSize;
    nNewSize = vinfoBlockFile[nFile].nUndoSize += nAddSize;
    setDirtyFileInfo.insert(nFile);

    unsigned int nOldChunks = (pos.nPos + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    unsigned int nNewChunks = (nNewSize + UNDOFILE_CHUNK_SIZE - 1) / UNDOFILE_CHUNK_SIZE;
    if (nNewChunks > nOldChunks) {
        if (CheckDiskSpace(nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos)) {
            FILE* file = OpenUndoFile(pos);
            if (file) {
                LogPrintf("Pre-allocating up to position 0x%x in rev%05u.dat\n", nNewChunks * UNDOFILE_CHUNK_SIZE, pos.nFile);
                AllocateFileRange(file, pos.nPos, nNewChunks * UNDOFILE_CHUNK_SIZE - pos.nPos);
                fclose(file);
            }
        } else
            return state.Error("out of disk space");
    }

    return true;
}


bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW)
{
    if (block.nVersion >= 11) {
        if (block.vAdamMiners.size() > 32 || block.vAdamSolutions.size() > 32 ||
            block.vAdamVRFProof.size() > 512 || block.vAdamCoordinatorSig.size() > 512 ||
            block.vQuorumSig.size() > 4096) {
            return state.DoS(100, false, REJECT_INVALID, "bad-adam-header-limits", false, "ADAM header field size limit exceeded");
        }
        for (const auto& miner : block.vAdamMiners) {
            if (!miner.IsValid()) {
                return state.DoS(100, false, REJECT_INVALID, "bad-adam-miner-pubkey", false, "invalid miner public key in ADAM header");
            }
        }
        for (const auto& sol : block.vAdamSolutions) {
            if (sol.size() > 512) {
                return state.DoS(100, false, REJECT_INVALID, "bad-adam-header-limits", false, "ADAM solution size limit exceeded");
            }
        }
        fCheckPOW = false;
    }
    // Check proof of work matches claimed amount
    if (fCheckPOW && !CheckProofOfWork(block.GetHash(), block.nBits))
        return state.DoS(50, false, REJECT_INVALID, "high-hash", false, "proof of work failed");

    return true;
}


bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig, const CCoinsViewCache* pView)
{
    if (block.fChecked)
        return true;

    bool fOfflineSync = (IsInitialBlockDownload() || fReindex || (fVerifyingBlocks && !pView) || (!g_fMasternodePingStateInitialized && !Params().IsRegTestNet()));

    // These are checks that are independent of context.
    const bool IsPoS = block.IsProofOfStake();

    // Check that the header is valid (particularly PoW).  This is mostly
    // redundant with the call in AcceptBlockHeader.
    if (!CheckBlockHeader(block, state, !IsPoS && fCheckPOW))
        return false;

    const uint256 hashBlock = block.GetHash();

    // All potential-corruption validation must be done before we do any
    // transaction validation, as otherwise we may mark the header as invalid
    // because we receive the wrong transactions for it.

    // Check the merkle root.
    if (fCheckMerkleRoot) {
        bool mutated;
        uint256 hashMerkleRoot2 = BlockMerkleRoot(block, &mutated);
        if (block.hashMerkleRoot != hashMerkleRoot2)
            return state.DoS(100, false, REJECT_INVALID, "bad-txnmrklroot", true, "hashMerkleRoot mismatch");

        // Check for merkle tree malleability (CVE-2012-2459): repeating sequences
        // of transactions in a block without affecting the merkle root of a block,
        // while still invalidating it.
        if (mutated)
            return state.DoS(100, false, REJECT_INVALID, "bad-txns-duplicate", true, "duplicate transaction");
    }

    // Size limits
    unsigned int nMaxBlockSize = MAX_BLOCK_SIZE_CURRENT;
    if (block.vtx.empty() || block.vtx.size() > nMaxBlockSize || ::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > nMaxBlockSize)
        return state.DoS(100, false, REJECT_INVALID, "bad-blk-length", false, "size limits failed");

    // First transaction must be coinbase, the rest must not be
    if (block.vtx.empty() || !block.vtx[0].IsCoinBase())
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-missing", false, "first tx is not coinbase");
    for (unsigned int i = 1; i < block.vtx.size(); i++)
        if (block.vtx[i].IsCoinBase())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-multiple", false, "more than one coinbase");

    // masternode payments / budgets (resolve parent/height early for PoW coinstake check & DIP3 state)
    bool fBlockOfflineSync = fOfflineSync;
    bool fParentIsActiveTip = false;
    bool fHasDip3List = false;
    CBlockIndex* pindexPrev = nullptr;
    int nHeight = 0;
    {
        LOCK(cs_main);
        if (chainActive.Tip() != NULL && chainActive.Tip()->GetBlockHash() == block.hashPrevBlock) {
            pindexPrev = chainActive.Tip();
            nHeight = pindexPrev->nHeight + 1;
            fParentIsActiveTip = (g_fMasternodePingStateInitialized || Params().IsRegTestNet());
        } else { //out of order
            BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
            if (mi != mapBlockIndex.end() && (*mi).second) {
                pindexPrev = (*mi).second;
                nHeight = pindexPrev->nHeight + 1;
                if (pView && pView->GetBestBlock() == block.hashPrevBlock && chainActive.Contains(pindexPrev)) {
                    fParentIsActiveTip = (g_fMasternodePingStateInitialized || Params().IsRegTestNet());
                }
            }
        }
        if (pindexPrev != NULL) {
            fBlockOfflineSync = fOfflineSync || fVerifyingBlocks || fReindex || (pindexBestHeader && chainActive.Height() + 6 < pindexBestHeader->nHeight);
            fHasDip3List = deterministicMNManager &&
                           (pindexPrev->nHeight == 0 || deterministicMNManager->HasListForBlock(pindexPrev->GetBlockHash()));
            if (fHasDip3List && !fVerifyingBlocks && !fReindex) {
                fBlockOfflineSync = false;
            }
            if (pView != nullptr && !fVerifyingBlocks && (g_fMasternodePingStateInitialized || Params().IsRegTestNet())) {
                fBlockOfflineSync = false;
            }
            if (pView == nullptr && !Params().IsRegTestNet()) {
                // Only force offline sync on non-tip blocks when deterministic DIP3 state is unavailable for pindexPrev.
                if (!fParentIsActiveTip && !fHasDip3List) {
                    fBlockOfflineSync = true;
                }
                if (deterministicMNManager &&
                    pindexPrev->nHeight > 0 && !deterministicMNManager->HasListForBlock(pindexPrev->GetBlockHash())) {
                    fBlockOfflineSync = true;
                }
            }
        }
    }

    if (IsPoS) {
        // Coinbase output should be empty if proof-of-stake block
        if (block.vtx[0].vout.size() != 1 || !block.vtx[0].vout[0].IsEmpty())
            return state.DoS(100, false, REJECT_INVALID, "bad-cb-pos", false, "coinbase output not empty for proof-of-stake block");

        // Second transaction must be coinstake, the rest must not be
        if (block.vtx.size() < 2 || !block.vtx[1].IsCoinStake())
            return state.DoS(100, false, REJECT_INVALID, "bad-cs-missing", false, "second tx is not coinstake");
        for (unsigned int i = 2; i < block.vtx.size(); i++)
            if (block.vtx[i].IsCoinStake())
                return state.DoS(100, false, REJECT_INVALID, "bad-cs-multiple", false, "more than one coinstake");
    } else {
        for (unsigned int i = 1; i < block.vtx.size(); i++) {
            if (block.vtx[i].IsCoinStake()) {
                return state.DoS(100, false, REJECT_INVALID, "bad-cs-in-pow", false, "coinstake transaction in proof-of-work block");
            }
        }
    }
    std::map<CPubKey, CBLSPubKey> mapBlockBlsKeys;
    std::map<CBLSPubKey, CPubKey> mapBlockBlsOwners;
    auto checkIntraBlockBlsConsistency = [&](const CPubKey& pk, const CBLSPubKey& blsPk) -> bool {
        if (!pk.IsValid() || !blsPk.IsValid()) return true;
        auto itOwner = mapBlockBlsOwners.find(blsPk);
        if (itOwner == mapBlockBlsOwners.end()) {
            mapBlockBlsOwners.emplace(blsPk, pk);
        } else if (itOwner->second != pk) {
            return false;
        }
        auto it = mapBlockBlsKeys.find(pk);
        if (it == mapBlockBlsKeys.end()) {
            mapBlockBlsKeys.emplace(pk, blsPk);
            return true;
        }
        return it->second == blsPk;
    };

    if (pindexPrev != NULL) {

        if (Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL)) {
            const Consensus::Params& consensus = Params().GetConsensus();
            if (block.nVersion != 12) {
                return state.DoS(100, false, REJECT_INVALID, "bad-version", false, "block version must be 12 for MPA consensus");
            }

            if (fCheckSig && IsModelDActive(nHeight) && !Params().IsRegTestNet() && nHeight > consensus.nAdamQuorumEnforcementHeight && block.vQuorumSig.empty()) {
                return state.DoS(100, false, REJECT_INVALID, "missing-quorum-sig", false, "missing LLMQ quorum signature");
            }

            if (fCheckSig && !block.vQuorumSig.empty()) {
                llmq::CQuorumSignature qsigCheck;
                try {
                    CDataStream ss(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
                    ss >> qsigCheck;
                    if (!ss.empty()) {
                        return state.DoS(100, false, REJECT_INVALID, "bad-quorum-sig-format", false, "trailing bytes in LLMQ quorum signature");
                    }
                } catch (...) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-quorum-sig-format", false, "failed to deserialize LLMQ quorum signature");
                }
                if (qsigCheck.blockHash != hashBlock) {
                    return state.DoS(0, false, REJECT_INVALID, "bad-quorum-sig-hash", false, "LLMQ quorum signature block hash mismatch");
                }
            }

            if (fCheckSig && !fBlockOfflineSync) {
                // Validate LLMQ Quorum Signature for Version 12 blocks
                int threshold = consensus.GetAdamThreshold(nHeight);
                bool fFallbackMode = (block.nVersion == 11) || !IsModelDActive(nHeight);
                if (!fFallbackMode) {
                    llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev, pView);
                    if (!quorum.members.empty()) {
                        if (block.vQuorumSig.empty()) {
                            return state.DoS(100, false, REJECT_INVALID, "missing-quorum-sig", false, "missing LLMQ quorum signature");
                        }
                        llmq::CQuorumSignature qsig;
                        try {
                            CDataStream ss(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
                            ss >> qsig;
                        } catch (...) {
                            return state.DoS(100, false, REJECT_INVALID, "bad-quorum-sig-format", false, "failed to deserialize LLMQ quorum signature");
                        }

                        if (qsig.blockHash != hashBlock) {
                            return state.DoS(0, false, REJECT_INVALID, "bad-quorum-sig-hash", false, "LLMQ quorum signature block hash mismatch");
                        }

                        if (!qsig.Verify(quorum, pindexPrev, BLSDomain::LLMQ_BLOCK, nHeight)) {
                            const int nQuorumRejectDoS = (fHasDip3List && !fBlockOfflineSync) ? 100 : 0;
                            return state.DoS(nQuorumRejectDoS, false, REJECT_INVALID, "bad-quorum-sig-verify", false, "LLMQ quorum signature verification failed");
                        }
                        for (const auto& sigPair : qsig.signatures) {
                            for (const auto& member : quorum.members) {
                                if (member.collateralOutpoint == sigPair.first && member.pubKeyMasternode.IsValid()) {
                                    CBLSPubKey qBlsPk;
                                    if (ExtractBLSPubKeyFromSignedData(sigPair.second, qBlsPk) &&
                                        !checkIntraBlockBlsConsistency(member.pubKeyMasternode, qBlsPk)) {
                                        return state.DoS(100, false, REJECT_INVALID, "bad-intra-block-bls-key", false, "conflicting BLS public keys for same secp256k1 identity within block");
                                    }
                                    break;
                                }
                            }
                        }
                    } else {
                        // If there are no masternodes active yet (e.g. at the start of regtest),
                        // allow block without quorum signature to avoid getting stuck.
                        if (!Params().IsRegTestNet() && nHeight > consensus.nAdamQuorumEnforcementHeight) {
                            return state.DoS(100, false, REJECT_INVALID, "empty-quorum-members", false, "LLMQ elected quorum is empty");
                        }
                    }
                }
            }
        }

        // KristaTech
        // It is entierly possible that we don't have enough data and this could fail
        // (i.e. the block could indeed be valid). Store the block for later consideration
        // but issue an initial reject message.
        // The case also exists that the sending peer could not have enough data to see
        // that this block is invalid, so don't issue an outright ban.
        if (nHeight != 0 && !fBlockOfflineSync && (pView != nullptr || fParentIsActiveTip || Params().IsRegTestNet())) {
            // check masternode payment
            if (!IsBlockPayeeValid(block, nHeight, pView)) {
                {
                    LOCK(cs_main);
                    if (mapRejectedBlocks.size() >= 512 && !mapRejectedBlocks.count(hashBlock)) {
                        mapRejectedBlocks.erase(mapRejectedBlocks.begin());
                    }
                    mapRejectedBlocks.insert(std::make_pair(hashBlock, GetTime()));
                }
                return state.DoS(0, false, REJECT_INVALID, "bad-cb-payee", false, "Couldn't find masternode payment");
            }
        } else {
            LogPrintf("%s: Masternode payment checks skipped on sync\n", __func__);
        }
    }

    // Check transactions
    int nZeroCoinPoWTxCount = 0;
    int nCoinLockTxCount = 0;
    int nPoWLockTxCount = 0;
    const bool fDualTrackRules = (nHeight >= 0 && IsAdamDualTrackActive(nHeight));
    std::map<CBLSPubKey, CPubKey> seenBlockRegBls;
    auto checkRegBlsInBlock = [&](const CPubKey& regPubKey, const CBLSPubKey& regBlsPk, const CBLSSignature& regBlsPoP) -> bool {
        std::string strBlsErr;
        if (!CheckRegistrationBlsBinding(regPubKey, regBlsPk, regBlsPoP, nHeight, nullptr, nullptr, false, strBlsErr)) {
            return state.DoS(100, false, REJECT_INVALID, strBlsErr, false, "block registration BLS binding failed");
        }
        if (regBlsPk.IsValid()) {
            auto itSeen = seenBlockRegBls.find(regBlsPk);
            if (itSeen != seenBlockRegBls.end() && itSeen->second != regPubKey) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-minerreg-dup-bls", false, "duplicate BLS pubkey across block registrations");
            }
            seenBlockRegBls.emplace(regBlsPk, regPubKey);
        }
        return true;
    };
    for (const CTransaction& tx : block.vtx) {
        if (!CheckTransaction(
                tx,
                state
        ))
            return state.Invalid(false, state.GetRejectCode(), state.GetRejectReason(),
                                             strprintf("Transaction check failed (tx hash %s) %s", tx.GetHash().ToString(), state.GetDebugMessage()));

        if (IsZeroCoinPoWTransaction(tx)) {
            nZeroCoinPoWTxCount++;
            if (nZeroCoinPoWTxCount > 1) {
                return state.DoS(100, false, REJECT_INVALID, "bad-blk-too-many-zerocoin-pow", false, "more than one zero-coin PoW registration per block");
            }
        }

        int nCoinLockOutputsInTx = 0;
        int nPoWLockOutputsInTx = 0;
        const bool fZeroCoinTx = IsZeroCoinPoWTransaction(tx);
        for (const CTxOut& txout : tx.vout) {
            std::vector<unsigned char> vchNonce;
            uint256 hashChallenge;
            CPubKey regPubKey;
            int64_t nRegLockTime = 0;
            CKeyID regKeyId;
            CBLSPubKey regBlsPk;
            CBLSSignature regBlsPoP;
            if (MatchCoinLockRegistration(txout.scriptPubKey, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                if (fDualTrackRules && txout.nValue < MINER_REGISTRATION_LOCK_AMOUNT) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-coinlock-amount-too-small", false, "Coin-Lock registration output below minimum amount");
                }
                if (!checkRegBlsInBlock(regPubKey, regBlsPk, regBlsPoP)) {
                    return false;
                }
                nCoinLockOutputsInTx++;
            } else if (MatchPoWLockRegistration(txout.scriptPubKey, vchNonce, hashChallenge, regPubKey, nRegLockTime, regKeyId, &regBlsPk, &regBlsPoP)) {
                if (fDualTrackRules && !fZeroCoinTx && txout.nValue < POW_LOCK_REGISTRATION_AMOUNT) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-txns-powlock-amount-too-small", false, "funded PoW-Lock registration output below minimum amount");
                }
                if (!checkRegBlsInBlock(regPubKey, regBlsPk, regBlsPoP)) {
                    return false;
                }
                nPoWLockOutputsInTx++;
            }
        }
        if (fDualTrackRules) {
            if (nCoinLockOutputsInTx > 1) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-too-many-coinlock", false, "more than one Coin-Lock registration output in a transaction");
            }
            if (nCoinLockOutputsInTx > 0) {
                nCoinLockTxCount++;
                if (nCoinLockTxCount > ADAM_MAX_COIN_LOCK_TX_PER_BLOCK) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-blk-too-many-coinlock", false, "too many Coin-Lock registrations in a single block");
                }
            }
            if (nPoWLockOutputsInTx > 1) {
                return state.DoS(100, false, REJECT_INVALID, "bad-txns-too-many-powlock", false, "more than one PoW-Lock registration output in a transaction");
            }
            if (!fZeroCoinTx && nPoWLockOutputsInTx > 0) {
                nPoWLockTxCount++;
                if (nPoWLockTxCount > ADAM_MAX_POW_LOCK_TX_PER_BLOCK) {
                    return state.DoS(100, false, REJECT_INVALID, "bad-blk-too-many-powlock", false, "too many PoW-Lock registrations in a single block");
                }
            }
        }
    }

    unsigned int nSigOps = 0;
    for (const CTransaction& tx : block.vtx) {
        nSigOps += GetLegacySigOpCount(tx);
    }
    unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_LEGACY;
    if (nSigOps > nMaxBlockSigOps)
        return state.DoS(100, error("%s : out-of-bounds SigOpCount", __func__),
            REJECT_INVALID, "bad-blk-sigops", true);

    // ADAM consensus checks
    int nAdamActualHeight = nHeight;
    if (nAdamActualHeight == 0 && !block.vtx.empty() && !block.vtx[0].vin.empty()) {
        const CScript& scriptSig = block.vtx[0].vin[0].scriptSig;
        CScript::const_iterator pc = scriptSig.begin();
        opcodetype opcode;
        std::vector<unsigned char> vch;
        if (scriptSig.GetOp(pc, opcode, vch) && !vch.empty()) {
            try {
                CScriptNum nHeightNum(vch, false);
                nAdamActualHeight = nHeightNum.getint();
            } catch (...) {}
        }
    }

    const Consensus::Params& consensus = Params().GetConsensus();
    if (hashBlock != consensus.hashGenesisBlock &&
        (block.nVersion >= 11 || (block.IsProofOfWork() && IsAdamActive(nAdamActualHeight, consensus) && nAdamActualHeight < 10000000))) {
        // 1. Verify block version
        if (block.nVersion < 11) {
            return state.DoS(100, error("CheckBlock() : ADAM block version must be >= 11"),
                REJECT_INVALID, "bad-version");
        }
        
        // Resolve pindexPrev to compute the rolling seed
        CBlockIndex* pindexPrev = nullptr;
        {
            LOCK(cs_main);
            if (chainActive.Tip() != nullptr && chainActive.Tip()->GetBlockHash() == block.hashPrevBlock) {
                pindexPrev = chainActive.Tip();
            } else {
                BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
                if (mi != mapBlockIndex.end() && (*mi).second) {
                    pindexPrev = (*mi).second;
                }
            }
        }
        
        if (pindexPrev == nullptr) {
            return state.DoS(100, error("CheckBlock() : predecessor index not found for ADAM block"),
                REJECT_INVALID, "bad-adam-predecessor");
        }
        if (nAdamActualHeight == 0) {
            nAdamActualHeight = pindexPrev->nHeight + 1;
        }
        
        uint256 adamSeed = GetAdamSeed(pindexPrev);
        int64_t nBlockTimeDiff = std::max<int64_t>(0, block.GetBlockTime() - pindexPrev->GetBlockTime());
        int nActiveMns = GetActiveMasternodePoolCount(nAdamActualHeight - 1, pView);
        int threshold = consensus.GetAdamEffectiveThreshold(nAdamActualHeight, nBlockTimeDiff, nActiveMns);
        bool fFallbackMode = (block.nVersion == 11);
        bool fAllowCoordinatorRotation = fFallbackMode || (nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS);

        if (!fBlockOfflineSync || Params().IsRegTestNet()) {
            // 2. Select expected miners and coordinator
            std::vector<CPubKey> vExpectedMiners;
            std::vector<CPubKey> vStandbyMiners;
            CPubKey expectedCoordinator;

            if (fFallbackMode) {
                // Fallback mode validation
                const int nMaxMiners = consensus.GetAdamMinersCount(nAdamActualHeight);
                if (block.vAdamMiners.size() < (size_t)threshold || block.vAdamMiners.size() > (size_t)(nMaxMiners + 3)) {
                    return state.DoS(100, error("CheckBlock() : fallback miners size must be between %d and %d", threshold, nMaxMiners + 3),
                        REJECT_INVALID, "bad-adam-miners-size");
                }
                if (block.vAdamSolutions.size() != block.vAdamMiners.size() - 1 && block.vAdamSolutions.size() != block.vAdamMiners.size()) {
                    return state.DoS(100, error("CheckBlock() : fallback solutions size mismatch"),
                         REJECT_INVALID, "bad-adam-solutions-size");
                }
                if (!Params().IsRegTestNet()) {
                    if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nAdamActualHeight, pView, &vStandbyMiners)) {
                        return state.DoS(100, error("CheckBlock() : failed to select ADAM nodes in v11 mode"),
                            REJECT_INVALID, "bad-adam-election");
                    }
                    std::vector<CPubKey> vBlockCommittee(
                        block.vAdamMiners.begin(),
                        block.vAdamMiners.size() > 0 ? (block.vAdamMiners.end() - 1) : block.vAdamMiners.end());
                    std::vector<std::vector<unsigned char>> vBlockCommitteeSols(
                        block.vAdamSolutions.begin(),
                        block.vAdamSolutions.begin() + std::min(block.vAdamSolutions.size(), vBlockCommittee.size()));
                    if (!ValidateAdamCommitteeForTime(
                            block.hashPrevBlock, adamSeed, nAdamActualHeight, nBlockTimeDiff,
                            block.nBits, block.nVersion, vBlockCommittee, vBlockCommitteeSols,
                            vExpectedMiners, vStandbyMiners, nullptr, nullptr, pView)) {
                        return state.DoS(0, error("CheckBlock() : elected miners mismatch in v11 mode"),
                            REJECT_INVALID, "bad-adam-miners");
                    }
                }
                expectedCoordinator = block.vAdamMiners.back();
            } else {
                // Standard mode validation
                if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nAdamActualHeight, pView, &vStandbyMiners)) {
                    return state.DoS(100, error("CheckBlock() : failed to select ADAM nodes"),
                        REJECT_INVALID, "bad-adam-election");
                }

                if (block.vAdamSolutions.size() != block.vAdamMiners.size()) {
                    return state.DoS(100, error("CheckBlock() : solutions size mismatch"),
                        REJECT_INVALID, "bad-adam-solutions-size");
                }

                if (!ValidateAdamCommitteeForTime(
                        block.hashPrevBlock, adamSeed, nAdamActualHeight, nBlockTimeDiff,
                        block.nBits, block.nVersion, block.vAdamMiners, block.vAdamSolutions,
                        vExpectedMiners, vStandbyMiners, nullptr, nullptr, pView)) {
                    LogPrintf("CheckBlock: miners mismatch! height: %d, version: %d, block.vAdamMiners.size(): %d, vExpectedMiners.size(): %d\n",
                        nAdamActualHeight, block.nVersion, block.vAdamMiners.size(), vExpectedMiners.size());
                    for (size_t i = 0; i < block.vAdamMiners.size(); ++i) {
                        LogPrintf("  block miner %d: %s\n", i, block.vAdamMiners[i].GetID().ToString());
                    }
                    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                        LogPrintf("  expected miner %d: %s\n", i, vExpectedMiners[i].GetID().ToString());
                    }
                    return state.DoS((fHasDip3List && !fBlockOfflineSync) ? 100 : 0, error("CheckBlock() : elected miners mismatch"),
                        REJECT_INVALID, "bad-adam-miners");
                }
            }
            
            // 4. Verify partial solutions
            int validSolutionsCount = 0;
            size_t minersToVerify = fFallbackMode ? (block.vAdamMiners.size() - 1) : block.vAdamMiners.size();
            for (size_t i = 0; i < minersToVerify; ++i) {
                if (!block.vAdamSolutions[i].empty()) {
                    if (VerifyAdamSolution(block.hashPrevBlock, adamSeed, block.vAdamMiners[i], block.vAdamSolutions[i], block.nBits, block.nVersion, pindexPrev->nHeight + 1, fFallbackMode ? -1 : (int)i, pView)) {
                        validSolutionsCount++;
                        try {
                            CDataStream ssSol(block.vAdamSolutions[i], SER_NETWORK, PROTOCOL_VERSION);
                            uint32_t nNonce = 0;
                            std::vector<unsigned char> vchSig;
                            ssSol >> nNonce >> vchSig;
                            CBLSPubKey solBlsPk;
                            if (ExtractBLSPubKeyFromSignedData(vchSig, solBlsPk) &&
                                !checkIntraBlockBlsConsistency(block.vAdamMiners[i], solBlsPk)) {
                                return state.DoS(100, error("CheckBlock() : conflicting BLS public keys for validator at index %u", (unsigned int)i),
                                    REJECT_INVALID, "bad-intra-block-bls-key");
                            }
                        } catch (...) {}
                    } else if (nAdamActualHeight > consensus.nAdamQuorumEnforcementHeight) {
                        return state.DoS(100, error("CheckBlock() : invalid non-empty ADAM solution at index %u", (unsigned int)i),
                            REJECT_INVALID, "bad-adam-solution-invalid");
                    }
                }
            }

            if (nAdamActualHeight > consensus.nAdamQuorumEnforcementHeight && validSolutionsCount < threshold) {
                return state.DoS(0, error("CheckBlock() : quorum threshold not met (valid=%d vs threshold=%d)", 
                    validSolutionsCount, threshold),
                    REJECT_INVALID, "bad-adam-quorum");
            }
            
            CPubKey signingCoordinator = expectedCoordinator;
            bool fCoordSigChecked = false;
            bool fCoordSigValid = false;
            if (block.nVersion >= 12 && (fCheckSig || !block.vAdamCoordinatorSig.empty())) {
                fCoordSigChecked = true;
                // If primary coordinator did not sign, check if the PoBLS XOR-ranked fallback coordinator signed
                if (VerifyAdamCoordinatorSig(block, signingCoordinator, &hashBlock, nAdamActualHeight, pindexPrev)) {
                    fCoordSigValid = true;
                } else if (fAllowCoordinatorRotation && !block.vAdamMiners.empty()) {
                    if (!fFallbackMode && nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                        const std::map<CPubKey, std::vector<unsigned char>> emptySolutions;
                        const std::vector<CPubKey>& vFbMiners = !vExpectedMiners.empty()
                            ? vExpectedMiners : block.vAdamMiners;
                        CPubKey activeFallback, prevFallback;
                        int rotationIndex = 0;
                        if (GetAdamFallbackCoordinatorForSlot(adamSeed, nAdamActualHeight, nBlockTimeDiff, vFbMiners, emptySolutions, activeFallback, prevFallback, rotationIndex, pindexPrev)) {
                            if (VerifyAdamCoordinatorSig(block, activeFallback, &hashBlock, nAdamActualHeight, pindexPrev)) {
                                signingCoordinator = activeFallback;
                                fCoordSigValid = true;
                            } else if (VerifyAdamCoordinatorSig(block, prevFallback, &hashBlock, nAdamActualHeight, pindexPrev)) {
                                signingCoordinator = prevFallback;
                                fCoordSigValid = true;
                            }
                        }
                    } else {
                        for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                            if (VerifyAdamCoordinatorSig(block, *it, &hashBlock, nAdamActualHeight, pindexPrev)) {
                                signingCoordinator = *it;
                                fCoordSigValid = true;
                                break;
                            }
                        }
                    }
                }
            }

            // 5. Verify coordinator VRF proof
            if (fCheckSig && (nAdamActualHeight > consensus.nAdamQuorumEnforcementHeight || !fFallbackMode) && !VerifyAdamVRFProof(adamSeed, block.vAdamVRFProof, signingCoordinator, nAdamActualHeight, pindexPrev)) {
                return state.DoS(0, error("CheckBlock() : invalid coordinator VRF proof"),
                    REJECT_INVALID, "bad-adam-vrf-proof");
            }
            
            // 6. Verify coordinator signature
            if (fCheckSig && (nAdamActualHeight > consensus.nAdamQuorumEnforcementHeight || !fFallbackMode) && !(fCoordSigChecked ? fCoordSigValid : VerifyAdamCoordinatorSig(block, signingCoordinator, &hashBlock, nAdamActualHeight, pindexPrev))) {
                return state.DoS(0, error("CheckBlock() : invalid coordinator signature"),
                    REJECT_INVALID, "bad-adam-coord-sig");
            }
            if (fCheckSig && !block.vAdamVRFProof.empty() && !block.vAdamCoordinatorSig.empty()) {
                CBLSPubKey vrfBlsPk, coordBlsPk;
                if (!ExtractBLSPubKeyFromSignedData(block.vAdamVRFProof, vrfBlsPk) ||
                    !ExtractBLSPubKeyFromSignedData(block.vAdamCoordinatorSig, coordBlsPk) ||
                    vrfBlsPk != coordBlsPk) {
                    return state.DoS(100, error("CheckBlock() : coordinator VRF and block signature BLS public keys do not match"),
                        REJECT_INVALID, "bad-adam-vrf-coord-bls-mismatch");
                }
                if (!checkIntraBlockBlsConsistency(signingCoordinator, coordBlsPk)) {
                    return state.DoS(100, error("CheckBlock() : coordinator BLS public key conflicts with another signature in block"),
                        REJECT_INVALID, "bad-intra-block-bls-key");
                }
            }
        } else if (fCheckSig && nAdamActualHeight > consensus.nAdamQuorumEnforcementHeight && !adamSeed.IsNull()) {
            // Self-contained cryptographic verification during offline sync / IBD
            if (block.vAdamMiners.size() < (size_t)threshold || block.vAdamSolutions.size() < (fFallbackMode ? (block.vAdamMiners.size() - 1) : block.vAdamMiners.size())) {
                return state.DoS(100, error("CheckBlock() : insufficient ADAM miners or solutions during sync"),
                    REJECT_INVALID, "bad-adam-solutions-size");
            }
            int validSolutionsCount = 0;
            size_t minersToVerify = fFallbackMode ? (block.vAdamMiners.size() - 1) : block.vAdamMiners.size();
            for (size_t i = 0; i < minersToVerify; ++i) {
                if (!block.vAdamSolutions[i].empty()) {
                    if (VerifyAdamSolution(block.hashPrevBlock, adamSeed, block.vAdamMiners[i], block.vAdamSolutions[i], block.nBits, block.nVersion, pindexPrev->nHeight + 1, fFallbackMode ? -1 : (int)i, pView)) {
                        validSolutionsCount++;
                    } else {
                        return state.DoS(100, error("CheckBlock() : invalid non-empty ADAM solution at index %u during sync", (unsigned int)i),
                            REJECT_INVALID, "bad-adam-solution-invalid");
                    }
                }
            }
            if (validSolutionsCount < threshold) {
                return state.DoS(0, error("CheckBlock() : quorum threshold not met during sync (valid=%d vs threshold=%d)",
                    validSolutionsCount, threshold),
                    REJECT_INVALID, "bad-adam-quorum");
            }
            // Verify that the block coordinator signature and VRF proof are cryptographically valid even during IBD
            CPubKey syncCoordinator;
            bool fSyncCoordValid = false;
            if (fFallbackMode && !block.vAdamMiners.empty() &&
                VerifyAdamCoordinatorSig(block, block.vAdamMiners.back(), &hashBlock, nAdamActualHeight, pindexPrev)) {
                syncCoordinator = block.vAdamMiners.back();
                fSyncCoordValid = true;
            }
            if (!fSyncCoordValid) {
                std::vector<CPubKey> vExpectedSyncMiners;
                CPubKey expectedSyncCoord;
                if (SelectAdamNodes(adamSeed, consensus, vExpectedSyncMiners, expectedSyncCoord, nAdamActualHeight) &&
                    VerifyAdamCoordinatorSig(block, expectedSyncCoord, &hashBlock, nAdamActualHeight, pindexPrev)) {
                    syncCoordinator = expectedSyncCoord;
                    fSyncCoordValid = true;
                }
            }
            if (!fSyncCoordValid) {
                for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                    if (VerifyAdamCoordinatorSig(block, *it, &hashBlock, nAdamActualHeight, pindexPrev)) {
                        syncCoordinator = *it;
                        fSyncCoordValid = true;
                        break;
                    }
                }
            }
            if (!fSyncCoordValid) {
                std::vector<CPubKey> vSyncPool = GetAdamMinerPool(nAdamActualHeight - 1);
                for (const auto& candKey : vSyncPool) {
                    if (VerifyAdamCoordinatorSig(block, candKey, &hashBlock, nAdamActualHeight, pindexPrev)) {
                        syncCoordinator = candKey;
                        fSyncCoordValid = true;
                        break;
                    }
                }
            }
            if (fSyncCoordValid) {
                if (!VerifyAdamVRFProof(adamSeed, block.vAdamVRFProof, syncCoordinator, nAdamActualHeight, pindexPrev)) {
                    return state.DoS(100, error("CheckBlock() : invalid coordinator VRF proof during sync"),
                        REJECT_INVALID, "bad-adam-vrf-proof");
                }
            } else {
                // Historical / in-flight IBD block where primary coordinator is not yet in local GetAdamMinerPool:
                // verify full cryptographic self-consistency of the BLS12-381 VRF proof and coordinator signature
                CBLSPubKey vrfBlsPk, coordBlsPk;
                if (!VerifySelfContainedBLSSignedData(adamSeed, block.vAdamVRFProof, vrfBlsPk, GetBLSDomainTag(BLSDomain::ADAM_VRF), false) ||
                    !VerifySelfContainedBLSSignedData(hashBlock, block.vAdamCoordinatorSig, coordBlsPk, GetBLSDomainTag(BLSDomain::ADAM_COORD), false) ||
                    vrfBlsPk != coordBlsPk) {
                    return state.DoS(100, error("CheckBlock() : invalid coordinator signature or VRF proof during sync"),
                        REJECT_INVALID, "bad-adam-coord-sig");
                }
            }
            if (!block.vAdamVRFProof.empty() && !block.vAdamCoordinatorSig.empty()) {
                CBLSPubKey vrfBlsPk, coordBlsPk;
                if (!ExtractBLSPubKeyFromSignedData(block.vAdamVRFProof, vrfBlsPk) ||
                    !ExtractBLSPubKeyFromSignedData(block.vAdamCoordinatorSig, coordBlsPk) ||
                    vrfBlsPk != coordBlsPk) {
                    return state.DoS(100, error("CheckBlock() : coordinator VRF and block signature BLS public keys do not match during sync"),
                        REJECT_INVALID, "bad-adam-vrf-coord-bls-mismatch");
                }
            }
        }
    }

    if (fCheckSig && block.nVersion >= 11 && !block.vAdamVRFProof.empty() && block.vAdamVRFProof.size() <= 512) {
        LOCK(cs_recent_vrf_proofs);
        if (mapRecentVRFProofs.size() >= 500) {
            mapRecentVRFProofs.erase(mapRecentVRFProofs.begin());
        }
        mapRecentVRFProofs[hashBlock] = block.vAdamVRFProof;
    }

    if (fCheckPOW && fCheckMerkleRoot && fCheckSig && (!fBlockOfflineSync || Params().IsRegTestNet()) && (pView != nullptr || Params().IsRegTestNet()))
        block.fChecked = true;

    return true;
}


bool CheckWork(const CBlock& block, CBlockIndex* const pindexPrev)
{
    if (pindexPrev == NULL)
        return error("%s : null pindexPrev for block %s", __func__, block.GetHash().GetHex());

    unsigned int nBitsRequired = GetNextWorkRequired(pindexPrev, &block);

    if (block.nBits != nBitsRequired) {
        return error("%s : incorrect proof of work at %d", __func__, pindexPrev->nHeight + 1);
    }

    return true;
}


bool CheckBlockTime(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev)
{
    // Not enforced on RegTest
    if (Params().IsRegTestNet())
        return true;

    const int64_t blockTime = block.GetBlockTime();
    const int blockHeight = pindexPrev->nHeight + 1;

    // Check blocktime against future drift (WANT: blk_time <= Now + MaxDrift)
    if (blockTime > pindexPrev->MaxFutureBlockTime())
        return state.Invalid(error("%s : block timestamp too far in the future", __func__), REJECT_INVALID, "time-too-new");

    // Check blocktime against prev (WANT: blk_time > MinPastBlockTime)
    if (blockTime <= pindexPrev->MinPastBlockTime())
        return state.DoS(50, error("%s : block timestamp too old", __func__), REJECT_INVALID, "time-too-old");

    // Check blocktime mask
    if (!Params().GetConsensus().IsValidBlockTimeStamp(blockTime, blockHeight))
        return state.DoS(100, error("%s : block timestamp mask not valid", __func__), REJECT_INVALID, "invalid-time-mask");

    // All good
    return true;
}


bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* const pindexPrev, bool fCheckQuorumSig)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    uint256 hash = block.GetHash();

    if (hash == consensus.hashGenesisBlock)
        return true;

    assert(pindexPrev);

    const int nHeight = pindexPrev->nHeight + 1;
    const int chainHeight = chainActive.Height();

    //If this is a reorg, check that it is not too deep
    int nMaxReorgDepth = GetArg("-maxreorg", DEFAULT_MAX_REORG_DEPTH);
    if (chainHeight - nHeight >= nMaxReorgDepth)
        return state.DoS(1, error("%s: forked chain older than max reorganization depth (height %d)", __func__, chainHeight - nHeight));

    // Check blocktime (past limit, future limit and mask)
    if (!CheckBlockTime(block, state, pindexPrev))
        return false;

    // Check that the block chain matches the known block chain up to a checkpoint
    if (!Checkpoints::CheckBlock(nHeight, hash))
        return state.DoS(100, error("%s : rejected by checkpoint lock-in at %d", __func__, nHeight),
            REJECT_CHECKPOINT, "checkpoint mismatch");

    // Don't accept any forks from the main chain prior to last checkpoint
    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint();
    if (pcheckpoint && nHeight < pcheckpoint->nHeight)
        return state.DoS(0, error("%s : forked chain older than last checkpoint (height %d)", __func__, nHeight));

    if((block.nVersion < 3 && nHeight >= 1) ||
        (block.nVersion < 5 && consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BIP65)) ||
        (block.nVersion < 7 && consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_TIME_PROTOCOL_V2)) ||
        (block.nVersion < 11 && IsAdamActive(nHeight, consensus)) ||
        (block.nVersion != 12 && consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL)))
    {
        std::string stringErr = strprintf("rejected block version %d at height %d", block.nVersion, nHeight);
        return state.Invalid(false, REJECT_OBSOLETE, "bad-version", stringErr);
    }

    if (!Params().IsRegTestNet() && block.nBits != GetNextWorkRequired(pindexPrev, &block)) {
        return state.DoS(100, error("%s : incorrect proof-of-work/difficulty target at height %d", __func__, nHeight), REJECT_INVALID, "bad-diffbits");
    }

    if (block.nVersion >= 11 && IsAdamActive(nHeight, consensus) && !Params().IsRegTestNet()) {
        uint256 adamSeed = GetAdamSeed(pindexPrev);
        const bool fOfflineHeaderSync = IsInitialBlockDownload() || fReindex || fVerifyingBlocks ||
                                        (pindexBestHeader && chainActive.Height() + 6 < pindexBestHeader->nHeight);
        const bool fDip3Header = (deterministicMNManager != nullptr);
        const bool fHasDip3HeaderList = fDip3Header &&
                                        (pindexPrev->nHeight == 0 || deterministicMNManager->HasListForBlock(pindexPrev->GetBlockHash()));
        int nHeaderActiveMns = 0;
        if (fHasDip3HeaderList && pindexPrev->nHeight > 0) {
            nHeaderActiveMns = (int)deterministicMNManager->GetListForBlock(pindexPrev).GetValidMNsCount();
        }

        int baseThreshold = consensus.GetAdamThreshold(nHeight, nHeaderActiveMns);
        int64_t nHeaderTimeDiff = std::max<int64_t>(0, block.GetBlockTime() - pindexPrev->GetBlockTime());
        int threshold = consensus.GetAdamEffectiveThreshold(nHeight, nHeaderTimeDiff, nHeaderActiveMns);
        size_t expectedSols = (block.nVersion == 11) ? (block.vAdamMiners.size() > 0 ? block.vAdamMiners.size() - 1 : 0) : block.vAdamMiners.size();
        size_t minMiners = (block.nVersion == 11) ? (size_t)threshold : (size_t)baseThreshold;
        if (block.vAdamMiners.size() < minMiners ||
            block.vAdamSolutions.size() < expectedSols ||
            block.vAdamVRFProof.empty() ||
            block.vAdamCoordinatorSig.empty()) {
            return state.DoS(100, error("%s : missing ADAM header proofs at height %d", __func__, nHeight), REJECT_INVALID, "bad-adam-header");
        }
        CBLSPubKey vrfBlsPk, coordBlsPk;
        const bool fVrfOk = !adamSeed.IsNull()
            ? VerifySelfContainedBLSSignedData(adamSeed, block.vAdamVRFProof, vrfBlsPk, GetBLSDomainTag(BLSDomain::ADAM_VRF), false)
            : ExtractBLSPubKeyFromSignedData(block.vAdamVRFProof, vrfBlsPk);
        if (!fVrfOk ||
            !VerifySelfContainedBLSSignedData(hash, block.vAdamCoordinatorSig, coordBlsPk, GetBLSDomainTag(BLSDomain::ADAM_COORD), false) ||
            vrfBlsPk != coordBlsPk) {
            return state.DoS(100, error("%s : invalid ADAM header coordinator BLS proof at height %d", __func__, nHeight), REJECT_INVALID, "bad-adam-header-sig");
        }
        if (!fOfflineHeaderSync && fHasDip3HeaderList && !adamSeed.IsNull() && nHeight > consensus.nAdamQuorumEnforcementHeight) {
            std::vector<CPubKey> vExpectedHdrMiners;
            CPubKey expectedHdrCoord;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedHdrMiners, expectedHdrCoord, nHeight)) {
                CPubKey verifiedHdrCoord;
                bool fDetCoordValid = false;
                if (VerifyAdamCoordinatorSig(block, expectedHdrCoord, &hash, nHeight, pindexPrev)) {
                    verifiedHdrCoord = expectedHdrCoord;
                    fDetCoordValid = true;
                } else if (block.nVersion >= 12 && nHeaderTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                    const std::vector<CPubKey>& vFbMiners = !vExpectedHdrMiners.empty()
                        ? vExpectedHdrMiners : block.vAdamMiners;
                    std::map<CPubKey, std::vector<unsigned char>> emptySols;
                    CPubKey activeFb, prevFb;
                    int rotIdx = 0;
                    if (GetAdamFallbackCoordinatorForSlot(adamSeed, nHeight, nHeaderTimeDiff, vFbMiners, emptySols, activeFb, prevFb, rotIdx, pindexPrev)) {
                        if (VerifyAdamCoordinatorSig(block, activeFb, &hash, nHeight, pindexPrev)) {
                            verifiedHdrCoord = activeFb;
                            fDetCoordValid = true;
                        } else if (VerifyAdamCoordinatorSig(block, prevFb, &hash, nHeight, pindexPrev)) {
                            verifiedHdrCoord = prevFb;
                            fDetCoordValid = true;
                        }
                    }
                } else if (block.nVersion == 11) {
                    for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                        if (VerifyAdamCoordinatorSig(block, *it, &hash, nHeight, pindexPrev)) {
                            verifiedHdrCoord = *it;
                            fDetCoordValid = true;
                            break;
                        }
                    }
                }
                if (!fDetCoordValid || !VerifyAdamVRFProof(adamSeed, block.vAdamVRFProof, verifiedHdrCoord, nHeight, pindexPrev)) {
                    return state.DoS(100, error("%s : header coordinator/VRF does not match deterministic committee at height %d", __func__, nHeight), REJECT_INVALID, "bad-adam-header-sig");
                }
            }
        }
        int nonEmptySols = 0;
        for (size_t i = 0; i < expectedSols; ++i) {
            if (!block.vAdamSolutions[i].empty()) {
                if (!adamSeed.IsNull() &&
                    !VerifyAdamSolution(block.hashPrevBlock, adamSeed, block.vAdamMiners[i], block.vAdamSolutions[i], block.nBits, block.nVersion, nHeight, block.nVersion == 11 ? -1 : (int)i)) {
                    return state.DoS(100, error("%s : invalid ADAM solution in header at height %d", __func__, nHeight), REJECT_INVALID, "bad-adam-header-solution");
                }
                nonEmptySols++;
            }
        }
        if (nonEmptySols < threshold) {
            return state.DoS(100, error("%s : insufficient ADAM solutions in header at height %d (%d < %d)", __func__, nHeight, nonEmptySols, threshold), REJECT_INVALID, "bad-adam-header-quorum");
        }
        if (block.nVersion >= 12 && IsModelDActive(nHeight) && nHeight > consensus.nAdamQuorumEnforcementHeight) {
            if (fCheckQuorumSig && block.vQuorumSig.empty()) {
                return state.DoS(100, error("%s : missing Model D LLMQ quorum signature at height %d", __func__, nHeight), REJECT_INVALID, "missing-quorum-sig");
            }
            if (!block.vQuorumSig.empty()) {
                if (block.vQuorumSig.size() > 4096) {
                    return state.DoS(100, error("%s : oversized Model D LLMQ quorum signature at height %d", __func__, nHeight), REJECT_INVALID, "missing-quorum-sig");
                }
                llmq::CQuorumSignature qsigHdr;
                try {
                    CDataStream ssQ(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
                    ssQ >> qsigHdr;
                    if (!ssQ.empty() || qsigHdr.blockHash != hash || qsigHdr.signatures.empty()) {
                        return state.DoS(100, error("%s : malformed Model D LLMQ quorum signature at height %d", __func__, nHeight), REJECT_INVALID, "bad-quorum-sig-format");
                    }
                } catch (...) {
                    return state.DoS(100, error("%s : failed to deserialize Model D LLMQ quorum signature at height %d", __func__, nHeight), REJECT_INVALID, "bad-quorum-sig-format");
                }
                if (fCheckQuorumSig && !fOfflineHeaderSync && fHasDip3HeaderList && !adamSeed.IsNull()) {
                    llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev);
                    if (!quorum.members.empty() && !qsigHdr.Verify(quorum, pindexPrev, BLSDomain::LLMQ_BLOCK, nHeight)) {
                        return state.DoS(100, error("%s : invalid Model D LLMQ quorum signature at height %d", __func__, nHeight), REJECT_INVALID, "bad-quorum-sig-verify");
                    }
                }
            }
        }
        if (block.vAdamVRFProof.size() <= 512) {
            LOCK(cs_recent_vrf_proofs);
            if (mapRecentVRFProofs.size() >= 500 && !mapRecentVRFProofs.count(hash)) {
                auto itEvict = mapRecentVRFProofs.begin();
                if (chainActive.Tip() && itEvict->first == chainActive.Tip()->GetBlockHash() && std::next(itEvict) != mapRecentVRFProofs.end()) {
                    ++itEvict;
                }
                mapRecentVRFProofs.erase(itEvict);
            }
            mapRecentVRFProofs[hash] = block.vAdamVRFProof;
        }
    }

    return true;
}


bool IsBlockHashInChain(const uint256& hashBlock)
{
    if (hashBlock.IsNull() || !mapBlockIndex.count(hashBlock))
        return false;

    return chainActive.Contains(mapBlockIndex[hashBlock]);
}

bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransaction& tx)
{
    uint256 hashBlock;
    if (!GetTransaction(txId, tx, hashBlock, true))
        return false;
    if (!IsBlockHashInChain(hashBlock))
        return false;

    auto it = mapBlockIndex.find(hashBlock);
    if (it == mapBlockIndex.end() || !it->second)
        return false;
    nHeightTx = it->second->nHeight;
    return true;
}


bool IsTransactionInChain(const uint256& txId, int& nHeightTx)
{
    CTransaction tx;
    return IsTransactionInChain(txId, nHeightTx, tx);
}

bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* const pindexPrev)
{
    const int nHeight = pindexPrev == nullptr ? 0 : pindexPrev->nHeight + 1;
    const Consensus::Params& consensus = Params().GetConsensus();

    // Check that all transactions are finalized
    for (const CTransaction& tx : block.vtx) {
        if (!IsFinalTx(tx, nHeight, block.GetBlockTime())) {
            return state.DoS(10, false, REJECT_INVALID, "bad-txns-nonfinal", false, "non-final transaction");
        }
    }

    // ----------- burn address scanning -----------
    if (!consensus.mBurnAddresses.empty()) {
        for (const CTransaction& tx : block.vtx) {
            if (!tx.IsCoinBase()) {
                for (unsigned int i = 0; i < tx.vin.size(); ++i) {
                    uint256 hashBlock;
                    CTransaction txPrev;
                    if (GetTransaction(tx.vin[i].prevout.hash, txPrev, hashBlock, true)) { // get the vin's previous transaction
                        if (tx.vin[i].prevout.n >= txPrev.vout.size()) {
                            return state.DoS(100, false, REJECT_INVALID, "bad-txns-prevout-null");
                        }
                        CTxDestination source;
                        if (ExtractDestination(txPrev.vout[tx.vin[i].prevout.n].scriptPubKey, source)) { // extract the destination of the previous transaction's vout[n]
                            const std::string addr = EncodeDestination(source);
                            if (consensus.mBurnAddresses.find(addr) != consensus.mBurnAddresses.end() &&
                                consensus.mBurnAddresses.at(addr) < nHeight) {
                                return state.DoS(100, error("%s : Burned address %s tried to send a transaction %s (rejecting it).", __func__, addr.c_str(), txPrev.GetHash().ToString().c_str()), REJECT_INVALID, "bad-txns-banned");
                            }
                        }
                    }
                }
            }
        }
    }

    // // Enforce block.nVersion=2 rule that the coinbase starts with serialized block height
    // if (pindexPrev) { // pindexPrev is only null on the first block which is a version 1 block.
    //     CScript expect = CScript() << nHeight;
    //     if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
    //         !std::equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin())) {
    //         return state.DoS(100, false, REJECT_INVALID, "bad-cb-height", false, "block height mismatch in coinbase");
    //     }
    // }

    // Size limits
    if (nHeight != 0 && !IsInitialBlockDownload()) {
        if (::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE_CURRENT) {
            return state.Invalid(error("%s : size limits failed", __func__), REJECT_INVALID, "bad-blk-length");
        }
    }

    return true;
}


bool AcceptBlockHeader(const CBlock& block, CValidationState& state, CBlockIndex** ppindex)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = block.GetHash();
    BlockMap::iterator miSelf = mapBlockIndex.find(hash);
    CBlockIndex* pindex = NULL;

    // TODO : ENABLE BLOCK CACHE IN SPECIFIC CASES
    if (miSelf != mapBlockIndex.end()) {
        // Block header is already known.
        pindex = miSelf->second;
        if (ppindex)
            *ppindex = pindex;
        if (pindex->nStatus & BLOCK_FAILED_MASK)
            return state.Invalid(error("%s : block is marked invalid", __func__), 0, "duplicate");
        return true;
    }

    if (!CheckBlockHeader(block, state, !block.IsProofOfStake())) {
        return error("%s: CheckBlockHeader failed for block %s: %s", __func__, hash.ToString(), FormatStateMessage(state));
    }

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (hash != Params().GetConsensus().hashGenesisBlock) {
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
            return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock.GetHex()), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            //If this "invalid" block is an exact match from the checkpoints, then reconsider it
            if (pindex && Checkpoints::CheckBlock(pindex->nHeight - 1, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash().GetHex(), pindexPrev->nHeight);
                CValidationState statePrev;
                ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) {
                    ActivateBestChain(statePrev);
                    return true;
                }
            }

            return state.DoS(100, error("%s : prev block height=%d hash=%s is invalid, unable to add block %s", __func__, pindexPrev->nHeight, block.hashPrevBlock.GetHex(), block.GetHash().GetHex()),
                             REJECT_INVALID, "bad-prevblk");
        }

    }

    if (!ContextualCheckBlockHeader(block, state, pindexPrev))
        return error("%s: ContextualCheckBlockHeader failed for block %s: %s", __func__, hash.ToString(), FormatStateMessage(state));

    if (pindex == NULL)
        pindex = AddToBlockIndex(block);

    if (ppindex)
        *ppindex = pindex;

    return true;
}


bool AcceptBlock(const CBlock& block, CValidationState& state, CBlockIndex** ppindex, CDiskBlockPos* dbp, bool fAlreadyCheckedBlock)
{
    AssertLockHeld(cs_main);

    CBlockIndex*& pindex = *ppindex;

    const Consensus::Params& consensus = Params().GetConsensus();

    // Get prev block index
    CBlockIndex* pindexPrev = NULL;
    if (block.GetHash() != consensus.hashGenesisBlock) {
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi == mapBlockIndex.end())
            return state.DoS(0, error("%s : prev block %s not found", __func__, block.hashPrevBlock.GetHex()), 0, "bad-prevblk");
        pindexPrev = (*mi).second;
        if (pindexPrev->nStatus & BLOCK_FAILED_MASK) {
            //If this "invalid" block is an exact match from the checkpoints, then reconsider it
            if (Checkpoints::CheckBlock(pindexPrev->nHeight, block.hashPrevBlock, true)) {
                LogPrintf("%s : Reconsidering block %s height %d\n", __func__, pindexPrev->GetBlockHash().GetHex(), pindexPrev->nHeight);
                CValidationState statePrev;
                ReconsiderBlock(statePrev, pindexPrev);
                if (statePrev.IsValid()) {
                    ActivateBestChain(statePrev);
                    return true;
                }
            }
            return state.DoS(100, error("%s : prev block %s is invalid, unable to add block %s", __func__, block.hashPrevBlock.GetHex(), block.GetHash().GetHex()),
                             REJECT_INVALID, "bad-prevblk");
        }
    }

    if (block.GetHash() != consensus.hashGenesisBlock && !CheckWork(block, pindexPrev))
        return state.DoS(100, error("%s : incorrect proof of work", __func__), REJECT_INVALID, "bad-diffbits");

    bool isPoS = block.IsProofOfStake();
    if (isPoS) {
        const int nHeightAccept = pindexPrev ? (pindexPrev->nHeight + 1) : 0;
        const bool enableP2PKHAccept = consensus.NetworkUpgradeActive(nHeightAccept, Consensus::UPGRADE_P2PKH_BLOCK_SIGNATURES);
        if (!CheckBlockSignature(block, enableP2PKHAccept, nHeightAccept)) {
            return state.DoS(100, error("%s: bad proof-of-stake block signature at height %d", __func__, nHeightAccept), REJECT_INVALID, "bad-blk-sig");
        }
        // Only run PoB/PoM stake-kernel weight validation here when pcoinsTip/evodb/burnCache match pindexPrev;
        // competing side-branch forks defer full weight verification to ConnectBlock after tip rewind.
        if (pindexPrev == chainActive.Tip() && pcoinsTip && pcoinsTip->GetBestBlock() == pindexPrev->GetBlockHash()) {
            std::string strError;
            if (!CheckProofOfStake(block, strError, pindexPrev, pcoinsTip)) {
                if (pindexPrev && !(pindexPrev->nStatus & BLOCK_HAVE_DATA)) {
                    return state.DoS(0, error("%s: proof of stake check failed but parent block data is missing (%s)", __func__, strError));
                }
                return state.DoS(100, error("%s: proof of stake check failed (%s)", __func__, strError));
            }
        }
    }

    if (!AcceptBlockHeader(block, state, &pindex))
        return false;

    if (pindex->nStatus & BLOCK_HAVE_DATA) {
        // TODO: deal better with duplicate blocks.
        // return state.DoS(20, error("AcceptBlock() : already have block %d %s", pindex->nHeight, pindex->GetBlockHash().ToString()), REJECT_DUPLICATE, "duplicate");
        LogPrintf("AcceptBlock() : already have block %d %s", pindex->nHeight, pindex->GetBlockHash().ToString());
        return true;
    }

    if ((!fAlreadyCheckedBlock && !CheckBlock(block, state)) || !ContextualCheckBlock(block, state, pindex->pprev)) {
        if (state.IsInvalid() && !state.CorruptionPossible()) {
            pindex->nStatus |= BLOCK_FAILED_VALID;
            setDirtyBlockIndex.insert(pindex);
        }
        return error("%s: %s", __func__, FormatStateMessage(state));
    }

    int nHeight = pindex->nHeight;
    int splitHeight = -1;

    if (isPoS) {
        LOCK(cs_main);

        // Blocks arrives in order, so if prev block is not the tip then we are on a fork.
        // Extra info: duplicated blocks are skipping this checks, so we don't have to worry about those here.
        bool isBlockFromFork = pindexPrev != nullptr && chainActive.Tip() != pindexPrev;

        // Coin stake
        const CTransaction &stakeTxIn = block.vtx[1];

        // Inputs
        std::vector<CTxIn> pivInputs;

        for (const CTxIn& stakeIn : stakeTxIn.vin) {
            pivInputs.push_back(stakeIn);            
        }
        const bool hasPIVInputs = !pivInputs.empty();


        // Check for serial double spent on the same block, TODO: Move this to the proper method..

        for (const CTransaction& tx : block.vtx) {
            for (const CTxIn& in: tx.vin) {
                if(tx.IsCoinStake()) continue;
                if(hasPIVInputs) {
                    // Check if coinstake input is double spent inside the same block
                    for (const CTxIn& pivIn : pivInputs)
                        if (pivIn.prevout == in.prevout) {
                            // double spent coinstake input inside block
                            state.DoS(100, error("%s: double spent coinstake input inside block", __func__), REJECT_INVALID, "bad-pos-double-spend");
                            if (chainActive.Tip()) InvalidBlockFound(pindex, state);
                            else { pindex->nStatus |= BLOCK_FAILED_VALID; setDirtyBlockIndex.insert(pindex); }
                            return false;
                        }
                }

            }
        }

        // Check whether is a fork or not
        if (isBlockFromFork) {

            // Start at the block we're adding on to
            CBlockIndex *prev = pindexPrev;

            CBlock bl;
            if (!ReadBlockFromDisk(bl, prev))
                return error("%s: previous block %s not on disk", __func__, prev->GetBlockHash().GetHex());

            int readBlock = 0;
            // Go backwards on the forked chain up to the split
            while (!chainActive.Contains(prev)) {

                // Increase amount of read blocks
                readBlock++;
                // Check if the forked chain is longer than the max reorg limit
                if (readBlock == GetArg("-maxreorg", DEFAULT_MAX_REORG_DEPTH)) {
                    // TODO: Remove this chain from disk.
                    return error("%s: forked chain longer than maximum reorg limit", __func__);
                }

                // Loop through every tx of this block
                for (const CTransaction& t : bl.vtx) {
                    // Loop through every input of this tx
                    for (const CTxIn& in: t.vin) {

                        // Loop through every input of the staking tx
                        if (hasPIVInputs) {
                            for (const CTxIn& stakeIn : pivInputs)
                                // check if the tx input is double spending any coinstake input
                                if (stakeIn.prevout == in.prevout) {
                                    state.DoS(100, error("%s: input already spent on a previous block", __func__), REJECT_INVALID, "bad-pos-double-spend");
                                    if (chainActive.Tip()) InvalidBlockFound(pindex, state);
                                    else { pindex->nStatus |= BLOCK_FAILED_VALID; setDirtyBlockIndex.insert(pindex); }
                                    return false;
                                }
                        }
                    }
                }

                // Prev block
                prev = prev->pprev;
                if (!ReadBlockFromDisk(bl, prev))
                    // Previous block not on disk
                    return error("%s: previous block %s not on disk", __func__, prev->GetBlockHash().GetHex());

            }

            // Split height
            splitHeight = prev->nHeight;
        }

        // If the stake is not a zPoS then let's check if the inputs were spent on the main chain
        if (!isBlockFromFork && pcoinsTip && pcoinsTip->GetBestBlock() == pindexPrev->GetBlockHash()) {
            const CCoinsViewCache coins(pcoinsTip);
            for (const CTxIn& in: stakeTxIn.vin) {
                const Coin& coin = coins.AccessCoin(in.prevout);
                if (coin.IsSpent()) {
                    // No coins on the main chain
                    state.DoS(100, error("%s: coin stake inputs not-available/already-spent on main chain, received height %d vs current %d", __func__, nHeight, chainActive.Height()), REJECT_INVALID, "bad-pos-inputs-spent");
                    if (chainActive.Tip()) InvalidBlockFound(pindex, state);
                    else { pindex->nStatus |= BLOCK_FAILED_VALID; setDirtyBlockIndex.insert(pindex); }
                    return false;
                }
            }
        }
    }

    // Write block to history file
    try {
        unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
        CDiskBlockPos blockPos;
        if (dbp != NULL)
            blockPos = *dbp;
        if (!FindBlockPos(state, blockPos, nBlockSize + 8, nHeight, block.GetBlockTime(), dbp != NULL))
            return error("AcceptBlock() : FindBlockPos failed");
        if (dbp == NULL)
            if (!WriteBlockToDisk(block, blockPos))
                return AbortNode(state, "Failed to write block");
        if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
            return error("AcceptBlock() : ReceivedBlockTransactions failed");
    } catch (const std::runtime_error& e) {
        return AbortNode(state, std::string("System error: ") + e.what());
    }

    return true;
}


/** Turn the lowest '1' bit in the binary representation of a number into a '0'. */
int static inline InvertLowestOne(int n) { return n & (n - 1); }

/** Compute what height to jump back to with the CBlockIndex::pskip pointer. */
int static inline GetSkipHeight(int height)
{
    if (height < 2)
        return 0;

    // Determine which height to jump back to. Any number strictly lower than height is acceptable,
    // but the following expression seems to perform well in simulations (max 110 steps to go back
    // up to 2**18 blocks).
    return (height & 1) ? InvertLowestOne(InvertLowestOne(height - 1)) + 1 : InvertLowestOne(height);
}

CBlockIndex* CBlockIndex::GetAncestor(int height)
{
    if (height > nHeight || height < 0)
        return NULL;

    CBlockIndex* pindexWalk = this;
    int heightWalk = nHeight;
    while (pindexWalk && heightWalk > height) {
        int heightSkip = GetSkipHeight(heightWalk);
        int heightSkipPrev = GetSkipHeight(heightWalk - 1);
        if (pindexWalk->pskip &&
            (heightSkip == height ||
             (heightSkip > height && !(heightSkipPrev < heightSkip - 2 && heightSkipPrev >= height)))) {
            // Only follow pskip if pprev->pskip isn't better than pskip->pprev.
            pindexWalk = pindexWalk->pskip;
            heightWalk = heightSkip;
        } else {
            pindexWalk = pindexWalk->pprev;
            heightWalk--;
        }
    }
    return pindexWalk;
}

const CBlockIndex* CBlockIndex::GetAncestor(int height) const
{
    return const_cast<CBlockIndex*>(this)->GetAncestor(height);
}

void CBlockIndex::BuildSkip()
{
    if (pprev)
        pskip = pprev->GetAncestor(GetSkipHeight(nHeight));
}

bool ProcessNewBlock(CValidationState& state, CNode* pfrom, const CBlock* pblock, CDiskBlockPos* dbp, CConnman* connman)
{
    AssertLockNotHeld(cs_main);

    // Preliminary checks
    int64_t nStartTime = GetTimeMillis();
    const Consensus::Params& consensus = Params().GetConsensus();

    // check block
    bool checked = CheckBlock(*pblock, state);
    if (!checked) {
        LOCK(cs_main);
        NotifyBlockReceivedFromPeer(pblock->GetHash());
        return error("%s : CheckBlock FAILED for block %s, %s", __func__, pblock->GetHash().GetHex(), FormatStateMessage(state));
    }

    int newHeight = 1;
    bool fMissingPrev = false;
    CBlockLocator missingPrevLocator;
    {
        LOCK(cs_main);
        const int chainHeight = chainActive.Height();
        BlockMap::iterator mi = mapBlockIndex.find(pblock->hashPrevBlock);
        if (mi != mapBlockIndex.end() && mi->second) {
            newHeight = mi->second->nHeight + 1;
        } else {
            newHeight = (chainHeight >= 0) ? (chainHeight + 1) : 1;
            if (pblock->GetHash() != consensus.hashGenesisBlock && pfrom != NULL) {
                fMissingPrev = true;
                missingPrevLocator = chainActive.GetLocator();
            }
        }
    }

    // For now, we need the previous block height to know whether p2pkh block signatures are accepted or not.
    const bool enableP2PKH = consensus.NetworkUpgradeActive(newHeight, Consensus::UPGRADE_P2PKH_BLOCK_SIGNATURES);
    if (pblock->IsProofOfStake()) {
        const bool fBlkSigOk = fMissingPrev
            ? (CheckBlockSignature(*pblock, true, newHeight) || CheckBlockSignature(*pblock, true, -1))
            : CheckBlockSignature(*pblock, enableP2PKH, newHeight);
        if (!fBlkSigOk) {
            return state.DoS(100, error("%s : bad proof-of-stake block signature", __func__), REJECT_INVALID, "bad-blk-sig");
        }
    }

    if (fMissingPrev && pfrom != NULL) {
        g_connman->PushMessage(pfrom, CNetMsgMaker(pfrom->GetSendVersion()).Make(NetMsgType::GETBLOCKS, missingPrevLocator, UINT256_ZERO));
        return false;
    }

    {
        LOCK(cs_main);

        NotifyBlockReceivedFromPeer(pblock->GetHash());

        // Store to disk
        CBlockIndex* pindex = nullptr;
        if (pfrom) {
            UpdatePeerBlockSourceAndTime(pfrom->GetId(), pblock->GetHash());
        }
        bool ret = AcceptBlock(*pblock, state, &pindex, dbp, checked);
        if (pindex && pfrom) {
            UpdatePeerBlockSourceAndTime(pfrom->GetId(), pindex->GetBlockHash());
        }
        CheckBlockIndex();
        if (!ret) {
            // Check spamming
            if (pfrom && GetBoolArg("-blockspamfilter", DEFAULT_BLOCK_SPAM_FILTER)) {
                const int nSpamHeight = pindex ? pindex->nHeight : newHeight;
                if (!CheckPeerBlockSpam(pfrom->GetId(), nSpamHeight, state))
                    return error("%s : AcceptBlock FAILED - block spam protection", __func__);
            }
            return error("%s : AcceptBlock FAILED", __func__);
        }
        if (pindex && pfrom && !IsInitialBlockDownload() &&
            pindex->pprev && pindex->pprev != chainActive.Tip() &&
            GetBoolArg("-blockspamfilter", DEFAULT_BLOCK_SPAM_FILTER)) {
            if (!CheckPeerBlockSpam(pfrom->GetId(), pindex->nHeight, state)) {
                return error("%s : side-chain block spam protection triggered", __func__);
            }
        }
    }

    if (!ActivateBestChain(state, pblock, checked && pblock->fChecked, connman))
        return error("%s : ActivateBestChain failed", __func__);

    if (pwalletMain) {
        // If turned on Auto Combine will scan wallet for dust to combine
        if (pwalletMain->fCombineDust)
            pwalletMain->AutoCombineDust(connman);
    }

    LogPrintf("%s : ACCEPTED Block %ld in %ld milliseconds with size=%d\n", __func__, newHeight, GetTimeMillis() - nStartTime,
              GetSerializeSize(*pblock, SER_DISK, CLIENT_VERSION));

    return true;
}


bool IsFutureBlockPreVerified(const CBlockHeader& header, const CBlockIndex* pindexPrev)
{
    if (!pindexPrev) {
        return false;
    }
    if (!Params().IsRegTestNet() && header.GetBlockTime() <= pindexPrev->MinPastBlockTime()) {
        return false;
    }

    const int nCandidateHeight = pindexPrev->nHeight + 1;
    const uint256 hashBlock = header.GetHash();
    const Consensus::Params& consensus = Params().GetConsensus();
    if ((header.nVersion < 3 && nCandidateHeight >= 1) ||
        (header.nVersion < 5 && consensus.NetworkUpgradeActive(nCandidateHeight, Consensus::UPGRADE_BIP65)) ||
        (header.nVersion < 7 && consensus.NetworkUpgradeActive(nCandidateHeight, Consensus::UPGRADE_TIME_PROTOCOL_V2)) ||
        (header.nVersion < 11 && IsAdamActive(nCandidateHeight, consensus)) ||
        (header.nVersion != 12 && consensus.NetworkUpgradeActive(nCandidateHeight, Consensus::UPGRADE_POMBL))) {
        return false;
    }

    if (header.nVersion >= 11) {
        if (header.vAdamCoordinatorSig.empty() || header.vAdamCoordinatorSig.size() > 512 ||
            header.vAdamVRFProof.empty() || header.vAdamVRFProof.size() > 512 ||
            !HasExactBLSSignedDataEncoding(header.vAdamCoordinatorSig) ||
            !HasExactBLSSignedDataEncoding(header.vAdamVRFProof)) {
            return false;
        }

        CBLSSignedData coordSignedData;
        try {
            CDataStream ssCoord(header.vAdamCoordinatorSig, SER_NETWORK, PROTOCOL_VERSION);
            ssCoord >> coordSignedData;
            if (!ssCoord.empty()) {
                return false;
            }
        } catch (...) {
            return false;
        }

        uint256 adamSeed;
        std::vector<CPubKey> vPool;
        {
            LOCK(cs_main);
            adamSeed = GetAdamSeed(pindexPrev);
            if (adamSeed.IsNull()) {
                return false;
            }
            const bool fAtChainTip = (chainActive.Tip() == pindexPrev && pcoinsTip != nullptr);
            vPool = GetAdamMinerPool(nCandidateHeight - 1, fAtChainTip ? pcoinsTip : nullptr);
        }

        CBLSPubKey vrfBlsPk, coordBlsPk;
        if (!VerifySelfContainedBLSSignedData(adamSeed, header.vAdamVRFProof, vrfBlsPk, GetBLSDomainTag(BLSDomain::ADAM_VRF), false) ||
            !VerifySelfContainedBLSSignedData(hashBlock, header.vAdamCoordinatorSig, coordBlsPk, GetBLSDomainTag(BLSDomain::ADAM_COORD), false) ||
            vrfBlsPk != coordBlsPk) {
            return false;
        }

        const uint256 hashCoordBlsPk = Hash(coordSignedData.blsPubKey.begin(), coordSignedData.blsPubKey.end());
        CPubKey matchedCandKey;
        if (coordSignedData.ecdsaSig.empty()) {
            CPubKey dip3OwnerKey;
            if (ResolveDip3OwnerByOperatorBls(coordSignedData.blsPubKey, nCandidateHeight, dip3OwnerKey, pindexPrev) &&
                dip3OwnerKey.IsValid() &&
                std::find(vPool.begin(), vPool.end(), dip3OwnerKey) != vPool.end()) {
                matchedCandKey = dip3OwnerKey;
            }
        } else {
            for (const auto& candKey : vPool) {
                if (!candKey.IsValid()) {
                    continue;
                }
                CBLSPubKey dip3CheckPk;
                if (ResolveDip3OperatorBlsPubKey(candKey, nCandidateHeight, dip3CheckPk, pindexPrev)) {
                    continue;
                }
                if (candKey.VerifyStrictDER(hashCoordBlsPk, coordSignedData.ecdsaSig)) {
                    matchedCandKey = candKey;
                    break;
                }
            }
        }
        if (!matchedCandKey.IsValid() ||
            !VerifyAdamVRFProof(adamSeed, header.vAdamVRFProof, matchedCandKey, nCandidateHeight, pindexPrev) ||
            !VerifyAdamCoordinatorSig(header, matchedCandKey, &hashBlock, nCandidateHeight, pindexPrev)) {
            return false;
        }
    }

    if (header.nVersion >= 12) {
        const bool fRequireModelDQuorum = (nCandidateHeight > 0) && IsModelDActive(nCandidateHeight);
        if (fRequireModelDQuorum) {
            if (header.vQuorumSig.empty() || header.vQuorumSig.size() > 4096) {
                return false;
            }
            llmq::CQuorumSignature qsig;
            try {
                CDataStream ssQ(header.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
                ssQ >> qsig;
                if (!ssQ.empty()) {
                    return false;
                }
            } catch (...) {
                return false;
            }
            if (qsig.blockHash != hashBlock || qsig.signatures.empty() || qsig.signatures.size() > 32) {
                return false;
            }
            for (const auto& sigPair : qsig.signatures) {
                if (sigPair.second.empty() || sigPair.second.size() > 512 || !HasExactBLSSignedDataEncoding(sigPair.second)) {
                    return false;
                }
            }
            llmq::CQuorum quorum;
            {
                LOCK(cs_main);
                if (!Params().IsRegTestNet() && deterministicMNManager &&
                    pindexPrev->nHeight > 0 &&
                    !deterministicMNManager->HasListForBlock(pindexPrev->GetBlockHash())) {
                    return false;
                }
                const bool fAtChainTip = (chainActive.Tip() == pindexPrev && pcoinsTip != nullptr);
                quorum = llmq::GetActiveQuorum(nCandidateHeight, pindexPrev, fAtChainTip ? pcoinsTip : nullptr);
            }
            if (quorum.members.empty() || !qsig.Verify(quorum, pindexPrev, BLSDomain::LLMQ_BLOCK, nCandidateHeight)) {
                return false;
            }
        } else if (header.vQuorumSig.size() > 4096) {
            return false;
        }
    }
    return true;
}


bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* const pindexPrev, bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckQuorumSig)
{
    AssertLockHeld(cs_main);
    assert(pindexPrev);
    if (pindexPrev != chainActive.Tip() || !pcoinsTip || pcoinsTip->GetBestBlock() != pindexPrev->GetBlockHash()) {
        LogPrintf("%s : No longer working on chain tip\n", __func__);
        return false;
    }

    CCoinsViewCache viewNew(pcoinsTip);
    uint256 hashDummy = block.GetHash();
    CBlockIndex indexDummy(block);
    indexDummy.phashBlock = &hashDummy;
    indexDummy.pprev = pindexPrev;
    indexDummy.nHeight = pindexPrev->nHeight + 1;

    // NOTE: CheckBlockHeader is called by CheckBlock
    if (!ContextualCheckBlockHeader(block, state, pindexPrev, fCheckQuorumSig))
        return error("%s: ContextualCheckBlockHeader failed: %s", __func__, FormatStateMessage(state));
    if (!CheckBlock(block, state, fCheckPOW, fCheckMerkleRoot, false))
        return error("%s: CheckBlock failed: %s", __func__, FormatStateMessage(state));
    if (!ContextualCheckBlock(block, state, pindexPrev))
        return error("%s: ContextualCheckBlock failed: %s", __func__, FormatStateMessage(state));
    if (!ConnectBlock(block, state, &indexDummy, viewNew, true, true))
        return false;
    assert(state.IsValid());

    return true;
}


bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
        return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

    return true;
}


FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly)
{
    if (pos.IsNull())
        return NULL;
    fs::path path = GetBlockPosFilename(pos, prefix);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, "rb+");
    if (!file && !fReadOnly)
        file = fsbridge::fopen(path, "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", path.string());
        return NULL;
    }
    if (pos.nPos) {
        if (fseek(file, pos.nPos, SEEK_SET)) {
            LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
            fclose(file);
            return NULL;
        }
    }
    return file;
}

FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "blk", fReadOnly);
}

FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly)
{
    return OpenDiskFile(pos, "rev", fReadOnly);
}

fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix)
{
    return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

CBlockIndex* InsertBlockIndex(uint256 hash)
{
    if (hash.IsNull())
        return NULL;

    // Return existing
    BlockMap::iterator mi = mapBlockIndex.find(hash);
    if (mi != mapBlockIndex.end())
        return (*mi).second;

    // Create new
    CBlockIndex* pindexNew = new CBlockIndex();
    if (!pindexNew)
        throw std::runtime_error("LoadBlockIndex() : new CBlockIndex failed");
    mi = mapBlockIndex.insert(std::make_pair(hash, pindexNew)).first;

    pindexNew->phashBlock = &((*mi).first);

    return pindexNew;
}

bool static LoadBlockIndexDB(std::string& strError)
{
    if (!pblocktree->LoadBlockIndexGuts(InsertBlockIndex))
        return false;

    boost::this_thread::interruption_point();

    // Calculate nChainWork
    std::vector<std::pair<int, CBlockIndex*> > vSortedByHeight;
    vSortedByHeight.reserve(mapBlockIndex.size());
    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex) {
        CBlockIndex* pindex = item.second;
        vSortedByHeight.push_back(std::make_pair(pindex->nHeight, pindex));
    }
    std::sort(vSortedByHeight.begin(), vSortedByHeight.end());
    for (const PAIRTYPE(int, CBlockIndex*) & item : vSortedByHeight) {
        // Stop if shutdown was requested
        if (ShutdownRequested()) return false;

        CBlockIndex* pindex = item.second;
        pindex->nChainWork = (pindex->pprev ? pindex->pprev->nChainWork : 0) + GetBlockProof(*pindex);
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            if (pindex->pprev) {
                if (pindex->pprev->nChainTx) {
                    pindex->nChainTx = pindex->pprev->nChainTx + pindex->nTx;
                } else {
                    pindex->nChainTx = 0;
                    mapBlocksUnlinked.insert(std::make_pair(pindex->pprev, pindex));
                }
            } else {
                pindex->nChainTx = pindex->nTx;
            }
        }
        if (pindex->IsValid(BLOCK_VALID_TRANSACTIONS) && (pindex->nChainTx || pindex->pprev == NULL))
            setBlockIndexCandidates.insert(pindex);
        if (pindex->nStatus & BLOCK_FAILED_MASK && (!pindexBestInvalid || pindex->nChainWork > pindexBestInvalid->nChainWork))
            pindexBestInvalid = pindex;
        if (pindex->pprev)
            pindex->BuildSkip();
        if (pindex->IsValid(BLOCK_VALID_TREE) && (pindexBestHeader == NULL || CBlockIndexWorkComparator()(pindexBestHeader, pindex)))
            pindexBestHeader = pindex;
    }

    // Load block file info
    pblocktree->ReadLastBlockFile(nLastBlockFile);
    vinfoBlockFile.resize(nLastBlockFile + 1);
    LogPrintf("%s: last block file = %i\n", __func__, nLastBlockFile);
    for (int nFile = 0; nFile <= nLastBlockFile; nFile++) {
        pblocktree->ReadBlockFileInfo(nFile, vinfoBlockFile[nFile]);
    }
    LogPrintf("%s: last block file info: %s\n", __func__, vinfoBlockFile[nLastBlockFile].ToString());
    for (int nFile = nLastBlockFile + 1; true; nFile++) {
        CBlockFileInfo info;
        if (pblocktree->ReadBlockFileInfo(nFile, info)) {
            vinfoBlockFile.push_back(info);
        } else {
            break;
        }
    }

    // Check presence of blk files
    LogPrintf("Checking all blk files are present...\n");
    std::set<int> setBlkDataFiles;
    for (const std::pair<const uint256, CBlockIndex*>& item : mapBlockIndex) {
        CBlockIndex* pindex = item.second;
        if (pindex->nStatus & BLOCK_HAVE_DATA) {
            setBlkDataFiles.insert(pindex->nFile);
        }
    }
    for (std::set<int>::iterator it = setBlkDataFiles.begin(); it != setBlkDataFiles.end(); it++) {
        CDiskBlockPos pos(*it, 0);
        if (CAutoFile(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION).IsNull()) {
            return false;
        }
    }

    //Check if the shutdown procedure was followed on last client exit
    bool fLastShutdownWasPrepared = true;
    pblocktree->ReadFlag("shutdown", fLastShutdownWasPrepared);
    LogPrintf("%s: Last shutdown was prepared: %s\n", __func__, fLastShutdownWasPrepared);

    // Check whether we need to continue reindexing
    bool fReindexing = false;
    pblocktree->ReadReindexing(fReindexing);
    if(fReindexing) fReindex = true;

    // Check whether we have a transaction index
    pblocktree->ReadFlag("txindex", fTxIndex);
    LogPrintf("LoadBlockIndexDB(): transaction index %s\n", fTxIndex ? "enabled" : "disabled");

    // If this is written true before the next client init, then we know the shutdown process failed
    pblocktree->WriteFlag("shutdown", false);

    // Load pointer to end of best chain
    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    if (it == mapBlockIndex.end())
        return true;
    chainActive.SetTip(it->second);

    PruneBlockIndexCandidates();

    const CBlockIndex* pChainTip = chainActive.Tip();
    LogPrintf("LoadBlockIndexDB(): hashBestChain=%s height=%d date=%s progress=%f\n",
            pChainTip->GetBlockHash().GetHex(), pChainTip->nHeight,
            DateTimeStrFormat("%Y-%m-%d %H:%M:%S", pChainTip->GetBlockTime()),
            Checkpoints::GuessVerificationProgress(pChainTip));

    return true;
}


CVerifyDB::CVerifyDB()
{
    uiInterface.ShowProgress(_("Verifying blocks..."), 0);
}

CVerifyDB::~CVerifyDB()
{
    uiInterface.ShowProgress("", 100);
}

bool CVerifyDB::VerifyDB(CCoinsView* coinsview, int nCheckLevel, int nCheckDepth)
{
    LOCK(cs_main);
    if (chainActive.Tip() == NULL || chainActive.Tip()->pprev == NULL)
        return true;

    struct SecVerifyingBlocks {
        CAmount nSavedMoneySupply;
        CBurnCacheScopedBackup burnBackup;
        SecVerifyingBlocks() : nSavedMoneySupply(nMoneySupply) { fVerifyingBlocks = true; }
        ~SecVerifyingBlocks() {
            fVerifyingBlocks = false;
            nMoneySupply = nSavedMoneySupply;
        }
    } secVerifyingBlocks;

    const int chainHeight = chainActive.Height();
    // Verify blocks in the best chain
    if (nCheckDepth <= 0)
        nCheckDepth = 1000000000; // suffices until the year 19000
    if (nCheckDepth > chainHeight)
        nCheckDepth = chainHeight;
    nCheckLevel = std::max(0, std::min(4, nCheckLevel));
    LogPrintf("Verifying last %i blocks at level %i\n", nCheckDepth, nCheckLevel);
    CCoinsViewCache coins(coinsview);
    CBlockIndex* pindexState = chainActive.Tip();
    CBlockIndex* pindexFailure = NULL;
    int nGoodTransactions = 0;
    CValidationState state;
    for (CBlockIndex* pindex = chainActive.Tip(); pindex && pindex->pprev; pindex = pindex->pprev) {
        boost::this_thread::interruption_point();
        uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, (int)(((double)(chainHeight - pindex->nHeight)) / (double)nCheckDepth * (nCheckLevel >= 4 ? 50 : 100)))));
        if (pindex->nHeight < chainHeight - nCheckDepth)
            break;
        CBlock block;
        // check level 0: read from disk
        if (!ReadBlockFromDisk(block, pindex))
            return error("%s: *** ReadBlockFromDisk failed at %d, hash=%s", __func__, pindex->nHeight, pindex->GetBlockHash().ToString());
        // check level 1: verify block validity
        if (nCheckLevel >= 1 && !CheckBlock(block, state))
            return error("%s: *** found bad block at %d, hash=%s (%s)\n", __func__, pindex->nHeight, pindex->GetBlockHash().ToString(), FormatStateMessage(state));
        // check level 2: verify undo validity
        if (nCheckLevel >= 2 && pindex) {
            CBlockUndo undo;
            CDiskBlockPos pos = pindex->GetUndoPos();
            if (!pos.IsNull()) {
                if (!UndoReadFromDisk(undo, pos, pindex->pprev->GetBlockHash()))
                    return error("%s: *** found bad undo data at %d, hash=%s\n", __func__, pindex->nHeight, pindex->GetBlockHash().ToString());
            }
        }
        // check level 3: check for inconsistencies during memory-only disconnect of tip blocks
        if (nCheckLevel >= 3 && pindex == pindexState && (coins.DynamicMemoryUsage() + pcoinsTip->DynamicMemoryUsage()) <= nCoinCacheUsage) {
            DisconnectResult res = DisconnectBlock(block, pindex, coins);
            if (res == DISCONNECT_FAILED) {
                return error("%s: *** irrecoverable inconsistency in block data at %d, hash=%s", __func__,
                             pindex->nHeight, pindex->GetBlockHash().ToString());
            }
            pindexState = pindex->pprev;
            if (res == DISCONNECT_UNCLEAN) {
                nGoodTransactions = 0;
                pindexFailure = pindex;
            } else {
                nGoodTransactions += block.vtx.size();
            }
        }
        if (ShutdownRequested())
            return true;
    }
    if (pindexFailure)
        return error("%s: *** coin database inconsistencies found (last %i blocks, %i good transactions before that)\n", __func__, chainHeight - pindexFailure->nHeight + 1, nGoodTransactions);

    // check level 4: try reconnecting blocks
    if (nCheckLevel >= 4) {
        CBlockIndex* pindex = pindexState;
        while (pindex != chainActive.Tip()) {
            boost::this_thread::interruption_point();
            uiInterface.ShowProgress(_("Verifying blocks..."), std::max(1, std::min(99, 100 - (int)(((double)(chainHeight - pindex->nHeight)) / (double)nCheckDepth * 50))));
            pindex = chainActive.Next(pindex);
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex))
                return error("%s: *** ReadBlockFromDisk failed at %d, hash=%s", __func__, pindex->nHeight, pindex->GetBlockHash().ToString());
            if (!ConnectBlock(block, state, pindex, coins, false))
                return error("%s: *** found unconnectable block at %d, hash=%s", __func__, pindex->nHeight, pindex->GetBlockHash().ToString());
        }
    }

    LogPrintf("No coin database inconsistencies in last %i blocks (%i transactions)\n", chainHeight - pindexState->nHeight, nGoodTransactions);

    return true;
}


void UnloadBlockIndex()
{
    LOCK(cs_main);
    setBlockIndexCandidates.clear();
    chainActive.SetTip(NULL);
    pindexBestInvalid = NULL;
    pindexBestHeader = NULL;
    mempool.clear();
    mapOrphanTransactions.clear();
    mapOrphanTransactionsByPrev.clear();
    nSyncStarted = 0;
    mapBlocksUnlinked.clear();
    vinfoBlockFile.clear();
    nLastBlockFile = 0;
    nBlockSequenceId = 1;
    setDirtyBlockIndex.clear();
    setDirtyFileInfo.clear();
    ClearNetProcessingState();

    for (BlockMap::value_type& entry : mapBlockIndex) {
        delete entry.second;
    }
    mapBlockIndex.clear();
}

bool LoadBlockIndex(std::string& strError)
{
    // Load block index from databases
    if (!fReindex && !LoadBlockIndexDB(strError))
        return false;
    return true;
}



bool InitBlockIndex()
{
    LOCK(cs_main);

    // Initialize global variables that cannot be constructed at startup.
    ResetRecentRejects();

    // Check whether we're already initialized
    if (chainActive.Genesis() != NULL)
        return true;

    // Use the provided setting for -txindex in the new database
    fTxIndex = GetBoolArg("-txindex", true);
    pblocktree->WriteFlag("txindex", fTxIndex);
    LogPrintf("Initializing databases...\n");

    // Only add the genesis block if not reindexing (in which case we reuse the one already on disk)
    if (!fReindex) {
        try {
            CBlock& block = const_cast<CBlock&>(Params().GenesisBlock());
            // Start new block file
            unsigned int nBlockSize = ::GetSerializeSize(block, SER_DISK, CLIENT_VERSION);
            CDiskBlockPos blockPos;
            CValidationState state;
            if (!FindBlockPos(state, blockPos, nBlockSize + 8, 0, block.GetBlockTime()))
                return error("LoadBlockIndex() : FindBlockPos failed");
            if (!WriteBlockToDisk(block, blockPos))
                return error("LoadBlockIndex() : writing genesis block to disk failed");
            CBlockIndex* pindex = AddToBlockIndex(block);
            if (!ReceivedBlockTransactions(block, state, pindex, blockPos))
                return error("LoadBlockIndex() : genesis block not accepted");
            // Force a chainstate write so that when we VerifyDB in a moment, it doesnt check stale data
            return FlushStateToDisk(state, FLUSH_STATE_ALWAYS);
        } catch (const std::runtime_error& e) {
            return error("LoadBlockIndex() : failed to initialize block database: %s", e.what());
        }
    }

    return true;
}



bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp)
{
    // Map of disk positions for blocks with unknown parent (only used for reindex)
    static std::multimap<uint256, CDiskBlockPos> mapBlocksUnknownParent;
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    try {
        // This takes over fileIn and calls fclose() on it in the CBufferedFile destructor
        CBufferedFile blkdat(fileIn, 2 * MAX_BLOCK_SIZE_CURRENT, MAX_BLOCK_SIZE_CURRENT + 8, SER_DISK, CLIENT_VERSION);
        uint64_t nRewind = blkdat.GetPos();
        while (!blkdat.eof()) {
            boost::this_thread::interruption_point();

            blkdat.SetPos(nRewind);
            nRewind++;         // start one byte further next time, in case of failure
            blkdat.SetLimit(); // remove former limit
            unsigned int nSize = 0;
            try {
                // locate a header
                unsigned char buf[MESSAGE_START_SIZE];
                blkdat.FindByte(Params().MessageStart()[0]);
                nRewind = blkdat.GetPos() + 1;
                blkdat >> FLATDATA(buf);
                if (memcmp(buf, Params().MessageStart(), MESSAGE_START_SIZE))
                    continue;
                // read size
                blkdat >> nSize;
                if (nSize < 80 || nSize > MAX_BLOCK_SIZE_CURRENT)
                    continue;
            } catch (const std::exception&) {
                // no valid block header found; don't complain
                break;
            }
            try {
                // read block
                uint64_t nBlockPos = blkdat.GetPos();
                if (dbp)
                    dbp->nPos = nBlockPos;
                blkdat.SetLimit(nBlockPos + nSize);
                blkdat.SetPos(nBlockPos);
                CBlock block;
                blkdat >> block;
                nRewind = blkdat.GetPos();

                // detect out of order blocks, and store them for later
                uint256 hash = block.GetHash();
                if (hash != Params().GetConsensus().hashGenesisBlock && mapBlockIndex.find(block.hashPrevBlock) == mapBlockIndex.end()) {
                    LogPrint(BCLog::REINDEX, "%s: Out of order block %s, parent %s not known\n", __func__,
                            hash.GetHex(), block.hashPrevBlock.GetHex());
                    if (dbp)
                        mapBlocksUnknownParent.insert(std::make_pair(block.hashPrevBlock, *dbp));
                    continue;
                }

                // process in case the block isn't known yet
                if (mapBlockIndex.count(hash) == 0 || (mapBlockIndex[hash]->nStatus & BLOCK_HAVE_DATA) == 0) {
                    CValidationState state;
                    if (ProcessNewBlock(state, nullptr, &block, dbp, nullptr))
                        nLoaded++;
                    if (state.IsError())
                        break;
                } else if (hash != Params().GetConsensus().hashGenesisBlock && mapBlockIndex[hash]->nHeight % 1000 == 0) {
                    LogPrintf("Block Import: already had block %s at height %d\n", hash.ToString(), mapBlockIndex[hash]->nHeight);
                }

                // Recursively process earlier encountered successors of this block
                std::deque<uint256> queue;
                queue.push_back(hash);
                while (!queue.empty()) {
                    uint256 head = queue.front();
                    queue.pop_front();
                    std::pair<std::multimap<uint256, CDiskBlockPos>::iterator, std::multimap<uint256, CDiskBlockPos>::iterator> range = mapBlocksUnknownParent.equal_range(head);
                    while (range.first != range.second) {
                        std::multimap<uint256, CDiskBlockPos>::iterator it = range.first;
                        if (ReadBlockFromDisk(block, it->second)) {
                            LogPrintf("%s: Processing out of order child %s of %s\n", __func__, block.GetHash().ToString(),
                                head.ToString());
                            CValidationState dummy;
                            if (ProcessNewBlock(dummy, nullptr, &block, &it->second, nullptr)) {
                                nLoaded++;
                                queue.push_back(block.GetHash());
                            }
                        }
                        range.first++;
                        mapBlocksUnknownParent.erase(it);
                    }
                }
            } catch (const std::exception& e) {
                LogPrintf("%s : Deserialize or I/O error - %s", __func__, e.what());
            }
        }
    } catch (const std::runtime_error& e) {
        AbortNode(std::string("System error: ") + e.what());
    }
    if (nLoaded > 0)
        LogPrintf("Loaded %i blocks from external file in %dms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

void CheckBlockIndex()
{
    if (!fCheckBlockIndex) {
        return;
    }

    LOCK(cs_main);

    // During a reindex, we read the genesis block and call CheckBlockIndex before ActivateBestChain,
    // so we have the genesis block in mapBlockIndex but no active chain.  (A few of the tests when
    // iterating the block tree require that chainActive has been initialized.)
    if (chainActive.Height() < 0) {
        assert(mapBlockIndex.size() <= 1);
        return;
    }

    // Build forward-pointing map of the entire block tree.
    std::multimap<CBlockIndex*, CBlockIndex*> forward;
    for (BlockMap::iterator it = mapBlockIndex.begin(); it != mapBlockIndex.end(); it++) {
        forward.insert(std::make_pair(it->second->pprev, it->second));
    }

    assert(forward.size() == mapBlockIndex.size());

    std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangeGenesis = forward.equal_range(NULL);
    CBlockIndex* pindex = rangeGenesis.first->second;
    rangeGenesis.first++;
    assert(rangeGenesis.first == rangeGenesis.second); // There is only one index entry with parent NULL.

    // Iterate over the entire block tree, using depth-first search.
    // Along the way, remember whether there are blocks on the path from genesis
    // block being explored which are the first to have certain properties.
    size_t nNodes = 0;
    int nHeight = 0;
    CBlockIndex* pindexFirstInvalid = NULL;         // Oldest ancestor of pindex which is invalid.
    CBlockIndex* pindexFirstMissing = NULL;         // Oldest ancestor of pindex which does not have BLOCK_HAVE_DATA.
    CBlockIndex* pindexFirstNotTreeValid = NULL;    // Oldest ancestor of pindex which does not have BLOCK_VALID_TREE (regardless of being valid or not).
    CBlockIndex* pindexFirstNotChainValid = NULL;   // Oldest ancestor of pindex which does not have BLOCK_VALID_CHAIN (regardless of being valid or not).
    CBlockIndex* pindexFirstNotScriptsValid = NULL; // Oldest ancestor of pindex which does not have BLOCK_VALID_SCRIPTS (regardless of being valid or not).
    while (pindex != NULL) {
        nNodes++;
        if (pindexFirstInvalid == NULL && pindex->nStatus & BLOCK_FAILED_VALID) pindexFirstInvalid = pindex;
        if (pindexFirstMissing == NULL && !(pindex->nStatus & BLOCK_HAVE_DATA)) pindexFirstMissing = pindex;
        if (pindex->pprev != NULL && pindexFirstNotTreeValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_TREE) pindexFirstNotTreeValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotChainValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_CHAIN) pindexFirstNotChainValid = pindex;
        if (pindex->pprev != NULL && pindexFirstNotScriptsValid == NULL && (pindex->nStatus & BLOCK_VALID_MASK) < BLOCK_VALID_SCRIPTS) pindexFirstNotScriptsValid = pindex;

        // Begin: actual consistency checks.
        if (pindex->pprev == NULL) {
            // Genesis block checks.
            assert(pindex->GetBlockHash() == Params().GetConsensus().hashGenesisBlock); // Genesis block's hash must match.
            assert(pindex == chainActive.Genesis());                       // The current active chain's genesis block must be this block.
        }
        // HAVE_DATA is equivalent to VALID_TRANSACTIONS and equivalent to nTx > 0 (we stored the number of transactions in the block)
        assert(!(pindex->nStatus & BLOCK_HAVE_DATA) == (pindex->nTx == 0));
        assert(((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TRANSACTIONS) == (pindex->nTx > 0));
        if (pindex->nChainTx == 0) assert(pindex->nSequenceId == 0); // nSequenceId can't be set for blocks that aren't linked
        // All parents having data is equivalent to all parents being VALID_TRANSACTIONS, which is equivalent to nChainTx being set.
        assert((pindexFirstMissing != NULL) == (pindex->nChainTx == 0));                                             // nChainTx == 0 is used to signal that all parent block's transaction data is available.
        assert(pindex->nHeight == nHeight);                                                                          // nHeight must be consistent.
        assert(pindex->pprev == NULL || pindex->nChainWork >= pindex->pprev->nChainWork);                            // For every block except the genesis block, the chainwork must be larger than the parent's.
        assert(nHeight < 2 || (pindex->pskip && (pindex->pskip->nHeight < nHeight)));                                // The pskip pointer must point back for all but the first 2 blocks.
        assert(pindexFirstNotTreeValid == NULL);                                                                     // All mapBlockIndex entries must at least be TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_TREE) assert(pindexFirstNotTreeValid == NULL);       // TREE valid implies all parents are TREE valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_CHAIN) assert(pindexFirstNotChainValid == NULL);     // CHAIN valid implies all parents are CHAIN valid
        if ((pindex->nStatus & BLOCK_VALID_MASK) >= BLOCK_VALID_SCRIPTS) assert(pindexFirstNotScriptsValid == NULL); // SCRIPTS valid implies all parents are SCRIPTS valid
        if (pindexFirstInvalid == NULL) {
            // Checks for not-invalid blocks.
            assert((pindex->nStatus & BLOCK_FAILED_MASK) == 0); // The failed mask cannot be set for blocks without invalid parents.
        }
        if (!CBlockIndexWorkComparator()(pindex, chainActive.Tip()) && pindexFirstMissing == NULL) {
            if (pindexFirstInvalid == NULL) { // If this block sorts at least as good as the current tip and is valid, it must be in setBlockIndexCandidates.
                assert(setBlockIndexCandidates.count(pindex));
            }
        } else { // If this block sorts worse than the current tip, it cannot be in setBlockIndexCandidates.
            assert(setBlockIndexCandidates.count(pindex) == 0);
        }
        // Check whether this block is in mapBlocksUnlinked.
        std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangeUnlinked = mapBlocksUnlinked.equal_range(pindex->pprev);
        bool foundInUnlinked = false;
        while (rangeUnlinked.first != rangeUnlinked.second) {
            assert(rangeUnlinked.first->first == pindex->pprev);
            if (rangeUnlinked.first->second == pindex) {
                foundInUnlinked = true;
                break;
            }
            rangeUnlinked.first++;
        }
        if (pindex->pprev && pindex->nStatus & BLOCK_HAVE_DATA && pindexFirstMissing != NULL) {
            if (pindexFirstInvalid == NULL) { // If this block has block data available, some parent doesn't, and has no invalid parents, it must be in mapBlocksUnlinked.
                assert(foundInUnlinked);
            }
        } else { // If this block does not have block data available, or all parents do, it cannot be in mapBlocksUnlinked.
            assert(!foundInUnlinked);
        }
        // assert(pindex->GetBlockHash() == pindex->GetBlockHeader().GetHash()); // Perhaps too slow
        // End: actual consistency checks.

        // Try descending into the first subnode.
        std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> range = forward.equal_range(pindex);
        if (range.first != range.second) {
            // A subnode was found.
            pindex = range.first->second;
            nHeight++;
            continue;
        }
        // This is a leaf node.
        // Move upwards until we reach a node of which we have not yet visited the last child.
        while (pindex) {
            // We are going to either move to a parent or a sibling of pindex.
            // If pindex was the first with a certain property, unset the corresponding variable.
            if (pindex == pindexFirstInvalid) pindexFirstInvalid = NULL;
            if (pindex == pindexFirstMissing) pindexFirstMissing = NULL;
            if (pindex == pindexFirstNotTreeValid) pindexFirstNotTreeValid = NULL;
            if (pindex == pindexFirstNotChainValid) pindexFirstNotChainValid = NULL;
            if (pindex == pindexFirstNotScriptsValid) pindexFirstNotScriptsValid = NULL;
            // Find our parent.
            CBlockIndex* pindexPar = pindex->pprev;
            // Find which child we just visited.
            std::pair<std::multimap<CBlockIndex*, CBlockIndex*>::iterator, std::multimap<CBlockIndex*, CBlockIndex*>::iterator> rangePar = forward.equal_range(pindexPar);
            while (rangePar.first->second != pindex) {
                assert(rangePar.first != rangePar.second); // Our parent must have at least the node we're coming from as child.
                rangePar.first++;
            }
            // Proceed to the next one.
            rangePar.first++;
            if (rangePar.first != rangePar.second) {
                // Move to the sibling.
                pindex = rangePar.first->second;
                break;
            } else {
                // Move up further.
                pindex = pindexPar;
                nHeight--;
                continue;
            }
        }
    }

    // Check that we actually traversed the entire map.
    assert(nNodes == forward.size());
}


std::string GetWarnings(std::string strFor)
{
    std::string strStatusBar;
    std::string strRPC;

    if (!CLIENT_VERSION_IS_RELEASE)
        strStatusBar = _("This is a pre-release test build - use at your own risk - do not use for staking or merchant applications!");

    if (GetBoolArg("-testsafemode", DEFAULT_TESTSAFEMODE))
        strStatusBar = strRPC = "testsafemode enabled";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "") {
        strStatusBar = strMiscWarning;
    }

    if (fLargeWorkForkFound) {
        strStatusBar = strRPC = _("Warning: The network does not appear to fully agree! Some miners appear to be experiencing issues.");
    } else if (fLargeWorkInvalidChainFound) {
        strStatusBar = strRPC = _("Warning: We do not appear to fully agree with our peers! You may need to upgrade, or other nodes may need to upgrade.");
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}


//////////////////////////////////////////////////////////////////////////////
//
// Messages
//

class CMainCleanup
{
public:
    CMainCleanup() {}
    ~CMainCleanup()
    {
        // block headers
        BlockMap::iterator it1 = mapBlockIndex.begin();
        for (; it1 != mapBlockIndex.end(); it1++)
            delete (*it1).second;
        mapBlockIndex.clear();

        // orphan transactions
        mapOrphanTransactions.clear();
        mapOrphanTransactionsByPrev.clear();
    }
} instance_of_cmaincleanup;
