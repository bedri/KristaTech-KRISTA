// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "protocol.h"

#include "util.h"
#include "utilstrencodings.h"

#ifndef WIN32
#include <arpa/inet.h>
#endif

namespace NetMsgType
{
const char* VERSION = "version";
const char* VERACK = "verack";
const char* ADDR = "addr";
const char* INV = "inv";
const char* GETDATA = "getdata";
const char* MERKLEBLOCK = "merkleblock";
const char* GETBLOCKS = "getblocks";
const char* GETHEADERS = "getheaders";
const char* TX = "tx";
const char* HEADERS = "headers";
const char* BLOCK = "block";
const char* GETADDR = "getaddr";
const char* MEMPOOL = "mempool";
const char* PING = "ping";
const char* PONG = "pong";
const char* ALERT = "alert";
const char* NOTFOUND = "notfound";
const char* FILTERLOAD = "filterload";
const char* FILTERADD = "filteradd";
const char* FILTERCLEAR = "filterclear";
const char* REJECT = "reject";
const char* SENDHEADERS = "sendheaders";
const char* IX = "ix";
const char* IXLOCKVOTE = "txlvote";
const char* SPORK = "spork";
const char* GETSPORKS = "getsporks";
const char* MNBROADCAST = "mnb";
const char* MNPING = "mnp";
const char* MNWINNER = "mnw";
const char* GETMNWINNERS = "mnget";
const char* SYNCSTATUSCOUNT = "ssc";
const char* GETMNLIST = "dseg";
const char* ADAMSOL = "adamsol";
}; // namespace NetMsgType

// ppszTypeName removed in favor of switch-based type resolution

/** All known message types. Keep this in the same order as the list of
 * messages above and in protocol.h.
 */
const static std::string allNetMessageTypes[] = {
    NetMsgType::VERSION,
    NetMsgType::VERACK,
    NetMsgType::ADDR,
    NetMsgType::INV,
    NetMsgType::GETDATA,
    NetMsgType::MERKLEBLOCK,
    NetMsgType::GETBLOCKS,
    NetMsgType::GETHEADERS,
    NetMsgType::TX,
    NetMsgType::HEADERS,
    NetMsgType::BLOCK,
    NetMsgType::GETADDR,
    NetMsgType::MEMPOOL,
    NetMsgType::PING,
    NetMsgType::PONG,
    NetMsgType::ALERT,
    NetMsgType::NOTFOUND,
    NetMsgType::FILTERLOAD,
    NetMsgType::FILTERADD,
    NetMsgType::FILTERCLEAR,
    NetMsgType::REJECT,
    NetMsgType::SENDHEADERS,
    NetMsgType::IX,
    NetMsgType::IXLOCKVOTE,
    NetMsgType::SPORK,
    NetMsgType::GETSPORKS,
    NetMsgType::MNBROADCAST,
    NetMsgType::MNPING,
    NetMsgType::MNWINNER,
    NetMsgType::GETMNWINNERS,
    NetMsgType::GETMNLIST,
    NetMsgType::SYNCSTATUSCOUNT,
    NetMsgType::ADAMSOL,
};
const static std::vector<std::string> allNetMessageTypesVec(allNetMessageTypes, allNetMessageTypes + ARRAYLEN(allNetMessageTypes));

CMessageHeader::CMessageHeader(const MessageStartChars& pchMessageStartIn)
{
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    nMessageSize = -1;
    memset(pchChecksum, 0, CHECKSUM_SIZE);
}

CMessageHeader::CMessageHeader(const MessageStartChars& pchMessageStartIn, const char* pszCommand, unsigned int nMessageSizeIn)
{
    memcpy(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE);
    memset(pchCommand, 0, sizeof(pchCommand));
    strncpy(pchCommand, pszCommand, COMMAND_SIZE);
    nMessageSize = nMessageSizeIn;
    memset(pchChecksum, 0, CHECKSUM_SIZE);
}

std::string CMessageHeader::GetCommand() const
{
    return std::string(pchCommand, pchCommand + strnlen(pchCommand, COMMAND_SIZE));
}

bool CMessageHeader::IsValid(const MessageStartChars& pchMessageStartIn) const
{
    // Check start string
    if (memcmp(pchMessageStart, pchMessageStartIn, MESSAGE_START_SIZE) != 0)
        return false;

    // Check the command string for errors
    for (const char* p1 = pchCommand; p1 < pchCommand + COMMAND_SIZE; p1++) {
        if (*p1 == 0) {
            // Must be all zeros after the first zero
            for (; p1 < pchCommand + COMMAND_SIZE; p1++)
                if (*p1 != 0)
                    return false;
        } else if (*p1 < ' ' || *p1 > 0x7E)
            return false;
    }

    // Message size
    if (nMessageSize > MAX_SIZE) {
        LogPrintf("CMessageHeader::IsValid() : (%s, %u bytes) nMessageSize > MAX_SIZE\n", GetCommand(), nMessageSize);
        return false;
    }

    return true;
}


CAddress::CAddress() : CService()
{
    Init();
}

CAddress::CAddress(CService ipIn, ServiceFlags nServicesIn) : CService(ipIn)
{
    Init();
    nServices = nServicesIn;
}

void CAddress::Init()
{
    nServices = NODE_NONE;
    nTime = 100000000;
}

CInv::CInv()
{
    type = 0;
    hash.SetNull();
}

CInv::CInv(int typeIn, const uint256& hashIn)
{
    type = typeIn;
    hash = hashIn;
}

CInv::CInv(const std::string& strType, const uint256& hashIn)
{
    if (strType == NetMsgType::TX) type = MSG_TX;
    else if (strType == NetMsgType::BLOCK) type = MSG_BLOCK;
    else if (strType == "filtered block") type = MSG_FILTERED_BLOCK;
    else if (strType == NetMsgType::IX) type = 4;
    else if (strType == NetMsgType::IXLOCKVOTE) type = 5;
    else if (strType == NetMsgType::SPORK) type = MSG_SPORK;
    else if (strType == NetMsgType::MNWINNER) type = MSG_MASTERNODE_WINNER;
    else if (strType == "mnse") type = MSG_MASTERNODE_SCANNING_ERROR;
    else if (strType == "mnq") type = MSG_MASTERNODE_QUORUM;
    else if (strType == NetMsgType::MNBROADCAST) type = MSG_MASTERNODE_ANNOUNCE;
    else if (strType == NetMsgType::MNPING) type = MSG_MASTERNODE_PING;
    else if (strType == "dstx") type = MSG_DSTX;
    else {
        type = 0;
        LogPrint(BCLog::NET, "CInv::CInv(string, uint256) : unknown type '%s'", strType);
    }
    hash = hashIn;
}

bool operator<(const CInv& a, const CInv& b)
{
    return (a.type < b.type || (a.type == b.type && a.hash < b.hash));
}

bool CInv::IsKnownType() const
{
    switch (type) {
    case MSG_TX:
    case MSG_BLOCK:
    case MSG_FILTERED_BLOCK:
    case 4:
    case 5:
    case MSG_SPORK:
    case MSG_MASTERNODE_WINNER:
    case MSG_MASTERNODE_SCANNING_ERROR:
    case MSG_MASTERNODE_QUORUM:
    case MSG_MASTERNODE_ANNOUNCE:
    case MSG_MASTERNODE_PING:
    case MSG_DSTX:
        return true;
    default:
        return false;
    }
}

bool CInv::IsMasterNodeType() const{
     return (type >= 6);
}

const char* CInv::GetCommand() const
{
    switch (type) {
    case MSG_TX:                         return NetMsgType::TX;
    case MSG_BLOCK:                      return NetMsgType::BLOCK;
    case MSG_FILTERED_BLOCK:             return "filtered block";
    case 4:                              return NetMsgType::IX;
    case 5:                              return NetMsgType::IXLOCKVOTE;
    case MSG_SPORK:                      return NetMsgType::SPORK;
    case MSG_MASTERNODE_WINNER:          return NetMsgType::MNWINNER;
    case MSG_MASTERNODE_SCANNING_ERROR:  return "mnse";
    case MSG_MASTERNODE_QUORUM:          return "mnq";
    case MSG_MASTERNODE_ANNOUNCE:        return NetMsgType::MNBROADCAST;
    case MSG_MASTERNODE_PING:            return NetMsgType::MNPING;
    case MSG_DSTX:                       return "dstx";
    default:
        LogPrint(BCLog::NET, "CInv::GetCommand() : type=%d unknown type", type);
        return "UNKNOWN";
    }
}

std::string CInv::ToString() const
{
    return strprintf("%s %s", GetCommand(), hash.ToString());
}

const std::vector<std::string>& getAllNetMessageTypes()
{
    return allNetMessageTypesVec;
}
