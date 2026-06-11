// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "test_pivx.h"
#include <boost/test/unit_test.hpp>
#include "masternode.h"

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

enum BlockSignatureType{
    P2PK,
    P2PKH
};

CScript GetScriptForType(CPubKey pubKey, BlockSignatureType type)
{
    switch(type){
        case P2PK:
            return CScript() << pubKey << OP_CHECKSIG;
        default:
            return GetScriptForDestination(pubKey.GetID());
    }
}

std::vector<unsigned char> CreateDummyScriptSigWithKey(CPubKey pubKey)
{
    std::vector<unsigned char> vchSig;
    const CScript scriptCode;
    DummySignatureCreator(nullptr).CreateSig(vchSig, pubKey.GetID(), scriptCode, SIGVERSION_BASE);
    return vchSig;
}

CScript GetDummyScriptSigByType(CPubKey pubKey, bool isP2PK)
{
    CScript script = CScript() << CreateDummyScriptSigWithKey(pubKey);
    if (!isP2PK)
        script << ToByteVector(pubKey);
    return script;
}

CBlock CreateDummyBlockWithSignature(CKey stakingKey, BlockSignatureType type, bool useInputP2PK)
{
    CMutableTransaction txCoinStake;
    // Dummy input
    CTxIn input(uint256(), 0);
    // P2PKH input
    input.scriptSig = GetDummyScriptSigByType(stakingKey.GetPubKey(), useInputP2PK);
    // Add dummy input
    txCoinStake.vin.emplace_back(input);
    // Empty first output
    txCoinStake.vout.emplace_back(CTxOut(0, CScript()));
    // P2PK staking output
    CScript scriptPubKey = GetScriptForType(stakingKey.GetPubKey(), type);
    txCoinStake.vout.emplace_back(CTxOut(0, scriptPubKey));

    // Now the block.
    CBlock block;
    block.nTime = Params().Checkpoints().nTimeLastCheckpoint + 1;
    block.vtx.emplace_back(CTransaction()); // dummy first tx
    block.vtx.emplace_back(txCoinStake);
    SignBlockWithKey(block, stakingKey);

    return block;
}

bool TestBlockSignaturePreEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, false);
}

bool TestBlockSignaturePostEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, true);
}

BOOST_AUTO_TEST_CASE(block_signature_test)
{
    for (int i = 0; i < 20; ++i) {
        CKey stakingKey;
        stakingKey.MakeNewKey(true);
        bool useInputP2PK = i % 2 == 0;

        // Test P2PK block signature pre enforcement.
        CBlock block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePreEnforcementV5(block));

        // Test P2PK block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));

        // Test P2PKH block signature pre enforcement ---> must fail.
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        BOOST_CHECK(!TestBlockSignaturePreEnforcementV5(block));

        // Test P2PKH block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        if (useInputP2PK) {
            // If it's using a P2PK scriptsig as input and a P2PKH output
            // The block doesn't contain the public key to verify the sig anywhere.
            // Must fail.
            BOOST_CHECK(!TestBlockSignaturePostEnforcementV5(block));
        } else {
            BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));
        }
    }
}

CAmount nMoneySupplyPoWEnd = 43199500 * COIN;

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    // Check height 0 (returns starting reward: 19 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(0) == 1900000000LL);

    // Check height 1 (Premine)
    BOOST_CHECK(CMasternode::GetBlockValue(1) == 0LL);

    // Check height in bootstrap period (should be 50 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(5000) == 5000000000LL);

    // Check various heights in Period 0 (should all be 19 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(50000) == 1900000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(150000) == 1900000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(250000) == 1900000000LL);
    
    // Check height in Period 1 (should be 18.05 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(350000) == 1805000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(450000) == 1805000000LL);

    // Check height at Period 4 (should be 15.47561875 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(1061202) == 1547561875LL);

    // Check height at Period 8 (should be 12.60498819 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(2112402) == 1260498819LL);

    // Check height at Period 12 (should be 10.26684167 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(3163602) == 1026684167LL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool(), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}

BOOST_AUTO_TEST_CASE(debug_block_hashing)
{
    CBlock block;
    block.nVersion = 11;
    block.hashPrevBlock = uint256S("0768e3a54adc76099a5c0c9f78eb72999f11120e98178493ef577045cbad3240");
    block.hashMerkleRoot = uint256S("cda6ecfc5d14c88f86e29fce1cf1a300c259d7b2d11e7a1d29b7857b9ac5aae9");
    block.nTime = 1780825609;
    block.nBits = 0x1e0ffff0;
    block.nNonce = 0;

    std::cout << "\n=== DEBUG BLOCK HASHING WITH EXACT FIELDS ===" << std::endl;
    
    // Case 1: All ADAM fields empty
    block.vAdamMiners.clear();
    block.vAdamSolutions.clear();
    block.vAdamVRFProof.clear();
    block.vAdamCoordinatorSig.clear();
    std::cout << "1. Empty ADAM fields:" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    std::cout << "   SerializeHash(CBlockHeader) = " << SerializeHash(block.GetBlockHeader()).ToString() << std::endl;

    // Case 2: Populating vAdamMiners with 14 dummy public keys (but empty solutions/vrf)
    for (int i = 0; i < 14; ++i) {
        CKey key;
        key.MakeNewKey(true);
        block.vAdamMiners.push_back(key.GetPubKey());
    }
    std::cout << "2. Only vAdamMiners populated (14 miners):" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    
    CDataStream ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << block.GetBlockHeader();
    std::cout << "   Serialized Hex (SER_GETHASH) = " << HexStr(ss.begin(), ss.end()) << std::endl;
    
    const unsigned char* pbegin = (const unsigned char*)&ss[0];
    const unsigned char* pend = pbegin + ss.size();
    std::cout << "   DoubleSHA256 of Serialized = " << Hash(pbegin, pend).ToString() << std::endl;
    std::cout << "   X11KVS of Serialized = " << HashX11KVS(pbegin, pend).ToString() << std::endl;

    // Case 3: Populating vAdamCoordinatorSig
    block.vAdamCoordinatorSig = std::vector<unsigned char>(71, 1);
    std::cout << "3. Populated vAdamCoordinatorSig:" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    
    CDataStream ss3(SER_GETHASH, PROTOCOL_VERSION);
    ss3 << block.GetBlockHeader();
    std::cout << "   Serialized Hex 3 (SER_GETHASH) = " << HexStr(ss3.begin(), ss3.end()) << std::endl;
    
    std::cout << "=== END DEBUG BLOCK HASHING ===\n" << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
