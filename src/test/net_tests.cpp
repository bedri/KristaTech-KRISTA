// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "addrman.h"
#include "chainparams.h"
#include "hash.h"
#include "net.h"
#include "netbase.h"
#include "protocol.h"
#include "serialize.h"
#include "streams.h"

#include "test/test_kristatech.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include <boost/test/unit_test.hpp>

class CAddrManSerializationMock : public CAddrMan
{
public:
    virtual void Serialize(CDataStream::CBaseDataStream& s) const = 0;

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic()
    {
        nKey.SetNull();
        SeedInsecureRand(true);
    }
};

class CAddrManUncorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream::CBaseDataStream& s) const
    {
        CAddrMan::Serialize(s);
    }
};

class CAddrManCorrupted : public CAddrManSerializationMock
{
public:
    void Serialize(CDataStream::CBaseDataStream& s) const
    {
        // Produces corrupt output that claims addrman has 20 addrs when it only has one addr.
        unsigned char nVersion = 1;
        s << nVersion;
        s << ((unsigned char)32);
        s << nKey;
        s << 10; // nNew
        s << 10; // nTried

        int nUBuckets = ADDRMAN_NEW_BUCKET_COUNT ^ (1 << 30);
        s << nUBuckets;

        CService serv;
        Lookup("252.1.1.1", serv, 7777, false);
        CAddress addr = CAddress(serv, NODE_NONE);
        CNetAddr resolved;
        LookupHost("252.2.2.2", resolved, false);
        CAddrInfo info = CAddrInfo(addr, resolved);
        s << info;
    }
};

CDataStream AddrmanToStream(CAddrManSerializationMock& addrman)
{
    CDataStream ssPeersIn(SER_DISK, CLIENT_VERSION);
    ssPeersIn << FLATDATA(Params().MessageStart());
    ssPeersIn << addrman;
    std::string str = ssPeersIn.str();
    std::vector<unsigned char> vchData(str.begin(), str.end());
    return CDataStream(vchData, SER_DISK, CLIENT_VERSION);
}

BOOST_FIXTURE_TEST_SUITE(net_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(caddrdb_read)
{
    CAddrManUncorrupted addrmanUncorrupted;
    addrmanUncorrupted.MakeDeterministic();

    CService addr1, addr2, addr3;
    Lookup("250.7.1.1", addr1, 8333, false);
    Lookup("250.7.2.2", addr2, 9999, false);
    Lookup("250.7.3.3", addr3, 9999, false);

    // Add three addresses to new table.
    CService source;
    Lookup("252.5.1.1", source, 8333, false);
    addrmanUncorrupted.Add(CAddress(addr1, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr2, NODE_NONE), source);
    addrmanUncorrupted.Add(CAddress(addr3, NODE_NONE), source);

    // Test that the de-serialization does not throw an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanUncorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;

    BOOST_CHECK(addrman1.size() == 0);
    try {
        unsigned char pchMsgTmp[4];
        ssPeers1 >> FLATDATA(pchMsgTmp);
        ssPeers1 >> addrman1;
    } catch (const std::exception& e) {
        exceptionThrown = true;
    }

    BOOST_CHECK(addrman1.size() == 3);
    BOOST_CHECK(exceptionThrown == false);

    // Test that CAddrDB::Read creates an addrman with the correct number of addrs.
    CDataStream ssPeers2 = AddrmanToStream(addrmanUncorrupted);

    CAddrMan addrman2;
    CAddrDB adb;
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 3);
}


BOOST_AUTO_TEST_CASE(caddrdb_read_corrupted)
{
    CAddrManCorrupted addrmanCorrupted;
    addrmanCorrupted.MakeDeterministic();

    // Test that the de-serialization of corrupted addrman throws an exception.
    CDataStream ssPeers1 = AddrmanToStream(addrmanCorrupted);
    bool exceptionThrown = false;
    CAddrMan addrman1;
    BOOST_CHECK(addrman1.size() == 0);
    try {
        unsigned char pchMsgTmp[4];
        ssPeers1 >> FLATDATA(pchMsgTmp);
        ssPeers1 >> addrman1;
    } catch (const std::exception& e) {
        exceptionThrown = true;
    }
    // Even through de-serialization failed addrman is not left in a clean state.
    BOOST_CHECK(addrman1.size() == 1);
    BOOST_CHECK(exceptionThrown);

    // Test that CAddrDB::Read leaves addrman in a clean state if de-serialization fails.
    CDataStream ssPeers2 = AddrmanToStream(addrmanCorrupted);

    CAddrMan addrman2;
    CAddrDB adb;
    BOOST_CHECK(addrman2.size() == 0);
    adb.Read(addrman2, ssPeers2);
    BOOST_CHECK(addrman2.size() == 0);
}

BOOST_AUTO_TEST_CASE(cnode_simple_test)
{
    SOCKET hSocket = INVALID_SOCKET;
    NodeId id = 0;
    int height = 0;

    in_addr ipv4Addr;
    ipv4Addr.s_addr = 0xa0b0c001;

    CAddress addr = CAddress(CService(ipv4Addr, 7777), NODE_NETWORK);
    std::string pszDest = "";
    bool fInboundIn = false;

    // Test that fFeeler is false by default.
    CNode* pnode1 = new CNode(id++, NODE_NETWORK, height, hSocket, addr, 0, 0, pszDest, fInboundIn);
    BOOST_CHECK(pnode1->fInbound == false);
    BOOST_CHECK(pnode1->fFeeler == false);

    fInboundIn = true;
    CNode* pnode2 = new CNode(id++, NODE_NETWORK, height, hSocket, addr, 1, 1, pszDest, fInboundIn);
    BOOST_CHECK(pnode2->fInbound == true);
    BOOST_CHECK(pnode2->fFeeler == false);
}

BOOST_AUTO_TEST_CASE(c1_h9_m9_net_hardening_regression)
{
    // H-9: Default receive buffer must accommodate >5 MB messages (16 MB default)
    BOOST_CHECK_GE(DEFAULT_MAXRECEIVEBUFFER, 16000U);

    // M-9: CInv::IsMasterNodeType must exclude MSG_CMPCT_BLOCK while matching masternode types
    BOOST_CHECK(!CInv(MSG_CMPCT_BLOCK, uint256()).IsMasterNodeType());
    BOOST_CHECK(!CInv(MSG_TX, uint256()).IsMasterNodeType());
    BOOST_CHECK(!CInv(MSG_BLOCK, uint256()).IsMasterNodeType());
    BOOST_CHECK(CInv(MSG_SPORK, uint256()).IsMasterNodeType());
    BOOST_CHECK(CInv(MSG_MASTERNODE_PING, uint256()).IsMasterNodeType());
    BOOST_CHECK(CInv(MSG_MASTERNODE_QUORUM, uint256()).IsMasterNodeType());

    // M-9 & H-9: 12-byte non-NUL command header and chunked 6 MB v1 message over CNode::ReceiveMsgBytes
    std::vector<unsigned char> payload(6 * 1024 * 1024, 0x42);
    CMessageHeader hdr(Params().MessageStart(), "twelvebytes!", static_cast<unsigned int>(payload.size()));
    BOOST_CHECK(std::memchr(hdr.pchCommand, '\0', CMessageHeader::COMMAND_SIZE) == nullptr);
    BOOST_CHECK_EQUAL(hdr.GetCommand(), "twelvebytes!");
    BOOST_CHECK(hdr.IsValid(Params().MessageStart()));

    uint256 hPayload = Hash(payload.begin(), payload.end());
    std::memcpy(hdr.pchChecksum, hPayload.begin(), CMessageHeader::CHECKSUM_SIZE);

    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << hdr;
    wire.write(reinterpret_cast<const char*>(payload.data()), payload.size());
    const std::string wireBytes = wire.str();

    CAddress addr(LookupNumeric("127.0.0.1", 27999), NODE_NETWORK);
    std::unique_ptr<CNode> pnode(new CNode(999, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true));
    pnode->fV2AutoDetect = false;
    pnode->fV2Transport = false;

    bool complete = false;
    const size_t chunkSz = 65536;
    for (size_t off = 0; off < wireBytes.size(); off += chunkSz) {
        const unsigned int nChunk = static_cast<unsigned int>(std::min(chunkSz, wireBytes.size() - off));
        BOOST_REQUIRE(pnode->ReceiveMsgBytes(wireBytes.data() + off, nChunk, complete));
        if (off + nChunk < wireBytes.size()) {
            BOOST_CHECK(!complete);
        }
    }
    BOOST_CHECK(complete);
    BOOST_CHECK(!pnode->fPauseRecv);
    BOOST_CHECK_GE(pnode->GetTotalRecvSize(), payload.size());
}

BOOST_AUTO_TEST_SUITE_END()
