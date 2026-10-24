// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "kernel.h"
#include "llmq.h"
#include "main.h"
#include "chainparams.h"
#include "masternodeman.h"
#include "core_io.h"
#include "script/mescal.h"
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
        BOOST_CHECK(memberKeys[i].Sign(blockHash, sig));
        qsig.signatures.emplace_back(customQuorum.members[i].collateralOutpoint, sig);
    }
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // Duplicate signature from member 0 must be ignored (still 2 unique members -> fail)
    qsig.signatures.push_back(qsig.signatures[0]);
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // Signature from a non-member must be ignored (still 2 unique members -> fail)
    CKey outsiderKey;
    outsiderKey.MakeNewKey(true);
    std::vector<unsigned char> outsiderSig;
    BOOST_CHECK(outsiderKey.Sign(blockHash, outsiderSig));
    qsig.signatures.emplace_back(COutPoint(uint256S("9999"), 0), outsiderSig);
    BOOST_CHECK(!qsig.Verify(customQuorum));

    // Add 3rd valid quorum member signature -> reaches 3/5 majority threshold -> must pass!
    std::vector<unsigned char> sig2;
    BOOST_CHECK(memberKeys[2].Sign(blockHash, sig2));
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

BOOST_AUTO_TEST_SUITE_END()
