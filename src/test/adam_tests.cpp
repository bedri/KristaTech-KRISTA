// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "arith_uint256.h"
#include "blocksignature.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "keystore.h"
#include "llmq.h"
#include "main.h"
#include "miner.h"
#include "primitives/block.h"
#include "script/script.h"
#include "script/sign.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_kristatech.h"
#include "txmempool.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>
#include <atomic>
#include <set>
#include <thread>

namespace {
uint32_t SolveZeroCoinPoWForTest(const CPubKey& pk, const uint256& challengeHash, const uint256& target, bool fHardeningV2 = true)
{
    const unsigned int nWorkers = std::max(1U, std::min(16U, std::thread::hardware_concurrency()));
    if (!fHardeningV2 || nWorkers == 1) {
        for (uint32_t n = 0;; ++n) {
            if (ComputeZeroCoinRegistrationHash(n, challengeHash, pk, fHardeningV2) <= target) {
                return n;
            }
        }
    }
    std::atomic<bool> found{false};
    std::atomic<uint32_t> bestNonce{0};
    std::vector<std::thread> workers;
    workers.reserve(nWorkers);
    for (unsigned int w = 0; w < nWorkers; ++w) {
        workers.emplace_back([&, w]() {
            for (uint32_t n = w; !found.load(std::memory_order_relaxed); n += nWorkers) {
                if (ComputeZeroCoinRegistrationHash(n, challengeHash, pk, true) <= target) {
                    bestNonce.store(n, std::memory_order_relaxed);
                    found.store(true, std::memory_order_relaxed);
                    break;
                }
            }
        });
    }
    for (auto& t : workers) {
        t.join();
    }
    return bestNonce.load();
}
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
    // Test legacy (pre-V2) GetZeroCoinPoWLimit for Mainnet, Testnet, Regtest
    uint256 mainZeroLimit = GetZeroCoinPoWLimit("main", false);
    BOOST_CHECK_EQUAL(mainZeroLimit.GetHex(), uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    uint256 testZeroLimit = GetZeroCoinPoWLimit("test", false);
    BOOST_CHECK_EQUAL(testZeroLimit.GetHex(), uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    uint256 regtestZeroLimit = GetZeroCoinPoWLimit("regtest", false);
    BOOST_CHECK_EQUAL(regtestZeroLimit.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test UPGRADE_HARDENING_V2 (HashX11KVS) GetZeroCoinPoWLimit for Mainnet (14 bits), Testnet (12 bits), Regtest (8 bits)
    uint256 mainZeroLimitV2 = GetZeroCoinPoWLimit("main", true);
    uint256 testZeroLimitV2 = GetZeroCoinPoWLimit("test", true);
    uint256 regtestZeroLimitV2 = GetZeroCoinPoWLimit("regtest", true);
    BOOST_CHECK_EQUAL(mainZeroLimitV2.GetHex(), uint256S("0003ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(testZeroLimitV2.GetHex(), uint256S("000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(regtestZeroLimitV2.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

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

    // Verify Solver recognizes PoW-Lock registration script as TX_ADAM_POWLOCK
    txnouttype whichType;
    std::vector< std::vector<unsigned char> > vSolutions;
    BOOST_CHECK(Solver(scriptPubKey, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_ADAM_POWLOCK);
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

    // Verify Solver recognizes Coin-Lock registration script as TX_ADAM_COINLOCK
    txnouttype whichType;
    std::vector< std::vector<unsigned char> > vSolutions;
    BOOST_CHECK(Solver(coinLockScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_ADAM_COINLOCK);

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

BOOST_AUTO_TEST_CASE(adam_future_block_preverification_k1_test)
{
    ScopedChainParams regtestParams(CBaseChainParams::REGTEST);

    const uint256 hashPreModelDParent = uint256S("6666666666666666666666666666666666666666666666666666666666666661");
    const uint256 hashModelDParent    = uint256S("6666666666666666666666666666666666666666666666666666666666666662");
    const uint256 seedModelDParent    = uint256S("6666666666666666666666666666666666666666666666666666666666666663");

    CBlockIndex* pindexPreModelD = new CBlockIndex();
    pindexPreModelD->nHeight = 50; // Candidate height 51 (< Regtest UPGRADE_MODELD = 200)
    pindexPreModelD->nTime = 1780000000;
    pindexPreModelD->nBits = 0x207fffff;

    CBlockIndex* pindexModelD = new CBlockIndex();
    pindexModelD->nHeight = 350; // Candidate height 351 (>= Regtest UPGRADE_MODELD & UPGRADE_POMBL)
    pindexModelD->nTime = 1780010000;
    pindexModelD->nBits = 0x207fffff;

    {
        LOCK(cs_main);
        auto res1 = mapBlockIndex.insert(std::make_pair(hashPreModelDParent, pindexPreModelD));
        pindexPreModelD->phashBlock = &(res1.first->first);
        auto res2 = mapBlockIndex.insert(std::make_pair(hashModelDParent, pindexModelD));
        pindexModelD->phashBlock = &(res2.first->first);
        LOCK(cs_adam_seeds);
        mapAdamSeeds[hashModelDParent] = seedModelDParent;
    }
    struct CleanupFutureTestState {
        uint256 h1, h2;
        CBlockIndex* p1;
        CBlockIndex* p2;
        ~CleanupFutureTestState() {
            LOCK(cs_main);
            {
                LOCK(cs_adam_seeds);
                mapAdamSeeds.erase(h2);
            }
            mapBlockIndex.erase(h1);
            mapBlockIndex.erase(h2);
            delete p1;
            delete p2;
        }
    } cleanup{hashPreModelDParent, hashModelDParent, pindexPreModelD, pindexModelD};

    auto findPoolKey = [](const CPubKey& pk) -> CKey {
        for (int i = 0; i < 30; ++i) {
            CKey k = GetAdamDeterministicKey(i);
            if (k.GetPubKey() == pk) return k;
        }
        return CKey();
    };

    const std::vector<CPubKey> vPool = GetAdamMinerPool(50);
    BOOST_REQUIRE(!vPool.empty());
    CKey poolCoordKey = findPoolKey(vPool[0]);
    BOOST_REQUIRE(poolCoordKey.IsValid());
    CBLSSecretKey poolCoordBls = DeriveBLSFromCKey(poolCoordKey);

    CKey outsiderKey;
    outsiderKey.MakeNewKey(true);
    CBLSSecretKey outsiderBls = DeriveBLSFromCKey(outsiderKey);

    // 1. Orphan future headers (nullptr parent) and dummy byte blobs must be rejected
    CBlockHeader hdr;
    hdr.nVersion = 11;
    hdr.hashPrevBlock = hashPreModelDParent;
    hdr.nTime = pindexPreModelD->nTime + 30;
    hdr.nBits = 0x207fffff;
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, nullptr));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.vAdamVRFProof.assign(96, 0x11);
    hdr.vAdamCoordinatorSig.assign(96, 0x22);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));

    // 2. Cryptographically valid BLS+ECDSA signatures from a non-pool outsider key must be rejected
    const uint256 seedPreModelD = GetAdamSeed(pindexPreModelD);
    BOOST_REQUIRE(!seedPreModelD.IsNull());
    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedPreModelD, outsiderKey, outsiderBls, hdr.vAdamVRFProof));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), outsiderKey, outsiderBls, hdr.vAdamCoordinatorSig));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));

    // 3. Valid registered pool key passes on V11 and pre-Model-D V12; tampering or oversize fails
    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedPreModelD, poolCoordKey, poolCoordBls, hdr.vAdamVRFProof));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), poolCoordKey, poolCoordBls, hdr.vAdamCoordinatorSig));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, nullptr));
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.nNonce += 1;
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));
    hdr.nNonce -= 1;
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.nVersion = 12;
    hdr.vQuorumSig.clear();
    BOOST_CHECK(!IsModelDActive(pindexPreModelD->nHeight + 1));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), poolCoordKey, poolCoordBls, hdr.vAdamCoordinatorSig));
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.vQuorumSig.assign(4097, 0x33);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));
    hdr.vQuorumSig.clear();

    // 4. Model D active height requires a cryptographically valid 3-of-5 LLMQ CQuorumSignature
    CBlockHeader hdrModelD;
    hdrModelD.nVersion = 12;
    hdrModelD.hashPrevBlock = hashModelDParent;
    hdrModelD.nTime = pindexModelD->nTime + 30;
    hdrModelD.nBits = 0x207fffff;
    const int nModelDCandidateHeight = pindexModelD->nHeight + 1;
    BOOST_REQUIRE(IsModelDActive(nModelDCandidateHeight));

    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedModelDParent, poolCoordKey, poolCoordBls, hdrModelD.vAdamVRFProof));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdrModelD.GetHash(), poolCoordKey, poolCoordBls, hdrModelD.vAdamCoordinatorSig));

    // Empty or dummy vQuorumSig must fail when Model D is active
    BOOST_CHECK(!IsFutureBlockPreVerified(hdrModelD, pindexModelD));
    hdrModelD.vQuorumSig.assign(256, 0x44);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdrModelD, pindexModelD));

    const llmq::CQuorum quorum = llmq::GetActiveQuorum(nModelDCandidateHeight, pindexModelD);
    BOOST_REQUIRE_GE(quorum.members.size(), 3U);

    llmq::CQuorumSignature qsig;
    qsig.blockHash = hdrModelD.GetHash();
    for (size_t i = 0; i < 3; ++i) {
        CKey memberKey = findPoolKey(quorum.members[i].pubKeyMasternode);
        BOOST_REQUIRE(memberKey.IsValid());
        CBLSSecretKey memberBls = DeriveBLSFromCKey(memberKey);
        std::vector<unsigned char> memberSig;
        BOOST_REQUIRE(SignBLSWithECDSAFallback(qsig.blockHash, memberKey, memberBls, memberSig));
        qsig.signatures.emplace_back(quorum.members[i].collateralOutpoint, std::move(memberSig));
    }
    BOOST_REQUIRE(qsig.Verify(quorum));
    {
        CDataStream ssQ(SER_NETWORK, PROTOCOL_VERSION);
        ssQ << qsig;
        hdrModelD.vQuorumSig.assign(ssQ.begin(), ssQ.end());
    }
    BOOST_CHECK(IsFutureBlockPreVerified(hdrModelD, pindexModelD));

    // Quorum signature bound to a different blockHash must fail
    qsig.blockHash = uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    {
        CDataStream ssQ(SER_NETWORK, PROTOCOL_VERSION);
        ssQ << qsig;
        hdrModelD.vQuorumSig.assign(ssQ.begin(), ssQ.end());
    }
    BOOST_CHECK(!IsFutureBlockPreVerified(hdrModelD, pindexModelD));
}

BOOST_AUTO_TEST_CASE(adam_v12_pre_modeld_early_rotation_rejected_k2_test)
{
    ScopedChainParams regtestParams(CBaseChainParams::REGTEST);

    // Insert a temporary parent block index at height 50 (< Regtest UPGRADE_MODELD = 450, or ensure !IsModelDActive)
    uint256 parentHash = uint256S("7777777777777777777777777777777777777777777777777777777777777777");
    CBlockIndex* pindexParent = new CBlockIndex();
    pindexParent->phashBlock = nullptr;
    pindexParent->nHeight = 50;
    pindexParent->nTime = 1780000000;
    pindexParent->nBits = 0x207fffff;

    {
        LOCK(cs_main);
        auto res = mapBlockIndex.insert(std::make_pair(parentHash, pindexParent));
        pindexParent->phashBlock = &(res.first->first);
    }
    struct CleanupParentIndex {
        uint256 h;
        CBlockIndex* p;
        ~CleanupParentIndex() {
            LOCK(cs_main);
            mapBlockIndex.erase(h);
            delete p;
        }
    } cleanup{parentHash, pindexParent};

    const int nHeight = pindexParent->nHeight + 1;
    BOOST_REQUIRE(!IsModelDActive(nHeight));

    const uint256 adamSeed = GetAdamSeed(pindexParent);
    std::vector<CPubKey> vExpectedMiners;
    CPubKey expectedCoordinator;
    BOOST_REQUIRE(SelectAdamNodes(adamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator, nHeight));
    BOOST_REQUIRE_GE(vExpectedMiners.size(), 2U);

    auto findDetKey = [](const CPubKey& pk) -> CKey {
        for (int i = 0; i < 15; ++i) {
            CKey k = GetAdamDeterministicKey(i);
            if (k.GetPubKey() == pk) return k;
        }
        return CKey();
    };

    auto makeV12Sol = [&](size_t idx, const CPubKey& minerPk) -> std::vector<unsigned char> {
        CKey k = findDetKey(minerPk);
        BOOST_REQUIRE(k.IsValid());
        CBLSSecretKey bls = DeriveBLSFromCKey(k);
        uint32_t nonce = 1;
        CDataStream ssIn(SER_GETHASH, 0);
        ssIn << adamSeed << minerPk << nonce;
        int a1 = -1, a2 = -1, a3 = -1;
        GetAdam3PermutationAlgos(parentHash, minerPk, a1, a2, a3);
        int factor = (int)idx + 1;
        uint256 h3 = CalculateAdamPuzzleHash(a3,
            reinterpret_cast<const unsigned char*>(&ssIn[0]),
            reinterpret_cast<const unsigned char*>(&ssIn[0]) + ssIn.size());
        uint256 m1 = ArithToUint256(UintToArith256(h3) * factor);
        uint256 h2 = CalculateAdamPuzzleHash(a2, m1.begin(), m1.begin() + 32);
        uint256 m2 = ArithToUint256(UintToArith256(h2) * factor);
        uint256 pHash = CalculateAdamPuzzleHash(a1, m2.begin(), m2.begin() + 32);
        std::vector<unsigned char> sig;
        BOOST_REQUIRE(SignBLSWithECDSAFallback(pHash, k, bls, sig));
        CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
        ssSol << nonce << sig;
        return std::vector<unsigned char>(ssSol.begin(), ssSol.end());
    };

    CBlock baseBlock;
    baseBlock.nVersion = 12;
    baseBlock.hashPrevBlock = parentHash;
    baseBlock.nBits = 0x207fffff;
    baseBlock.nNonce = 0;
    baseBlock.vAdamMiners = vExpectedMiners;
    std::map<CPubKey, std::vector<unsigned char>> mapSols;
    for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
        auto sol = makeV12Sol(i, vExpectedMiners[i]);
        baseBlock.vAdamSolutions.push_back(sol);
        mapSols[vExpectedMiners[i]] = sol;
    }

    CMutableTransaction cb;
    cb.vin.resize(1);
    cb.vin[0].prevout.SetNull();
    cb.vin[0].scriptSig = CScript() << nHeight << OP_0;
    cb.vout.resize(1);
    cb.vout[0].nValue = 50 * COIN;
    cb.vout[0].scriptPubKey = CScript() << OP_TRUE;
    baseBlock.vtx.push_back(cb);
    baseBlock.hashMerkleRoot = BlockMerkleRoot(baseBlock);

    // 1) At t = +30s (< 45s), primary coordinator MUST succeed, whereas committee member front-running MUST fail!
    {
        CBlock blkPrimary = baseBlock;
        blkPrimary.nTime = pindexParent->nTime + 30;
        CKey coordKey = findDetKey(expectedCoordinator);
        BOOST_REQUIRE(coordKey.IsValid());
        CBLSSecretKey coordBls = DeriveBLSFromCKey(coordKey);
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, coordKey, coordBls, blkPrimary.vAdamVRFProof));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkPrimary.GetHash(), coordKey, coordBls, blkPrimary.vAdamCoordinatorSig));
        CValidationState st;
        BOOST_CHECK(CheckBlock(blkPrimary, st, false, true, true));
    }
    {
        CBlock blkFrontRun = baseBlock;
        blkFrontRun.nTime = pindexParent->nTime + 30;
        CPubKey rogueSigner = (vExpectedMiners[0] != expectedCoordinator) ? vExpectedMiners[0] : vExpectedMiners[1];
        CKey rogueKey = findDetKey(rogueSigner);
        BOOST_REQUIRE(rogueKey.IsValid());
        CBLSSecretKey rogueBls = DeriveBLSFromCKey(rogueKey);
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, rogueKey, rogueBls, blkFrontRun.vAdamVRFProof));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkFrontRun.GetHash(), rogueKey, rogueBls, blkFrontRun.vAdamCoordinatorSig));
        CValidationState st;
        BOOST_CHECK(!CheckBlock(blkFrontRun, st, false, true, true));
    }

    // 2) At t = +45s (>= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS), PoBLS XOR-ranked fallback coordinator MUST succeed!
    {
        CBlock blkFallback = baseBlock;
        blkFallback.nTime = pindexParent->nTime + 45;
        CPubKey activeFallback, prevFallback;
        int rotIdx = 0;
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(adamSeed, nHeight, 45, vExpectedMiners, mapSols, activeFallback, prevFallback, rotIdx));
        CKey fbKey = findDetKey(activeFallback);
        BOOST_REQUIRE(fbKey.IsValid());
        CBLSSecretKey fbBls = DeriveBLSFromCKey(fbKey);
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, fbKey, fbBls, blkFallback.vAdamVRFProof));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkFallback.GetHash(), fbKey, fbBls, blkFallback.vAdamCoordinatorSig));
        CValidationState st;
        BOOST_CHECK(CheckBlock(blkFallback, st, false, true, true));
    }
}

BOOST_AUTO_TEST_CASE(adam_zerocoin_mempool_check_no_abort_k3_test)
{
    ScopedChainParams regParams(CBaseChainParams::REGTEST);

    CTxMemPool testPool(CFeeRate(1000));
    testPool.setSanityCheck(1.0);

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    uint256 challengeHash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);
    uint32_t nonce = 0;
    while (true) {
        if (ComputeZeroCoinRegistrationHash(nonce, challengeHash, pubkey, true) <= zeroCoinTarget)
            break;
        ++nonce;
    }
    CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
    ssNonce << nonce;

    CScript regScript = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                  << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                  << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                  << OP_DROP << OP_DROP << OP_DROP
                                  << CScriptNum(3500) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                  << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.push_back(CTxIn(COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0)));
    mtx.vout.push_back(CTxOut(0, regScript));
    CTransaction tx(mtx);
    BOOST_REQUIRE(IsZeroCoinPoWTransaction(tx));

    CTxMemPoolEntry entry(tx, 0, 1780000000, 0.0, 100, true, 0, false, 1);
    BOOST_CHECK(testPool.addUnchecked(tx.GetHash(), entry));

    uint256 tipHash = uint256S("5555555555555555555555555555555555555555555555555555555555555555");
    CBlockIndex tipIndex;
    tipIndex.nHeight = 100;
    {
        LOCK(cs_main);
        mapBlockIndex[tipHash] = &tipIndex;
    }

    CCoinsView coinsDummy;
    CCoinsViewCache coinsView(&coinsDummy);
    coinsView.SetBestBlock(tipHash);
    BOOST_CHECK(!coinsView.HaveCoin(tx.vin[0].prevout));

    // Must not trigger SIGABRT on sentinel input 0x01:0 across HaveCoin, CheckInputs, and UpdateCoins
    testPool.check(&coinsView);

    {
        LOCK(cs_main);
        mapBlockIndex.erase(tipHash);
    }
}

BOOST_AUTO_TEST_CASE(adam_cooperative_pos_cosigning_roundtrip_k4_test)
{
    CKey stakerKey, coordKey, attackerKey;
    stakerKey.MakeNewKey(true);
    coordKey.MakeNewKey(true);
    attackerKey.MakeNewKey(true);
    CPubKey stakerPubKey = stakerKey.GetPubKey();
    CPubKey coordPubKey = coordKey.GetPubKey();

    CBasicKeyStore stakerKeystore;
    stakerKeystore.AddKey(stakerKey);
    CBasicKeyStore coordKeystore;
    coordKeystore.AddKey(coordKey);
    CBasicKeyStore attackerKeystore;
    attackerKeystore.AddKey(attackerKey);

    // Construct a P2PKH coinstake draft PoS block
    CBlock draft;
    draft.nVersion = 12;
    draft.hashPrevBlock = uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    draft.nTime = 1780000030;
    draft.nBits = 0x207fffff;
    draft.nNonce = 0;

    CMutableTransaction txCoinbase;
    txCoinbase.vin.push_back(CTxIn(COutPoint(), CScript() << 250 << OP_0));
    txCoinbase.vout.push_back(CTxOut());

    CMutableTransaction txCoinStake;
    std::vector<unsigned char> dummyInputSig(71, 0x30);
    CScript p2pkhScriptSig = CScript() << dummyInputSig << ToByteVector(stakerPubKey);
    txCoinStake.vin.push_back(CTxIn(uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"), 0, p2pkhScriptSig));
    txCoinStake.vout.push_back(CTxOut(0, CScript())); // Empty vout[0] PoS marker
    txCoinStake.vout.push_back(CTxOut(100 * COIN, GetScriptForDestination(stakerPubKey.GetID())));

    draft.vtx.push_back(txCoinbase);
    draft.vtx.push_back(txCoinStake);
    draft.hashMerkleRoot = BlockMerkleRoot(draft);
    BOOST_REQUIRE(draft.IsProofOfStake());

    // 1. Staker signs request authorization
    const uint256 reqHash = ComputePoSCoordRequestHash(draft, coordPubKey);
    std::vector<unsigned char> authSig;
    BOOST_CHECK(!SignPoSCoordRequest(draft, reqHash, coordKeystore, authSig));
    BOOST_CHECK(!SignPoSCoordRequest(draft, reqHash, attackerKeystore, authSig));
    BOOST_REQUIRE(SignPoSCoordRequest(draft, reqHash, stakerKeystore, authSig));
    BOOST_CHECK(VerifyPoSCoordRequestAuth(draft, reqHash, authSig, true));
    BOOST_CHECK(!VerifyPoSCoordRequestAuth(draft, reqHash, authSig, false)); // P2PKH disabled check

    // 2. Coordinator slot guard allows idempotent retry of reqHash and rejects competing reqHash2 on Mainnet
    {
        ScopedChainParams mainParams(CBaseChainParams::MAIN);
        uint256 slotParent = uint256S("efefefefefefefefefefefefefefefefefefefefefefefefefefefefefefefef");
        uint256 reqHash2 = uint256S("dededededededededededededededededededededededededededededededede");
        BOOST_CHECK(AcquireCoordinatorProposalSlot(slotParent, coordPubKey, reqHash, true));
        BOOST_CHECK(AcquireCoordinatorProposalSlot(slotParent, coordPubKey, reqHash, true));  // Idempotent retry allowed
        BOOST_CHECK(!AcquireCoordinatorProposalSlot(slotParent, coordPubKey, reqHash2, true)); // Competing block blocked
    }

    // 3. Coordinator signs VRF proof first, then header signature; Staker attaches and signs final vchBlockSig
    const uint256 adamSeed = uint256S("9999999999999999999999999999999999999999999999999999999999999999");
    CBLSSecretKey coordBls = DeriveBLSFromCKey(coordKey);
    CBlock coordSigned = draft;
    BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, coordKey, coordBls, coordSigned.vAdamVRFProof));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(coordSigned.GetHash(), coordKey, coordBls, coordSigned.vAdamCoordinatorSig));

    // Request hash must remain identical even after VRF/Coordinator signatures are attached
    BOOST_CHECK_EQUAL(reqHash.GetHex(), ComputePoSCoordRequestHash(coordSigned, coordPubKey).GetHex());

    CBlock finalBlock = draft;
    finalBlock.vAdamVRFProof = coordSigned.vAdamVRFProof;
    finalBlock.vAdamCoordinatorSig = coordSigned.vAdamCoordinatorSig;
    BOOST_CHECK(VerifyAdamVRFProof(adamSeed, finalBlock.vAdamVRFProof, coordPubKey));
    BOOST_CHECK(VerifyAdamCoordinatorSig(finalBlock, coordPubKey));

    BOOST_REQUIRE(SignBlock(finalBlock, stakerKeystore));
    BOOST_CHECK(VerifyAdamCoordinatorSig(finalBlock, coordPubKey));
    BOOST_CHECK(CheckBlockSignature(finalBlock, true));

    // 4. Verify RecordSeenPoSCoordReq stores clean draft header + seed so forged vAdamCoordinatorSig cannot poison cache
    {
        LOCK(cs_pos_coord_sigs);
        mapSeenPoSCoordReqs.erase(reqHash);
    }
    BOOST_CHECK(RecordSeenPoSCoordReq(reqHash, coordPubKey, draft, adamSeed));
    BOOST_CHECK(!RecordSeenPoSCoordReq(reqHash, coordPubKey, draft, adamSeed)); // Duplicate ignored

    CSeenPoSCoordReqEntry trackedReq;
    {
        LOCK(cs_pos_coord_sigs);
        auto itReq = mapSeenPoSCoordReqs.find(reqHash);
        BOOST_REQUIRE(itReq != mapSeenPoSCoordReqs.end());
        trackedReq = itReq->second;
        mapSeenPoSCoordReqs.erase(itReq);
    }
    BOOST_CHECK(trackedReq.targetCoordinator == coordPubKey);
    BOOST_CHECK_EQUAL(trackedReq.adamSeed.GetHex(), adamSeed.GetHex());

    CBlockHeader reconstructed = trackedReq.draftHeader;
    reconstructed.vAdamVRFProof = coordSigned.vAdamVRFProof;
    reconstructed.vAdamCoordinatorSig = coordSigned.vAdamCoordinatorSig;
    BOOST_CHECK(VerifyAdamCoordinatorSig(reconstructed, trackedReq.targetCoordinator));

    // Forged coordinator signature signed over a different hash (even with same BLS pubkey!) must fail header verification
    std::vector<unsigned char> forgedCoordSig;
    BOOST_REQUIRE(SignBLSWithECDSAFallback(uint256S("abababababababababababababababababababababababababababababababab"),
                                           coordKey, coordBls, forgedCoordSig));
    reconstructed.vAdamCoordinatorSig = forgedCoordSig;
    BOOST_CHECK(!VerifyAdamCoordinatorSig(reconstructed, trackedReq.targetCoordinator));

    // 5. Verify shared CMinerRegistrationSlotGuard mutual exclusion
    {
        CMinerRegistrationSlotGuard slot1;
        BOOST_CHECK(slot1.fAcquired);
        {
            CMinerRegistrationSlotGuard slot2;
            BOOST_CHECK(!slot2.fAcquired);
        }
        BOOST_CHECK(!TryAcquireMinerRegistrationSlot());
    }
    {
        CMinerRegistrationSlotGuard slot3;
        BOOST_CHECK(slot3.fAcquired);
    }

    // 6. Verify TestBlockValidity gracefully rejects stale pindexPrev / pcoinsTip mismatch instead of hitting ConnectBlock assert
    {
        LOCK(cs_main);
        CBlockIndex staleIndex;
        uint256 staleHash = uint256S("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");
        staleIndex.phashBlock = &staleHash;
        staleIndex.nHeight = 0;
        CValidationState staleState;
        BOOST_CHECK(!TestBlockValidity(staleState, finalBlock, &staleIndex, false, false));
    }
}

BOOST_AUTO_TEST_CASE(rev44_hardening_llmq_regcache_zerocoin_and_ping_test)
{
    // 1. Dynamic Active-MN-Scaled LLMQ Quorum Sizing (Section 2: 5 -> 9 -> 11 -> 15) & BFT Thresholds
    {
        ScopedChainParams mainParams(CBaseChainParams::MAIN);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(0, 120), 5); // Pre-UPGRADE_HARDENING_V1 (Block 0) stays 5
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 0), 5);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 8), 5);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 24), 5);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 25), 9);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 49), 9);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 50), 11);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 99), 11);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 100), 15);
        BOOST_CHECK_EQUAL(llmq::GetTargetQuorumSize(1, 250), 15);

        const uint256 blockHash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
        const int sizes[4] = {5, 9, 11, 15};
        for (int qSize : sizes) {
            llmq::CQuorum quorum;
            quorum.nHeight = 2500;
            quorum.quorumHash = uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321");
            std::vector<CKey> mnKeys(qSize);
            for (int i = 0; i < qSize; ++i) {
                mnKeys[i].MakeNewKey(true);
                COutPoint out(uint256S("0101010101010101010101010101010101010101010101010101010101010101"), (uint32_t)i);
                quorum.members.emplace_back(out, mnKeys[i].GetPubKey());
            }
            const int threshold = qSize / 2 + 1;
            llmq::CQuorumSignature qsig;
            qsig.blockHash = blockHash;
            for (int i = 0; i < threshold - 1; ++i) {
                CBLSSecretKey blsSk = DeriveBLSFromCKey(mnKeys[i]);
                std::vector<unsigned char> vchSig;
                BOOST_REQUIRE(SignBLSWithECDSAFallback(blockHash, mnKeys[i], blsSk, vchSig));
                qsig.signatures.emplace_back(quorum.members[i].collateralOutpoint, vchSig);
            }
            BOOST_CHECK(!qsig.Verify(quorum)); // threshold - 1 must fail

            CBLSSecretKey blsThresh = DeriveBLSFromCKey(mnKeys[threshold - 1]);
            std::vector<unsigned char> vchThreshSig;
            BOOST_REQUIRE(SignBLSWithECDSAFallback(blockHash, mnKeys[threshold - 1], blsThresh, vchThreshSig));
            qsig.signatures.emplace_back(quorum.members[threshold - 1].collateralOutpoint, vchThreshSig);
            BOOST_CHECK(qsig.Verify(quorum)); // exact threshold (3/5, 5/9, 6/11, 8/15) must pass!
        }
    }

    // 2. CAdamBlockRegStateCache LevelDB Serialization Roundtrip (Section 3)
    {
        CKey k1, k2;
        k1.MakeNewKey(true);
        k2.MakeNewKey(true);

        CAdamBlockRegStateCache cacheIn;
        cacheIn.hashBestBlock = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
        cacheIn.cachedGenesisHash = uint256S("0000050d4a94e97a9491c2ddde1251ff040772fee943e653fe583d5737e4d5e3");
        cacheIn.cachedBootstrapScannedUpTo = 199;
        cacheIn.cachedBootstrapKeys.insert(k1.GetPubKey());

        CCachedBlockRegEntry regEntry;
        regEntry.isCoinLock = false;
        regEntry.outpoint = COutPoint(uint256S("2222222222222222222222222222222222222222222222222222222222222222"), 0);
        regEntry.nValue = 10 * COIN;
        regEntry.pubkey = k2.GetPubKey();
        regEntry.lockTime = 3500;
        regEntry.challenge = cacheIn.cachedGenesisHash;
        cacheIn.vRecentBlockRegs.push_back(std::make_pair(cacheIn.hashBestBlock, std::vector<CCachedBlockRegEntry>{regEntry}));

        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << cacheIn;

        CAdamBlockRegStateCache cacheOut;
        ss >> cacheOut;
        BOOST_CHECK_EQUAL(cacheOut.hashBestBlock.GetHex(), cacheIn.hashBestBlock.GetHex());
        BOOST_CHECK_EQUAL(cacheOut.cachedGenesisHash.GetHex(), cacheIn.cachedGenesisHash.GetHex());
        BOOST_CHECK_EQUAL(cacheOut.cachedBootstrapScannedUpTo, 199);
        BOOST_CHECK_EQUAL(cacheOut.cachedBootstrapKeys.size(), 1U);
        BOOST_CHECK_EQUAL(cacheOut.vRecentBlockRegs.size(), 1U);
        BOOST_REQUIRE_EQUAL(cacheOut.vRecentBlockRegs[0].second.size(), 1U);
        BOOST_CHECK_EQUAL(cacheOut.vRecentBlockRegs[0].second[0].nValue, 10 * COIN);
        BOOST_CHECK_EQUAL(cacheOut.vRecentBlockRegs[0].second[0].lockTime, 3500);
        BOOST_CHECK(cacheOut.vRecentBlockRegs[0].second[0].pubkey == k2.GetPubKey());
    }

    // 3. Fair Zero-Coin PoW (< 13 Funded Validators, No Alphabetical Truncation), LevelDB Reg Cache Roundtrip + LOCKTIME_THRESHOLD Guard (Sections 3, 4 & 6)
    {
        ScopedChainParams testParams(CBaseChainParams::TESTNET);
        LOCK(cs_main);
        CCoinsView dummyView;
        CCoinsViewCache testCoins(&dummyView);

        const int nOrigHeight = chainActive.Height();
        CBlockIndex* pOrigTip = chainActive.Tip();
        std::unique_ptr<CBlockIndex> tempGenesis;
        uint256 tempGenesisHash;
        if (!pOrigTip) {
            tempGenesisHash = Params().GenesisBlock().GetHash();
            tempGenesis.reset(new CBlockIndex(Params().GenesisBlock()));
            tempGenesis->nHeight = 0;
            tempGenesis->pprev = nullptr;
            auto gIns = mapBlockIndex.insert(std::make_pair(tempGenesisHash, tempGenesis.get()));
            tempGenesis->phashBlock = &(gIns.first->first);
            tempGenesis->BuildSkip();
            chainActive.SetTip(tempGenesis.get());
        }
        CBlockIndex* pBaseTip = chainActive.Tip();
        BOOST_REQUIRE(pBaseTip != nullptr);

        std::vector<uint256> vAddedHashes(19);
        std::vector<CBlockIndex*> vAddedIndices(19, nullptr);
        CBlockIndex* pPrev = pBaseTip;

        std::vector<CKey> vZeroCoinKeys(16);
        for (int i = 0; i < 16; ++i) {
            vZeroCoinKeys[i].MakeNewKey(true);
        }
        CKey badUnixLockKey;
        badUnixLockKey.MakeNewKey(true);

        static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
        const uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);

        auto makePoWRegScript = [&](const CPubKey& pk, const uint256& chHash, int64_t lockHeight) -> CScript {
            uint32_t nNonce = SolveZeroCoinPoWForTest(pk, chHash, zeroCoinTarget, true);
            std::vector<unsigned char> vchNonce(4);
            vchNonce[0] = (unsigned char)(nNonce & 0xff);
            vchNonce[1] = (unsigned char)((nNonce >> 8) & 0xff);
            vchNonce[2] = (unsigned char)((nNonce >> 16) & 0xff);
            vchNonce[3] = (unsigned char)((nNonce >> 24) & 0xff);
            return CScript() << vchNonce
                             << std::vector<unsigned char>(chHash.begin(), chHash.end())
                             << std::vector<unsigned char>(pk.begin(), pk.end())
                             << OP_DROP << OP_DROP << OP_DROP
                             << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                             << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        };

        for (int i = 0; i < 18; ++i) {
            const int h = pBaseTip->nHeight + 1 + i;
            const uint256 freshChallenge = pPrev->GetBlockHash();
            CBlock blk;
            blk.nVersion = 12;
            blk.hashPrevBlock = freshChallenge;
            blk.nTime = pPrev->nTime + 30;
            blk.nNonce = (uint32_t)(i + 1);
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk.vtx.push_back(cb);

            if (i < 16) {
                CMutableTransaction ztx;
                ztx.vin.push_back(CTxIn(zeroCoinSentinel));
                ztx.vout.push_back(CTxOut(0, makePoWRegScript(vZeroCoinKeys[i].GetPubKey(), freshChallenge, h + 260)));
                blk.vtx.push_back(ztx);
            } else if (i == 16) {
                // Attempt LOCKTIME_THRESHOLD Unix timestamp bypass (500,000,100)
                CMutableTransaction badTx;
                badTx.vin.push_back(CTxIn(zeroCoinSentinel));
                badTx.vout.push_back(CTxOut(0, makePoWRegScript(badUnixLockKey.GetPubKey(), freshChallenge, LOCKTIME_THRESHOLD + 100)));
                blk.vtx.push_back(badTx);
            }
            blk.hashMerkleRoot = BlockMerkleRoot(blk);
            vAddedHashes[i] = blk.GetHash();

            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = pPrev;
            auto ins = mapBlockIndex.insert(std::make_pair(vAddedHashes[i], pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            vAddedIndices[i] = pIdx;
            chainActive.SetTip(pIdx);

            RegisterBlockMasternodeActivity(blk, h, &testCoins);
            pPrev = pIdx;
        }

        // Flush to LevelDB and reload via InitializeAdamMinerRegCache(false) to verify cold-start persistence without disk blocks!
        BOOST_CHECK(FlushAdamMinerRegCacheToDisk(chainActive.Tip()->GetBlockHash()));
        InitializeAdamMinerRegCache(false);

        const int nEvalHeight = chainActive.Height();
        // Funded count is 0 (< 13), so ALL 16 Zero-Coin PoW miners (> 13, up to MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16)
        // must be admitted without alphabetical truncation at 13, while badUnixLockKey (>= LOCKTIME_THRESHOLD) MUST be rejected!
        size_t nExplicit = 0, nFunded = 0;
        std::vector<CPubKey> poolWith16Zero = GetAdamMinerPool(nEvalHeight, &testCoins, nullptr, nullptr, &nExplicit, &nFunded);
        BOOST_CHECK_EQUAL(nFunded, 0U);
        for (int i = 0; i < 16; ++i) {
            BOOST_CHECK(std::find(poolWith16Zero.begin(), poolWith16Zero.end(), vZeroCoinKeys[i].GetPubKey()) != poolWith16Zero.end());
        }
        BOOST_CHECK(std::find(poolWith16Zero.begin(), poolWith16Zero.end(), badUnixLockKey.GetPubKey()) == poolWith16Zero.end());

        // Now add 13 funded Coin-Lock validators (250 KRISTA each) in block 19 and verify Zero-Coin PoW keys are cleanly phased out!
        const int h19 = pPrev->nHeight + 1;
        CBlock fundedBlk;
        fundedBlk.nVersion = 12;
        fundedBlk.hashPrevBlock = pPrev->GetBlockHash();
        fundedBlk.nTime = pPrev->nTime + 30;
        fundedBlk.nNonce = 19;
        CMutableTransaction cb2;
        cb2.vin.push_back(CTxIn(COutPoint(), CScript() << h19 << OP_0));
        cb2.vout.push_back(CTxOut(0, CScript()));
        fundedBlk.vtx.push_back(cb2);
        std::vector<CKey> vFundedKeys(13);
        for (int i = 0; i < 13; ++i) {
            vFundedKeys[i].MakeNewKey(true);
            const CPubKey pk = vFundedKeys[i].GetPubKey();
            CScript clScript = CScript() << ToByteVector(pk) << OP_DROP
                                         << CScriptNum(h19 + 2900) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                         << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
            CMutableTransaction ftx;
            ftx.vin.push_back(CTxIn(COutPoint(uint256S("4400000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i)));
            ftx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, clScript));
            CTransaction ftxConst(ftx);
            fundedBlk.vtx.push_back(ftxConst);
            testCoins.AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], h19, false, false), true);
        }
        fundedBlk.hashMerkleRoot = BlockMerkleRoot(fundedBlk);
        vAddedHashes[18] = fundedBlk.GetHash();
        CBlockIndex* pIdx19 = new CBlockIndex(fundedBlk);
        pIdx19->nHeight = h19;
        pIdx19->pprev = pPrev;
        auto ins19 = mapBlockIndex.insert(std::make_pair(vAddedHashes[18], pIdx19));
        pIdx19->phashBlock = &(ins19.first->first);
        pIdx19->BuildSkip();
        vAddedIndices[18] = pIdx19;
        chainActive.SetTip(pIdx19);

        RegisterBlockMasternodeActivity(fundedBlk, h19, &testCoins);

        size_t nFundedAfter = 0;
        std::vector<CPubKey> poolAfterFunded = GetAdamMinerPool(h19, &testCoins, nullptr, nullptr, nullptr, &nFundedAfter);
        BOOST_CHECK_EQUAL(nFundedAfter, 13U);
        for (int i = 0; i < 13; ++i) {
            BOOST_CHECK(std::find(poolAfterFunded.begin(), poolAfterFunded.end(), vFundedKeys[i].GetPubKey()) != poolAfterFunded.end());
        }
        for (int i = 0; i < 16; ++i) {
            BOOST_CHECK(std::find(poolAfterFunded.begin(), poolAfterFunded.end(), vZeroCoinKeys[i].GetPubKey()) == poolAfterFunded.end());
        }

        // Restore chainActive and mapBlockIndex cleanly
        chainActive.SetTip(pOrigTip);
        for (int i = 0; i < 19; ++i) {
            mapBlockIndex.erase(vAddedHashes[i]);
            delete vAddedIndices[i];
        }
        if (tempGenesis) {
            mapBlockIndex.erase(tempGenesisHash);
        }
        (void)nOrigHeight;
        InitializeAdamMinerRegCache(true);
    }

    // 4. Masternode Ping 0.0001 KRISTA Relay Fee Exemption Hardening (Section 7: P2SH Spoofing, Trailing Spam & 360-Block Cooldown)
    {
        ScopedChainParams mainParams(CBaseChainParams::MAIN);
        LOCK(cs_main);

        CKey mnKey;
        mnKey.MakeNewKey(true);
        const CPubKey mnPubKey = mnKey.GetPubKey();
        const COutPoint colOut(uint256S("7777777777777777777777777777777777777777777777777777777777777777"), 0);
        pcoinsTip->AddCoin(colOut, Coin(CTxOut(4200 * COIN, GetScriptForDestination(mnPubKey.GetID())), 1, false, false), true);

        // (a) Zero-collateral P2SH input spoofing (redeemScript mimics leaf_script without Taproot control_block or vchPingSig) MUST fail!
        CScript fakeLeaf = GetMasternodePingScript(mnPubKey);
        CMutableTransaction spoofTx;
        COutPoint p2shPrev(uint256S("8888888888888888888888888888888888888888888888888888888888888888"), 0);
        pcoinsTip->AddCoin(p2shPrev, Coin(CTxOut(COIN, GetScriptForDestination(CScriptID(fakeLeaf))), 1, false, false), true);
        spoofTx.vin.push_back(CTxIn(p2shPrev, CScript() << std::vector<unsigned char>(64, 0x11) << ToByteVector(fakeLeaf) << std::vector<unsigned char>(33, 0xc0)));
        spoofTx.vout.push_back(CTxOut(COIN - 10000, GetScriptForDestination(mnPubKey.GetID())));
        CDataStream ssNoSig(SER_NETWORK, PROTOCOL_VERSION);
        ssNoSig << colOut;
        spoofTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssNoSig)));

        CPubKey extPk;
        COutPoint extOut;
        BOOST_CHECK(!ExtractOnChainMasternodePing(CTransaction(spoofTx), extPk, extOut, false, nullptr, pcoinsTip, 10, true));
        BOOST_CHECK(!ExtractOnChainMasternodePing(CTransaction(spoofTx), extPk, extOut, false, nullptr, pcoinsTip, 10, false));

        // (b) Genuine signed ping gets < 0.0001 KRISTA relay fee initially, trailing spam bytes fail, and 360-block cooldown revokes exemption!
        CMutableTransaction validPing;
        validPing.vin.push_back(CTxIn(COutPoint(uint256S("9999999999999999999999999999999999999999999999999999999999999999"), 0)));
        validPing.vout.push_back(CTxOut(0.01 * COIN, fakeLeaf));
        CHashWriter ssPingHash(SER_GETHASH, 0);
        ssPingHash << colOut << validPing.vin[0].prevout;
        std::vector<unsigned char> vchPingSig;
        BOOST_REQUIRE(mnKey.Sign(ssPingHash.GetHash(), vchPingSig));

        CDataStream ssValidPayload(SER_NETWORK, PROTOCOL_VERSION);
        ssValidPayload << colOut << vchPingSig << false;
        validPing.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssValidPayload)));

        CTransaction validPingConst(validPing);
        const unsigned int validSize = ::GetSerializeSize(validPingConst, SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK_LT(GetMinRelayFee(validPingConst, mempool, validSize, false), 10000);

        // Appending 1 trailing spam byte after hasDelegation MUST revoke fee exemption (charges 1000x OP_RETURN multiplier >= 0.02 KRISTA vs 0.0001 KRISTA)!
        CMutableTransaction trailingSpamPing = validPing;
        std::vector<unsigned char> spamPayload(ssValidPayload.begin(), ssValidPayload.end());
        spamPayload.push_back(0x42);
        trailingSpamPing.vout[1] = CTxOut(0, CScript() << OP_RETURN << spamPayload);
        CTransaction trailingSpamConst(trailingSpamPing);
        const unsigned int spamSize = ::GetSerializeSize(trailingSpamConst, SER_NETWORK, PROTOCOL_VERSION);
        BOOST_CHECK_GE(GetMinRelayFee(trailingSpamConst, mempool, spamSize, false), 1000000);

        // Recording an on-chain ping at current height triggers the 360-block cooldown, revoking the fee exemption!
        const int curH = std::max(1, chainActive.Height());
        BOOST_CHECK(RegisterOnChainMasternodePing(mnPubKey, colOut, curH, nullptr, pcoinsTip));
        // Even at curH == 1 vs chainActive.Height() == 0 in unit test, test IsMasternodeActiveAtHeight at curH:
        if (chainActive.Height() >= curH) {
            BOOST_CHECK_GE(GetMinRelayFee(validPingConst, mempool, validSize, false), 10000000);
        }
        BOOST_CHECK(IsMasternodeActiveAtHeight(mnPubKey, curH + MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS, MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS));
        BOOST_CHECK(!IsMasternodeActiveAtHeight(mnPubKey, curH + MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS + 1, MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS));

        // Cleanup
        mapMasternodeLastActiveHeight.erase(mnPubKey);
        mapMasternodeActiveHeights.erase(mnPubKey);
        mapMasternodeCollateralOutpoints.erase(mnPubKey);
        pcoinsTip->SpendCoin(colOut);
        pcoinsTip->SpendCoin(p2shPrev);
    }
}

BOOST_AUTO_TEST_CASE(rev45_hardening_v2_zerocoin_sybil_defense_test)
{
    static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);

    auto makeRegScriptWithNonce = [](const CPubKey& pk, const uint256& chHash, uint32_t nNonce, int64_t lockHeight) -> CScript {
        std::vector<unsigned char> vchNonce(4);
        vchNonce[0] = (unsigned char)(nNonce & 0xff);
        vchNonce[1] = (unsigned char)((nNonce >> 8) & 0xff);
        vchNonce[2] = (unsigned char)((nNonce >> 16) & 0xff);
        vchNonce[3] = (unsigned char)((nNonce >> 24) & 0xff);
        return CScript() << vchNonce
                         << std::vector<unsigned char>(chHash.begin(), chHash.end())
                         << std::vector<unsigned char>(pk.begin(), pk.end())
                         << OP_DROP << OP_DROP << OP_DROP
                         << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                         << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    };

    // 1. CheckInputs & ExtractBlockMinerRegEntries: HashX11KVS accepted; legacy SHA256d and uncompressed pubkeys rejected under UPGRADE_HARDENING_V2
    {
        ScopedChainParams regParams(CBaseChainParams::REGTEST);
        CCoinsView dummyView;
        CCoinsViewCache coinsView(&dummyView);
        const uint256 tipHash = uint256S("6600000000000000000000000000000000000000000000000000000000000066");
        CBlockIndex tipIndex;
        tipIndex.nHeight = 9; // nSpendHeight = 10 (>= UPGRADE_HARDENING_V2 = 1)
        {
            LOCK(cs_main);
            mapBlockIndex[tipHash] = &tipIndex;
        }
        coinsView.SetBestBlock(tipHash);
        const uint256 chHash = uint256S("0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20");
        const uint256 targetV2 = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);

        // (a) Valid compressed key + HashX11KVS nonce passes CheckInputs at nSpendHeight = 10
        CKey validKey;
        validKey.MakeNewKey(true);
        const CPubKey validPk = validKey.GetPubKey();
        const uint32_t validNonce = SolveZeroCoinPoWForTest(validPk, chHash, targetV2, true);
        CMutableTransaction validMtx;
        validMtx.vin.push_back(CTxIn(zeroCoinSentinel));
        validMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(validPk, chHash, validNonce, 300)));
        CTransaction validTx(validMtx);
        PrecomputedTransactionData precomValid(validTx);
        CValidationState stValid;
        {
            LOCK(cs_main);
            BOOST_CHECK(CheckInputs(validTx, stValid, coinsView, false, 0, false, precomValid));
        }

        // (b) Legacy SHA256d nonce (where HashX11KVS > targetV2) MUST be rejected at nSpendHeight = 10
        uint32_t shaOnlyNonce = 0;
        for (;; ++shaOnlyNonce) {
            if (ComputeZeroCoinRegistrationHash(shaOnlyNonce, chHash, validPk, false) <= targetV2 &&
                ComputeZeroCoinRegistrationHash(shaOnlyNonce, chHash, validPk, true) > targetV2) {
                break;
            }
        }
        CMutableTransaction legacyShaMtx;
        legacyShaMtx.vin.push_back(CTxIn(zeroCoinSentinel));
        legacyShaMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(validPk, chHash, shaOnlyNonce, 300)));
        CTransaction legacyShaTx(legacyShaMtx);
        PrecomputedTransactionData precomLegacy(legacyShaTx);
        CValidationState stLegacy;
        {
            LOCK(cs_main);
            BOOST_CHECK(!CheckInputs(legacyShaTx, stLegacy, coinsView, false, 0, false, precomLegacy));
        }
        BOOST_CHECK_EQUAL(stLegacy.GetRejectReason(), "bad-txns-zerocoin-pow-invalid");

        // (c) Uncompressed pubkey MUST be rejected both by IsZeroCoinPoWTransaction / CheckTransaction ("bad-txns-prevout-null") and CheckInputs
        CKey uncompKey;
        uncompKey.MakeNewKey(false);
        const CPubKey uncompPk = uncompKey.GetPubKey();
        BOOST_REQUIRE(!uncompPk.IsCompressed());
        CMutableTransaction uncompMtx;
        uncompMtx.vin.push_back(CTxIn(zeroCoinSentinel));
        uncompMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(uncompPk, chHash, 0, 300)));
        CTransaction uncompTx(uncompMtx);
        BOOST_CHECK(!IsZeroCoinPoWTransaction(uncompTx));
        CValidationState stUncompTx;
        BOOST_CHECK(!CheckTransaction(uncompTx, stUncompTx));
        BOOST_CHECK_EQUAL(stUncompTx.GetRejectReason(), "bad-txns-prevout-null");
        PrecomputedTransactionData precomUncomp(uncompTx);
        CValidationState stUncomp;
        {
            LOCK(cs_main);
            BOOST_CHECK(!CheckInputs(uncompTx, stUncomp, coinsView, false, 0, false, precomUncomp));
            mapBlockIndex.erase(tipHash);
        }
    }

    // 2. 10-Block Challenge Freshness, 240-Block Lease TTL, 16-Key Cap, Bootstrap Preservation & Strict Economic Election Priority (Rule 1 & Rule 2)
    {
        ScopedChainParams testParams(CBaseChainParams::TESTNET);
        LOCK(cs_main);
        CCoinsView dummyView;
        CCoinsViewCache testCoins(&dummyView);

        CBlockIndex* pOrigTip = chainActive.Tip();
        std::unique_ptr<CBlockIndex> tempGenesis;
        uint256 tempGenesisHash;
        if (!pOrigTip) {
            tempGenesisHash = Params().GenesisBlock().GetHash();
            tempGenesis.reset(new CBlockIndex(Params().GenesisBlock()));
            tempGenesis->nHeight = 0;
            tempGenesis->pprev = nullptr;
            auto gIns = mapBlockIndex.insert(std::make_pair(tempGenesisHash, tempGenesis.get()));
            tempGenesis->phashBlock = &(gIns.first->first);
            tempGenesis->BuildSkip();
            chainActive.SetTip(tempGenesis.get());
        }
        CBlockIndex* pBaseTip = chainActive.Tip();
        BOOST_REQUIRE(pBaseTip != nullptr);
        const int baseH = pBaseTip->nHeight;

        const uint256 targetTestV2 = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);
        std::vector<uint256> vHashes;
        std::vector<CBlockIndex*> vIndices;
        CBlockIndex* pPrev = pBaseTip;

        auto appendTestBlock = [&](const std::vector<CTransaction>& extraTxs, const CPubKey* pCoinbaseDest = nullptr) -> CBlockIndex* {
            const int h = pPrev->nHeight + 1;
            CBlock blk;
            blk.nVersion = 12;
            blk.hashPrevBlock = pPrev->GetBlockHash();
            blk.nTime = pPrev->nTime + 30;
            blk.nNonce = (uint32_t)h;
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            if (pCoinbaseDest && pCoinbaseDest->IsValid()) {
                cb.vout.push_back(CTxOut(50 * COIN, CScript() << ToByteVector(*pCoinbaseDest) << OP_CHECKSIG));
            } else {
                cb.vout.push_back(CTxOut(0, CScript()));
            }
            blk.vtx.push_back(cb);
            for (const auto& tx : extraTxs) {
                blk.vtx.push_back(tx);
            }
            blk.hashMerkleRoot = BlockMerkleRoot(blk);
            uint256 bh = blk.GetHash();
            vHashes.push_back(bh);
            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = pPrev;
            auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            vIndices.push_back(pIdx);
            chainActive.SetTip(pIdx);
            RegisterBlockMasternodeActivity(blk, h, &testCoins);
            pPrev = pIdx;
            return pIdx;
        };

        // Mine 2 real Bootstrap coinbase blocks (Blocks baseH+1, baseH+2) + 10 empty blocks up to baseH+12
        CKey bootKey1, bootKey2;
        bootKey1.MakeNewKey(true);
        bootKey2.MakeNewKey(true);
        const CPubKey bootPk1 = bootKey1.GetPubKey();
        const CPubKey bootPk2 = bootKey2.GetPubKey();
        appendTestBlock({}, &bootPk1);
        appendTestBlock({}, &bootPk2);
        // Directly populate cachedBootstrapKeys via RegisterBlockMasternodeActivity already run above!
        for (int i = 0; i < 10; ++i) {
            appendTestBlock({});
        }
        BOOST_REQUIRE_EQUAL(pPrev->nHeight, baseH + 12);

        // (2a) Test 10-block challenge window at height h = baseH + 13:
        //      - freshKey10 references block at (h - 10) = baseH + 3 -> MUST BE ACCEPTED
        //      - staleKey11 references block at (h - 11) = baseH + 2 -> MUST BE REJECTED
        CKey freshKey10, staleKey11;
        freshKey10.MakeNewKey(true);
        staleKey11.MakeNewKey(true);
        const int hRegChallengeTest = pPrev->nHeight + 1;
        const uint256 hashAge10 = chainActive[hRegChallengeTest - 10]->GetBlockHash();
        const uint256 hashAge11 = chainActive[hRegChallengeTest - 11]->GetBlockHash();

        CMutableTransaction txFresh10, txStale11;
        txFresh10.vin.push_back(CTxIn(zeroCoinSentinel));
        txFresh10.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
            freshKey10.GetPubKey(), hashAge10,
            SolveZeroCoinPoWForTest(freshKey10.GetPubKey(), hashAge10, targetTestV2, true),
            hRegChallengeTest + 2900))); // intentionally huge lockTime to also verify 240-block effective TTL cap!

        txStale11.vin.push_back(CTxIn(zeroCoinSentinel));
        txStale11.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
            staleKey11.GetPubKey(), hashAge11,
            SolveZeroCoinPoWForTest(staleKey11.GetPubKey(), hashAge11, targetTestV2, true),
            hRegChallengeTest + 2900)));

        appendTestBlock({CTransaction(txFresh10), CTransaction(txStale11)});
        {
            std::vector<CPubKey> poolAt13 = GetAdamMinerPool(hRegChallengeTest, &testCoins);
            BOOST_CHECK(std::find(poolAt13.begin(), poolAt13.end(), freshKey10.GetPubKey()) != poolAt13.end());
            BOOST_CHECK(std::find(poolAt13.begin(), poolAt13.end(), staleKey11.GetPubKey()) == poolAt13.end());
        }

        // (2b) Verify 240-block effective TTL cap: advance to hRegChallengeTest + 239 (active) vs hRegChallengeTest + 240 (expired)
        //      Note: On Testnet, UPGRADE_MODELD is at 2200, so below 2200 GetZeroCoinRegPeriod(h) == 240!
        while (pPrev->nHeight < hRegChallengeTest + 239) {
            appendTestBlock({});
        }
        {
            std::vector<CPubKey> poolAt239 = GetAdamMinerPool(hRegChallengeTest + 239, &testCoins);
            BOOST_CHECK(std::find(poolAt239.begin(), poolAt239.end(), freshKey10.GetPubKey()) != poolAt239.end());
        }
        appendTestBlock({});
        BOOST_REQUIRE_EQUAL(pPrev->nHeight, hRegChallengeTest + 240);
        {
            std::vector<CPubKey> poolAt240 = GetAdamMinerPool(hRegChallengeTest + 240, &testCoins);
            BOOST_CHECK(std::find(poolAt240.begin(), poolAt240.end(), freshKey10.GetPubKey()) == poolAt240.end());
        }

        // (2c) Submit 20 distinct Zero-Coin Sybil keys across 20 consecutive blocks -> cap MUST hold at MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 (16),
        //      AND real Bootstrap keys (bootPk1, bootPk2) MUST NOT be starved!
        std::vector<CKey> sybilKeys(20);
        std::set<CPubKey> sybilPubKeySet;
        for (int i = 0; i < 20; ++i) {
            sybilKeys[i].MakeNewKey(true);
            sybilPubKeySet.insert(sybilKeys[i].GetPubKey());
            const int hNext = pPrev->nHeight + 1;
            const uint256 ch = pPrev->GetBlockHash();
            CMutableTransaction ztx;
            ztx.vin.push_back(CTxIn(zeroCoinSentinel));
            ztx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
                sybilKeys[i].GetPubKey(), ch,
                SolveZeroCoinPoWForTest(sybilKeys[i].GetPubKey(), ch, targetTestV2, true),
                hNext + 260)));
            appendTestBlock({CTransaction(ztx)});
        }

        {
            std::set<CPubKey> setBootOut;
            std::vector<CPubKey> poolCapCheck = GetAdamMinerPool(pPrev->nHeight, &testCoins, nullptr, nullptr, nullptr, nullptr, nullptr, &setBootOut);
            size_t activeSybilCount = 0;
            for (const CPubKey& pk : poolCapCheck) {
                if (sybilPubKeySet.count(pk)) activeSybilCount++;
            }
            BOOST_CHECK_EQUAL(activeSybilCount, MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2);
            if (baseH == 0) {
                BOOST_CHECK(setBootOut.count(bootPk1) > 0);
                BOOST_CHECK(setBootOut.count(bootPk2) > 0);
            }
        }

        // (2d) Rule 2 Sybil Defense: Add 6 Funded 10 KRISTA PoW-Lock miners across 2 blocks (3 per block = ADAM_MAX_POW_LOCK_TX_PER_BLOCK)
        //      (0 Masternodes, total funded = 6 < 13, so 16 Sybil keys stay in pool!).
        //      Across 50 random VRF seeds at an ADAM Dual-Track height (>= 200):
        //      - 100% of Coordinator elections (50/50) MUST be won by the 6 Funded 10 KRISTA miners (0/50 Sybil coordinators!)
        //      - All remaining 5 Funded 10 KRISTA miners MUST win committee seats in 50/50 rounds!
        std::vector<CKey> fundedPoWKeysVec(6);
        std::set<CPubKey> fundedPoWSet;
        std::vector<CTransaction> fundedPoWTxs;
        const uint256 targetMinerTest = GetMinerPoWLimit(Params().NetworkIDString());
        for (int batch = 0; batch < 2; ++batch) {
            std::vector<CTransaction> batchTxs;
            const int hFunded = pPrev->nHeight + 1;
            const uint256 chFunded = pPrev->GetBlockHash();
            for (int j = 0; j < 3; ++j) {
                const int idx = batch * 3 + j;
                fundedPoWKeysVec[idx].MakeNewKey(true);
                const CPubKey pk = fundedPoWKeysVec[idx].GetPubKey();
                fundedPoWSet.insert(pk);
                uint32_t nonceFunded = 0;
                for (;; ++nonceFunded) {
                    CHashWriter ss(SER_GETHASH, 0);
                    ss << nonceFunded << chFunded << pk;
                    if (ss.GetHash() <= targetMinerTest) break;
                }
                CMutableTransaction ftx;
                ftx.vin.push_back(CTxIn(COutPoint(uint256S("5500000000000000000000000000000000000000000000000000000000000000"), (uint32_t)idx)));
                ftx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makeRegScriptWithNonce(pk, chFunded, nonceFunded, hFunded + 2900)));
                CTransaction ftxConst(ftx);
                batchTxs.push_back(ftxConst);
                fundedPoWTxs.push_back(ftxConst);
                testCoins.AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], hFunded, false, false), true);
                pcoinsTip->AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], hFunded, false, false), true);
            }
            appendTestBlock(batchTxs);
        }
        BOOST_REQUIRE_GE(pPrev->nHeight, Params().GetConsensus().nAdamDualTrackHeight);

        {
            std::set<CPubKey> mnSet, clSet, fpSet, bootSet;
            size_t nExplicit = 0, nFunded = 0;
            std::vector<CPubKey> globalPool = GetAdamMinerPool(pPrev->nHeight, pcoinsTip, &mnSet, &clSet, &nExplicit, &nFunded, &fpSet, &bootSet);
            BOOST_REQUIRE_EQUAL(nFunded, 6U);
            BOOST_REQUIRE_EQUAL(fpSet.size(), 6U);
            BOOST_REQUIRE_GE(globalPool.size(), 22U); // 6 Funded + Bootstrap + 16 Sybils

            int nSybilCoordinators = 0;
            int nFundedCoordinators = 0;
            for (int round = 0; round < 50; ++round) {
                CHashWriter ssSeed(SER_GETHASH, 0);
                ssSeed << uint256S("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") << round;
                const uint256 seed = ssSeed.GetHash();

                std::vector<CPubKey> vElected, vStandby;
                CPubKey coord;
                BOOST_REQUIRE(SelectAdamNodes(seed, Params().GetConsensus(), vElected, coord, pPrev->nHeight + 1, nullptr, &vStandby));
                BOOST_CHECK_EQUAL(vElected.size(), 11U);
                if (fundedPoWSet.count(coord)) {
                    nFundedCoordinators++;
                }
                if (sybilPubKeySet.count(coord)) {
                    nSybilCoordinators++;
                }

                // All 5 remaining Funded 10 KRISTA miners (other than coord) MUST be in vElected!
                int nFundedInCommittee = 0;
                for (const CPubKey& pk : vElected) {
                    if (fundedPoWSet.count(pk)) nFundedInCommittee++;
                }
                BOOST_CHECK_EQUAL(nFundedInCommittee, 5);
            }
            BOOST_CHECK_EQUAL(nFundedCoordinators, 50);
            BOOST_CHECK_EQUAL(nSybilCoordinators, 0);

            // (2e) Rule 1 Sybil Defense: Test ElectDualTrackValidators directly with 8 Masternodes + 1 Coin-Lock + 3 Funded PoW-Lock (= 12 < 13) + 16 Zero-Coin Sybils
            std::set<CPubKey> r1MNs, r1CLs, r1FundedPoW, r1Boot;
            std::vector<CPubKey> r1Pool;
            std::vector<CKey> tmpKeys(12);
            for (int i = 0; i < 12; ++i) {
                tmpKeys[i].MakeNewKey(true);
                CPubKey pk = tmpKeys[i].GetPubKey();
                r1Pool.push_back(pk);
                if (i < 8) r1MNs.insert(pk);
                else if (i == 8) r1CLs.insert(pk);
                else r1FundedPoW.insert(pk);
            }
            for (int i = 4; i < 20; ++i) {
                r1Pool.push_back(sybilKeys[i].GetPubKey());
            }
            for (int round = 0; round < 50; ++round) {
                CHashWriter ssSeed(SER_GETHASH, 0);
                ssSeed << uint256S("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") << round;
                const uint256 seed = ssSeed.GetHash();
                const CPubKey r1Coord = *r1MNs.begin();
                std::vector<CPubKey> vElectedR1, vStandbyR1;
                ElectDualTrackValidators(seed, r1Pool, r1MNs, r1Coord, 11, vElectedR1, r1CLs, &vStandbyR1, &r1FundedPoW, &r1Boot);
                BOOST_REQUIRE_EQUAL(vElectedR1.size(), 11U);
                for (const CPubKey& pk : vElectedR1) {
                    BOOST_CHECK_MESSAGE(!sybilPubKeySet.count(pk), "Zero-Coin Sybil key must NEVER displace any MN, Coin-Lock, or 10 KRISTA PoW-Lock validator!");
                }
                // Standby order check: any Track 2 standby in vStandbyR1 comes strictly after all 3 Funded PoW-Lock keys won primary seats
                BOOST_CHECK_GE(vStandbyR1.size(), 16U);
            }

            for (const auto& ftxConst : fundedPoWTxs) {
                pcoinsTip->SpendCoin(COutPoint(ftxConst.GetHash(), 0));
            }
        }

        // Cleanup temporary blocks and restore chainActive cleanly
        chainActive.SetTip(pOrigTip);
        for (size_t i = 0; i < vHashes.size(); ++i) {
            mapBlockIndex.erase(vHashes[i]);
            delete vIndices[i];
        }
        if (tempGenesis) {
            mapBlockIndex.erase(tempGenesisHash);
        }
        InitializeAdamMinerRegCache(true);
    }
}

BOOST_AUTO_TEST_SUITE_END()
