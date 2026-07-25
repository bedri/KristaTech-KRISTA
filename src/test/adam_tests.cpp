// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "chainparams.h"
#include "consensus/tx_verify.h"
#include "script/script.h"
#include "test/test_kristatech.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(adam_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(adam_pow_limits_test)
{
    // Test GetZeroCoinPoWLimit for Mainnet
    uint256 mainZeroLimit = GetZeroCoinPoWLimit("main");
    BOOST_CHECK_EQUAL(mainZeroLimit, uint256S("000007ffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    // Test GetZeroCoinPoWLimit for Testnet
    uint256 testZeroLimit = GetZeroCoinPoWLimit("test");
    BOOST_CHECK_EQUAL(testZeroLimit, uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    // Test GetZeroCoinPoWLimit for Regtest
    uint256 regtestZeroLimit = GetZeroCoinPoWLimit("regtest");
    BOOST_CHECK_EQUAL(regtestZeroLimit, uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));

    // Verify GetZeroCoinPoWLimit is stricter than GetMinerPoWLimit for funded PoW
    uint256 mainMinerLimit = GetMinerPoWLimit("main");
    BOOST_CHECK(mainZeroLimit < mainMinerLimit);
}

BOOST_AUTO_TEST_CASE(adam_pow_registration_script_matching_test)
{
    // Construct a valid PoW Registration script
    uint32_t nonce = 12345;
    uint256 challengeHash = uint256S("11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff");
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    int64_t locktime = 5000;

    CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
    ssNonce << nonce;

    CScript scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                     << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                     << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                     << OP_DROP << OP_DROP << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> nonceOut;
    uint256 challengeOut;
    CPubKey pubkeyOut;
    int64_t lockTimeOut = 0;
    CKeyID pubkeyHashOut;

    bool matchResult = MatchPoWLockRegistration(scriptPubKey, nonceOut, challengeOut, pubkeyOut, lockTimeOut, pubkeyHashOut);
    BOOST_CHECK(matchResult);
    BOOST_CHECK(pubkeyOut == pubkey);
    BOOST_CHECK_EQUAL(lockTimeOut, locktime);
    BOOST_CHECK(challengeOut == challengeHash);
    BOOST_CHECK(pubkeyHashOut == pubkey.GetID());
}

BOOST_AUTO_TEST_CASE(adam_zero_coin_pow_tx_validation_test)
{
    // Construct a Zero-Coin PoW Registration Transaction with Null Prevout
    uint32_t nonce = 67890;
    uint256 challengeHash = uint256S("aabbccddeeff11223344556677889900aabbccddeeff11223344556677889900");
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    int64_t locktime = 6000;

    CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
    ssNonce << nonce;

    CScript scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                     << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                     << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                     << OP_DROP << OP_DROP << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.nLockTime = 0;

    // Null input (zero-coin registration input)
    CTxIn txin;
    txin.prevout.SetNull();
    mtx.vin.push_back(txin);

    // Output with 0 value
    CTxOut txout(0, scriptPubKey);
    mtx.vout.push_back(txout);

    CTransaction tx(mtx);
    CValidationState state;

    // CheckTransaction must accept null prevouts for Zero-Coin PoW Registration tx
    bool checkRes = CheckTransaction(tx, state);
    BOOST_CHECK_MESSAGE(checkRes, "CheckTransaction should accept Zero-Coin PoW Registration transaction");
}

BOOST_AUTO_TEST_SUITE_END()
