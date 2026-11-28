// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_SYNC_H
#define MASTERNODE_SYNC_H

#include <stdint.h>
#include <string>

#define MASTERNODE_SYNC_INITIAL 0
#define MASTERNODE_SYNC_FAILED 998
#define MASTERNODE_SYNC_FINISHED 999

#define MASTERNODE_SYNC_TIMEOUT 5
#define MASTERNODE_SYNC_THRESHOLD 2

class CMasternodeSync;
extern CMasternodeSync masternodeSync;

/** Tracks blockchain readiness for masternode activation.
 */
class CMasternodeSync
{
private:
    int64_t nAssetSyncStarted;
    int64_t lastProcess;
    bool fBlockchainSynced;

public:
    int64_t lastFailure;
    int nCountFailures;

    int RequestedMasternodeAssets;
    int RequestedMasternodeAttempt;

    CMasternodeSync();

    void Reset();
    void Process();
    bool IsSynced();
    bool NotCompleted();
    bool IsBlockchainSynced();
    void GetNextAsset();
    std::string GetSyncStatus();
};

#endif // MASTERNODE_SYNC_H
