// Copyright (c) 2015-2017 The Bitcoin Core developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "httprpc.h"

#include "base58.h"
#include "chainparams.h"
#include "crypto/hmac_sha256.h"
#include "httpserver.h"
#include "rpc/protocol.h"
#include "rpc/server.h"
#include "random.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "guiinterface.h"

#include <algorithm>
#include <map>
#include <boost/algorithm/string.hpp> // boost::trim

namespace {
struct AuthCooldownEntry {
    int64_t nAllowAfterMs{0};
    int64_t nLastFailMs{0};
    int nFailures{0};
};
RecursiveMutex cs_rpc_auth_cooldown;
std::map<CNetAddr, AuthCooldownEntry> mapRpcAuthCooldown;
} // namespace

/** Simple one-shot callback timer to be used by the RPC mechanism to e.g.
 * re-lock the wellet.
 */
class HTTPRPCTimer : public RPCTimerBase
{
public:
    HTTPRPCTimer(struct event_base* eventBase, std::function<void(void)>& func, int64_t millis) :
        ev(eventBase, false, func)
    {
        struct timeval tv;
        tv.tv_sec = millis/1000;
        tv.tv_usec = (millis%1000)*1000;
        ev.trigger(&tv);
    }
private:
    HTTPEvent ev;
};

class HTTPRPCTimerInterface : public RPCTimerInterface
{
public:
    HTTPRPCTimerInterface(struct event_base* base) : base(base)
    {
    }
    const char* Name()
    {
        return "HTTP";
    }
    RPCTimerBase* NewTimer(std::function<void(void)>& func, int64_t millis)
    {
        return new HTTPRPCTimer(base, func, millis);
    }
private:
    struct event_base* base;
};


/* Pre-base64-encoded authentication token */
static std::string strRPCUserColonPass;
/* Stored RPC timer interface (for unregistration) */
static HTTPRPCTimerInterface* httpRPCTimerInterface = 0;

static void JSONErrorReply(HTTPRequest* req, const UniValue& objError, const UniValue& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    int code = find_value(objError, "code").get_int();

    if (code == RPC_INVALID_REQUEST)
        nStatus = HTTP_BAD_REQUEST;
    else if (code == RPC_METHOD_NOT_FOUND)
        nStatus = HTTP_NOT_FOUND;

    std::string strReply = JSONRPCReply(NullUniValue, objError, id);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(nStatus, strReply);
}

static bool multiUserAuthorized(const std::string& strUserPass)
{
    if (strUserPass.find(":") == std::string::npos) {
        return false;
    }
    const std::string strUser = strUserPass.substr(0, strUserPass.find(":"));
    const std::string strPass = strUserPass.substr(strUserPass.find(":") + 1);

    bool fAuthorized = false;
    if (mapMultiArgs.count("-rpcauth")) {
        for (const std::string& strRPCAuth : mapMultiArgs.at("-rpcauth")) {
            std::string::size_type posColon = strRPCAuth.find(':');
            std::string::size_type posDollar = strRPCAuth.find('$');
            if (posColon == std::string::npos || posDollar == std::string::npos || posDollar < posColon) {
                continue;
            }
            const std::string authUser = strRPCAuth.substr(0, posColon);
            const std::string salt = strRPCAuth.substr(posColon + 1, posDollar - posColon - 1);
            const std::string expectedHex = strRPCAuth.substr(posDollar + 1);
            std::vector<unsigned char> hash(CHMAC_SHA256::OUTPUT_SIZE);
            CHMAC_SHA256((const unsigned char*)salt.data(), salt.size())
                .Write((const unsigned char*)strPass.data(), strPass.size())
                .Finalize(hash.data());
            const bool fUserEqual = TimingResistantEqual(authUser, strUser);
            const bool fPassEqual = TimingResistantEqual(HexStr(hash), boost::algorithm::to_lower_copy(expectedHex));
            if (fUserEqual && fPassEqual) {
                fAuthorized = true;
            }
        }
    }
    return fAuthorized;
}

static bool RPCAuthorized(const std::string& strAuth, std::string& strAuthUsernameOut)
{
    if (strAuth.substr(0, 6) != "Basic ")
        return false;
    std::string strUserPass64 = strAuth.substr(6);
    boost::trim(strUserPass64);
    std::string strUserPass = DecodeBase64(strUserPass64);

    if (strUserPass.find(":") != std::string::npos)
        strAuthUsernameOut = strUserPass.substr(0, strUserPass.find(":"));

    // Check -rpcauth salted HMAC-SHA256 entries first
    if (multiUserAuthorized(strUserPass)) {
        return true;
    }

    if (strRPCUserColonPass.empty()) // Belt-and-suspenders measure if InitRPCAuthentication was not called
        return false;

    return TimingResistantEqual(strUserPass, strRPCUserColonPass);
}

static bool HTTPReq_JSONRPC(HTTPRequest* req, const std::string &)
{
    // JSONRPC handles only POST
    if (req->GetRequestMethod() != HTTPRequest::POST) {
        req->WriteReply(HTTP_BAD_METHOD, "JSONRPC server handles only POST requests");
        return false;
    }
    // Check authorization
    std::pair<bool, std::string> authHeader = req->GetHeader("authorization");
    if (!authHeader.first) {
        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }

    const CNetAddr peerAddr(req->GetPeer());
    const int64_t nNowMs = GetTimeMillis();
    {
        LOCK(cs_rpc_auth_cooldown);
        auto itCd = mapRpcAuthCooldown.find(peerAddr);
        if (itCd != mapRpcAuthCooldown.end() && nNowMs < itCd->second.nAllowAfterMs) {
            req->WriteReply(HTTP_UNAUTHORIZED);
            return false;
        }
    }

    JSONRPCRequest jreq;
    if (!RPCAuthorized(authHeader.second, jreq.authUser)) {
        LogPrintf("ThreadRPCServer incorrect password attempt from %s\n", req->GetPeer().ToString());
        {
            LOCK(cs_rpc_auth_cooldown);
            if (mapRpcAuthCooldown.size() >= 1024) {
                for (auto it = mapRpcAuthCooldown.begin(); it != mapRpcAuthCooldown.end(); ) {
                    if (nNowMs - it->second.nLastFailMs > 60000) {
                        it = mapRpcAuthCooldown.erase(it);
                    } else {
                        ++it;
                    }
                }
                while (mapRpcAuthCooldown.size() >= 1024) {
                    auto itOldest = mapRpcAuthCooldown.begin();
                    for (auto it = mapRpcAuthCooldown.begin(); it != mapRpcAuthCooldown.end(); ++it) {
                        if (it->second.nLastFailMs < itOldest->second.nLastFailMs) {
                            itOldest = it;
                        }
                    }
                    mapRpcAuthCooldown.erase(itOldest);
                }
            }
            AuthCooldownEntry& entry = mapRpcAuthCooldown[peerAddr];
            if (nNowMs - entry.nLastFailMs > 60000) {
                entry.nFailures = 0;
            }
            entry.nFailures = std::min(entry.nFailures + 1, 5);
            const int64_t nBackoffMs = std::min<int64_t>(4000, 250LL << (entry.nFailures - 1));
            entry.nLastFailMs = nNowMs;
            entry.nAllowAfterMs = nNowMs + nBackoffMs;
        }

        req->WriteReply(HTTP_UNAUTHORIZED);
        return false;
    }
    {
        LOCK(cs_rpc_auth_cooldown);
        mapRpcAuthCooldown.erase(peerAddr);
    }

    try {
        // Parse request
        UniValue valRequest;
        if (!valRequest.read(req->ReadBody()))
            throw JSONRPCError(RPC_PARSE_ERROR, "Parse error");

        // Set the URI
        jreq.URI = req->GetURI();

        std::string strReply;
        // singleton request
        if (valRequest.isObject()) {
            jreq.parse(valRequest);

            UniValue result = tableRPC.execute(jreq);

            // Send reply
            strReply = JSONRPCReply(result, NullUniValue, jreq.id);

        // array of requests
        } else if (valRequest.isArray())
            strReply = JSONRPCExecBatch(valRequest.get_array());
        else
            throw JSONRPCError(RPC_PARSE_ERROR, "Top-level object parse error");

        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(HTTP_OK, strReply);
    } catch (const UniValue& objError) {
        JSONErrorReply(req, objError, jreq.id);
        return false;
    } catch (const std::exception& e) {
        JSONErrorReply(req, JSONRPCError(RPC_PARSE_ERROR, e.what()), jreq.id);
        return false;
    }
    return true;
}

static bool InitRPCAuthentication()
{
    if (mapArgs["-rpcpassword"] == "")
    {
        LogPrintf("No rpcpassword set - using random cookie authentication\n");
        if (!GenerateAuthCookie(&strRPCUserColonPass)) {
            uiInterface.ThreadSafeMessageBox(
                _("Error: A fatal internal error occurred, see debug.log for details"), // Same message as AbortNode
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
    } else {
        strRPCUserColonPass = mapArgs["-rpcuser"] + ":" + mapArgs["-rpcpassword"];
    }
    return true;
}

bool StartHTTPRPC()
{
    LogPrint(BCLog::RPC, "Starting HTTP RPC server\n");
    if (!InitRPCAuthentication())
        return false;

    RegisterHTTPHandler("/", true, HTTPReq_JSONRPC);

    assert(EventBase());
    httpRPCTimerInterface = new HTTPRPCTimerInterface(EventBase());
    RPCSetTimerInterface(httpRPCTimerInterface);
    return true;
}

void InterruptHTTPRPC()
{
    LogPrint(BCLog::RPC, "Interrupting HTTP RPC server\n");
}

void StopHTTPRPC()
{
    LogPrint(BCLog::RPC, "Stopping HTTP RPC server\n");
    UnregisterHTTPHandler("/", true);
    if (httpRPCTimerInterface) {
        RPCUnsetTimerInterface(httpRPCTimerInterface);
        delete httpRPCTimerInterface;
        httpRPCTimerInterface = 0;
    }
}
