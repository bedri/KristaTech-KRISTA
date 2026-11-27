// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "activemasternodeman.h"
#include "chainparams.h"
#include "db.h"
#include "init.h"
#include "main.h"
#include "masternode-payments.h"
#include "masternode-sync.h"
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

        CMasternode mn;
        if (mnodeman.Get(s.second.vin, mn)) {
            if (strFilter != "" && strTxHash.find(strFilter) == std::string::npos &&
                mn.Status().find(strFilter) == std::string::npos &&
                EncodeDestination(mn.pubKeyCollateralAddress.GetID()).find(strFilter) == std::string::npos) continue;

            std::string strStatus = mn.Status();
            std::string strHost;
            int port;
            SplitHostPort(mn.addr.ToString(), port, strHost);
            CNetAddr node;
            LookupHost(strHost.c_str(), node, false);
            std::string strNetwork = GetNetworkName(node.GetNetwork());

            obj.push_back(Pair("rank", (strStatus == "ENABLED" ? s.first : 0)));
            obj.push_back(Pair("network", strNetwork));
            obj.push_back(Pair("txhash", strTxHash));
            obj.push_back(Pair("outidx", (uint64_t)oIdx));
            obj.push_back(Pair("pubkey", HexStr(mn.pubKeyMasternode)));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("addr", EncodeDestination(mn.pubKeyCollateralAddress.GetID())));
            obj.push_back(Pair("ip", mn.addr.ToString()));
            obj.push_back(Pair("version", mn.protocolVersion));
            obj.push_back(Pair("lastseen", (int64_t)mn.lastPing.sigTime));
            obj.push_back(Pair("activetime", (int64_t)(mn.lastPing.sigTime - mn.sigTime)));
            obj.push_back(Pair("lastpaid", (int64_t)mn.GetLastPaid()));

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
    CMasternode winner;
    if (mnodeman.GetNextMasternodeInQueueForPayment(nHeight, true, nCount, &winner)) {
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("protocol", (int64_t)winner.protocolVersion));
        obj.push_back(Pair("txhash", winner.vin.prevout.hash.ToString()));
        obj.push_back(Pair("pubkey", EncodeDestination(winner.pubKeyCollateralAddress.GetID())));
        obj.push_back(Pair("lastseen", winner.lastPing.IsNull() ? winner.sigTime : (int64_t)winner.lastPing.sigTime));
        obj.push_back(Pair("activeseconds", winner.lastPing.IsNull() ? 0 : (int64_t)(winner.lastPing.sigTime - winner.sigTime)));
        return obj;
    }

    throw std::runtime_error("unknown");
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

    if (!pwalletMain) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
    }

    // Find possible candidates
    LOCK2(cs_main, pwalletMain->cs_wallet);
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

    if (deterministicMNManager) {
        int nTipHeight = 0;
        {
            LOCK(cs_main);
            nTipHeight = chainActive.Height();
        }
        if (Params().GetConsensus().NetworkUpgradeActive(nTipHeight + 1, Consensus::UPGRADE_DIP3)) {
            amnodeman.ManageStatus();
        }
    }

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

        CMasternode mn;
        if (mnodeman.Get(*(activeMasternode.vin), mn)) {
            UniValue mnObj(UniValue::VOBJ);
            mnObj.push_back(Pair("alias", activeMasternode.strAlias));
            mnObj.push_back(Pair("txhash", activeMasternode.vin->prevout.hash.ToString()));
            mnObj.push_back(Pair("outputidx", (uint64_t)activeMasternode.vin->prevout.n));
            mnObj.push_back(Pair("netaddr", activeMasternode.service.ToString()));
            mnObj.push_back(Pair("addr", EncodeDestination(mn.pubKeyCollateralAddress.GetID())));
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

    const size_t threshold = llmq::GetQuorumThreshold(quorum.members.size(), quorum.nHeight);

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
        CMasternode mn;
        const bool hasMn = mnodeman.Get(CTxIn(m.collateralOutpoint), mn);
        if (strAddr == EncodeDestination(m.pubKeyMasternode.GetID()) && hasMn && mn.pubKeyCollateralAddress.IsValid()) {
            strAddr = EncodeDestination(mn.pubKeyCollateralAddress.GetID());
        }
        mObj.push_back(Pair("addr", strAddr));
        mObj.push_back(Pair("status", "ENABLED"));

        std::string strIp = "On-Chain MN";
        if (hasMn && mn.addr.IsValid()) {
            strIp = mn.addr.ToString();
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

static int GetProTxNextHeight()
{
    LOCK(cs_main);
    return chainActive.Tip() ? (chainActive.Height() + 1) : 0;
}

static bool TryResolveLocalOperatorPoP(const COutPoint* pColOut, const CBLSPubKey& opPubKey, CBLSSignature& popOut)
{
    popOut.Reset();
    if (!opPubKey.IsValid()) {
        return false;
    }
    for (const auto& activeMn : amnodeman.GetActiveMasternodesCopy()) {
        if (activeMn.blsKeyMasternode.IsValid() && CBLSPubKey(activeMn.blsKeyMasternode) == opPubKey) {
            if (popOut.SignPoP(activeMn.blsKeyMasternode) && popOut.VerifyPoP(opPubKey)) {
                return true;
            }
        }
    }
    if (pwalletMain) {
        if (pColOut) {
            CKey mnKey;
            CBLSSecretKey opSk;
            const bool gotColKey = pwalletMain->GetMasternodeOperatorKeys(*pColOut, mnKey, opSk);
            mnKey = CKey();
            if (gotColKey && opSk.IsValid() && CBLSPubKey(opSk) == opPubKey) {
                const bool ok = popOut.SignPoP(opSk) && popOut.VerifyPoP(opPubKey);
                opSk.Reset();
                if (ok) {
                    return true;
                }
            }
            opSk.Reset();
        }
        {
            LOCK(pwalletMain->cs_wallet);
            for (const auto& rec : pwalletMain->GetAllMasternodeMeta()) {
                if (rec.operatorKeyID.IsNull()) {
                    continue;
                }
                CBLSSecretKey opSk;
                if (pwalletMain->GetBLSKey(rec.operatorKeyID, opSk) && opSk.IsValid() && CBLSPubKey(opSk) == opPubKey) {
                    const bool ok = popOut.SignPoP(opSk) && popOut.VerifyPoP(opPubKey);
                    opSk.Reset();
                    if (ok) {
                        return true;
                    }
                }
                opSk.Reset();
            }
        }
    }
    return false;
}

bool RegisterDip3MasternodeFromWallet(const COutPoint& colOut,
                                      const CService& addr,
                                      const CBLSPubKey& opPubKey,
                                      std::string& strTxHashOut,
                                      std::string& strErrOut,
                                      uint16_t nOperatorRewardBps,
                                      const std::string& strPayoutAddress,
                                      const CBLSSignature* pBlsPoP)
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
    if (nOperatorRewardBps > 10000) {
        strErrOut = "Operator reward must be 0..10000 bps.";
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

    CScript scriptPayout = collateralTxOut.scriptPubKey;
    if (!strPayoutAddress.empty()) {
        CTxDestination payoutDest = DecodeDestination(strPayoutAddress);
        if (!IsValidDestination(payoutDest)) {
            strErrOut = "Invalid payout address.";
            return false;
        }
        scriptPayout = GetScriptForDestination(payoutDest);
    }

    const int nNextHeight = GetProTxNextHeight();
    const bool fHardeningV3 = IsHardeningV3Active(nNextHeight);

    CBLSSignature blsPoP;
    if (pBlsPoP && pBlsPoP->IsValid()) {
        if (!pBlsPoP->VerifyPoP(opPubKey)) {
            strErrOut = "Invalid BLS Proof-of-Possession signature for operator public key.";
            return false;
        }
        blsPoP = *pBlsPoP;
    } else {
        TryResolveLocalOperatorPoP(&colOut, opPubKey, blsPoP);
    }
    if (fHardeningV3 && (!blsPoP.IsValid() || !blsPoP.VerifyPoP(opPubKey))) {
        strErrOut = "KTDIP3 ProRegTx v2 requires a valid BLS operator Proof-of-Possession (generate <public, pop> with 'protx bls_generate' or 'protx bls_pop').";
        return false;
    }

    dip3::CProRegTx ptx;
    ptx.nVersion = (fHardeningV3 || blsPoP.IsValid()) ? 2 : 1;
    ptx.collateralOutpoint = colOut;
    ptx.addr = addr;
    ptx.pubKeyOwner = ownerKey.GetPubKey();
    ptx.blsPubKeyOperator = opPubKey;
    ptx.blsPoP = blsPoP;
    ptx.keyIDVoting = *colKeyID;
    ptx.nOperatorReward = nOperatorRewardBps;
    ptx.scriptPayout = scriptPayout;
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
        pwalletMain->UpdateMasternodeProTxHash(colOut, txHash, addr.ToString(), HexStr(opPubKey.begin(), opPubKey.end()));
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
            "  bls_generate                                                              Generate a fresh BLS12-381 operator keypair & PoP\n"
            "  bls_pop <opBlsSecret>                                                     Compute BLS12-381 Proof-of-Possession for an existing secret key\n"
            "  list [registered|valid|detailed]                                          List deterministic masternodes at chain tip\n"
            "  info <proTxHash>                                                          Show detailed info for a deterministic masternode\n"
            "  register <colHash> <colIdx> <ip:port> <ownerAddr> <opBlsPub> <voteAddr> <opRewardBps> <payoutAddr> (<opBlsPoP>)\n"
            "  update_service <proTxHash> <ip:port> <opBlsSecret> (<opPayoutAddr>)       Update service IP:port / revive PoSe ban\n"
            "  update_registrar <proTxHash> <opBlsPub> <voteAddr> <payoutAddr> (<opBlsPoP>) Update operator / voting / payout\n"
            "  revoke <proTxHash> <opBlsSecret> (<reason>)                               Revoke operator BLS key\n"
            "  wallet_list                                                               List wallet-managed masternodes & drafts (wallet.dat)\n"
            "  set_alias <outpoint_or_protxhash> <alias>                                 Set or rename local alias in wallet.dat\n"
            "  remove_meta <outpoint_or_protxhash_or_alias> (unlock_collateral)          Remove local metadata/draft from wallet.dat\n"
        );
    }

    const std::string cmd = request.params[0].get_str();

    if (cmd == "bls_generate") {
        CBLSSecretKey sk;
        sk.MakeNewKey();
        CBLSPubKey pk(sk);
        CBLSSignature pop;
        if (!pop.SignPoP(sk)) {
            sk.Reset();
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign BLS Proof-of-Possession");
        }
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("secret", HexStr(sk.begin(), sk.end())));
        obj.push_back(Pair("public", HexStr(pk.begin(), pk.end())));
        obj.push_back(Pair("pop", HexStr(pop.begin(), pop.end())));
        sk.Reset();
        return obj;
    }

    if (cmd == "bls_pop") {
        if (request.params.size() < 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx bls_pop requires <operatorSecretKey>");
        }
        std::vector<unsigned char> vchOpSk = ParseHexV(request.params[1], "operatorSecretKey");
        CBLSSecretKey sk;
        const bool fOk = sk.SetBuf(vchOpSk.data(), vchOpSk.size()) && sk.IsValid();
        if (!vchOpSk.empty()) {
            memory_cleanse(vchOpSk.data(), vchOpSk.size());
        }
        if (!fOk) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 32-byte BLS operatorSecretKey");
        }
        CBLSPubKey pk(sk);
        CBLSSignature pop;
        const bool fPopOk = pk.IsValid() && pop.SignPoP(sk);
        sk.Reset();
        if (!fPopOk) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign BLS Proof-of-Possession");
        }
        UniValue obj(UniValue::VOBJ);
        obj.push_back(Pair("public", HexStr(pk.begin(), pk.end())));
        obj.push_back(Pair("pop", HexStr(pop.begin(), pop.end())));
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

        const int nNextHeight = GetProTxNextHeight();
        const bool fHardeningV3 = IsHardeningV3Active(nNextHeight);
        CBLSSignature blsPoP;
        if (request.params.size() > 9 && !request.params[9].get_str().empty()) {
            std::vector<unsigned char> vchPoP = ParseHexV(request.params[9], "operatorBlsPoP");
            if (vchPoP.size() != 96 || !blsPoP.SetBuf(vchPoP.data(), vchPoP.size()) || !blsPoP.VerifyPoP(opPubKey)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 96-byte BLS operatorBlsPoP for operatorPubKey");
            }
        } else {
            TryResolveLocalOperatorPoP(&colOut, opPubKey, blsPoP);
        }
        if (fHardeningV3 && (!blsPoP.IsValid() || !blsPoP.VerifyPoP(opPubKey))) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "KTDIP3 ProRegTx v2 requires <operatorBlsPoP> (9th param) or local operator secret key");
        }

        dip3::CProRegTx ptx;
        ptx.nVersion = (fHardeningV3 || blsPoP.IsValid()) ? 2 : 1;
        ptx.collateralOutpoint = colOut;
        ptx.addr = addr;
        ptx.pubKeyOwner = ownerKey.GetPubKey();
        ptx.blsPubKeyOperator = opPubKey;
        ptx.blsPoP = blsPoP;
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
        {
            CMasternodeWalletRecord metaRec;
            if (pwalletMain->GetMasternodeMeta(colOut, metaRec)) {
                pwalletMain->UpdateMasternodeProTxHash(colOut, txHash, addr.ToString(), HexStr(opPubKey.begin(), opPubKey.end()));
            } else {
                bool fTrackInWallet = false;
                {
                    LOCK(pwalletMain->cs_wallet);
                    if (pwalletMain->HaveKey(*ownerKeyID) || pwalletMain->HaveKey(*voteKeyID)) {
                        fTrackInWallet = true;
                    } else {
                        auto itCol = pwalletMain->mapWallet.find(colOut.hash);
                        if (itCol != pwalletMain->mapWallet.end() && colOut.n < itCol->second.vout.size() &&
                            pwalletMain->IsMine(itCol->second.vout[colOut.n]) != ISMINE_NO) {
                            fTrackInWallet = true;
                        }
                    }
                }
                if (fTrackInWallet) {
                    metaRec.collateralOutpoint = colOut;
                    metaRec.strService = addr.ToString();
                    metaRec.strOperatorBlsPubKey = HexStr(opPubKey.begin(), opPubKey.end());
                    if (blsPoP.IsValid()) {
                        metaRec.strOperatorBlsPoP = HexStr(blsPoP.begin(), blsPoP.end());
                    }
                    metaRec.proTxHash = txHash;
                    metaRec.nOperatorRewardBps = (uint16_t)opReward;
                    metaRec.strPayoutAddress = EncodeDestination(payoutDest);
                    metaRec.nCreateTime = GetTime();
                    pwalletMain->AddOrUpdateMasternodeMeta(metaRec);
                }
            }
        }
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
        const bool fOpSkOk = opSk.SetBuf(vchOpSk.data(), vchOpSk.size()) && opSk.IsValid();
        if (!vchOpSk.empty()) {
            memory_cleanse(vchOpSk.data(), vchOpSk.size());
        }
        if (!fOpSkOk) {
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
        const int nNextHeight = GetProTxNextHeight();
        const std::string& servDst = GetBLSDomainTag(BLSDomain::PROTX_UPSERV, IsHardeningV3Active(nNextHeight));
        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_UPSERV, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            if (!ptx.blsSig.Sign(opSk, ptx.GetSignHash(), servDst)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to BLS-sign ProUpServTx");
            }
            return dip3::BuildProTxScript(dip3::PROTX_UPSERV, ptx);
        });
        opSk.Reset();
        if (pwalletMain) {
            CMasternodeWalletRecord metaRec;
            if (pwalletMain->GetMasternodeMeta(pmn->collateralOutpoint, metaRec)) {
                pwalletMain->UpdateMasternodeProTxHash(pmn->collateralOutpoint, proTxHash, addr.ToString(), "");
            }
        }
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

        const int nNextHeight = GetProTxNextHeight();
        const bool fHardeningV3 = IsHardeningV3Active(nNextHeight);
        CBLSSignature blsPoP;
        if (request.params.size() > 5 && !request.params[5].get_str().empty()) {
            std::vector<unsigned char> vchPoP = ParseHexV(request.params[5], "operatorBlsPoP");
            if (vchPoP.size() != 96 || !blsPoP.SetBuf(vchPoP.data(), vchPoP.size()) || !blsPoP.VerifyPoP(opPubKey)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 96-byte BLS operatorBlsPoP for operatorPubKey");
            }
        } else {
            TryResolveLocalOperatorPoP(&pmn->collateralOutpoint, opPubKey, blsPoP);
        }
        if (fHardeningV3 && (!blsPoP.IsValid() || !blsPoP.VerifyPoP(opPubKey))) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "KTDIP3 ProUpRegTx v2 requires <operatorBlsPoP> (5th param) or local operator secret key");
        }

        dip3::CProUpRegTx ptx;
        ptx.nVersion = (fHardeningV3 || blsPoP.IsValid()) ? 2 : 1;
        ptx.proTxHash = proTxHash;
        ptx.blsPubKeyOperator = opPubKey;
        ptx.blsPoP = blsPoP;
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
        const bool fRevSkOk = opSk.SetBuf(vchOpSk.data(), vchOpSk.size()) && opSk.IsValid();
        if (!vchOpSk.empty()) {
            memory_cleanse(vchOpSk.data(), vchOpSk.size());
        }
        if (!fRevSkOk) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid 32-byte BLS operatorSecretKey");
        }
        int reason = (request.params.size() > 3) ? ParseProTxIntArg(request.params[3], "reason") : 1;
        if (reason <= 0 || reason > 65535) throw JSONRPCError(RPC_INVALID_PARAMETER, "reason must be 1..65535");

        dip3::CProUpRevTx ptx;
        ptx.nVersion = 1;
        ptx.proTxHash = proTxHash;
        ptx.nReason = (uint16_t)reason;
        const int nNextHeight = GetProTxNextHeight();
        const std::string& revDst = GetBLSDomainTag(BLSDomain::PROTX_UPREV, IsHardeningV3Active(nNextHeight));
        CScript dummyScript = dip3::BuildProTxScript(dip3::PROTX_UPREV, ptx);
        uint256 txHash = CreateAndSendProTx(dummyScript, [&](const uint256& inputsHash) {
            ptx.inputsHash = inputsHash;
            if (!ptx.blsSig.Sign(opSk, ptx.GetSignHash(), revDst)) {
                throw JSONRPCError(RPC_WALLET_ERROR, "Failed to BLS-sign ProUpRevTx");
            }
            return dip3::BuildProTxScript(dip3::PROTX_UPREV, ptx);
        });
        opSk.Reset();
        return txHash.ToString();
    }

    if (cmd == "wallet_list") {
        if (!pwalletMain) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
        }

        std::set<COutPoint> setMempoolPending;
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
                        setMempoolPending.insert(pendingPtx.collateralOutpoint);
                    } catch (...) {
                    }
                }
            }
        }

        LOCK2(cs_main, pwalletMain->cs_wallet);
        std::map<COutPoint, CMasternodeWalletRecord> mapMeta;
        std::vector<COutPoint> orderedOutpoints;
        std::set<COutPoint> seenOutpoints;
        for (const CMasternodeWalletRecord& rec : pwalletMain->GetAllMasternodeMeta()) {
            mapMeta[rec.collateralOutpoint] = rec;
            if (seenOutpoints.insert(rec.collateralOutpoint).second) {
                orderedOutpoints.push_back(rec.collateralOutpoint);
            }
        }
        for (const auto& kv : mnList.mapMNs) {
            const dip3::CDeterministicMN& dmn = kv.second;
            if (seenOutpoints.count(dmn.collateralOutpoint)) {
                continue;
            }
            bool fOwned = pwalletMain->HaveKey(dmn.state.pubKeyOwner.GetID()) || pwalletMain->HaveKey(dmn.state.keyIDVoting);
            if (!fOwned) {
                auto itCol = pwalletMain->mapWallet.find(dmn.collateralOutpoint.hash);
                if (itCol != pwalletMain->mapWallet.end() && dmn.collateralOutpoint.n < itCol->second.vout.size() &&
                    pwalletMain->IsMine(itCol->second.vout[dmn.collateralOutpoint.n]) != ISMINE_NO) {
                    fOwned = true;
                }
            }
            if (fOwned && seenOutpoints.insert(dmn.collateralOutpoint).second) {
                orderedOutpoints.push_back(dmn.collateralOutpoint);
            }
        }

        UniValue arr(UniValue::VARR);
        for (const COutPoint& colOut : orderedOutpoints) {
            const CMasternodeWalletRecord rec = mapMeta.count(colOut) ? mapMeta.at(colOut) : CMasternodeWalletRecord();
            const dip3::CDeterministicMN* dmn = mnList.GetMNByCollateral(colOut);

            int nConfirmations = 0;
            auto itWtx = pwalletMain->mapWallet.find(colOut.hash);
            if (itWtx != pwalletMain->mapWallet.end() && colOut.n < itWtx->second.vout.size()) {
                nConfirmations = itWtx->second.GetDepthInMainChain();
            } else if (pcoinsTip) {
                Coin coin;
                if (pcoinsTip->GetCoin(colOut, coin) && !coin.IsSpent() && coin.nHeight > 0 && chainActive.Height() >= (int)coin.nHeight) {
                    nConfirmations = chainActive.Height() - (int)coin.nHeight + 1;
                }
            }

            std::string strStatus;
            if (dmn) {
                if (dmn->state.nRevocationReason > 0) {
                    strStatus = "REVOKED";
                } else if (dmn->state.nPoSeBanHeight >= 0) {
                    strStatus = "POSE_BANNED";
                } else {
                    strStatus = "ENABLED";
                }
            } else if (setMempoolPending.count(colOut)) {
                strStatus = "PENDING_MEMPOOL";
            } else if (nConfirmations >= MASTERNODE_MIN_CONFIRMATIONS) {
                strStatus = "READY_FOR_PROTX";
            } else {
                strStatus = "DRAFT_PENDING_CONFS";
            }

            std::string strPayout = rec.strPayoutAddress;
            if (dmn) {
                CTxDestination payoutDest;
                if (ExtractDestination(dmn->state.scriptPayout, payoutDest)) {
                    strPayout = EncodeDestination(payoutDest);
                }
            }

            UniValue obj(UniValue::VOBJ);
            obj.push_back(Pair("alias", rec.strAlias));
            obj.push_back(Pair("collateralHash", colOut.hash.ToString()));
            obj.push_back(Pair("collateralIndex", (int)colOut.n));
            obj.push_back(Pair("collateralOutpoint", strprintf("%s:%u", colOut.hash.ToString(), colOut.n)));
            obj.push_back(Pair("service", dmn ? dmn->state.addr.ToString() : rec.strService));
            obj.push_back(Pair("operatorBlsPubKey", dmn ? HexStr(dmn->state.blsPubKeyOperator.begin(), dmn->state.blsPubKeyOperator.end()) : rec.strOperatorBlsPubKey));
            obj.push_back(Pair("hasLocalOperatorKey", pwalletMain->HasMasternodeOperatorKey(colOut)));
            obj.push_back(Pair("proTxHash", dmn ? dmn->proTxHash.ToString() : (rec.proTxHash.IsNull() ? "" : rec.proTxHash.ToString())));
            obj.push_back(Pair("onChain", dmn != nullptr));
            obj.push_back(Pair("status", strStatus));
            obj.push_back(Pair("confirmations", nConfirmations));
            obj.push_back(Pair("registeredHeight", dmn ? dmn->state.nRegisteredHeight : -1));
            obj.push_back(Pair("lastPaidHeight", dmn ? dmn->state.nLastPaidHeight : 0));
            obj.push_back(Pair("posePenalty", dmn ? dmn->state.nPoSePenalty : 0));
            obj.push_back(Pair("poseBanHeight", dmn ? dmn->state.nPoSeBanHeight : -1));
            obj.push_back(Pair("payoutAddress", strPayout));
            obj.push_back(Pair("operatorReward", dmn ? (int)dmn->state.nOperatorReward : (int)rec.nOperatorRewardBps));
            arr.push_back(obj);
        }
        return arr;
    }

    auto ResolveTargetOutpoint = [&](const std::string& target, COutPoint& colOutRet, uint256& proTxHashRet) -> bool {
        size_t colonPos = target.rfind(':');
        if (colonPos != std::string::npos) {
            std::string hashPart = target.substr(0, colonPos);
            std::string idxPart = target.substr(colonPos + 1);
            int64_t voutIdx64 = -1;
            if (IsHex(hashPart) && hashPart.size() == 64 &&
                ParseInt64(idxPart, &voutIdx64) && voutIdx64 >= 0 && voutIdx64 <= 0xffffffffLL) {
                colOutRet = COutPoint(uint256S(hashPart), static_cast<uint32_t>(voutIdx64));
                const dip3::CDeterministicMN* dmn = mnList.GetMNByCollateral(colOutRet);
                if (dmn) {
                    proTxHashRet = dmn->proTxHash;
                } else {
                    CMasternodeWalletRecord tmp;
                    if (pwalletMain->GetMasternodeMeta(colOutRet, tmp)) {
                        proTxHashRet = tmp.proTxHash;
                    }
                }
                return true;
            }
        }
        if (IsHex(target) && target.size() == 64) {
            uint256 h = uint256S(target);
            const dip3::CDeterministicMN* dmn = mnList.GetMN(h);
            if (dmn) {
                colOutRet = dmn->collateralOutpoint;
                proTxHashRet = dmn->proTxHash;
                return true;
            }
            CMasternodeWalletRecord rec;
            if (pwalletMain->GetMasternodeMetaByProTxHash(h, rec)) {
                colOutRet = rec.collateralOutpoint;
                proTxHashRet = rec.proTxHash;
                return true;
            }
        }
        CMasternodeWalletRecord byAlias;
        if (pwalletMain->GetMasternodeMetaByAlias(target, byAlias)) {
            colOutRet = byAlias.collateralOutpoint;
            const dip3::CDeterministicMN* dmn = mnList.GetMNByCollateral(colOutRet);
            proTxHashRet = dmn ? dmn->proTxHash : byAlias.proTxHash;
            return true;
        }
        return false;
    };

    if (cmd == "set_alias") {
        if (request.params.size() < 3) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx set_alias requires <outpoint_or_protxhash> <alias>");
        }
        if (!pwalletMain) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
        }
        const std::string target = request.params[1].get_str();
        const std::string newAlias = request.params[2].get_str();
        COutPoint colOut;
        uint256 proTxHash;
        if (!ResolveTargetOutpoint(target, colOut, proTxHash)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode outpoint or proTxHash not found");
        }
        std::string strErr;
        if (!pwalletMain->SetMasternodeAlias(colOut, newAlias, strErr, proTxHash)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, strErr);
        }
        UniValue ret(UniValue::VOBJ);
        ret.push_back(Pair("collateralOutpoint", strprintf("%s:%u", colOut.hash.ToString(), colOut.n)));
        ret.push_back(Pair("proTxHash", proTxHash.IsNull() ? "" : proTxHash.ToString()));
        ret.push_back(Pair("alias", newAlias));
        return ret;
    }

    if (cmd == "remove_meta") {
        if (request.params.size() < 2) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "protx remove_meta requires <outpoint_or_protxhash_or_alias> (unlock_collateral)");
        }
        if (!pwalletMain) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Wallet is not available");
        }
        const std::string target = request.params[1].get_str();
        bool fUnlock = false;
        if (request.params.size() > 2 && !request.params[2].isNull()) {
            if (request.params[2].isBool()) {
                fUnlock = request.params[2].get_bool();
            } else if (request.params[2].isStr()) {
                const std::string& s = request.params[2].get_str();
                fUnlock = (s == "true" || s == "1");
            }
        }
        COutPoint colOut;
        uint256 proTxHash;
        if (!ResolveTargetOutpoint(target, colOut, proTxHash)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Masternode metadata entry not found");
        }
        bool fPendingInMempool = false;
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
                            fPendingInMempool = true;
                            break;
                        }
                    } catch (...) {
                    }
                }
            }
        }
        const bool removed = pwalletMain->RemoveMasternodeMeta(colOut);
        bool unlocked = false;
        if (fUnlock && mnList.GetMNByCollateral(colOut) == nullptr && !fPendingInMempool) {
            LOCK(pwalletMain->cs_wallet);
            if (pwalletMain->IsLockedCoin(colOut.hash, colOut.n)) {
                pwalletMain->UnlockCoin(colOut);
                unlocked = true;
            }
        }
        UniValue ret(UniValue::VOBJ);
        ret.push_back(Pair("removed", removed));
        ret.push_back(Pair("collateralOutpoint", strprintf("%s:%u", colOut.hash.ToString(), colOut.n)));
        ret.push_back(Pair("unlocked", unlocked));
        ret.push_back(Pair("pendingInMempool", fPendingInMempool));
        return ret;
    }
#endif // ENABLE_WALLET

    throw JSONRPCError(RPC_INVALID_PARAMETER, "Unknown protx command");
}
