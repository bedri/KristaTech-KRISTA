// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "arith_uint256.h"
#include "chainparams.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "primitives/block.h"
#include "script/script.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_kristatech.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>
#include <set>

namespace {
struct ScopedChainParams {
    CBaseChainParams::Network prevNetwork;
    explicit ScopedChainParams(CBaseChainParams::Network network) : prevNetwork(Params().NetworkID()) {
        SelectParams(network);
    }
    ~ScopedChainParams() {
        SelectParams(prevNetwork);
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(adam_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(adam_pow_limits_test)
{
    // Test GetZeroCoinPoWLimit for Mainnet (64 hex chars = 256 bits)
    uint256 mainZeroLimit = GetZeroCoinPoWLimit("main");
    BOOST_CHECK_EQUAL(mainZeroLimit.GetHex(), uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetZeroCoinPoWLimit for Testnet
    uint256 testZeroLimit = GetZeroCoinPoWLimit("test");
    BOOST_CHECK_EQUAL(testZeroLimit.GetHex(), uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetZeroCoinPoWLimit for Regtest
    uint256 regtestZeroLimit = GetZeroCoinPoWLimit("regtest");
    BOOST_CHECK_EQUAL(regtestZeroLimit.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetMinerPoWLimit for Mainnet, Testnet, Regtest
    uint256 mainMinerLimit = GetMinerPoWLimit("main");
    uint256 testMinerLimit = GetMinerPoWLimit("test");
    uint256 regtestMinerLimit = GetMinerPoWLimit("regtest");
    BOOST_CHECK_EQUAL(mainMinerLimit.GetHex(), uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(testMinerLimit.GetHex(), uint256S("00000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(regtestMinerLimit.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Verify GetZeroCoinPoWLimit is a larger target (fewer leading zero bits) than GetMinerPoWLimit on Mainnet
    BOOST_CHECK(mainZeroLimit > mainMinerLimit);
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

    // Must not match Coin-Lock registration
    BOOST_CHECK(!MatchCoinLockRegistration(scriptPubKey, pubkeyOut, lockTimeOut, pubkeyHashOut));

    // Verify Solver recognizes PoW-Lock registration script as a valid MESCAL contract
    txnouttype whichType;
    std::vector< std::vector<unsigned char> > vSolutions;
    BOOST_CHECK(Solver(scriptPubKey, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_MESCAL_CONTRACT);
}

BOOST_AUTO_TEST_CASE(adam_coin_lock_registration_script_matching_test)
{
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    int64_t locktime = 12000;

    CScript coinLockScript = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                       << OP_DROP
                                       << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                       << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CPubKey pubkeyOut;
    int64_t lockTimeOut = 0;
    CKeyID pubkeyHashOut;

    BOOST_CHECK(MatchCoinLockRegistration(coinLockScript, pubkeyOut, lockTimeOut, pubkeyHashOut));
    BOOST_CHECK(pubkeyOut == pubkey);
    BOOST_CHECK_EQUAL(lockTimeOut, locktime);
    BOOST_CHECK(pubkeyHashOut == pubkey.GetID());

    // Must not match PoW-Lock registration
    std::vector<unsigned char> nonceOut;
    uint256 challengeOut;
    BOOST_CHECK(!MatchPoWLockRegistration(coinLockScript, nonceOut, challengeOut, pubkeyOut, lockTimeOut, pubkeyHashOut));

    // Verify Solver recognizes Coin-Lock registration script as a valid MESCAL contract
    txnouttype whichType;
    std::vector< std::vector<unsigned char> > vSolutions;
    BOOST_CHECK(Solver(coinLockScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_MESCAL_CONTRACT);

    // Negative tests: standard P2PKH and malformed scripts must not match
    CScript p2pkhScript = GetScriptForDestination(pubkey.GetID());
    BOOST_CHECK(!MatchCoinLockRegistration(p2pkhScript, pubkeyOut, lockTimeOut, pubkeyHashOut));
    BOOST_CHECK(!MatchPoWLockRegistration(p2pkhScript, nonceOut, challengeOut, pubkeyOut, lockTimeOut, pubkeyHashOut));

    // Malformed Coin-Lock script with invalid 20-byte push instead of 33/65-byte pubkey
    CScript invalidPubKeyScript = CScript() << ToByteVector(pubkey.GetID())
                                            << OP_DROP
                                            << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                            << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    BOOST_CHECK(!MatchCoinLockRegistration(invalidPubKeyScript, pubkeyOut, lockTimeOut, pubkeyHashOut));
}

BOOST_AUTO_TEST_CASE(adam_zero_coin_pow_tx_validation_test)
{
    // Construct a Zero-Coin PoW Registration Transaction with Sentinel Prevout
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

    // Sentinel input (zero-coin registration input with non-null dummy outpoint 0000...0001:0)
    CTxIn txin;
    txin.prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    mtx.vin.push_back(txin);

    // Output with 0 value
    CTxOut txout(0, scriptPubKey);
    mtx.vout.push_back(txout);

    CTransaction tx(mtx);
    CValidationState state;

    // CheckTransaction and IsZeroCoinPoWTransaction must accept Zero-Coin PoW Registration transaction
    bool checkRes = CheckTransaction(tx, state);
    BOOST_CHECK_MESSAGE(checkRes, "CheckTransaction should accept Zero-Coin PoW Registration transaction");
    BOOST_CHECK_MESSAGE(IsZeroCoinPoWTransaction(tx), "IsZeroCoinPoWTransaction must return true for 0x01:0 sentinel outpoint");

    // Multi-output Zero-Coin PoW transaction must be rejected
    CMutableTransaction mtxMulti(mtx);
    mtxMulti.vout.push_back(txout);
    CTransaction txMulti(mtxMulti);
    CValidationState stateMulti;
    BOOST_CHECK(!IsZeroCoinPoWTransaction(txMulti));
    BOOST_CHECK(!CheckTransaction(txMulti, stateMulti));
}

BOOST_AUTO_TEST_CASE(adam_deterministic_keys_test)
{
    std::set<CPubKey> pubkeys;
    for (int i = 0; i < 15; ++i) {
        CKey key = GetAdamDeterministicKey(i);
        BOOST_CHECK(key.IsValid());
        BOOST_CHECK(key.IsCompressed());

        CPubKey pubkey = GetAdamDeterministicPubKey(i);
        BOOST_CHECK(pubkey.IsValid());
        BOOST_CHECK(pubkey.IsCompressed());
        BOOST_CHECK(key.GetPubKey() == pubkey);

        pubkeys.insert(pubkey);
    }
    // All 15 deterministic bootstrap keys must be distinct
    BOOST_CHECK_EQUAL(pubkeys.size(), 15U);
}

BOOST_AUTO_TEST_CASE(adam_node_selection_test)
{
    ScopedChainParams regtestParams(CBaseChainParams::REGTEST);

    uint256 seed1 = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
    std::vector<CPubKey> miners1;
    CPubKey coordinator1;

    BOOST_CHECK(SelectAdamNodes(seed1, Params().GetConsensus(), miners1, coordinator1));
    BOOST_CHECK_EQUAL(miners1.size(), 11U);
    BOOST_CHECK(coordinator1.IsValid());

    std::set<CPubKey> uniqueMiners(miners1.begin(), miners1.end());
    BOOST_CHECK_EQUAL(uniqueMiners.size(), 11U);
    // Coordinator must be distinct from the 11 elected miners
    BOOST_CHECK_EQUAL(uniqueMiners.count(coordinator1), 0U);

    // Determinism check: identical seed must elect identical miners in identical order and identical coordinator
    std::vector<CPubKey> miners1Repeat;
    CPubKey coordinator1Repeat;
    BOOST_CHECK(SelectAdamNodes(seed1, Params().GetConsensus(), miners1Repeat, coordinator1Repeat));
    BOOST_CHECK(miners1 == miners1Repeat);
    BOOST_CHECK(coordinator1 == coordinator1Repeat);

    // Different seed must produce a different election ordering
    uint256 seed2 = uint256S("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210");
    std::vector<CPubKey> miners2;
    CPubKey coordinator2;
    BOOST_CHECK(SelectAdamNodes(seed2, Params().GetConsensus(), miners2, coordinator2));
    BOOST_CHECK_EQUAL(miners2.size(), 11U);
    BOOST_CHECK(miners1 != miners2 || coordinator1 != coordinator2);
}

BOOST_AUTO_TEST_CASE(adam_puzzle_algorithms_and_3permutation_test)
{
    const std::vector<std::string> expectedNames = {
        "blake", "bmw", "groestl", "jh", "keccak", "skein",
        "luffa", "cubehash", "shavite", "simd", "echo", "X11KVS",
        "DoubleSHA256", "hamsi", "fugue", "shabal", "whirlpool", "haval"
    };

    std::string sampleInput = "KRISTA_ADAM_PUZZLE_TEST_VECTOR_2026";
    std::set<uint256> distinctHashes;

    for (int algo = 0; algo < 18; ++algo) {
        BOOST_CHECK_EQUAL(GetAdamPuzzleAlgoName(algo), expectedNames[algo]);
        uint256 h1 = CalculateAdamPuzzleHash(algo,
            reinterpret_cast<const unsigned char*>(sampleInput.data()),
            reinterpret_cast<const unsigned char*>(sampleInput.data() + sampleInput.size()));
        uint256 h2 = CalculateAdamPuzzleHash(algo,
            reinterpret_cast<const unsigned char*>(sampleInput.data()),
            reinterpret_cast<const unsigned char*>(sampleInput.data() + sampleInput.size()));
        BOOST_CHECK(!h1.IsNull());
        BOOST_CHECK(h1 == h2);
        distinctHashes.insert(h1);
    }
    // All 18 algorithms must produce distinct hashes for the same input
    BOOST_CHECK_EQUAL(distinctHashes.size(), 18U);
    BOOST_CHECK_EQUAL(GetAdamPuzzleAlgoName(99), "DoubleSHA256");

    // Verify 3-Permutation algorithm selection (algo1 != algo2 != algo3 in 0..17)
    uint256 hashPrevBlock = uint256S("000000112233445566778899aabbccddeeff00112233445566778899aabbccdd");
    for (int i = 0; i < 15; ++i) {
        CPubKey minerKey = GetAdamDeterministicPubKey(i);
        int algo1 = -1, algo2 = -1, algo3 = -1;
        GetAdam3PermutationAlgos(hashPrevBlock, minerKey, algo1, algo2, algo3);
        BOOST_CHECK(algo1 >= 0 && algo1 < 18);
        BOOST_CHECK(algo2 >= 0 && algo2 < 18);
        BOOST_CHECK(algo3 >= 0 && algo3 < 18);
        BOOST_CHECK_NE(algo1, algo2);
        BOOST_CHECK_NE(algo1, algo3);
        BOOST_CHECK_NE(algo2, algo3);

        // Determinism check
        int r1 = -1, r2 = -1, r3 = -1;
        GetAdam3PermutationAlgos(hashPrevBlock, minerKey, r1, r2, r3);
        BOOST_CHECK_EQUAL(algo1, r1);
        BOOST_CHECK_EQUAL(algo2, r2);
        BOOST_CHECK_EQUAL(algo3, r3);
    }
}

BOOST_AUTO_TEST_CASE(adam_vrf_and_coordinator_sig_test)
{
    CKey coordKey = GetAdamDeterministicKey(0);
    CPubKey coordPubKey = coordKey.GetPubKey();
    CBLSSecretKey blsKey = DeriveBLSFromCKey(coordKey);

    CKey otherKey = GetAdamDeterministicKey(1);
    CPubKey otherPubKey = otherKey.GetPubKey();

    uint256 prevSeed = uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    uint256 wrongSeed = uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    std::vector<unsigned char> vchProof;
    BOOST_CHECK(SignBLSWithECDSAFallback(prevSeed, coordKey, blsKey, vchProof));
    BOOST_CHECK(!vchProof.empty());

    // Valid VRF proof
    BOOST_CHECK(VerifyAdamVRFProof(prevSeed, vchProof, coordPubKey));
    // Wrong seed or wrong coordinator key or empty proof must fail
    BOOST_CHECK(!VerifyAdamVRFProof(wrongSeed, vchProof, coordPubKey));
    BOOST_CHECK(!VerifyAdamVRFProof(prevSeed, vchProof, otherPubKey));
    BOOST_CHECK(!VerifyAdamVRFProof(prevSeed, std::vector<unsigned char>(), coordPubKey));

    // Coordinator block header signature test
    CBlockHeader header;
    header.nVersion = 12;
    header.hashPrevBlock = prevSeed;
    header.hashMerkleRoot = uint256S("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc");
    header.nTime = 1780000000;
    header.nBits = 0x207fffff;
    header.nNonce = 42;
    header.vAdamVRFProof = vchProof;

    BOOST_CHECK(SignBLSWithECDSAFallback(header.GetHash(), coordKey, blsKey, header.vAdamCoordinatorSig));
    BOOST_CHECK(VerifyAdamCoordinatorSig(header, coordPubKey));
    BOOST_CHECK(!VerifyAdamCoordinatorSig(header, otherPubKey));

    // Tampering with header invalidates coordinator signature
    header.nNonce = 43;
    BOOST_CHECK(!VerifyAdamCoordinatorSig(header, coordPubKey));
}

BOOST_AUTO_TEST_CASE(adam_verify_solution_v11_and_v12_test)
{
    ScopedChainParams regtestParams(CBaseChainParams::REGTEST);

    uint256 hashPrevBlock = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
    uint256 hashAdamSeed = uint256S("2222222222222222222222222222222222222222222222222222222222222222");

    std::vector<CPubKey> vMiners;
    CPubKey coordinator;
    BOOST_REQUIRE(SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator));
    BOOST_REQUIRE_GE(vMiners.size(), 1U);

    CPubKey electedMinerPubKey = vMiners[0];
    CKey electedMinerKey;
    for (int i = 0; i < 15; ++i) {
        CKey k = GetAdamDeterministicKey(i);
        if (k.GetPubKey() == electedMinerPubKey) {
            electedMinerKey = k;
            break;
        }
    }
    BOOST_REQUIRE(electedMinerKey.IsValid());
    CBLSSecretKey blsMinerKey = DeriveBLSFromCKey(electedMinerKey);

    uint32_t nNonce = 777;
    CDataStream ssInput(SER_GETHASH, 0);
    ssInput << hashAdamSeed;
    ssInput << electedMinerPubKey;
    ssInput << nNonce;

    // 1. Version 11 (Fallback single-algorithm mode)
    int algoV11 = GetAdamPuzzleAlgo(hashAdamSeed, electedMinerPubKey, true);
    uint256 puzzleHashV11 = CalculateAdamPuzzleHash(algoV11,
        reinterpret_cast<const unsigned char*>(&ssInput[0]),
        reinterpret_cast<const unsigned char*>(&ssInput[0]) + ssInput.size());

    std::vector<unsigned char> vchSigV11;
    BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHashV11, electedMinerKey, blsMinerKey, vchSigV11));

    CDataStream ssSolV11(SER_NETWORK, PROTOCOL_VERSION);
    ssSolV11 << nNonce << vchSigV11;
    std::vector<unsigned char> vchSolutionV11(ssSolV11.begin(), ssSolV11.end());

    BOOST_CHECK(VerifyAdamSolution(hashPrevBlock, hashAdamSeed, electedMinerPubKey, vchSolutionV11, 0x207fffff, 11, 500));

    // 2. Version 12 (Standard 3-Permutation mode)
    int algo1 = -1, algo2 = -1, algo3 = -1;
    GetAdam3PermutationAlgos(hashPrevBlock, electedMinerPubKey, algo1, algo2, algo3);
    int i_factor = 1; // minerIdx == 0 -> i_factor = 1
    uint256 hash3 = CalculateAdamPuzzleHash(algo3,
        reinterpret_cast<const unsigned char*>(&ssInput[0]),
        reinterpret_cast<const unsigned char*>(&ssInput[0]) + ssInput.size());
    uint256 multiplied1 = ArithToUint256(UintToArith256(hash3) * i_factor);
    uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
    uint256 multiplied2 = ArithToUint256(UintToArith256(hash2) * i_factor);
    uint256 puzzleHashV12 = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);

    std::vector<unsigned char> vchSigV12;
    BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHashV12, electedMinerKey, blsMinerKey, vchSigV12));

    CDataStream ssSolV12(SER_NETWORK, PROTOCOL_VERSION);
    ssSolV12 << nNonce << vchSigV12;
    std::vector<unsigned char> vchSolutionV12(ssSolV12.begin(), ssSolV12.end());

    BOOST_CHECK(VerifyAdamSolution(hashPrevBlock, hashAdamSeed, electedMinerPubKey, vchSolutionV12, 0x207fffff, 12, 500));

    // Negative checks: V11 solution must not pass V12 verification, and non-elected miner key must fail V12
    BOOST_CHECK(!VerifyAdamSolution(hashPrevBlock, hashAdamSeed, electedMinerPubKey, vchSolutionV11, 0x207fffff, 12, 500));
    BOOST_CHECK(!VerifyAdamSolution(hashPrevBlock, hashAdamSeed, coordinator, vchSolutionV12, 0x207fffff, 12, 500));
    BOOST_CHECK(!VerifyAdamSolution(hashPrevBlock, hashAdamSeed, electedMinerPubKey, std::vector<unsigned char>(), 0x207fffff, 12, 500));
}

BOOST_AUTO_TEST_SUITE_END()
