// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"

#include "addrman.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "messagesigner.h"
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

    if (status == ACTIVE_MASTERNODE_INITIAL) {
        CMasternode* pmn;
        pmn = mnodeman.Find(pubKeyMasternode);
        if (pmn != nullptr) {
            pmn->Check();
            if (pmn->IsEnabled() && pmn->protocolVersion == PROTOCOL_VERSION)
                EnableHotColdMasterNode(pmn->vin, pmn->addr);
        }
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        // Set defaults
        status = ACTIVE_MASTERNODE_NOT_CAPABLE;
        notCapableReason = "";

        if (pwalletMain->IsLocked()) {
            notCapableReason = "Wallet is locked.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (pwalletMain->GetAvailableBalance() < CMasternode::GetMasternodeNodeCollateral(chainActive.Height())) {
            notCapableReason = "Hot node, waiting for remote activation.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        if (!GetLocal(service)) {
            notCapableReason = "Can't detect external address.";
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }

        // The service needs the correct default port to work properly
        if (!CMasternodeBroadcast::CheckDefaultPort(service, errorMessage, "CActiveMasternode::ManageStatus()"))
            return;

        LogPrintf("CActiveMasternode::ManageStatus() - Checking inbound connection to '%s'\n", service.ToString());

        CAddress addr(service, NODE_NETWORK);
        if (!IsLocal(addr) && !g_connman->OpenNetworkConnection(addr, true, nullptr)) {
            notCapableReason = "Could not connect to " + service.ToString();
            LogPrintf("CActiveMasternode::ManageStatus() - not capable: %s\n", notCapableReason);
            return;
        }
    }

    //send to all peers
    if (!SendMasternodePing(errorMessage)) {
        LogPrintf("CActiveMasternode::ManageStatus() - Error on Ping: %s\n", errorMessage);
    } else {
        std::string onChainError;
        if (!SendOnChainPing(onChainError)) {
            LogPrintf("CActiveMasternode::ManageStatus() - Error on On-Chain Ping: %s\n", onChainError);
        }
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

bool CActiveMasternode::SendMasternodePing(std::string& errorMessage)
{
    if (vin == nullopt) {
        errorMessage = "Active Masternode not initialized";
        return false;
    }

    if (status != ACTIVE_MASTERNODE_STARTED) {
        errorMessage = "Masternode is not in a running status";
        return false;
    }

    CPubKey pubKeyMasternode;
    CKey keyMasternode;

    if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        errorMessage = "Error upon calling GetKeysFromSecret.\n";
        return false;
    }

    CMasternodePing mnp(*vin);
    if (!mnp.Sign(keyMasternode, pubKeyMasternode)) {
        errorMessage = "Couldn't sign Masternode Ping";
        return false;
    }

    CMasternode* pmn = mnodeman.Find(*vin);
    if (!pmn) pmn = mnodeman.Find(pubKeyMasternode);
    if (pmn != NULL) {
        if (pmn->IsPingedWithin(MASTERNODE_PING_SECONDS, mnp.sigTime)) {
            errorMessage = "Too early to send Masternode Ping (Ignoring)";
            return true;
        }
    } else {
        errorMessage = "Masternode List doesn't include our Masternode yet, retrying ping... " + vin->ToString();
        return false;
    }

    LogPrintf("CActiveMasternode::SendMasternodePing() - Relay Masternode Ping vin = %s\n", vin->ToString());

    // Update lastPing for our masternode in Masternode list
    pmn->lastPing = mnp;
    mnodeman.mapSeenMasternodePing.insert(std::make_pair(mnp.GetHash(), mnp));

    //mnodeman.mapSeenMasternodeBroadcast.lastPing is probably outdated, so we'll update it
    CMasternodeBroadcast mnb(*pmn);
    uint256 hash = mnb.GetHash();
    if (mnodeman.mapSeenMasternodeBroadcast.count(hash)) mnodeman.mapSeenMasternodeBroadcast[hash].lastPing = mnp;

    mnp.Relay();
    return true;
}

// when starting a Masternode, this can enable to run as a hot wallet with no funds
bool CActiveMasternode::EnableHotColdMasterNode(CTxIn& newVin, CService& newService)
{
    if (!fMasterNode) return false;

    status = ACTIVE_MASTERNODE_STARTED;

    //The values below are needed for signing mnping messages going forward
    vin = newVin;
    service = newService;

    LogPrintf("CActiveMasternode::EnableHotColdMasterNode() - Enabled! You may shut down the cold daemon.\n");

    return true;
}

bool CActiveMasternode::SendOnChainPing(std::string& errorMessage)
{
    if (vin == nullopt) {
        errorMessage = "Active Masternode not initialized";
        return false;
    }

    CPubKey pubKeyMasternode;
    CKey keyMasternode;
    if (!CMessageSigner::GetKeysFromSecret(strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
        errorMessage = "Error upon calling GetKeysFromSecret.";
        return false;
    }

    // Ensure key is imported into the wallet
    if (pwalletMain) {
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->HaveKey(keyMasternode.GetPubKey().GetID())) {
            pwalletMain->AddKey(keyMasternode);
        }
    }

    // Check last ping height to prevent spamming
    int nTipHeight = 0;
    {
        LOCK(cs_main);
        if (chainActive.Tip()) {
            nTipHeight = chainActive.Tip()->nHeight;
        }
    }
    
    auto it = mapMasternodeLastActiveHeight.find(pubKeyMasternode);
    if (it != mapMasternodeLastActiveHeight.end() && (nTipHeight - it->second < 10)) {
        // Too early to send another on-chain ping
        return true;
    }

    // Parse collateral outpoint
    COutPoint collateralOutpoint = vin->prevout;

    // Search wallet for Ping UTXO
    std::vector<COutput> vCoins;
    if (pwalletMain) {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->AvailableCoins(&vCoins);
    }
    
    COutput pingOutput(nullptr, 0, 0, false, false);
    bool found = false;
    CScript pingScript = GetMasternodePingScript(pubKeyMasternode);
    for (const auto& out : vCoins) {
        if (out.tx->vout[out.i].scriptPubKey == pingScript) {
            pingOutput = out;
            found = true;
            break;
        }
    }
    
    CMutableTransaction tx;
    CAmount nAmount = 0;
    std::vector<COutput> selectedCoins;
    
    if (found) {
        selectedCoins.push_back(pingOutput);
        nAmount = pingOutput.tx->vout[pingOutput.i].nValue;
    } else {
        // Find any available coin with at least 0.2 KRISTA
        for (const auto& out : vCoins) {
            if (out.tx->vout[out.i].nValue >= 0.2 * COIN) {
                selectedCoins.push_back(out);
                nAmount = out.tx->vout[out.i].nValue;
                break;
            }
        }
    }
    
    if (selectedCoins.empty()) {
        errorMessage = "No funding UTXO found for on-chain ping (need at least 0.2 KRISTA)";
        return false;
    }
    
    tx.vin.push_back(CTxIn(selectedCoins[0].tx->GetHash(), selectedCoins[0].i));
    
    CAmount nPingAmount = 0.1 * COIN;
    CAmount nFee = 0.05 * COIN;
    if (nAmount < nPingAmount + nFee) {
        errorMessage = "Insufficient funds in selected UTXO for on-chain ping";
        return false;
    }
    
    // Output 0: The Ping Script destination
    tx.vout.push_back(CTxOut(nPingAmount, pingScript));
    
    // Output 1: OP_RETURN containing the collateral outpoint
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << collateralOutpoint;
    CScript opReturnScript = CScript() << OP_RETURN << ToByteVector(ss);
    tx.vout.push_back(CTxOut(0, opReturnScript));
    
    // Output 2: Change output
    CAmount nChange = nAmount - nPingAmount - nFee;
    if (nChange > 0) {
        CPubKey changePubKey;
        if (pwalletMain->GetKeyFromPool(changePubKey)) {
            tx.vout.push_back(CTxOut(nChange, GetScriptForDestination(changePubKey.GetID())));
        } else {
            errorMessage = "Failed to get change address from keypool";
            return false;
        }
    }
    
    // Sign the input
    if (!SignSignature(*pwalletMain, *selectedCoins[0].tx, tx, 0, SIGHASH_ALL)) {
        errorMessage = "SignSignature failed for on-chain ping";
        return false;
    }
    
    // Commit and broadcast
    CWalletTx wtx(pwalletMain, tx);
    CReserveKey reservekey(pwalletMain);
    CWallet::CommitResult res = pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get());
    if (res.status != CWallet::CommitStatus::OK) {
        errorMessage = "CommitTransaction failed for on-chain ping";
        return false;
    }
    
    LogPrintf("CActiveMasternode::SendOnChainPing - On-chain ping transaction %s broadcasted successfully for MN key %s\n",
        tx.GetHash().ToString(), pubKeyMasternode.GetID().ToString());
    return true;
}
