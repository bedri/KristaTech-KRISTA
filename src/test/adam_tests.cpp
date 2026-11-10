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
    ScopedChainParams mainParams(CBaseChainParams::MAIN);

    CBlockHeader hdr;
    hdr.nVersion = 11;
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, 500));

    hdr.vAdamVRFProof.assign(96, 0x11);
    hdr.vAdamCoordinatorSig.assign(96, 0x22);
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, 500));

    hdr.vAdamVRFProof.assign(513, 0x11);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, 500));
    hdr.vAdamVRFProof.assign(96, 0x11);

    // Block 2000..2199 on Mainnet: V12 (UPGRADE_POMBL active), Model D NOT yet active -> empty vQuorumSig is valid
    hdr.nVersion = 12;
    hdr.vQuorumSig.clear();
    BOOST_CHECK(!IsModelDActive(2050));
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, 2050));

    hdr.vQuorumSig.assign(4097, 0x33);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, 2050));

    // Block 2200+ on Mainnet: Model D active -> non-empty vQuorumSig <= 4096 required
    hdr.vQuorumSig.clear();
    BOOST_CHECK(IsModelDActive(2250));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, 2250));

    hdr.vQuorumSig.assign(256, 0x44);
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, 2250));

    hdr.vQuorumSig.assign(4097, 0x44);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, 2250));
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
    uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString());
    uint32_t nonce = 0;
    while (true) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << nonce;
        ss << challengeHash;
        ss << pubkey;
        if (ss.GetHash() <= zeroCoinTarget)
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
}

BOOST_AUTO_TEST_SUITE_END()
