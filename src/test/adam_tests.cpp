// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "evo/deterministicmns.h"
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
#include "llmq_messages.h"
#include "main.h"
#include "masternode-payments.h"
#include "miner.h"
#include "net.h"
#include "net_processing.h"
#include "netmessagemaker.h"
#include "primitives/block.h"
#include "wallet/wallet.h"
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
uint32_t SolveZeroCoinPoWForTest(const CPubKey& pk, const uint256& challengeHash, const uint256& target, bool fHardeningV2 = true, const CBLSPubKey* pBlsPubKey = nullptr)
{
    const unsigned int nWorkers = std::max(1U, std::min(16U, std::thread::hardware_concurrency()));
    if (!fHardeningV2 || nWorkers == 1) {
        for (uint32_t n = 0;; ++n) {
            if (ComputeZeroCoinRegistrationHash(n, challengeHash, pk, fHardeningV2, pBlsPubKey) <= target) {
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
                if (ComputeZeroCoinRegistrationHash(n, challengeHash, pk, true, pBlsPubKey) <= target) {
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
uint32_t SolveMinerPoWForTest(const CPubKey& pk, const uint256& challengeHash, const uint256& target, bool fHardeningV2 = true, const CBLSPubKey* pBlsPubKey = nullptr)
{
    const unsigned int nWorkers = std::max(1U, std::min(16U, std::thread::hardware_concurrency()));
    if (!fHardeningV2 || nWorkers == 1) {
        for (uint32_t n = 0;; ++n) {
            if (ComputeMinerPoWRegistrationHash(n, challengeHash, pk, fHardeningV2, pBlsPubKey) <= target) {
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
                if (ComputeMinerPoWRegistrationHash(n, challengeHash, pk, true, pBlsPubKey) <= target) {
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
CBLSPubKey DeriveTestBlsPubKey(const CKey& key)
{
    return CBLSPubKey(DeriveBLSFromCKey(key));
}
CScript PrependTestV4Bls(const CKey& key, const CScript& innerScript)
{
    CBLSSecretKey blsSk = DeriveBLSFromCKey(key);
    CBLSPubKey blsPk(blsSk);
    CBLSSignature blsPoP;
    BOOST_REQUIRE(blsPoP.SignPoP(blsSk));
    CScript out;
    out << std::vector<unsigned char>(blsPk.begin(), blsPk.end())
        << std::vector<unsigned char>(blsPoP.begin(), blsPoP.end())
        << OP_2DROP;
    out.insert(out.end(), innerScript.begin(), innerScript.end());
    return out;
}
CScript BuildTestCoinLockScript(const CKey& key, int64_t lockHeight, bool fWithV4Bls = true)
{
    const CPubKey pk = key.GetPubKey();
    CScript inner = CScript() << std::vector<unsigned char>(pk.begin(), pk.end())
                              << OP_DROP
                              << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                              << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    return fWithV4Bls ? PrependTestV4Bls(key, inner) : inner;
}
CScript BuildTestPoWLockScript(const CKey& key, const uint256& chHash, uint32_t nNonce, int64_t lockHeight, bool fWithV4Bls = true)
{
    const CPubKey pk = key.GetPubKey();
    std::vector<unsigned char> vchNonce = {
        static_cast<unsigned char>(nNonce & 0xff),
        static_cast<unsigned char>((nNonce >> 8) & 0xff),
        static_cast<unsigned char>((nNonce >> 16) & 0xff),
        static_cast<unsigned char>((nNonce >> 24) & 0xff)
    };
    CScript inner = CScript() << vchNonce
                              << std::vector<unsigned char>(chHash.begin(), chHash.end())
                              << std::vector<unsigned char>(pk.begin(), pk.end())
                              << OP_DROP << OP_DROP << OP_DROP
                              << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                              << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    return fWithV4Bls ? PrependTestV4Bls(key, inner) : inner;
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

    // Test UPGRADE_FAIRNESS GetZeroCoinPoWLimit for Mainnet (15 zero bits, matching Funded PoW-Lock registration work)
    uint256 mainZeroLimitFairness = GetZeroCoinPoWLimit("main", true, true);
    BOOST_CHECK_EQUAL(mainZeroLimitFairness.GetHex(), uint256S("0001ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetMinerPoWLimit for Mainnet, Testnet, Regtest (legacy SHA256d and UPGRADE_HARDENING_V2 HashX11KVS)
    uint256 mainMinerLimit = GetMinerPoWLimit("main", false);
    uint256 testMinerLimit = GetMinerPoWLimit("test", false);
    uint256 regtestMinerLimit = GetMinerPoWLimit("regtest", false);
    BOOST_CHECK_EQUAL(mainMinerLimit.GetHex(), uint256S("0000000fffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(testMinerLimit.GetHex(), uint256S("00000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(regtestMinerLimit.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    uint256 mainMinerLimitV2 = GetMinerPoWLimit("main", true);
    uint256 testMinerLimitV2 = GetMinerPoWLimit("test", true);
    uint256 regtestMinerLimitV2 = GetMinerPoWLimit("regtest", true);
    BOOST_CHECK_EQUAL(mainMinerLimitV2.GetHex(), uint256S("0001ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(testMinerLimitV2.GetHex(), uint256S("0007ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());
    BOOST_CHECK_EQUAL(regtestMinerLimitV2.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Verify GetZeroCoinPoWLimit is a larger target (fewer leading zero bits) than GetMinerPoWLimit on Mainnet & Testnet
    BOOST_CHECK(mainZeroLimit > mainMinerLimit);
    BOOST_CHECK(mainZeroLimitV2 > mainMinerLimitV2);
    BOOST_CHECK(testZeroLimitV2 > testMinerLimitV2);

    // Domain separation: KRPL/POWLKv2 (Funded PoW-Lock) vs KRZC/SYBILV2 (Zero-Coin PoW) must never collide
    {
        CKey domKey;
        domKey.MakeNewKey(true);
        const uint256 domChal = uint256S("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        BOOST_CHECK(ComputeMinerPoWRegistrationHash(42, domChal, domKey.GetPubKey(), true) !=
                    ComputeZeroCoinRegistrationHash(42, domChal, domKey.GetPubKey(), true));
    }
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

    const bool fV3 = IsHardeningV3Active(chainActive.Tip() ? (chainActive.Height() + 1) : 0);
    std::vector<unsigned char> vchProof;
    BOOST_CHECK(SignBLSWithECDSAFallback(prevSeed, coordKey, blsKey, vchProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3)));
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

    BOOST_CHECK(SignBLSWithECDSAFallback(header.GetHash(), coordKey, blsKey, header.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3)));
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
    BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHashV11, electedMinerKey, blsMinerKey, vchSigV11, GetBLSDomainTag(BLSDomain::ADAM_SOL, IsHardeningV3Active(500))));

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
    BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHashV12, electedMinerKey, blsMinerKey, vchSigV12, GetBLSDomainTag(BLSDomain::ADAM_SOL, IsHardeningV3Active(500))));

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
    const bool fV3Pre = IsHardeningV3Active(pindexPreModelD->nHeight + 1);
    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedPreModelD, outsiderKey, outsiderBls, hdr.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3Pre)));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), outsiderKey, outsiderBls, hdr.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3Pre)));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));

    // 3. Valid registered pool key passes on V11 and pre-Model-D V12; tampering or oversize fails
    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedPreModelD, poolCoordKey, poolCoordBls, hdr.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3Pre)));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), poolCoordKey, poolCoordBls, hdr.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3Pre)));
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, nullptr));
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.nNonce += 1;
    BOOST_CHECK(!IsFutureBlockPreVerified(hdr, pindexPreModelD));
    hdr.nNonce -= 1;
    BOOST_CHECK(IsFutureBlockPreVerified(hdr, pindexPreModelD));

    hdr.nVersion = 12;
    hdr.vQuorumSig.clear();
    BOOST_CHECK(!IsModelDActive(pindexPreModelD->nHeight + 1));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), poolCoordKey, poolCoordBls, hdr.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3Pre)));
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
    const bool fV3ModelD = IsHardeningV3Active(nModelDCandidateHeight);
    BOOST_REQUIRE(IsModelDActive(nModelDCandidateHeight));

    BOOST_REQUIRE(SignBLSWithECDSAFallback(seedModelDParent, poolCoordKey, poolCoordBls, hdrModelD.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3ModelD)));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdrModelD.GetHash(), poolCoordKey, poolCoordBls, hdrModelD.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3ModelD)));

    // Empty or dummy vQuorumSig must fail when Model D is active
    BOOST_CHECK(!IsFutureBlockPreVerified(hdrModelD, pindexModelD));
    hdrModelD.vQuorumSig.assign(256, 0x44);
    BOOST_CHECK(!IsFutureBlockPreVerified(hdrModelD, pindexModelD));

    const llmq::CQuorum quorum = llmq::GetActiveQuorum(nModelDCandidateHeight, pindexModelD);
    const size_t nQuorumThreshold = llmq::GetQuorumThreshold(quorum.members.size(), nModelDCandidateHeight);
    BOOST_REQUIRE_GE(quorum.members.size(), nQuorumThreshold);

    llmq::CQuorumSignature qsig;
    qsig.blockHash = hdrModelD.GetHash();
    for (size_t i = 0; i < nQuorumThreshold; ++i) {
        CKey memberKey = findPoolKey(quorum.members[i].pubKeyMasternode);
        BOOST_REQUIRE(memberKey.IsValid());
        CBLSSecretKey memberBls = DeriveBLSFromCKey(memberKey);
        std::vector<unsigned char> memberSig;
        BOOST_REQUIRE(SignBLSWithECDSAFallback(qsig.blockHash, memberKey, memberBls, memberSig, GetBLSDomainTag(BLSDomain::LLMQ_BLOCK, fV3ModelD)));
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
        for (int i = 0; i < 20; ++i) {
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
        BOOST_REQUIRE(SignBLSWithECDSAFallback(pHash, k, bls, sig, GetBLSDomainTag(BLSDomain::ADAM_SOL, IsHardeningV3Active(nHeight))));
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
    const bool fV3K2 = IsHardeningV3Active(nHeight);
    {
        CBlock blkPrimary = baseBlock;
        blkPrimary.nTime = pindexParent->nTime + 30;
        CKey coordKey = findDetKey(expectedCoordinator);
        BOOST_REQUIRE(coordKey.IsValid());
        CBLSSecretKey coordBls = DeriveBLSFromCKey(coordKey);
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, coordKey, coordBls, blkPrimary.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3K2)));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkPrimary.GetHash(), coordKey, coordBls, blkPrimary.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3K2)));
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
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, rogueKey, rogueBls, blkFrontRun.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3K2)));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkFrontRun.GetHash(), rogueKey, rogueBls, blkFrontRun.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3K2)));
        CValidationState st;
        BOOST_CHECK(!CheckBlock(blkFrontRun, st, false, true, true));
    }

    // 2) At t = +45s (>= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS), PoBLS XOR-ranked fallback coordinator MUST succeed!
    {
        CBlock blkFallback = baseBlock;
        blkFallback.nTime = pindexParent->nTime + 45;
        CPubKey activeFallback, prevFallback;
        int rotIdx = 0;
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(adamSeed, nHeight, 45, vExpectedMiners, mapSols, activeFallback, prevFallback, rotIdx, pindexParent));
        CKey fbKey = findDetKey(activeFallback);
        BOOST_REQUIRE(fbKey.IsValid());
        CBLSSecretKey fbBls = DeriveBLSFromCKey(fbKey);
        BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, fbKey, fbBls, blkFallback.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3K2)));
        BOOST_REQUIRE(SignBLSWithECDSAFallback(blkFallback.GetHash(), fbKey, fbBls, blkFallback.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3K2)));
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
    CBLSPubKey blsPk = DeriveTestBlsPubKey(key);
    uint256 challengeHash = uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef");
    uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);
    uint32_t nonce = SolveZeroCoinPoWForTest(pubkey, challengeHash, zeroCoinTarget, true, &blsPk);
    CScript regScript = BuildTestPoWLockScript(key, challengeHash, nonce, 3500);

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
    const bool fV3K4 = IsHardeningV3Active(chainActive.Tip() ? (chainActive.Height() + 1) : 0);
    CBlock coordSigned = draft;
    BOOST_REQUIRE(SignBLSWithECDSAFallback(adamSeed, coordKey, coordBls, coordSigned.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3K4)));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(coordSigned.GetHash(), coordKey, coordBls, coordSigned.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3K4)));

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
                                           coordKey, coordBls, forgedCoordSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3K4)));
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
            const int threshold = (int)llmq::GetQuorumThreshold(qSize, quorum.nHeight);
            llmq::CQuorumSignature qsig;
            qsig.blockHash = blockHash;
            for (int i = 0; i < threshold - 1; ++i) {
                CBLSSecretKey blsSk = DeriveBLSFromCKey(mnKeys[i]);
                std::vector<unsigned char> vchSig;
                BOOST_REQUIRE(SignBLSWithECDSAFallback(blockHash, mnKeys[i], blsSk, vchSig, GetBLSDomainTag(BLSDomain::LLMQ_BLOCK, IsHardeningV3Active(quorum.nHeight))));
                qsig.signatures.emplace_back(quorum.members[i].collateralOutpoint, vchSig);
            }
            BOOST_CHECK(!qsig.Verify(quorum)); // threshold - 1 must fail

            CBLSSecretKey blsThresh = DeriveBLSFromCKey(mnKeys[threshold - 1]);
            std::vector<unsigned char> vchThreshSig;
            BOOST_REQUIRE(SignBLSWithECDSAFallback(blockHash, mnKeys[threshold - 1], blsThresh, vchThreshSig, GetBLSDomainTag(BLSDomain::LLMQ_BLOCK, IsHardeningV3Active(quorum.nHeight))));
            qsig.signatures.emplace_back(quorum.members[threshold - 1].collateralOutpoint, vchThreshSig);
            BOOST_CHECK(qsig.Verify(quorum)); // exact V4 threshold (4/5, 7/9, 8/11, 11/15) must pass!
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

        std::vector<uint256> vAddedHashes(31);
        std::vector<CBlockIndex*> vAddedIndices(31, nullptr);
        CBlockIndex* pPrev = pBaseTip;

        std::vector<CKey> vZeroCoinKeys(16);
        for (int i = 0; i < 16; ++i) {
            vZeroCoinKeys[i].MakeNewKey(true);
        }
        CKey badUnixLockKey;
        badUnixLockKey.MakeNewKey(true);

        static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
        const uint256 zeroCoinTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);

        auto makePoWRegScript = [&](const CKey& k, const uint256& chHash, int64_t lockHeight) -> CScript {
            const CBLSPubKey blsPk = DeriveTestBlsPubKey(k);
            uint32_t nNonce = SolveZeroCoinPoWForTest(k.GetPubKey(), chHash, zeroCoinTarget, true, &blsPk);
            return BuildTestPoWLockScript(k, chHash, nNonce, lockHeight);
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
                ztx.vout.push_back(CTxOut(0, makePoWRegScript(vZeroCoinKeys[i], freshChallenge, h + 260)));
                blk.vtx.push_back(ztx);
            } else if (i == 16) {
                // Attempt LOCKTIME_THRESHOLD Unix timestamp bypass (500,000,100)
                CMutableTransaction badTx;
                badTx.vin.push_back(CTxIn(zeroCoinSentinel));
                badTx.vout.push_back(CTxOut(0, makePoWRegScript(badUnixLockKey, freshChallenge, LOCKTIME_THRESHOLD + 100)));
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

        // Now add 13 funded Coin-Lock validators (250 KRISTA each, 1 per block) and verify Zero-Coin PoW keys are cleanly phased out!
        std::vector<CKey> vFundedKeys(13);
        for (int i = 0; i < 13; ++i) {
            const int hCl = pPrev->nHeight + 1;
            CBlock fundedBlk;
            fundedBlk.nVersion = 12;
            fundedBlk.hashPrevBlock = pPrev->GetBlockHash();
            fundedBlk.nTime = pPrev->nTime + 30;
            fundedBlk.nNonce = (uint32_t)(19 + i);
            CMutableTransaction cb2;
            cb2.vin.push_back(CTxIn(COutPoint(), CScript() << hCl << OP_0));
            cb2.vout.push_back(CTxOut(0, CScript()));
            fundedBlk.vtx.push_back(cb2);

            vFundedKeys[i].MakeNewKey(true);
            CScript clScript = BuildTestCoinLockScript(vFundedKeys[i], hCl + 2900);
            CMutableTransaction ftx;
            ftx.vin.push_back(CTxIn(COutPoint(uint256S("4400000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i)));
            ftx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, clScript));
            CTransaction ftxConst(ftx);
            fundedBlk.vtx.push_back(ftxConst);
            testCoins.AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], hCl, false, false), true);

            fundedBlk.hashMerkleRoot = BlockMerkleRoot(fundedBlk);
            vAddedHashes[18 + i] = fundedBlk.GetHash();
            CBlockIndex* pIdxCl = new CBlockIndex(fundedBlk);
            pIdxCl->nHeight = hCl;
            pIdxCl->pprev = pPrev;
            auto insCl = mapBlockIndex.insert(std::make_pair(vAddedHashes[18 + i], pIdxCl));
            pIdxCl->phashBlock = &(insCl.first->first);
            pIdxCl->BuildSkip();
            vAddedIndices[18 + i] = pIdxCl;
            chainActive.SetTip(pIdxCl);

            RegisterBlockMasternodeActivity(fundedBlk, hCl, &testCoins);
            pPrev = pIdxCl;
        }

        size_t nFundedAfter = 0;
        std::vector<CPubKey> poolAfterFunded = GetAdamMinerPool(pPrev->nHeight, &testCoins, nullptr, nullptr, nullptr, &nFundedAfter);
        BOOST_CHECK_EQUAL(nFundedAfter, 13U);
        for (int i = 0; i < 13; ++i) {
            BOOST_CHECK(std::find(poolAfterFunded.begin(), poolAfterFunded.end(), vFundedKeys[i].GetPubKey()) != poolAfterFunded.end());
        }
        // Under permanent Open-Entry (nAdamOpenEntryHeight = 1 on Regtest), newest 16 Zero-Coin keys
        // remain admitted even when 13+ funded validators are active (competing for the 1 Open-Entry seat).
        for (int i = 0; i < 16; ++i) {
            BOOST_CHECK(std::find(poolAfterFunded.begin(), poolAfterFunded.end(), vZeroCoinKeys[i].GetPubKey()) != poolAfterFunded.end());
        }

        // Restore chainActive and mapBlockIndex cleanly
        chainActive.SetTip(pOrigTip);
        for (int i = 0; i < 31; ++i) {
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

    auto makeRegScriptWithNonce = [](const CKey& k, const uint256& chHash, uint32_t nNonce, int64_t lockHeight) -> CScript {
        return BuildTestPoWLockScript(k, chHash, nNonce, lockHeight);
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

        // (a) Valid compressed key + HashX11KVS nonce (bound to V4 BLS) passes CheckInputs at nSpendHeight = 10
        CKey validKey;
        validKey.MakeNewKey(true);
        const CPubKey validPk = validKey.GetPubKey();
        const CBLSPubKey validBlsPk = DeriveTestBlsPubKey(validKey);
        const uint32_t validNonce = SolveZeroCoinPoWForTest(validPk, chHash, targetV2, true, &validBlsPk);
        CMutableTransaction validMtx;
        validMtx.vin.push_back(CTxIn(zeroCoinSentinel));
        validMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(validKey, chHash, validNonce, 300)));
        CTransaction validTx(validMtx);
        PrecomputedTransactionData precomValid(validTx);
        CValidationState stValid;
        {
            LOCK(cs_main);
            BOOST_CHECK(CheckInputs(validTx, stValid, coinsView, false, 0, false, precomValid));
        }

        // (b) Legacy SHA256d nonce (where V4 HashX11KVS > targetV2) MUST be rejected at nSpendHeight = 10
        uint32_t shaOnlyNonce = 0;
        for (;; ++shaOnlyNonce) {
            if (ComputeZeroCoinRegistrationHash(shaOnlyNonce, chHash, validPk, false) <= targetV2 &&
                ComputeZeroCoinRegistrationHash(shaOnlyNonce, chHash, validPk, true, &validBlsPk) > targetV2) {
                break;
            }
        }
        CMutableTransaction legacyShaMtx;
        legacyShaMtx.vin.push_back(CTxIn(zeroCoinSentinel));
        legacyShaMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(validKey, chHash, shaOnlyNonce, 300)));
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
        uncompMtx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(uncompKey, chHash, 0, 300)));
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

        const CBLSPubKey freshBls10 = DeriveTestBlsPubKey(freshKey10);
        const CBLSPubKey staleBls11 = DeriveTestBlsPubKey(staleKey11);
        CMutableTransaction txFresh10, txStale11;
        txFresh10.vin.push_back(CTxIn(zeroCoinSentinel));
        txFresh10.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
            freshKey10, hashAge10,
            SolveZeroCoinPoWForTest(freshKey10.GetPubKey(), hashAge10, targetTestV2, true, &freshBls10),
            hRegChallengeTest + 2900))); // intentionally huge lockTime to also verify 240-block effective TTL cap!

        txStale11.vin.push_back(CTxIn(zeroCoinSentinel));
        txStale11.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
            staleKey11, hashAge11,
            SolveZeroCoinPoWForTest(staleKey11.GetPubKey(), hashAge11, targetTestV2, true, &staleBls11),
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

        // (2c) Submit 20 distinct Zero-Coin Sybil keys across 20 consecutive blocks -> under UPGRADE_FAIRNESS,
        //      cap is MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS (240), so all 20 distinct Zero-Coin miners stay active
        //      AND real Bootstrap keys (bootPk1, bootPk2) MUST NOT be starved!
        std::vector<CKey> sybilKeys(20);
        std::set<CPubKey> sybilPubKeySet;
        for (int i = 0; i < 20; ++i) {
            sybilKeys[i].MakeNewKey(true);
            sybilPubKeySet.insert(sybilKeys[i].GetPubKey());
            const int hNext = pPrev->nHeight + 1;
            const uint256 ch = pPrev->GetBlockHash();
            const CBLSPubKey sybilBls = DeriveTestBlsPubKey(sybilKeys[i]);
            CMutableTransaction ztx;
            ztx.vin.push_back(CTxIn(zeroCoinSentinel));
            ztx.vout.push_back(CTxOut(0, makeRegScriptWithNonce(
                sybilKeys[i], ch,
                SolveZeroCoinPoWForTest(sybilKeys[i].GetPubKey(), ch, targetTestV2, true, &sybilBls),
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
            BOOST_CHECK_EQUAL(activeSybilCount, 20U);
            BOOST_CHECK_LE(activeSybilCount, MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS);
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
        const uint256 targetMinerTest = GetMinerPoWLimit(Params().NetworkIDString(), true);
        for (int idx = 0; idx < 6; ++idx) {
            const int hFunded = pPrev->nHeight + 1;
            const uint256 chFunded = pPrev->GetBlockHash();
            fundedPoWKeysVec[idx].MakeNewKey(true);
            const CPubKey pk = fundedPoWKeysVec[idx].GetPubKey();
            fundedPoWSet.insert(pk);
            const CBLSPubKey blsFunded = DeriveTestBlsPubKey(fundedPoWKeysVec[idx]);
            const uint32_t nonceFunded = SolveMinerPoWForTest(pk, chFunded, targetMinerTest, true, &blsFunded);
            CMutableTransaction ftx;
            ftx.vin.push_back(CTxIn(COutPoint(uint256S("5500000000000000000000000000000000000000000000000000000000000000"), (uint32_t)idx)));
            ftx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makeRegScriptWithNonce(fundedPoWKeysVec[idx], chFunded, nonceFunded, hFunded + 2900)));
            CTransaction ftxConst(ftx);
            fundedPoWTxs.push_back(ftxConst);
            testCoins.AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], hFunded, false, false), true);
            pcoinsTip->AddCoin(COutPoint(ftxConst.GetHash(), 0), Coin(ftxConst.vout[0], hFunded, false, false), true);
            appendTestBlock({ftxConst});
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

BOOST_AUTO_TEST_CASE(adam_fairness_overflow_and_weighted_coordinator_test)
{
    // 1. Strict 1-seat Coin-Lock quota isolation & Open PoW spillover priority (Active MN -> Open PoW -> Coin-Lock last-resort fallback)
    {
        ScopedChainParams regParams(CBaseChainParams::REGTEST);
        std::set<CPubKey> mnSet, clSet, fpSet, bootSet, sybilSet;
        std::vector<CPubKey> pool;
        std::vector<CKey> keys(39);
        for (size_t i = 0; i < keys.size(); ++i) {
            keys[i].MakeNewKey(true);
            const CPubKey pk = keys[i].GetPubKey();
            pool.push_back(pk);
            if (i < 8) mnSet.insert(pk);
            else if (i < 28) clSet.insert(pk);      // 20 Coin-Locks (250 KRISTA)
            else if (i < 31) fpSet.insert(pk);      // 3 Funded PoW-Locks (10 KRISTA)
            else sybilSet.insert(pk);               // 8 Zero-Coin Sybils (0 KRISTA)
        }
        const CPubKey coord = *mnSet.begin(); // 7 MNs remain for Sub-track 1A

        for (int round = 0; round < 400; ++round) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc") << round;
            const uint256 seed = ssSeed.GetHash();

            std::vector<CPubKey> vElected, vStandby;
            ElectDualTrackValidators(seed, pool, mnSet, coord, 11, vElected, clSet, &vStandby, &fpSet, &bootSet);
            BOOST_REQUIRE_EQUAL(vElected.size(), 11U);

            std::set<CPubKey> electedUnique(vElected.begin(), vElected.end());
            BOOST_CHECK_EQUAL(electedUnique.size(), 11U);

            std::set<CPubKey> standbyUnique;
            for (const CPubKey& spk : vStandby) {
                BOOST_CHECK(!electedUnique.count(spk));
                BOOST_CHECK(standbyUnique.insert(spk).second);
            }
            BOOST_CHECK_EQUAL(electedUnique.size() + standbyUnique.size(), 38U);

            int nMnSeats = 0, nClSeats = 0, nFpSeats = 0, nSybilSeats = 0;
            for (const CPubKey& pk : vElected) {
                if (mnSet.count(pk)) nMnSeats++;
                else if (clSet.count(pk)) nClSeats++;
                else if (fpSet.count(pk)) nFpSeats++;
                else if (sybilSet.count(pk)) nSybilSeats++;
            }
            // Coin-Lock MUST stay strictly isolated to its 1 dedicated seat; all 3 Funded PoW-Locks keep Track 2!
            BOOST_CHECK_EQUAL(nMnSeats, 7);
            BOOST_CHECK_EQUAL(nClSeats, 1);
            BOOST_CHECK_EQUAL(nFpSeats, 3);
            BOOST_CHECK_EQUAL(nSybilSeats, 0);
        }

        // Empty-MN Spillover Check: 0 Active MNs, 10 Coin-Locks, 5 Funded PoW-Locks, 4 Bootstrap keys (= 9 Open PoW)
        // Open PoW (5 Funded PoW-Lock + 4 Bootstrap = 9 seats) MUST fill vacant MN seats BEFORE extra Coin-Locks,
        // so Coin-Locks take 1 primary seat + only 1 last-resort liveness fallback seat (2 total, NOT 10)!
        std::set<CPubKey> emptyMnSet, clSet2, fpSet2, bootSet2;
        std::vector<CPubKey> pool2;
        std::vector<CKey> keys2(20);
        for (size_t i = 0; i < keys2.size(); ++i) {
            keys2[i].MakeNewKey(true);
            const CPubKey pk = keys2[i].GetPubKey();
            pool2.push_back(pk);
            if (i < 10) clSet2.insert(pk);
            else if (i < 15) fpSet2.insert(pk);
            else bootSet2.insert(pk);
        }
        const CPubKey coord2 = *bootSet2.begin(); // 4 Bootstrap keys remain + 5 Funded PoW-Locks = 9 Open PoW
        std::vector<CPubKey> vElected2, vStandby2;
        ElectDualTrackValidators(uint256S("abababababababababababababababababababababababababababababababab"),
                                 pool2, emptyMnSet, coord2, 11, vElected2, clSet2, &vStandby2, &fpSet2, &bootSet2);
        BOOST_REQUIRE_EQUAL(vElected2.size(), 11U);
        int nCl2 = 0, nFp2 = 0, nBoot2 = 0;
        for (const CPubKey& pk : vElected2) {
            if (clSet2.count(pk)) nCl2++;
            else if (fpSet2.count(pk)) nFp2++;
            else if (bootSet2.count(pk)) nBoot2++;
        }
        BOOST_CHECK_EQUAL(nFp2, 5);
        BOOST_CHECK_EQUAL(nBoot2, 4);
        BOOST_CHECK_EQUAL(nCl2, 2); // 1 dedicated Sub-track 1B seat + 1 last-resort liveness fallback seat

        // 1b. Balanced Model D 15-seat Dual-Track quota test (8 MN + 2 Coin-Lock + 3 Funded PoW-Lock + 2 Free Entry)
        {
            std::set<CPubKey> mdMnSet, mdClSet, mdFpSet, mdZeroSet;
            std::vector<CPubKey> mdPool;
            std::vector<CKey> mdKeys(45);
            for (size_t i = 0; i < mdKeys.size(); ++i) {
                mdKeys[i].MakeNewKey(true);
                const CPubKey pk = mdKeys[i].GetPubKey();
                mdPool.push_back(pk);
                if (i < 12) mdMnSet.insert(pk);         // 12 Active MNs (1 coord + 11 pool)
                else if (i < 18) mdClSet.insert(pk);    // 6 Coin-Locks (250 KRISTA)
                else if (i < 24) mdFpSet.insert(pk);    // 6 Funded PoW-Locks (10 KRISTA)
                else mdZeroSet.insert(pk);              // 21 Zero-Coin Free Entry (0 KRISTA)
            }
            const CPubKey mdCoord = *mdMnSet.begin();
            std::vector<CPubKey> vElectedMD, vStandbyMD;
            ElectDualTrackValidators(uint256S("deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef"),
                                     mdPool, mdMnSet, mdCoord, Consensus::Params::ADAM_MODELD_MINERS_COUNT,
                                     vElectedMD, mdClSet, &vStandbyMD, &mdFpSet, nullptr, true,
                                     Consensus::Params::ADAM_MODELD_MASTERNODE_TRACK_QUOTA,
                                     Consensus::Params::ADAM_MODELD_COIN_LOCK_TRACK_QUOTA,
                                     Consensus::Params::ADAM_MODELD_ZERO_COIN_OPEN_SEATS);
            BOOST_REQUIRE_EQUAL(vElectedMD.size(), 15U);
            int cMn = 0, cCl = 0, cFp = 0, cZero = 0;
            for (const CPubKey& pk : vElectedMD) {
                if (mdMnSet.count(pk)) ++cMn;
                else if (mdClSet.count(pk)) ++cCl;
                else if (mdFpSet.count(pk)) ++cFp;
                else if (mdZeroSet.count(pk)) ++cZero;
            }
            BOOST_CHECK_EQUAL(cMn, 8);
            BOOST_CHECK_EQUAL(cCl, 2);
            BOOST_CHECK_EQUAL(cFp, 3);
            BOOST_CHECK_EQUAL(cZero, 2);
        }
    }

    // 2. Quota-capped stake-weighted VRF Coordinator election (all MNs = 4200, top-1 Coin-Lock = 250, top-3 PoW-Lock = 10)
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
        CBlockIndex* pPrev = chainActive.Tip();
        BOOST_REQUIRE(pPrev != nullptr);

        std::vector<uint256> vHashes;
        std::vector<CBlockIndex*> vIndices;
        std::vector<COutPoint> vAddedCoins;
        std::vector<CPubKey> vRegisteredMNs;

        auto makeCoinLockScript = [](const CKey& k, int64_t lockHeight) -> CScript {
            return BuildTestCoinLockScript(k, lockHeight);
        };
        auto makePoWLockScript = [](const CKey& k, const uint256& chHash, uint32_t nNonce, int64_t lockHeight) -> CScript {
            return BuildTestPoWLockScript(k, chHash, nNonce, lockHeight);
        };
        auto appendTestBlock = [&](const std::vector<CTransaction>& extraTxs) -> CBlockIndex* {
            const int h = pPrev->nHeight + 1;
            CBlock blk;
            blk.nVersion = 12;
            blk.hashPrevBlock = pPrev->GetBlockHash();
            blk.nTime = pPrev->nTime + 30;
            blk.nNonce = (uint32_t)h;
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk.vtx.push_back(cb);
            for (const auto& tx : extraTxs) blk.vtx.push_back(tx);
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

        while (pPrev->nHeight < 205) {
            appendTestBlock({});
        }

        // Register 7 Active Masternodes (4200 weight each), 16 Coin-Locks (250 weight each), and 20 Funded PoW-Locks (10 weight each)
        std::set<CPubKey> mnKeysSet, clKeysSet, fpKeysSet;
        std::vector<CPubKey> vMnPubKeys;
        for (int i = 0; i < 7; ++i) {
            CKey k;
            k.MakeNewKey(true);
            const CPubKey pk = k.GetPubKey();
            mnKeysSet.insert(pk);
            vMnPubKeys.push_back(pk);
            vRegisteredMNs.push_back(pk);
            COutPoint colOut(uint256S("7100000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i);
            testCoins.AddCoin(colOut, Coin(CTxOut(4200 * COIN, GetScriptForDestination(pk.GetID())), pPrev->nHeight, false, false), true);
            pcoinsTip->AddCoin(colOut, Coin(CTxOut(4200 * COIN, GetScriptForDestination(pk.GetID())), pPrev->nHeight, false, false), true);
            vAddedCoins.push_back(colOut);
            BOOST_REQUIRE(RegisterOnChainMasternodePing(pk, colOut, pPrev->nHeight, nullptr, &testCoins));
        }

        for (int i = 0; i < 16; ++i) {
            const int hCl = pPrev->nHeight + 1;
            CKey k;
            k.MakeNewKey(true);
            const CPubKey pk = k.GetPubKey();
            clKeysSet.insert(pk);
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(COutPoint(uint256S("7200000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i)));
            mtx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, makeCoinLockScript(k, hCl + 2900)));
            CTransaction ctx(mtx);
            COutPoint outp(ctx.GetHash(), 0);
            testCoins.AddCoin(outp, Coin(ctx.vout[0], hCl, false, false), true);
            pcoinsTip->AddCoin(outp, Coin(ctx.vout[0], hCl, false, false), true);
            vAddedCoins.push_back(outp);
            appendTestBlock({ctx});
        }

        // Verify legacy SHA256D nonce on a 10 KRISTA PoW-Lock output is strictly ignored under UPGRADE_HARDENING_V2
        CPubKey legacyShaKey;
        {
            const int hLegacy = pPrev->nHeight + 1;
            const uint256 chLegacy = pPrev->GetBlockHash();
            const uint256 legacyShaTarget = GetMinerPoWLimit(Params().NetworkIDString(), false);
            CKey kLeg;
            kLeg.MakeNewKey(true);
            legacyShaKey = kLeg.GetPubKey();
            const CBLSPubKey blsLeg = DeriveTestBlsPubKey(kLeg);
            uint32_t nonceSha = 0;
            for (;; ++nonceSha) {
                CHashWriter ss(SER_GETHASH, 0);
                ss << nonceSha << chLegacy << legacyShaKey;
                if (ss.GetHash() <= legacyShaTarget &&
                    ComputeMinerPoWRegistrationHash(nonceSha, chLegacy, legacyShaKey, true, &blsLeg) > GetMinerPoWLimit(Params().NetworkIDString(), true)) {
                    break;
                }
            }
            CMutableTransaction mtxLeg;
            mtxLeg.vin.push_back(CTxIn(COutPoint(uint256S("7250000000000000000000000000000000000000000000000000000000000000"), 0)));
            mtxLeg.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makePoWLockScript(kLeg, chLegacy, nonceSha, hLegacy + 2900)));
            CTransaction ctxLeg(mtxLeg);
            COutPoint outLeg(ctxLeg.GetHash(), 0);
            testCoins.AddCoin(outLeg, Coin(ctxLeg.vout[0], hLegacy, false, false), true);
            pcoinsTip->AddCoin(outLeg, Coin(ctxLeg.vout[0], hLegacy, false, false), true);
            vAddedCoins.push_back(outLeg);
            appendTestBlock({ctxLeg});
        }

        const uint256 targetMinerTest = GetMinerPoWLimit(Params().NetworkIDString(), true);
        for (int j = 0; j < 6; ++j) {
            const int hFp = pPrev->nHeight + 1;
            const uint256 chFp = pPrev->GetBlockHash();
            CKey k;
            k.MakeNewKey(true);
            const CPubKey pk = k.GetPubKey();
            fpKeysSet.insert(pk);
            const CBLSPubKey blsFp = DeriveTestBlsPubKey(k);
            const uint32_t nonceFp = SolveMinerPoWForTest(pk, chFp, targetMinerTest, true, &blsFp);
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(COutPoint(uint256S("7300000000000000000000000000000000000000000000000000000000000000"), (uint32_t)j)));
            mtx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makePoWLockScript(k, chFp, nonceFp, hFp + 2900)));
            CTransaction ctx(mtx);
            COutPoint outp(ctx.GetHash(), 0);
            testCoins.AddCoin(outp, Coin(ctx.vout[0], hFp, false, false), true);
            pcoinsTip->AddCoin(outp, Coin(ctx.vout[0], hFp, false, false), true);
            vAddedCoins.push_back(outp);
            appendTestBlock({ctx});
        }

        {
            std::set<CPubKey> checkClSet, checkFpSet;
            std::vector<CPubKey> checkPool = GetAdamMinerPool(pPrev->nHeight, &testCoins, nullptr, &checkClSet, nullptr, nullptr, &checkFpSet);
            BOOST_CHECK_EQUAL(checkFpSet.count(legacyShaKey), 0U);
            BOOST_CHECK_EQUAL(checkClSet.size(), 16U);
            BOOST_CHECK_EQUAL(checkFpSet.size(), 6U);
        }

        // (2a) Proportional Capital Coordinator Weighting (ADAM_FAIR_COORD_V4: 4200 : 250 : 10) at h=229 with
        //      7 Active MNs (29,400 weight) + 16 Coin-Locks (4,000 weight) + 6 Funded PoW-Locks (60 weight) = 33,460 total weight.
        //      3 * 4,060 = 12,180 <= 29,400 (no floor clamp): MN ~87.9% (~1757/2000), CL ~12.0% (~239/2000), FP ~0.18% (~4/2000).
        int nMnCoord = 0, nClCoord = 0, nFpCoord = 0, nOtherCoord = 0;
        for (int round = 0; round < 2000; ++round) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd") << round;
            std::vector<CPubKey> vElected;
            CPubKey coord;
            BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, pPrev->nHeight + 1, &testCoins));
            if (mnKeysSet.count(coord)) nMnCoord++;
            else if (clKeysSet.count(coord)) nClCoord++;
            else if (fpKeysSet.count(coord)) nFpCoord++;
            else nOtherCoord++;
        }
        BOOST_CHECK_EQUAL(nOtherCoord, 0);
        BOOST_CHECK_EQUAL(nMnCoord + nClCoord + nFpCoord, 2000);
        BOOST_CHECK_GT(nMnCoord, 1650);
        BOOST_CHECK_LT(nMnCoord, 1860);
        BOOST_CHECK_GT(nClCoord, 175);
        BOOST_CHECK_LT(nClCoord, 310);
        BOOST_CHECK_LT(nFpCoord, 20);

        // (2b) Single-MN Anti-Domination Floor under UPGRADE_FAIRNESS (ADAM_FAIR_COORD_V4, active from Block 1):
        //      1 Active MN (4,200 weight) vs 16 Coin-Locks (4,000 weight) + 6 Funded PoW-Locks (60 weight) = 4,060 non-MN weight.
        //      3 * 4,060 = 12,180 > 4,200 -> exact 75.0% Active MN floor (~1500 / 2000; std ~19.36).
        for (int i = 1; i < 7; ++i) {
            mapMasternodeLastActiveHeight.erase(vMnPubKeys[i]);
            mapMasternodeActiveHeights.erase(vMnPubKeys[i]);
            mapMasternodeCollateralOutpoints.erase(vMnPubKeys[i]);
        }
        BumpMasternodePingStateGen();

        int nSingleMnCoord = 0, nSplitPoolCoord = 0;
        for (int round = 0; round < 2000; ++round) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee") << round;
            std::vector<CPubKey> vElected;
            CPubKey coord;
            BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, pPrev->nHeight + 1, &testCoins));
            if (coord == vMnPubKeys[0]) nSingleMnCoord++;
            else if (clKeysSet.count(coord) || fpKeysSet.count(coord)) nSplitPoolCoord++;
        }
        BOOST_CHECK_EQUAL(nSingleMnCoord + nSplitPoolCoord, 2000);
        BOOST_CHECK_GT(nSingleMnCoord, 1410);
        BOOST_CHECK_LT(nSingleMnCoord, 1590);

        // (2b-V2) Verify V2 parameter table (65 : 8 : 2 from Block 1 on Testnet) while UPGRADE_FAIRNESS supersedes
        //         live SelectAdamNodes election at Block 1 with the exact 75.0% MN floor (~1500 / 2000).
        {
            uint64_t wMn = 0, wCl = 0, wFp = 0;
            BOOST_CHECK_EQUAL(Params().GetConsensus().nAdamCoordWeightV2Height, 1);
            Params().GetConsensus().GetAdamCoordinatorWeights(Params().GetConsensus().nAdamCoordWeightV2Height, wMn, wCl, wFp);
            BOOST_CHECK_EQUAL(wMn, 65ULL);
            BOOST_CHECK_EQUAL(wCl, 8ULL);
            BOOST_CHECK_EQUAL(wFp, 2ULL);

            int nSingleMnCoordV2 = 0, nSplitPoolCoordV2 = 0;
            const int hV2 = std::max(pPrev->nHeight + 1, Params().GetConsensus().nAdamCoordWeightV2Height);
            for (int round = 0; round < 2000; ++round) {
                CHashWriter ssSeed(SER_GETHASH, 0);
                ssSeed << uint256S("eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee") << round;
                std::vector<CPubKey> vElected;
                CPubKey coord;
                BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, hV2, &testCoins));
                if (coord == vMnPubKeys[0]) nSingleMnCoordV2++;
                else if (clKeysSet.count(coord) || fpKeysSet.count(coord)) nSplitPoolCoordV2++;
            }
            BOOST_CHECK_EQUAL(nSingleMnCoordV2 + nSplitPoolCoordV2, 2000);
            BOOST_CHECK_GT(nSingleMnCoordV2, 1410);
            BOOST_CHECK_LT(nSingleMnCoordV2, 1590);

            // (2b-ModelD) Verify Balanced Model D (height 600 on Testnet >= UPGRADE_MODELD = 500) shifts the active MN
            //             Coordinator VRF floor cleanly from 75% to 55.0% (~1100 / 2000) under ADAM_FAIR_COORD_V5.
            int nSingleMnCoordModelD = 0, nSplitPoolCoordModelD = 0;
            const int hModelDCord = std::max(600, Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight);
            for (int round = 0; round < 2000; ++round) {
                CHashWriter ssSeed(SER_GETHASH, 0);
                ssSeed << uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") << round;
                std::vector<CPubKey> vElected;
                CPubKey coord;
                BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, hModelDCord, &testCoins));
                if (coord == vMnPubKeys[0]) nSingleMnCoordModelD++;
                else if (clKeysSet.count(coord) || fpKeysSet.count(coord)) nSplitPoolCoordModelD++;
            }
            BOOST_CHECK_EQUAL(nSingleMnCoordModelD + nSplitPoolCoordModelD, 2000);
            BOOST_CHECK_GT(nSingleMnCoordModelD, 1010);
            BOOST_CHECK_LT(nSingleMnCoordModelD, 1190);
        }

        // (2c) Pre-Masternode (0 Active MNs) Proportional Capital Balance: Deactivate the last MN so 0 MNs exist,
        //      leaving 16 Coin-Locks (16 * 250 = 4,000 weight) vs 6 Funded PoW-Locks (6 * 10 = 60 weight) = 4,060 total weight!
        //      Coin-Lock wins ~98.52% (~985 / 1000) and Funded PoW-Lock wins ~1.48% (~15 / 1000).
        mapMasternodeLastActiveHeight.erase(vMnPubKeys[0]);
        mapMasternodeActiveHeights.erase(vMnPubKeys[0]);
        mapMasternodeCollateralOutpoints.erase(vMnPubKeys[0]);
        BumpMasternodePingStateGen();

        int nPreMnClCoord = 0, nPreMnFpCoord = 0, nPreMnOther = 0;
        for (int round = 0; round < 1000; ++round) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("abababababababababababababababababababababababababababababababab") << round;
            std::vector<CPubKey> vElected;
            CPubKey coord;
            BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, pPrev->nHeight + 1, &testCoins));
            if (clKeysSet.count(coord)) nPreMnClCoord++;
            else if (fpKeysSet.count(coord)) nPreMnFpCoord++;
            else nPreMnOther++;
        }
        BOOST_CHECK_EQUAL(nPreMnOther, 0);
        BOOST_CHECK_GT(nPreMnClCoord, 950);
        BOOST_CHECK_LT(nPreMnClCoord, 1000);
        BOOST_CHECK_GT(nPreMnFpCoord, 0);
        BOOST_CHECK_LT(nPreMnFpCoord, 50);

        // Cleanup all temporary state
        for (const CPubKey& pk : vRegisteredMNs) {
            mapMasternodeLastActiveHeight.erase(pk);
            mapMasternodeActiveHeights.erase(pk);
            mapMasternodeCollateralOutpoints.erase(pk);
        }
        BumpMasternodePingStateGen();
        for (const COutPoint& outp : vAddedCoins) {
            pcoinsTip->SpendCoin(outp);
        }
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

BOOST_AUTO_TEST_CASE(adam_create_new_block_pow_lock_cap_and_depth_test)
{
    ScopedChainParams regParams(CBaseChainParams::REGTEST);
    CBlockIndex* pOrigTip = nullptr;
    uint256 origBestBlock;
    std::unique_ptr<CBlockIndex> tempGenesis;
    uint256 tempGenesisHash;
    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vAddedCoins;

    auto makePoWLockScript = [](const CKey& k, const uint256& chHash, uint32_t nNonce, int64_t lockHeight) -> CScript {
        return BuildTestPoWLockScript(k, chHash, nNonce, lockHeight);
    };

    auto makeCoinLockScript = [](const CKey& k, int64_t lockHeight) -> CScript {
        return BuildTestCoinLockScript(k, lockHeight);
    };
    CBlockIndex* pPrev = nullptr;
    auto appendRegBlock = [&](int64_t nBlockTime) -> CBlockIndex* {
        const int h = pPrev->nHeight + 1;
        CBlock blk;
        blk.nVersion = 7;
        blk.hashPrevBlock = pPrev->GetBlockHash();
        blk.nTime = static_cast<uint32_t>(nBlockTime);
        blk.nBits = Params().GenesisBlock().nBits;
        blk.nNonce = static_cast<uint32_t>(h);
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        cb.vout.push_back(CTxOut(0, CScript() << OP_TRUE));
        blk.vtx.push_back(cb);
        blk.hashMerkleRoot = BlockMerkleRoot(blk);
        const uint256 bh = blk.GetHash();
        vHashes.push_back(bh);
        CBlockIndex* pIdx = new CBlockIndex(blk);
        pIdx->nHeight = h;
        pIdx->pprev = pPrev;
        pIdx->nBits = blk.nBits;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
        pIdx->phashBlock = &(ins.first->first);
        pIdx->BuildSkip();
        vIndices.push_back(pIdx);
        chainActive.SetTip(pIdx);
        pPrev = pIdx;
        return pIdx;
    };

    CKey minerKey;
    minerKey.MakeNewKey(true);
    const CScript minerScript = GetScriptForDestination(minerKey.GetPubKey().GetID());
    CBasicKeyStore keystore;
    TestMemPoolEntryHelper entry;
    std::set<uint256> maturePoWTxids, matureCoinLockTxids;
    uint256 immaturePoWTxid, immatureCoinLockTxid;

    {
        LOCK2(cs_main, mempool.cs);
        mempool.clear();
        pOrigTip = chainActive.Tip();
        origBestBlock = pcoinsTip->GetBestBlock();
        if (!pOrigTip) {
            tempGenesisHash = Params().GenesisBlock().GetHash();
            tempGenesis.reset(new CBlockIndex(Params().GenesisBlock()));
            tempGenesis->nHeight = 0;
            tempGenesis->pprev = nullptr;
            tempGenesis->nBits = Params().GenesisBlock().nBits;
            auto gIns = mapBlockIndex.insert(std::make_pair(tempGenesisHash, tempGenesis.get()));
            tempGenesis->phashBlock = &(gIns.first->first);
            tempGenesis->BuildSkip();
            chainActive.SetTip(tempGenesis.get());
        }
        pPrev = chainActive.Tip();
        BOOST_REQUIRE(pPrev != nullptr);

        const int64_t nowSlot = (GetAdjustedTime() / 15) * 15;
        while (pPrev->nHeight < 199) {
            const int nextH = pPrev->nHeight + 1;
            const int64_t targetTime = nowSlot - static_cast<int64_t>(205 - nextH) * 15;
            appendRegBlock(std::max<int64_t>(pPrev->GetBlockTime() + 15, targetTime));
        }
        BOOST_REQUIRE(IsAdamDualTrackActive(pPrev->nHeight + 1));
        pcoinsTip->SetBestBlock(pPrev->GetBlockHash());

        const uint256 chHash = pPrev->GetBlockHash();
        const uint256 minerTarget = GetMinerPoWLimit(Params().NetworkIDString(), true);

        // Add 2 mature (>= 100 confirmations) + 1 immature (< 100 confirmations) funded 10 KRISTA PoW-Lock txs
        for (int i = 0; i < 3; ++i) {
            CKey fundKey, regKey;
            fundKey.MakeNewKey(true);
            regKey.MakeNewKey(true);
            keystore.AddKey(fundKey);

            const CScript fundScript = GetScriptForDestination(fundKey.GetPubKey().GetID());
            const COutPoint prevOut(uint256S("8100000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
            const int coinHeight = (i < 2) ? 2 : (pPrev->nHeight - 80); // >= 100 confs vs 81 confs (< 100)
            pcoinsTip->AddCoin(prevOut, Coin(CTxOut(15 * COIN, fundScript), coinHeight, false, false), true);
            vAddedCoins.push_back(prevOut);

            const CBLSPubKey regBlsPk = DeriveTestBlsPubKey(regKey);
            const uint32_t nonce = SolveMinerPoWForTest(regKey.GetPubKey(), chHash, minerTarget, true, &regBlsPk);
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(prevOut));
            mtx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makePoWLockScript(regKey, chHash, nonce, pPrev->nHeight + 200)));
            mtx.vout.push_back(CTxOut(5 * COIN - 10000, fundScript));
            BOOST_REQUIRE(SignSignature(keystore, fundScript, mtx, 0, 15 * COIN, SIGHASH_ALL));

            CTransaction ctx(mtx);
            if (i < 2) {
                maturePoWTxids.insert(ctx.GetHash());
            } else {
                immaturePoWTxid = ctx.GetHash();
            }
            mempool.addUnchecked(ctx.GetHash(), entry.Fee(10000).Time(GetTime()).Priority(1e8).Height(pPrev->nHeight).FromTx(mtx, &mempool));
        }

        // Add 2 mature (>= 100 confirmations) + 1 immature (< 100 confirmations) 250 KRISTA Coin-Lock txs
        for (int i = 0; i < 3; ++i) {
            CKey fundKey, regKey;
            fundKey.MakeNewKey(true);
            regKey.MakeNewKey(true);
            keystore.AddKey(fundKey);

            const CScript fundScript = GetScriptForDestination(fundKey.GetPubKey().GetID());
            const COutPoint prevOut(uint256S("8150000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
            const int coinHeight = (i < 2) ? 2 : (pPrev->nHeight - 80); // >= 100 confs vs 81 confs (< 100)
            pcoinsTip->AddCoin(prevOut, Coin(CTxOut(260 * COIN, fundScript), coinHeight, false, false), true);
            vAddedCoins.push_back(prevOut);

            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(prevOut));
            mtx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, makeCoinLockScript(regKey, pPrev->nHeight + 200)));
            mtx.vout.push_back(CTxOut(10 * COIN - 10000, fundScript));
            BOOST_REQUIRE(SignSignature(keystore, fundScript, mtx, 0, 260 * COIN, SIGHASH_ALL));

            CTransaction ctx(mtx);
            if (i < 2) {
                matureCoinLockTxids.insert(ctx.GetHash());
            } else {
                immatureCoinLockTxid = ctx.GetHash();
            }
            mempool.addUnchecked(ctx.GetHash(), entry.Fee(10000).Time(GetTime()).Priority(1e8).Height(pPrev->nHeight).FromTx(mtx, &mempool));
        }
        BOOST_CHECK_EQUAL(mempool.size(), 6U);
    }

    // Block 1: must include strictly 1 mature PoW-Lock tx AND strictly 1 mature Coin-Lock tx, deferring immature inputs!
    std::unique_ptr<CBlockTemplate> pbt1(CreateNewBlock(minerScript, nullptr, false));
    BOOST_REQUIRE(pbt1 != nullptr);

    std::set<uint256> confirmedMaturePoWTxids, confirmedMatureCoinLockTxids;
    int nPoWLockInBlock1 = 0, nCoinLockInBlock1 = 0;
    for (size_t idx = 1; idx < pbt1->block.vtx.size(); ++idx) {
        const uint256 txid = pbt1->block.vtx[idx].GetHash();
        BOOST_CHECK(txid != immaturePoWTxid);
        BOOST_CHECK(txid != immatureCoinLockTxid);
        if (maturePoWTxids.count(txid)) {
            nPoWLockInBlock1++;
            confirmedMaturePoWTxids.insert(txid);
        }
        if (matureCoinLockTxids.count(txid)) {
            nCoinLockInBlock1++;
            confirmedMatureCoinLockTxids.insert(txid);
        }
    }
    BOOST_CHECK_EQUAL(nPoWLockInBlock1, ADAM_MAX_POW_LOCK_TX_PER_BLOCK);
    BOOST_CHECK_EQUAL(nCoinLockInBlock1, ADAM_MAX_COIN_LOCK_TX_PER_BLOCK);

    // Confirm Block 1's 1 PoW-Lock + 1 Coin-Lock tx, leaving 1 mature + 1 immature of each in mempool
    {
        LOCK2(cs_main, mempool.cs);
        std::list<CTransaction> removed;
        for (size_t idx = 1; idx < pbt1->block.vtx.size(); ++idx) {
            const CTransaction& confirmedTx = pbt1->block.vtx[idx];
            for (const CTxIn& txin : confirmedTx.vin) {
                pcoinsTip->SpendCoin(txin.prevout);
            }
            mempool.remove(confirmedTx, removed, true);
        }
        appendRegBlock(pPrev->GetBlockTime() + 15);
        pcoinsTip->SetBestBlock(pPrev->GetBlockHash());
        BOOST_CHECK_EQUAL(mempool.size(), 4U);
    }

    // Block 2: must include the remaining 1 mature PoW-Lock + 1 mature Coin-Lock tx while still deferring both immature txs!
    std::unique_ptr<CBlockTemplate> pbt2(CreateNewBlock(minerScript, nullptr, false));
    BOOST_REQUIRE(pbt2 != nullptr);

    int nPoWLockInBlock2 = 0, nCoinLockInBlock2 = 0;
    for (size_t idx = 1; idx < pbt2->block.vtx.size(); ++idx) {
        const uint256 txid = pbt2->block.vtx[idx].GetHash();
        BOOST_CHECK(txid != immaturePoWTxid);
        BOOST_CHECK(txid != immatureCoinLockTxid);
        if (maturePoWTxids.count(txid)) {
            nPoWLockInBlock2++;
            confirmedMaturePoWTxids.insert(txid);
        }
        if (matureCoinLockTxids.count(txid)) {
            nCoinLockInBlock2++;
            confirmedMatureCoinLockTxids.insert(txid);
        }
    }
    BOOST_CHECK_EQUAL(nPoWLockInBlock2, 1);
    BOOST_CHECK_EQUAL(nCoinLockInBlock2, 1);
    BOOST_CHECK_EQUAL(confirmedMaturePoWTxids.size(), 2U);
    BOOST_CHECK_EQUAL(confirmedMatureCoinLockTxids.size(), 2U);

    // Mixed Zero-Coin PoW + 2 Funded PoW-Lock + 2 Coin-Lock txs in mempool:
    // Zero-Coin PoW must NOT starve Funded PoW-Lock; block includes 1 Zero-Coin + 1 Funded PoW-Lock + 1 Coin-Lock!
    {
        LOCK2(cs_main, mempool.cs);
        mempool.clear();
        appendRegBlock(pPrev->GetBlockTime() + 15);
        pcoinsTip->SetBestBlock(pPrev->GetBlockHash());

        const uint256 chHash = pPrev->GetBlockHash();
        const uint256 zcTarget = GetZeroCoinPoWLimit(Params().NetworkIDString(), true);
        const uint256 minerTarget = GetMinerPoWLimit(Params().NetworkIDString(), true);

        CKey zcKey;
        zcKey.MakeNewKey(true);
        const CBLSPubKey zcBlsPk = DeriveTestBlsPubKey(zcKey);
        const uint32_t zcNonce = SolveZeroCoinPoWForTest(zcKey.GetPubKey(), chHash, zcTarget, true, &zcBlsPk);
        CMutableTransaction zcMtx;
        zcMtx.vin.push_back(CTxIn(COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0), CScript() << OP_0));
        zcMtx.vout.push_back(CTxOut(0, makePoWLockScript(zcKey, chHash, zcNonce, pPrev->nHeight + 50)));
        CTransaction zcCtx(zcMtx);
        BOOST_REQUIRE(IsZeroCoinPoWTransaction(zcCtx));
        mempool.addUnchecked(zcCtx.GetHash(), entry.Fee(0).Time(GetTime()).Priority(1e9).Height(pPrev->nHeight).FromTx(zcMtx, &mempool));

        for (int i = 0; i < 2; ++i) {
            CKey fundKey, regKey;
            fundKey.MakeNewKey(true);
            regKey.MakeNewKey(true);
            keystore.AddKey(fundKey);
            const CScript fundScript = GetScriptForDestination(fundKey.GetPubKey().GetID());
            const COutPoint prevOut(uint256S("8200000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
            pcoinsTip->AddCoin(prevOut, Coin(CTxOut(15 * COIN, fundScript), 2, false, false), true);
            vAddedCoins.push_back(prevOut);

            const CBLSPubKey regBlsPk = DeriveTestBlsPubKey(regKey);
            const uint32_t nonce = SolveMinerPoWForTest(regKey.GetPubKey(), chHash, minerTarget, true, &regBlsPk);
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(prevOut));
            mtx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makePoWLockScript(regKey, chHash, nonce, pPrev->nHeight + 200)));
            mtx.vout.push_back(CTxOut(5 * COIN - 10000, fundScript));
            BOOST_REQUIRE(SignSignature(keystore, fundScript, mtx, 0, 15 * COIN, SIGHASH_ALL));
            CTransaction ctx(mtx);
            mempool.addUnchecked(ctx.GetHash(), entry.Fee(10000).Time(GetTime()).Priority(1e8).Height(pPrev->nHeight).FromTx(mtx, &mempool));
        }

        for (int i = 0; i < 2; ++i) {
            CKey fundKey, regKey;
            fundKey.MakeNewKey(true);
            regKey.MakeNewKey(true);
            keystore.AddKey(fundKey);
            const CScript fundScript = GetScriptForDestination(fundKey.GetPubKey().GetID());
            const COutPoint prevOut(uint256S("8250000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
            pcoinsTip->AddCoin(prevOut, Coin(CTxOut(260 * COIN, fundScript), 2, false, false), true);
            vAddedCoins.push_back(prevOut);

            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(prevOut));
            mtx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, makeCoinLockScript(regKey, pPrev->nHeight + 200)));
            mtx.vout.push_back(CTxOut(10 * COIN - 10000, fundScript));
            BOOST_REQUIRE(SignSignature(keystore, fundScript, mtx, 0, 260 * COIN, SIGHASH_ALL));
            CTransaction ctx(mtx);
            mempool.addUnchecked(ctx.GetHash(), entry.Fee(10000).Time(GetTime()).Priority(1e8).Height(pPrev->nHeight).FromTx(mtx, &mempool));
        }
        BOOST_CHECK_EQUAL(mempool.size(), 5U);
    }

    std::unique_ptr<CBlockTemplate> pbt3(CreateNewBlock(minerScript, nullptr, false));
    BOOST_REQUIRE(pbt3 != nullptr);
    int nZeroCoinInBlock3 = 0, nFundedPoWInBlock3 = 0, nCoinLockInBlock3 = 0;
    for (size_t idx = 1; idx < pbt3->block.vtx.size(); ++idx) {
        const CTransaction& btx = pbt3->block.vtx[idx];
        const bool isZc = IsZeroCoinPoWTransaction(btx);
        if (isZc) nZeroCoinInBlock3++;
        for (const CTxOut& txout : btx.vout) {
            std::vector<unsigned char> vchNonce;
            uint256 hashChallenge;
            CPubKey regPubKey;
            int64_t nRegLockTime = 0;
            CKeyID regKeyId;
            if (MatchCoinLockRegistration(txout.scriptPubKey, regPubKey, nRegLockTime, regKeyId)) {
                nCoinLockInBlock3++;
                break;
            }
            if (!isZc && MatchPoWLockRegistration(txout.scriptPubKey, vchNonce, hashChallenge, regPubKey, nRegLockTime, regKeyId)) {
                nFundedPoWInBlock3++;
                break;
            }
        }
    }
    BOOST_CHECK_EQUAL(nZeroCoinInBlock3, 1);
    BOOST_CHECK_EQUAL(nFundedPoWInBlock3, ADAM_MAX_POW_LOCK_TX_PER_BLOCK);
    BOOST_CHECK_EQUAL(nCoinLockInBlock3, ADAM_MAX_COIN_LOCK_TX_PER_BLOCK);

    // Cleanup temporary mempool, coins, solutions cache, and chain state
    {
        LOCK2(cs_main, mempool.cs);
        mempool.clear();
        {
            LOCK(cs_adam_solutions);
            mapAdamSolutionsCache.clear();
        }
        for (const COutPoint& outp : vAddedCoins) {
            pcoinsTip->SpendCoin(outp);
        }
        pcoinsTip->SetBestBlock(origBestBlock);
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

BOOST_AUTO_TEST_CASE(adam_fairness_17algo_and_compressed_pubkey_test)
{
    // 1. Uniform 17 single-pass algorithm sampling (excludes asymmetric algo 11 = X11KVS)
    CKey sampleKey;
    sampleKey.MakeNewKey(true);
    CPubKey samplePk = sampleKey.GetPubKey();
    for (int i = 0; i < 10000; ++i) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << uint256S("1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef") << i;
        int a1 = -1, a2 = -1, a3 = -1;
        GetAdam3PermutationAlgos(ss.GetHash(), samplePk, a1, a2, a3);
        BOOST_CHECK_NE(a1, 11);
        BOOST_CHECK_NE(a2, 11);
        BOOST_CHECK_NE(a3, 11);
        BOOST_CHECK(a1 >= 0 && a1 <= 17);
        BOOST_CHECK(a2 >= 0 && a2 <= 17);
        BOOST_CHECK(a3 >= 0 && a3 <= 17);
        BOOST_CHECK(a1 != a2 && a2 != a3 && a1 != a3);
    }
    for (int i = 0; i < 500; ++i) {
        CHashWriter ss(SER_GETHASH, 0);
        ss << uint256S("fedcba0987654321fedcba0987654321fedcba0987654321fedcba0987654321") << i;
        int algoV11 = GetAdamPuzzleAlgo(ss.GetHash(), samplePk, true);
        BOOST_CHECK_NE(algoV11, 11);
        BOOST_CHECK(algoV11 >= 0 && algoV11 <= 17);
    }

    // 2. VerifyAdamSolution rejects uncompressed 65B pubkeys under UPGRADE_FAIRNESS, accepts 33B compressed pubkeys
    const CBaseChainParams::Network prevNet = Params().NetworkID();
    struct RestoreNet {
        CBaseChainParams::Network net;
        ~RestoreNet() { SelectParams(net); }
    } restoreNet{prevNet};
    SelectParams(CBaseChainParams::REGTEST);

    auto buildSignedV12Sol = [](const CKey& k, const uint256& prevHash, const uint256& seed) -> std::vector<unsigned char> {
        const CPubKey pk = k.GetPubKey();
        CBLSSecretKey blsSk = DeriveBLSFromCKey(k);
        const uint32_t nNonce = 42;
        unsigned char inputBuf[104];
        const unsigned int keyLen = pk.size();
        memcpy(inputBuf, seed.begin(), 32);
        inputBuf[32] = static_cast<unsigned char>(keyLen);
        memcpy(inputBuf + 33, pk.begin(), keyLen);
        WriteLE32(inputBuf + 33 + keyLen, nNonce);
        const size_t inputLen = 37 + keyLen;

        int a1 = -1, a2 = -1, a3 = -1;
        GetAdam3PermutationAlgos(prevHash, pk, a1, a2, a3);
        uint256 h3 = CalculateAdamPuzzleHash(a3, inputBuf, inputBuf + inputLen);
        uint256 h2 = CalculateAdamPuzzleHash(a2, h3.begin(), h3.begin() + 32);
        uint256 pHash = CalculateAdamPuzzleHash(a1, h2.begin(), h2.begin() + 32);

        std::vector<unsigned char> vchSig;
        BOOST_REQUIRE(SignBLSWithECDSAFallback(pHash, k, blsSk, vchSig, GetBLSDomainTag(BLSDomain::ADAM_SOL, IsHardeningV3Active(500))));
        CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
        ssSol << nNonce << vchSig;
        return std::vector<unsigned char>(ssSol.begin(), ssSol.end());
    };

    const uint256 prevHash = uint256S("0101010101010101010101010101010101010101010101010101010101010101");
    const uint256 adamSeed = uint256S("0202020202020202020202020202020202020202020202020202020202020202");

    CKey uncompKey;
    uncompKey.MakeNewKey(false);
    BOOST_REQUIRE_EQUAL(uncompKey.GetPubKey().size(), 65U);
    std::vector<unsigned char> solUncomp = buildSignedV12Sol(uncompKey, prevHash, adamSeed);
    BOOST_CHECK(!VerifyAdamSolution(prevHash, adamSeed, uncompKey.GetPubKey(), solUncomp, 0x207fffff, 12, 500, 0));

    CKey compKey;
    compKey.MakeNewKey(true);
    BOOST_REQUIRE_EQUAL(compKey.GetPubKey().size(), 33U);
    std::vector<unsigned char> solComp = buildSignedV12Sol(compKey, prevHash, adamSeed);
    BOOST_CHECK(VerifyAdamSolution(prevHash, adamSeed, compKey.GetPubKey(), solComp, 0x207fffff, 12, 500, 0));
}

BOOST_AUTO_TEST_CASE(model_d_pre_quorum_header_check_test)
{
    ScopedChainParams mainParams(CBaseChainParams::MAIN);
    const Consensus::Params& consensus = Params().GetConsensus();
    const uint256 prevHash = uint256S("00000123456789abcdef0123456789abcdef0123456789abcdef0123456789ab");

    CBlockIndex prevIdx;
    prevIdx.pprev = nullptr;
    prevIdx.nHeight = 4999;
    prevIdx.nVersion = 12;
    prevIdx.phashBlock = &prevHash;
    prevIdx.nBits = consensus.powLimit.GetCompact();
    const int64_t nowSlot = (GetAdjustedTime() / consensus.nTimeSlotLength) * consensus.nTimeSlotLength;
    prevIdx.nTime = static_cast<unsigned int>(nowSlot - 30);

    CBlockHeader hdr;
    hdr.nVersion = 12;
    hdr.hashPrevBlock = prevHash;
    hdr.nTime = prevIdx.nTime + 30;
    hdr.nBits = GetNextWorkRequired(&prevIdx, &hdr);
    hdr.nNonce = 0;

    CKey coordKey;
    coordKey.MakeNewKey(true);
    CBLSSecretKey coordBls = DeriveBLSFromCKey(coordKey);
    hdr.vAdamMiners.push_back(coordKey.GetPubKey());
    hdr.vAdamSolutions.emplace_back(64, 0x01);
    for (int i = 1; i < 5; ++i) {
        CKey k;
        k.MakeNewKey(true);
        hdr.vAdamMiners.push_back(k.GetPubKey());
        hdr.vAdamSolutions.emplace_back(64, static_cast<unsigned char>(i + 1));
    }

    const bool fV3Hdr = IsHardeningV3Active(prevIdx.nHeight + 1);
    BOOST_REQUIRE(SignBLSWithECDSAFallback(prevHash, coordKey, coordBls, hdr.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, fV3Hdr)));
    BOOST_REQUIRE(SignBLSWithECDSAFallback(hdr.GetHash(), coordKey, coordBls, hdr.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, fV3Hdr)));

    // 1. Empty vQuorumSig must fail with missing-quorum-sig when fCheckQuorumSig=true
    CValidationState st1;
    BOOST_CHECK(!ContextualCheckBlockHeader(hdr, st1, &prevIdx, true));
    BOOST_CHECK_EQUAL(st1.GetRejectReason(), "missing-quorum-sig");

    // 2. Empty vQuorumSig must pass pre-quorum validation when fCheckQuorumSig=false
    CValidationState st2;
    BOOST_CHECK(ContextualCheckBlockHeader(hdr, st2, &prevIdx, false));

    // 3. Oversized vQuorumSig must fail even when fCheckQuorumSig=false
    hdr.vQuorumSig.assign(4097, 0x33);
    CValidationState st3;
    BOOST_CHECK(!ContextualCheckBlockHeader(hdr, st3, &prevIdx, false));

    // 4. Malformed vQuorumSig must fail with bad-quorum-sig-format even when fCheckQuorumSig=false
    hdr.vQuorumSig = {0x01, 0x02};
    CValidationState st4;
    BOOST_CHECK(!ContextualCheckBlockHeader(hdr, st4, &prevIdx, false));
    BOOST_CHECK_EQUAL(st4.GetRejectReason(), "bad-quorum-sig-format");

    // 5. Well-formed vQuorumSig matching hdr.GetHash() must pass with fCheckQuorumSig=true
    llmq::CQuorumSignature qsig;
    qsig.blockHash = hdr.GetHash();
    COutPoint dummyOut(prevHash, 0);
    qsig.signatures.emplace_back(dummyOut, hdr.vAdamCoordinatorSig);
    CDataStream ssQ(SER_NETWORK, PROTOCOL_VERSION);
    ssQ << qsig;
    hdr.vQuorumSig.assign(ssQ.begin(), ssQ.end());
    CValidationState st5;
    BOOST_CHECK(ContextualCheckBlockHeader(hdr, st5, &prevIdx, true));

    {
        LOCK(cs_recent_vrf_proofs);
        mapRecentVRFProofs.erase(hdr.GetHash());
    }
}

BOOST_AUTO_TEST_CASE(adam_phase2_coord_llmq_cache_regression_test)
{
    const CBaseChainParams::Network prevNet = Params().NetworkID();
    CBlockIndex* const pOrigTip = chainActive.Tip();
    const bool origPingInit = g_fMasternodePingStateInitialized;
    std::unique_ptr<dip3::CDeterministicMNManager> origMgr = std::move(deterministicMNManager);
    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vCoins;
    std::vector<CPubKey> vLegacyMns;
    struct Cleanup {
        CBaseChainParams::Network net;
        CBlockIndex* tip;
        bool pingInit;
        std::unique_ptr<dip3::CDeterministicMNManager>& origM;
        std::vector<uint256>& hashes;
        std::vector<CBlockIndex*>& idxs;
        std::vector<COutPoint>& coins;
        std::vector<CPubKey>& legacyMns;
        ~Cleanup() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, 1);
            for (const CPubKey& pk : legacyMns) {
                mapMasternodeLastActiveHeight.erase(pk);
                mapMasternodeActiveHeights.erase(pk);
                mapMasternodeCollateralOutpoints.erase(pk);
            }
            BumpMasternodePingStateGen();
            for (const COutPoint& o : coins) {
                pcoinsTip->SpendCoin(o);
            }
            chainActive.SetTip(tip);
            for (size_t i = 0; i < idxs.size(); ++i) {
                mapBlockIndex.erase(hashes[i]);
                delete idxs[i];
            }
            llmq::ClearQuorumCache();
            g_fMasternodePingStateInitialized = pingInit;
            deterministicMNManager = std::move(origM);
            InitializeAdamMinerRegCache(true);
            SelectParams(net);
        }
    } cleanup{prevNet, pOrigTip, origPingInit, origMgr, vHashes, vIndices, vCoins, vLegacyMns};

    // 1. H-3: GetAdamFallbackCoordinatorForSlot ignores local mapSolutions under V3, whereas pre-V3 depends on mapSolutions
    {
        SelectParams(CBaseChainParams::REGTEST);
        const uint256 fbSeed = uint256S("5555555555555555555555555555555555555555555555555555555555555555");
        std::vector<CPubKey> vMiners;
        for (int i = 0; i < 5; ++i) {
            vMiners.push_back(GetAdamDeterministicKey(i).GetPubKey());
        }
        std::vector<CPubKey> vRanked = RankValidatorsByPoBLSDistance(fbSeed, 500, vMiners, {});
        BOOST_REQUIRE_EQUAL(vRanked.size(), 5U);
        const CPubKey topRanked = vRanked[0];
        const CPubKey otherMiner = vRanked[vRanked.size() - 1];

        std::map<CPubKey, std::vector<unsigned char>> mapOnlyOther;
        {
            CKey otherKey;
            for (int i = 0; i < 5; ++i) {
                if (GetAdamDeterministicKey(i).GetPubKey() == otherMiner) {
                    otherKey = GetAdamDeterministicKey(i);
                    break;
                }
            }
            BOOST_REQUIRE(otherKey.IsValid());
            CBLSSecretKey otherBls = DeriveBLSFromCKey(otherKey);
            std::vector<unsigned char> vchTicketSig;
            BOOST_REQUIRE(SignBLSWithECDSAFallback(fbSeed, otherKey, otherBls, vchTicketSig, GetBLSDomainTag(BLSDomain::ADAM_SOL, false)));
            CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
            ssSol << (uint32_t)1 << vchTicketSig;
            mapOnlyOther[otherMiner] = std::vector<unsigned char>(ssSol.begin(), ssSol.end());
        }
        std::map<CPubKey, std::vector<unsigned char>> mapEmpty;

        CPubKey fb1, prevFb1, fb2, prevFb2;
        int rot1 = -1, rot2 = -1;
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(fbSeed, 500, 45, vMiners, mapOnlyOther, fb1, prevFb1, rot1));
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(fbSeed, 500, 45, vMiners, mapEmpty, fb2, prevFb2, rot2));
        BOOST_CHECK(fb1 == fb2);
        BOOST_CHECK(fb1 == topRanked);

        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        CPubKey fbPre1, prevPre1, fbPre2, prevPre2;
        int rotPre1 = -1, rotPre2 = -1;
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(fbSeed, 500, 45, vMiners, mapOnlyOther, fbPre1, prevPre1, rotPre1));
        BOOST_REQUIRE(GetAdamFallbackCoordinatorForSlot(fbSeed, 500, 45, vMiners, mapEmpty, fbPre2, prevPre2, rotPre2));
        BOOST_CHECK(fbPre1 == otherMiner);
        BOOST_CHECK(fbPre1 != fbPre2);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, 1);
    }

    // 2. C-2 & M-2: Testnet DIP3 + V3 pre-seed snapshot (seedHeight - 1), proTxHash sort, legacy ping cutoff, and SelectAdamNodes fOpenEntry cache isolation
    SelectParams(CBaseChainParams::TESTNET);
    g_fMasternodePingStateInitialized = true;
    deterministicMNManager.reset(new dip3::CDeterministicMNManager(1 << 20, true, true));

    BOOST_REQUIRE(pOrigTip != nullptr);
    CBlockIndex* pPrev = pOrigTip;
    auto pushBlock = [&](const std::vector<CTransaction>& txs) -> CBlockIndex* {
        const int h = pPrev->nHeight + 1;
        CBlock blk;
        blk.nVersion = 12;
        blk.hashPrevBlock = pPrev->GetBlockHash();
        blk.nTime = pPrev->nTime + 30;
        blk.nNonce = (uint32_t)h;
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk.vtx.push_back(cb);
        for (const auto& t : txs) blk.vtx.push_back(t);
        blk.hashMerkleRoot = BlockMerkleRoot(blk);
        vHashes.push_back(blk.GetHash());
        {
            LOCK(cs_recent_vrf_proofs);
            mapRecentVRFProofs[vHashes.back()] = blk.vAdamVRFProof;
        }
        CBlockIndex* pIdx = new CBlockIndex(blk);
        pIdx->nHeight = h;
        pIdx->pprev = pPrev;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
        pIdx->phashBlock = &(ins.first->first);
        pIdx->BuildSkip();
        vIndices.push_back(pIdx);
        chainActive.SetTip(pIdx);
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk, pIdx, st, *pcoinsTip, false));
        pPrev = pIdx;
        return pIdx;
    };

    while (pPrev->nHeight < 999) {
        pushBlock({});
    }
    auto makeProReg = [&](int idx, CKey& ownerOut, COutPoint& colOut) -> CTransaction {
        ownerOut = GetAdamDeterministicKey(idx);
        const CPubKey pk = ownerOut.GetPubKey();
        const CScript spk = GetScriptForDestination(pk.GetID());
        colOut = COutPoint(uint256S("b100000000000000000000000000000000000000000000000000000000000000"), (uint32_t)idx);
        COutPoint feeOut(uint256S("b200000000000000000000000000000000000000000000000000000000000000"), (uint32_t)idx);
        pcoinsTip->AddCoin(colOut, Coin(CTxOut(4200 * COIN, spk), 10, false, false), true);
        pcoinsTip->AddCoin(feeOut, Coin(CTxOut(1 * COIN, spk), 10, false, false), true);
        vCoins.push_back(colOut);
        vCoins.push_back(feeOut);

        CBLSSecretKey opSk;
        opSk.MakeNewKey();
        CMutableTransaction mtx;
        mtx.vin.push_back(CTxIn(feeOut));
        dip3::CProRegTx ptx;
        ptx.nVersion = 2;
        ptx.collateralOutpoint = colOut;
        ptx.addr = LookupNumeric("198.51.100.60", 27900 + idx);
        ptx.pubKeyOwner = pk;
        ptx.blsPubKeyOperator = CBLSPubKey(opSk);
        BOOST_REQUIRE(ptx.blsPoP.SignPoP(opSk));
        ptx.keyIDVoting = pk.GetID();
        ptx.nOperatorReward = 0;
        ptx.scriptPayout = spk;
        ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
        BOOST_REQUIRE(ownerOut.Sign(ptx.GetSignHash(), ptx.vchSig));
        mtx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));
        return CTransaction(mtx);
    };

    // Register 5 MNs at height 1000 (strictly before epoch 1010's snapshot height = 1008)
    std::vector<CTransaction> preSnapTxs;
    std::set<CPubKey> setPreSnapPks;
    for (int i = 0; i < 5; ++i) {
        CKey owner;
        COutPoint col;
        preSnapTxs.push_back(makeProReg(i, owner, col));
        setPreSnapPks.insert(owner.GetPubKey());
    }
    pushBlock(preSnapTxs); // height 1000

    while (pPrev->nHeight < 1008) {
        pushBlock({});
    }
    // Register a 6th MN at height 1009 (= seedHeight for epoch 1010, AFTER snapHeight 1008!)
    CKey lateOwner;
    COutPoint lateCol;
    pushBlock({makeProReg(5, lateOwner, lateCol)}); // height 1009

    // Also inject a legacy ping-only MN (not in DIP3) -> C-2 must ignore it post-DIP3+V3
    CKey legacyOnlyKey = GetAdamDeterministicKey(6);
    COutPoint legacyCol(uint256S("b300000000000000000000000000000000000000000000000000000000000000"), 6);
    pcoinsTip->AddCoin(legacyCol, Coin(CTxOut(4200 * COIN, GetScriptForDestination(legacyOnlyKey.GetPubKey().GetID())), 10, false, false), true);
    vCoins.push_back(legacyCol);
    vLegacyMns.push_back(legacyOnlyKey.GetPubKey());
    BOOST_REQUIRE(RegisterOnChainMasternodePing(legacyOnlyKey.GetPubKey(), legacyCol, 1009, nullptr, pcoinsTip));
    InitializeAdamMinerRegCache(true);

    while (pPrev->nHeight < 1014) {
        pushBlock({});
    }

    // Epoch 1010..1019 (Testnet fRotationActive=true -> dkgInterval=10, seedHeight=1009, snapHeight=1008):
    // Late MN registered at 1009 MUST NOT enter quorum at 1015; legacy ping MN must NOT appear in DIP3 pool or quorum
    llmq::CQuorum q1015 = llmq::GetActiveQuorum(1015, pPrev, pcoinsTip);
    BOOST_REQUIRE_EQUAL(q1015.members.size(), 5U);
    for (const auto& m : q1015.members) {
        BOOST_CHECK(setPreSnapPks.count(m.pubKeyMasternode) > 0);
        BOOST_CHECK(m.pubKeyMasternode != lateOwner.GetPubKey());
        BOOST_CHECK(m.pubKeyMasternode != legacyOnlyKey.GetPubKey());
    }

    std::set<CPubKey> mnPoolPks;
    std::vector<CPubKey> fullPool = GetAdamMinerPool(1014, pcoinsTip, &mnPoolPks);
    BOOST_CHECK(mnPoolPks.count(lateOwner.GetPubKey()) > 0);
    BOOST_CHECK_EQUAL(mnPoolPks.count(legacyOnlyKey.GetPubKey()), 0U);
    BOOST_CHECK_EQUAL(std::count(fullPool.begin(), fullPool.end(), legacyOnlyKey.GetPubKey()), 0);

    // M-2: Verify SelectAdamNodes cache key isolates across fOpenEntry boundary (nAdamOpenEntryHeight = 2500 on Mainnet)
    {
        const uint256 sameSeed = GetAdamSeed(pPrev);
        SelectParams(CBaseChainParams::MAIN);
        std::vector<CPubKey> vPreOpen, vPostOpen;
        CPubKey cPreOpen, cPostOpen;
        const int openH = Params().GetConsensus().nAdamOpenEntryHeight;
        BOOST_REQUIRE_GT(openH, 1);
        BOOST_CHECK(SelectAdamNodes(sameSeed, Params().GetConsensus(), vPreOpen, cPreOpen, openH - 1, pcoinsTip));
        BOOST_CHECK(SelectAdamNodes(sameSeed, Params().GetConsensus(), vPostOpen, cPostOpen, openH, pcoinsTip));
        BOOST_CHECK(!vPreOpen.empty() && !vPostOpen.empty());
    }
}

BOOST_AUTO_TEST_CASE(adam_collect_cached_solutions_sync_test)
{
    struct CacheGuard {
        std::map<uint256, std::map<CPubKey, std::vector<unsigned char>>> saved;
        CacheGuard() {
            LOCK(cs_adam_solutions);
            saved.swap(mapAdamSolutionsCache);
        }
        ~CacheGuard() {
            LOCK(cs_adam_solutions);
            mapAdamSolutionsCache.swap(saved);
        }
    } guard;

    const uint256 tipHash = uint256S("00000000000000000000000000000000000000000000000000000000deadbeef");
    const uint256 unknownTipHash = uint256S("00000000000000000000000000000000000000000000000000000000cafebabe");

    BOOST_CHECK(CollectCachedAdamSolutionsForTip(uint256()).empty());
    BOOST_CHECK(CollectCachedAdamSolutionsForTip(unknownTipHash).empty());

    std::set<CPubKey> expectedValidKeys;
    {
        LOCK(cs_adam_solutions);
        auto& tipMap = mapAdamSolutionsCache[tipHash];

        // Invalid key, empty solution, and oversized (>512) solution must be filtered out
        tipMap[CPubKey()] = std::vector<unsigned char>(32, 0x11);

        CKey emptySolKey;
        emptySolKey.MakeNewKey(true);
        tipMap[emptySolKey.GetPubKey()] = std::vector<unsigned char>();

        CKey oversizedSolKey;
        oversizedSolKey.MakeNewKey(true);
        tipMap[oversizedSolKey.GetPubKey()] = std::vector<unsigned char>(513, 0x22);

        for (int i = 0; i < 40; ++i) {
            CKey k;
            k.MakeNewKey(true);
            CPubKey pk = k.GetPubKey();
            BOOST_REQUIRE(pk.IsValid());
            expectedValidKeys.insert(pk);
            tipMap[pk] = std::vector<unsigned char>(32, static_cast<unsigned char>(0x30 + (i % 16)));
        }
    }

    BOOST_CHECK(CollectCachedAdamSolutionsForTip(tipHash, 0).empty());
    BOOST_CHECK(CollectCachedAdamSolutionsForTip(unknownTipHash).empty());

    BOOST_CHECK_EQUAL(MAX_ADAMSOL_SYNC_PER_PEER, 32U);
    const std::vector<CAdamSolutionMsg> vDefault = CollectCachedAdamSolutionsForTip(tipHash);
    BOOST_CHECK_EQUAL(vDefault.size(), MAX_ADAMSOL_SYNC_PER_PEER);
    std::set<CPubKey> seenKeys;
    for (const auto& msg : vDefault) {
        BOOST_CHECK(msg.hashPrevBlock == tipHash);
        BOOST_CHECK(msg.minerKey.IsValid());
        BOOST_CHECK(expectedValidKeys.count(msg.minerKey) == 1U);
        BOOST_CHECK(!msg.vchSolution.empty() && msg.vchSolution.size() <= 512U);
        seenKeys.insert(msg.minerKey);
    }
    BOOST_CHECK_EQUAL(seenKeys.size(), MAX_ADAMSOL_SYNC_PER_PEER);

    const std::vector<CAdamSolutionMsg> vCapped = CollectCachedAdamSolutionsForTip(tipHash, 5);
    BOOST_CHECK_EQUAL(vCapped.size(), 5U);

    const std::vector<CAdamSolutionMsg> vAllValid = CollectCachedAdamSolutionsForTip(tipHash, 64);
    BOOST_CHECK_EQUAL(vAllValid.size(), 40U);
}

BOOST_AUTO_TEST_CASE(hardening_v4_bls_reg_fork_maturity_and_cltv_test)
{
    ScopedChainParams regParams(CBaseChainParams::REGTEST);
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
    CBlockIndex* pPrev = chainActive.Tip();
    BOOST_REQUIRE(pPrev != nullptr);
    const int baseH = pPrev->nHeight;

    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vAddedCoins;
    auto appendTestBlock = [&](const std::vector<CTransaction>& extraTxs) -> CBlockIndex* {
        const int h = pPrev->nHeight + 1;
        CBlock blk;
        blk.nVersion = 12;
        blk.hashPrevBlock = pPrev->GetBlockHash();
        blk.nTime = pPrev->nTime + 30;
        blk.nNonce = (uint32_t)h;
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk.vtx.push_back(cb);
        for (const auto& tx : extraTxs) blk.vtx.push_back(tx);
        blk.hashMerkleRoot = BlockMerkleRoot(blk);
        uint256 bh = blk.GetHash();
        vHashes.push_back(bh);
        CBlockIndex* pIdx = new CBlockIndex(blk);
        pIdx->nHeight = h;
        pIdx->pprev = pPrev;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
        pIdx->phashBlock = &(ins.first->first);
        pIdx->BuildSkip();
        CDiskBlockPos pos;
        WriteBlockToDisk(blk, pos);
        pIdx->nFile = pos.nFile;
        pIdx->nDataPos = pos.nPos;
        pIdx->nStatus |= BLOCK_HAVE_DATA;
        vIndices.push_back(pIdx);
        chainActive.SetTip(pIdx);
        testCoins.SetBestBlock(bh);
        RegisterBlockMasternodeActivity(blk, h, &testCoins);
        pPrev = pIdx;
        return pIdx;
    };

    // 1. C-5b: V4 BLS+PoP validation (valid, missing, bad PoP, duplicate BLS) & puzzle binding
    {
        CKey kA, kB;
        kA.MakeNewKey(true);
        kB.MakeNewKey(true);
        CBLSSecretKey blsA = DeriveBLSFromCKey(kA);
        CBLSSecretKey blsB = DeriveBLSFromCKey(kB);
        CBLSPubKey pkBlsA(blsA), pkBlsB(blsB);
        CBLSSignature popA, popWrong;
        BOOST_REQUIRE(popA.SignPoP(blsA));
        BOOST_REQUIRE(popWrong.SignPoP(blsB));

        std::string err;
        BOOST_CHECK(CheckRegistrationBlsBinding(kA.GetPubKey(), pkBlsA, popA, baseH + 1, pPrev, &testCoins, true, err));
        BOOST_CHECK(!CheckRegistrationBlsBinding(kA.GetPubKey(), CBLSPubKey(), CBLSSignature(), baseH + 1, pPrev, &testCoins, true, err));
        BOOST_CHECK_EQUAL(err, "bad-txns-minerreg-missing-bls");
        BOOST_CHECK(!CheckRegistrationBlsBinding(kA.GetPubKey(), pkBlsA, popWrong, baseH + 1, pPrev, &testCoins, true, err));
        BOOST_CHECK_EQUAL(err, "bad-txns-minerreg-bls-pop");

        // PoW registration puzzle hash commits to BLS public key under V4
        const uint256 ch = pPrev->GetBlockHash();
        const uint256 targetMiner = GetMinerPoWLimit(Params().NetworkIDString(), true);
        const uint32_t nonceBound = SolveMinerPoWForTest(kA.GetPubKey(), ch, targetMiner, true, &pkBlsA);
        BOOST_CHECK(ComputeMinerPoWRegistrationHash(nonceBound, ch, kA.GetPubKey(), true, &pkBlsA) <= targetMiner);
        BOOST_CHECK(ComputeMinerPoWRegistrationHash(nonceBound, ch, kA.GetPubKey(), true, &pkBlsB) !=
                    ComputeMinerPoWRegistrationHash(nonceBound, ch, kA.GetPubKey(), true, &pkBlsA));
    }

    // 2. H-6: Fork-safe pinned BLS lookup + UnpinBLSPubKeysAtHeight
    {
        CKey ecdsaA, ecdsaB;
        ecdsaA.MakeNewKey(true);
        ecdsaB.MakeNewKey(true);
        CBLSSecretKey blsSk;
        blsSk.MakeNewKey();
        CBLSPubKey blsPk(blsSk);

        CBlockIndex* pActiveIdx = appendTestBlock({});
        const uint256 activeHash = pActiveIdx->GetBlockHash();

        CBlockIndex sideIdx;
        sideIdx.nHeight = pActiveIdx->nHeight;
        uint256 sideHash = uint256S("0x7777777777777777777777777777777777777777777777777777777777777777");
        sideIdx.phashBlock = &sideHash;

        // Pin on disconnected fork block first: active-chain predicate ignores fork-only pin
        BOOST_REQUIRE(PinBLSPubKey(ecdsaA.GetPubKey(), blsPk, sideIdx.nHeight, sideHash));
        const PinActivePredicate activePred = MakeActiveChainPinPredicate(pPrev);
        BOOST_CHECK(!IsBLSPubKeyPinnedToOther(ecdsaB.GetPubKey(), blsPk, activePred));
        UnpinBLSPubKeysAtHeight(sideIdx.nHeight, sideHash);

        // Pin on active chain block: another ECDSA owner is blocked; same ECDSA owner is allowed; unpin cleans up
        BOOST_REQUIRE(PinBLSPubKey(ecdsaA.GetPubKey(), blsPk, pActiveIdx->nHeight, activeHash, activePred));
        BOOST_CHECK(!IsBLSPubKeyPinnedToOther(ecdsaA.GetPubKey(), blsPk, activePred));
        BOOST_CHECK(IsBLSPubKeyPinnedToOther(ecdsaB.GetPubKey(), blsPk, activePred));
        UnpinBLSPubKeysAtHeight(pActiveIdx->nHeight, activeHash);
        BOOST_CHECK(!IsBLSPubKeyPinnedToOther(ecdsaB.GetPubKey(), blsPk, activePred));
    }

    // 3. H-5: Historical CLTV time-travel in GetAdamMinerPool when testCoins tip is strictly ahead and spent the coin
    {
        CKey clKey;
        clKey.MakeNewKey(true);
        CMutableTransaction mtxCl;
        mtxCl.vin.push_back(CTxIn(COutPoint(uint256S("9900000000000000000000000000000000000000000000000000000000000000"), 1)));
        const int hReg = pPrev->nHeight + 1;
        mtxCl.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, BuildTestCoinLockScript(clKey, hReg + 2900)));
        CTransaction txCl(mtxCl);
        COutPoint outCl(txCl.GetHash(), 0);
        testCoins.AddCoin(outCl, Coin(txCl.vout[0], hReg, false, false), true);
        vAddedCoins.push_back(outCl);
        appendTestBlock({txCl});
        const int hPast = pPrev->nHeight;

        appendTestBlock({});
        testCoins.SpendCoin(outCl); // Spend after hPast while tip advances ahead of hPast

        std::set<CPubKey> pastClKeys;
        GetAdamMinerPool(hPast, &testCoins, nullptr, &pastClKeys);
        BOOST_CHECK(pastClKeys.count(clKey.GetPubKey()) == 1U);
    }

    // 4. C-5a & M-5: VRF key maturity (ADAM_VRF_KEY_MATURITY = 100) + Open-Entry activation check
    {
        for (const COutPoint& outp : vAddedCoins) {
            if (testCoins.HaveCoin(outp)) testCoins.SpendCoin(outp);
        }
        vAddedCoins.clear();
        for (const uint256& h : vHashes) {
            mapBlockIndex.erase(h);
        }
        vHashes.clear();
        for (CBlockIndex* pIdx : vIndices) {
            delete pIdx;
        }
        vIndices.clear();
        chainActive.SetTip(pOrigTip);
        if (tempGenesis) {
            mapBlockIndex.erase(tempGenesisHash);
            tempGenesis.reset();
        }
        BumpMasternodePingStateGen();

        ScopedChainParams tnParams(CBaseChainParams::TESTNET);
        CCoinsView dummyViewTn;
        CCoinsViewCache testCoinsTn(&dummyViewTn);
        std::unique_ptr<CBlockIndex> tnGenesis(new CBlockIndex(Params().GenesisBlock()));
        tnGenesis->nHeight = 0;
        tnGenesis->pprev = nullptr;
        uint256 tnGenHash = Params().GenesisBlock().GetHash();
        auto tnIns = mapBlockIndex.insert(std::make_pair(tnGenHash, tnGenesis.get()));
        tnGenesis->phashBlock = &(tnIns.first->first);
        tnGenesis->BuildSkip();
        chainActive.SetTip(tnGenesis.get());
        pPrev = tnGenesis.get();

        auto appendTnBlock = [&](const std::vector<CTransaction>& extraTxs) -> CBlockIndex* {
            const int h = pPrev->nHeight + 1;
            CBlock blk;
            blk.nVersion = 12;
            blk.hashPrevBlock = pPrev->GetBlockHash();
            blk.nTime = pPrev->nTime + 30;
            blk.nNonce = (uint32_t)h;
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk.vtx.push_back(cb);
            for (const auto& tx : extraTxs) blk.vtx.push_back(tx);
            blk.hashMerkleRoot = BlockMerkleRoot(blk);
            uint256 bh = blk.GetHash();
            vHashes.push_back(bh);
            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = pPrev;
            auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            CDiskBlockPos pos;
            WriteBlockToDisk(blk, pos);
            pIdx->nFile = pos.nFile;
            pIdx->nDataPos = pos.nPos;
            pIdx->nStatus |= BLOCK_HAVE_DATA;
            vIndices.push_back(pIdx);
            chainActive.SetTip(pIdx);
            testCoinsTn.SetBestBlock(bh);
            RegisterBlockMasternodeActivity(blk, h, &testCoinsTn);
            pPrev = pIdx;
            return pIdx;
        };

        CKey matKey, newKey1, newKey2;
        matKey.MakeNewKey(true);
        newKey1.MakeNewKey(true);
        newKey2.MakeNewKey(true);

        const int hMat = pPrev->nHeight + 1;
        CMutableTransaction mtxMat;
        mtxMat.vin.push_back(CTxIn(COutPoint(uint256S("9911000000000000000000000000000000000000000000000000000000000000"), 1)));
        mtxMat.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, BuildTestCoinLockScript(matKey, hMat + 2900)));
        CTransaction txMat(mtxMat);
        COutPoint outMat(txMat.GetHash(), 0);
        testCoinsTn.AddCoin(outMat, Coin(txMat.vout[0], hMat, false, false), true);
        vAddedCoins.push_back(outMat);
        appendTnBlock({txMat});

        // Advance to 202 on Testnet (nAdamDualTrackHeight=200, UPGRADE_MODELD=500 -> nRegPeriod=2880, matKey age >= 100)
        while (pPrev->nHeight < 202) {
            appendTnBlock({});
        }
        for (int idx = 0; idx < 2; ++idx) {
            CKey& nk = (idx == 0) ? newKey1 : newKey2;
            const int hNew = pPrev->nHeight + 1;
            CMutableTransaction mtxNew;
            mtxNew.vin.push_back(CTxIn(COutPoint(uint256S("9922000000000000000000000000000000000000000000000000000000000000"), (uint32_t)idx)));
            mtxNew.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, BuildTestCoinLockScript(nk, hNew + 2900)));
            CTransaction txNew(mtxNew);
            COutPoint outNew(txNew.GetHash(), 0);
            testCoinsTn.AddCoin(outNew, Coin(txNew.vout[0], hNew, false, false), true);
            vAddedCoins.push_back(outNew);
            appendTnBlock({txNew});
        }
        const int evalH = pPrev->nHeight + 1;
        std::set<CPubKey> maturedSet;
        GetAdamMinerPool(pPrev->nHeight, &testCoinsTn, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &maturedSet);
        BOOST_CHECK(maturedSet.count(matKey.GetPubKey()) == 1U);
        BOOST_CHECK(maturedSet.count(newKey1.GetPubKey()) == 0U);
        BOOST_CHECK(maturedSet.count(newKey2.GetPubKey()) == 0U);

        for (int round = 0; round < 30; ++round) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("1212121212121212121212121212121212121212121212121212121212121212") << round;
            std::vector<CPubKey> vElected;
            CPubKey coord;
            BOOST_REQUIRE(SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, evalH, &testCoinsTn));
            BOOST_CHECK(maturedSet.count(coord) == 1U);
            BOOST_CHECK(coord != newKey1.GetPubKey());
            BOOST_CHECK(coord != newKey2.GetPubKey());
        }

        const Consensus::Params& mainParams = Params(CBaseChainParams::MAIN).GetConsensus();
        BOOST_CHECK(mainParams.IsAdamOpenEntryActive(mainParams.nAdamOpenEntryHeight));
        BOOST_CHECK(!mainParams.IsAdamOpenEntryActive(mainParams.nAdamOpenEntryHeight - 1));

        for (const COutPoint& outp : vAddedCoins) {
            if (testCoinsTn.HaveCoin(outp)) testCoinsTn.SpendCoin(outp);
        }
        vAddedCoins.clear();
        for (const uint256& h : vHashes) {
            mapBlockIndex.erase(h);
        }
        vHashes.clear();
        for (CBlockIndex* pIdx : vIndices) {
            delete pIdx;
        }
        vIndices.clear();
        chainActive.SetTip(nullptr);
        mapBlockIndex.erase(tnGenHash);
        BumpMasternodePingStateGen();
    }

    for (const COutPoint& outp : vAddedCoins) {
        if (testCoins.HaveCoin(outp)) testCoins.SpendCoin(outp);
    }
    for (const uint256& h : vHashes) {
        mapBlockIndex.erase(h);
    }
    for (CBlockIndex* pIdx : vIndices) {
        delete pIdx;
    }
    chainActive.SetTip(pOrigTip);
    if (tempGenesis) {
        mapBlockIndex.erase(tempGenesisHash);
    }
    BumpMasternodePingStateGen();
    InitializeAdamMinerRegCache(true);
}

BOOST_AUTO_TEST_CASE(remote_coordinator_proposal_guard_release_and_lease_ttl)
{
    ScopedChainParams mainParams(CBaseChainParams::MAIN);
    CKey kCoord;
    kCoord.MakeNewKey(true);
    const CPubKey coordPk = kCoord.GetPubKey();
    const uint256 parentHash = uint256S("0101010101010101010101010101010101010101010101010101010101010101");
    const uint256 req1 = uint256S("1111111111111111111111111111111111111111111111111111111111111111");
    const uint256 req2 = uint256S("2222222222222222222222222222222222222222222222222222222222222222");
    const uint256 req3 = uint256S("3333333333333333333333333333333333333333333333333333333333333333");

    // 1. Failing path (destructor runs without MarkProduced) must immediately release in-flight slot
    {
        RemoteCoordinatorProposalGuard failGuard;
        BOOST_REQUIRE(failGuard.Acquire(parentHash, coordPk, req1));
        BOOST_CHECK(!AcquireCoordinatorProposalSlot(parentHash, coordPk, req2, false));
    }
    BOOST_CHECK(AcquireCoordinatorProposalSlot(parentHash, coordPk, req2, false));

    // 2. Producing path commits 30s produced lease: competing request blocked, same reqId idempotent,
    //    and competing request allowed once 30s lease TTL elapses.
    {
        RemoteCoordinatorProposalGuard prodGuard;
        BOOST_REQUIRE(prodGuard.Acquire(parentHash, coordPk, req2));
        prodGuard.MarkProduced();
    }
    BOOST_CHECK(AcquireCoordinatorProposalSlot(parentHash, coordPk, req2, false));
    BOOST_CHECK(!AcquireCoordinatorProposalSlot(parentHash, coordPk, req3, false));

    AgeCoordinatorProposalSlotsForTest(31);
    BOOST_CHECK(AcquireCoordinatorProposalSlot(parentHash, coordPk, req3, false));
}

BOOST_AUTO_TEST_CASE(proposeblock_r35_1_omission_veto_5s_test)
{
    const CBaseChainParams::Network prevNet = Params().NetworkID();
    CBlockIndex* const pOrigTip = chainActive.Tip();
    BOOST_REQUIRE(pOrigTip != nullptr);
    const uint256 origBestCoins = pcoinsTip->GetBestBlock();
    const bool hadBlsArg = mapArgs.count("-masternodeblsprivkey") > 0;
    const std::string prevBlsArg = hadBlsArg ? mapArgs["-masternodeblsprivkey"] : "";
    const bool origPingInit = g_fMasternodePingStateInitialized;
    std::unique_ptr<dip3::CDeterministicMNManager> origMgr = std::move(deterministicMNManager);

    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vCoins;
    std::vector<uint256> vPropBlocks;
    uint256 testPrevHash;
    struct R35Cleanup {
        CBaseChainParams::Network net;
        CBlockIndex* tip;
        uint256 bestCoins;
        bool hadArg;
        std::string prevArg;
        bool pingInit;
        std::unique_ptr<dip3::CDeterministicMNManager>& origM;
        std::vector<uint256>& hashes;
        std::vector<CBlockIndex*>& idxs;
        std::vector<COutPoint>& coins;
        std::vector<uint256>& propBlocks;
        uint256& prevHashRef;
        ~R35Cleanup() {
            if (hadArg) {
                mapArgs["-masternodeblsprivkey"] = prevArg;
            } else {
                mapArgs.erase("-masternodeblsprivkey");
            }
            {
                LOCK(cs_adam_solutions);
                if (!prevHashRef.IsNull()) {
                    mapAdamSolutionsCache.erase(prevHashRef);
                    mapAdamSolutionFirstSeen.erase(prevHashRef);
                }
            }
            {
                LOCK(cs_quorum_sigs);
                for (const uint256& bh : propBlocks) {
                    mapQuorumBlockSigs.erase(bh);
                }
            }
            for (const COutPoint& o : coins) {
                if (pcoinsTip->HaveCoin(o)) pcoinsTip->SpendCoin(o);
            }
            pcoinsTip->SetBestBlock(bestCoins);
            chainActive.SetTip(tip);
            {
                LOCK(cs_adam_seeds);
                LOCK(cs_recent_vrf_proofs);
                for (const uint256& h : hashes) {
                    mapAdamSeeds.erase(h);
                    mapRecentVRFProofs.erase(h);
                }
            }
            for (size_t i = 0; i < idxs.size(); ++i) {
                mapBlockIndex.erase(hashes[i]);
                delete idxs[i];
            }
            llmq::ClearQuorumCache();
            BumpMasternodePingStateGen();
            g_fMasternodePingStateInitialized = pingInit;
            deterministicMNManager = std::move(origM);
            InitializeAdamMinerRegCache(true);
            SelectParams(net);
        }
    } cleanup{prevNet, pOrigTip, origBestCoins, hadBlsArg, prevBlsArg, origPingInit, origMgr, vHashes, vIndices, vCoins, vPropBlocks, testPrevHash};

    SelectParams(CBaseChainParams::REGTEST);
    g_fMasternodePingStateInitialized = true;
    deterministicMNManager.reset(new dip3::CDeterministicMNManager(1 << 20, true, true));

    CBlockIndex* pPrev = pOrigTip;
    const int64_t baseTime = GetTime() - 2500;
    auto pushRegBlock = [&](const std::vector<CTransaction>& txs) -> CBlockIndex* {
        const int h = pPrev->nHeight + 1;
        CBlock blk;
        blk.nVersion = 12;
        blk.hashPrevBlock = pPrev->GetBlockHash();
        blk.nTime = static_cast<unsigned int>(baseTime + h * 2);
        blk.nBits = 0x207fffff;
        blk.nNonce = static_cast<uint32_t>(h);
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk.vtx.push_back(cb);
        for (const auto& t : txs) blk.vtx.push_back(t);
        blk.hashMerkleRoot = BlockMerkleRoot(blk);
        vHashes.push_back(blk.GetHash());
        {
            LOCK(cs_recent_vrf_proofs);
            mapRecentVRFProofs[vHashes.back()] = blk.vAdamVRFProof;
        }
        CBlockIndex* pIdx = new CBlockIndex(blk);
        pIdx->nHeight = h;
        pIdx->pprev = pPrev;
        auto ins = mapBlockIndex.emplace(vHashes.back(), pIdx);
        pIdx->phashBlock = &(ins.first->first);
        pIdx->BuildSkip();
        vIndices.push_back(pIdx);
        chainActive.SetTip(pIdx);
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk, pIdx, st, *pcoinsTip, false));
        pPrev = pIdx;
        return pIdx;
    };

    while (pPrev->nHeight < 797) {
        pushRegBlock({});
    }

    std::map<CPubKey, CBLSSecretKey> mapOpSks;
    std::vector<CTransaction> regTxs;
    for (int i = 0; i < 20; ++i) {
        CKey owner = GetAdamDeterministicKey(i);
        const CPubKey pk = owner.GetPubKey();
        const CScript spk = GetScriptForDestination(pk.GetID());
        COutPoint colOut(uint256S("d100000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
        COutPoint feeOut(uint256S("d200000000000000000000000000000000000000000000000000000000000000"), static_cast<uint32_t>(i));
        pcoinsTip->AddCoin(colOut, Coin(CTxOut(4200 * COIN, spk), 10, false, false), true);
        pcoinsTip->AddCoin(feeOut, Coin(CTxOut(1 * COIN, spk), 10, false, false), true);
        vCoins.push_back(colOut);
        vCoins.push_back(feeOut);

        CBLSSecretKey opSk;
        opSk.MakeNewKey();
        mapOpSks[pk] = opSk;

        CMutableTransaction mtx;
        mtx.vin.push_back(CTxIn(feeOut));
        dip3::CProRegTx ptx;
        ptx.nVersion = 2;
        ptx.collateralOutpoint = colOut;
        ptx.addr = LookupNumeric("198.51.100.70", 27900 + i);
        ptx.pubKeyOwner = pk;
        ptx.blsPubKeyOperator = CBLSPubKey(opSk);
        BOOST_REQUIRE(ptx.blsPoP.SignPoP(opSk));
        ptx.keyIDVoting = pk.GetID();
        ptx.nOperatorReward = 0;
        ptx.scriptPayout = spk;
        ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
        BOOST_REQUIRE(owner.Sign(ptx.GetSignHash(), ptx.vchSig));
        mtx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));
        regTxs.push_back(CTransaction(mtx));
    }
    pushRegBlock(regTxs); // height 798 (<= snapHeight 798 for epoch 800)
    while (pPrev->nHeight < 805) {
        pushRegBlock({});
    }
    pcoinsTip->SetBestBlock(pPrev->GetBlockHash());
    InitializeAdamMinerRegCache(true);

    testPrevHash = pPrev->GetBlockHash();
    const int nH = pPrev->nHeight + 1; // 806
    const uint256 adamSeed = GetAdamSeed(pPrev);
    BOOST_REQUIRE(!adamSeed.IsNull());

    llmq::CQuorum quorum = llmq::GetActiveQuorum(nH, pPrev, pcoinsTip);
    BOOST_REQUIRE_GE(quorum.members.size(), 2U);
    const CBLSSecretKey& localQuorumOpSk = mapOpSks.at(quorum.members[0].pubKeyMasternode);
    mapArgs["-masternodeblsprivkey"] = HexStr(localQuorumOpSk.begin(), localQuorumOpSk.end());

    std::vector<CPubKey> vExpectedMiners;
    std::vector<CPubKey> vStandbyMiners;
    CPubKey expectedCoordinator;
    BOOST_REQUIRE(SelectAdamNodes(adamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator, nH, pcoinsTip, &vStandbyMiners));
    BOOST_REQUIRE_GE(vExpectedMiners.size(), 2U);
    const CBLSSecretKey& coordOpSk = mapOpSks.at(expectedCoordinator);

    auto buildValidSol = [&](int idx, const CPubKey& minerPk) -> std::vector<unsigned char> {
        const CBLSSecretKey& opSk = mapOpSks.at(minerPk);
        arith_uint256 bnTarget;
        bnTarget.SetCompact(0x207fffff);
        unsigned char inputBuf[104];
        const unsigned int keyLen = minerPk.size();
        std::memcpy(inputBuf, adamSeed.begin(), 32);
        inputBuf[32] = static_cast<unsigned char>(keyLen);
        std::memcpy(inputBuf + 33, minerPk.begin(), keyLen);
        const size_t inputLen = 37 + keyLen;

        int algo1 = -1, algo2 = -1, algo3 = -1;
        GetAdam3PermutationAlgos(testPrevHash, minerPk, algo1, algo2, algo3);
        const int i_factor = idx + 1;

        for (uint32_t solNonce = 0; solNonce < 256; ++solNonce) {
            WriteLE32(inputBuf + 33 + keyLen, solNonce);
            uint256 hash3 = CalculateAdamPuzzleHash(algo3, inputBuf, inputBuf + inputLen);
            uint256 m1 = ArithToUint256(UintToArith256(hash3) * i_factor);
            uint256 hash2 = CalculateAdamPuzzleHash(algo2, m1.begin(), m1.begin() + 32);
            uint256 m2 = ArithToUint256(UintToArith256(hash2) * i_factor);
            uint256 puzzleHash = CalculateAdamPuzzleHash(algo1, m2.begin(), m2.begin() + 32);
            if (UintToArith256(puzzleHash) <= bnTarget) {
                std::vector<unsigned char> vchSig;
                BOOST_REQUIRE(SignBLSOperatorOnly(puzzleHash, opSk, vchSig, GetBLSDomainTag(BLSDomain::ADAM_SOL, true)));
                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                ss << solNonce << vchSig;
                std::vector<unsigned char> sol(ss.begin(), ss.end());
                BOOST_REQUIRE(VerifyAdamSolution(testPrevHash, adamSeed, minerPk, sol, 0x207fffff, 12, nH, idx, pcoinsTip));
                return sol;
            }
        }
        BOOST_FAIL("Failed to find valid test ADAM puzzle solution within 256 nonces");
        return {};
    };

    const std::vector<unsigned char> omittedPrimarySol = buildValidSol(0, vExpectedMiners[0]);
    std::vector<std::vector<unsigned char>> blockSols(vExpectedMiners.size());
    std::vector<CPubKey> solvedMiners;
    for (size_t i = 1; i < vExpectedMiners.size(); ++i) {
        blockSols[i] = buildValidSol(static_cast<int>(i), vExpectedMiners[i]);
        solvedMiners.push_back(vExpectedMiners[i]);
    }

    auto makeOmittedProposal = [&](uint32_t nNonce) -> CBlock {
        CBlock blk;
        blk.nVersion = 12;
        blk.hashPrevBlock = testPrevHash;
        blk.nTime = pPrev->GetBlockTime() + 15;
        blk.nBits = GetNextWorkRequired(pPrev, &blk);
        blk.nNonce = nNonce;
        blk.vAdamMiners = vExpectedMiners;
        blk.vAdamSolutions = blockSols; // Slot 0 omitted!

        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << nH << OP_0));
        cb.vout.push_back(CTxOut(0, GetScriptForDestination(expectedCoordinator.GetID())));
        FillBlockPayee(cb, pPrev, false, &solvedMiners, &expectedCoordinator);
        blk.vtx.push_back(CTransaction(cb));
        blk.hashMerkleRoot = BlockMerkleRoot(blk);

        BOOST_REQUIRE(SignBLSOperatorOnly(adamSeed, coordOpSk, blk.vAdamVRFProof, GetBLSDomainTag(BLSDomain::ADAM_VRF, true)));
        BOOST_REQUIRE(SignBLSOperatorOnly(blk.GetHash(), coordOpSk, blk.vAdamCoordinatorSig, GetBLSDomainTag(BLSDomain::ADAM_COORD, true)));
        vPropBlocks.push_back(blk.GetHash());
        return blk;
    };

    auto pushMsgToNode = [&](CNode& node, const std::string& cmd, const std::string& payload) {
        CMessageHeader hdr(Params().MessageStart(), cmd.c_str(), static_cast<unsigned int>(payload.size()));
        uint256 h = Hash(payload.begin(), payload.end());
        std::memcpy(hdr.pchChecksum, h.begin(), CMessageHeader::CHECKSUM_SIZE);
        CDataStream wire(SER_NETWORK, PROTOCOL_VERSION);
        wire << hdr;
        wire.write(payload.data(), payload.size());
        const std::string wireStr = wire.str();
        bool complete = false;
        BOOST_REQUIRE(node.ReceiveMsgBytes(wireStr.data(), static_cast<unsigned int>(wireStr.size()), complete));
        BOOST_REQUIRE(complete);
        node.QueueCompleteRecvMessages(connman->GetReceiveFloodSize());
        std::atomic<bool> interruptDummy(false);
        while (GetNodeSignals().ProcessMessages(&node, *connman, interruptDummy)) {}
    };

    CService dummySvc;
    Lookup("198.51.100.77:27999", dummySvc, 27999, false);
    CAddress peerAddr(dummySvc, NODE_NETWORK);
    CNode dummyNode(9101, NODE_NETWORK, 0, INVALID_SOCKET, peerAddr, 0, 0, "", true);
    dummyNode.fV2AutoDetect = false;
    dummyNode.fV2Transport = false;
    dummyNode.fWhitelisted = true;
    dummyNode.SetSendVersion(PROTOCOL_VERSION);
    GetNodeSignals().InitializeNode(&dummyNode, *connman);
    dummyNode.nVersion = PROTOCOL_VERSION;
    dummyNode.SetRecvVersion(PROTOCOL_VERSION);
    dummyNode.fSuccessfullyConnected = true;

    // Case A: Local valid primary solution seen >= 5s ago -> PROPOSEBLOCK accepted & seen, but local quorum signing VETOED
    {
        LOCK(cs_adam_solutions);
        mapAdamSolutionsCache[testPrevHash][vExpectedMiners[0]] = omittedPrimarySol;
        mapAdamSolutionFirstSeen[testPrevHash][vExpectedMiners[0]] = GetTime() - 6;
    }

    const CBlock blkVetoed = makeOmittedProposal(1001);
    const uint256 hashVetoed = blkVetoed.GetHash();
    {
        CBlockProposeMsg propMsg;
        propMsg.block = blkVetoed;
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << propMsg;
        pushMsgToNode(dummyNode, NetMsgType::PROPOSEBLOCK, ss.str());
    }
    CNodeStateStats stats;
    BOOST_REQUIRE(GetNodeStateStats(dummyNode.GetId(), stats));
    BOOST_CHECK_EQUAL(stats.nMisbehavior, 0);
    {
        LOCK(cs_quorum_sigs);
        BOOST_CHECK_EQUAL(mapQuorumBlockSigs.count(hashVetoed), 0U);
    }

    // Confirm blkVetoed entered setSeenProposedBlocks by submitting another quorum member's valid share for it
    {
        CQuorumSigShareMsg shareMsg;
        shareMsg.blockHash = hashVetoed;
        shareMsg.collateralOutpoint = quorum.members[1].collateralOutpoint;
        const CBLSSecretKey& member1OpSk = mapOpSks.at(quorum.members[1].pubKeyMasternode);
        BOOST_REQUIRE(SignBLSOperatorOnly(hashVetoed, member1OpSk, shareMsg.vchSig, GetBLSDomainTag(BLSDomain::LLMQ_BLOCK, true)));
        CDataStream ssShare(SER_NETWORK, PROTOCOL_VERSION);
        ssShare << shareMsg;
        pushMsgToNode(dummyNode, NetMsgType::QUORUMSIGSHARE, ssShare.str());

        LOCK(cs_quorum_sigs);
        BOOST_REQUIRE(mapQuorumBlockSigs.count(hashVetoed) > 0U);
        BOOST_CHECK_EQUAL(mapQuorumBlockSigs[hashVetoed].count(quorum.members[0].collateralOutpoint), 0U);
        BOOST_CHECK_EQUAL(mapQuorumBlockSigs[hashVetoed].count(quorum.members[1].collateralOutpoint), 1U);
    }

    // Case B: Local valid primary solution seen < 5s ago -> grace window allows quorum signing
    {
        LOCK(cs_adam_solutions);
        mapAdamSolutionFirstSeen[testPrevHash][vExpectedMiners[0]] = GetTime() - 1;
    }
    const CBlock blkAllowed = makeOmittedProposal(1002);
    const uint256 hashAllowed = blkAllowed.GetHash();
    {
        CBlockProposeMsg propMsg;
        propMsg.block = blkAllowed;
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << propMsg;
        pushMsgToNode(dummyNode, NetMsgType::PROPOSEBLOCK, ss.str());
    }
    {
        LOCK(cs_quorum_sigs);
        BOOST_REQUIRE(mapQuorumBlockSigs.count(hashAllowed) > 0U);
        BOOST_CHECK_EQUAL(mapQuorumBlockSigs[hashAllowed].count(quorum.members[0].collateralOutpoint), 1U);
    }

    bool fUpdate = false;
    GetNodeSignals().FinalizeNode(dummyNode.GetId(), fUpdate);
}

BOOST_AUTO_TEST_SUITE_END()
