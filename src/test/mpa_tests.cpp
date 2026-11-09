// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "kernel.h"
#include "llmq.h"
#include "main.h"
#include "chainparams.h"
#include "masternode-payments.h"
#include "masternodeman.h"
#include "core_io.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "script/mescal.h"
#include "script/sign.h"
#include "script/standard.h"
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

    // Test Proof of Burn (PoB) decay logic
    // Add burn transaction to burn cache at height 450
    AddBurnToCache(dest, 10 * COIN, 450);

    // Height 500: T = 50. Decay should be: 1.0 - 50 / nBurnDecayBlocks.
    int nBurnDecayBlocks = Params().GetConsensus().nBurnDecayBlocks;
    double decay = 1.0 - 50.0 / (double)nBurnDecayBlocks;
    weight = GetActiveBurnWeight(dest, 500);
    BOOST_CHECK_EQUAL(weight, (CAmount)(10 * COIN * 5.0 * decay));

    // Height past decay limit: T = nBurnDecayBlocks + 50. Weight must be 0
    weight = GetActiveBurnWeight(dest, 450 + nBurnDecayBlocks + 50);
    BOOST_CHECK_EQUAL(weight, 0);
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
    CAmount nBlockValActual = 14 * COIN + COIN / 2;
    CAmount nTreasurySplit = nBlockValActual * 7 / 100;
    CAmount nFaucetSplit = nBlockValActual * 5 / 100;
    
    BOOST_CHECK_EQUAL(nTreasurySplit, 101500000); // 14.5 * 0.07 = 1.015 COIN
    BOOST_CHECK_EQUAL(nFaucetSplit, 72500000);   // 14.5 * 0.05 = 0.725 COIN
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
    // Height 2500 -> nHeight = 2501 (above UPGRADE_POMBL activation height 2000 on Mainnet)
    CBlockIndex indexPrev;
    indexPrev.nHeight = 2500;
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
    pcoinsTip->AddCoin(polOutHalf, Coin(CTxOut(100 * COIN, polScriptHalf), 2400, false, false), true);

    int weightType = -1;
    CAmount weightHalf = CalculateMPAWeight(polOutHalf, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POL);
    BOOST_CHECK_GT(weightHalf, 100 * COIN);

    // 2. Proof of Lock (PoL) with longer CLTV lock script (3-byte locktime push > 2-byte locktime push)
    int64_t lockHeightMax = nHeight + 60000;
    CScript polScriptMax = CScript() << CScriptNum(lockHeightMax) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint polOutMax(uint256S("1111000011110000111100001111000011110000111100001111000011110002"), 0);
    pcoinsTip->AddCoin(polOutMax, Coin(CTxOut(100 * COIN, polScriptMax), 2400, false, false), true);

    CAmount weightMax = CalculateMPAWeight(polOutMax, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POL);
    BOOST_CHECK_GT(weightMax, weightHalf);

    // 3. Proof of Burn (PoB) integration via CalculateMPAWeight and RemoveBurnFromCache
    CScript p2pkhScript = GetScriptForDestination(dest);
    COutPoint pobOut(uint256S("1111000011110000111100001111000011110000111100001111000011110003"), 0);
    pcoinsTip->AddCoin(pobOut, Coin(CTxOut(100 * COIN, p2pkhScript), 2400, false, false), true);

    AddBurnToCache(dest, 20 * COIN, 2451);
    CAmount expectedBurnWeight = GetActiveBurnWeight(dest, nHeight);
    BOOST_CHECK_GT(expectedBurnWeight, 0);

    CAmount totalPobWeight = CalculateMPAWeight(pobOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POB);
    BOOST_CHECK_EQUAL(totalPobWeight, 100 * COIN + expectedBurnWeight);

    // Removing the burn from cache reverts the UTXO to standard PoS weight
    RemoveBurnFromCache(dest, 20 * COIN, 2451);
    BOOST_CHECK_EQUAL(GetActiveBurnWeight(dest, nHeight), 0);
    CAmount posFallbackWeight = CalculateMPAWeight(pobOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(posFallbackWeight, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(mpa_weight_pom_test)
{
    CBlockIndex indexPrev;
    indexPrev.nHeight = 2500;

    CKey mnKey;
    mnKey.MakeNewKey(true);
    COutPoint mnOutpoint(uint256S("2222000022220000222200002222000022220000222200002222000022220001"), 0);

    CMasternode mn;
    mn.vin = CTxIn(mnOutpoint);
    mn.pubKeyMasternode = mnKey.GetPubKey();
    mn.sigTime = GetTime() - 3600;
    mn.protocolVersion = PROTOCOL_VERSION;
    mnodeman.Add(mn);

    int weightType = -1;
    CAmount pomWeight = CalculateMPAWeight(mnOutpoint, 10000 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_GE(pomWeight, 10000 * COIN);
    BOOST_CHECK_LE(pomWeight, 20000 * COIN);

    mnodeman.Clear();
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

    // Sign with only 2 members (below 3/5 threshold) -> must fail
    for (int i = 0; i < 2; ++i) {
        std::vector<unsigned char> sig;
        CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(memberKeys[i], blockHash);
        BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[i], blsSk, sig));
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
    BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, outsiderKey, outsiderBlsSk, outsiderSig));
    qsig.signatures.emplace_back(COutPoint(uint256S("9999"), 0), outsiderSig);
    BOOST_CHECK(!qsig.Verify(customQuorum));
    qsig.signatures.pop_back();

    // Plain ECDSA signature without BLS12-381 component must be rejected even for a valid member
    std::vector<unsigned char> plainEcdsaSig;
    BOOST_CHECK(memberKeys[2].Sign(blockHash, plainEcdsaSig));
    qsig.signatures.emplace_back(customQuorum.members[2].collateralOutpoint, plainEcdsaSig);
    BOOST_CHECK(!qsig.Verify(customQuorum));
    qsig.signatures.pop_back();

    // Add 3rd valid quorum member hybrid BLS12-381 signature -> reaches 3/5 majority threshold -> must pass!
    std::vector<unsigned char> sig2;
    CBLSSecretKey blsSk2 = DeriveEphemeralBLSFromCKey(memberKeys[2], blockHash);
    BOOST_CHECK(SignBLSWithECDSAFallback(blockHash, memberKeys[2], blsSk2, sig2));
    qsig.signatures.emplace_back(customQuorum.members[2].collateralOutpoint, sig2);
    BOOST_CHECK(qsig.Verify(customQuorum));

    // Tampering with blockHash must invalidate quorum signature verification
    qsig.blockHash = uint256S("6666666666666666666666666666666666666666666666666666666666666666");
    BOOST_CHECK(!qsig.Verify(customQuorum));
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

    // 6. Fixed-point 128-bit integer subsidy determinism across graduated bootstrap and decay periods
    {
        ScopedMpaChainParams mainScope(CBaseChainParams::MAIN);
        BOOST_CHECK_EQUAL(Params().GetConsensus().nAdamQuorumEnforcementHeight, 200);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(0), 15 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(1), 0);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2), 40 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2200), 40 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2201), 30 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(5000), 30 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(5001), 20 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(9999), 20 * COIN);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(10000), 1500000000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(50000), 1500000000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(350000), 1471500000LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(1061202), 1389208041LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2112402), 1286599322LL);
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(3163602), 1191569416LL);
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
        BOOST_CHECK_EQUAL(electedMiners1.size(), (size_t)Params().GetConsensus().nAdamMinersCount);
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

        // Active ticket holder (valKey1) must be ranked ahead of validator without a submitted ticket (valKey2)
        std::vector<CPubKey> vMiners = {valKey2.GetPubKey(), valKey1.GetPubKey()};
        std::map<CPubKey, std::vector<unsigned char>> mapSols;
        mapSols[valKey1.GetPubKey()] = vchSol1;
        std::vector<CPubKey> ranked = RankValidatorsByPoBLSDistance(seedH, 1000, vMiners, mapSols);
        BOOST_CHECK_EQUAL(ranked.size(), 2U);
        BOOST_CHECK(ranked[0] == valKey1.GetPubKey());
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

        // Sign with 3 out of 5 members using ephemeral BLS12-381 + secp256k1 authorization
        for (int i = 0; i < 3; ++i) {
            CBLSSecretKey blsSk = DeriveEphemeralBLSFromCKey(memberKeys[i], qsig.blockHash);
            std::vector<unsigned char> vchMemberSig;
            BOOST_CHECK(SignBLSWithECDSAFallback(qsig.blockHash, memberKeys[i], blsSk, vchMemberSig));
            qsig.signatures.push_back({quorum.members[i].collateralOutpoint, vchMemberSig});
        }

        BOOST_CHECK(qsig.Verify(quorum));
        CBLSPubKey aggPubKey;
        CBLSSignature aggSig;
        BOOST_CHECK(qsig.AggregateBLSSignatures(quorum, aggPubKey, aggSig));
        BOOST_CHECK(aggPubKey.IsValid());
        BOOST_CHECK(aggSig.IsValid());
        BOOST_CHECK(aggSig.Verify(aggPubKey, qsig.blockHash));
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

        CMasternode mnDelegated;
        mnDelegated.vin = CTxIn(hotColdOut);
        mnDelegated.pubKeyCollateralAddress = coldCollateralKey.GetPubKey();
        mnDelegated.pubKeyMasternode = hotMnKey.GetPubKey();
        mnDelegated.protocolVersion = PROTOCOL_VERSION;
        CMasternodeBroadcast mnbDelegated(mnDelegated);
        BOOST_CHECK(mnbDelegated.Sign(coldCollateralKey, coldCollateralKey.GetPubKey()));
        BOOST_CHECK(mnbDelegated.CheckSignature());

        COnChainMasternodeDelegation delProof;
        delProof.pubKeyCollateralAddress = mnbDelegated.pubKeyCollateralAddress;
        delProof.pubKeyMasternode = mnbDelegated.pubKeyMasternode;
        delProof.addr = mnbDelegated.addr;
        delProof.sigTime = mnbDelegated.sigTime;
        delProof.protocolVersion = mnbDelegated.protocolVersion;
        delProof.nMessVersion = MessageVersion::MESS_VER_HASH; // Simulate mncache.dat deserialization where nMessVersion defaulted to 1
        delProof.vchCollateralSig = mnbDelegated.GetVchSig();

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

            CMasternode mnUncomp;
            mnUncomp.vin = CTxIn(uncompHotOut);
            mnUncomp.pubKeyCollateralAddress = coldCollateralKey.GetPubKey();
            mnUncomp.pubKeyMasternode = uncompHotMnKey.GetPubKey();
            mnUncomp.protocolVersion = PROTOCOL_VERSION;
            CMasternodeBroadcast mnbUncomp(mnUncomp);
            BOOST_CHECK(mnbUncomp.Sign(coldCollateralKey, coldCollateralKey.GetPubKey()));
            BOOST_CHECK(mnbUncomp.CheckSignature());

            COnChainMasternodeDelegation uncompDel;
            uncompDel.pubKeyCollateralAddress = mnbUncomp.pubKeyCollateralAddress;
            uncompDel.pubKeyMasternode = mnbUncomp.pubKeyMasternode;
            uncompDel.addr = mnbUncomp.addr;
            uncompDel.sigTime = mnbUncomp.sigTime;
            uncompDel.protocolVersion = mnbUncomp.protocolVersion;
            uncompDel.nMessVersion = mnbUncomp.nMessVersion;
            uncompDel.vchCollateralSig = mnbUncomp.GetVchSig();

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

        // 11. Test LevelDB Burn Cache Persistence (FlushBurnCacheToDisk / ReadBurnCache)
        if (pblocktree) {
            CTxDestination burnDest = mnOwnerKey.GetPubKey().GetID();
            uint256 testTipHash = uint256S("0x9988776655443322110099887766554433221100998877665544332211009988");
            AddBurnToCache(burnDest, 50 * COIN, 1234);
            BOOST_CHECK(FlushBurnCacheToDisk(testTipHash));

            uint256 readTipHash;
            std::map<std::string, std::vector<CBurnCoins>> readBurnMap;
            BOOST_CHECK(pblocktree->ReadBurnCache(readTipHash, readBurnMap));
            BOOST_CHECK(readTipHash == testTipHash);
            std::string strBurnAddr = EncodeDestination(burnDest);
            BOOST_CHECK(readBurnMap.count(strBurnAddr) > 0);
            BOOST_CHECK(!readBurnMap[strBurnAddr].empty());
            BOOST_CHECK_EQUAL(readBurnMap[strBurnAddr].back().nAmount, 50 * COIN);
            BOOST_CHECK_EQUAL(readBurnMap[strBurnAddr].back().nHeight, 1234);

            RemoveBurnFromCache(burnDest, 50 * COIN, 1234);
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

                // Model D net 35% passive Masternode payment after 7% Treasury + 0.7% Faucet deduction
                // Height 2500 (30 KRISTA block value -> 27.69 KRISTA net -> 9.6915 KRISTA passive MN share)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(2500), 969150000LL);
                // Height 7500 (20 KRISTA block value -> 18.46 KRISTA net -> 6.461 KRISTA passive MN share)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(7500), 646100000LL);
                // Height 15000 (15 KRISTA block value -> 13.845 KRISTA net -> 4.84575 KRISTA passive MN share)
                BOOST_CHECK_EQUAL(CMasternode::GetMasternodePayment(15000), 484575000LL);
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
    BOOST_CHECK(qsig.Verify(quorum));
}

BOOST_AUTO_TEST_CASE(mpa_security_hardening_test)
{
    CBlockIndex indexPrev;
    indexPrev.nHeight = 2500;
    const int nHeight = indexPrev.nHeight + 1;

    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();

    // 1. Fake conditional CLTV script (OP_0 OP_IF <lock> OP_CLTV OP_ENDIF ...) must NOT grant PoL weight
    CScript fakeIfCltv = CScript() << OP_0 << OP_IF << CScriptNum(nHeight + 50000) << OP_CHECKLOCKTIMEVERIFY << OP_DROP << OP_ENDIF
                                   << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint fakeIfOut(uint256S("3333000033330000333300003333000033330000333300003333000033330001"), 0);
    pcoinsTip->AddCoin(fakeIfOut, Coin(CTxOut(100 * COIN, fakeIfCltv), 2400, false, false), true);

    int weightType = -1;
    CAmount fakeWeight = CalculateMPAWeight(fakeIfOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(fakeWeight, 100 * COIN);

    // 2. Expired absolute CLTV lock (lockHeight <= nHeight) must revert to PoS (1x), not relative CSV
    CScript expiredCltv = CScript() << CScriptNum(nHeight - 10) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                    << OP_DUP << OP_HASH160 << ToByteVector(pubKey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
    COutPoint expiredOut(uint256S("3333000033330000333300003333000033330000333300003333000033330002"), 0);
    pcoinsTip->AddCoin(expiredOut, Coin(CTxOut(100 * COIN, expiredCltv), 2000, false, false), true);

    CAmount expiredWeight = CalculateMPAWeight(expiredOut, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
    BOOST_CHECK_EQUAL(expiredWeight, 100 * COIN);

    // 3. On-chain Masternode collateral deterministically receives MPA_WEIGHT_POM without mnodeman
    mnodeman.Clear();
    COutPoint onChainMnOut(uint256S("3333000033330000333300003333000033330000333300003333000033330003"), 0);
    CScript mnCollScript = GetScriptForDestination(pubKey.GetID());
    CAmount nCollAmt = CMasternode::GetMasternodeNodeCollateral(nHeight);
    pcoinsTip->AddCoin(onChainMnOut, Coin(CTxOut(nCollAmt, mnCollScript), 1000, false, false), true);
    BOOST_CHECK(RegisterOnChainMasternodePing(pubKey, onChainMnOut, 1000));
    BOOST_CHECK(RegisterOnChainMasternodePing(pubKey, onChainMnOut, 2500));

    CAmount onChainPomWeight = CalculateMPAWeight(onChainMnOut, nCollAmt, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POM);
    BOOST_CHECK_GT(onChainPomWeight, nCollAmt);

    // 4. Dual-Track (8 Collateralized + 3 Open PoW) Committee Election & Bidirectional Spillover
    {
        uint256 seed = Hash(std::string("dual_track_test_seed").begin(), std::string("dual_track_test_seed").end());
        std::vector<CPubKey> vMnKeys;
        std::map<CPubKey, CKey> mapMnPrivKeys;
        std::set<CPubKey> colSet;
        for (int i = 0; i < 31; ++i) {
            CKey k;
            k.MakeNewKey(false);
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
            BOOST_CHECK_EQUAL(nPostBootstrapThreshold, 7);
            BOOST_CHECK_EQUAL(Params().GetConsensus().GetAdamThreshold(nPostBootstrapHeight), 7);

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
                BOOST_CHECK(SignBLSWithECDSAFallback(puzzleHash, minerPrivKey, blsSk, vchSig));

                CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
                ssSol << nNonce << vchSig;
                return std::vector<unsigned char>(ssSol.begin(), ssSol.end());
            };

            // Case E: All 4 non-Masternode seats withhold solutions, 7 Masternodes online -> 7 >= 7
            std::vector<std::vector<unsigned char>> vSolutions(electedPrimaryTrack.size());
            int nWithheldNonMnSeats = 0;
            for (size_t idx = 0; idx < electedPrimaryTrack.size(); ++idx) {
                const CPubKey& minerPk = electedPrimaryTrack[idx];
                if (!colSet.count(minerPk)) {
                    nWithheldNonMnSeats++;
                    continue;
                }
                vSolutions[idx] = buildValidV12Solution(minerPk, mapMnPrivKeys.at(minerPk), (int)idx, 100 + (uint32_t)idx);
            }

            int nValidSolutions = 0;
            for (size_t idx = 0; idx < electedPrimaryTrack.size(); ++idx) {
                if (!vSolutions[idx].empty()) {
                    BOOST_CHECK(VerifyAdamSolution(prevHash, seed, electedPrimaryTrack[idx], vSolutions[idx], 0x207fffff, 12, nPostBootstrapHeight, (int)idx));
                    nValidSolutions++;
                }
            }
            BOOST_CHECK_EQUAL(nWithheldNonMnSeats, ADAM_COIN_LOCK_TRACK_QUOTA + ADAM_OPEN_POW_TRACK_QUOTA); // 4 withheld
            BOOST_CHECK_EQUAL(nValidSolutions, ADAM_MASTERNODE_TRACK_QUOTA);                                // 7 valid MN solutions
            BOOST_CHECK_GE(nValidSolutions, nPostBootstrapThreshold);                                       // 7 >= 7 quorum met!

            // Case F (R31-1 / R32-2 / R32-3): 5 seats are silent (1 MN offline + 4 non-MN seats withholding -> only 6 primary solutions < 7).
            // Verify:
            //   1) At t = 60s (ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS), standby Masternode (standbyTrack[0]) steps into the silent Masternode seat
            //      (same-track replacement MN -> MN) via ResolveAdamCommitteeForTime and passes ValidateAdamCommitteeForTime with 7 >= 7 valid solutions!
            //   2) Cross-track substitution (e.g. replacing a Masternode seat with a Coin-Lock or Open PoW standby key, or replacing a non-MN seat with an MN standby key)
            //      is rejected by ValidateAdamCommitteeForTime (preserving the 7 MN + 1 Coin-Lock + 3 Open PoW quota).
            //   3) Skipping standbyTrack[0] to use standbyTrack[1] at Slot 1 (t = 60s) is rejected by ValidateAdamCommitteeForTime,
            //      whereas at Slot 2 (t = 90s) when standbyTrack[0] is silent, standbyTrack[1] is accepted.
            //   4) At t = 120s (ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS), even if 0 standby candidates respond,
            //      GetAdamEffectiveThreshold relaxes from 7 to 6 (with a post-bootstrap floor of 5 > 4 non-MN seats, and a bootstrap floor of 3).
            std::map<CPubKey, std::vector<unsigned char>> mapSixPrimarySols;
            int nDroppedMn = 0;
            int droppedMnSeatIdx = -1;
            int firstSilentNonMnSeatIdx = -1;
            for (size_t idx = 0; idx < electedPrimaryTrack.size(); ++idx) {
                const CPubKey& minerPk = electedPrimaryTrack[idx];
                if (!colSet.count(minerPk)) {
                    if (firstSilentNonMnSeatIdx < 0) {
                        firstSilentNonMnSeatIdx = (int)idx;
                    }
                    continue;
                }
                if (nDroppedMn == 0) {
                    // Simulate 1 offline Masternode (so 1 MN + 4 non-MN = 5 silent seats total!)
                    droppedMnSeatIdx = (int)idx;
                    nDroppedMn++;
                    continue;
                }
                mapSixPrimarySols[minerPk] = vSolutions[idx];
            }
            BOOST_CHECK_EQUAL(mapSixPrimarySols.size(), 6U);
            BOOST_REQUIRE_GE(droppedMnSeatIdx, 0);
            BOOST_REQUIRE_GE(firstSilentNonMnSeatIdx, 0);

            // Standby Masternode standbyTrack[0] solves using its deterministic seat or standby index
            const CPubKey& standbyMnPk = standbyTrack[0];
            BOOST_CHECK(colSet.count(standbyMnPk) > 0);
            std::map<CPubKey, std::vector<unsigned char>> mapWithStandbySol = mapSixPrimarySols;
            mapWithStandbySol[standbyMnPk] = buildValidV12Solution(standbyMnPk, mapMnPrivKeys.at(standbyMnPk), droppedMnSeatIdx, 999);

            // Before timeout (t = 30s < 60s): standby substitution is not yet active (only 6 solutions < 7)
            std::vector<CPubKey> vResolvedEarly;
            std::vector<std::vector<unsigned char>> vResolvedSolsEarly;
            int nValidEarly = 0;
            ResolveAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, 30, 0x207fffff, 12,
                electedPrimaryTrack, standbyTrack, mapWithStandbySol, vResolvedEarly, vResolvedSolsEarly, nValidEarly, &colSet, &coinLockSet);
            BOOST_CHECK_EQUAL(nValidEarly, 6);
            BOOST_CHECK(vResolvedEarly == electedPrimaryTrack);

            // At timeout (t = 60s >= ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS): standby Masternode steps into the silent MN seat -> 7 >= 7!
            std::vector<CPubKey> vResolvedTimeout;
            std::vector<std::vector<unsigned char>> vResolvedSolsTimeout;
            int nValidTimeout = 0;
            ResolveAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS, 0x207fffff, 12,
                electedPrimaryTrack, standbyTrack, mapWithStandbySol, vResolvedTimeout, vResolvedSolsTimeout, nValidTimeout, &colSet, &coinLockSet);
            BOOST_CHECK_EQUAL(nValidTimeout, 7);
            BOOST_CHECK(vResolvedTimeout[droppedMnSeatIdx] == standbyMnPk);
            BOOST_CHECK(ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vResolvedTimeout, vResolvedSolsTimeout, electedPrimaryTrack, standbyTrack, &colSet, &coinLockSet));
            // Reject standby substitution if timestamp is forged below 60s
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, 30,
                0x207fffff, 12, vResolvedTimeout, vResolvedSolsTimeout, electedPrimaryTrack, standbyTrack, &colSet, &coinLockSet));

            // R32-2 (Cross-Track Rejection): Attempting to place standbyMnPk (Track 0 MN) into a non-MN seat is rejected
            std::vector<CPubKey> vCrossTrackMiners = electedPrimaryTrack;
            std::vector<std::vector<unsigned char>> vCrossTrackSols = vResolvedSolsEarly;
            vCrossTrackMiners[firstSilentNonMnSeatIdx] = standbyMnPk;
            vCrossTrackSols[firstSilentNonMnSeatIdx] = buildValidV12Solution(standbyMnPk, mapMnPrivKeys.at(standbyMnPk), firstSilentNonMnSeatIdx, 1001);
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vCrossTrackMiners, vCrossTrackSols, electedPrimaryTrack, standbyTrack, &colSet, &coinLockSet));

            // R32-2 (Strict Sequential Order within Track): Skipping standbyTrack[0] to use standbyTrack[1] at Slot 1 (60s) is rejected,
            // but accepted at Slot 2 (90s = 60s + 30s) when standbyTrack[0] is silent
            const CPubKey& secondStandbyMnPk = standbyTrack[1];
            BOOST_CHECK(colSet.count(secondStandbyMnPk) > 0);
            std::vector<CPubKey> vSkippedStandbyMiners = electedPrimaryTrack;
            std::vector<std::vector<unsigned char>> vSkippedStandbySols = vResolvedSolsEarly;
            vSkippedStandbyMiners[droppedMnSeatIdx] = secondStandbyMnPk;
            vSkippedStandbySols[droppedMnSeatIdx] = buildValidV12Solution(secondStandbyMnPk, mapMnPrivKeys.at(secondStandbyMnPk), droppedMnSeatIdx, 1002);
            BOOST_CHECK(!ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS,
                0x207fffff, 12, vSkippedStandbyMiners, vSkippedStandbySols, electedPrimaryTrack, standbyTrack, &colSet, &coinLockSet));
            BOOST_CHECK(ValidateAdamCommitteeForTime(prevHash, seed, nPostBootstrapHeight, ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS + ADAM_COMMITTEE_FALLBACK_SLOT_SECS,
                0x207fffff, 12, vSkippedStandbyMiners, vSkippedStandbySols, electedPrimaryTrack, standbyTrack, &colSet, &coinLockSet));

            // At t = 120s (ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS): even with 0 standby candidates, threshold relaxes (7 -> 6, 5 -> 4)
            const int nRelaxedPostBootstrap = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(nPostBootstrapHeight, ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS);
            BOOST_CHECK_EQUAL(nRelaxedPostBootstrap, 6);
            BOOST_CHECK_GE((int)mapSixPrimarySols.size(), nRelaxedPostBootstrap); // 6 >= 6 block advances!

            // R32-3 & R33-6: Post-bootstrap relaxed threshold floors at 5 (> 4 non-MN seats, so at least 1 MN is always required).
            // During bootstrap (nHeight < 50,000), if active Masternodes >= 7, floor is also 5; only when active MNs < 7 does it floor at 3.
            const int nFloorPostBootstrap = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(nPostBootstrapHeight, 600, 10);
            BOOST_CHECK_EQUAL(nFloorPostBootstrap, Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD);
            BOOST_CHECK_EQUAL(nFloorPostBootstrap, 5);
            BOOST_CHECK_GT(nFloorPostBootstrap, ADAM_COIN_LOCK_TRACK_QUOTA + ADAM_OPEN_POW_TRACK_QUOTA);

            const int nFloorBootstrapWith7Mns = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(205, 600, 7);
            BOOST_CHECK_EQUAL(nFloorBootstrapWith7Mns, Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD);
            BOOST_CHECK_EQUAL(nFloorBootstrapWith7Mns, 5);

            const int nRelaxedBootstrapNoMns = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(205, ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS, 0);
            BOOST_CHECK_EQUAL(nRelaxedBootstrapNoMns, 4); // 4 online keys during bootstrap (with <7 MNs) >= 4!
            const int nFloorBootstrapNoMns = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(205, 600, 0);
            BOOST_CHECK_EQUAL(nFloorBootstrapNoMns, Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP);
            BOOST_CHECK_EQUAL(nFloorBootstrapNoMns, 3);

            // Case G (R34-3 / Equal Work Equal Reward):
            // Verify FillBlockPayee and IsBlockPayeeValid under Model D (height 600 on Regtest):
            //   - Every solved seat (both primary seats and legitimate same-track standby-substituted seats) receives equal 1/11 share.
            //   - Passive Masternode Winner receives exact 50% (nMNSplit).
            //   - Unclaimed empty-seat shares (nUnclaimedPartSplit) are added to the Developer Treasury output (nTreasurySplit + nUnclaimedPartSplit).
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
                BOOST_REQUIRE_EQUAL(vElectedModelD.size(), 11U);
                BOOST_REQUIRE(!vStandbyModelD.empty());

                auto findDetKey = [&](const CPubKey& pk) -> CKey {
                    for (int i = 0; i < 24; ++i) {
                        CKey k = GetAdamDeterministicKey(i);
                        if (k.GetPubKey() == pk) return k;
                    }
                    return CKey();
                };

                // Build a block at t = 60s where seat 0 is substituted by standby vStandbyModelD[0], seats 1..6 are solved by primary validators,
                // and seats 7..10 are empty (7 solved seats total: 1 standby + 6 primary, 4 empty seats).
                CBlock testBlock;
                testBlock.nVersion = 12;
                testBlock.hashPrevBlock = testPrevHash;
                testBlock.nTime = pindexPrevTest->nTime + ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS;
                testBlock.nBits = 0x207fffff;
                testBlock.vAdamMiners = vElectedModelD;
                testBlock.vAdamMiners[0] = vStandbyModelD[0];
                testBlock.vAdamSolutions.resize(11);

                std::vector<CPubKey> vSolvedMiners;
                for (int seat = 0; seat < 7; ++seat) {
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
                    BOOST_CHECK(SignBLSWithECDSAFallback(pHash, seatKey, blsSk, vchSig));
                    CDataStream ssSol(SER_NETWORK, PROTOCOL_VERSION);
                    ssSol << nNonce << vchSig;
                    testBlock.vAdamSolutions[seat] = std::vector<unsigned char>(ssSol.begin(), ssSol.end());
                    vSolvedMiners.push_back(seatPk);
                }

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

                // Verify exact amounts under Option 2 (35% Passive MN / 10% LLMQ / 40% Validator [20% 4 Miner + 20% 7 MN] / 15% Coordinator):
                // - Passive MN gets exact 35% (nMNSplit),
                // - 4 primary independent miners (seats 1..4) get the 4 Miner Quota seats (5% each = 20% total = nMinerPartPool),
                // - Standby seat 0 and spillover primary seats 5..6 get the MN/spillover Validator seat share (nMNSeatBase = 20% / 7),
                // - Coordinator gets 15% minus standby substitution deduction (nCoordPenalty = nMNSeatBase),
                // - Developer Treasury gets base 7% + unclaimed empty-seat shares + coordinator substitution deduction!
                CAmount nBlockValActual = CMasternode::GetBlockValue(nModelDTestHeight);
                CAmount nTreasurySplit = nBlockValActual * 7 / 100;
                CAmount nFaucetSplit = nBlockValActual * 7 / 1000;
                CAmount nBlockVal = nBlockValActual - (nTreasurySplit + nFaucetSplit);
                CAmount nMNSplit = nBlockVal * 35 / 100;
                CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
                CAmount nPartSplitTotal = nBlockVal * 40 / 100;
                CAmount nMinerPartPool = nBlockVal * 20 / 100;
                CAmount nMNPartPool = nPartSplitTotal - nMinerPartPool;
                CAmount nMNSeatBase = nMNPartPool / 7;
                CAmount nPaidPartTotal = nMinerPartPool + (3 * nMNSeatBase);
                CAmount nCoordPenalty = 1 * nMNSeatBase;
                CAmount nExpectedUnclaimed = (nPartSplitTotal - nPaidPartTotal) + nCoordPenalty;
                CAmount nExpectedCoord = nBlockVal - (nMNSplit + nLLMQSplitTotal + nPartSplitTotal + nCoordPenalty);

                BOOST_CHECK_EQUAL(testBlock.vtx[0].vout[0].nValue, nExpectedCoord);
                BOOST_CHECK_EQUAL(testBlock.vtx[0].vout[1].nValue, nMNSplit);
                CScript standbyScript = GetScriptForDestination(vStandbyModelD[0].GetID());
                bool foundStandbyPayee = false;
                for (const auto& out : testBlock.vtx[0].vout) {
                    if (out.scriptPubKey == standbyScript && out.nValue == nMNSeatBase) {
                        foundStandbyPayee = true;
                        break;
                    }
                }
                BOOST_CHECK(foundStandbyPayee);

                CScript treasuryScript = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
                bool foundTreasuryWithUnclaimed = false;
                for (const auto& out : testBlock.vtx[0].vout) {
                    if (out.scriptPubKey == treasuryScript && out.nValue == nTreasurySplit + nExpectedUnclaimed) {
                        foundTreasuryWithUnclaimed = true;
                        break;
                    }
                }
                BOOST_CHECK(foundTreasuryWithUnclaimed);

                // Attempting to divert nExpectedUnclaimed from Developer Treasury to Passive MN Winner must be rejected!
                CBlock badDivertBlock = testBlock;
                CMutableTransaction badTx(badDivertBlock.vtx[0]);
                badTx.vout[1].nValue += nExpectedUnclaimed;
                for (auto& out : badTx.vout) {
                    if (out.scriptPubKey == treasuryScript) {
                        out.nValue = nTreasurySplit;
                    }
                }
                badDivertBlock.vtx[0] = badTx;
                BOOST_CHECK(!IsBlockPayeeValid(badDivertBlock, nModelDTestHeight, pcoinsTip));

                {
                    LOCK(cs_main);
                    mapBlockIndex.erase(testPrevHash);
                }
                delete pindexPrevTest;
            }
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
