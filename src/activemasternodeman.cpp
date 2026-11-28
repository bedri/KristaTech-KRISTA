// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "activemasternodeman.h"
#include "activemasternodeconfig.h"
#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "main.h"
#include "init.h"
#include "masternode.h"
#include "masternode-sync.h"
#include "util.h"
#include "util/threadnames.h"
#include "utiltime.h"

#include <boost/thread.hpp>
#include <vector>

CActiveMasternodeMan amnodeman;

void CActiveMasternodeMan::ManageStatus() {
    if (!fMasterNode) return;
    if (deterministicMNManager) {
        const dip3::CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();
        LOCK(cs);
        for (auto& amn : vActiveMasternodes) {
            amn.ManageStatusDip3(mnList);
        }
        return;
    }
    LOCK(cs);
    for (auto& amn : vActiveMasternodes) {
        amn.ManageStatus();
    }
}

void CActiveMasternodeMan::ResetStatus() {
    LOCK(cs);
    for(auto& amn : vActiveMasternodes) {
        amn.ResetStatus();
    }
}

std::vector<CActiveMasternode> CActiveMasternodeMan::GetActiveMasternodesCopy() const {
    LOCK(cs);
    return vActiveMasternodes;
}

void CActiveMasternodeMan::Add(CActiveMasternode activeMasternode) {
    LOCK(cs);
    vActiveMasternodes.push_back(activeMasternode);
}

void ThreadCheckMasternodes()
{
    if (fLiteMode) return; //disable all Masternode related functionality

    util::ThreadRename("kristatech-masternodeman");
    LogPrintf("Masternodes thread started\n");

    unsigned int c = 0;
    uint256 lastDip3TipHash;

    try {
        while (true) {
            if (ShutdownRequested()) {
                break;
            }

            MilliSleep(1000);
            boost::this_thread::interruption_point();

            {
                LOCK(cs_main);
                masternodeSync.Process();
            }

            if (fMasterNode && deterministicMNManager &&
                (Params().IsRegTestNet() || masternodeSync.IsBlockchainSynced())) {
                uint256 tipHash;
                {
                    LOCK(cs_main);
                    if (chainActive.Tip()) {
                        tipHash = chainActive.Tip()->GetBlockHash();
                    }
                }
                if (!tipHash.IsNull() && tipHash != lastDip3TipHash) {
                    lastDip3TipHash = tipHash;
                    amnodeman.ManageStatus();
                }
            }

            if (masternodeSync.IsBlockchainSynced()) {
                c++;
                if (c % MASTERNODE_PING_SECONDS == 1) {
                    amnodeman.ManageStatus();
                }
            }
        }
    } catch (boost::thread_interrupted&) {
        // nothing, thread interrupted.
    }
}
