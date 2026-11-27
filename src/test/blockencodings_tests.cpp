// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "random.h"
#include "streams.h"
#include "test/test_kristatech.h"

#include <limits>
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

BOOST_AUTO_TEST_CASE(compact_block_bounds_and_overflow_test)
{
    CBlockHeader hdr;
    uint64_t nonce = 42;

    // 1. shorttxids_size > 65535 must throw std::ios_base::failure
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << hdr << nonce;
        WriteCompactSize(ss, static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1ULL);
        CBlockHeaderAndShortTxIDs cmpct;
        BOOST_CHECK_THROW(ss >> cmpct, std::ios_base::failure);
    }

    // 2. PrefilledTransaction index > 65535 must throw std::ios_base::failure
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << hdr << nonce;
        WriteCompactSize(ss, 0ULL); // shorttxids_size
        WriteCompactSize(ss, 1ULL); // prefilledtxn count
        WriteCompactSize(ss, static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1ULL);
        CBlockHeaderAndShortTxIDs cmpct;
        BOOST_CHECK_THROW(ss >> cmpct, std::ios_base::failure);
    }

    // 3. BlockTransactionsRequest indexes_size > 65535 must throw std::ios_base::failure
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << uint256();
        WriteCompactSize(ss, static_cast<uint64_t>(std::numeric_limits<uint16_t>::max()) + 1ULL);
        BlockTransactionsRequest req;
        BOOST_CHECK_THROW(ss >> req, std::ios_base::failure);
    }

    // 4. BlockTransactionsRequest uint16_t differential wraparound [10, 0xFFFF, 0xFFFF] -> throws
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << uint256();
        WriteCompactSize(ss, 3ULL);
        WriteCompactSize(ss, 10ULL);
        WriteCompactSize(ss, 0xFFFFULL);
        WriteCompactSize(ss, 0xFFFFULL);
        BlockTransactionsRequest req;
        BOOST_CHECK_THROW(ss >> req, std::ios_base::failure);
    }

    // 5. Boundary overflow [10, 65525] => 10 + 65525 + 1 = 65536 > 65535 -> throws
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << uint256();
        WriteCompactSize(ss, 2ULL);
        WriteCompactSize(ss, 10ULL);
        WriteCompactSize(ss, 65525ULL);
        BlockTransactionsRequest req;
        BOOST_CHECK_THROW(ss >> req, std::ios_base::failure);
    }

    // 6. Valid roundtrip up to uint16_t max [0, 10, 65535]
    {
        BlockTransactionsRequest reqIn;
        reqIn.blockhash = uint256S("0x1234");
        reqIn.indexes = {0, 10, 65535};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << reqIn;
        BlockTransactionsRequest reqOut;
        ss >> reqOut;
        BOOST_CHECK(reqOut.blockhash == reqIn.blockhash);
        BOOST_CHECK(reqOut.indexes == reqIn.indexes);
    }
}

BOOST_AUTO_TEST_CASE(blockheader_adam_deserialization_bounds_test)
{
    // C-4: CBlockHeader::ForRead() must enforce hard bounds matching CheckBlockHeader()
    auto makeStreamToSolutions = [](const std::vector<CPubKey>& miners, uint64_t nSolCount, const std::vector<unsigned char>* firstSol = nullptr) {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader base;
        base.nVersion = 12;
        ss << base.nVersion << base.hashPrevBlock << base.hashMerkleRoot << base.nTime << base.nBits << base.nNonce;
        WriteCompactSize(ss, miners.size());
        for (const auto& pk : miners) {
            ss << pk;
        }
        WriteCompactSize(ss, nSolCount);
        if (firstSol) {
            ss << *firstSol;
        }
        return ss;
    };

    CKey k;
    k.MakeNewKey(true);
    CPubKey pk = k.GetPubKey();

    // 1. vAdamMiners > 32 throws
    {
        std::vector<CPubKey> miners(33, pk);
        CDataStream ss = makeStreamToSolutions(miners, 0);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 2. Oversized CPubKey (> 65 bytes) inside vAdamMiners throws
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        CBlockHeader base;
        base.nVersion = 12;
        ss << base.nVersion << base.hashPrevBlock << base.hashMerkleRoot << base.nTime << base.nBits << base.nNonce;
        WriteCompactSize(ss, 1ULL);
        WriteCompactSize(ss, static_cast<uint64_t>(CPubKey::PUBLIC_KEY_SIZE) + 1ULL);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 3. vAdamSolutions > 32 throws
    {
        CDataStream ss = makeStreamToSolutions({pk}, 33ULL);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 4. Individual solution > 512 bytes throws
    {
        std::vector<unsigned char> bigSol(513, 0x11);
        CDataStream ss = makeStreamToSolutions({pk}, 1ULL, &bigSol);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 5. vAdamVRFProof > 512 bytes throws
    {
        std::vector<unsigned char> smallSol(10, 0x22);
        CDataStream ss = makeStreamToSolutions({pk}, 1ULL, &smallSol);
        ss << std::vector<unsigned char>(513, 0x33);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 6. vAdamCoordinatorSig > 512 bytes throws
    {
        std::vector<unsigned char> smallSol(10, 0x22);
        CDataStream ss = makeStreamToSolutions({pk}, 1ULL, &smallSol);
        ss << std::vector<unsigned char>(10, 0x44);
        ss << std::vector<unsigned char>(513, 0x55);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 7. vQuorumSig > 4096 bytes throws
    {
        std::vector<unsigned char> smallSol(10, 0x22);
        CDataStream ss = makeStreamToSolutions({pk}, 1ULL, &smallSol);
        ss << std::vector<unsigned char>(10, 0x44);
        ss << std::vector<unsigned char>(10, 0x55);
        ss << std::vector<unsigned char>(4097, 0x66);
        CBlockHeader out;
        BOOST_CHECK_THROW(ss >> out, std::ios_base::failure);
    }

    // 8. In-memory serialize / GetSerializeSize still works on valid oversized check inputs without throwing
    {
        CBlockHeader hdr;
        hdr.nVersion = 12;
        hdr.vQuorumSig.assign(4097, 0x66);
        BOOST_CHECK_GT(::GetSerializeSize(hdr, SER_NETWORK, PROTOCOL_VERSION), 4096U);
    }
}

BOOST_AUTO_TEST_SUITE_END()
