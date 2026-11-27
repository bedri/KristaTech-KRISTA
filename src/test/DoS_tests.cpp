// Copyright (c) 2011-2014 The Bitcoin Core developers
// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for denial-of-service detection/prevention code
//



#include "adam.h"
#include "arith_uint256.h"
#include "blockencodings.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "hash.h"
#include "keystore.h"
#include "main.h"
#include "net.h"
#include "net_processing.h"
#include "netmessagemaker.h"
#include "pow.h"
#include "primitives/block.h"
#include "script/sign.h"
#include "serialize.h"
#include "util.h"

#include "test/test_kristatech.h"

#include <cstring>
#include <stdint.h>

#include <boost/assign/list_of.hpp> // for 'map_list_of()'
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/test/unit_test.hpp>

// Tests this internal-to-main.cpp method:
extern bool AddOrphanTx(const CTransaction& tx, NodeId peer);
extern void EraseOrphansFor(NodeId peer);
extern unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans);
extern std::map<uint256, COrphanTx> mapOrphanTransactions;
extern std::map<uint256, std::set<uint256> > mapOrphanTransactionsByPrev;

CService ip(uint32_t i)
{
    struct in_addr s;
    s.s_addr = i;
    return CService(CNetAddr(s), Params().GetDefaultPort());
}

static NodeId id = 0;

BOOST_FIXTURE_TEST_SUITE(DoS_tests, TestingSetup)

void misbehave(NodeId id, int value) {
    LOCK(cs_main);
    Misbehaving(id, value); // Should get banned
}

BOOST_AUTO_TEST_CASE(DoS_banning)
{
    std::atomic<bool> interruptDummy(false);

    connman->ClearBanned();
    CAddress addr1(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 0, 0, "", true);
    dummyNode1.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode1, *connman);
    dummyNode1.nVersion = 1;
    dummyNode1.fSuccessfullyConnected = true;
    misbehave(dummyNode1.GetId(), 100); // Should get banned
    SendMessages(&dummyNode1, *connman, interruptDummy);
    BOOST_CHECK(connman->IsBanned(addr1));
    BOOST_CHECK(!connman->IsBanned(ip(0xa0b0c001|0x0000ff00))); // Different IP, not banned

    CAddress addr2(ip(0xa0b0c002), NODE_NONE);
    CNode dummyNode2(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr2, 1, 1, "", true);
    dummyNode2.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode2, *connman);
    dummyNode2.nVersion = 1;
    dummyNode2.fSuccessfullyConnected = true;
    misbehave(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2, *connman, interruptDummy);
    BOOST_CHECK(!connman->IsBanned(addr2)); // 2 not banned yet...
    BOOST_CHECK(connman->IsBanned(addr1));  // ... but 1 still should be
    misbehave(dummyNode2.GetId(), 50);
    SendMessages(&dummyNode2, *connman, interruptDummy);
    BOOST_CHECK(connman->IsBanned(addr2));
}

BOOST_AUTO_TEST_CASE(DoS_banscore)
{
    std::atomic<bool> interruptDummy(false);

    connman->ClearBanned();
    mapArgs["-banscore"] = "111"; // because 11 is my favorite number
    CAddress addr1(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode1(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr1, 3, 1, "", true);
    dummyNode1.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode1, *connman);
    dummyNode1.nVersion = 1;
    dummyNode1.fSuccessfullyConnected = true;
    misbehave(dummyNode1.GetId(), 100);
    SendMessages(&dummyNode1, *connman, interruptDummy);
    BOOST_CHECK(!connman->IsBanned(addr1));
    misbehave(dummyNode1.GetId(), 10);
    SendMessages(&dummyNode1, *connman, interruptDummy);
    BOOST_CHECK(!connman->IsBanned(addr1));
    misbehave(dummyNode1.GetId(), 1);
    SendMessages(&dummyNode1, *connman, interruptDummy);
    BOOST_CHECK(connman->IsBanned(addr1));
    mapArgs.erase("-banscore");
}

BOOST_AUTO_TEST_CASE(DoS_bantime)
{
    std::atomic<bool> interruptDummy(false);

    connman->ClearBanned();
    int64_t nStartTime = GetTime();
    SetMockTime(nStartTime); // Overrides future calls to GetTime()

    CAddress addr(ip(0xa0b0c001), NODE_NONE);
    CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 4, 4, "", true);
    dummyNode.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode, *connman);
    dummyNode.nVersion = 1;
    dummyNode.fSuccessfullyConnected = true;

    misbehave(dummyNode.GetId(), 100);
    SendMessages(&dummyNode, *connman, interruptDummy);
    BOOST_CHECK(connman->IsBanned(addr));

    SetMockTime(nStartTime+60*60);
    BOOST_CHECK(connman->IsBanned(addr));

    SetMockTime(nStartTime+60*60*24+1);
    BOOST_CHECK(!connman->IsBanned(addr));
}

CTransaction RandomOrphan()
{
    std::map<uint256, COrphanTx>::iterator it;
    it = mapOrphanTransactions.lower_bound(InsecureRand256());
    if (it == mapOrphanTransactions.end())
        it = mapOrphanTransactions.begin();
    return it->second.tx;
}

BOOST_AUTO_TEST_CASE(DoS_mapOrphans)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    keystore.AddKey(key);

    // 50 orphan transactions:
    for (int i = 0; i < 50; i++)
    {
        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = InsecureRand256();
        tx.vin[0].scriptSig << OP_1;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

        AddOrphanTx(tx, i);
    }

    // ... and 50 that depend on other orphans:
    for (int i = 0; i < 50; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vin.resize(1);
        tx.vin[0].prevout.n = 0;
        tx.vin[0].prevout.hash = txPrev.GetHash();
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        SignSignature(keystore, txPrev, tx, 0, SIGHASH_ALL);

        AddOrphanTx(tx, i);
    }

    // This really-big orphan should be ignored:
    for (int i = 0; i < 10; i++)
    {
        CTransaction txPrev = RandomOrphan();

        CMutableTransaction tx;
        tx.vout.resize(1);
        tx.vout[0].nValue = 1*CENT;
        tx.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());
        tx.vin.resize(500);
        for (unsigned int j = 0; j < tx.vin.size(); j++)
        {
            tx.vin[j].prevout.n = j;
            tx.vin[j].prevout.hash = txPrev.GetHash();
        }
        SignSignature(keystore, txPrev, tx, 0, SIGHASH_ALL);
        // Re-use same signature for other inputs
        // (they don't have to be valid for this test)
        for (unsigned int j = 1; j < tx.vin.size(); j++)
            tx.vin[j].scriptSig = tx.vin[0].scriptSig;

        BOOST_CHECK(!AddOrphanTx(tx, i));
    }

    // Test EraseOrphansFor:
    for (NodeId i = 0; i < 3; i++)
    {
        size_t sizeBefore = mapOrphanTransactions.size();
        EraseOrphansFor(i);
        BOOST_CHECK(mapOrphanTransactions.size() < sizeBefore);
    }

    // Test LimitOrphanTxSize() function:
    LimitOrphanTxSize(40);
    BOOST_CHECK(mapOrphanTransactions.size() <= 40);
    LimitOrphanTxSize(10);
    BOOST_CHECK(mapOrphanTransactions.size() <= 10);
    LimitOrphanTxSize(0);
    BOOST_CHECK(mapOrphanTransactions.empty());
    BOOST_CHECK(mapOrphanTransactionsByPrev.empty());
}

static void PushV1WireMessage(CNode& node, const std::string& strCommand, const std::string& payloadBytes, CConnman& connman)
{
    CMessageHeader hdr(Params().MessageStart(), strCommand.c_str(), static_cast<unsigned int>(payloadBytes.size()));
    uint256 h = Hash(payloadBytes.begin(), payloadBytes.end());
    std::memcpy(hdr.pchChecksum, h.begin(), CMessageHeader::CHECKSUM_SIZE);

    CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
    wire << hdr;
    if (!payloadBytes.empty()) {
        wire.write(payloadBytes.data(), payloadBytes.size());
    }
    const std::string wireStr = wire.str();
    bool complete = false;
    BOOST_REQUIRE(node.ReceiveMsgBytes(wireStr.data(), static_cast<unsigned int>(wireStr.size()), complete));
    BOOST_REQUIRE(complete);
    node.QueueCompleteRecvMessages(connman.GetReceiveFloodSize());
    std::atomic<bool> interruptDummy(false);
    while (GetNodeSignals().ProcessMessages(&node, connman, interruptDummy)) {}
}

BOOST_AUTO_TEST_CASE(DoS_cblockheader_oversized_deser_misbehaves)
{
    // C-4: ProcessMessages catching std::ios_base::failure on oversized CBlockHeader fields must apply Misbehaving(20)
    connman->ClearBanned();
    CAddress addr(ip(0xa0b0c101), NODE_NONE);
    CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
    dummyNode.fV2AutoDetect = false;
    dummyNode.fV2Transport = false;
    dummyNode.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode, *connman);
    dummyNode.nVersion = PROTOCOL_VERSION;
    dummyNode.SetRecvVersion(PROTOCOL_VERSION);
    dummyNode.fSuccessfullyConnected = true;

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    CBlockHeader base;
    base.nVersion = 12;
    payload << base.nVersion << base.hashPrevBlock << base.hashMerkleRoot << base.nTime << base.nBits << base.nNonce;
    WriteCompactSize(payload, 33ULL); // > 32 max vAdamMiners -> throws std::ios_base::failure

    PushV1WireMessage(dummyNode, NetMsgType::PROPOSEBLOCK, payload.str(), *connman);

    CNodeStateStats stats;
    BOOST_REQUIRE(GetNodeStateStats(dummyNode.GetId(), stats));
    BOOST_CHECK_GE(stats.nMisbehavior, 20);

    bool fUpdate = false;
    GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
}

BOOST_AUTO_TEST_CASE(DoS_verack_duplicate_and_adamsol_sync_guard)
{
    // H-10: Duplicate VERACK must be ignored once connected, and initial ADAMSOL sync runs at most once per peer
    connman->ClearBanned();
    const CBaseChainParams::Network prevNet = Params().NetworkID();
    CBlockIndex* pOrigTip = chainActive.Tip();
    CBlockIndex* pOrigBestHeader = pindexBestHeader;
    BOOST_REQUIRE(pOrigTip != nullptr);
    SelectParams(CBaseChainParams::REGTEST);

    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    CBlockIndex* pPrev = pOrigTip;
    for (int i = 0; i < 205; ++i) {
        CBlockIndex* pIdx = new CBlockIndex();
        pIdx->pprev = pPrev;
        pIdx->nHeight = pPrev->nHeight + 1;
        pIdx->nTime = static_cast<unsigned int>(GetTime());
        pIdx->nBits = pOrigTip->nBits;
        pIdx->nChainWork = ArithToUint256(arith_uint256(i + 1));
        uint256 h = InsecureRand256();
        auto itMap = mapBlockIndex.emplace(h, pIdx).first;
        pIdx->phashBlock = &itMap->first;
        pIdx->BuildSkip();
        vHashes.push_back(h);
        vIndices.push_back(pIdx);
        pPrev = pIdx;
    }
    chainActive.SetTip(pPrev);
    pindexBestHeader = pPrev;

    CKey k;
    k.MakeNewKey(true);
    const uint256 tipHash = pPrev->GetBlockHash();
    {
        LOCK(cs_adam_solutions);
        mapAdamSolutionsCache[tipHash][k.GetPubKey()] = {0x01, 0x02, 0x03};
    }

    CAddress addr(ip(0xa0b0c102), NODE_NONE);
    CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
    dummyNode.fV2AutoDetect = false;
    dummyNode.fV2Transport = false;
    dummyNode.fFeeler = false;
    dummyNode.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode, *connman);
    dummyNode.nVersion = PROTOCOL_VERSION;
    dummyNode.fSuccessfullyConnected = false;

    // 1st VERACK sets fSuccessfullyConnected and fSentInitialAdamSolSync and queues ADAMSOL once
    PushV1WireMessage(dummyNode, NetMsgType::VERACK, "", *connman);
    BOOST_CHECK(dummyNode.fSuccessfullyConnected);
    BOOST_CHECK(dummyNode.fSentInitialAdamSolSync.load());
    size_t sendCountAfterFirst = 0;
    {
        LOCK(dummyNode.cs_vSend);
        sendCountAfterFirst = dummyNode.vSendMsg.size();
        dummyNode.vSendMsg.clear();
        dummyNode.nSendSize = 0;
    }
    BOOST_CHECK_GT(sendCountAfterFirst, 0U);

    // 2nd VERACK (even if atomic flag were reset) is ignored due to fSuccessfullyConnected guard
    dummyNode.fSentInitialAdamSolSync.store(false);
    PushV1WireMessage(dummyNode, NetMsgType::VERACK, "", *connman);
    {
        LOCK(dummyNode.cs_vSend);
        BOOST_CHECK_EQUAL(dummyNode.vSendMsg.size(), 0U);
    }

    bool fUpdate = false;
    GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
    {
        LOCK(cs_adam_solutions);
        mapAdamSolutionsCache.erase(tipHash);
    }
    {
        LOCK(cs_main);
        pindexBestHeader = pOrigBestHeader;
        chainActive.SetTip(pOrigTip);
        for (const uint256& h : vHashes) {
            mapBlockIndex.erase(h);
        }
    }
    for (CBlockIndex* pIdx : vIndices) {
        delete pIdx;
    }
    SelectParams(prevNet);
}

BOOST_AUTO_TEST_CASE(DoS_headers_presync_validation_and_nonzero_txcount)
{
    connman->ClearBanned();

    // 1. Disabled HEADERS wire path safely ignores unsolicited HEADERS without touching mapBlockIndex
    BOOST_CHECK(!Params().HeadersFirstSyncingActive());

    // 2. Non-zero tx count inside HEADERS triggers Misbehaving(20)
    {
        CAddress addr(ip(0xa0b0c103), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
        dummyNode.fV2AutoDetect = false;
        dummyNode.fV2Transport = false;
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        GetNodeSignals().InitializeNode(&dummyNode, *connman);
        dummyNode.nVersion = PROTOCOL_VERSION;
        dummyNode.SetRecvVersion(PROTOCOL_VERSION);
        dummyNode.fSuccessfullyConnected = true;

        CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader hdr;
        hdr.nVersion = 12;
        WriteCompactSize(payload, 1ULL);
        payload << hdr;
        WriteCompactSize(payload, 1ULL); // non-zero tx count invalid in HEADERS
        PushV1WireMessage(dummyNode, NetMsgType::HEADERS, payload.str(), *connman);
        BOOST_CHECK_EQUAL(mapBlockIndex.count(hdr.GetHash()), 0U);

        CDataStream directPayload(SER_NETWORK, PROTOCOL_VERSION);
        WriteCompactSize(directPayload, 1ULL);
        directPayload << hdr;
        WriteCompactSize(directPayload, 1ULL);
        BOOST_CHECK(!ProcessHeadersMessage(&dummyNode, directPayload, *connman));

        CNodeStateStats stats;
        BOOST_REQUIRE(GetNodeStateStats(dummyNode.GetId(), stats));
        BOOST_CHECK_GE(stats.nMisbehavior, 20);

        bool fUpdate = false;
        GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
    }

    // 3. Presync header with invalid compact nBits (0) must trigger Misbehaving(20) and not enter mapBlockIndex
    {
        CBlockIndex* pOrigTip = chainActive.Tip();
        BOOST_REQUIRE(pOrigTip != nullptr);

        CBlockIndex* pHighTip = new CBlockIndex();
        pHighTip->pprev = pOrigTip;
        pHighTip->nHeight = pOrigTip->nHeight + 1;
        pHighTip->nTime = static_cast<unsigned int>(GetTime());
        pHighTip->nBits = pOrigTip->nBits;
        pHighTip->nChainWork = ArithToUint256(arith_uint256(1) << 100);
        uint256 highHash = InsecureRand256();
        auto itHigh = mapBlockIndex.emplace(highHash, pHighTip).first;
        pHighTip->phashBlock = &itHigh->first;
        pHighTip->BuildSkip();

        CBlockIndex* pLowFork = new CBlockIndex();
        pLowFork->pprev = pOrigTip;
        pLowFork->nHeight = pOrigTip->nHeight + 1;
        pLowFork->nTime = pOrigTip->GetBlockTime() + 60;
        pLowFork->nBits = pOrigTip->nBits;
        pLowFork->nChainWork = ArithToUint256(arith_uint256(1));
        uint256 lowHash = InsecureRand256();
        auto itLow = mapBlockIndex.emplace(lowHash, pLowFork).first;
        pLowFork->phashBlock = &itLow->first;
        pLowFork->BuildSkip();

        chainActive.SetTip(pHighTip);

        CAddress addr(ip(0xa0b0c104), NODE_NONE);
        CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
        dummyNode.fV2AutoDetect = false;
        dummyNode.fV2Transport = false;
        dummyNode.SetSendVersion(PROTOCOL_VERSION);
        GetNodeSignals().InitializeNode(&dummyNode, *connman);
        dummyNode.nVersion = PROTOCOL_VERSION;
        dummyNode.SetRecvVersion(PROTOCOL_VERSION);
        dummyNode.fSuccessfullyConnected = true;

        CBlockHeader badHdr;
        badHdr.nVersion = 12;
        badHdr.hashPrevBlock = lowHash;
        badHdr.nTime = pLowFork->GetBlockTime() + 60;
        badHdr.nBits = 0; // Invalid zero target during presync

        CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
        WriteCompactSize(payload, 1ULL);
        payload << badHdr;
        WriteCompactSize(payload, 0ULL);
        BOOST_CHECK(!ProcessHeadersMessage(&dummyNode, payload, *connman));

        CNodeStateStats stats;
        BOOST_REQUIRE(GetNodeStateStats(dummyNode.GetId(), stats));
        BOOST_CHECK_GE(stats.nMisbehavior, 20);
        BOOST_CHECK_EQUAL(mapBlockIndex.count(badHdr.GetHash()), 0U);

        bool fUpdate = false;
        GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
        {
            LOCK(cs_main);
            chainActive.SetTip(pOrigTip);
            mapBlockIndex.erase(lowHash);
            mapBlockIndex.erase(highHash);
        }
        delete pLowFork;
        delete pHighTip;
    }
}

BOOST_AUTO_TEST_CASE(DoS_cmpctblock_empty_shorttxids)
{
    connman->ClearBanned();
    CBlockIndex* pTip = chainActive.Tip();
    BOOST_REQUIRE(pTip != nullptr);

    CAddress addr(ip(0xa0b0c105), NODE_NONE);
    CNode dummyNode(id++, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
    dummyNode.fV2AutoDetect = false;
    dummyNode.fV2Transport = false;
    dummyNode.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode, *connman);
    dummyNode.nVersion = PROTOCOL_VERSION;
    dummyNode.SetRecvVersion(PROTOCOL_VERSION);
    dummyNode.fSuccessfullyConnected = true;

    CBlock block;
    block.nVersion = 12;
    block.hashPrevBlock = pTip->GetBlockHash();
    block.nTime = pTip->GetBlockTime() + 60;
    block.nBits = GetNextWorkRequired(pTip, &block);
    block.nNonce = 1;

    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << 1 << OP_0;
    cb.vout.resize(1);
    cb.vout[0].nValue = 0;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(CTransaction(cb));
    block.hashMerkleRoot = BlockMerkleRoot(block);

    CBlockHeaderAndShortTxIDs cmpct;
    cmpct.header = block.GetBlockHeader();
    cmpct.nonce = 0;
    cmpct.shorttxids.clear();
    cmpct.prefilledtxn.push_back(PrefilledTransaction{0, block.vtx[0]});

    CDataStream payload(SER_NETWORK, PROTOCOL_VERSION);
    payload << cmpct;
    PushV1WireMessage(dummyNode, NetMsgType::CMPCTBLOCK, payload.str(), *connman);

    CNodeStateStats stats;
    BOOST_REQUIRE(GetNodeStateStats(dummyNode.GetId(), stats));
    BOOST_CHECK(!dummyNode.fDisconnect);

    bool fUpdate = false;
    GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
}

BOOST_AUTO_TEST_SUITE_END()
