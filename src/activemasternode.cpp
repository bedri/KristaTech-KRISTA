// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"

#include "addrman.h"
#include "evo/deterministicmns.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "netbase.h"
#include "net.h"
#include "protocol.h"
#include "script/standard.h"
#include "script/mescal.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "adam.h"
#include "utilstrencodings.h"

//
// Bootup the Masternode, look for the collateral in README.md and register on the network.
//
void CActiveMasternode::ManageStatus()
{
    std::string errorMessage;

    if (!fMasterNode) return;

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStatus() - Begin\n");

    //need correct blocks to send ping
    if (!Params().IsRegTestNet() && !masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        LogPrintf("CActiveMasternode::ManageStatus() - %s\n", GetStatusMessage());
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) status = ACTIVE_MASTERNODE_INITIAL;

    if (deterministicMNManager) {
        const dip3::CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();
        ManageStatusDip3(mnList);
    }
}

void CActiveMasternode::ManageStatusDip3(const dip3::CDeterministicMNList& mnList)
{
    if (!fMasterNode) return;

    LogPrint(BCLog::MASTERNODE, "CActiveMasternode::ManageStatusDip3() - Begin (%s)\n", strAlias);

    if (!Params().IsRegTestNet() && !masternodeSync.IsBlockchainSynced()) {
        status = ACTIVE_MASTERNODE_SYNC_IN_PROCESS;
        return;
    }

    if (status == ACTIVE_MASTERNODE_SYNC_IN_PROCESS) status = ACTIVE_MASTERNODE_INITIAL;

    const int prevStatus = status;
    const std::string prevReason = notCapableReason;

    if (!blsKeyMasternode.IsValid()) {
        vin = nullopt;
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Hot node missing valid operator BLS key.";
        if (prevStatus != status || prevReason != notCapableReason) {
            LogPrintf("CActiveMasternode::ManageStatusDip3() - Hot node '%s': %s\n", strAlias, notCapableReason);
        }
        return;
    }

    const CBLSPubKey opBlsPubKey(blsKeyMasternode);
    if (!opBlsPubKey.IsValid()) {
        vin = nullopt;
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Hot node operator BLS public key is invalid.";
        if (prevStatus != status || prevReason != notCapableReason) {
            LogPrintf("CActiveMasternode::ManageStatusDip3() - Hot node '%s': %s\n", strAlias, notCapableReason);
        }
        return;
    }

    const dip3::CDeterministicMN* dmn = mnList.GetMNByOperator(opBlsPubKey);
    if (!dmn) {
        const bool fWasStarted = (status == ACTIVE_MASTERNODE_STARTED);
        vin = nullopt;
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Hot node, waiting for on-chain ProRegTx activation.";
        if (fWasStarted) {
            LogPrintf("CActiveMasternode::ManageStatusDip3() - Hot node '%s' deactivated: %s\n", strAlias, notCapableReason);
        }
        return;
    }

    if (dmn->state.IsRevoked()) {
        vin = CTxIn(dmn->collateralOutpoint);
        service = dmn->state.addr;
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Masternode operator revoked on-chain (ProUpRevTx).";
        if (prevStatus != status || prevReason != notCapableReason) {
            LogPrintf("CActiveMasternode::ManageStatusDip3() - Hot node '%s': %s\n", strAlias, notCapableReason);
        }
        return;
    }

    if (dmn->state.IsBanned()) {
        vin = CTxIn(dmn->collateralOutpoint);
        service = dmn->state.addr;
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "Masternode PoSe-banned on-chain.";
        if (prevStatus != status || prevReason != notCapableReason) {
            LogPrintf("CActiveMasternode::ManageStatusDip3() - Hot node '%s': %s\n", strAlias, notCapableReason);
        }
        return;
    }

    const bool fChanged = (status != ACTIVE_MASTERNODE_STARTED ||
                           vin == nullopt ||
                           vin->prevout != dmn->collateralOutpoint ||
                           service != dmn->state.addr);
    vin = CTxIn(dmn->collateralOutpoint);
    service = dmn->state.addr;
    status = ACTIVE_MASTERNODE_STARTED;
    notCapableReason.clear();
    if (fChanged) {
        LogPrintf("CActiveMasternode::ManageStatusDip3() - KTDIP3 Hot Node '%s' active: collateral=%s addr=%s\n",
                  strAlias, dmn->collateralOutpoint.ToString(), dmn->state.addr.ToString());
    }
}

void CActiveMasternode::ResetStatus()
{
    status = ACTIVE_MASTERNODE_INITIAL;
    ManageStatus();
}

std::string CActiveMasternode::GetStatusMessage() const
{
    switch (status) {
    case ACTIVE_MASTERNODE_INITIAL:
        return "Node just started, not yet activated";
    case ACTIVE_MASTERNODE_SYNC_IN_PROCESS:
        return "Sync in progress. Must wait until sync is complete to start Masternode";
    case ACTIVE_MASTERNODE_NOT_CAPABLE:
        return "Not capable masternode: " + notCapableReason;
    case ACTIVE_MASTERNODE_STARTED:
        return "Masternode successfully started";
    default:
        return "unknown";
    }
}

