// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "random.h"
#include "streams.h"
#include "test/test_kristatech.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(blockencodings_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(compact_block_serialization_test)
{
    // Create a dummy block
    CBlock block;
    block.nVersion = 12;
    block.nTime = 123456789;
    block.nBits = 0x1d00ffff;
    block.nNonce = 98765;
    
    // Create coinbase
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vin[0].scriptSig = CScript() << 100 << OP_0;
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 50 * COIN;
    coinbase.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(CTransaction(coinbase));

    // Create standard tx
    CMutableTransaction tx1;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.hash = block.vtx[0].GetHash();
    tx1.vin[0].prevout.n = 0;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = 49 * COIN;
    tx1.vout[0].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(CTransaction(tx1));
    
    // Serialize and deserialize
    CBlockHeaderAndShortTxIDs cmpct(block);
    BOOST_CHECK_EQUAL(cmpct.prefilledtxn.size(), 1);
    BOOST_CHECK_EQUAL(cmpct.shorttxids.size(), 1);
    
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << cmpct;
    
    CBlockHeaderAndShortTxIDs cmpct2;
    stream >> cmpct2;
    
    BOOST_CHECK_EQUAL(cmpct2.header.GetHash().GetHex(), cmpct.header.GetHash().GetHex());
    BOOST_CHECK_EQUAL(cmpct2.nonce, cmpct.nonce);
    BOOST_CHECK_EQUAL(cmpct2.prefilledtxn.size(), 1);
    BOOST_CHECK_EQUAL(cmpct2.shorttxids.size(), 1);
    BOOST_CHECK_EQUAL(cmpct2.shorttxids[0], cmpct.shorttxids[0]);
}

BOOST_AUTO_TEST_CASE(compact_block_pos_prefill_test)
{
    // Create a dummy PoS block
    CBlock block;
    block.nVersion = 12;
    block.nTime = 123456789;
    block.nBits = 0x1d00ffff;
    block.nNonce = 98765;
    
    // Coinbase (vtx[0])
    CMutableTransaction coinbase;
    coinbase.vin.resize(1);
    coinbase.vin[0].prevout.SetNull();
    coinbase.vout.resize(1);
    coinbase.vout[0].nValue = 0; // Empty coinbase in PoS
    block.vtx.push_back(CTransaction(coinbase));
    
    // Coinstake (vtx[1])
    CMutableTransaction coinstake;
    coinstake.vin.resize(1);
    coinstake.vin[0].prevout.hash = uint256S("0xabc");
    coinstake.vin[0].prevout.n = 0;
    coinstake.vout.resize(2);
    coinstake.vout[0].SetEmpty(); // First output of coinstake is empty
    coinstake.vout[1].nValue = 10 * COIN;
    coinstake.vout[1].scriptPubKey = CScript() << OP_TRUE;
    block.vtx.push_back(CTransaction(coinstake));
    BOOST_CHECK(block.vtx[1].IsCoinStake()); // Ensure it returns true for IsCoinStake()

    // Add block signature
    block.vchBlockSig = std::vector<unsigned char>({1, 2, 3, 4, 5});

    // Another normal tx
    CMutableTransaction tx1;
    tx1.vin.resize(1);
    tx1.vin[0].prevout.hash = uint256S("0xdef");
    tx1.vin[0].prevout.n = 1;
    tx1.vout.resize(1);
    tx1.vout[0].nValue = 5 * COIN;
    block.vtx.push_back(CTransaction(tx1));
    
    // Construct compact block
    CBlockHeaderAndShortTxIDs cmpct(block);
    // Coinbase and Coinstake should be prefilled (total 2)
    BOOST_CHECK_EQUAL(cmpct.prefilledtxn.size(), 2);
    BOOST_CHECK_EQUAL(cmpct.shorttxids.size(), 1);
    BOOST_CHECK(cmpct.vchBlockSig == block.vchBlockSig);
    
    BOOST_CHECK_EQUAL(cmpct.prefilledtxn[0].index, 0); // Coinbase index = 0
    BOOST_CHECK_EQUAL(cmpct.prefilledtxn[1].index, 0); // Coinstake index = 1 - 0 - 1 = 0 (differential)

    // Verify serialization preserves signature
    CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
    stream << cmpct;
    
    CBlockHeaderAndShortTxIDs cmpct2;
    stream >> cmpct2;
    BOOST_CHECK(cmpct2.vchBlockSig == block.vchBlockSig);
}

BOOST_AUTO_TEST_SUITE_END()
