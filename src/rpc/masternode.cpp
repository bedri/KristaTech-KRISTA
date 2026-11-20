// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "activemasternodeman.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "netbase.h"
#include "rpc/server.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"
#include "crypto/bls.h"
#include "llmq.h"
#include "adam.h"
#include "base58.h"
#include "evo/deterministicmns.h"
#include "script/sign.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <univalue.h>

#include <boost/tokenizer.hpp>

UniValue listmasternodes(const JSONRPCRequest& request)
{
    std::string strFilter = "";

    if (request.params.size() == 1) strFilter = request.params[0].get_str();

    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listmasternodes ( \"filter\" )\n"
            "\nGet a ranked list of masternodes\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match by txhash, status, or addr.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"rank\": n,           (numeric) Masternode Rank (or 0 if not enabled)\n"
            "    \"txhash\": \"hash\",  (string) Collateral transaction hash\n"
            "    \"outidx\": n,         (numeric) Collateral transaction output index\n"
            "    \"pubkey\": \"key\",   (string) Masternode public key used for message broadcasting\n"
            "    \"status\": s,         (string) Status (ENABLED/EXPIRED/REMOVE/etc)\n"
            "    \"addr\": \"addr\",    (string) Masternode KRISTA address\n"
            "    \"ip\": \"ip\",        (string) Masternode IP address\n"
            "    \"version\": v,        (numeric) Masternode protocol version\n"
            "    \"lastseen\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) of the last seen\n"
            "    \"activetime\": ttt,   (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode has been active\n"
            "    \"lastpaid\": ttt,     (numeric) The time in seconds since epoch (Jan 1 1970 GMT) masternode was last paid\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodes", "") + HelpExampleRpc("listmasternodes", ""));

    UniValue ret(UniValue::VARR);
    int nHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nHeight < 0) return "[]";

    std::set<COutPoint> seenDip3Collaterals;
    if (deterministicMNManager) {
        const dip3::CDeterministicMNList dip3List = deterministicMNManager->GetListAtChainTip();
        int rank = 0;
        for (const auto& kv : dip3List.mapMNs) {
            const dip3::CDeterministicMN& dmn = kv.second;
            seenDip3Collaterals.insert(dmn.collateralOutpoint);

            const std::string strTxHash = dmn.collateralOutpoint.hash.ToString();
            const std::string strOwnerAddr = EncodeDestination(dmn.state.pubKeyOwner.GetID());
            const std::string strStatus = dmn.state.IsValid()
                ? "ENABLED"
                : (dmn.state.IsBanned() ? "POSE_BANNED" : "EXPIRED");
            const std::string strIp = dmn.state.addr.ToString();

            if (!strFilter.empty() &&
                strTxHash.find(strFilter) == std::string::npos &&
                strStatus.find(strFilter) == std::string::npos &&
                strOwnerAddr.find(strFilter) == std::string::npos &&
                strIp.find(strFilter) == std::string::npos &&
                dmn.proTxHash.ToString().find(strFilter) == std::string::npos) {
                continue;
            }

            std::string strHost;
            int port = 0;
            SplitHostPort(strIp, port, strHost);
            CNetAddr node;
            LookupHost(strHost.c_str(), node, false);
            const std::string strNetwork = GetNetworkName(node.GetNetwork());

            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? ++rank : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)dmn.collateralOutpoint.n));
            obj.push_back(Pair("proTxHash", dmn.proTxHash.ToString()));
            obj.push_back(Pair("pubkey", HexStr(dmn.state.blsPubKeyOperator.begin(), dmn.state.blsPubKeyOperator.end())));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", strOwnerAddr));
            obj.push_back(Pair("ip", strIp));
            obj.push_back(Pair("version", PROTOCOL_VERSION));
            obj.push_back(Pair("registeredHeight", dmn.state.nRegisteredHeight));
            obj.push_back(Pair("lastpaid", (int64_t)dmn.state.nLastPaidHeight));
            obj.push_back(Pair("dip3", true));
            ret.push_back(obj);
        }
    }

    std::vector<std::pair<int, CMasternode> > vMasternodeRanks = mnodeman.GetMasternodeRanks(nHeight);
    for (PAIRTYPE(int, CMasternode) & s : vMasternodeRanks) {
        if (seenDip3Collaterals.count(s.second.vin.prevout)) {
            continue;
        }
        UniValue obj(UniValue::VOBJ);
        std::string strVin = s.second.vin.prevout.ToStringShort();
        std::string strTxHash = s.second.vin.prevout.hash.ToString();
        uint32_t oIdx = s.second.vin.prevout.n;

        CMasternode* mn = mnodeman.Find(s.second.vin);

        if (mn != NULL) {
            if (strFilter != "" && strTxHash.find(strFilter) == std::string::npos &&
                mn->Status().find(strFilter) == std::string::npos &&
                EncodeDestination(mn->pubKeyCollateralAddress.GetID()).find(strFilter) == std::string::npos) continue;

            std::string strStatus = mn->Status();
            std::string strHost;
            int port;
            SplitHostPort(mn->addr.ToString(), port, strHost);
            CNetAddr node;
            LookupHost(strHost.c_str(), node, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("pubkey", HexStr(mn->pubKeyMasternode)));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", EncodeDestination(mn->pubKeyCollateralAddress.GetID())));
            obj.push_back(Pair("ip", mn->addr.ToString()));
            obj.push_back(Pair("version", mn->protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)mn->lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn->lastPing.sigTime - mn->sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn->GetLastPaid()));

            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue getmasternodecount (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() > 0))
        throw std::runtime_error(
            "getmasternodecount\n"
            "\nGet masternode count values\n"

            "\nResult:\n"
            "{\n"
            "  \"total\": n,        (numeric) Total masternodes\n"
            "  \"stable\": n,       (numeric) Stable count\n"
            "  \"enabled\": n,      (numeric) Enabled masternodes\n"
            "  \"inqueue\": n       (numeric) Masternodes in queue\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodecount", "") + HelpExampleRpc("getmasternodecount", ""));

    UniValue obj(UniValue::VOBJ);
    int nCount = 0;
    int ipv4 = 0, ipv6 = 0, onion = 0;

    int nChainHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nChainHeight < 0) return "unknown";

    if (deterministicMNManager) {
        const dip3::CDeterministicMNList dip3List = deterministicMNManager->GetListAtChainTip();
        if (!dip3List.mapMNs.empty()) {
            int dip3Total = 0;
            int dip3Valid = 0;
            for (const auto& kv : dip3List.mapMNs) {
                const dip3::CDeterministicMN& dmn = kv.second;
                ++dip3Total;
                if (dmn.state.IsValid()) ++dip3Valid;
                if (dmn.state.addr.IsIPv4()) ++ipv4;
                else if (dmn.state.addr.IsTor()) ++onion;
                else if (dmn.state.addr.IsIPv6()) ++ipv6;
            }
            obj.push_back(Pair("total", dip3Total));
            obj.push_back(Pair("stable", dip3Valid));
            obj.push_back(Pair("enabled", dip3Valid));
            obj.push_back(Pair("inqueue", dip3Valid));
            obj.push_back(Pair("ipv4", ipv4));
            obj.push_back(Pair("ipv6", ipv6));
            obj.push_back(Pair("onion", onion));
            return obj;
        }
    }

    mnodeman.GetNextMasternodeInQueueForPayment(nChainHeight, true, nCount);
    mnodeman.CountNetworks(ActiveProtocol(), ipv4, ipv6, onion);

    obj.push_back(Pair("total", mnodeman.size()));
    obj.push_back(Pair("stable", mnodeman.stable_size()));
    obj.push_back(Pair("enabled", mnodeman.CountEnabled()));
    obj.push_back(Pair("inqueue", nCount));
    obj.push_back(Pair("ipv4", ipv4));
    obj.push_back(Pair("ipv6", ipv6));
    obj.push_back(Pair("onion", onion));

    return obj;
}

UniValue masternodecurrent (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "masternodecurrent\n"
            "\nGet current masternode winner (scheduled to be paid next).\n"

            "\nResult:\n"
            "{\n"
            "  \"protocol\": xxxx,        (numeric) Protocol version\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"pubkey\": \"xxxx\",      (string) MN Public key\n"
            "  \"lastseen\": xxx,         (numeric) Time since epoch of last seen\n"
            "  \"activeseconds\": xxx,    (numeric) Seconds MN has been active\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("masternodecurrent", "") + HelpExampleRpc("masternodecurrent", ""));

    const int nHeight = WITH_LOCK(cs_main, return chainActive.Height() + 1);
    int nCount = 0;
    CMasternode* winner = mnodeman.GetNextMasternodeInQueueForPayment(nHeight, true, nCount);
    if (winner) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("protocol", (int64_t)winner->protocolVersion));
        obj.push_back(Pair("txhash", winner->vin.prevout.hash.ToString()));
        obj.push_back(Pair("pubkey", EncodeDestination(winner->pubKeyCollateralAddress.GetID())));
        obj.push_back(Pair("lastseen", winner->lastPing.IsNull() ? winner->sigTime : (int64_t)winner->lastPing.sigTime));
        obj.push_back(Pair("activeseconds", winner->lastPing.IsNull() ? 0 : (int64_t)(winner->lastPing.sigTime - winner->sigTime)));
        return obj;
    }

    throw std::runtime_error("unknown");
}

bool StartMasternodeEntry(UniValue& statusObjRet, CMasternodeBroadcast& mnbRet, bool& fSuccessRet, const CMasternodeConfig::CMasternodeEntry& mne, std::string& errorMessage, std::string strCommand = "")
{
    int nIndex;
    if(!mne.castOutputIndex(nIndex)) {
        return false;
    }

    CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
    CMasternode* pmn = mnodeman.Find(vin);
    if (pmn != NULL) {
        if (strCommand == "missing") return false;
        if (strCommand == "disabled" && pmn->IsEnabled()) return false;
    }

    fSuccessRet = CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), errorMessage, mnbRet);

    if (fSuccessRet) {
        mnodeman.UpdateMasternodeList(mnbRet);
        // Broadcast initial on-chain ping (with MN_DELEGATE_MARKER) from the cold controller wallet
        // WITHOUT importing keyMasternode into pwalletMain (fMasterNode == false guard in SendOnChainPing).
        // This registers 0-balance remote hot Masternodes in mapMasternodeCollateralOutpoints immediately.
        CActiveMasternode tempActiveMN;
        tempActiveMN.strAlias = mne.getAlias();
        tempActiveMN.strMasterNodePrivKey = mne.getPrivKey();
        tempActiveMN.pubKeyMasternode = mnbRet.pubKeyMasternode;
        tempActiveMN.vin = mnbRet.vin;
        tempActiveMN.service = mnbRet.addr;
        std::string onChainError;
        if (!tempActiveMN.SendOnChainPing(onChainError)) {
            LogPrintf("StartMasternodeEntry: Initial on-chain ping from controller wallet skipped/failed for %s: %s\n",
                mne.getAlias(), onChainError);
        }
    }

    statusObjRet.push_back(Pair("alias", mne.getAlias()));
    statusObjRet.push_back(Pair("result", fSuccessRet ? "success" : "failed"));
    statusObjRet.push_back(Pair("error", fSuccessRet ? "" : errorMessage));

    return true;
}

void RelayMNB(CMasternodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if (fSuccess) {
        successful++;
        mnodeman.UpdateMasternodeList(mnb);
        mnb.Relay();
    } else {
        failed++;
    }
}

void RelayMNB(CMasternodeBroadcast& mnb, const bool fSucces)
{
    int successful = 0, failed = 0;
    return RelayMNB(mnb, fSucces, successful, failed);
}

void SerializeMNB(UniValue& statusObjRet, const CMasternodeBroadcast& mnb, const bool fSuccess, int& successful, int& failed)
{
    if(fSuccess) {
        successful++;
        CDataStream ssMnb(SER_NETWORK, PROTOCOL_VERSION);
        ssMnb << mnb;
        statusObjRet.push_back(Pair("hex", HexStr(ssMnb.begin(), ssMnb.end())));
    } else {
        failed++;
    }
}

void SerializeMNB(UniValue& statusObjRet, const CMasternodeBroadcast& mnb, const bool fSuccess)
{
    int successful = 0, failed = 0;
    return SerializeMNB(statusObjRet, mnb, fSuccess, successful, failed);
}

UniValue startmasternode (const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1) {
        strCommand = request.params[0].get_str();

        // Backwards compatibility with legacy 'masternode' super-command forwarder
        if (strCommand == "start") strCommand = "local";
        if (strCommand == "start-alias") strCommand = "alias";
        if (strCommand == "start-all") strCommand = "all";
        if (strCommand == "start-many") strCommand = "many";
        if (strCommand == "start-missing") strCommand = "missing";
        if (strCommand == "start-disabled") strCommand = "disabled";
    }

    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3 ||
        (request.params.size() == 2 && (strCommand != "local" && strCommand != "all" && strCommand != "many" && strCommand != "missing" && strCommand != "disabled")) ||
        (request.params.size() == 3 && strCommand != "alias"))
        throw std::runtime_error(
            "startmasternode \"local|all|many|missing|disabled|alias\" lockwallet ( \"alias\" )\n"
            "\nAttempts to start one or more masternode(s)\n"

            "\nArguments:\n"
            "1. set         (string, required) Specify which set of masternode(s) to start.\n"
            "2. lockwallet  (boolean, required) Lock wallet after completion.\n"
            "3. alias       (string) Masternode alias. Required if using 'alias' as the set.\n"

            "\nResult: (for 'local' set):\n"
            "\"status\"     (string) Masternode status message\n"

            "\nResult: (for other sets):\n"
            "{\n"
            "  \"overall\": \"xxxx\",     (string) Overall status message\n"
            "  \"detail\": [\n"
            "    {\n"
            "      \"node\": \"xxxx\",    (string) Node name or alias\n"
            "      \"result\": \"xxxx\",  (string) 'success' or 'failed'\n"
            "      \"error\": \"xxxx\"    (string) Error message, if failed\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("startmasternode", "\"alias\" \"0\" \"my_mn\"") + HelpExampleRpc("startmasternode", "\"alias\" \"0\" \"my_mn\""));

    bool fLock = (request.params[1].get_str() == "true" ? true : false);

    EnsureWalletIsUnlocked();

    {
        std::string strConfigErr;
        masternodeConfig.clear();
        masternodeConfig.read(strConfigErr);
    }

    if (strCommand == "local") {
        throw std::runtime_error("'local' disabled\n");
    }

    if (strCommand == "all" || strCommand == "many" || strCommand == "missing" || strCommand == "disabled") {
        if ((strCommand == "missing" || strCommand == "disabled") &&
            (masternodeSync.RequestedMasternodeAssets <= MASTERNODE_SYNC_LIST ||
                masternodeSync.RequestedMasternodeAssets == MASTERNODE_SYNC_FAILED)) {
            throw std::runtime_error("You can't use this command until masternode list is synced\n");
        }

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            UniValue statusObj(UniValue::VOBJ);
            CMasternodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;
            if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                continue;
            resultsObj.push_back(statusObj);
            RelayMNB(mnb, fSuccess, successful, failed);
        }
        if (fLock)
            pwalletMain->Lock();

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully started %d masternodes, failed to start %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }

    if (strCommand == "alias") {
        std::string alias = request.params[2].get_str();

        bool found = false;

        UniValue resultsObj(UniValue::VARR);
        UniValue statusObj(UniValue::VOBJ);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            if (mne.getAlias() == alias) {
                CMasternodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;
                if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                        continue;
                RelayMNB(mnb, fSuccess);
                break;
            }
        }

        if (fLock)
            pwalletMain->Lock();

        if(!found) {
            statusObj.push_back(Pair("success", false));
            statusObj.push_back(Pair("error_message", "Could not find alias in config. Verify with listmasternodeconf."));
        }

        return statusObj;
    }
    return NullUniValue;
}

UniValue createmasternodekey (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "createmasternodekey\n"
            "\nCreate a new masternode private key\n"

            "\nResult:\n"
            "\"key\"    (string) Masternode private key\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodekey", "") + HelpExampleRpc("createmasternodekey", ""));

    CKey secret;
    secret.MakeNewKey(false);

    return EncodeSecret(secret);
}

UniValue createblsprivkey (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "createblsprivkey\n"
            "\nCreate a new cryptographically secure BLS private key in hex format\n"

            "\nResult:\n"
            "\"hex\"    (string) BLS private key in hex\n"

            "\nExamples:\n" +
            HelpExampleCli("createblsprivkey", "") + HelpExampleRpc("createblsprivkey", ""));

    CBLSSecretKey blsKey;
    blsKey.MakeNewKey();
    if (!blsKey.IsValid()) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to generate BLS key");
    }

    return HexStr(blsKey.begin(), blsKey.end());
}

UniValue getmasternodeoutputs (const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getmasternodeoutputs\n"
            "\nPrint all masternode transaction outputs\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txhash\": \"xxxx\",    (string) output transaction hash\n"
            "    \"outputidx\": n       (numeric) output index number\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodeoutputs", "") + HelpExampleRpc("getmasternodeoutputs", ""));

    // Find possible candidates
    std::vector<COutput> possibleCoins;
    pwalletMain->AvailableCoins(&possibleCoins, nullptr, ONLY_10000);

    UniValue ret(UniValue::VARR);
    for (COutput& out : possibleCoins) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("txhash", out.tx->GetHash().ToString()));
        obj.push_back(Pair("outputidx", out.i));
        ret.push_back(obj);
    }

    return ret;
}

UniValue listmasternodeconf (const JSONRPCRequest& request)
{
    std::string strFilter = "";

    if (request.params.size() == 1) strFilter = request.params[0].get_str();

    if (request.fHelp || (request.params.size() > 1))
        throw std::runtime_error(
            "listmasternodeconf ( \"filter\" )\n"
            "\nPrint masternode.conf in JSON format\n"

            "\nArguments:\n"
            "1. \"filter\"    (string, optional) Filter search text. Partial match on alias, address, txHash, or status.\n"

            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"alias\": \"xxxx\",        (string) masternode alias\n"
            "    \"address\": \"xxxx\",      (string) masternode IP address\n"
            "    \"privateKey\": \"xxxx\",   (string) masternode private key\n"
            "    \"txHash\": \"xxxx\",       (string) transaction hash\n"
            "    \"outputIndex\": n,       (numeric) transaction output index\n"
            "    \"status\": \"xxxx\"        (string) masternode status\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("listmasternodeconf", "") + HelpExampleRpc("listmasternodeconf", ""));

    {
        std::string strConfigErr;
        masternodeConfig.clear();
        masternodeConfig.read(strConfigErr);
    }

    std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
    mnEntries = masternodeConfig.getEntries();

    UniValue ret(UniValue::VARR);

    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;
        CTxIn vin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(vin);

        std::string strStatus = pmn ? pmn->Status() : "MISSING";

        if (strFilter != "" && mne.getAlias().find(strFilter) == std::string::npos &&
            mne.getIp().find(strFilter) == std::string::npos &&
            mne.getTxHash().find(strFilter) == std::string::npos &&
            strStatus.find(strFilter) == std::string::npos) continue;

        UniValue mnObj(UniValue::VOBJ);
        mnObj.push_back(Pair("alias", mne.getAlias()));
        mnObj.push_back(Pair("address", mne.getIp()));
        mnObj.push_back(Pair("privateKey", mne.getPrivKey()));
        mnObj.push_back(Pair("txHash", mne.getTxHash()));
        mnObj.push_back(Pair("outputIndex", mne.getOutputIndex()));
        mnObj.push_back(Pair("status", strStatus));
        ret.push_back(mnObj);
    }

    return ret;
}

UniValue getmasternodestatus(const JSONRPCRequest& request)
{
    if (request.fHelp || (request.params.size() != 0))
        throw std::runtime_error(
            "getmasternodestatus\n"
            "\nPrint masternode status\n"

            "\nResult:\n"
            "{\n"
            "  \"txhash\": \"xxxx\",      (string) Collateral transaction hash\n"
            "  \"outputidx\": n,          (numeric) Collateral transaction output index number\n"
            "  \"netaddr\": \"xxxx\",     (string) Masternode network address\n"
            "  \"addr\": \"xxxx\",        (string) KRISTA address for masternode payments\n"
            "  \"status\": \"xxxx\",      (string) Masternode status\n"
            "  \"message\": \"xxxx\"      (string) Masternode status message\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodestatus", "") + HelpExampleRpc("getmasternodestatus", ""));

    if (!fMasterNode)
        throw JSONRPCError(RPC_MISC_ERROR, _("This is not a masternode."));

    UniValue resultsObj(UniValue::VARR);

    for (const auto& activeMasternode : amnodeman.GetActiveMasternodesCopy()) {
        if (activeMasternode.vin == nullopt) {
            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", activeMasternode.strAlias));
            mnObj.push_back(Pair("txhash", "N/A"));
            mnObj.push_back(Pair("outputidx", -1));
            mnObj.push_back(Pair("netaddr", activeMasternode.service.ToString()));
            mnObj.push_back(Pair("addr", "N/A"));
            mnObj.push_back(Pair("status", activeMasternode.GetStatus()));
            mnObj.push_back(Pair("message", activeMasternode.GetStatusMessage()));
            resultsObj.push_back(mnObj);
            continue;
        }

        const dip3::CDeterministicMNList mnList = deterministicMNManager
            ? deterministicMNManager->GetListAtChainTip()
            : dip3::CDeterministicMNList();
        const dip3::CDeterministicMN* dip3Mn = nullptr;
        if (activeMasternode.blsKeyMasternode.IsValid()) {
            dip3Mn = mnList.GetMNByOperator(CBLSPubKey(activeMasternode.blsKeyMasternode));
        }
        if (!dip3Mn) {
            dip3Mn = mnList.GetMNByCollateral(activeMasternode.vin->prevout);
        }

        if (dip3Mn) {
            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", activeMasternode.strAlias));
            mnObj.push_back(Pair("txhash", activeMasternode.vin->prevout.hash.ToString()));
            mnObj.push_back(Pair("outputidx", (uint64_t)activeMasternode.vin->prevout.n));
            mnObj.push_back(Pair("netaddr", activeMasternode.service.ToString()));
            mnObj.push_back(Pair("addr", EncodeDestination(dip3Mn->state.pubKeyOwner.GetID())));
            mnObj.push_back(Pair("status", activeMasternode.GetStatus()));
            mnObj.push_back(Pair("message", activeMasternode.GetStatusMessage() + " (KTDIP3)"));
            resultsObj.push_back(mnObj);
            continue;
        }

        CMasternode* pmn = mnodeman.Find(*(activeMasternode.vin));

        if (pmn) {
            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", activeMasternode.strAlias));
            mnObj.push_back(Pair("txhash", activeMasternode.vin->prevout.hash.ToString()));
            mnObj.push_back(Pair("outputidx", (uint64_t)activeMasternode.vin->prevout.n));
            mnObj.push_back(Pair("netaddr", activeMasternode.service.ToString()));
            mnObj.push_back(Pair("addr", EncodeDestination(pmn->pubKeyCollateralAddress.GetID())));
            mnObj.push_back(Pair("status", activeMasternode.GetStatus()));
            mnObj.push_back(Pair("message", activeMasternode.GetStatusMessage()));
            resultsObj.push_back(mnObj);
        } else {
            throw std::runtime_error("Masternode not found in the list of available masternodes. Current status: " + activeMasternode.GetStatusMessage());
        }
    }

    return resultsObj;
}

UniValue getmasternodewinners (const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 3)
        throw std::runtime_error(
            "getmasternodewinners ( blocks \"filter\" )\n"
            "\nPrint the masternode winners for the last n blocks\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Number of previous blocks to show (default: 10)\n"
            "2. filter      (string, optional) Search filter matching MN address\n"

            "\nResult (single winner):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": {\n"
            "      \"address\": \"xxxx\",    (string) KRISTA MN Address\n"
            "      \"nVotes\": n,          (numeric) Number of votes for winner\n"
            "    }\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nResult (multiple winners):\n"
            "[\n"
            "  {\n"
            "    \"nHeight\": n,           (numeric) block height\n"
            "    \"winner\": [\n"
            "      {\n"
            "        \"address\": \"xxxx\",  (string) KRISTA MN Address\n"
            "        \"nVotes\": n,        (numeric) Number of votes for winner\n"
            "      }\n"
            "      ,...\n"
            "    ]\n"
            "  }\n"
            "  ,...\n"
            "]\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodewinners", "") + HelpExampleRpc("getmasternodewinners", ""));

    int nHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nHeight < 0) return "[]";

    int nLast = 10;
    std::string strFilter = "";

    if (request.params.size() >= 1) {
        nLast = atoi(request.params[0].get_str());
        if (nLast < 1 || nLast > 1000)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Last blocks count must be between 1 and 1000");
    }

    if (request.params.size() == 2)
        strFilter = request.params[1].get_str();

    UniValue ret(UniValue::VARR);

    for (int i = nHeight - nLast; i < nHeight + 20; i++) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("nHeight", i));

        std::string strPayment = GetRequiredPaymentsString(i);
        if (strFilter != "" && strPayment.find(strFilter) == std::string::npos) continue;

        if (strPayment.find(',') != std::string::npos) {
            UniValue winner(UniValue::VARR);
            boost::char_separator<char> sep(",");
            boost::tokenizer< boost::char_separator<char> > tokens(strPayment, sep);
            for (const std::string& t : tokens) {
                UniValue addr(UniValue::VOBJ);
                std::size_t pos = t.find(":");
                std::string strAddress = t.substr(0,pos);
                uint64_t nVotes = atoi(t.substr(pos+1));
                addr.push_back(Pair("address", strAddress));
                addr.push_back(Pair("nVotes", nVotes));
                winner.push_back(addr);
            }
            obj.push_back(Pair("winner", winner));
        } else if (strPayment.find("Unknown") == std::string::npos) {
            UniValue winner(UniValue::VOBJ);
            std::size_t pos = strPayment.find(":");
            std::string strAddress = strPayment.substr(0,pos);
            uint64_t nVotes = atoi(strPayment.substr(pos+1));
            winner.push_back(Pair("address", strAddress));
            winner.push_back(Pair("nVotes", nVotes));
            obj.push_back(Pair("winner", winner));
        } else {
            UniValue winner(UniValue::VOBJ);
            winner.push_back(Pair("address", strPayment));
            winner.push_back(Pair("nVotes", 0));
            obj.push_back(Pair("winner", winner));
        }

            ret.push_back(obj);
    }

    return ret;
}

UniValue getmasternodescores (const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getmasternodescores ( blocks )\n"
            "\nPrint list of winning masternode by score\n"

            "\nArguments:\n"
            "1. blocks      (numeric, optional) Show the last n blocks (default 10)\n"

            "\nResult:\n"
            "{\n"
            "  xxxx: \"xxxx\"   (numeric : string) Block height : Masternode hash\n"
            "  ,...\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmasternodescores", "") + HelpExampleRpc("getmasternodescores", ""));

    int nLast = 10;

    if (request.params.size() == 1) {
        try {
            nLast = std::stoi(request.params[0].get_str());
        } catch (const std::exception&) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid number of blocks");
        }
        if (nLast < 1 || nLast > 1000)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Last blocks count must be between 1 and 1000");
    }
    int nChainHeight = WITH_LOCK(cs_main, return chainActive.Height());
    if (nChainHeight < 0) return "unknown";
    UniValue obj(UniValue::VOBJ);
    std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeVector();
    for (int nHeight = nChainHeight - nLast; nHeight < nChainHeight + 20; nHeight++) {
        uint256 nHigh;
        CMasternode* pBestMasternode = NULL;
        for (CMasternode& mn : vMasternodes) {
            uint256 n = mn.CalculateScore(1, nHeight - 100);
            if (n > nHigh) {
                nHigh = n;
                pBestMasternode = &mn;
            }
        }
        if (pBestMasternode)
            obj.push_back(Pair(strprintf("%d", nHeight), pBestMasternode->vin.prevout.hash.ToString().c_str()));
    }

    return obj;
}

bool DecodeHexMnb(CMasternodeBroadcast& mnb, std::string strHexMnb) {

    if (!IsHex(strHexMnb))
        return false;

    std::vector<unsigned char> mnbData(ParseHex(strHexMnb));
    CDataStream ssData(mnbData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> mnb;
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}
UniValue createmasternodebroadcast(const JSONRPCRequest& request)
{
    std::string strCommand;
    if (request.params.size() >= 1)
        strCommand = request.params[0].get_str();
    if (request.fHelp || (strCommand != "alias" && strCommand != "all") || (strCommand == "alias" && request.params.size() < 2))
        throw std::runtime_error(
            "createmasternodebroadcast \"command\" ( \"alias\")\n"
            "\nCreates a masternode broadcast message for one or all masternodes configured in masternode.conf\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"command\"      (string, required) \"alias\" for single masternode, \"all\" for all masternodes\n"
            "2. \"alias\"        (string, required if command is \"alias\") Alias of the masternode\n"

            "\nResult (all):\n"
            "{\n"
            "  \"overall\": \"xxx\",        (string) Overall status message indicating number of successes.\n"
            "  \"detail\": [                (array) JSON array of broadcast objects.\n"
            "    {\n"
            "      \"alias\": \"xxx\",      (string) Alias of the masternode.\n"
            "      \"success\": true|false, (boolean) Success status.\n"
            "      \"hex\": \"xxx\"         (string, if success=true) Hex encoded broadcast message.\n"
            "      \"error_message\": \"xxx\"   (string, if success=false) Error message, if any.\n"
            "    }\n"
            "    ,...\n"
            "  ]\n"
            "}\n"

            "\nResult (alias):\n"
            "{\n"
            "  \"alias\": \"xxx\",      (string) Alias of the masternode.\n"
            "  \"success\": true|false, (boolean) Success status.\n"
            "  \"hex\": \"xxx\"         (string, if success=true) Hex encoded broadcast message.\n"
            "  \"error_message\": \"xxx\"   (string, if success=false) Error message, if any.\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("createmasternodebroadcast", "alias mymn1") + HelpExampleRpc("createmasternodebroadcast", "alias mymn1"));

    EnsureWalletIsUnlocked();

    if (strCommand == "alias")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::string alias = request.params[1].get_str();
        bool found = false;

        UniValue statusObj(UniValue::VOBJ);
        statusObj.push_back(Pair("alias", alias));

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            if(mne.getAlias() == alias) {
                CMasternodeBroadcast mnb;
                found = true;
                std::string errorMessage;
                bool fSuccess = false;
                if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                        continue;
                SerializeMNB(statusObj, mnb, fSuccess);
                break;
            }
        }

        if(!found) {
            statusObj.push_back(Pair("success", false));
            statusObj.push_back(Pair("error_message", "Could not find alias in config. Verify with listmasternodeconf."));
        }

        return statusObj;
    }

    if (strCommand == "all")
    {
        // wait for reindex and/or import to finish
        if (fImporting || fReindex)
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wait for reindex and/or import to finish");

        std::vector<CMasternodeConfig::CMasternodeEntry> mnEntries;
        mnEntries = masternodeConfig.getEntries();

        int successful = 0;
        int failed = 0;

        UniValue resultsObj(UniValue::VARR);

        for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
            UniValue statusObj(UniValue::VOBJ);
            CMasternodeBroadcast mnb;
            std::string errorMessage;
            bool fSuccess = false;
            if (!StartMasternodeEntry(statusObj, mnb, fSuccess, mne, errorMessage, strCommand))
                    continue;
            SerializeMNB(statusObj, mnb, fSuccess, successful, failed);
            resultsObj.push_back(statusObj);
        }

        UniValue returnObj(UniValue::VOBJ);
        returnObj.push_back(Pair("overall", strprintf("Successfully created broadcast messages for %d masternodes, failed to create %d, total %d", successful, failed, successful + failed)));
        returnObj.push_back(Pair("detail", resultsObj));

        return returnObj;
    }
    return NullUniValue;
}

UniValue decodemasternodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "decodemasternodebroadcast \"hexstring\"\n"
            "\nCommand to decode masternode broadcast messages\n"

            "\nArgument:\n"
            "1. \"hexstring\"        (string) The hex encoded masternode broadcast message\n"

            "\nResult:\n"
            "{\n"
            "  \"vin\": \"xxxx\"                (string) The unspent output which is holding the masternode collateral\n"
            "  \"addr\": \"xxxx\"               (string) IP address of the masternode\n"
            "  \"pubkeycollateral\": \"xxxx\"   (string) Collateral address's public key\n"
            "  \"pubkeymasternode\": \"xxxx\"   (string) Masternode's public key\n"
            "  \"vchsig\": \"xxxx\"             (string) Base64-encoded signature of this message (verifiable via pubkeycollateral)\n"
            "  \"sigtime\": \"nnn\"             (numeric) Signature timestamp\n"
            "  \"sigvalid\": \"xxx\"            (string) \"true\"/\"false\" whether or not the mnb signature checks out.\n"
            "  \"protocolversion\": \"nnn\"     (numeric) Masternode's protocol version\n"
            "  \"nlastdsq\": \"nnn\"            (numeric) The last time the masternode sent a DSQ message (for mixing) (DEPRECATED)\n"
            "  \"nMessVersion\": \"nnn\"        (numeric) MNB Message version number\n"
            "  \"lastping\" : {                 (object) JSON object with information about the masternode's last ping\n"
            "      \"vin\": \"xxxx\"            (string) The unspent output of the masternode which is signing the message\n"
            "      \"blockhash\": \"xxxx\"      (string) Current chaintip blockhash minus 12\n"
            "      \"sigtime\": \"nnn\"         (numeric) Signature time for this ping\n"
            "      \"sigvalid\": \"xxx\"        (string) \"true\"/\"false\" whether or not the mnp signature checks out.\n"
            "      \"vchsig\": \"xxxx\"         (string) Base64-encoded signature of this ping (verifiable via pubkeymasternode)\n"
            "      \"nMessVersion\": \"nnn\"    (numeric) MNP Message version number\n"
            "  }\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("decodemasternodebroadcast", "hexstring") + HelpExampleRpc("decodemasternodebroadcast", "hexstring"));

    CMasternodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

    UniValue resultObj(UniValue::VOBJ);

    resultObj.push_back(Pair("vin", mnb.vin.prevout.ToString()));
    resultObj.push_back(Pair("addr", mnb.addr.ToString()));
    resultObj.push_back(Pair("pubkeycollateral", EncodeDestination(mnb.pubKeyCollateralAddress.GetID())));
    resultObj.push_back(Pair("pubkeymasternode", EncodeDestination(mnb.pubKeyMasternode.GetID())));
    resultObj.push_back(Pair("vchsig", mnb.GetSignatureBase64()));
    resultObj.push_back(Pair("sigtime", mnb.sigTime));
    resultObj.push_back(Pair("sigvalid", mnb.CheckSignature() ? "true" : "false"));
    resultObj.push_back(Pair("protocolversion", mnb.protocolVersion));
    resultObj.push_back(Pair("nlastdsq", mnb.nLastDsq));
    resultObj.push_back(Pair("nMessVersion", mnb.nMessVersion));

    UniValue lastPingObj(UniValue::VOBJ);
    lastPingObj.push_back(Pair("vin", mnb.lastPing.vin.prevout.ToString()));
    lastPingObj.push_back(Pair("blockhash", mnb.lastPing.blockHash.ToString()));
    lastPingObj.push_back(Pair("sigtime", mnb.lastPing.sigTime));
    lastPingObj.push_back(Pair("sigvalid", mnb.lastPing.CheckSignature(mnb.pubKeyMasternode) ? "true" : "false"));
    lastPingObj.push_back(Pair("vchsig", mnb.lastPing.GetSignatureBase64()));
    lastPingObj.push_back(Pair("nMessVersion", mnb.lastPing.nMessVersion));

    resultObj.push_back(Pair("lastping", lastPingObj));

    return resultObj;
}

UniValue relaymasternodebroadcast(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "relaymasternodebroadcast \"hexstring\"\n"
            "\nCommand to relay masternode broadcast messages\n"

            "\nArguments:\n"
            "1. \"hexstring\"        (string) The hex encoded masternode broadcast message\n"

            "\nExamples:\n" +
            HelpExampleCli("relaymasternodebroadcast", "hexstring") + HelpExampleRpc("relaymasternodebroadcast", "hexstring"));


    CMasternodeBroadcast mnb;

    if (!DecodeHexMnb(mnb, request.params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Masternode broadcast message decode failed");

    if(!mnb.CheckSignature())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Masternode broadcast signature verification failed");

    mnodeman.UpdateMasternodeList(mnb);
    mnb.Relay();

    return strprintf("Masternode broadcast sent (service %s, vin %s)", mnb.addr.ToString(), mnb.vin.ToString());
}

UniValue getactivequorum(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getactivequorum ( height )\n"
            "\nReturns the deterministic on-chain LLMQ quorum active at the given height (defaults to next block nSpendHeight = currentHeight + 1).\n"
            "\nArguments:\n"
            "1. height    (numeric, optional) Target block height (defaults to currentHeight + 1).\n");

    LOCK(cs_main);
    int nCurrentHeight = chainActive.Height();
    if (nCurrentHeight < 0)
        throw JSONRPCError(RPC_IN_WARMUP, "Chain is not initialized");

    int nTargetHeight = nCurrentHeight + 1;
    if (request.params.size() >= 1 && !request.params[0].isNull()) {
        nTargetHeight = request.params[0].get_int();
        if (nTargetHeight < 0 || nTargetHeight > nCurrentHeight + 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
    }

    const CBlockIndex* pindexPrev = (nTargetHeight > 0 && nTargetHeight - 1 <= nCurrentHeight)
        ? chainActive[nTargetHeight - 1]
        : chainActive.Tip();

    llmq::CQuorum quorum = llmq::GetActiveQuorum(nTargetHeight, pindexPrev);

    bool fRotationActive = false;
    if (Params().NetworkID() == CBaseChainParams::MAIN) {
        fRotationActive = (quorum.nHeight >= 1400);
    } else if (Params().NetworkID() == CBaseChainParams::TESTNET) {
        fRotationActive = (quorum.nHeight >= 1000);
    } else {
        fRotationActive = (quorum.nHeight >= 800);
    }
    int dkgInterval = (Params().NetworkID() == CBaseChainParams::MAIN) ? 100 : 10;
    int seedHeight = quorum.nHeight - 1;
    if (fRotationActive) {
        seedHeight = (quorum.nHeight / dkgInterval) * dkgInterval - 1;
    }
    if (seedHeight < 0) seedHeight = 0;

    uint256 seed;
    if (seedHeight <= nCurrentHeight && chainActive[seedHeight]) {
        seed = GetAdamSeed(chainActive[seedHeight]);
    }

    size_t threshold = (quorum.members.size() / 2 + 1);
    if (threshold < 2) threshold = 2;
    if (!quorum.members.empty() && quorum.members.size() < threshold) {
        threshold = quorum.members.size();
    }

    UniValue membersArr(UniValue::VARR);
    for (size_t i = 0; i < quorum.members.size(); ++i) {
        const llmq::CQuorumMember& m = quorum.members[i];
        UniValue mObj(UniValue::VOBJ);
        mObj.push_back(Pair("rank", (int)(i + 1)));
        mObj.push_back(Pair("txhash", m.collateralOutpoint.hash.ToString()));
        mObj.push_back(Pair("outidx", (uint64_t)m.collateralOutpoint.n));
        mObj.push_back(Pair("pubkey", HexStr(m.pubKeyMasternode)));

        std::string strAddr = EncodeDestination(m.pubKeyMasternode.GetID());
        if (pcoinsTip) {
            const Coin& coin = pcoinsTip->AccessCoin(m.collateralOutpoint);
            if (!coin.IsSpent()) {
                CTxDestination dest;
                if (ExtractDestination(coin.out.scriptPubKey, dest)) {
                    strAddr = EncodeDestination(dest);
                }
            }
        }
        CMasternode* pmn = mnodeman.Find(CTxIn(m.collateralOutpoint));
        if (strAddr == EncodeDestination(m.pubKeyMasternode.GetID()) && pmn && pmn->pubKeyCollateralAddress.IsValid()) {
            strAddr = EncodeDestination(pmn->pubKeyCollateralAddress.GetID());
        }
        mObj.push_back(Pair("addr", strAddr));
        mObj.push_back(Pair("status", "ENABLED"));

        std::string strIp = "On-Chain MN";
        if (pmn && pmn->addr.IsValid()) {
            strIp = pmn->addr.ToString();
        }
        mObj.push_back(Pair("ip", strIp));
        membersArr.push_back(mObj);
    }

    UniValue ret(UniValue::VOBJ);
    ret.push_back(Pair("chain", Params().NetworkIDString()));
    ret.push_back(Pair("currentHeight", nCurrentHeight));
    ret.push_back(Pair("targetHeight", nTargetHeight));
    ret.push_back(Pair("dkgHeight", quorum.nHeight));
    ret.push_back(Pair("quorumHash", quorum.quorumHash.ToString()));
    ret.push_back(Pair("seedHeight", seedHeight));
    ret.push_back(Pair("seed", seed.ToString()));
    ret.push_back(Pair("threshold", (int)threshold));
    ret.push_back(Pair("developerFundAddress", Params().DeveloperFundAddress()));
    ret.push_back(Pair("sporkPubKey", Params().GetConsensus().strSporkPubKey));
    ret.push_back(Pair("treasuryGovernanceStartHeight", Params().GetConsensus().nTreasuryGovernanceStartHeight));
    ret.push_back(Pair("members", membersArr));
    return ret;
}

static UniValue DeterministicMNToJSON(const dip3::CDeterministicMN& mn)
{
    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("proTxHash", mn.proTxHash.ToString()));
    obj.push_back(Pair("collateralHash", mn.collateralOutpoint.hash.ToString()));
    obj.push_back(Pair("collateralIndex", (int)mn.collateralOutpoint.n));
    obj.push_back(Pair("service", mn.state.addr.ToString()));
    obj.push_back(Pair("ownerAddress", EncodeDestination(mn.state.pubKeyOwner.GetID())));
    obj.push_back(Pair("votingAddress", EncodeDestination(mn.state.keyIDVoting)));
    obj.push_back(Pair("operatorPubKey", HexStr(mn.state.blsPubKeyOperator.begin(), mn.state.blsPubKeyOperator.end())));
    obj.push_back(Pair("operatorReward", (int)mn.state.nOperatorReward));
    CTxDestination payoutDest;
    if (ExtractDestination(mn.state.scriptPayout, payoutDest)) {
        obj.push_back(Pair("payoutAddress", EncodeDestination(payoutDest)));
    }
    obj.push_back(Pair("registeredHeight", mn.state.nRegisteredHeight));
    obj.push_back(Pair("lastPaidHeight", mn.state.nLastPaidHeight));
    obj.push_back(Pair("PoSePenalty", mn.state.nPoSePenalty));
    obj.push_back(Pair("PoSeRevivedHeight", mn.state.nPoSeRevivedHeight));
    obj.push_back(Pair("PoSeBanHeight", mn.state.nPoSeBanHeight));
    obj.push_back(Pair("revocationReason", (int)mn.state.nRevocationReason));
    obj.push_back(Pair("valid", mn.state.IsValid()));
    return obj;
}

#ifdef ENABLE_WALLET
template <typename SignPayloadFn>
static uint256 CreateAndSendProTx(const CScript& dummyScript, SignPayloadFn signPayloadFn, const COutPoint* pExcludeOutpoint = nullptr)
{
    if (!pwalletMain) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
    }
    EnsureWalletIsUnlocked();

    LOCK2(cs_main, pwalletMain->cs_wallet);
    bool fLockedExclude = false;
    if (pExcludeOutpoint && !pExcludeOutpoint->IsNull() && !pwalletMain->IsLockedCoin(pExcludeOutpoint->hash, pExcludeOutpoint->n)) {
        pwalletMain->LockCoin(*pExcludeOutpoint);
        fLockedExclude = true;
    }

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired = 0;
    std::string strError;

    if (!pwalletMain->CreateTransaction(dummyScript, 0, wtx, reservekey, nFeeRequired, strError)) {
        if (fLockedExclude) {
            pwalletMain->UnlockCoin(*pExcludeOutpoint);
        }
        throw JSONRPCError(RPC_WALLET_ERROR, strError.empty() ? "CreateTransaction failed" : strError);
    }

    CMutableTransaction mtx(static_cast<const CTransaction&>(wtx));
    const uint256 inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
    CScript finalScript = signPayloadFn(inputsHash);

    bool replaced = false;
    for (auto& out : mtx.vout) {
        if (out.nValue == 0 && !out.scriptPubKey.empty() && out.scriptPubKey[0] == OP_RETURN) {
            out.scriptPubKey = finalScript;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        if (fLockedExclude) {
            pwalletMain->UnlockCoin(*pExcludeOutpoint);
        }
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to locate OP_RETURN output in ProTx");
    }

    const CTransaction txConst(mtx);
    for (unsigned int nIn = 0; nIn < mtx.vin.size(); ++nIn) {
        auto mi = pwalletMain->mapWallet.find(mtx.vin[nIn].prevout.hash);
        if (mi == pwalletMain->mapWallet.end() || mtx.vin[nIn].prevout.n >= mi->second.vout.size()) {
            if (fLockedExclude) {
                pwalletMain->UnlockCoin(*pExcludeOutpoint);
            }
            throw JSONRPCError(RPC_WALLET_ERROR, "Input not found in wallet for ProTx signing");
        }
        const CTxOut& prevOut = mi->second.vout[mtx.vin[nIn].prevout.n];
        SignatureData sigdata;
        if (!ProduceSignature(TransactionSignatureCreator(pwalletMain, &txConst, nIn, prevOut.nValue, SIGHASH_ALL), prevOut.scriptPubKey, sigdata)) {
            if (fLockedExclude) {
                pwalletMain->UnlockCoin(*pExcludeOutpoint);
            }
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign ProTx input");
        }
        mtx.vin[nIn].scriptSig = sigdata.scriptSig;
    }

    CWalletTx wtxSigned(pwalletMain, CTransaction(mtx));
    CWallet::CommitResult res = pwalletMain->CommitTransaction(wtxSigned, reservekey, g_connman.get());
    if (res.status != CWallet::CommitStatus::OK) {
        if (fLockedExclude) {
            pwalletMain->UnlockCoin(*pExcludeOutpoint);
        }
        throw JSONRPCError(RPC_WALLET_ERROR, res.ToString());
    }
    return wtxSigned.GetHash();
}

bool RegisterDip3MasternodeFromWallet(const COutPoint& colOut,
                                      const CService& addr,
                                      const CBLSPubKey& opPubKey,
                                      std::string& strTxHashOut,
                                      std::string& strErrOut)
{
    if (!pwalletMain) {
        strErrOut = "Wallet is not available.";
        return false;
    }
    if (!deterministicMNManager) {
        strErrOut = "Deterministic MN manager not initialized.";
        return false;
    }
    if (pwalletMain->IsLocked()) {
        strErrOut = "Wallet is locked. Please unlock the wallet first.";
        return false;
    }
    if (!addr.IsValid() || addr.GetPort() == 0) {
        strErrOut = "Invalid Masternode IP:port.";
        return false;
    }
    if (!opPubKey.IsValid()) {
        strErrOut = "Invalid BLS operator public key.";
        return false;
    }

    const dip3::CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();
    if (mnList.GetMNByCollateral(colOut) != nullptr) {
        strErrOut = "Masternode collateral is already registered on-chain via KTDIP3.";
        return false;
    }

    {
        LOCK(mempool.cs);
        for (const auto& entry : mempool.mapTx) {
            std::vector<unsigned char> vchPayload;
            bool fMalformed = false;
            if (dip3::ExtractProTxPayload(entry.GetTx(), vchPayload, fMalformed) == dip3::PROTX_REG && !fMalformed) {
                try {
                    CDataStream ds(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
                    dip3::CProRegTx pendingPtx;
                    ds >> pendingPtx;
                    if (pendingPtx.collateralOutpoint == colOut) {
                        strErrOut = "A KTDIP3 ProRegTx registration for this collateral is already pending in the mempool (waiting for next block).";
                        return false;
                    }
                } catch (...) {
                }
            }
        }
    }

    CTxOut collateralTxOut;
    int depth = 0;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        auto it = pwalletMain->mapWallet.find(colOut.hash);
        if (it != pwalletMain->mapWallet.end() && colOut.n < it->second.vout.size()) {
            collateralTxOut = it->second.vout[colOut.n];
            depth = it->second.GetDepthInMainChain();
        } else {
            Coin coin;
            if (!pcoinsTip || !pcoinsTip->GetCoin(colOut, coin) || coin.IsSpent()) {
                strErrOut = "Collateral UTXO not found or already spent.";
                return false;
            }
            collateralTxOut = coin.out;
            depth = chainActive.Height() - coin.nHeight + 1;
        }
    }

    if (depth < MASTERNODE_MIN_CONFIRMATIONS) {
        strErrOut = strprintf("Collateral requires %d confirmations before KTDIP3 registration (currently %d/%d).",
                              MASTERNODE_MIN_CONFIRMATIONS, std::max(0, depth), MASTERNODE_MIN_CONFIRMATIONS);
        return false;
    }

    CTxDestination colDest;
    if (!ExtractDestination(collateralTxOut.scriptPubKey, colDest)) {
        strErrOut = "Could not extract collateral address.";
        return false;
    }
    const CKeyID* colKeyID = boost::get<CKeyID>(&colDest);
    if (!colKeyID) {
        strErrOut = "Collateral output must be a P2PKH address.";
        return false;
    }
    CKey ownerKey;
    {
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(*colKeyID, ownerKey)) {
            strErrOut = "Private key for collateral address not found in wallet.";
            return false;
        }
    }

    dip3::CProRegTx ptx;
    ptx.nVersion = 1;
    ptx.collateralOutpoint = colOut;
    ptx.addr = addr;
    ptx.pubKeyOwner = ownerKey.GetPubKey();
    ptx.blsPubKeyOperator = opPubKey;
    ptx.keyIDVoting = *colKeyID;
    ptx.nOperatorReward = 0;
    ptx.scriptPayout = collateralTxOut.scriptPubKey;
    ptx.vchSig.resize(72, 0);

    try {
        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_REG, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            ptx.vchSig.clear();
            if (!ownerKey.Sign(ptx.GetSignHash(), ptx.vchSig)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign ProRegTx with owner key");
            }
            return dip3::BuildProTxScript(dip3::PROTX_REG, ptx);
        }, &colOut);
        strTxHashOut = txHash.ToString();
        return true;
    } catch (const UniValue& objError) {
        const UniValue& msgVal = find_value(objError, "message");
        strErrOut = msgVal.isStr() ? msgVal.get_str() : objError.write();
        return false;
    } catch (const std::exception& e) {
        strErrOut = e.what();
        return false;
    }
}
#endif // ENABLE_WALLET

static int ParseProTxIntArg(const UniValue& val, const std::string& name)
{
    if (val.isNum()) {
        return val.get_int();
    }
    if (val.isStr()) {
        int32_t out = 0;
        if (ParseInt32(val.get_str(), &out)) {
            return out;
        }
    }
    throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid integer for %s", name));
}

UniValue protx(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1) {
        throw std::runtime_error(
            "protx \"command\" ...\n"
            "Manage and query KTIP-0014 (DIP3) deterministic masternodes.\n"
            "Commands:\n"
            "  bls_generate                                                              Generate a fresh BLS12-381 operator keypair\n"
            "  list [registered|valid|detailed]                                          List deterministic masternodes at chain tip\n"
            "  info <proTxHash>                                                          Show detailed info for a deterministic masternode\n"
            "  register <colHash> <colIdx> <ip:port> <ownerAddr> <opBlsPub> <voteAddr> <opRewardBps> <payoutAddr>\n"
            "  update_service <proTxHash> <ip:port> <opBlsSecret> (<opPayoutAddr>)       Update service IP:port / revive PoSe ban\n"
            "  update_registrar <proTxHash> <opBlsPub> <voteAddr> <payoutAddr>           Update operator / voting / payout\n"
            "  revoke <proTxHash> <opBlsSecret> (<reason>)                               Revoke operator BLS key\n"
        );
    }

    const std::string cmd = request.params[0].get_str();

    if (cmd == "bls_generate") {
        CBLSSecretKey sk;
        sk.MakeNewKey();
        CBLSPubKey pk(sk);
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("secret", HexStr(sk.begin(), sk.end())));
        obj.push_back(Pair("public", HexStr(pk.begin(), pk.end())));
        return obj;
    }

    if (!deterministicMNManager) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Deterministic MN manager not initialized");
    }

    const dip3::CDeterministicMNList mnList = deterministicMNManager->GetListAtChainTip();

    if (cmd == "list") {
        std::string mode = (request.params.size() > 1) ? request.params[1].get_str() : "registered";
        UniValue arr(UniValue::VARR);
        for (const auto& kv : mnList.mapMNs) {
            if (mode == "valid" && !kv.second.state.IsValid()) {
                continue;
            }
            if (mode == "detailed") {
                arr.push_back(DeterministicMNToJSON(kv.second));
            } else {
                arr.push_back(kv.first.ToString());
            }
        }
        return arr;
    }

    if (cmd == "info") {
        if (request.params.size() < 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx info requires <proTxHash>");
        }
        uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
        const dip3::CDeterministicMN* pmn = mnList.GetMN(proTxHash);
        if (!pmn) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "ProTx not found");
        }
        return DeterministicMNToJSON(*pmn);
    }

#ifdef ENABLE_WALLET
    if (cmd == "register") {
        if (request.params.size() < 9) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx register requires 8 arguments: <colHash> <colIdx> <ip:port> <ownerAddr> <opBlsPub> <voteAddr> <opRewardBps> <payoutAddr>");
        }
        if (!pwalletMain) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
        }

        uint256 colHash = ParseHashV(request.params[1], "collateralHash");
        int colIdx = ParseProTxIntArg(request.params[2], "collateralIndex");
        if (colIdx < 0) throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid collateralIndex");
        COutPoint colOut(colHash, (uint32_t)colIdx);

        CService addr;
        if (!Lookup(request.params[3].get_str().c_str(), addr, Params().GetDefaultPort(), false) || !addr.IsValid() || addr.GetPort() == 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ipAndPort");
        }

        CTxDestination ownerDest = DecodeDestination(request.params[4].get_str());
        const CKeyID* ownerKeyID = boost::get<CKeyID>(&ownerDest);
        if (!ownerKeyID) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "ownerAddress must be a P2PKH address");
        }
        CKey ownerKey;
        if (!pwalletMain->GetKey(*ownerKeyID, ownerKey)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Private key for ownerAddress not found in wallet");
        }

        std::vector<unsigned char> vchOpPub = ParseHexV(request.params[5], "operatorPubKey");
        CBLSPubKey opPubKey;
        if (!opPubKey.SetBuf(vchOpPub.data(), vchOpPub.size()) || !opPubKey.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 48-byte BLS operatorPubKey");
        }

        CTxDestination voteDest = DecodeDestination(request.params[6].get_str());
        const CKeyID* voteKeyID = boost::get<CKeyID>(&voteDest);
        if (!voteKeyID) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "votingAddress must be a P2PKH address");
        }

        int opReward = ParseProTxIntArg(request.params[7], "operatorReward");
        if (opReward < 0 || opReward > 10000) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "operatorReward must be 0..10000 (bps)");
        }

        CTxDestination payoutDest = DecodeDestination(request.params[8].get_str());
        if (!IsValidDestination(payoutDest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid payoutAddress");
        }

        dip3::CProRegTx ptx;
        ptx.nVersion = 1;
        ptx.collateralOutpoint = colOut;
        ptx.addr = addr;
        ptx.pubKeyOwner = ownerKey.GetPubKey();
        ptx.blsPubKeyOperator = opPubKey;
        ptx.keyIDVoting = *voteKeyID;
        ptx.nOperatorReward = (uint16_t)opReward;
        ptx.scriptPayout = GetScriptForDestination(payoutDest);
        ptx.vchSig.resize(72, 0);

        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_REG, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            ptx.vchSig.clear();
            if (!ownerKey.Sign(ptx.GetSignHash(), ptx.vchSig)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign ProRegTx with owner key");
            }
            return dip3::BuildProTxScript(dip3::PROTX_REG, ptx);
        }, &colOut);
        return txHash.ToString();
    }

    if (cmd == "update_service") {
        if (request.params.size() < 4) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx update_service requires <proTxHash> <ip:port> <opBlsSecret> (<opPayoutAddr>)");
        }
        uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
        const dip3::CDeterministicMN* pmn = mnList.GetMN(proTxHash);
        if (!pmn) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "ProTx not found");

        CService addr;
        if (!Lookup(request.params[2].get_str().c_str(), addr, Params().GetDefaultPort(), false) || !addr.IsValid() || addr.GetPort() == 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid ipAndPort");
        }

        std::vector<unsigned char> vchOpSk = ParseHexV(request.params[3], "operatorSecretKey");
        CBLSSecretKey opSk;
        if (!opSk.SetBuf(vchOpSk.data(), vchOpSk.size()) || !opSk.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 32-byte BLS operatorSecretKey");
        }

        CScript opPayout = pmn->state.scriptOperatorPayout;
        if (request.params.size() > 4 && !request.params[4].get_str().empty()) {
            CTxDestination dest = DecodeDestination(request.params[4].get_str());
            if (!IsValidDestination(dest)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid operatorPayoutAddress");
            opPayout = GetScriptForDestination(dest);
        }

        dip3::CProUpServTx ptx;
        ptx.nVersion = 1;
        ptx.proTxHash = proTxHash;
        ptx.addr = addr;
        ptx.scriptOperatorPayout = opPayout;
        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_UPSERV, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            if (!ptx.blsSig.Sign(opSk, ptx.GetSignHash())) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to BLS-sign ProUpServTx");
            }
            return dip3::BuildProTxScript(dip3::PROTX_UPSERV, ptx);
        });
        return txHash.ToString();
    }

    if (cmd == "update_registrar") {
        if (request.params.size() < 5) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx update_registrar requires <proTxHash> <opBlsPub> <voteAddr> <payoutAddr>");
        }
        if (!pwalletMain) throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");

        uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
        const dip3::CDeterministicMN* pmn = mnList.GetMN(proTxHash);
        if (!pmn) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "ProTx not found");

        CKey ownerKey;
        if (!pwalletMain->GetKey(pmn->state.pubKeyOwner.GetID(), ownerKey)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Owner private key not found in wallet");
        }

        std::vector<unsigned char> vchOpPub = ParseHexV(request.params[2], "operatorPubKey");
        CBLSPubKey opPubKey;
        if (!opPubKey.SetBuf(vchOpPub.data(), vchOpPub.size()) || !opPubKey.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 48-byte BLS operatorPubKey");
        }

        CTxDestination voteDest = DecodeDestination(request.params[3].get_str());
        const CKeyID* voteKeyID = boost::get<CKeyID>(&voteDest);
        if (!voteKeyID) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "votingAddress must be P2PKH");

        CTxDestination payoutDest = DecodeDestination(request.params[4].get_str());
        if (!IsValidDestination(payoutDest)) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid payoutAddress");

        dip3::CProUpRegTx ptx;
        ptx.nVersion = 1;
        ptx.proTxHash = proTxHash;
        ptx.blsPubKeyOperator = opPubKey;
        ptx.keyIDVoting = *voteKeyID;
        ptx.scriptPayout = GetScriptForDestination(payoutDest);
        ptx.vchSig.resize(72, 0);

        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_UPREG, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            ptx.vchSig.clear();
            if (!ownerKey.Sign(ptx.GetSignHash(), ptx.vchSig)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign ProUpRegTx");
            }
            return dip3::BuildProTxScript(dip3::PROTX_UPREG, ptx);
        });
        return txHash.ToString();
    }

    if (cmd == "revoke") {
        if (request.params.size() < 3) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx revoke requires <proTxHash> <opBlsSecret> (<reason>)");
        }
        uint256 proTxHash = ParseHashV(request.params[1], "proTxHash");
        std::vector<unsigned char> vchOpSk = ParseHexV(request.params[2], "operatorSecretKey");
        CBLSSecretKey opSk;
        if (!opSk.SetBuf(vchOpSk.data(), vchOpSk.size()) || !opSk.IsValid()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 32-byte BLS operatorSecretKey");
        }
        int reason = (request.params.size() > 3) ? ParseProTxIntArg(request.params[3], "reason") : 1;
        if (reason <= 0 || reason > 65535) throw JSONRPCError(RPC_INVALID_PARAMETER, "reason must be 1..65535");

        dip3::CProUpRevTx ptx;
        ptx.nVersion = 1;
        ptx.proTxHash = proTxHash;
        ptx.nReason = (uint16_t)reason;
        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_UPREV, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            if (!ptx.blsSig.Sign(opSk, ptx.GetSignHash())) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to BLS-sign ProUpRevTx");
            }
            return dip3::BuildProTxScript(dip3::PROTX_UPREV, ptx);
        });
        return txHash.ToString();
    }
#endif // ENABLE_WALLET

    throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown protx command");
}
