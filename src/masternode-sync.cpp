// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// clang-format off
#include "main.h"
#include "activemasternodeman.h"
#include "chainparams.h"
#include "masternode-sync.h"
#include "net.h"
#include "util.h"
// clang-format on

CMasternodeSync masternodeSync;

CMasternodeSync::CMasternodeSync()
{
    Reset();
}

bool CMasternodeSync::IsSynced()
{
    return RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED;
}

bool CMasternodeSync::NotCompleted()
{
    return !IsSynced();
}

bool CMasternodeSync::IsBlockchainSynced()
{
    int64_t now = GetTime();

    // if the last call to this function was more than 60 minutes ago (client was in sleep mode) reset the sync process
    if (now > lastProcess + 60 * 60) {
        Reset();
        fBlockchainSynced = false;
    }
    lastProcess = now;

    if (fImporting || fReindex) {
        fBlockchainSynced = false;
        return false;
    }

    if (IsInitialBlockDownload()) {
        fBlockchainSynced = false;
        return false;
    }

    int64_t blockTime = 0;
    int tipHeight = 0;
    {
        LOCK(cs_main);
        CBlockIndex *pindex = chainActive.Tip();
        if (pindex == nullptr) {
            fBlockchainSynced = false;
            return false;
        }
        blockTime = pindex->nTime;
        tipHeight = pindex->nHeight;
    }

    int maxPeerHeight = 0;
    if (g_connman) {
        g_connman->ForEachNode([&maxPeerHeight](CNode* pnode) {
            if (pnode->nStartingHeight > maxPeerHeight)
                maxPeerHeight = pnode->nStartingHeight;
        });
    }

    if (maxPeerHeight > 0 && tipHeight < maxPeerHeight) {
        fBlockchainSynced = false;
        return false;
    }

    // Check if the best valid header is beyond our tip
    {
        LOCK(cs_main);
        if (pindexBestHeader && pindexBestHeader->IsValid(BLOCK_VALID_TREE) && !(pindexBestHeader->nStatus & BLOCK_FAILED_MASK) && pindexBestHeader->nHeight > tipHeight) {
            fBlockchainSynced = false;
            return false;
        }
    }

    if (fBlockchainSynced) return true;

    if (blockTime + 60 * 60 < lastProcess) {
        LOCK(cs_main);
        CBlockIndex* pindexHeader = pindexBestHeader;
        if (pindexHeader && (!pindexHeader->IsValid(BLOCK_VALID_TREE) || (pindexHeader->nStatus & BLOCK_FAILED_MASK))) {
            pindexHeader = chainActive.Tip();
        }
        if (!(pindexHeader && chainActive.Tip() == pindexHeader && tipHeight >= maxPeerHeight && tipHeight > 0)) {
            return false;
        }
    }

    fBlockchainSynced = true;

    return true;
}

void CMasternodeSync::Reset()
{
    fBlockchainSynced = false;
    lastProcess = 0;
    lastFailure = 0;
    nCountFailures = 0;
    RequestedMasternodeAssets = MASTERNODE_SYNC_INITIAL;
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

void CMasternodeSync::GetNextAsset()
{
    switch (RequestedMasternodeAssets) {
    case (MASTERNODE_SYNC_INITIAL):
    case (MASTERNODE_SYNC_FAILED): // should never be used here actually, use Reset() instead
        LogPrintf("CMasternodeSync::GetNextAsset - Sync has finished\n");
        RequestedMasternodeAssets = MASTERNODE_SYNC_FINISHED;
        break;
    }
    RequestedMasternodeAttempt = 0;
    nAssetSyncStarted = GetTime();
}

std::string CMasternodeSync::GetSyncStatus()
{
    switch (masternodeSync.RequestedMasternodeAssets) {
    case MASTERNODE_SYNC_INITIAL:
        return _("MNs synchronization pending...");
    case MASTERNODE_SYNC_FAILED:
        return _("Synchronization failed");
    case MASTERNODE_SYNC_FINISHED:
        return _("Synchronization finished");
    }
    return "";
}

void CMasternodeSync::Process()
{
    static int tick = 0;
    const bool isRegTestNet = Params().IsRegTestNet();

    if (tick++ % MASTERNODE_SYNC_TIMEOUT != 0) return;

    if (RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED) {
        return;
    }

    //try syncing again
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED && lastFailure + (1 * 60) < GetTime()) {
        Reset();
    } else if (RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED) {
        return;
    }

    LogPrint(BCLog::MASTERNODE, "CMasternodeSync::Process() - tick %d RequestedMasternodeAssets %d\n", tick, RequestedMasternodeAssets);

    const int nPrevAsset = RequestedMasternodeAssets;
    if (RequestedMasternodeAssets == MASTERNODE_SYNC_INITIAL) {
        int nTotalPeers = g_connman ? g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) : 0;
        if (isRegTestNet || (IsBlockchainSynced() && nTotalPeers > 0)) {
            GetNextAsset();
        }
    }

    if (nPrevAsset != MASTERNODE_SYNC_FINISHED && RequestedMasternodeAssets == MASTERNODE_SYNC_FINISHED) {
        amnodeman.ManageStatus();
    }
}


