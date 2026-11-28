// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "blocksignature.h"
#include "evo/deterministicmns.h"
#include "kernel.h"
#include "llmq.h"
#include "main.h"
#include "chainparams.h"
#include "masternode-payments.h"
#include "masternode.h"
#include "messagesigner.h"
#include "core_io.h"
#include "consensus/merkle.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "policy/policy.h"
#include "script/interpreter.h"
#include "script/mescal.h"
#include "script/sign.h"
#include "script/standard.h"
#include "stakeinput.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif
#include "test_kristatech.h"
#include <boost/test/unit_test.hpp>

namespace {
struct ScopedMpaChainParams {
    CBaseChainParams::Network prevNetwork;
    explicit ScopedMpaChainParams(CBaseChainParams::Network network) : prevNetwork(Params().NetworkID()) {
        SelectParams(network);
    }
    ~ScopedMpaChainParams() {
        SelectParams(prevNetwork);
    }
};
} // namespace

BOOST_FIXTURE_TEST_SUITE(mpa_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(mpa_weight_pos_test)
{
    // Test that for heights below activation height, we always get PoS baseline weight (original amount)
    COutPoint prevout(uint256S("123"), 0);
    CBlockIndex indexPrev;
    indexPrev.nHeight = 10; // below activation
    int weightType = -1;
    CAmount weight = CalculateMPAWeight(prevout, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weight, 100 * COIN);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
}

BOOST_AUTO_TEST_CASE(mpa_weight_decay_and_decay_limit_test)
{
    // Set up active height above Regtest activation height (300)
    CBlockIndex indexPrev;
    indexPrev.nHeight = 500;
    int weightType = -1;

    // Test default PoS at active height for normal destination
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();
    CTxDestination dest = pubKey.GetID();
    
    COutPoint prevout(uint256S("abc"), 0);
    CAmount weight = CalculateMPAWeight(prevout, 50 * COIN, GetTime(), &indexPrev, weightType);
    // Since prevout is not masternode collateral, doesn't have a timelock, and hasn't burned, it must return 50 * COIN
    BOOST_CHECK_EQUAL(weight, 50 * COIN);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);

    // Test outpoint Proof of Burn (PoB) decay logic
    COutPoint pobDecayOut(uint256S("0x5555666677778888555566667777888855556666777788885555666677778888"), 0);
    AddOutpointBurnToCache(pobDecayOut, 10 * COIN, 450);

    // Height 500: T = 50. Decay should be: 1.0 - 50 / nBurnDecayBlocks.
    int nBurnDecayBlocks = Params().GetConsensus().nBurnDecayBlocks;
    double decay = 1.0 - 50.0 / (double)nBurnDecayBlocks;
    weight = GetOutpointBurnWeight(pobDecayOut, 500);
    BOOST_CHECK_EQUAL(weight, (CAmount)(10 * COIN * 5.0 * decay));

    // Height past decay limit: T = nBurnDecayBlocks + 50. Weight must be 0
    weight = GetOutpointBurnWeight(pobDecayOut, 450 + nBurnDecayBlocks + 50);
    BOOST_CHECK_EQUAL(weight, 0);
    RemoveOutpointBurnFromCache(pobDecayOut, 10 * COIN, 450);
}

BOOST_AUTO_TEST_CASE(mescal_drop_compilation_test)
{
    std::string jsonStr = R"({
        "basic": {
            "MyDrop": {
                "role": "drop"
            }
        },
        "contract": {
            "TestDrop": {
                "description": "Test drop contract",
                "actions": [
                    { "type": "basic", "name": "MyDrop" }
                ]
            }
        },
        "active_contract": "TestDrop"
    })";
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    BOOST_CHECK(errorStr.empty());
    BOOST_CHECK_EQUAL(ScriptToAsmStr(script), "OP_DROP");

    UniValue decompileResult = CMescal::Decompile(script, errorStr);
    BOOST_CHECK(errorStr.empty());
    // Compile decompiled result back
    CScript recompiled = CMescal::Compile(decompileResult.write(), errorStr);
    BOOST_CHECK(errorStr.empty());
    BOOST_CHECK_EQUAL(ScriptToAsmStr(recompiled), "OP_DROP");
}

BOOST_AUTO_TEST_CASE(treasury_reward_split_test)
{
    CAmount nBlockValActual = 17 * COIN;
    CAmount nTreasurySplit = nBlockValActual * 7 / 100;
    CAmount nNetPool = nBlockValActual - nTreasurySplit;

    BOOST_CHECK_EQUAL(nTreasurySplit, 119000000); // 17.0 * 0.07 = 1.19 COIN
    BOOST_CHECK_EQUAL(nNetPool, 1581000000);      // 17.0 * 0.93 = 15.81 COIN
}

BOOST_AUTO_TEST_CASE(taproot_rpc_hang_test)
{
    std::string jsonStr = R"({
        "basic": {
            "MyDrop": {"role": "drop"}
        },
        "contract": {
            "TestDrop": {
                "description": "Test drop contract",
                "actions": [{"type": "basic", "name": "MyDrop"}]
            }
        },
        "active_contract": "TestDrop"
    })";
    std::string internalPubKeyHex = "697caf5a1ea29fa2e41e32ce0514e6c11d0b549be82136369ce8d1d435e9e731";
    std::vector<unsigned char> internalPubKeyBytes = ParseHex(internalPubKeyHex);
    CXOnlyPubKey P(internalPubKeyBytes.data(), 32);
    BOOST_CHECK(P.IsFullyValid());

    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    BOOST_CHECK(errorStr.empty());

    uint8_t leaf_version = 0xc0;

    unsigned char tag_hash[32];
    CSHA256 shaTapLeaf;
    shaTapLeaf.Write((const unsigned char*)"TapLeaf", 7);
    shaTapLeaf.Finalize(tag_hash);

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << leaf_version;
    ss << script;

    CSHA256 sha;
    sha.Write(tag_hash, 32);
    sha.Write(tag_hash, 32);
    if (ss.size() > 0) {
        sha.Write((const unsigned char*)&ss[0], ss.size());
    }
    unsigned char leaf_hash_bytes[32];
    sha.Finalize(leaf_hash_bytes);
    uint256 leaf_hash(std::vector<unsigned char>(leaf_hash_bytes, leaf_hash_bytes + 32));

    bool parity = false;
    CPubKey Q = P.CreateTapTweak(&leaf_hash, &parity);
    BOOST_CHECK(Q.IsValid());
}

BOOST_AUTO_TEST_CASE(mpa_weight_pol_and_pob_integration_test)
{
    // Height 5500 -> nHeight = 5501 (above UPGRADE_POMBL activation height 200 on Mainnet)
    CBlockIndex indexPrev;
    indexPrev.nHeight = 5500;
    const int nHeight = indexPrev.nHeight + 1;

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();
    CTxDestination dest = pubKey.GetID();

    // 1. Proof of Lock (PoL) with CLTV script in pcoinsTip
    int64_t lockHeightHalf = nHeight + 25000;
    CScript polScriptHalf = CScript() << CScriptNum(lockHeightHalf) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                      << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint polOutHalf(uint256S("1111000011110000111100001111000011110000111100001111000011110001"), 0);
    pcoinsTip->AddCoin(polOutHalf, Coin(CTxOut(100 * COIN, polScriptHalf), 5400, false, false), true);

    int weightType = -1;
    CAmount weightHalf = CalculateMPAWeight(polOutHalf, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POL);
    BOOST_CHECK_GT(weightHalf, 100 * COIN);

    // 2. Proof of Lock (PoL) with longer CLTV lock script (3-byte locktime push > 2-byte locktime push)
    int64_t lockHeightMax = nHeight + 60000;
    CScript polScriptMax = CScript() << CScriptNum(lockHeightMax) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint polOutMax(uint256S("1111000011110000111100001111000011110000111100001111000011110002"), 0);
    pcoinsTip->AddCoin(polOutMax, Coin(CTxOut(100 * COIN, polScriptMax), 5400, false, false), true);

    CAmount weightMax = CalculateMPAWeight(polOutMax, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POL);
    BOOST_CHECK_GT(weightMax, weightHalf);

    // 3. Proof of Burn (PoB): V3 COutPoint-bound vs pre-V3 address-bound integration
    CScript p2pkhScript = GetScriptForDestination(dest);
    COutPoint pobOut(uint256S("1111000011110000111100001111000011110000111100001111000011110003"), 0);
    pcoinsTip->AddCoin(pobOut, Coin(CTxOut(100 * COIN, p2pkhScript), 5400, false, false), true);
    COutPoint otherSameAddrOut(uint256S("1111000011110000111100001111000011110000111100001111000011110004"), 0);
    pcoinsTip->AddCoin(otherSameAddrOut, Coin(CTxOut(100 * COIN, p2pkhScript), 5400, false, false), true);

    // 3a. Under UPGRADE_HARDENING_V3 (H-1): PoB binds strictly per-COutPoint, NOT per-address
    AddOutpointBurnToCache(pobOut, 20 * COIN, 5451);
    const CAmount v3PobWeight = CalculateMPAWeight(pobOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POB);
    BOOST_CHECK_GT(v3PobWeight, 100 * COIN);
    int unrelatedType = -1;
    const CAmount unrelatedWeight = CalculateMPAWeight(otherSameAddrOut, 100 * COIN, GetTime(), &indexPrev, unrelatedType);
    BOOST_CHECK_EQUAL(unrelatedType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(unrelatedWeight, 100 * COIN);
    RemoveOutpointBurnFromCache(pobOut, 20 * COIN, 5451);
    BOOST_CHECK_EQUAL(CalculateMPAWeight(pobOut, 100 * COIN, GetTime(), &indexPrev, weightType), 100 * COIN);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    pcoinsTip->SpendCoin(otherSameAddrOut);

    // 5. C-04: OP_CHECKSEQUENCEVERIFY PoL decays dynamically with remaining relative lock blocks (coin height -> chain height)
    {
        CScript csvScript = CScript() << CScriptNum(1000) << OP_CHECKSEQUENCEVERIFY << OP_DROP
                                      << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        COutPoint csvOut(uint256S("1111000011110000111100001111000011110000111100001111000011110077"), 0);
        pcoinsTip->AddCoin(csvOut, Coin(CTxOut(100 * COIN, csvScript), 5400, false, false), true);

        // At height 5501: 101 blocks elapsed since coin height 5400 -> remaining L = 5400 + 1000 - 5501 = 899
        int csvType = -1;
        CAmount wCsvActive = CalculateMPAWeight(csvOut, 100 * COIN, GetTime(), &indexPrev, csvType);
        BOOST_CHECK_EQUAL(csvType, MPA_WEIGHT_POL);
        BOOST_CHECK_EQUAL(wCsvActive, 100 * COIN + (CAmount)(((__int128)(100 * COIN) * 2 * 899) / 50000));

        // Once height >= 5400 + 1000, remaining L = 0 -> falls back to baseline PoS weight
        CBlockIndex idxExpired;
        idxExpired.nHeight = 6400; // next height 6401
        csvType = -1;
        CAmount wCsvExpired = CalculateMPAWeight(csvOut, 100 * COIN, GetTime(), &idxExpired, csvType);
        BOOST_CHECK_EQUAL(csvType, MPA_WEIGHT_POS);
        BOOST_CHECK_EQUAL(wCsvExpired, 100 * COIN);
        pcoinsTip->SpendCoin(csvOut);
    }

    pcoinsTip->SpendCoin(polOutHalf);
    pcoinsTip->SpendCoin(polOutMax);
    pcoinsTip->SpendCoin(pobOut);
}

BOOST_AUTO_TEST_CASE(mpa_weight_pom_test)
{
    CBlockIndex indexPrev;
    indexPrev.nHeight = 5500;

    CKey mnKey;
    mnKey.MakeNewKey(true);
    COutPoint mnOutpoint(uint256S("2222000022220000222200002222000022220000222200002222000022220001"), 0);

    CScript mnCoinScript = GetScriptForDestination(mnKey.GetPubKey().GetID());
    const CAmount nCollAmt = CMasternode::GetMasternodeNodeCollateral(5000);
    pcoinsTip->AddCoin(mnOutpoint, Coin(CTxOut(nCollAmt, mnCoinScript), 5000, false, false), true);

    BOOST_CHECK(RegisterOnChainMasternodePing(mnKey.GetPubKey(), mnOutpoint, 5000));
    BOOST_CHECK(RegisterOnChainMasternodePing(mnKey.GetPubKey(), mnOutpoint, 5500));

    int weightType = -1;
    CAmount pomWeight = CalculateMPAWeight(mnOutpoint, nCollAmt, GetTime(), &indexPrev, weightType);
    // Under UPGRADE_FAIRNESS, PoM is active with reduced alpha = 0.25 (max 1.25x uptime loyalty bonus)
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_GT(pomWeight, nCollAmt);
    BOOST_CHECK_LE(pomWeight, nCollAmt + nCollAmt / 4);

    // Half-limit uptime on Mainnet (limit = 100,000; coinHeight = 5,000; nHeight = 55,000 -> t_active = 50,000) => 1.125x
    indexPrev.nHeight = 54999;
    BOOST_CHECK(RegisterOnChainMasternodePing(mnKey.GetPubKey(), mnOutpoint, 54900));
    CAmount pomHalf = CalculateMPAWeight(mnOutpoint, nCollAmt, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_EQUAL(pomHalf, nCollAmt + nCollAmt / 8);

    // Saturated uptime (nHeight = 200,001 -> t_active >= limit) => capped at 1.25x
    indexPrev.nHeight = 200000;
    BOOST_CHECK(RegisterOnChainMasternodePing(mnKey.GetPubKey(), mnOutpoint, 199990));
    CAmount pomCap = CalculateMPAWeight(mnOutpoint, nCollAmt, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_EQUAL(pomCap, nCollAmt + nCollAmt / 4);

    mapMasternodeLastActiveHeight.erase(mnKey.GetPubKey());
    mapMasternodeActiveHeights.erase(mnKey.GetPubKey());
    mapMasternodeCollateralOutpoints.erase(mnKey.GetPubKey());
    BumpMasternodePingStateGen();
    pcoinsTip->SpendCoin(mnOutpoint);
}

BOOST_AUTO_TEST_CASE(llmq_quorum_election_and_signature_verify_test)
{
    ScopedMpaChainParams regtestParams(CBaseChainParams::REGTEST);

    // 1. Deterministic 5-member DKG quorum election on Regtest
    llmq::CQuorum dkgQuorum1 = llmq::RunDKG(500);
    llmq::CQuorum dkgQuorum2 = llmq::RunDKG(500);
    BOOST_CHECK_EQUAL(dkgQuorum1.nHeight, 500);
    BOOST_CHECK_EQUAL(dkgQuorum1.members.size(), 5U);
    BOOST_REQUIRE_EQUAL(dkgQuorum2.members.size(), 5U);
    for (size_t i = 0; i < 5; ++i) {
        BOOST_CHECK(dkgQuorum1.members[i].collateralOutpoint == dkgQuorum2.members[i].collateralOutpoint);
        BOOST_CHECK(dkgQuorum1.members[i].pubKeyMasternode == dkgQuorum2.members[i].pubKeyMasternode);
    }

    // 2. Test CQuorumSignature::Verify 3-of-5 BFT majority threshold
    llmq::CQuorum customQuorum;
    customQuorum.nHeight = 500;
    customQuorum.quorumHash = uint256S("3333333333333333333333333333333333333333333333333333333333333333");

    std::vector<CKey> memberKeys(5);
    for (int i = 0; i < 5; ++i) {
        memberKeys[i].MakeNewKey(true);
        COutPoint out(uint256S("4444444444444444444444444444444444444444444444444444444444444444"), i);
        customQuorum.members.emplace_back(out, memberKeys[i].GetPubKey());
    }

    uint256 blockHash = uint256S("5555555555555555555555555555555555555555555555555555555555555555");
    llmq::CQuorumSignature qsig;
    qsig.blockHash = blockHash;

    const std::string& llmqDst = GetBLSDomainTag(BLSDomain::LLMQ_BLOCK);

    // Sign with only 2 members (below 3/5 threshold) -> must fail
    for (int i = 0; i < 2; ++i) {
        std::vector<unsigned char> sig;
        CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(memberKeys[i], blockHash);
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[i], blsSk, sig, llmqDst));
        qsig.signatures.emplace_back(customQuorum.members[i].collateralOutpoint, sig);
    }
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // Duplicate signature from member 0 must be rejected
    qsig.signatures.push_back(qsig.signatures[0]);
    BOOST_CHECK(!qsig.Verify(customQuorum));
    qsig.signatures.pop_back();

    // Signature from a non-member must be rejected
    CKey outsiderKey;
    outsiderKey.MakeNewKey(true);
    std::vector<unsigned char> outsiderSig;
    CBLSSecretKey outsiderBlsSk = DeriveEphemeralBLSFromCKey(outsiderKey, blockHash);
    BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, outsiderKey, outsiderBlsSk, outsiderSig, llmqDst));
    qsig.signatures.emplace_back(COutPoint(uint256S("9999"), 0), outsiderSig);
    BOOST_CHECK(!qsig.Verify(customQuorum));
    qsig.signatures.pop_back();

    // Plain ECDSA signature without BLS12-381 component must be rejected even for a valid member
    std::vector<unsigned char> plainEcdsaSig;
    BOOST_CHECK(memberKeys[2].Sign(blockHash, plainEcdsaSig));
    qsig.signatures.emplace_back(customQuorum.members[2].collateralOutpoint, plainEcdsaSig);
    BOOST_CHECK(!qsig.Verify(customQuorum));
    qsig.signatures.pop_back();

    // Add 3rd valid sig (still below V4 4/5 threshold = 4) -> must still fail
    std::vector<unsigned char> sig2;
    CBLSSecretKey blsSk2 = DeriveEphemeralBLSFromCKey(memberKeys[2], blockHash);
    BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[2], blsSk2, sig2, llmqDst));
    qsig.signatures.emplace_back(customQuorum.members[2].collateralOutpoint, sig2);
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // Add 4th valid quorum member hybrid BLS12-381 signature -> reaches 4/5 V4 majority threshold -> must pass!
    std::vector<unsigned char> sig3;
    CBLSSecretKey blsSk3 = DeriveEphemeralBLSFromCKey(memberKeys[3], blockHash);
    BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[3], blsSk3, sig3, llmqDst));
    qsig.signatures.emplace_back(customQuorum.members[3].collateralOutpoint, sig3);
    BOOST_CHECK(qsig.Verify(customQuorum));

    // Tampering with blockHash must invalidate quorum signature verification
    qsig.blockHash = uint256S("6666666666666666666666666666666666666666666666666666666666666666");
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // H-06: Two distinct quorum members reusing the same CBLSPubKey in a single CQuorumSignature must fail
    {
        llmq::CQuorumSignature qsigDupBls;
        qsigDupBls.blockHash = blockHash;
        CBLSSecretKey sharedBls;
        sharedBls.MakeNewKey();
        std::vector<unsigned char> dupSig0, dupSig1, uniqSig2, uniqSig3;
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[0], sharedBls, dupSig0, llmqDst));
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[1], sharedBls, dupSig1, llmqDst));
        CBLSSecretKey distinctBls2, distinctBls3;
        distinctBls2.MakeNewKey();
        distinctBls3.MakeNewKey();
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[2], distinctBls2, uniqSig2, llmqDst));
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[3], distinctBls3, uniqSig3, llmqDst));
        qsigDupBls.signatures.emplace_back(customQuorum.members[0].collateralOutpoint, dupSig0);
        qsigDupBls.signatures.emplace_back(customQuorum.members[1].collateralOutpoint, dupSig1);
        qsigDupBls.signatures.emplace_back(customQuorum.members[2].collateralOutpoint, uniqSig2);
        qsigDupBls.signatures.emplace_back(customQuorum.members[3].collateralOutpoint, uniqSig3);
        BOOST_CHECK(!qsigDupBls.Verify(customQuorum));
    }
}

BOOST_AUTO_TEST_CASE(mescal_all_roles_and_conditions_test)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    std::string pk1Hex = HexStr(key1.GetPubKey());
    std::string pk2Hex = HexStr(key2.GetPubKey());
    std::string pkh1Hex = HexStr(ToByteVector(key1.GetPubKey().GetID()));

    // 1. Test Coin-Lock Miner role compilation & round-trip decompilation
    std::string coinLockJson = strprintf(R"({
        "basic": {
            "MinerLock": {
                "role": "coin-lock-miner",
                "inputs": [
                    {"name": "Pubkey", "type": "pubkey", "value": "%s"},
                    {"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 15000},
                    {"name": "PubkeyHash", "type": "pubkeyhash", "value": "%s"}
                ]
            }
        },
        "contract": {
            "CoinLockContract": {
                "actions": [{"type": "basic", "name": "MinerLock"}]
            }
        },
        "active_contract": "CoinLockContract"
    })", pk1Hex, pkh1Hex);

    std::string err;
    CScript coinLockScript = CMescal::Compile(coinLockJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);

    CPubKey matchedPubKey;
    int64_t matchedLockTime = 0;
    CKeyID matchedKeyId;
    BOOST_CHECK(MatchCoinLockRegistration(coinLockScript, matchedPubKey, matchedLockTime, matchedKeyId));
    BOOST_CHECK(matchedPubKey == key1.GetPubKey());
    BOOST_CHECK_EQUAL(matchedLockTime, 15000);
    BOOST_CHECK(matchedKeyId == key1.GetPubKey().GetID());

    UniValue decompiledCoinLock = CMescal::Decompile(coinLockScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledCoinLock = CMescal::Compile(decompiledCoinLock.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(coinLockScript == recompiledCoinLock);

    // 2. Test PoW-Miner role compilation & round-trip decompilation
    std::string challengeHex = "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff";
    std::string powLockJson = strprintf(R"({
        "basic": {
            "PoWMinerLock": {
                "role": "pow-miner",
                "inputs": [
                    {"name": "Nonce", "type": "nonce", "value": "39300000"},
                    {"name": "Challenge", "type": "challenge", "value": "%s"},
                    {"name": "Pubkey", "type": "pubkey", "value": "%s"},
                    {"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 20000},
                    {"name": "PubkeyHash", "type": "pubkeyhash", "value": "%s"}
                ]
            }
        },
        "contract": {
            "PoWLockContract": {
                "actions": [{"type": "basic", "name": "PoWMinerLock"}]
            }
        },
        "active_contract": "PoWLockContract"
    })", challengeHex, pk1Hex, pkh1Hex);

    CScript powLockScript = CMescal::Compile(powLockJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);

    std::vector<unsigned char> nonceOut;
    uint256 challengeOut;
    BOOST_CHECK(MatchPoWLockRegistration(powLockScript, nonceOut, challengeOut, matchedPubKey, matchedLockTime, matchedKeyId));
    BOOST_CHECK_EQUAL(matchedLockTime, 20000);
    BOOST_CHECK(matchedPubKey == key1.GetPubKey());

    UniValue decompiledPoWLock = CMescal::Decompile(powLockScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledPoWLock = CMescal::Compile(decompiledPoWLock.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(powLockScript == recompiledPoWLock);

    // 3. Test lock-time, equalverify-checksig, arithmetic, and if-condition branches
    std::string conditionalJson = strprintf(R"({
        "basic": {
            "Push5": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 5}]},
            "Push3": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 3}]},
            "GreaterOp": {"role": "greater-than"},
            "LockCheck": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 1000}]},
            "P2PKHCheck": {"role": "equalverify-checksig", "inputs": [{"name": "PubkeyHash", "type": "pubkeyhash", "value": "%s"}]},
            "MultiSigCheck": {
                "role": "multi-signature",
                "inputs": [
                    {"name": "m", "type": "number", "value": 1},
                    {"name": "n", "type": "number", "value": 2},
                    {"name": "Signatures", "type": "array", "value": ["%s", "%s"]}
                ]
            }
        },
        "condition": {
            "BranchCond": {
                "role": "if-condition",
                "expressions": [
                    {"type": "basic", "name": "Push5"},
                    {"type": "basic", "name": "Push3"}
                ],
                "true": [
                    {"type": "basic", "name": "LockCheck"},
                    {"type": "basic", "name": "P2PKHCheck"}
                ],
                "false": [
                    {"type": "basic", "name": "MultiSigCheck"}
                ]
            }
        },
        "contract": {
            "ConditionalContract": {
                "actions": [{"type": "condition", "name": "BranchCond"}]
            }
        },
        "active_contract": "ConditionalContract"
    })", pkh1Hex, pk1Hex, pk2Hex);

    CScript condScript = CMescal::Compile(conditionalJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    std::string asmStr = ScriptToAsmStr(condScript);
    BOOST_CHECK(asmStr.find("OP_IF") != std::string::npos);
    BOOST_CHECK(asmStr.find("OP_ELSE") != std::string::npos);
    BOOST_CHECK(asmStr.find("OP_ENDIF") != std::string::npos);
    BOOST_CHECK(asmStr.find("OP_CHECKLOCKTIMEVERIFY") != std::string::npos);
    BOOST_CHECK(asmStr.find("OP_CHECKMULTISIG") != std::string::npos);

    UniValue decompiledCond = CMescal::Decompile(condScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledCond = CMescal::Compile(decompiledCond.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(condScript == recompiledCond);

    // 4. Error handling on invalid JSON or unknown role
    std::string badErr;
    CScript badScript = CMescal::Compile("{invalid_json", badErr);
    BOOST_CHECK(!badErr.empty());
    BOOST_CHECK(badScript.empty());
}

BOOST_AUTO_TEST_CASE(mescal_decompile_roundtrip_and_execution_test)
{
    CKey key1, key2, key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    key3.MakeNewKey(true);
    std::string pk1Hex = HexStr(key1.GetPubKey());
    std::string pk2Hex = HexStr(key2.GetPubKey());
    std::string pk3Hex = HexStr(key3.GetPubKey());
    std::string pkh1Hex = HexStr(ToByteVector(key1.GetPubKey().GetID()));

    // 1. Standalone 2-of-3 Multi-Signature: Compile -> Solver -> Decompile -> Recompile
    std::string multiSigJson = strprintf(R"({
        "basic": {
            "MS2of3": {
                "role": "multi-signature",
                "inputs": [
                    {"name": "m", "type": "number", "value": 2},
                    {"name": "n", "type": "number", "value": 3},
                    {"name": "Signatures", "type": "array", "value": ["%s", "%s", "%s"]}
                ]
            }
        },
        "contract": {
            "Escrow2of3": {
                "actions": [{"type": "basic", "name": "MS2of3"}]
            }
        },
        "active_contract": "Escrow2of3"
    })", pk1Hex, pk2Hex, pk3Hex);

    std::string err;
    CScript msScript = CMescal::Compile(multiSigJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);

    txnouttype whichType;
    std::vector<std::vector<unsigned char>> vSolutions;
    BOOST_CHECK(Solver(msScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_MULTISIG);

    UniValue decompiledMs = CMescal::Decompile(msScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledMs = CMescal::Compile(decompiledMs.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(msScript == recompiledMs);

    // 2. hash160 + check-signature-verification & conditional HTLC (if-condition with num-equal expression,
    //    true sig-check, false lock-time + sig-check):
    //    Compile -> Solver -> Decompile -> Recompile -> VerifyScript (TRUE & FALSE branches)
    std::vector<unsigned char> preimage = {'k', 'r', 'i', 's', 't', 'a', '_', 'h', 't', 'l', 'c'};
    uint160 preimageHash = Hash160(preimage);
    std::string hashHex = HexStr(std::vector<unsigned char>(preimageHash.begin(), preimageHash.end()));

    std::string hashSigJson = strprintf(R"({
        "basic": {
            "HashPush": {"role": "hash160", "inputs": [{"name": "Hash160", "type": "string-or-number", "value": "%s"}]},
            "ClaimSig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]}
        },
        "contract": {
            "HashSigContract": {
                "actions": [
                    {"type": "basic", "name": "HashPush"},
                    {"type": "basic", "name": "ClaimSig"}
                ]
            }
        },
        "active_contract": "HashSigContract"
    })", hashHex, pk1Hex);

    CScript hashSigScript = CMescal::Compile(hashSigJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    UniValue decompiledHashSig = CMescal::Decompile(hashSigScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledHashSig = CMescal::Compile(decompiledHashSig.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(hashSigScript == recompiledHashSig);

    std::string htlcJson = strprintf(R"({
        "basic": {
            "NumEq": {"role": "num-equal"},
            "ClaimSig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]},
            "RefundLock": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 500}]},
            "RefundSig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]}
        },
        "condition": {
            "HTLCCond": {
                "role": "if-condition",
                "expressions": [{"type": "basic", "name": "NumEq"}],
                "true": [{"type": "basic", "name": "ClaimSig"}],
                "false": [
                    {"type": "basic", "name": "RefundLock"},
                    {"type": "basic", "name": "RefundSig"}
                ]
            }
        },
        "contract": {
            "HTLC": {
                "actions": [{"type": "condition", "name": "HTLCCond"}]
            }
        },
        "active_contract": "HTLC"
    })", pk1Hex, pk2Hex);

    CScript htlcScript = CMescal::Compile(htlcJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(Solver(htlcScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_MESCAL_CONTRACT);

    UniValue decompiledHtlc = CMescal::Decompile(htlcScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledHtlc = CMescal::Compile(decompiledHtlc.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(htlcScript == recompiledHtlc);

    // Execute HTLC TRUE branch (1 == 1 -> OP_IF true branch + key1 signature)
    CMutableTransaction txTrue;
    txTrue.nVersion = 1;
    txTrue.vin.push_back(CTxIn(COutPoint(uint256S("01"), 0)));
    txTrue.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
    uint256 sighashTrue = SignatureHash(htlcScript, txTrue, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
    std::vector<unsigned char> sigTrue;
    BOOST_CHECK(key1.Sign(sighashTrue, sigTrue));
    sigTrue.push_back((unsigned char)SIGHASH_ALL);
    txTrue.vin[0].scriptSig = CScript() << OP_1 << sigTrue << OP_1 << OP_1;

    ScriptError serror;
    CTransaction txTrueConst(txTrue);
    BOOST_CHECK(VerifyScript(txTrue.vin[0].scriptSig, htlcScript, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
                             TransactionSignatureChecker(&txTrueConst, 0, 10 * COIN), &serror));

    // Execute HTLC FALSE branch (1 == 0 -> OP_ELSE refund branch after lock-time 500 + key2 signature)
    CMutableTransaction txFalse;
    txFalse.nVersion = 1;
    CTxIn txInFalse(COutPoint(uint256S("01"), 0));
    txInFalse.nSequence = std::numeric_limits<uint32_t>::max() - 1;
    txFalse.vin.push_back(txInFalse);
    txFalse.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
    txFalse.nLockTime = 500;
    uint256 sighashFalse = SignatureHash(htlcScript, txFalse, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
    std::vector<unsigned char> sigFalse;
    BOOST_CHECK(key2.Sign(sighashFalse, sigFalse));
    sigFalse.push_back((unsigned char)SIGHASH_ALL);
    txFalse.vin[0].scriptSig = CScript() << OP_1 << sigFalse << OP_1 << OP_0;

    CTransaction txFalseConst(txFalse);
    BOOST_CHECK(VerifyScript(txFalse.vin[0].scriptSig, htlcScript, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
                             TransactionSignatureChecker(&txFalseConst, 0, 10 * COIN), &serror));

    // 3. equalverify-checksig & number (-1, 0, 16, 1000) round-trip and execution
    std::string pkhAndNumsJson = strprintf(R"({
        "basic": {
            "NegOne": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": -1}]},
            "Drop1": {"role": "drop"},
            "Zero": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": 0}]},
            "Drop2": {"role": "drop"},
            "Sixteen": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": 16}]},
            "Drop3": {"role": "drop"},
            "BigNum": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": 1000}]},
            "Drop4": {"role": "drop"},
            "PkhCheck": {"role": "equalverify-checksig", "inputs": [{"name": "PubKeyHash", "type": "string", "value": "%s"}]}
        },
        "contract": {
            "PkhContract": {
                "actions": [
                    {"type": "basic", "name": "NegOne"},
                    {"type": "basic", "name": "Drop1"},
                    {"type": "basic", "name": "Zero"},
                    {"type": "basic", "name": "Drop2"},
                    {"type": "basic", "name": "Sixteen"},
                    {"type": "basic", "name": "Drop3"},
                    {"type": "basic", "name": "BigNum"},
                    {"type": "basic", "name": "Drop4"},
                    {"type": "basic", "name": "PkhCheck"}
                ]
            }
        },
        "active_contract": "PkhContract"
    })", pkh1Hex);

    CScript pkhScript = CMescal::Compile(pkhAndNumsJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    UniValue decompiledPkh = CMescal::Decompile(pkhScript, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiledPkh = CMescal::Compile(decompiledPkh.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(pkhScript == recompiledPkh);

    CMutableTransaction txPkh;
    txPkh.nVersion = 1;
    txPkh.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0)));
    txPkh.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
    uint256 sighashPkh = SignatureHash(pkhScript, txPkh, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
    std::vector<unsigned char> sigPkh;
    BOOST_CHECK(key1.Sign(sighashPkh, sigPkh));
    sigPkh.push_back((unsigned char)SIGHASH_ALL);
    txPkh.vin[0].scriptSig = CScript() << sigPkh << ToByteVector(key1.GetPubKey());
    CTransaction txPkhConst(txPkh);
    BOOST_CHECK(VerifyScript(txPkh.vin[0].scriptSig, pkhScript, SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY,
                             TransactionSignatureChecker(&txPkhConst, 0, 5 * COIN), &serror));

    // 4. Exception-safe error handling on mismatched JSON value types
    std::string wrongTypeJson = R"({
        "basic": {
            "BadLock": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": "not_a_number"}]}
        },
        "contract": {
            "BadContract": {"actions": [{"type": "basic", "name": "BadLock"}]}
        },
        "active_contract": "BadContract"
    })";
    std::string typeErr;
    CScript wrongTypeScript = CMescal::Compile(wrongTypeJson, typeErr);
    BOOST_CHECK(!typeErr.empty());
    BOOST_CHECK(wrongTypeScript.empty());
}

BOOST_AUTO_TEST_CASE(mescal_and_core_security_hardening_test)
{
    // 1. Malformed >4 byte CScriptNum in CLTV script must not throw uncaught scriptnum_error in Decompile or Solver
    std::vector<unsigned char> oversizedLockTime = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    CScript badCltvScript;
    badCltvScript << oversizedLockTime << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_TRUE;

    std::string decompErr;
    UniValue decompRes = CMescal::Decompile(badCltvScript, decompErr);
    BOOST_CHECK(!decompErr.empty());
    BOOST_CHECK(decompRes.isNull());

    txnouttype whichType;
    std::vector<std::vector<unsigned char>> vSolutions;
    BOOST_CHECK(!Solver(badCltvScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_NONSTANDARD);

    // 2. Unclosed OP_IF and excessive OP_IF nesting (>32 levels) in CMescal::Decompile
    CScript unclosedIfScript;
    unclosedIfScript << OP_1 << OP_IF << OP_1;
    decompErr.clear();
    decompRes = CMescal::Decompile(unclosedIfScript, decompErr);
    BOOST_CHECK(!decompErr.empty());
    BOOST_CHECK(decompRes.isNull());

    CScript deepIfScript;
    for (int i = 0; i < 40; ++i) {
        deepIfScript << OP_1 << OP_IF;
    }
    deepIfScript << OP_1;
    for (int i = 0; i < 40; ++i) {
        deepIfScript << OP_ENDIF;
    }
    decompErr.clear();
    decompRes = CMescal::Decompile(deepIfScript, decompErr);
    BOOST_CHECK(!decompErr.empty());
    BOOST_CHECK(decompRes.isNull());

    // 3. CPubKey::Unserialize must invalidate truncated public keys (preventing uninitialized memory reads)
    {
        CDataStream ssTruncated(SER_NETWORK, PROTOCOL_VERSION);
        std::vector<unsigned char> truncBytes = {0x01, 0x04}; // compact size 1, prefix 0x04 (claims 65 bytes)
        ssTruncated.write((const char*)truncBytes.data(), truncBytes.size());
        CPubKey truncatedPubKey;
        ssTruncated >> truncatedPubKey;
        BOOST_CHECK(!truncatedPubKey.IsValid());
        BOOST_CHECK_EQUAL(truncatedPubKey.size(), 0U);
    }

    // 4. VerifyBLSWithECDSAFallback must reject an invalid ecdsaPubKey even if BLS self-signature is valid
    {
        CBLSSecretKey blsSk;
        blsSk.MakeNewKey();
        CBLSPubKey blsPk(blsSk);
        uint256 msgHash = Hash(std::string("security_test").begin(), std::string("security_test").end());

        CBLSSignedData sigData;
        sigData.blsPubKey = blsPk;
        BOOST_CHECK(sigData.blsSig.Sign(blsSk, msgHash));

        CDataStream ssSig(SER_NETWORK, PROTOCOL_VERSION);
        ssSig << sigData;
        std::vector<unsigned char> vchSig(ssSig.begin(), ssSig.end());

        CPubKey invalidEcdsaPubKey;
        BOOST_CHECK(!invalidEcdsaPubKey.IsValid());
        BOOST_CHECK(!VerifyBLSWithECDSAFallback(msgHash, invalidEcdsaPubKey, vchSig));
    }

    // 5. CheckBlockHeader must reject invalid miner pubkeys and oversized inner vAdamSolutions in v11+ headers
    {
        CBlockHeader badHeader;
        badHeader.nVersion = 12;
        badHeader.vAdamMiners.push_back(CPubKey()); // invalid pubkey
        CValidationState state1;
        BOOST_CHECK(!CheckBlockHeader(badHeader, state1, false));

        CKey validKey;
        validKey.MakeNewKey(true);
        CBlockHeader oversizedSolHeader;
        oversizedSolHeader.nVersion = 12;
        oversizedSolHeader.vAdamMiners.push_back(validKey.GetPubKey());
        oversizedSolHeader.vAdamSolutions.push_back(std::vector<unsigned char>(600, 0x42));
        CValidationState state2;
        BOOST_CHECK(!CheckBlockHeader(oversizedSolHeader, state2, false));
    }

    // 6. Fixed-point 128-bit integer subsidy determinism across 17 KRISTA base & -2.15% decay periods
    {
        ScopedMpaChainParams mainScope(CBaseChainParams::MAIN);
        BOOST_CHECK_EQUAL(Params().GetConsensus().nAdamQuorumEnforcementHeight, 200);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(0), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(1), 0);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2200), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(4800), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(5000), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(10000), 17 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(259201), 1700000000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(259202), 1663450000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(350000), 1663450000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(518402), 1627685825LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(1036802), 1558447732LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2073602), 1428681961LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(3110402), 1309721272LL);
    }

    // 7. SelectAdamNodes explicit nBlockHeight & on-chain masternode ping tracking
    {
        ScopedMpaChainParams regScope(CBaseChainParams::REGTEST);
        uint256 testSeed = Hash(std::string("explicit_height_seed").begin(), std::string("explicit_height_seed").end());
        std::vector<CPubKey> electedMiners1, electedMiners2;
        CPubKey coord1, coord2;
        BOOST_CHECK(SelectAdamNodes(testSeed, Params().GetConsensus(), electedMiners1, coord1, 500));
        BOOST_CHECK(SelectAdamNodes(testSeed, Params().GetConsensus(), electedMiners2, coord2, 500));
        BOOST_CHECK(electedMiners1 == electedMiners2);
        BOOST_CHECK(coord1 == coord2);
        BOOST_CHECK_EQUAL(electedMiners1.size(), (size_t)Params().GetConsensus().GetAdamMinersCount(500));
    }

    // 8. Ephemeral per-block BLS12-381 key derivation, PoBLS Tickets, and XOR Distance Ranking
    {
        CKey valKey1, valKey2;
        valKey1.MakeNewKey(true);
        valKey2.MakeNewKey(true);

        uint256 seedH = Hash(std::string("pobls_seed_1000").begin(), std::string("pobls_seed_1000").end());
        uint256 seedHNext = Hash(std::string("pobls_seed_1001").begin(), std::string("pobls_seed_1001").end());

        // Ephemeral BLS keys must be deterministic for the same (key, context) and distinct across contexts
        CBLSSecretKey ephSk1A = DeriveEphemeralBLSFromCKey(valKey1, seedH);
        CBLSSecretKey ephSk1B = DeriveEphemeralBLSFromCKey(valKey1, seedH);
        CBLSSecretKey ephSk1Next = DeriveEphemeralBLSFromCKey(valKey1, seedHNext);
        BOOST_CHECK(ephSk1A.IsValid() && ephSk1B.IsValid() && ephSk1Next.IsValid());
        BOOST_CHECK(CBLSPubKey(ephSk1A) == CBLSPubKey(ephSk1B));
        BOOST_CHECK(CBLSPubKey(ephSk1A) != CBLSPubKey(ephSk1Next));

        // Build a valid PoBLS solution payload for valKey1
        std::vector<unsigned char> vchSig1;
        BOOST_CHECK(SignBLSWithECDSAFallback(seedH, valKey1, ephSk1A, vchSig1));
        BOOST_CHECK(VerifyBLSWithECDSAFallback(seedH, valKey1.GetPubKey(), vchSig1));

        CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
        uint32_t nonce = 42;
        ssSol << nonce << vchSig1;
        std::vector<unsigned char> vchSol1(ssSol.begin(), ssSol.end());

        uint256 ticket1;
        BOOST_CHECK(ExtractPoBLSTicketFromSolution(vchSol1, 1000, ticket1));
        BOOST_CHECK(!ticket1.IsNull());

        uint256 target = ComputePoBLSTarget(seedH, 1000);
        // XOR metric space properties: d(x, x) == 0, d(x, y) == d(y, x)
        BOOST_CHECK(ComputePoBLSXORDistance(ticket1, ticket1).IsNull());
        BOOST_CHECK(ComputePoBLSXORDistance(ticket1, target) == ComputePoBLSXORDistance(target, ticket1));

        // H-3 under UPGRADE_HARDENING_V3: RankValidatorsByPoBLSDistance ignores local mapSols cache so all nodes produce bit-identical fallback order
        std::vector<CPubKey> vMiners = {valKey2.GetPubKey(), valKey1.GetPubKey()};
        std::map<CPubKey, std::vector<unsigned char>> mapSols;
        mapSols[valKey1.GetPubKey()] = vchSol1;
        std::map<CPubKey, std::vector<unsigned char>> mapEmptySols;
        std::vector<CPubKey> rankedWithSol = RankValidatorsByPoBLSDistance(seedH, 1000, vMiners, mapSols);
        std::vector<CPubKey> rankedEmpty = RankValidatorsByPoBLSDistance(seedH, 1000, vMiners, mapEmptySols);
        BOOST_CHECK_EQUAL(rankedWithSol.size(), 2U);
        BOOST_CHECK(rankedWithSol == rankedEmpty);
    }

    // 9. Hybrid BLS12-381 LLMQ Quorum Signatures & BLS12-381 Aggregation
    {
        llmq::CQuorum quorum;
        quorum.nHeight = 1000;
        std::vector<CKey> memberKeys(5);
        for (int i = 0; i < 5; ++i) {
            memberKeys[i].MakeNewKey(true);
            COutPoint out(Hash(memberKeys[i].begin(), memberKeys[i].end()), i);
            quorum.members.push_back(llmq::CQuorumMember(out, memberKeys[i].GetPubKey()));
        }

        llmq::CQuorumSignature qsig;
        qsig.blockHash = Hash(std::string("llmq_bls_block_1000").begin(), std::string("llmq_bls_block_1000").end());

        // Sign with 4 out of 5 members (V4 floor(2*5/3)+1 = 4) using ephemeral BLS12-381 + secp256k1 authorization
        const std::string& llmqDst = GetBLSDomainTag(BLSDomain::LLMQ_BLOCK);
        const size_t threshold = llmq::GetQuorumThreshold(quorum.members.size());
        for (size_t i = 0; i < threshold; ++i) {
            CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(memberKeys[i], qsig.blockHash);
            std::vector<unsigned char> vchMemberSig;
            BOOST_CHECK(SignBLSWithECDSAFallback(qsig.blockHash, memberKeys[i], blsSk, vchMemberSig, llmqDst));
            qsig.signatures.push_back({quorum.members[i].collateralOutpoint, vchMemberSig});
        }

        BOOST_CHECK(qsig.Verify(quorum));
        CBLSPubKey aggPubKey;
        CBLSSignature aggSig;
        BOOST_CHECK(qsig.AggregateBLSSignatures(quorum, aggPubKey, aggSig));
        BOOST_CHECK(aggPubKey.IsValid());
        BOOST_CHECK(aggSig.IsValid());
        BOOST_CHECK(aggSig.Verify(aggPubKey, qsig.blockHash, llmqDst));
    }

    // 10. On-Chain Masternode Ping 4,200 KRISTA Collateral & Ownership Enforcement (Anti-Spoofing)
    {
        BOOST_CHECK_EQUAL(Params(CBaseChainParams::MAIN).GetConsensus().nAdamQuorumEnforcementHeight, 200);

        CKey mnOwnerKey, attackerKey;
        mnOwnerKey.MakeNewKey(true);
        attackerKey.MakeNewKey(true);

        COutPoint validCollateralOut(uint256S("0x1111111111111111111111111111111111111111111111111111111111111111"), 0);
        COutPoint dustOut(uint256S("0x2222222222222222222222222222222222222222222222222222222222222222"), 0);

        BOOST_REQUIRE(pcoinsTip != nullptr);
        Coin validCollateralCoin(CTxOut(4200 * COIN, GetScriptForDestination(mnOwnerKey.GetPubKey().GetID())), 100, false, false);
        Coin dustCoin(CTxOut(1 * COIN, GetScriptForDestination(attackerKey.GetPubKey().GetID())), 100, false, false);
        pcoinsTip->AddCoin(validCollateralOut, std::move(validCollateralCoin), true);
        pcoinsTip->AddCoin(dustOut, std::move(dustCoin), true);

        // Attacker with < 4,200 KRISTA UTXO must be rejected
        BOOST_CHECK(!IsValidMasternodeCollateralAndAuth(attackerKey.GetPubKey(), dustOut, 5000));
        BOOST_CHECK(!RegisterOnChainMasternodePing(attackerKey.GetPubKey(), dustOut, 5000));

        // Attacker attempting to spoof mnOwnerKey's 4,200 KRISTA collateral outpoint must be rejected
        BOOST_CHECK(!IsValidMasternodeCollateralAndAuth(attackerKey.GetPubKey(), validCollateralOut, 5000));
        BOOST_CHECK(!RegisterOnChainMasternodePing(attackerKey.GetPubKey(), validCollateralOut, 5000));

        // Legitimate owner of the 4,200 KRISTA collateral outpoint must succeed and support multi-height history
        BOOST_CHECK(IsValidMasternodeCollateralAndAuth(mnOwnerKey.GetPubKey(), validCollateralOut, 5000));
        BOOST_CHECK(RegisterOnChainMasternodePing(mnOwnerKey.GetPubKey(), validCollateralOut, 5000));
        BOOST_CHECK(RegisterOnChainMasternodePing(mnOwnerKey.GetPubKey(), validCollateralOut, 5020));
        BOOST_CHECK_EQUAL(mapMasternodeLastActiveHeight[mnOwnerKey.GetPubKey()], 5020);
        BOOST_CHECK(mapMasternodeCollateralOutpoints[mnOwnerKey.GetPubKey()] == validCollateralOut);

        // Historical height queries must remain valid across multiple pings without leaking backward before 5000
        BOOST_CHECK(!IsMasternodeActiveAtHeight(mnOwnerKey.GetPubKey(), 4999, 50));
        BOOST_CHECK(IsMasternodeActiveAtHeight(mnOwnerKey.GetPubKey(), 5005, 50));
        BOOST_CHECK(IsMasternodeActiveAtHeight(mnOwnerKey.GetPubKey(), 5065, 50));
        BOOST_CHECK(!IsMasternodeActiveAtHeight(mnOwnerKey.GetPubKey(), 5075, 50));

        // 10a. Unsigned third-party vout keep-alive spoofing (griefing attack) MUST be rejected
        CMutableTransaction unsignedSpoofTx;
        unsignedSpoofTx.vin.push_back(CTxIn(COutPoint(uint256S("0x33"), 0)));
        CScript pingScript = GetMasternodePingScript(mnOwnerKey.GetPubKey());
        unsignedSpoofTx.vout.push_back(CTxOut(0.1 * COIN, pingScript));
        CDataStream ssUnsignedOut(SER_NETWORK, PROTOCOL_VERSION);
        ssUnsignedOut << validCollateralOut;
        unsignedSpoofTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssUnsignedOut)));

        CPubKey extractedKey;
        COutPoint extractedOutpoint;
        BOOST_CHECK(!ExtractOnChainMasternodePing(CTransaction(unsignedSpoofTx), extractedKey, extractedOutpoint));

        // 10b. Legitimate signed vout ping (signed with mnOwnerKey over collateralOutpoint || vin[0].prevout) MUST succeed
        CMutableTransaction signedPingTx;
        signedPingTx.vin.push_back(CTxIn(COutPoint(uint256S("0x33"), 0)));
        signedPingTx.vout.push_back(CTxOut(0.1 * COIN, pingScript));
        CHashWriter ssPingHash(SER_GETHASH, 0);
        ssPingHash << validCollateralOut << signedPingTx.vin[0].prevout;
        std::vector<unsigned char> vchPingSig;
        BOOST_CHECK(mnOwnerKey.Sign(ssPingHash.GetHash(), vchPingSig));

        CDataStream ssSignedOut(SER_NETWORK, PROTOCOL_VERSION);
        ssSignedOut << validCollateralOut << vchPingSig << false;
        signedPingTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssSignedOut)));

        BOOST_CHECK(ExtractOnChainMasternodePing(CTransaction(signedPingTx), extractedKey, extractedOutpoint));
        BOOST_CHECK(extractedKey == mnOwnerKey.GetPubKey());
        BOOST_CHECK(extractedOutpoint == validCollateralOut);

        // 10c. On-chain Hot/Cold Delegation Proof inside OP_RETURN (verifiable during IBD without mnodeman)
        CKey coldCollateralKey, hotMnKey;
        coldCollateralKey.MakeNewKey(true);
        hotMnKey.MakeNewKey(true);
        COutPoint hotColdOut(uint256S("0x4444444444444444444444444444444444444444444444444444444444444444"), 0);
        Coin hotColdCoin(CTxOut(4200 * COIN, GetScriptForDestination(coldCollateralKey.GetPubKey().GetID())), 100, false, false);
        pcoinsTip->AddCoin(hotColdOut, std::move(hotColdCoin), true);

        auto makeDelegationProof = [](const CKey& colKey, const CPubKey& mnPubKey, int nMessVerOut) {
            COnChainMasternodeDelegation del;
            del.pubKeyCollateralAddress = colKey.GetPubKey();
            del.pubKeyMasternode = mnPubKey;
            del.sigTime = GetAdjustedTime();
            del.protocolVersion = PROTOCOL_VERSION;
            del.nMessVersion = nMessVerOut;

            const std::string vchPubKey(del.pubKeyCollateralAddress.begin(), del.pubKeyCollateralAddress.end());
            const std::string vchPubKey2(del.pubKeyMasternode.begin(), del.pubKeyMasternode.end());
            const std::string oldStrMessage = del.addr.ToString() + std::to_string(del.sigTime) + vchPubKey + vchPubKey2 + std::to_string(del.protocolVersion);
            CHashWriter ss(SER_GETHASH, 0);
            ss << strMessageMagic << oldStrMessage;
            BOOST_CHECK(colKey.SignCompact(ss.GetHash(), del.vchCollateralSig));
            return del;
        };

        COnChainMasternodeDelegation delProof = makeDelegationProof(coldCollateralKey, hotMnKey.GetPubKey(), MessageVersion::MESS_VER_HASH);

        CMutableTransaction hotPingTx;
        hotPingTx.vin.push_back(CTxIn(COutPoint(uint256S("0x55"), 0)));
        hotPingTx.vout.push_back(CTxOut(0.1 * COIN, GetMasternodePingScript(hotMnKey.GetPubKey())));
        CHashWriter ssHotPingHash(SER_GETHASH, 0);
        ssHotPingHash << hotColdOut << hotPingTx.vin[0].prevout;
        std::vector<unsigned char> vchHotPingSig;
        BOOST_CHECK(hotMnKey.Sign(ssHotPingHash.GetHash(), vchHotPingSig));

        CDataStream ssHotOut(SER_NETWORK, PROTOCOL_VERSION);
        ssHotOut << hotColdOut << vchHotPingSig << true << delProof;
        hotPingTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssHotOut)));

        CPubKey extractedHotKey;
        COutPoint extractedHotOut;
        COnChainMasternodeDelegation extractedHotDel;
        // Read-only extraction (fRecordDelegation = false) must NOT mutate mapOnChainCollateralDelegations
        BOOST_CHECK(ExtractOnChainMasternodePing(CTransaction(hotPingTx), extractedHotKey, extractedHotOut, false, &extractedHotDel));
        BOOST_CHECK_EQUAL(mapOnChainCollateralDelegations.count(hotColdOut), 0U);
        BOOST_CHECK(IsValidMasternodeCollateralAndAuth(hotMnKey.GetPubKey(), hotColdOut, 5000, &extractedHotDel));
        // Block-level registration (fRecordDelegation = true) records delegation in mapOnChainCollateralDelegations
        BOOST_CHECK(ExtractOnChainMasternodePing(CTransaction(hotPingTx), extractedHotKey, extractedHotOut, true));
        BOOST_CHECK_EQUAL(mapOnChainCollateralDelegations.count(hotColdOut), 1U);
        BOOST_CHECK(extractedHotKey == hotMnKey.GetPubKey());
        BOOST_CHECK(extractedHotOut == hotColdOut);
        BOOST_CHECK(IsValidMasternodeCollateralAndAuth(hotMnKey.GetPubKey(), hotColdOut, 5000));

        // 10d. Uncompressed (65-byte, createmasternodekey) Hot Masternode Ping + GetMinRelayFee + IsMine + SignSignature
        {
            CKey uncompHotMnKey;
            uncompHotMnKey.MakeNewKey(false); // 65-byte uncompressed pubkey (exact format of createmasternodekey)
            BOOST_CHECK(!uncompHotMnKey.GetPubKey().IsCompressed());
            BOOST_CHECK_EQUAL(uncompHotMnKey.GetPubKey().size(), 65U);

            COutPoint uncompHotOut(uint256S("0x4545454545454545454545454545454545454545454545454545454545454545"), 0);
            Coin uncompCoin(CTxOut(4200 * COIN, GetScriptForDestination(coldCollateralKey.GetPubKey().GetID())), 100, false, false);
            pcoinsTip->AddCoin(uncompHotOut, std::move(uncompCoin), true);

            COnChainMasternodeDelegation uncompDel = makeDelegationProof(coldCollateralKey, uncompHotMnKey.GetPubKey(), MessageVersion::MESS_VER_STRMESS);

            CScript uncompPingScript = GetMasternodePingScript(uncompHotMnKey.GetPubKey());
            BOOST_CHECK_EQUAL(uncompPingScript.size(), 68U);

            CBasicKeyStore tempKeyStore;
            tempKeyStore.AddKey(uncompHotMnKey);
            BOOST_CHECK_EQUAL(IsMine(tempKeyStore, uncompPingScript), ISMINE_SPENDABLE);

            CMutableTransaction uncompPingTx;
            uncompPingTx.vin.push_back(CTxIn(COutPoint(uint256S("0x56"), 0)));
            uncompPingTx.vout.push_back(CTxOut(0.01 * COIN, uncompPingScript));
            CHashWriter ssUncompHash(SER_GETHASH, 0);
            ssUncompHash << uncompHotOut << uncompPingTx.vin[0].prevout;
            std::vector<unsigned char> vchUncompSig;
            BOOST_CHECK(uncompHotMnKey.Sign(ssUncompHash.GetHash(), vchUncompSig));

            CDataStream ssUncompOut(SER_NETWORK, PROTOCOL_VERSION);
            ssUncompOut << uncompHotOut << vchUncompSig << true << uncompDel;
            uncompPingTx.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ToByteVector(ssUncompOut)));

            CTransaction finalUncompTx(uncompPingTx);
            CPubKey extUncompKey;
            COutPoint extUncompOut;
            // Verify GetMinRelayFee works read-only BEFORE delegation is recorded in mapOnChainCollateralDelegations
            unsigned int nBytes = ::GetSerializeSize(finalUncompTx, SER_NETWORK, PROTOCOL_VERSION);
            CAmount minRelayFee = GetMinRelayFee(finalUncompTx, mempool, nBytes, false);
            BOOST_CHECK_LT(minRelayFee, 10000); // Exempt from 1000x OP_RETURN multiplier (< 0.0001 KRISTA)
            BOOST_CHECK_EQUAL(mapOnChainCollateralDelegations.count(uncompHotOut), 0U);

            BOOST_CHECK(ExtractOnChainMasternodePing(finalUncompTx, extUncompKey, extUncompOut, true));
            BOOST_CHECK(extUncompKey == uncompHotMnKey.GetPubKey());
            BOOST_CHECK(extUncompOut == uncompHotOut);
            BOOST_CHECK(IsValidMasternodeCollateralAndAuth(uncompHotMnKey.GetPubKey(), uncompHotOut, 5000));

            // Verify SignSignature can also spend the 68-byte uncompressed ping output
            CMutableTransaction spendPingTx;
            spendPingTx.vin.push_back(CTxIn(COutPoint(finalUncompTx.GetHash(), 0)));
            spendPingTx.vout.push_back(CTxOut(0.0099 * COIN, uncompPingScript));
            BOOST_CHECK(SignSignature(tempKeyStore, finalUncompTx, spendPingTx, 0, SIGHASH_ALL));

            mapOnChainCollateralDelegations.erase(uncompHotOut);
        }

        // Clean up test entries
        mapMasternodeLastActiveHeight.erase(mnOwnerKey.GetPubKey());
        mapMasternodeActiveHeights.erase(mnOwnerKey.GetPubKey());
        mapMasternodeCollateralOutpoints.erase(mnOwnerKey.GetPubKey());
        mapOnChainCollateralDelegations.erase(hotColdOut);

        // 11. Test LevelDB Burn Cache V3 Persistence (FlushBurnCacheToDisk / ReadBurnCacheV3)
        if (pblocktree) {
            COutPoint burnOut(uint256S("0x7766554433221100776655443322110077665544332211007766554433221100"), 1);
            uint256 testTipHash = uint256S("0x9988776655443322110099887766554433221100998877665544332211009988");
            AddOutpointBurnToCache(burnOut, 25 * COIN, 1235);
            BOOST_CHECK(FlushBurnCacheToDisk(testTipHash));

            uint256 readTipHash;
            std::map<std::string, std::vector<CBurnCoins>> readBurnMap;
            std::map<COutPoint, std::vector<CBurnCoins>> readOutBurnMap;
            BOOST_REQUIRE(pblocktree->ReadBurnCacheV3(readTipHash, readBurnMap, readOutBurnMap));
            BOOST_CHECK(readTipHash == testTipHash);
            BOOST_CHECK(readBurnMap.empty());
            BOOST_REQUIRE(readOutBurnMap.count(burnOut) > 0);
            BOOST_REQUIRE(!readOutBurnMap[burnOut].empty());
            BOOST_CHECK_EQUAL(readOutBurnMap[burnOut].back().nAmount, 25 * COIN);
            BOOST_CHECK_EQUAL(readOutBurnMap[burnOut].back().nHeight, 1235);

            RemoveOutpointBurnFromCache(burnOut, 25 * COIN, 1235);
        }

        // 12. Strict BLS12-381 + secp256k1 Verification & Deterministic VRF Seed Anti-Grinding
        {
            CKey strictKey;
            strictKey.MakeNewKey(true);
            uint256 msgHash = Hash(std::string("strict_bls_test").begin(), std::string("strict_bls_test").end());

            // Plain ECDSA signature without BLS12-381 pubkey/signature must be rejected
            CBLSSignedData plainEcdsaPayload;
            BOOST_CHECK(strictKey.Sign(msgHash, plainEcdsaPayload.ecdsaSig));
            CDataStream ssPlain(SER_NETWORK, PROTOCOL_VERSION);
            ssPlain << plainEcdsaPayload;
            std::vector<unsigned char> vchPlain(ssPlain.begin(), ssPlain.end());
            BOOST_CHECK(!VerifyBLSWithECDSAFallback(msgHash, strictKey.GetPubKey(), vchPlain));

            // ComputeNextAdamSeed must depend solely on deterministic BLS12-381 components (immune to ECDSA DER grinding)
            CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(strictKey, msgHash);
            std::vector<unsigned char> vchValidProof1;
            BOOST_CHECK(SignBLSWithECDSAFallback(msgHash, strictKey, blsSk, vchValidProof1));

            CBLSSignedData proof2Data;
            CDataStream ssIn(vchValidProof1, SER_NETWORK, PROTOCOL_VERSION);
            ssIn >> proof2Data;
            proof2Data.ecdsaSig.push_back(0x99); // mutate ECDSA bytes
            CDataStream ssOut(SER_NETWORK, PROTOCOL_VERSION);
            ssOut << proof2Data;
            std::vector<unsigned char> vchMutatedEcdsaProof(ssOut.begin(), ssOut.end());

            BOOST_CHECK(ComputeNextAdamSeed(msgHash, vchValidProof1) == ComputeNextAdamSeed(msgHash, vchMutatedEcdsaProof));

            // On-chain BLS Public Key Pinning: once a CPubKey is pinned to its deterministic CBLSPubKey,
            // any attempt to sign with a different (ephemeral/random) CBLSSecretKey must be rejected
            CBLSSecretKey detBlsSk = DeriveBLSFromCKey(strictKey);
            CBLSPubKey detBlsPk(detBlsSk);
            BOOST_CHECK(PinBLSPubKey(strictKey.GetPubKey(), detBlsPk, 7777));

            std::vector<unsigned char> vchPinnedSig;
            BOOST_CHECK(SignBLSWithECDSAFallback(msgHash, strictKey, detBlsSk, vchPinnedSig));
            BOOST_CHECK(VerifyBLSWithECDSAFallback(msgHash, strictKey.GetPubKey(), vchPinnedSig));

            // Attempting to use a different BLS key (e.g. blsSk != detBlsSk) for strictKey must now fail!
            BOOST_CHECK(!VerifyBLSWithECDSAFallback(msgHash, strictKey.GetPubKey(), vchValidProof1));

            UnpinBLSPubKeysAtHeight(7777);
            CBLSPubKey unpinnedCheck;
            BOOST_CHECK(!GetPinnedBLSPubKey(strictKey.GetPubKey(), unpinnedCheck));

            // 13. Test LevelDB Persistence of Masternode Delegations, Active Heights, and Pinned BLS Keys
            BOOST_CHECK_EQUAL(ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS, 45);
            if (pblocktree) {
                uint256 mnStateTip = uint256S("0x1122334455667788990011223344556677889900112233445566778899001122");
                CPubKey testMnPub = strictKey.GetPubKey();
                COutPoint testMnOut(uint256S("0xabcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890"), 1);
                COnChainMasternodeDelegation testDel;
                testDel.pubKeyCollateralAddress = mnOwnerKey.GetPubKey();
                testDel.pubKeyMasternode = testMnPub;
                testDel.sigTime = 1790000000;

                mapMasternodeActiveHeights[testMnPub].insert(450);
                mapMasternodeLastActiveHeight[testMnPub] = 450;
                mapMasternodeCollateralOutpoints[testMnPub] = testMnOut;
                mapOnChainCollateralDelegations[testMnOut] = testDel;
                BOOST_CHECK(PinBLSPubKey(testMnPub, detBlsPk, 450));

                BOOST_CHECK(FlushMasternodePingStateToDisk(mnStateTip));

                COnChainMasternodeStateCache readMnState;
                BOOST_CHECK(pblocktree->ReadMasternodePingState(readMnState));
                BOOST_CHECK(readMnState.hashBestBlock == mnStateTip);
                BOOST_CHECK_EQUAL(readMnState.mapLastActiveHeight[testMnPub], 450);
                BOOST_CHECK(readMnState.mapCollateralOutpoints[testMnPub] == testMnOut);
                BOOST_CHECK(readMnState.mapCollateralDelegations[testMnOut].pubKeyMasternode == testMnPub);
                BOOST_CHECK(readMnState.mapPinnedBLS[testMnPub].first == detBlsPk);
                BOOST_CHECK_EQUAL(readMnState.mapPinnedBLS[testMnPub].second, 450);

                mapMasternodeActiveHeights.erase(testMnPub);
                mapMasternodeLastActiveHeight.erase(testMnPub);
                mapMasternodeCollateralOutpoints.erase(testMnPub);
                mapOnChainCollateralDelegations.erase(testMnOut);
                UnpinBLSPubKeysAtHeight(450);
            }

            // 14. Test Unified Fallback Coordinator Slot Resolution and Model D Net Masternode Payment Math
            {
                std::vector<CPubKey> vTestMiners = {
                    GetAdamDeterministicPubKey(0),
                    GetAdamDeterministicPubKey(1),
                    GetAdamDeterministicPubKey(2)
                };
                std::map<CPubKey, std::vector<unsigned char>> emptySols;
                CPubKey activeFb0, prevFb0, activeFb1, prevFb1;
                int rot0 = -1, rot1 = -1;

                // Below 45s timeout -> no fallback rotation
                BOOST_CHECK(!GetAdamFallbackCoordinatorForSlot(msgHash, 3000, 44, vTestMiners, emptySols, activeFb0, prevFb0, rot0));
                // At 45s..74s -> rotationIndex 0
                BOOST_CHECK(GetAdamFallbackCoordinatorForSlot(msgHash, 3000, 45, vTestMiners, emptySols, activeFb0, prevFb0, rot0));
                BOOST_CHECK_EQUAL(rot0, 0);
                // At 75s..104s -> rotationIndex 1, and prevFallback must equal activeFb0 from slot 0
                BOOST_CHECK(GetAdamFallbackCoordinatorForSlot(msgHash, 3000, 75, vTestMiners, emptySols, activeFb1, prevFb1, rot1));
                BOOST_CHECK_EQUAL(rot1, 1);
                BOOST_CHECK(prevFb1 == activeFb0);

                // Balanced Model D net 5% passive Masternode payment after 7% Treasury deduction
                // Height 199 (< UPGRADE_MODELD = 200 on Mainnet -> 0 MN payment)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(199), 0LL);
                // Height 200, 2500, 5000, 7500, 15000 (17 KRISTA block value -> 15.81 KRISTA net -> 0.7905 KRISTA passive MN share)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(200), 79050000LL);
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(2500), 79050000LL);
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(5000), 79050000LL);
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(7500), 79050000LL);
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(15000), 79050000LL);
                // Height 259202 (Period 1: 16.6345 KRISTA -> 15.470085 net -> 0.77350425 KRISTA passive MN share)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(259202), 77350425LL);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(llmq_quorum_anti_equivocation_guard_test)
{
    CKey coordA, coordB;
    coordA.MakeNewKey(true);
    coordB.MakeNewKey(true);
    const uint256 prevHash = uint256S("7777777777777777777777777777777777777777777777777777777777777701");
    const uint256 blockA1 = uint256S("a1");
    const uint256 blockA2 = uint256S("a2");
    const uint256 blockB1 = uint256S("b1");

    // First proposal on a parent is always signable
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blockA1));
    // Re-broadcast of the same block is signable
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blockA1));
    // Equivocating with a different block hash from the same coordinator inside the guard window is refused
    BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blockA2));
    // A competing block from a different coordinator on the same parent inside the guard window is refused
    BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash, coordB.GetPubKey(), blockB1));
    // A different parent is independent
    const uint256 otherPrev = uint256S("7777777777777777777777777777777777777777777777777777777777777702");
    BOOST_CHECK(llmq::AllowQuorumSignature(otherPrev, coordB.GetPubKey(), blockB1));
}

BOOST_AUTO_TEST_CASE(treasury_portal_quorum_signature_interop_test)
{
    // Verify that a 3-of-5 CQuorumSignature (containing CBLSSignedData: 48B blsPubKey + 96B blsSig + DER ecdsaSig)
    // produced by KristaTech-Treasury-Portal (@noble/curves/bls12-381 + elliptic) deserializes and passes CQuorumSignature::Verify
    llmq::CQuorum quorum;
    quorum.nHeight = 1500;
    const std::vector<std::string> memberPubKeys = {
        "031b84c5567b126440995d3ed5aaba0565d71e1834604819ff9c17f5e9d5dd078f",
        "024d4b6cd1361032ca9bd2aeb9d900aa4d45d9ead80ac9423374c451a7254d0766",
        "02531fe6068134503d2723133227c867ac8fa6c83c537e9a44c3c5bdbdcb1fe337",
        "03462779ad4aad39514614751a71085f2f10e1c7a593e4e030efb5b8721ce55b0b",
        "0362c0a046dacce86ddd0343c6d3c7c79c2208ba0d9c9cf24a6d046d21d21f90f7"
    };
    const std::vector<std::string> memberTxHashes = {
        "1010101010101010101010101010101010101010101010101010101010101010",
        "1111111111111111111111111111111111111111111111111111111111111111",
        "1212121212121212121212121212121212121212121212121212121212121212",
        "1313131313131313131313131313131313131313131313131313131313131313",
        "1414141414141414141414141414141414141414141414141414141414141414"
    };
    for (uint32_t i = 0; i < 5; ++i) {
        std::vector<unsigned char> pkBytes = ParseHex(memberPubKeys[i]);
        CPubKey pk(pkBytes.begin(), pkBytes.end());
        BOOST_CHECK(pk.IsValid());
        quorum.members.emplace_back(COutPoint(uint256S(memberTxHashes[i]), i), pk);
    }

    const std::string qsigHex =
        "ffeeddccbbaa00998877665544332211ffeeddccbbaa0099887766554433221103"
        "101010101010101010101010101010101010101010101010101010101010101000000000"
        "d7b91d567a877453e54dced4404472d2c7383e3c269ea6f9fd9e43b3876d4d46bcccd16554067ecf3daef4744178e0c5"
        "5d93b3ba17663e2f4ef790cefc7cb08244723b790aebec628f3a3c7e92aa4bcb9c7aaa930501b5e67641afc024ea9f72"
        "261637f51385d742edf7a982a8fbae9da33e0232d86ae5cdea2c2243a276451c2b5d935c7deed483d80d73a7ec468c34"
        "7046304402203b4bb599ab0672a7d68661be4a3e7199b29f926a8177ebbe53614434c03bf317022042ce6fd3761bbc83453b49f948f5141f9b4d9d2e19d751759f892adfdf568bde"
        "111111111111111111111111111111111111111111111111111111111111111101000000"
        "d8aa81a474ed4588b5b07b2a7c02195bbc9eb5bf40db54bb9053e15f0aa6aff5939cca84778c95f2910cf1cf9cccd4f5"
        "6487d8666d38942aa0cb9a48a63bf58869f15c918f9c5b3f32d42fe635992e800dcb0af576879032c20493e52201c81d"
        "ce0b38ecc90e99d7ef305784fb1cc234ed6859c71596b917b89e72051ea1845bc00b7843b3f96039d4f88b305b8269d6"
        "1c473045022100abe4e7b52432739bc2c24c905ab87a6c2a7df67fa64b5936d26eca4923a2753d02206d82a08c54632a652c0ffb79406268d9fed9b3fd66d50ec9694940d69d2bfdbe"
        "121212121212121212121212121212121212121212121212121212121212121202000000"
        "d7b4bf1219c607c8c87c4d815cee515afc77fa1375692d4582e2c01df5f81710d9ec990a8cbea941d2c48224177d9376"
        "0fa5ffc35dc6040a838edf734ffbdaf12a91df808fbcbdb6a564eee24755c256f14d8117d5ab62e1f36298506d750cd6"
        "ba0853f797b7fec8fdbe935191adfd66e16cb0bb9a11c8e64a6886171bed1b70dbc3839f85ba311473e0d6a62db12e43"
        "81463044022017f0e12092e6975d5345dad4b5f3202c1ce635cbbcec4161653b4b4962177c410220025fef86dfa6389edbd899037c89ed7fa5f41248d23ac4b091e1e61c6e214d39";

    std::vector<unsigned char> qsigBytes = ParseHex(qsigHex);
    CDataStream ss(qsigBytes, SER_NETWORK, PROTOCOL_VERSION);
    llmq::CQuorumSignature qsig;
    ss >> qsig;

    BOOST_CHECK_EQUAL(qsig.blockHash.ToString(), "11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff");
    BOOST_CHECK_EQUAL(qsig.signatures.size(), 3U);
    // 3-of-5 signature is always rejected under mandatory 2/3+1 BFT threshold (4-of-5)
    BOOST_CHECK(!qsig.Verify(quorum));
}

BOOST_AUTO_TEST_CASE(mpa_security_hardening_test)
{
    CBlockIndex indexPrev;
    indexPrev.nHeight = 5500;
    const int nHeight = indexPrev.nHeight + 1;

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();

    // 1. Fake conditional CLTV script (OP_0 OP_IF <lock> OP_CLTV OP_ENDIF ...) must NOT grant PoL weight
    CScript fakeIfCltv = CScript() << OP_0 << OP_IF << CScriptNum(nHeight + 50000) << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_ENDIF
                                   << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint fakeIfOut(uint256S("3333000033330000333300003333000033330000333300003333000033330001"), 0);
    pcoinsTip->AddCoin(fakeIfOut, Coin(CTxOut(100 * COIN, fakeIfCltv), 5400, false, false), true);

    int weightType = -1;
    CAmount fakeWeight = CalculateMPAWeight(fakeIfOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(fakeWeight, 100 * COIN);

    // 2. Expired absolute CLTV lock (lockHeight <= nHeight) must revert to PoS (1x), not relative CSV
    CScript expiredCltv = CScript() << CScriptNum(nHeight - 10) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                    << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint expiredOut(uint256S("3333000033330000333300003333000033330000333300003333000033330002"), 0);
    pcoinsTip->AddCoin(expiredOut, Coin(CTxOut(100 * COIN, expiredCltv), 5000, false, false), true);

    CAmount expiredWeight = CalculateMPAWeight(expiredOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(expiredWeight, 100 * COIN);

    // 3. On-chain Masternode collateral deterministically receives MPA_WEIGHT_POM without mnodeman
    COutPoint onChainMnOut(uint256S("3333000033330000333300003333000033330000333300003333000033330003"), 0);
    CScript mnCollScript = GetScriptForDestination(pubKey.GetID());
    CAmount nCollAmt = CMasternode::GetMasternodeNodeCollateral(nHeight);
    pcoinsTip->AddCoin(onChainMnOut, Coin(CTxOut(nCollAmt, mnCollScript), 4000, false, false), true);
    BOOST_CHECK(RegisterOnChainMasternodePing(pubKey, onChainMnOut, 4000));
    BOOST_CHECK(RegisterOnChainMasternodePing(pubKey, onChainMnOut, 5500));

    CAmount onChainPomWeight = CalculateMPAWeight(onChainMnOut, nCollAmt, GetTime(), &indexPrev, weightType);
    const CAmount limit = Params().GetConsensus().nMasternodeUptimeLimit;
    const CAmount expectedPom = nCollAmt + (CAmount)(((__int128)nCollAmt * 1501) / (4LL * limit));
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_GT(onChainPomWeight, nCollAmt);
    BOOST_CHECK_EQUAL(onChainPomWeight, expectedPom);
    BOOST_CHECK_LE(onChainPomWeight, nCollAmt + nCollAmt / 4);

    // Saturated uptime (nHeight = 200,001, t_active = 196,001 >= limit = 100,000) caps at exact 1.25x (+25%)
    CBlockIndex idxSat;
    idxSat.nHeight = 200000;
    BOOST_CHECK(RegisterOnChainMasternodePing(pubKey, onChainMnOut, 199990));
    CAmount satPomWeight = CalculateMPAWeight(onChainMnOut, nCollAmt, GetTime(), &idxSat, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_EQUAL(satPomWeight, nCollAmt + nCollAmt / 4);

    mapMasternodeLastActiveHeight.erase(pubKey);
    mapMasternodeActiveHeights.erase(pubKey);
    mapMasternodeCollateralOutpoints.erase(pubKey);
    BumpMasternodePingStateGen();
    pcoinsTip->SpendCoin(onChainMnOut);

    // 4. Dual-Track (8 Collateralized + 3 Open PoW) Committee Election & Bidirectional Spillover
    {
        uint256 seed = Hash(std::string("dual_track_test_seed").begin(), std::string("dual_track_test_seed").end());
        std::vector<CPubKey> vMnKeys;
        std::map<CPubKey, CKey> mapMnPrivKeys;
        std::set<CPubKey> colSet;
        for (int i = 0; i < 31; ++i) {
            CKey k;
            k.MakeNewKey(true);
            vMnKeys.push_back(k.GetPubKey());
            mapMnPrivKeys[k.GetPubKey()] = k;
            colSet.insert(k.GetPubKey());
        }
        std::vector<CPubKey> vPowKeys;
        for (int i = 0; i < 100; ++i) {
            CKey k;
            k.MakeNewKey(true);
            vPowKeys.push_back(k.GetPubKey());
        }

        // Case A: 31 MNs (1 coordinator + 30 remaining) + 100 Sybil PoW keys -> 8 Collateralized + 3 Open PoW
        std::vector<CPubKey> poolSybil = vMnKeys;
        poolSybil.insert(poolSybil.end(), vPowKeys.begin(), vPowKeys.end());
        std::vector<CPubKey> electedSybil;
        ElectDualTrackValidators(seed, poolSybil, colSet, vMnKeys[0], 11, electedSybil);
        BOOST_CHECK_EQUAL(electedSybil.size(), 11U);
        int nColElected = 0, nPowElected = 0;
        for (const auto& pk : electedSybil) {
            if (colSet.count(pk)) nColElected++;
            else nPowElected++;
        }
        BOOST_CHECK_EQUAL(nColElected, ADAM_COLLATERALIZED_TRACK_QUOTA); // 8
        BOOST_CHECK_EQUAL(nPowElected, ADAM_OPEN_POW_TRACK_QUOTA);       // 3

        // Case B: 31 MNs + 2 Open PoW keys -> 9 Collateralized (spillover) + 2 Open PoW (both PoW keys elected!)
        std::vector<CPubKey> poolTwoPoW = vMnKeys;
        poolTwoPoW.push_back(vPowKeys[0]);
        poolTwoPoW.push_back(vPowKeys[1]);
        std::vector<CPubKey> electedTwoPoW;
        ElectDualTrackValidators(seed, poolTwoPoW, colSet, vMnKeys[0], 11, electedTwoPoW);
        BOOST_CHECK_EQUAL(electedTwoPoW.size(), 11U);
        nColElected = 0;
        nPowElected = 0;
        for (const auto& pk : electedTwoPoW) {
            if (colSet.count(pk)) nColElected++;
            else nPowElected++;
        }
        BOOST_CHECK_EQUAL(nColElected, 9);
        BOOST_CHECK_EQUAL(nPowElected, 2);

        // Case C: Bootstrap with 4 MNs + 20 Open PoW keys -> 4 Collateralized + 7 Open PoW (spillover)
        std::set<CPubKey> bootstrapColSet(vMnKeys.begin(), vMnKeys.begin() + 4);
        std::vector<CPubKey> poolBootstrap(vMnKeys.begin(), vMnKeys.begin() + 4);
        poolBootstrap.insert(poolBootstrap.end(), vPowKeys.begin(), vPowKeys.begin() + 20);
        std::vector<CPubKey> electedBootstrap;
        ElectDualTrackValidators(seed, poolBootstrap, bootstrapColSet, CPubKey(), 11, electedBootstrap);
        BOOST_CHECK_EQUAL(electedBootstrap.size(), 11U);
        nColElected = 0;
        nPowElected = 0;
        for (const auto& pk : electedBootstrap) {
            if (bootstrapColSet.count(pk)) nColElected++;
            else nPowElected++;
        }
        BOOST_CHECK_EQUAL(nColElected, 4);
        BOOST_CHECK_EQUAL(nPowElected, 7);

        // Case D: 4,200 KRISTA Masternodes (primaryColKeys, 7 seats) + 1,000 KRISTA Coin-Locks (secondaryColKeys, 1 seat) + Open PoW (3 seats)
        std::set<CPubKey> coinLockSet(vPowKeys.begin(), vPowKeys.begin() + 10);
        std::vector<CPubKey> electedPrimaryTrack;
        std::vector<CPubKey> standbyTrack;
        ElectDualTrackValidators(seed, poolSybil, colSet, vMnKeys[0], 11, electedPrimaryTrack, coinLockSet, &standbyTrack);
        BOOST_CHECK_EQUAL(electedPrimaryTrack.size(), 11U);
        BOOST_CHECK(!standbyTrack.empty());
        int nMnElected = 0, nCoinLockElected = 0, nOpenPoWElected = 0;
        for (const auto& pk : electedPrimaryTrack) {
            if (colSet.count(pk)) nMnElected++;
            else if (coinLockSet.count(pk)) nCoinLockElected++;
            else nOpenPoWElected++;
        }
        BOOST_CHECK_EQUAL(nMnElected, ADAM_MASTERNODE_TRACK_QUOTA);       // 7 Active Masternodes
        BOOST_CHECK_EQUAL(nCoinLockElected, ADAM_COIN_LOCK_TRACK_QUOTA);  // 1 Coin-Lock (1,000 KRISTA)
        BOOST_CHECK_EQUAL(nOpenPoWElected, ADAM_OPEN_POW_TRACK_QUOTA);    // 3 Open PoW (10 KRISTA)

        // Case E & F: Post-bootstrap liveness (RAII-guarded Regtest scope)
        struct ScopedMpaChainParams {
            CBaseChainParams::Network prevNet;
            explicit ScopedMpaChainParams(CBaseChainParams::Network targetNet) : prevNet(Params().NetworkID()) {
                SelectParams(targetNet);
            }
            ~ScopedMpaChainParams() {
                SelectParams(prevNet);
            }
        };
        {
            ScopedMpaChainParams regtestGuard(CBaseChainParams::REGTEST);
            const int nPostBootstrapHeight = 50000;
            const int nPostBootstrapThreshold = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamThreshold(nPostBootstrapHeight);
            BOOST_CHECK_EQUAL(nPostBootstrapThreshold, 8);
            BOOST_CHECK_EQUAL(Params().GetConsensus().GetAdamThreshold(nPostBootstrapHeight), 8);

            // Elect a 15-seat Model D primary track committee (8 MN + 2 Coin-Lock + 5 Open PoW)
            std::set<CPubKey> mdCoinLockSet(vPowKeys.begin(), vPowKeys.begin() + 10);
            std::vector<CPubKey> electedModelDTrack, standbyModelDTrack;
            ElectDualTrackValidators(seed, poolSybil, colSet, vMnKeys[0], Consensus::Params::ADAM_MODELD_MINERS_COUNT,
                                     electedModelDTrack, mdCoinLockSet, &standbyModelDTrack, nullptr, nullptr, false,
                                     Consensus::Params::ADAM_MODELD_MASTERNODE_TRACK_QUOTA,
                                     Consensus::Params::ADAM_MODELD_COIN_LOCK_TRACK_QUOTA,
                                     Consensus::Params::ADAM_MODELD_ZERO_COIN_OPEN_SEATS);
            BOOST_REQUIRE_EQUAL(electedModelDTrack.size(), 15U);
            BOOST_REQUIRE_GE(standbyModelDTrack.size(), 2U);

            uint256 prevHash = Hash(std::string("post_bootstrap_prev_block").begin(), std::string("post_bootstrap_prev_block").end());
            auto buildValidV12Solution = [&](const CPubKey& minerPk, const CKey& minerPrivKey, int idx, uint32_t nNonce) -> std::vector<unsigned char> {
                CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(minerPrivKey, prevHash);
                CDataStream ssInput(SER_GETHASH, 0);
                ssInput << seed << minerPk << nNonce;

                int algo1 = -1, algo2 = -1, algo3 = -1;
                GetAdam3PermutationAlgos(prevHash, minerPk, algo1, algo2, algo3);
                int i_factor = idx + 1;
                uint256 hash3 = CalculateAdamPuzzleHash(algo3,
                    reinterpret_cast<const unsigned char*>(&ssInput[0]),
                    reinterpret_cast<const unsigned char*>(&ssInput[0]) + ssInput.size());
                uint256 multiplied1 = ArithToUint256(UintToArith256(hash3) * i_factor);
                uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
                uint256 multiplied2 = ArithToUint256(UintToArith256(hash2) * i_factor);
                uint256 puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);

                std::vector<unsigned char> vchSig;
                BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHash, minerPrivKey, blsSk, vchSig, GetBLSDomainTag(BLSDomain::ADAM_SOL)));

                CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
                ssSol << nNonce << vchSig;
                return std::vector<unsigned char>(ssSol.begin(), ssSol.end());
            };

            // Case E: All 7 non-Masternode seats withhold solutions, 8 Masternodes online -> 8 >= 8
            std::vector<std::vector<unsigned char>> vSolutions(electedModelDTrack.size());
            int nWithheldNonMnSeats = 0;
            for (size_t idx = 0; idx < electedModelDTrack.size(); ++idx) {
                const CPubKey& minerPk = electedModelDTrack[idx];
                if (!colSet.count(minerPk)) {
                    nWithheldNonMnSeats++;
                    continue;
                }
                vSolutions[idx] = buildValidV12Solution(minerPk, mapMnPrivKeys.at(minerPk), (int)idx, 100 + (uint32_t)idx);
            }

            int nValidSolutions = 0;
            for (size_t idx = 0; idx < electedModelDTrack.size(); ++idx) {
                if (!vSolutions[idx].empty()) {
                    BOOST_CHECK(VerifyAdamSolution(prevHash, seed, electedModelDTrack[idx], vSolutions[idx], 0x207fffff, 12, nPostBootstrapHeight, (int)idx));
                    nValidSolutions++;
                }
            }
            BOOST_CHECK_EQUAL(nWithheldNonMnSeats, Consensus::Params::ADAM_MODELD_COIN_LOCK_TRACK_QUOTA + Consensus::Params::ADAM_MODELD_OPEN_POW_TRACK_QUOTA); // 7 withheld
            BOOST_CHECK_EQUAL(nValidSolutions, Consensus::Params::ADAM_MODELD_MASTERNODE_TRACK_QUOTA); // 8 valid MN solutions
            BOOST_CHECK_GE(nValidSolutions, nPostBootstrapThreshold);                                  // 8 >= 8 quorum met!

            // Case F: 8 seats are silent (1 MN offline + 7 non-MN seats withholding -> only 7 primary solutions < 8).
            // Verify standby MN substitution at t = 60s restores 8 >= 8, cross-track and skipped-standby are rejected,
            // and Model D post-bootstrap floor holds at 8 (> 7 non-MN seats).
            std::map<CPubKey, std::vector<unsigned char>> mapSevenPrimarySols;
            int nDroppedMn = 0;
            int droppedMnSeatIdx = -1;
            int firstSilentNonMnSeatIdx = -1;
            for (size_t idx = 0; idx < electedModelDTrack.size(); ++idx) {
                const CPubKey& minerPk = electedModelDTrack[idx];
                if (!colSet.count(minerPk)) {
                    if (firstSilentNonMnSeatIdx < 0) {
                        firstSilentNonMnSeatIdx = (int)idx;
                    }
                    continue;
                }
                if (nDroppedMn == 0) {
                    droppedMnSeatIdx = (int)idx;
                    nDroppedMn++;
                    continue;
                }
                mapSevenPrimarySols[minerPk] = vSolutions[idx];
            }
            BOOST_CHECK_EQUAL(mapSevenPrimarySols.size(), 7U);
            BOOST_REQUIRE_GE(droppedMnSeatIdx, 0);
            BOOST_REQUIRE_GE(firstSilentNonMnSeatIdx, 0);

            const CPubKey& standbyMnPk = standbyModelDTrack[0];
            BOOST_CHECK(colSet.count(standbyMnPk) > 0);
            std::map<CPubKey, std::vector<unsigned char>> mapWithStandbySol = mapSevenPrimarySols;
            mapWithStandbySol[standbyMnPk] = buildValidV12Solution(standbyMnPk, mapMnPrivKeys.at(standbyMnPk), droppedMnSeatIdx, 999);

            std::vector<CPubKey> vResolvedEarly;
            std::vector<std::vector<unsigned char>> vResolvedSolsEarly;
            int nValidEarly = 0;
            ResolveAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, 30, 0x207fffff, 12,
                electedModelDTrack, standbyModelDTrack, mapWithStandbySol, vResolvedEarly, vResolvedSolsEarly, nValidEarly, &colSet, &mdCoinLockSet);
            BOOST_CHECK_EQUAL(nValidEarly, 7);
            BOOST_CHECK(vResolvedEarly == electedModelDTrack);

            std::vector<CPubKey> vResolvedTimeout;
            std::vector<std::vector<unsigned char>> vResolvedSolsTimeout;
            int nValidTimeout = 0;
            ResolveAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS, 0x207fffff, 12,
                electedModelDTrack, standbyModelDTrack, mapWithStandbySol, vResolvedTimeout, vResolvedSolsTimeout, nValidTimeout, &colSet, &mdCoinLockSet);
            BOOST_CHECK_EQUAL(nValidTimeout, 8);
            BOOST_CHECK(vResolvedTimeout[droppedMnSeatIdx] == standbyMnPk);
            BOOST_CHECK(ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vResolvedTimeout, vResolvedSolsTimeout, electedModelDTrack, standbyModelDTrack, &colSet, &mdCoinLockSet));
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, 30,
                0x207fffff, 12, vResolvedTimeout, vResolvedSolsTimeout, electedModelDTrack, standbyModelDTrack, &colSet, &mdCoinLockSet));

            std::vector<CPubKey> vCrossTrackMiners = electedModelDTrack;
            std::vector<std::vector<unsigned char>> vCrossTrackSols = vResolvedSolsEarly;
            vCrossTrackMiners[firstSilentNonMnSeatIdx] = standbyMnPk;
            vCrossTrackSols[firstSilentNonMnSeatIdx] = buildValidV12Solution(standbyMnPk, mapMnPrivKeys.at(standbyMnPk), firstSilentNonMnSeatIdx, 1001);
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vCrossTrackMiners, vCrossTrackSols, electedModelDTrack, standbyModelDTrack, &colSet, &mdCoinLockSet));

            const CPubKey& secondStandbyMnPk = standbyModelDTrack[1];
            BOOST_CHECK(colSet.count(secondStandbyMnPk) > 0);
            std::vector<CPubKey> vSkippedStandbyMiners = electedModelDTrack;
            std::vector<std::vector<unsigned char>> vSkippedStandbySols = vResolvedSolsEarly;
            vSkippedStandbyMiners[droppedMnSeatIdx] = secondStandbyMnPk;
            vSkippedStandbySols[droppedMnSeatIdx] = buildValidV12Solution(secondStandbyMnPk, mapMnPrivKeys.at(secondStandbyMnPk), droppedMnSeatIdx, 1002);
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vSkippedStandbyMiners, vSkippedStandbySols, electedModelDTrack, standbyModelDTrack, &colSet, &mdCoinLockSet));
            BOOST_CHECK(ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS + ADAM_COMMITTEE_FALLBACK_SLOT_SECS,
                0x207fffff, 12, vSkippedStandbyMiners, vSkippedStandbySols, electedModelDTrack, standbyModelDTrack, &colSet, &mdCoinLockSet));

            const int nFloorPostBootstrap = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(nPostBootstrapHeight, 600, 10);
            BOOST_CHECK_EQUAL(nFloorPostBootstrap, Consensus::Params::ADAM_MODELD_MIN_RELAXED_THRESHOLD);
            BOOST_CHECK_EQUAL(nFloorPostBootstrap, 8);
            BOOST_CHECK_GT(nFloorPostBootstrap, Consensus::Params::ADAM_MODELD_COIN_LOCK_TRACK_QUOTA + Consensus::Params::ADAM_MODELD_OPEN_POW_TRACK_QUOTA);

            // Model D bootstrap window (h = 10000): under UPGRADE_HARDENING_V4, >=8 MNs enforces 8 > 7 non-MN seats,
            // while <8 MNs preserves bootstrap base 5 and floor 3.
            BOOST_CHECK_EQUAL(Params(CBaseChainParams::MAIN).GetConsensus().GetAdamThreshold(10000, 8), 8);
            BOOST_CHECK_EQUAL(Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(10000, 600, 8), 8);
            BOOST_CHECK_EQUAL(Params(CBaseChainParams::MAIN).GetConsensus().GetAdamThreshold(10000, 4), 5);
            BOOST_CHECK_EQUAL(Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(10000, 600, 4), 3);

            // Pre-Model D bootstrap window (explicit UPGRADE_MODELD=500 baseline at h = 205): floor 5 with 7 MNs, floor 4/3 with 0 MNs
            Consensus::Params preModelDMainParams = Params(CBaseChainParams::MAIN).GetConsensus();
            preModelDMainParams.vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight = 500;
            const int nFloorBootstrapWith7Mns = preModelDMainParams.GetAdamEffectiveThreshold(205, 600, 7);
            BOOST_CHECK_EQUAL(nFloorBootstrapWith7Mns, Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD);
            BOOST_CHECK_EQUAL(nFloorBootstrapWith7Mns, 5);

            const int nRelaxedBootstrapNoMns = preModelDMainParams.GetAdamEffectiveThreshold(205, ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS, 0);
            BOOST_CHECK_EQUAL(nRelaxedBootstrapNoMns, 4);
            const int nFloorBootstrapNoMns = preModelDMainParams.GetAdamEffectiveThreshold(205, 600, 0);
            BOOST_CHECK_EQUAL(nFloorBootstrapNoMns, Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP);
            BOOST_CHECK_EQUAL(nFloorBootstrapNoMns, 3);

            // Case G (Balanced Model D: 5% Passive MN / 10% LLMQ / 60% Equal-Work Validator across 15 seats [4% each] / 25% Producer):
            {
                const int nModelDTestHeight = 600;
                BOOST_CHECK(IsModelDActive(nModelDTestHeight));

                CBlockIndex* pindexPrevTest = new CBlockIndex();
                pindexPrevTest->nHeight = nModelDTestHeight - 1;
                pindexPrevTest->nTime = 1780000000;
                uint256 testPrevHash = Hash(std::string("r34_modeld_equal_work_prev").begin(), std::string("r34_modeld_equal_work_prev").end());
                {
                    LOCK(cs_main);
                    auto ins = mapBlockIndex.insert(std::make_pair(testPrevHash, pindexPrevTest));
                    pindexPrevTest->phashBlock = &(ins.first->first);
                }

                uint256 modelDSeed = GetAdamSeed(pindexPrevTest);
                std::vector<CPubKey> vElectedModelD, vStandbyModelD;
                CPubKey coordModelD;
                BOOST_REQUIRE(SelectAdamNodes(modelDSeed, Params().GetConsensus(), vElectedModelD, coordModelD, nModelDTestHeight, pcoinsTip, &vStandbyModelD));
                BOOST_REQUIRE_EQUAL(vElectedModelD.size(), 15U);
                BOOST_REQUIRE(!vStandbyModelD.empty());

                auto findDetKey = [&](const CPubKey& pk) -> CKey {
                    for (int i = 0; i < 24; ++i) {
                        CKey k = GetAdamDeterministicKey(i);
                        if (k.GetPubKey() == pk) return k;
                    }
                    return CKey();
                };

                // Build a 15-seat Model D block at t = 60s where seat 0 is substituted by standby vStandbyModelD[0], seats 1..7 are solved (8/15 solved, 7 empty).
                CBlock testBlock;
                testBlock.nVersion = 12;
                testBlock.hashPrevBlock = testPrevHash;
                testBlock.nTime = pindexPrevTest->nTime + ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS;
                testBlock.nBits = 0x207fffff;
                testBlock.vAdamMiners = vElectedModelD;
                testBlock.vAdamMiners[0] = vStandbyModelD[0];
                testBlock.vAdamSolutions.resize(15);

                std::vector<CPubKey> vSolvedMiners;
                for (int seat = 0; seat < 8; ++seat) {
                    const CPubKey& seatPk = testBlock.vAdamMiners[seat];
                    CKey seatKey = findDetKey(seatPk);
                    BOOST_REQUIRE(seatKey.IsValid());

                    CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(seatKey, testPrevHash);
                    uint32_t nNonce = 500 + seat;
                    CDataStream ssIn(SER_GETHASH, 0);
                    ssIn << modelDSeed << seatPk << nNonce;
                    int a1 = -1, a2 = -1, a3 = -1;
                    GetAdam3PermutationAlgos(testPrevHash, seatPk, a1, a2, a3);
                    int i_factor = seat + 1;
                    uint256 h3 = CalculateAdamPuzzleHash(a3,
                        reinterpret_cast<const unsigned char*>(&ssIn[0]),
                        reinterpret_cast<const unsigned char*>(&ssIn[0]) + ssIn.size());
                    uint256 m1 = ArithToUint256(UintToArith256(h3) * i_factor);
                    uint256 h2 = CalculateAdamPuzzleHash(a2, m1.begin(), m1.begin() + 32);
                    uint256 m2 = ArithToUint256(UintToArith256(h2) * i_factor);
                    uint256 pHash = CalculateAdamPuzzleHash(a1, m2.begin(), m2.begin() + 32);

                    std::vector<unsigned char> vchSig;
                    BOOST_CHECK(SignBLSWithECDSAFallback(pHash, seatKey, blsSk, vchSig, GetBLSDomainTag(BLSDomain::ADAM_SOL)));
                    CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
                    ssSol << nNonce << vchSig;
                    testBlock.vAdamSolutions[seat] = std::vector<unsigned char>(ssSol.begin(), ssSol.end());
                    vSolvedMiners.push_back(seatPk);
                }

                // Verify CBlockHeader::GetHash() includes all 15 v12 seats in non-fallback mode (sensitive to miner[14])
                const uint256 hash15A = testBlock.GetHash();
                CBlock testBlockAlt = testBlock;
                testBlockAlt.vAdamMiners[14] = coordModelD;
                BOOST_CHECK(hash15A != testBlockAlt.GetHash());

                BOOST_CHECK(ValidateAdamCommitteeForTime(
                    testPrevHash, modelDSeed, nModelDTestHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                    testBlock.nBits, testBlock.nVersion, testBlock.vAdamMiners, testBlock.vAdamSolutions,
                    vElectedModelD, vStandbyModelD, nullptr, nullptr, pcoinsTip));

                CMutableTransaction coinbaseTx;
                coinbaseTx.vin.resize(1);
                coinbaseTx.vin[0].prevout.SetNull();
                coinbaseTx.vout.resize(1);
                coinbaseTx.vout[0].scriptPubKey = GetScriptForDestination(coordModelD.GetID());
                FillBlockPayee(coinbaseTx, pindexPrevTest, false, &vSolvedMiners);
                testBlock.vtx.push_back(coinbaseTx);

                BOOST_CHECK(IsBlockPayeeValid(testBlock, nModelDTestHeight, pcoinsTip));

                CAmount nBlockValActual = CMasternode::GetBlockValue(nModelDTestHeight);
                CAmount nTreasurySplit = nBlockValActual * 7 / 100;
                CAmount nBlockVal = nBlockValActual - nTreasurySplit;
                CAmount nMNSplit = nBlockVal * 5 / 100;
                CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
                CAmount nPartSplitTotal = nBlockVal * 60 / 100;
                CAmount nSeatBase = nPartSplitTotal / 15;
                CAmount nPaidPartTotal = 8 * nSeatBase;
                CAmount nExpectedUnclaimed = (nPartSplitTotal - nPaidPartTotal);
                CAmount nExpectedBurn = nExpectedUnclaimed / 2;
                CAmount nExpectedUnclaimedTreasury = nExpectedUnclaimed - nExpectedBurn;
                CAmount nExpectedCoord = nBlockVal - (nMNSplit + nLLMQSplitTotal + nPartSplitTotal);

                BOOST_CHECK_EQUAL(testBlock.vtx[0].vout[0].nValue, nExpectedCoord);
                BOOST_CHECK_EQUAL(testBlock.vtx[0].vout[1].nValue, nMNSplit);
                CScript standbyScript = GetScriptForDestination(vStandbyModelD[0].GetID());
                bool foundStandbyPayee = false;
                for (const auto& out : testBlock.vtx[0].vout) {
                    if (out.scriptPubKey == standbyScript && out.nValue == nSeatBase) {
                        foundStandbyPayee = true;
                        break;
                    }
                }
                BOOST_CHECK(foundStandbyPayee);

                CScript treasuryScript = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
                bool foundTreasuryWithUnclaimed = false;
                for (const auto& out : testBlock.vtx[0].vout) {
                    if (out.scriptPubKey == treasuryScript && out.nValue == nTreasurySplit + nExpectedUnclaimedTreasury) {
                        foundTreasuryWithUnclaimed = true;
                        break;
                    }
                }
                BOOST_CHECK(foundTreasuryWithUnclaimed);

                const std::string& strBurnAddr = Params().GetConsensus().mBurnAddresses.begin()->first;
                CScript burnScript = GetScriptForDestination(DecodeDestination(strBurnAddr));
                bool foundProtocolBurn = false;
                for (const auto& out : testBlock.vtx[0].vout) {
                    if (out.scriptPubKey == burnScript && out.nValue == nExpectedBurn) {
                        foundProtocolBurn = true;
                        break;
                    }
                }
                BOOST_CHECK(foundProtocolBurn);

                // Attempting to divert nExpectedUnclaimed from Developer Treasury / Protocol Burn to Passive MN Winner must be rejected!
                CBlock badDivertBlock = testBlock;
                CMutableTransaction badTx(badDivertBlock.vtx[0]);
                badTx.vout[1].nValue += nExpectedUnclaimedTreasury;
                for (auto& out : badTx.vout) {
                    if (out.scriptPubKey == treasuryScript) {
                        out.nValue = nTreasurySplit;
                    }
                }
                badDivertBlock.vtx[0] = badTx;
                BOOST_CHECK(!IsBlockPayeeValid(badDivertBlock, nModelDTestHeight, pcoinsTip));

                // Rev 44 (Section 1): Model D payouts are unconditionally enforced in consensus
                // once UPGRADE_HARDENING_V1 is active: stripping MN/LLMQ/Validator outputs to pay only Producer+Treasury MUST fail!
                CBlock badProducerOnlyBlock = testBlock;
                CMutableTransaction badProdTx;
                badProdTx.vin = testBlock.vtx[0].vin;
                badProdTx.vout.push_back(CTxOut(nBlockVal, GetScriptForDestination(coordModelD.GetID())));
                badProdTx.vout.push_back(CTxOut(nTreasurySplit, treasuryScript));
                badProducerOnlyBlock.vtx[0] = badProdTx;
                BOOST_CHECK(!IsBlockPayeeValid(badProducerOnlyBlock, nModelDTestHeight, pcoinsTip));

                // M-07: Substituting a canonical validator P2PKH scriptPubKey with a non-canonical script carrying the same CKeyID (e.g. TX_PUBKEY) must fail!
                {
                    CBlock badScriptSubstBlock = testBlock;
                    CMutableTransaction substTx(badScriptSubstBlock.vtx[0]);
                    bool fReplaced = false;
                    const CScript p2pkSameKeyId = CScript() << ToByteVector(vStandbyModelD[0]) << OP_CHECKSIG;
                    for (auto& out : substTx.vout) {
                        if (out.scriptPubKey == standbyScript && out.nValue == nSeatBase) {
                            out.scriptPubKey = p2pkSameKeyId;
                            fReplaced = true;
                            break;
                        }
                    }
                    BOOST_REQUIRE(fReplaced);
                    badScriptSubstBlock.vtx[0] = substTx;
                    BOOST_CHECK(!IsBlockPayeeValid(badScriptSubstBlock, nModelDTestHeight, pcoinsTip));
                }

                {
                    LOCK(cs_main);
                    mapBlockIndex.erase(testPrevHash);
                }
                delete pindexPrevTest;
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(pre_modeld_adam_validator_split_and_tier_payout_test)
{
    ScopedMpaChainParams regParams(CBaseChainParams::REGTEST);
    const int origModelDHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight;
    struct ScopedModelDHeightRestore {
        int prevHeight;
        ~ScopedModelDHeightRestore() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_MODELD, prevHeight);
        }
    } restoreModelD{origModelDHeight};

    // Temporarily move UPGRADE_MODELD to 500 on Regtest so heights 200..499 exercise Pre-Model-D Cooperative ADAM (Fix A & Fix B)
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_MODELD, 500);
    BOOST_CHECK(!IsAdamValidatorSplitActive(199));
    BOOST_CHECK(IsAdamValidatorSplitActive(200));
    BOOST_CHECK(IsAdamValidatorSplitActive(250));
    BOOST_CHECK(!IsModelDActive(250));
    BOOST_CHECK(IsModelDActive(500));

    auto findDetKey = [](const CPubKey& pk) -> CKey {
        for (int i = 0; i < 24; ++i) {
            CKey k = GetAdamDeterministicKey(i);
            if (k.GetPubKey() == pk) return k;
        }
        return CKey();
    };

    // 1. Fix A: Pre-Model-D V11 Block (h = 250) enforces 60% Producer + 40% Active Validators + 50/50 Unclaimed Burn/Treasury
    {
        const int nTestHeight = 250;
        CBlockIndex* pindexPrev = new CBlockIndex();
        pindexPrev->nHeight = nTestHeight - 1;
        pindexPrev->nTime = 1785000000;
        const uint256 prevHash = Hash(std::string("pre_modeld_v11_split_prev").begin(), std::string("pre_modeld_v11_split_prev").end());
        {
            LOCK(cs_main);
            auto ins = mapBlockIndex.insert(std::make_pair(prevHash, pindexPrev));
            pindexPrev->phashBlock = &(ins.first->first);
        }

        const uint256 seed = GetAdamSeed(pindexPrev);
        std::vector<CPubKey> vElected, vStandby;
        CPubKey coord;
        BOOST_REQUIRE(SelectAdamNodes(seed, Params().GetConsensus(), vElected, coord, nTestHeight, pcoinsTip, &vStandby));
        BOOST_REQUIRE_EQUAL(vElected.size(), 11U);

        // V11 header layout: 11 committee seats + Coordinator appended as 12th element in vAdamMiners, 11 entries in vAdamSolutions
        CBlock v11Block;
        v11Block.nVersion = 11;
        v11Block.hashPrevBlock = prevHash;
        v11Block.nTime = pindexPrev->nTime + 20;
        v11Block.nBits = 0x207fffff;
        v11Block.vAdamMiners = vElected;
        v11Block.vAdamMiners.push_back(coord);
        v11Block.vAdamSolutions.resize(11);

        std::vector<CPubKey> vSolvedMiners;
        for (int seat = 0; seat < 7; ++seat) {
            const CPubKey& seatPk = vElected[seat];
            CKey seatKey = findDetKey(seatPk);
            BOOST_REQUIRE(seatKey.IsValid());

            CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(seatKey, prevHash);
            const uint32_t nNonce = 700 + seat;
            CDataStream ssIn(SER_GETHASH, 0);
            ssIn << seed << seatPk << nNonce;
            const int algoIdx = GetAdamPuzzleAlgo(seed, seatPk);
            const uint256 pHash = CalculateAdamPuzzleHash(algoIdx,
                reinterpret_cast<const unsigned char*>(&ssIn[0]),
                reinterpret_cast<const unsigned char*>(&ssIn[0]) + ssIn.size());

            std::vector<unsigned char> vchSig;
            BOOST_CHECK(SignBLSWithECDSAFallback(pHash, seatKey, blsSk, vchSig, GetBLSDomainTag(BLSDomain::ADAM_SOL)));
            CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
            ssSol << nNonce << vchSig;
            v11Block.vAdamSolutions[seat] = std::vector<unsigned char>(ssSol.begin(), ssSol.end());
            vSolvedMiners.push_back(seatPk);
        }

        CMutableTransaction cbTx;
        cbTx.vin.resize(1);
        cbTx.vin[0].prevout.SetNull();
        cbTx.vout.resize(1);
        cbTx.vout[0].scriptPubKey = GetScriptForDestination(coord.GetID());
        FillBlockPayee(cbTx, pindexPrev, false, &vSolvedMiners);
        v11Block.vtx.push_back(cbTx);

        BOOST_CHECK(IsBlockPayeeValid(v11Block, nTestHeight, pcoinsTip));

        const CAmount nBlockValActual = CMasternode::GetBlockValue(nTestHeight);
        const CAmount nTreasurySplit = nBlockValActual * 7 / 100;
        const CAmount nBlockVal = nBlockValActual - nTreasurySplit;
        const CAmount nPartSplitTotal = nBlockVal * 75 / 100;
        const CAmount nExpectedProducer = nBlockVal - nPartSplitTotal; // 25% of Net Pool under UPGRADE_FAIRNESS!
        const CAmount nSeatBase = nPartSplitTotal / 11;
        const CAmount nPaidPartTotal = 7 * nSeatBase;
        const CAmount nExpectedUnclaimed = nPartSplitTotal - nPaidPartTotal;
        const CAmount nExpectedBurn = nExpectedUnclaimed / 2;
        const CAmount nExpectedUnclaimedTreasury = nExpectedUnclaimed - nExpectedBurn;

        BOOST_CHECK_EQUAL(v11Block.vtx[0].vout[0].nValue, nExpectedProducer);

        const CScript treasuryScript = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        const CScript burnScript = GetScriptForDestination(DecodeDestination(Params().GetConsensus().mBurnAddresses.begin()->first));
        bool foundTreasury = false, foundBurn = false;
        for (const auto& out : v11Block.vtx[0].vout) {
            if (out.scriptPubKey == treasuryScript && out.nValue == nTreasurySplit + nExpectedUnclaimedTreasury) foundTreasury = true;
            if (out.scriptPubKey == burnScript && out.nValue == nExpectedBurn) foundBurn = true;
        }
        BOOST_CHECK(foundTreasury);
        BOOST_CHECK(foundBurn);

        // Reject 100% Producer-only coinbase that strips the 40% Validator split
        CBlock bad100PctProducer = v11Block;
        CMutableTransaction badProdTx;
        badProdTx.vin = v11Block.vtx[0].vin;
        badProdTx.vout.push_back(CTxOut(nBlockVal, GetScriptForDestination(coord.GetID())));
        badProdTx.vout.push_back(CTxOut(nTreasurySplit, treasuryScript));
        bad100PctProducer.vtx[0] = badProdTx;
        BOOST_CHECK(!IsBlockPayeeValid(bad100PctProducer, nTestHeight, pcoinsTip));

        // Reject diverting unclaimed validator shares to Producer instead of 50/50 Burn + Treasury
        CBlock badUnclaimedToProducer = v11Block;
        CMutableTransaction badUnclaimedTx(v11Block.vtx[0]);
        badUnclaimedTx.vout[0].nValue += nExpectedBurn;
        for (auto it = badUnclaimedTx.vout.begin(); it != badUnclaimedTx.vout.end(); ) {
            if (it->scriptPubKey == burnScript) it = badUnclaimedTx.vout.erase(it);
            else ++it;
        }
        badUnclaimedToProducer.vtx[0] = badUnclaimedTx;
        BOOST_CHECK(!IsBlockPayeeValid(badUnclaimedToProducer, nTestHeight, pcoinsTip));

        {
            LOCK(cs_main);
            mapBlockIndex.erase(prevHash);
        }
        delete pindexPrev;
    }

    // 2. Fix B: Strict Collateral-Tier Payout Priority when < 7 Masternodes (250 KRISTA Coin-Lock -> 10 KRISTA PoW-Lock -> Bootstrap/Zero-Coin)
    {
        LOCK(cs_main);
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

        auto prependV4Bls = [](const CKey& key, const CScript& inner) -> CScript {
            CBLSSecretKey blsSk = DeriveBLSFromCKey(key);
            CBLSPubKey blsPk(blsSk);
            CBLSSignature blsPoP;
            BOOST_REQUIRE(blsPoP.SignPoP(blsSk));
            CScript out;
            out << std::vector<unsigned char>(blsPk.begin(), blsPk.end())
                << std::vector<unsigned char>(blsPoP.begin(), blsPoP.end())
                << OP_2DROP;
            out.insert(out.end(), inner.begin(), inner.end());
            return out;
        };
        auto makeCoinLockScript = [&](const CKey& k, int64_t lockHeight) -> CScript {
            const CPubKey pk = k.GetPubKey();
            return prependV4Bls(k, CScript() << std::vector<unsigned char>(pk.begin(), pk.end())
                                             << OP_DROP
                                             << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                             << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG);
        };
        auto makePoWLockScript = [&](const CKey& k, const uint256& chHash, uint32_t nNonce, int64_t lockHeight) -> CScript {
            const CPubKey pk = k.GetPubKey();
            std::vector<unsigned char> vchNonce = {
                (unsigned char)(nNonce & 0xff),
                (unsigned char)((nNonce >> 8) & 0xff),
                (unsigned char)((nNonce >> 16) & 0xff),
                (unsigned char)((nNonce >> 24) & 0xff)
            };
            return prependV4Bls(k, CScript() << vchNonce
                                             << std::vector<unsigned char>(chHash.begin(), chHash.end())
                                             << std::vector<unsigned char>(pk.begin(), pk.end())
                                             << OP_DROP << OP_DROP << OP_DROP
                                             << CScriptNum(lockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                             << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG);
        };
        auto appendBlock = [&](const std::vector<CTransaction>& extraTxs) -> CBlockIndex* {
            const int h = pPrev->nHeight + 1;
            CBlock blk;
            blk.nVersion = 11;
            blk.hashPrevBlock = pPrev->GetBlockHash();
            blk.nTime = pPrev->nTime + 30;
            blk.nNonce = (uint32_t)h;
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk.vtx.push_back(cb);
            for (const auto& tx : extraTxs) blk.vtx.push_back(tx);
            const uint256 bh = blk.GetHash();
            vHashes.push_back(bh);
            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = pPrev;
            auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            vIndices.push_back(pIdx);
            chainActive.SetTip(pIdx);
            RegisterBlockMasternodeActivity(blk, h, pcoinsTip);
            pPrev = pIdx;
            return pIdx;
        };

        while (pPrev->nHeight < 243) {
            appendBlock({});
        }

        // Blocks 244..247: Register 4 Coin-Locks (250 KRISTA each, 1 per block)
        std::set<CPubKey> clSet, fpSet;
        for (int i = 0; i < 4; ++i) {
            const int hCl = pPrev->nHeight + 1;
            CKey k;
            k.MakeNewKey(true);
            const CPubKey pk = k.GetPubKey();
            clSet.insert(pk);
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(COutPoint(uint256S("8100000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i)));
            mtx.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, makeCoinLockScript(k, hCl + 2900)));
            CTransaction ctx(mtx);
            COutPoint outp(ctx.GetHash(), 0);
            pcoinsTip->AddCoin(outp, Coin(ctx.vout[0], hCl, false, false), true);
            vAddedCoins.push_back(outp);
            appendBlock({ctx});
        }

        // Blocks 248..250: Register 3 Funded PoW-Locks (10 KRISTA each, 1 per block, UPGRADE_HARDENING_V2 HashX11KVS + V4 BLS)
        const uint256 targetReg = GetMinerPoWLimit(Params().NetworkIDString());
        for (int i = 0; i < 3; ++i) {
            const int hFp = pPrev->nHeight + 1;
            const uint256 chFp = pPrev->GetBlockHash();
            CKey k;
            k.MakeNewKey(true);
            const CPubKey pk = k.GetPubKey();
            fpSet.insert(pk);
            const CBLSPubKey blsPk(DeriveBLSFromCKey(k));
            uint32_t nonce = 0;
            for (;; ++nonce) {
                if (ComputeMinerPoWRegistrationHash(nonce, chFp, pk, blsPk) <= targetReg) break;
            }
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(COutPoint(uint256S("8200000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i)));
            mtx.vout.push_back(CTxOut(POW_LOCK_REGISTRATION_AMOUNT, makePoWLockScript(k, chFp, nonce, hFp + 2900)));
            CTransaction ctx(mtx);
            COutPoint outp(ctx.GetHash(), 0);
            pcoinsTip->AddCoin(outp, Coin(ctx.vout[0], hFp, false, false), true);
            vAddedCoins.push_back(outp);
            appendBlock({ctx});
        }

        const int nEvalHeight = pPrev->nHeight + 1; // 251
        BOOST_REQUIRE_EQUAL(nEvalHeight, 251);

        // Choose a seed where the VRF-top Coin-Lock is elected Coordinator:
        // Committee MUST contain strictly 1 Coin-Lock (Sub-track 1B) + 3 Funded PoW-Locks (Track 2) + 7 Bootstrap spillover seats!
        std::vector<CPubKey> vElected;
        CPubKey coord;
        bool foundClCoordSeed = false;
        for (int s = 0; s < 500; ++s) {
            CHashWriter ssSeed(SER_GETHASH, 0);
            ssSeed << uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff") << s;
            if (SelectAdamNodes(ssSeed.GetHash(), Params().GetConsensus(), vElected, coord, nEvalHeight, pcoinsTip) && clSet.count(coord)) {
                foundClCoordSeed = true;
                break;
            }
        }
        BOOST_REQUIRE(foundClCoordSeed);

        // Deliberately pass vSolvedMiners in REVERSE collateral order (Bootstrap FIRST -> 10 KRISTA SECOND -> 250 KRISTA LAST)
        std::vector<CPubKey> vBootSolved, vFpSolved, vClSolved;
        for (const CPubKey& pk : vElected) {
            if (clSet.count(pk)) vClSolved.push_back(pk);
            else if (fpSet.count(pk)) vFpSolved.push_back(pk);
            else vBootSolved.push_back(pk);
        }
        BOOST_REQUIRE_EQUAL(vClSolved.size(), 1U);
        BOOST_REQUIRE_EQUAL(vFpSolved.size(), 3U);
        BOOST_REQUIRE_EQUAL(vBootSolved.size(), 7U);

        std::vector<CPubKey> vReversedSolved;
        vReversedSolved.insert(vReversedSolved.end(), vBootSolved.begin(), vBootSolved.end());
        vReversedSolved.insert(vReversedSolved.end(), vFpSolved.begin(), vFpSolved.end());
        vReversedSolved.insert(vReversedSolved.end(), vClSolved.begin(), vClSolved.end());

        CMutableTransaction cbTx;
        cbTx.vin.resize(1);
        cbTx.vin[0].prevout.SetNull();
        cbTx.vout.resize(1);
        cbTx.vout[0].scriptPubKey = GetScriptForDestination(coord.GetID());
        FillBlockPayee(cbTx, pPrev, false, &vReversedSolved);

        const CAmount nBlockValActual = CMasternode::GetBlockValue(nEvalHeight);
        const CAmount nBlockVal = nBlockValActual - (nBlockValActual * 7 / 100);
        const CAmount nPartSplitTotal = nBlockVal * 75 / 100;
        const CAmount nSeatBase = nPartSplitTotal / 11;
        const CAmount nSeatRem = nPartSplitTotal % 11;

        auto getPayeeAmount = [&](const CPubKey& pk) -> CAmount {
            const CScript scr = GetScriptForDestination(pk.GetID());
            for (size_t idx = 1; idx < cbTx.vout.size(); ++idx) {
                if (cbTx.vout[idx].scriptPubKey == scr) return cbTx.vout[idx].nValue;
            }
            return -1;
        };

        // Under UPGRADE_FAIRNESS (Equal Work -> Equal Pay), all 11 solved seats receive equal nSeatBase
        // (with remainder nSeatRem on the final solved seat regardless of Coin-Lock / Funded PoW-Lock / Bootstrap track)!
        BOOST_CHECK_EQUAL(getPayeeAmount(vClSolved[0]), nSeatBase + nSeatRem);
        for (const CPubKey& pk : vFpSolved) {
            BOOST_CHECK_EQUAL(getPayeeAmount(pk), nSeatBase);
        }
        for (const CPubKey& pk : vBootSolved) {
            BOOST_CHECK_EQUAL(getPayeeAmount(pk), nSeatBase);
        }

        // Also verify standby / liveness-fallback seats earn the exact same equal per-seat share nSeatBase:
        std::vector<CPubKey> vFallbackSolved;
        for (size_t i = 0; i < 5; ++i) vFallbackSolved.push_back(vBootSolved[i]);
        for (const CPubKey& pk : vFpSolved) vFallbackSolved.push_back(pk);
        for (const CPubKey& pk : clSet) {
            if (pk != coord) vFallbackSolved.push_back(pk); // 1 primary Coin-Lock + 2 standby Coin-Locks = 3 Coin-Locks
        }
        BOOST_REQUIRE_EQUAL(vFallbackSolved.size(), 11U);
        CMutableTransaction cbTxFallback;
        cbTxFallback.vin.resize(1);
        cbTxFallback.vin[0].prevout.SetNull();
        cbTxFallback.vout.resize(1);
        cbTxFallback.vout[0].scriptPubKey = GetScriptForDestination(coord.GetID());
        FillBlockPayee(cbTxFallback, pPrev, false, &vFallbackSolved);

        auto getFallbackAmount = [&](const CPubKey& pk) -> CAmount {
            const CScript scr = GetScriptForDestination(pk.GetID());
            for (size_t idx = 1; idx < cbTxFallback.vout.size(); ++idx) {
                if (cbTxFallback.vout[idx].scriptPubKey == scr) return cbTxFallback.vout[idx].nValue;
            }
            return -1;
        };
        for (size_t idx = 0; idx < vFallbackSolved.size(); ++idx) {
            const CAmount expected = nSeatBase + ((idx + 1 == vFallbackSolved.size()) ? nSeatRem : 0);
            BOOST_CHECK_EQUAL(getFallbackAmount(vFallbackSolved[idx]), expected);
        }

        // Verify LLMQ < 5 Active Masternodes fallback guarantees existing real Active Masternodes
        // and caps Coin-Lock fallback candidates to 1 seat!
        std::vector<CPubKey> vTempMns;
        for (int m = 0; m < 2; ++m) {
            CKey mnK;
            mnK.MakeNewKey(true);
            const CPubKey mnPk = mnK.GetPubKey();
            vTempMns.push_back(mnPk);
            COutPoint mnOut(uint256S("8300000000000000000000000000000000000000000000000000000000000000"), (uint32_t)m);
            pcoinsTip->AddCoin(mnOut, Coin(CTxOut(4200 * COIN, GetScriptForDestination(mnPk.GetID())), pPrev->nHeight, false, false), true);
            vAddedCoins.push_back(mnOut);
            BOOST_REQUIRE(RegisterOnChainMasternodePing(mnPk, mnOut, pPrev->nHeight, nullptr, pcoinsTip));
        }
        std::vector<llmq::CQuorumMember> qMembers = llmq::ElectQuorumMembers(nEvalHeight, 0, pPrev, pcoinsTip);
        BOOST_REQUIRE_EQUAL(qMembers.size(), 5U);
        int nMnInQ = 0, nClInQ = 0, nFpInQ = 0;
        for (const auto& qm : qMembers) {
            if (qm.pubKeyMasternode == vTempMns[0] || qm.pubKeyMasternode == vTempMns[1]) nMnInQ++;
            else if (clSet.count(qm.pubKeyMasternode)) nClInQ++;
            else if (fpSet.count(qm.pubKeyMasternode)) nFpInQ++;
        }
        BOOST_CHECK_EQUAL(nMnInQ, 2);   // Both real Active Masternodes guaranteed!
        BOOST_CHECK_LE(nClInQ, 1);      // At most 1 Coin-Lock out of 4 can enter the 5-member quorum!
        BOOST_CHECK_EQUAL(nMnInQ + nClInQ + nFpInQ, 5);

        for (const CPubKey& mnPk : vTempMns) {
            mapMasternodeLastActiveHeight.erase(mnPk);
            mapMasternodeActiveHeights.erase(mnPk);
            mapMasternodeCollateralOutpoints.erase(mnPk);
        }
        BumpMasternodePingStateGen();

        // Cleanup temporary blocks and coins
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

BOOST_AUTO_TEST_CASE(pol_self_renewal_coinstake_test)
{
    CKey polKey;
    polKey.MakeNewKey(true);
    CPubKey polPubKey = polKey.GetPubKey();
    CBLSSecretKey blsSk = DeriveBLSFromCKey(polKey);
    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddKeyPubKey(polKey, polPubKey));

    const int nSpendHeight = 500;
    const int64_t nLockHeight = 60000;
    const CAmount nPrincipal = 100 * COIN;
    CScript polScript = CScript() << CScriptNum(nLockHeight) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                  << OP_DUP << OP_HASH160 << ToByteVector(polPubKey.GetID())
                                  << OP_EQUALVERIFY << OP_CHECKSIG;

    txnouttype whichType = TX_NONSTANDARD;
    std::vector<std::vector<unsigned char>> vSolutions;
    BOOST_REQUIRE(Solver(polScript, whichType, vSolutions));
    BOOST_CHECK_EQUAL(whichType, TX_CLTV_TIMELOCK);

    int64_t parsedLock = 0;
    CKeyID parsedKeyId;
    BOOST_REQUIRE(MatchPoLTimelockScript(polScript, parsedLock, parsedKeyId));
    BOOST_CHECK_EQUAL(parsedLock, nLockHeight);
    BOOST_CHECK(parsedKeyId == polPubKey.GetID());

    CMutableTransaction mtxFunding;
    mtxFunding.vin.push_back(CTxIn(GetRandHash(), 0));
    mtxFunding.vout.push_back(CTxOut(nPrincipal, polScript));
    CTransaction txFunding(mtxFunding);

    // 1. Early regular spend (!IsCoinStake) is NOT self-renewal and MUST fail CLTV
    CMutableTransaction mtxEarlyRegular;
    mtxEarlyRegular.nLockTime = nSpendHeight;
    mtxEarlyRegular.vin.push_back(CTxIn(txFunding.GetHash(), 0, CScript(), 0));
    mtxEarlyRegular.vout.push_back(CTxOut(nPrincipal - 1 * COIN, GetScriptForDestination(polPubKey.GetID())));
    BOOST_CHECK(!IsValidPoLSelfRenewal(CTransaction(mtxEarlyRegular), 0, txFunding.vout[0], nSpendHeight));
    BOOST_CHECK(!SignSignature(keystore, txFunding, mtxEarlyRegular, 0, SIGHASH_ALL));

    // 2. Valid lock-preserving self-renewing coinstake passes IsValidPoLSelfRenewal, signing, and block signature checks
    CMutableTransaction mtxCoinStake;
    mtxCoinStake.nLockTime = nSpendHeight;
    mtxCoinStake.vin.push_back(CTxIn(txFunding.GetHash(), 0, CScript(), 0));
    mtxCoinStake.vout.push_back(CTxOut(0, CScript())); // coinstake marker
    mtxCoinStake.vout.push_back(CTxOut(nPrincipal + 2 * COIN, polScript));
    BOOST_REQUIRE(CTransaction(mtxCoinStake).IsCoinStake());
    BOOST_CHECK(IsValidPoLSelfRenewal(CTransaction(mtxCoinStake), 0, txFunding.vout[0], nSpendHeight));
    BOOST_REQUIRE(SignSignature(keystore, txFunding, mtxCoinStake, 0, SIGHASH_ALL));

    const CTransaction txCs(mtxCoinStake);
    PrecomputedTransactionData precom(txCs);
    precom.Init(txCs, std::vector<CTxOut>{txFunding.vout[0]});
    ScriptError serror = SCRIPT_ERR_OK;
    // Passes without CLTV/DISCOURAGE_UPGRADABLE_NOPS (self-renewal exemption), fails with CLTV (lock not expired yet)
    BOOST_CHECK(VerifyScript(txCs.vin[0].scriptSig, polScript,
        STANDARD_SCRIPT_VERIFY_FLAGS & ~(SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_DISCOURAGE_UPGRADABLE_NOPS),
        TransactionSignatureChecker(&txCs, 0, nPrincipal, precom), &serror));
    BOOST_CHECK(!VerifyScript(txCs.vin[0].scriptSig, polScript,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&txCs, 0, nPrincipal, precom), &serror));

    CKeyID utxoKeyId;
    BOOST_CHECK(txCs.vout[1].GetKeyIDFromUTXO(utxoKeyId));
    BOOST_CHECK(utxoKeyId == polPubKey.GetID());

    CMutableTransaction mtxEmptyCb;
    mtxEmptyCb.vin.push_back(CTxIn(COutPoint(), CScript() << nSpendHeight << OP_0));
    mtxEmptyCb.vout.push_back(CTxOut(0, CScript()));
    CBlock posBlock;
    posBlock.nVersion = 12;
    posBlock.hashPrevBlock = GetRandHash();
    posBlock.nTime = 1780000000;
    posBlock.vtx.push_back(CTransaction(mtxEmptyCb));
    posBlock.vtx.push_back(txCs);
    posBlock.hashMerkleRoot = BlockMerkleRoot(posBlock);
    BOOST_REQUIRE(SignBlockWithKey(posBlock, polKey, blsSk));
    BOOST_CHECK(CheckBlockSignature(posBlock, true));

    const uint256 reqHash = ComputePoSCoordRequestHash(posBlock, polPubKey);
    std::vector<unsigned char> reqSig;
    BOOST_REQUIRE(SignPoSCoordRequest(posBlock, reqHash, keystore, reqSig));
    BOOST_CHECK(VerifyPoSCoordRequestAuth(posBlock, reqHash, reqSig, true));

    // 3. Reject principal reduction, script escape, or extra inputs
    CMutableTransaction mtxDrain = mtxCoinStake;
    mtxDrain.vout[1].nValue = nPrincipal - 1;
    BOOST_CHECK(!IsValidPoLSelfRenewal(CTransaction(mtxDrain), 0, txFunding.vout[0], nSpendHeight));

    CMutableTransaction mtxEscape = mtxCoinStake;
    mtxEscape.vout[1].scriptPubKey = GetScriptForDestination(polPubKey.GetID());
    BOOST_CHECK(!IsValidPoLSelfRenewal(CTransaction(mtxEscape), 0, txFunding.vout[0], nSpendHeight));

    CMutableTransaction mtxMultiIn = mtxCoinStake;
    mtxMultiIn.vin.push_back(CTxIn(GetRandHash(), 1));
    BOOST_CHECK(!IsValidPoLSelfRenewal(CTransaction(mtxMultiIn), 0, txFunding.vout[0], nSpendHeight));
}

BOOST_AUTO_TEST_CASE(llmq_signer_only_payout_and_pose_test)
{
    ScopedMpaChainParams testParams(CBaseChainParams::TESTNET);
    llmq::ClearQuorumCache();
    std::unique_ptr<dip3::CDeterministicMNManager> origMgr = std::move(deterministicMNManager);
    deterministicMNManager.reset(new dip3::CDeterministicMNManager(1 << 20, true, true));

    const bool origPingInit = g_fMasternodePingStateInitialized;
    g_fMasternodePingStateInitialized = true;

    CBlockIndex* pOrigTip = chainActive.Tip();
    BOOST_REQUIRE(pOrigTip != nullptr);
    CBlockIndex* pPrev = pOrigTip;
    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vAddedCoins;
    std::vector<CPubKey> vRegMns;

    auto appendSimpleBlock = [&](int targetH) -> CBlockIndex* {
        CBlock blk;
        blk.nVersion = 12;
        blk.hashPrevBlock = pPrev->GetBlockHash();
        blk.nTime = pPrev->nTime + 30;
        blk.nNonce = (uint32_t)targetH;
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << targetH << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk.vtx.push_back(cb);
        blk.hashMerkleRoot = BlockMerkleRoot(blk);
        vHashes.push_back(blk.GetHash());
        CBlockIndex* pIdx = new CBlockIndex(blk);
        pIdx->nHeight = targetH;
        pIdx->pprev = pPrev;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
        pIdx->phashBlock = &(ins.first->first);
        pIdx->BuildSkip();
        vIndices.push_back(pIdx);
        chainActive.SetTip(pIdx);
        pPrev = pIdx;
        return pIdx;
    };

    // Seed empty DIP3 lists across heights 1..494 so evodb has every DIP3 parent block before epoch 500's C-2 snapshot (498)
    while (pPrev->nHeight < 494) {
        CBlockIndex* pIdx = appendSimpleBlock(pPrev->nHeight + 1);
        CBlock bEmpty;
        bEmpty.nVersion = 12;
        bEmpty.hashPrevBlock = pIdx->pprev->GetBlockHash();
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(bEmpty, pIdx, st, *pcoinsTip, false));
    }

    // Build 5 DIP3 MNs and register via PROTX_REG in block 495 (strictly before epoch 500 C-2 snapshot height 498)
    std::vector<CKey> mnKeys(5);
    std::vector<CBLSSecretKey> opKeys(5);
    std::vector<COutPoint> colOuts(5);
    std::map<COutPoint, CKey> mapColToKey;
    std::map<COutPoint, uint256> mapColToProTx;
    CBlock blk501;
    blk501.nVersion = 12;
    blk501.hashPrevBlock = pPrev->GetBlockHash();
    blk501.nTime = pPrev->nTime + 30;
    {
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << 495 << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk501.vtx.push_back(cb);
    }
    const CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(495);
    for (int i = 0; i < 5; ++i) {
        mnKeys[i].MakeNewKey(true);
        opKeys[i].MakeNewKey();
        CPubKey pk = mnKeys[i].GetPubKey();
        vRegMns.push_back(pk);
        colOuts[i] = COutPoint(uint256S("9100000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i);
        COutPoint feeOut(uint256S("9200000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i);
        CScript payoutScript = GetScriptForDestination(pk.GetID());
        pcoinsTip->AddCoin(colOuts[i], Coin(CTxOut(colAmt, payoutScript), 10, false, false), true);
        pcoinsTip->AddCoin(feeOut, Coin(CTxOut(1 * COIN, payoutScript), 10, false, false), true);
        vAddedCoins.push_back(colOuts[i]);
        vAddedCoins.push_back(feeOut);
        mapColToKey[colOuts[i]] = mnKeys[i];

        CMutableTransaction mtx;
        mtx.vin.push_back(CTxIn(feeOut));
        dip3::CProRegTx ptx;
        ptx.nVersion = 2;
        ptx.collateralOutpoint = colOuts[i];
        ptx.addr = LookupNumeric("192.168.50.10", 27900 + i);
        ptx.pubKeyOwner = pk;
        ptx.blsPubKeyOperator = CBLSPubKey(opKeys[i]);
        BOOST_REQUIRE(ptx.blsPoP.SignPoP(opKeys[i]));
        ptx.keyIDVoting = pk.GetID();
        ptx.nOperatorReward = 0;
        ptx.scriptPayout = payoutScript;
        ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
        BOOST_REQUIRE(mnKeys[i].Sign(ptx.GetSignHash(), ptx.vchSig));
        mtx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));
        CTransaction proTx(mtx);
        blk501.vtx.push_back(proTx);
        mapColToProTx[colOuts[i]] = proTx.GetHash();
        BOOST_REQUIRE(RegisterOnChainMasternodePing(pk, colOuts[i], 501, nullptr, pcoinsTip));
    }
    blk501.hashMerkleRoot = BlockMerkleRoot(blk501);
    vHashes.push_back(blk501.GetHash());
    CBlockIndex* pIdx501 = new CBlockIndex(blk501);
    pIdx501->nHeight = 495;
    pIdx501->pprev = pPrev;
    {
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx501));
        pIdx501->phashBlock = &(ins.first->first);
        pIdx501->BuildSkip();
        vIndices.push_back(pIdx501);
        chainActive.SetTip(pIdx501);
        pPrev = pIdx501;
    }
    {
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk501, pIdx501, st, *pcoinsTip, false));
    }

    while (pPrev->nHeight < 508) {
        CBlockIndex* pIdx = appendSimpleBlock(pPrev->nHeight + 1);
        CBlock bEmpty;
        bEmpty.nVersion = 12;
        bEmpty.hashPrevBlock = pIdx->pprev->GetBlockHash();
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(bEmpty, pIdx, st, *pcoinsTip, false));
    }
    for (int i = 0; i < 5; ++i) {
        BOOST_REQUIRE(RegisterOnChainMasternodePing(mnKeys[i].GetPubKey(), colOuts[i], 508, nullptr, pcoinsTip));
    }
    InitializeAdamMinerRegCache(true);

    // Build block 509 with ONLY first 3 of 5 LLMQ members signing
    llmq::CQuorum q509 = llmq::GetActiveQuorum(509, pPrev, pcoinsTip);
    BOOST_REQUIRE_EQUAL(q509.members.size(), 5U);

    CBlock blk509;
    blk509.nVersion = 12;
    blk509.hashPrevBlock = pPrev->GetBlockHash();
    blk509.nTime = pPrev->nTime + 30;
    blk509.nBits = 0x207fffff;
    {
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << 509 << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        blk509.vtx.push_back(cb);
    }
    blk509.hashMerkleRoot = BlockMerkleRoot(blk509);

    auto makeQsigBytes = [&](const uint256& blockHash, const llmq::CQuorum& q, const std::vector<size_t>& signerIndices) -> std::vector<unsigned char> {
        llmq::CQuorumSignature qsig;
        qsig.blockHash = blockHash;
        const std::string& dst = GetBLSDomainTag(BLSDomain::LLMQ_BLOCK);
        for (size_t idx : signerIndices) {
            const COutPoint& col = q.members[idx].collateralOutpoint;
            CBLSSecretKey opSk;
            for (int m = 0; m < 5; ++m) {
                if (colOuts[m] == col) {
                    opSk = opKeys[m];
                    break;
                }
            }
            BOOST_REQUIRE(opSk.IsValid());
            std::vector<unsigned char> vchSig;
            BOOST_REQUIRE(SignBLSOperatorOnly(blockHash, opSk, vchSig, dst));
            qsig.signatures.emplace_back(col, vchSig);
        }
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << qsig;
        return std::vector<unsigned char>(ss.begin(), ss.end());
    };

    blk509.vQuorumSig = makeQsigBytes(blk509.GetHash(), q509, {0, 1, 2});
    CDiskBlockPos pos509(1, 0);
    BOOST_REQUIRE(WriteBlockToDisk(blk509, pos509));
    vHashes.push_back(blk509.GetHash());
    CBlockIndex* pIdx509 = new CBlockIndex(blk509);
    pIdx509->nHeight = 509;
    pIdx509->pprev = pPrev;
    pIdx509->nFile = pos509.nFile;
    pIdx509->nDataPos = pos509.nPos;
    pIdx509->nStatus |= BLOCK_HAVE_DATA;
    {
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx509));
        pIdx509->phashBlock = &(ins.first->first);
        pIdx509->BuildSkip();
        vIndices.push_back(pIdx509);
        chainActive.SetTip(pIdx509);
    }

    // M-4 under UPGRADE_HARDENING_V4: LLMQ omission no longer triggers PoSe penalty (only standby substitution in primary slots does)
    {
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk509, pIdx509, st, *pcoinsTip, false));
        const dip3::CDeterministicMNList list509 = deterministicMNManager->GetListForBlock(pIdx509);
        for (size_t idx = 0; idx < 3; ++idx) {
            const uint256& ptxHash = mapColToProTx.at(q509.members[idx].collateralOutpoint);
            BOOST_CHECK_EQUAL(list509.GetMN(ptxHash)->state.nPoSePenalty, 0);
        }
        for (size_t idx = 3; idx < 5; ++idx) {
            const uint256& ptxHash = mapColToProTx.at(q509.members[idx].collateralOutpoint);
            BOOST_CHECK_EQUAL(list509.GetMN(ptxHash)->state.nPoSePenalty, 0);
        }
    }

    // At height 510: LLMQ rewards ONLY the 3 signers of block 509; missed members receive 0 and forfeited 2/5 goes 50% burn + 50% treasury
    uint256 seed510 = GetAdamSeed(pIdx509);
    std::vector<CPubKey> vElected510;
    CPubKey coord510;
    BOOST_REQUIRE(SelectAdamNodes(seed510, Params().GetConsensus(), vElected510, coord510, 510, pcoinsTip));
    std::vector<CPubKey> vNoSolved;

    CBlock blk510;
    blk510.nVersion = 12;
    blk510.hashPrevBlock = pIdx509->GetBlockHash();
    blk510.nTime = pIdx509->nTime + 30;
    blk510.nBits = 0x207fffff;
    blk510.vAdamMiners = vElected510;
    blk510.vAdamSolutions.resize(vElected510.size());
    CMutableTransaction cb510;
    cb510.vin.push_back(CTxIn(COutPoint(), CScript() << 510 << OP_0));
    cb510.vout.push_back(CTxOut(0, GetScriptForDestination(coord510.GetID())));
    FillBlockPayee(cb510, pIdx509, false, &vNoSolved, &coord510);
    blk510.vtx.push_back(cb510);
    blk510.hashMerkleRoot = BlockMerkleRoot(blk510);
    BOOST_CHECK(IsBlockPayeeValid(blk510, 510, pcoinsTip));

    const CAmount nVal510 = CMasternode::GetBlockValue(510);
    const CAmount nNet510 = nVal510 - (nVal510 * 7 / 100);
    const CAmount nLlmqPool = nNet510 * 10 / 100;
    const CAmount nPerLlmqMember = nLlmqPool / 5;
    auto countLlmqOutput = [&](const CPubKey& pk) -> CAmount {
        const CScript scr = GetScriptForDestination(pk.GetID());
        CAmount sum = 0;
        for (size_t oi = 1; oi < blk510.vtx[0].vout.size(); ++oi) {
            if (blk510.vtx[0].vout[oi].scriptPubKey == scr && blk510.vtx[0].vout[oi].nValue == nPerLlmqMember) {
                sum += blk510.vtx[0].vout[oi].nValue;
            }
        }
        return sum;
    };
    for (size_t idx = 0; idx < 3; ++idx) {
        BOOST_CHECK_EQUAL(countLlmqOutput(q509.members[idx].pubKeyMasternode), nPerLlmqMember);
    }
    for (size_t idx = 3; idx < 5; ++idx) {
        BOOST_CHECK_EQUAL(countLlmqOutput(q509.members[idx].pubKeyMasternode), 0);
    }

    // Tamper blk510 to pay missed member 3 out of treasury -> IsBlockPayeeValid MUST reject
    {
        CBlock bad510 = blk510;
        CMutableTransaction badCb(bad510.vtx[0]);
        CScript treasuryScr = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        CScript missedScr = GetScriptForDestination(q509.members[3].pubKeyMasternode.GetID());
        bool deducted = false;
        for (auto& o : badCb.vout) {
            if (o.scriptPubKey == treasuryScr && o.nValue >= nPerLlmqMember) {
                o.nValue -= nPerLlmqMember;
                deducted = true;
                break;
            }
        }
        BOOST_REQUIRE(deducted);
        badCb.vout.push_back(CTxOut(nPerLlmqMember, missedScr));
        bad510.vtx[0] = badCb;
        BOOST_CHECK(!IsBlockPayeeValid(bad510, 510, pcoinsTip));
    }

    // LLMQ PoSe recovery check at block 510: member 3 signs (5 -> 0), member 4 misses again (5 -> 10)
    llmq::CQuorum q510 = llmq::GetActiveQuorum(510, pIdx509, pcoinsTip);
    BOOST_REQUIRE_EQUAL(q510.members.size(), 5U);
    size_t m3Idx = 999, m4Idx = 999;
    std::vector<size_t> sigs510;
    for (size_t i = 0; i < q510.members.size(); ++i) {
        if (q510.members[i].collateralOutpoint == q509.members[3].collateralOutpoint) m3Idx = i;
        if (q510.members[i].collateralOutpoint == q509.members[4].collateralOutpoint) m4Idx = i;
        else sigs510.push_back(i); // everyone except member 4 of q509 signs block 510
    }
    BOOST_REQUIRE_LT(m3Idx, q510.members.size());
    BOOST_REQUIRE_LT(m4Idx, q510.members.size());
    blk510.vAdamMiners.clear();
    blk510.vAdamSolutions.clear();
    blk510.vQuorumSig = makeQsigBytes(blk510.GetHash(), q510, sigs510);
    vHashes.push_back(blk510.GetHash());
    CBlockIndex* pIdx510 = new CBlockIndex(blk510);
    pIdx510->nHeight = 510;
    pIdx510->pprev = pIdx509;
    {
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx510));
        pIdx510->phashBlock = &(ins.first->first);
        pIdx510->BuildSkip();
        vIndices.push_back(pIdx510);
        chainActive.SetTip(pIdx510);
        CValidationState st;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk510, pIdx510, st, *pcoinsTip, false));
        const dip3::CDeterministicMNList list510 = deterministicMNManager->GetListForBlock(pIdx510);
        BOOST_CHECK_EQUAL(list510.GetMN(mapColToProTx.at(q509.members[3].collateralOutpoint))->state.nPoSePenalty, 0);
        BOOST_CHECK_EQUAL(list510.GetMN(mapColToProTx.at(q509.members[4].collateralOutpoint))->state.nPoSePenalty, 0);
    }

    // Cleanup
    for (const CPubKey& pk : vRegMns) {
        mapMasternodeLastActiveHeight.erase(pk);
        mapMasternodeActiveHeights.erase(pk);
        mapMasternodeCollateralOutpoints.erase(pk);
    }
    BumpMasternodePingStateGen();
    for (const COutPoint& outp : vAddedCoins) {
        pcoinsTip->SpendCoin(outp);
    }
    chainActive.SetTip(pOrigTip);
    for (size_t i = 0; i < vIndices.size(); ++i) {
        mapBlockIndex.erase(vHashes[i]);
        delete vIndices[i];
    }
    llmq::ClearQuorumCache();
    g_fMasternodePingStateInitialized = origPingInit;
    deterministicMNManager = std::move(origMgr);
    InitializeAdamMinerRegCache(true);
}

BOOST_AUTO_TEST_CASE(pos_coordinator_share_and_fees_test)
{
    ScopedMpaChainParams regParams(CBaseChainParams::REGTEST);
    const int origRegModelD = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight;
    struct RegModelDGuard {
        int prevHeight;
        explicit RegModelDGuard(int h) : prevHeight(h) {}
        ~RegModelDGuard() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_MODELD, prevHeight);
        }
    } regModelDGuard(origRegModelD);
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_MODELD, 500);

    std::unique_ptr<dip3::CDeterministicMNManager> origMgr = std::move(deterministicMNManager);
    deterministicMNManager.reset(new dip3::CDeterministicMNManager(1 << 20, true, true));

    const bool origPingInit = g_fMasternodePingStateInitialized;
    g_fMasternodePingStateInitialized = true;

    CBlockIndex* pOrigTip = chainActive.Tip();
    BOOST_REQUIRE(pOrigTip != nullptr);
    CBlockIndex* pPrev = pOrigTip;
    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    std::vector<COutPoint> vAddedCoins;
    std::vector<CPubKey> vRegMns;

    auto advanceTo = [&](int targetH) {
        while (pPrev->nHeight < targetH) {
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
            blk.hashMerkleRoot = BlockMerkleRoot(blk);
            vHashes.push_back(blk.GetHash());
            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = pPrev;
            auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            vIndices.push_back(pIdx);
            chainActive.SetTip(pIdx);
            CValidationState stStep;
            BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk, pIdx, stStep, *pcoinsTip, false));
            pPrev = pIdx;
        }
    };

    advanceTo(249);
    {
        CBlock blk250;
        blk250.nVersion = 12;
        blk250.hashPrevBlock = pPrev->GetBlockHash();
        blk250.nTime = pPrev->nTime + 30;
        {
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << 250 << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk250.vtx.push_back(cb);
        }
        for (int i = 0; i < 15; ++i) {
            CKey k = GetAdamDeterministicKey(i);
            CPubKey pk = k.GetPubKey();
            vRegMns.push_back(pk);
            COutPoint colOut(uint256S("a100000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i);
            COutPoint feeOut(uint256S("a200000000000000000000000000000000000000000000000000000000000000"), (uint32_t)i);
            CScript payoutScript = GetScriptForDestination(pk.GetID());
            pcoinsTip->AddCoin(colOut, Coin(CTxOut(4200 * COIN, payoutScript), 10, false, false), true);
            pcoinsTip->AddCoin(feeOut, Coin(CTxOut(1 * COIN, payoutScript), 10, false, false), true);
            vAddedCoins.push_back(colOut);
            vAddedCoins.push_back(feeOut);

            CBLSSecretKey opSk;
            opSk.MakeNewKey();
            CMutableTransaction mtx;
            mtx.vin.push_back(CTxIn(feeOut));
            dip3::CProRegTx ptx;
            ptx.nVersion = 2;
            ptx.collateralOutpoint = colOut;
            ptx.addr = LookupNumeric("192.168.50.20", 27900 + i);
            ptx.pubKeyOwner = pk;
            ptx.blsPubKeyOperator = CBLSPubKey(opSk);
            BOOST_REQUIRE(ptx.blsPoP.SignPoP(opSk));
            ptx.keyIDVoting = pk.GetID();
            ptx.nOperatorReward = 0;
            ptx.scriptPayout = payoutScript;
            ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
            BOOST_REQUIRE(k.Sign(ptx.GetSignHash(), ptx.vchSig));
            mtx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));
            blk250.vtx.push_back(CTransaction(mtx));
        }
        blk250.hashMerkleRoot = BlockMerkleRoot(blk250);
        vHashes.push_back(blk250.GetHash());
        CBlockIndex* pIdx250 = new CBlockIndex(blk250);
        pIdx250->nHeight = 250;
        pIdx250->pprev = pPrev;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx250));
        pIdx250->phashBlock = &(ins.first->first);
        pIdx250->BuildSkip();
        vIndices.push_back(pIdx250);
        chainActive.SetTip(pIdx250);
        CValidationState st250;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk250, pIdx250, st250, *pcoinsTip, false));
        pPrev = pIdx250;
    }
    InitializeAdamMinerRegCache(true);

    CKey stakerKey;
    stakerKey.MakeNewKey(true);
    const CScript stakerScript = GetScriptForDestination(stakerKey.GetPubKey().GetID());
    const CAmount nStakeIn = 1000 * COIN;

    // 1. Pre-Model D PoS (height 251 on Regtest with temporary UPGRADE_MODELD=500): 20% Staker / 5% Coordinator / 75% Validators
    {
        const int h = 251;
        CBlockIndex* pIdx250 = pPrev;
        uint256 seed = GetAdamSeed(pIdx250);
        std::vector<CPubKey> vElected;
        CPubKey coord;
        BOOST_REQUIRE(SelectAdamNodes(seed, Params().GetConsensus(), vElected, coord, h, pcoinsTip));
        BOOST_REQUIRE(coord.IsValid());

        const CAmount nValActual = CMasternode::GetBlockValue(h);
        const CAmount nTreasury = nValActual * 7 / 100;
        const CAmount nNet = nValActual - nTreasury;
        const CAmount nExpectedCoordShare = nNet * 5 / 100;
        const CAmount nExpectedStakerReward = nNet - (nNet * 75 / 100) - nExpectedCoordShare; // 20% of Net

        CMutableTransaction mtxCs;
        mtxCs.vin.push_back(CTxIn(GetRandHash(), 0));
        mtxCs.vout.push_back(CTxOut(0, CScript()));
        mtxCs.vout.push_back(CTxOut(nStakeIn + nValActual, stakerScript));
        std::vector<CPubKey> vNoSolved;
        FillBlockPayee(mtxCs, pIdx250, true, &vNoSolved, &coord);

        BOOST_CHECK_EQUAL(mtxCs.vout[1].nValue, nStakeIn + nExpectedStakerReward);
        const CScript coordScript = GetScriptForDestination(coord.GetID());
        bool foundCoord = false;
        for (size_t oi = 2; oi < mtxCs.vout.size(); ++oi) {
            if (mtxCs.vout[oi].scriptPubKey == coordScript && mtxCs.vout[oi].nValue == nExpectedCoordShare) {
                foundCoord = true;
                break;
            }
        }
        BOOST_CHECK(foundCoord);

        CBlock blkPre;
        blkPre.nVersion = 12;
        blkPre.hashPrevBlock = pIdx250->GetBlockHash();
        blkPre.nTime = pIdx250->nTime + 30;
        blkPre.nBits = 0x207fffff;
        blkPre.vAdamMiners = vElected;
        blkPre.vAdamSolutions.resize(vElected.size());
        CMutableTransaction emptyCb;
        emptyCb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        emptyCb.vout.push_back(CTxOut(0, CScript()));
        blkPre.vtx.push_back(emptyCb);
        blkPre.vtx.push_back(mtxCs);
        blkPre.hashMerkleRoot = BlockMerkleRoot(blkPre);
        BOOST_CHECK(IsBlockPayeeValid(blkPre, h, pcoinsTip));

        // Stripping coordinator output MUST fail validation
        CMutableTransaction badCs = mtxCs;
        for (auto it = badCs.vout.begin() + 2; it != badCs.vout.end(); ++it) {
            if (it->scriptPubKey == coordScript && it->nValue == nExpectedCoordShare) {
                badCs.vout[1].nValue += nExpectedCoordShare;
                badCs.vout.erase(it);
                break;
            }
        }
        blkPre.vtx[1] = badCs;
        BOOST_CHECK(!IsBlockPayeeValid(blkPre, h, pcoinsTip));
    }

    // 2. Balanced Model D PoS (height 505 on Regtest): 22% Staker (+ symmetric nFees) / 3% Coordinator / 5% Passive MN / 10% LLMQ / 60% Validators (15 seats)
    advanceTo(504);
    InitializeAdamMinerRegCache(true);
    {
        const int h = 505;
        CBlockIndex* pIdx504 = pPrev;
        uint256 seed = GetAdamSeed(pIdx504);
        std::vector<CPubKey> vElected;
        CPubKey coord;
        BOOST_REQUIRE(SelectAdamNodes(seed, Params().GetConsensus(), vElected, coord, h, pcoinsTip));
        BOOST_REQUIRE_EQUAL(vElected.size(), 15U);
        BOOST_REQUIRE(coord.IsValid());

        const CAmount nValActual = CMasternode::GetBlockValue(h);
        const CAmount nTreasury = nValActual * 7 / 100;
        const CAmount nNet = nValActual - nTreasury;
        const CAmount nExpectedCoordShare = nNet * 3 / 100;
        const CAmount nExpectedStakerReward = nNet - (nNet * 5 / 100) - (nNet * 10 / 100) - (nNet * 60 / 100) - nExpectedCoordShare; // 22% of Net
        const CAmount nSimFees = 50000000; // 0.5 KRISTA tx fees

        CMutableTransaction mtxCs;
        mtxCs.vin.push_back(CTxIn(GetRandHash(), 0));
        mtxCs.vout.push_back(CTxOut(0, CScript()));
        mtxCs.vout.push_back(CTxOut(nStakeIn + nValActual, stakerScript));
        std::vector<CPubKey> vNoSolved;
        FillBlockPayee(mtxCs, pIdx504, true, &vNoSolved, &coord);
        BOOST_CHECK_EQUAL(mtxCs.vout[1].nValue, nStakeIn + nExpectedStakerReward);

        // Add transaction fees to staker output symmetrically (CreateNewBlock PoS fee step)
        mtxCs.vout[1].nValue += nSimFees;
        BOOST_CHECK(IsBlockValueValid(h, nValActual + nSimFees, nValActual + nSimFees));

        CBlock blkModelD;
        blkModelD.nVersion = 12;
        blkModelD.hashPrevBlock = pIdx504->GetBlockHash();
        blkModelD.nTime = pIdx504->nTime + 30;
        blkModelD.nBits = 0x207fffff;
        blkModelD.vAdamMiners = vElected;
        blkModelD.vAdamSolutions.resize(vElected.size());
        CMutableTransaction emptyCb;
        emptyCb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
        emptyCb.vout.push_back(CTxOut(0, CScript()));
        blkModelD.vtx.push_back(emptyCb);
        blkModelD.vtx.push_back(mtxCs);
        blkModelD.hashMerkleRoot = BlockMerkleRoot(blkModelD);
        BOOST_CHECK(IsBlockPayeeValid(blkModelD, h, pcoinsTip));
    }

    // Cleanup
    for (const CPubKey& pk : vRegMns) {
        mapMasternodeLastActiveHeight.erase(pk);
        mapMasternodeActiveHeights.erase(pk);
        mapMasternodeCollateralOutpoints.erase(pk);
    }
    BumpMasternodePingStateGen();
    for (const COutPoint& outp : vAddedCoins) {
        pcoinsTip->SpendCoin(outp);
    }
    chainActive.SetTip(pOrigTip);
    for (size_t i = 0; i < vIndices.size(); ++i) {
        mapBlockIndex.erase(vHashes[i]);
        delete vIndices[i];
    }
    llmq::ClearQuorumCache();
    g_fMasternodePingStateInitialized = origPingInit;
    deterministicMNManager = std::move(origMgr);
    InitializeAdamMinerRegCache(true);
}

BOOST_AUTO_TEST_CASE(llmq_equivocation_and_cache_clear_test)
{
    // C-3: Smoke check clearing previous LLMQ cache runs cleanly
    ClearPrevLlmqCache();
    llmq::ClearQuorumCache();

    // H-8b: AllowQuorumSignature prevents same-coordinator equivocation and enforces rotation slice
    CKey coordKeyA, coordKeyB;
    coordKeyA.MakeNewKey(true);
    coordKeyB.MakeNewKey(true);
    const CPubKey coordA = coordKeyA.GetPubKey();
    const CPubKey coordB = coordKeyB.GetPubKey();

    const uint256 prevHash1 = GetRandHash();
    const uint256 prevHash2 = GetRandHash();
    const uint256 blk1 = GetRandHash();
    const uint256 blk2 = GetRandHash();

    // First signature for (prevHash1, coordA, blk1) is allowed
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash1, coordA, blk1));
    // Re-broadcast of identical (prevHash1, coordA, blk1) is idempotently allowed
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash1, coordA, blk1));
    // Same coordinator proposing a different block on the same parent is unconditionally rejected
    BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash1, coordA, blk2));
    // Distinct coordinator proposing a different block immediately (< 30s rotation slice) is rejected
    BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash1, coordB, blk2));
    // Distinct coordinator re-signing the already chosen blk1 on same parent is allowed
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash1, coordB, blk1));
    // Different parent block allows fresh proposal
    BOOST_CHECK(llmq::AllowQuorumSignature(prevHash2, coordA, blk2));
}

BOOST_AUTO_TEST_CASE(mpa_pob_outpoint_and_burnbackup_phase2_test)
{
    CKey stakeKey, destKey;
    stakeKey.MakeNewKey(true);
    destKey.MakeNewKey(true);
    const CScript stakeSpk = GetScriptForDestination(stakeKey.GetPubKey().GetID());
    const CScript destSpk = GetScriptForDestination(destKey.GetPubKey().GetID());

    // 1. H-1 ResolveVoluntaryBurnTargetOutpoint: explicit "KTPOB" tag vs first-spendable fallback
    {
        CMutableTransaction mtxFallback;
        mtxFallback.vin.push_back(CTxIn(GetRandHash(), 0));
        mtxFallback.vout.push_back(CTxOut(0, CScript() << OP_RETURN << ParseHex("deadbeef")));
        mtxFallback.vout.push_back(CTxOut(50 * COIN, stakeSpk)); // index 1 = first spendable
        mtxFallback.vout.push_back(CTxOut(10 * COIN, destSpk));
        COutPoint resolvedFallback;
        BOOST_CHECK(ResolveVoluntaryBurnTargetOutpoint(CTransaction(mtxFallback), 5501, CScript(), nullptr, resolvedFallback));
        BOOST_CHECK(resolvedFallback == COutPoint(mtxFallback.GetHash(), 1));

        uint32_t targetIdx = 2;
        std::vector<unsigned char> ktpobData = {'K', 'T', 'P', 'O', 'B',
            static_cast<unsigned char>(targetIdx & 0xff),
            static_cast<unsigned char>((targetIdx >> 8) & 0xff),
            static_cast<unsigned char>((targetIdx >> 16) & 0xff),
            static_cast<unsigned char>((targetIdx >> 24) & 0xff)};
        CMutableTransaction mtxTagged = mtxFallback;
        mtxTagged.vout[0].scriptPubKey = CScript() << OP_RETURN << ktpobData;
        COutPoint resolvedTagged;
        BOOST_CHECK(ResolveVoluntaryBurnTargetOutpoint(CTransaction(mtxTagged), 5501, CScript(), nullptr, resolvedTagged));
        BOOST_CHECK(resolvedTagged == COutPoint(mtxTagged.GetHash(), 2));
    }

    // 2. H-1 Coinstake vout[1] burn transfer & DisconnectBlock undo
    CBlockIndex idxPrev;
    idxPrev.nHeight = 5500;
    const COutPoint out0(GetRandHash(), 0);
    const uint256 csHash = GetRandHash();
    const COutPoint out1(csHash, 1);
    const uint256 blkHash = GetRandHash();

    pcoinsTip->AddCoin(out0, Coin(CTxOut(100 * COIN, stakeSpk), 5400, false, false), true);
    pcoinsTip->AddCoin(out1, Coin(CTxOut(101 * COIN, stakeSpk), 5501, false, true), true);
    AddOutpointBurnToCache(out0, 20 * COIN, 5450);

    int wt0 = -1;
    const CAmount wBefore = CalculateMPAWeight(out0, 100 * COIN, GetTime(), &idxPrev, wt0);
    BOOST_CHECK_EQUAL(wt0, MPA_WEIGHT_POB);
    BOOST_CHECK_GT(wBefore, 100 * COIN);

    TransferOutpointBurnOnCoinstake(out0, out1, 5501, blkHash);
    int wtOld = -1, wtNew = -1;
    BOOST_CHECK_EQUAL(CalculateMPAWeight(out0, 100 * COIN, GetTime(), &idxPrev, wtOld), 100 * COIN);
    BOOST_CHECK_EQUAL(wtOld, MPA_WEIGHT_POS);
    BOOST_CHECK_GT(CalculateMPAWeight(out1, 101 * COIN, GetTime(), &idxPrev, wtNew), 101 * COIN);
    BOOST_CHECK_EQUAL(wtNew, MPA_WEIGHT_POB);

    CMutableTransaction mtxUndoCs;
    mtxUndoCs.vin.push_back(CTxIn(out0));
    mtxUndoCs.vout.push_back(CTxOut(0, CScript()));
    mtxUndoCs.vout.push_back(CTxOut(101 * COIN, stakeSpk));
    CBlock blkUndo;
    CMutableTransaction mtxCb;
    mtxCb.vin.push_back(CTxIn(COutPoint(), CScript() << 5501 << OP_0));
    mtxCb.vout.push_back(CTxOut(0, CScript()));
    blkUndo.vtx.push_back(CTransaction(mtxCb));
    blkUndo.vtx.push_back(CTransaction(mtxUndoCs));

    BOOST_CHECK(UndoOutpointBurnTransferForBlock(blkUndo, 5501, blkHash, stakeSpk, 100 * COIN));
    int wtAfterUndoOld = -1, wtAfterUndoNew = -1;
    BOOST_CHECK_GT(CalculateMPAWeight(out0, 100 * COIN, GetTime(), &idxPrev, wtAfterUndoOld), 100 * COIN);
    BOOST_CHECK_EQUAL(wtAfterUndoOld, MPA_WEIGHT_POB);
    BOOST_CHECK_EQUAL(CalculateMPAWeight(out1, 101 * COIN, GetTime(), &idxPrev, wtAfterUndoNew), 101 * COIN);
    BOOST_CHECK_EQUAL(wtAfterUndoNew, MPA_WEIGHT_POS);

#ifdef ENABLE_WALLET
    // 3. H-1 Anti-split in CPivStake::CreateTxOuts when staking outpoint carries an active PoB lineage
    {
        CWallet dummyWallet;
        BOOST_REQUIRE(dummyWallet.AddKeyPubKey(stakeKey, stakeKey.GetPubKey()));
        CMutableTransaction mtxFrom;
        mtxFrom.vout.push_back(CTxOut(200 * COIN, stakeSpk));
        CPivStake stake;
        BOOST_REQUIRE(stake.SetPrevout(CTransaction(mtxFrom), 0, &idxPrev));
        const COutPoint pobStakeOut = stake.GetOutPoint();
        AddOutpointBurnToCache(pobStakeOut, 20 * COIN, 5450);

        std::vector<CTxOut> vSplitOut;
        BOOST_CHECK(stake.CreateTxOuts(&dummyWallet, vSplitOut, 205 * COIN, false));
        BOOST_CHECK_EQUAL(vSplitOut.size(), 1U); // Anti-split keeps single output instead of splitting at > stakeSplitThreshold
        RemoveOutpointBurnFromCache(pobStakeOut, 20 * COIN, 5450);
    }
#endif

    // 4. M-8 CBurnCacheScopedBackup restores outpoint burn map on scope exit
    const COutPoint extraOut(GetRandHash(), 0);
    {
        CBurnCacheScopedBackup backup;
        AddOutpointBurnToCache(extraOut, 33 * COIN, 5501);
        BOOST_CHECK_GT(GetOutpointBurnWeight(extraOut, 5502), 0);
    }
    BOOST_CHECK_EQUAL(GetOutpointBurnWeight(extraOut, 5502), 0);
    int wtExtra = -1;
    pcoinsTip->AddCoin(extraOut, Coin(CTxOut(50 * COIN, destSpk), 5500, false, false), true);
    BOOST_CHECK_EQUAL(CalculateMPAWeight(extraOut, 50 * COIN, GetTime(), &idxPrev, wtExtra), 50 * COIN);
    BOOST_CHECK_EQUAL(wtExtra, MPA_WEIGHT_POS);

    RemoveOutpointBurnFromCache(out0, 20 * COIN, 5450);
    pcoinsTip->SpendCoin(out0);
    pcoinsTip->SpendCoin(out1);
    pcoinsTip->SpendCoin(extraOut);
}

BOOST_AUTO_TEST_CASE(hardening_v4_quorum_threshold_equivocation_and_standby_pose_test)
{
    ScopedMpaChainParams testParams(CBaseChainParams::REGTEST);

    // 1. H-7: V4 Byzantine threshold floor(2N/3)+1 vs pre-V4 majority N/2+1
    BOOST_CHECK_EQUAL(llmq::GetQuorumThreshold(5), 4U);
    BOOST_CHECK_EQUAL(llmq::GetQuorumThreshold(9), 7U);
    BOOST_CHECK_EQUAL(llmq::GetQuorumThreshold(11), 8U);
    BOOST_CHECK_EQUAL(llmq::GetQuorumThreshold(15), 11U);

    // 2. H-7: AllowQuorumSignature anti-equivocation & 45s competing-coordinator guard
    {
        CKey coordA, coordB;
        coordA.MakeNewKey(true);
        coordB.MakeNewKey(true);
        const uint256 prevHash = GetRandHash();
        const uint256 blkHash1 = GetRandHash();
        const uint256 blkHash2 = GetRandHash();
        const uint256 otherPrev = GetRandHash();

        BOOST_CHECK(llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blkHash1));
        BOOST_CHECK(llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blkHash1));  // idempotent re-check
        BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash, coordA.GetPubKey(), blkHash2)); // same coord equivocating
        BOOST_CHECK(!llmq::AllowQuorumSignature(prevHash, coordB.GetPubKey(), blkHash2)); // competing coord within 45s
        BOOST_CHECK(llmq::AllowQuorumSignature(otherPrev, coordB.GetPubKey(), blkHash2)); // distinct parent allowed
    }

    // 3. M-4: Silent primary displaced by standby substitution receives +5 PoSe penalty under V4
    {
        LOCK(cs_main);
        std::unique_ptr<dip3::CDeterministicMNManager> origMgr = std::move(deterministicMNManager);
        deterministicMNManager.reset(new dip3::CDeterministicMNManager(1 << 20, true, true));
        const bool origPingInit = g_fMasternodePingStateInitialized;
        g_fMasternodePingStateInitialized = true;

        CBlockIndex* pOrigTip = chainActive.Tip();
        std::vector<uint256> vHashes;
        std::vector<CBlockIndex*> vIndices;
        std::vector<COutPoint> vAddedCoins;
        std::vector<CPubKey> vMnOwners;

        auto appendBlock = [&](const std::vector<CTransaction>& txs) -> CBlockIndex* {
            const int h = chainActive.Tip()->nHeight + 1;
            CBlock blk;
            blk.nVersion = 12;
            blk.hashPrevBlock = chainActive.Tip()->GetBlockHash();
            blk.nTime = chainActive.Tip()->nTime + 30;
            blk.nNonce = (uint32_t)h;
            CMutableTransaction cb;
            cb.vin.push_back(CTxIn(COutPoint(), CScript() << h << OP_0));
            cb.vout.push_back(CTxOut(0, CScript()));
            blk.vtx.push_back(cb);
            for (const auto& tx : txs) blk.vtx.push_back(tx);
            blk.hashMerkleRoot = BlockMerkleRoot(blk);
            vHashes.push_back(blk.GetHash());
            CBlockIndex* pIdx = new CBlockIndex(blk);
            pIdx->nHeight = h;
            pIdx->pprev = chainActive.Tip();
            auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pIdx));
            pIdx->phashBlock = &(ins.first->first);
            pIdx->BuildSkip();
            vIndices.push_back(pIdx);
            {
                LOCK(cs_recent_vrf_proofs);
                mapRecentVRFProofs[vHashes.back()] = blk.vAdamVRFProof;
            }
            chainActive.SetTip(pIdx);
            pcoinsTip->SetBestBlock(vHashes.back());
            CValidationState st;
            BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blk, pIdx, st, *pcoinsTip, false));
            return pIdx;
        };

        while (chainActive.Height() < 20) {
            appendBlock({});
        }

        std::map<CPubKey, uint256> mapOwnerToProTx;
        for (int i = 0; i < 14; ++i) {
            const int hNext = chainActive.Height() + 1;
            const CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(hNext);
            CKey owner, feeKey;
            owner.MakeNewKey(true);
            feeKey.MakeNewKey(true);
            CBLSSecretKey opSk;
            opSk.MakeNewKey();

            const COutPoint colOut(GetRandHash(), 0);
            const COutPoint feeOut(GetRandHash(), 1);
            const CScript colSpk = GetScriptForDestination(owner.GetPubKey().GetID());
            const CScript feeSpk = GetScriptForDestination(feeKey.GetPubKey().GetID());
            pcoinsTip->AddCoin(colOut, Coin(CTxOut(colAmt, colSpk), 1, false, false), true);
            pcoinsTip->AddCoin(feeOut, Coin(CTxOut(1 * COIN, feeSpk), 1, false, false), true);
            vAddedCoins.push_back(colOut);
            vAddedCoins.push_back(feeOut);

            CMutableTransaction mtxReg;
            mtxReg.vin.push_back(CTxIn(feeOut));
            dip3::CProRegTx reg;
            reg.nVersion = 2;
            reg.collateralOutpoint = colOut;
            reg.addr = LookupNumeric("198.51.100.10", 28000 + i);
            reg.pubKeyOwner = owner.GetPubKey();
            reg.blsPubKeyOperator = CBLSPubKey(opSk);
            BOOST_REQUIRE(reg.blsPoP.SignPoP(opSk));
            reg.keyIDVoting = owner.GetPubKey().GetID();
            reg.nOperatorReward = 0;
            reg.scriptPayout = colSpk;
            reg.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxReg));
            BOOST_REQUIRE(owner.Sign(reg.GetSignHash(), reg.vchSig));
            mtxReg.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, reg)));
            CTransaction txReg(mtxReg);
            appendBlock({txReg});
            vMnOwners.push_back(owner.GetPubKey());
            mapOwnerToProTx[owner.GetPubKey()] = txReg.GetHash();
            BOOST_REQUIRE(RegisterOnChainMasternodePing(owner.GetPubKey(), colOut, chainActive.Height(), nullptr, pcoinsTip));
        }

        // Advance past Regtest UPGRADE_ADAM (200) so ProcessBlock committee PoSe scoring is active
        while (chainActive.Height() < 205) {
            appendBlock({});
        }

        const int nextH = chainActive.Height() + 1;
        CBlockIndex* pPrev = chainActive.Tip();
        const uint256 seed = GetAdamSeed(pPrev);
        std::vector<CPubKey> vExpectedPrimary, vStandby;
        CPubKey expectedCoord;
        BOOST_REQUIRE(SelectAdamNodes(seed, Params().GetConsensus(), vExpectedPrimary, expectedCoord, nextH, pcoinsTip, &vStandby));
        BOOST_REQUIRE(!vExpectedPrimary.empty());

        CPubKey replacementKey;
        for (const auto& pk : vStandby) {
            if (std::find(vExpectedPrimary.begin(), vExpectedPrimary.end(), pk) == vExpectedPrimary.end()) {
                replacementKey = pk;
                break;
            }
        }
        if (!replacementKey.IsValid()) {
            for (const auto& pk : vMnOwners) {
                if (std::find(vExpectedPrimary.begin(), vExpectedPrimary.end(), pk) == vExpectedPrimary.end()) {
                    replacementKey = pk;
                    break;
                }
            }
        }
        BOOST_REQUIRE(replacementKey.IsValid());

        size_t subIdx = 0;
        while (subIdx < vExpectedPrimary.size() && !mapOwnerToProTx.count(vExpectedPrimary[subIdx])) {
            ++subIdx;
        }
        BOOST_REQUIRE_LT(subIdx, vExpectedPrimary.size());
        const CPubKey displacedPrimary = vExpectedPrimary[subIdx];
        CBlock blkSub;
        blkSub.nVersion = 12;
        blkSub.hashPrevBlock = pPrev->GetBlockHash();
        blkSub.nTime = pPrev->nTime + 30;
        blkSub.nNonce = (uint32_t)nextH;
        blkSub.vAdamMiners = vExpectedPrimary;
        blkSub.vAdamMiners[subIdx] = replacementKey;
        blkSub.vAdamSolutions.resize(blkSub.vAdamMiners.size(), std::vector<unsigned char>(32, 0x11));
        CMutableTransaction cbSub;
        cbSub.vin.push_back(CTxIn(COutPoint(), CScript() << nextH << OP_0));
        cbSub.vout.push_back(CTxOut(0, CScript()));
        blkSub.vtx.push_back(cbSub);
        blkSub.hashMerkleRoot = BlockMerkleRoot(blkSub);

        vHashes.push_back(blkSub.GetHash());
        CBlockIndex* pSubIdx = new CBlockIndex(blkSub);
        pSubIdx->nHeight = nextH;
        pSubIdx->pprev = pPrev;
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pSubIdx));
        pSubIdx->phashBlock = &(ins.first->first);
        pSubIdx->BuildSkip();
        vIndices.push_back(pSubIdx);
        chainActive.SetTip(pSubIdx);
        CValidationState stSub;
        BOOST_REQUIRE(deterministicMNManager->ProcessBlock(blkSub, pSubIdx, stSub, *pcoinsTip, false));

        const dip3::CDeterministicMNList listAfter = deterministicMNManager->GetListForBlock(pSubIdx);
        const dip3::CDeterministicMN* dmnDisplaced = listAfter.GetMN(mapOwnerToProTx.at(displacedPrimary));
        BOOST_REQUIRE(dmnDisplaced != nullptr);
        BOOST_CHECK_EQUAL(dmnDisplaced->state.nPoSePenalty, 5);

        for (const auto& pk : vMnOwners) {
            mapMasternodeLastActiveHeight.erase(pk);
            mapMasternodeActiveHeights.erase(pk);
            mapMasternodeCollateralOutpoints.erase(pk);
        }
        for (const auto& outp : vAddedCoins) {
            pcoinsTip->SpendCoin(outp);
        }
        {
            LOCK(cs_recent_vrf_proofs);
            for (const auto& h : vHashes) {
                mapBlockIndex.erase(h);
                mapRecentVRFProofs.erase(h);
            }
        }
        for (CBlockIndex* pIdx : vIndices) {
            delete pIdx;
        }
        chainActive.SetTip(pOrigTip);
        BumpMasternodePingStateGen();
        g_fMasternodePingStateInitialized = origPingInit;
        deterministicMNManager = std::move(origMgr);
    }
}

BOOST_AUTO_TEST_CASE(llmq_livelock_recovery_and_slot_sync_test)
{
    const int64_t parentTime = 1795864605;

    // 1. Verify deterministic PoBLS proposal slot mapping across boundaries
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime - 5), 0);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime), 0);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime + 44), 0);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime + 45), 1);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime + 74), 1);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime + 75), 2);
    BOOST_CHECK_EQUAL(llmq::ComputeQuorumProposalSlot(parentTime, parentTime + 495), 16);

    CKey k0, k1, k2;
    k0.MakeNewKey(true);
    k1.MakeNewKey(true);
    k2.MakeNewKey(true);

    // 2. Same-slot equivocation is refused even if >= 15s elapsed inside the same slot
    {
        const uint256 parent1 = GetRandHash();
        const uint256 blk0 = GetRandHash();
        const uint256 blk0Alt = GetRandHash();
        const uint256 blk1SameSlot = GetRandHash();
        const int64_t t0 = parentTime + 15;

        BOOST_CHECK(llmq::AllowQuorumSignature(parent1, k0.GetPubKey(), blk0, 0, t0));
        BOOST_CHECK(llmq::AllowQuorumSignature(parent1, k0.GetPubKey(), blk0, 0, t0 + 20)); // idempotent rebroadcast
        BOOST_CHECK(!llmq::AllowQuorumSignature(parent1, k0.GetPubKey(), blk0Alt, 0, t0 + 15));
        BOOST_CHECK(!llmq::AllowQuorumSignature(parent1, k1.GetPubKey(), blk1SameSlot, 0, t0 + 20));
    }

    // 3. Sub-settle slot transition (< 15s) is guarded, and recovery converges cleanly at the next settled slot
    {
        const uint256 parentDesync = GetRandHash();
        const uint256 blkLateSlot0 = GetRandHash();
        const uint256 blkSlot1 = GetRandHash();
        const uint256 blkSlot2 = GetRandHash();

        BOOST_CHECK(llmq::AllowQuorumSignature(parentDesync, k0.GetPubKey(), blkLateSlot0, 0, parentTime + 42));
        // Only 3s later at slot 1 boundary -> refused (< QUORUM_MIN_SLOT_SETTLE_SECS)
        BOOST_CHECK(!llmq::AllowQuorumSignature(parentDesync, k1.GetPubKey(), blkSlot1, 1, parentTime + 45));
        // Next rotation at t = +75s (33s >= 15s) cleanly advances to slot 2
        BOOST_CHECK(llmq::AllowQuorumSignature(parentDesync, k2.GetPubKey(), blkSlot2, 2, parentTime + 75));
        // Displaced prior block hash remains permanently rejected even in later slots
        BOOST_CHECK(!llmq::AllowQuorumSignature(parentDesync, k0.GetPubKey(), blkLateSlot0, 3, parentTime + 105));
    }

    // 4. Hard lock immutability: once >= quorum threshold shares exist, slot advance is forbidden
    {
        const uint256 parentLocked = GetRandHash();
        const uint256 blkLocked = GetRandHash();
        const uint256 blkNext = GetRandHash();

        BOOST_CHECK(llmq::AllowQuorumSignature(parentLocked, k0.GetPubKey(), blkLocked, 1, parentTime + 45));
        {
            LOCK(cs_quorum_sigs);
            auto& sigMap = mapQuorumBlockSigs[blkLocked];
            for (uint32_t idx = 0; idx < 4; ++idx) {
                sigMap.emplace(COutPoint(GetRandHash(), idx), std::vector<unsigned char>(64, 0x01));
            }
        }
        BOOST_CHECK(!llmq::AllowQuorumSignature(parentLocked, k1.GetPubKey(), blkNext, 2, parentTime + 90));
        {
            LOCK(cs_quorum_sigs);
            mapQuorumBlockSigs.erase(blkLocked);
        }
    }

    // 5. Full 15-committee rotation wrap-around recovery without permanent coordinator self-ban
    {
        const uint256 parentWrap = GetRandHash();
        std::vector<CPubKey> vCoords;
        std::vector<uint256> vBlks;
        for (int i = 0; i < 15; ++i) {
            CKey ck;
            ck.MakeNewKey(true);
            vCoords.push_back(ck.GetPubKey());
            vBlks.push_back(GetRandHash());
        }

        for (int i = 0; i < 15; ++i) {
            const int64_t slot = i + 1;
            const int64_t now = parentTime + 45 + static_cast<int64_t>(i) * 30;
            BOOST_CHECK(llmq::AllowQuorumSignature(parentWrap, vCoords[i], vBlks[i], slot, now));
        }

        // Rotation wraps back to vCoords[0] at slot 16 with a new block hash
        const uint256 blkWrap = GetRandHash();
        const int64_t tWrap = parentTime + 45 + 15 * 30;
        BOOST_CHECK(llmq::AllowQuorumSignature(parentWrap, vCoords[0], blkWrap, 16, tWrap));
        // Old displaced hashes from earlier slots remain permanently rejected
        BOOST_CHECK(!llmq::AllowQuorumSignature(parentWrap, vCoords[0], vBlks[0], 17, tWrap + 30));
        BOOST_CHECK(!llmq::AllowQuorumSignature(parentWrap, vCoords[14], vBlks[14], 17, tWrap + 30));
    }
}

BOOST_AUTO_TEST_SUITE_END()
