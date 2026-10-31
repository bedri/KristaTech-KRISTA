// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "core_io.h"
#include "key.h"
#include "pubkey.h"
#include "primitives/transaction.h"
#include "script/interpreter.h"
#include "script/mescal.h"
#include "script/standard.h"
#include "test/test_kristatech.h"
#include "utilstrencodings.h"

#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(mescal_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(mescal_compile_decompile_and_depth_limit_test)
{
    CKey key1, key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    std::string pk1Hex = HexStr(key1.GetPubKey());
    std::string pk2Hex = HexStr(key2.GetPubKey());
    std::string pkh1Hex = HexStr(ToByteVector(key1.GetPubKey().GetID()));

    // 1. Test arithmetic + boolean + lock-time + equalverify-checksig compilation & decompilation round-trip
    std::string contractJson = strprintf(R"({
        "basic": {
            "Push10": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 10}]},
            "Push4": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 4}]},
            "SubOp": {"role": "sub"},
            "Push6": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 6}]},
            "EqOp": {"role": "num-equal"},
            "TimeLock": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 2500}]},
            "P2PKH": {"role": "equalverify-checksig", "inputs": [{"name": "PubkeyHash", "type": "pubkeyhash", "value": "%s"}]}
        },
        "condition": {
            "MathCheck": {
                "role": "if-condition",
                "expressions": [
                    {"type": "basic", "name": "EqOp"}
                ],
                "true": [
                    {"type": "basic", "name": "TimeLock"},
                    {"type": "basic", "name": "P2PKH"}
                ],
                "false": []
            }
        },
        "contract": {
            "TestMathLock": {
                "actions": [
                    {"type": "basic", "name": "Push10"},
                    {"type": "basic", "name": "Push4"},
                    {"type": "basic", "name": "SubOp"},
                    {"type": "basic", "name": "Push6"},
                    {"type": "condition", "name": "MathCheck"}
                ]
            }
        },
        "active_contract": "TestMathLock"
    })", pkh1Hex);

    std::string err;
    CScript compiled = CMescal::Compile(contractJson, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(!compiled.empty());

    UniValue decompiled = CMescal::Decompile(compiled, err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    CScript recompiled = CMescal::Compile(decompiled.write(), err);
    BOOST_CHECK_MESSAGE(err.empty(), err);
    BOOST_CHECK(compiled == recompiled);

    // 2. Test MAX_MESCAL_CONDITION_DEPTH (32) DoS protection during Decompile
    CScript deepScript;
    for (int i = 0; i < 35; ++i) {
        deepScript << OP_1 << OP_IF;
    }
    deepScript << OP_1;
    for (int i = 0; i < 35; ++i) {
        deepScript << OP_ENDIF;
    }
    std::string depthErr;
    UniValue deepDecompiled = CMescal::Decompile(deepScript, depthErr);
    BOOST_CHECK(!depthErr.empty());
    BOOST_CHECK(deepDecompiled.isNull());
}

BOOST_AUTO_TEST_CASE(mescal_htlc_timelock_boundary_rejection_test)
{
    CKey claimKey, refundKey, wrongKey;
    claimKey.MakeNewKey(true);
    refundKey.MakeNewKey(true);
    wrongKey.MakeNewKey(true);
    std::string claimPkHex = HexStr(claimKey.GetPubKey());
    std::string refundPkHex = HexStr(refundKey.GetPubKey());

    const int64_t nLockExpiry = 1200;
    std::string htlcJson = strprintf(R"({
        "basic": {
            "NumEq": {"role": "num-equal"},
            "ClaimSig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]},
            "RefundLock": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %d}]},
            "RefundSig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]}
        },
        "condition": {
            "HTLCBranch": {
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
                "actions": [{"type": "condition", "name": "HTLCBranch"}]
            }
        },
        "active_contract": "HTLC"
    })", claimPkHex, (int)nLockExpiry, refundPkHex);

    std::string err;
    CScript htlcScript = CMescal::Compile(htlcJson, err);
    BOOST_REQUIRE_MESSAGE(err.empty(), err);

    const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;

    // Case 1: Attempting refund BEFORE nLockExpiry (tx.nLockTime = 1199 < 1200) -> MUST FAIL with SCRIPT_ERR_UNSATISFIED_LOCKTIME
    {
        CMutableTransaction txEarly;
        txEarly.nVersion = 1;
        CTxIn txIn(COutPoint(uint256S("01"), 0));
        txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
        txEarly.vin.push_back(txIn);
        txEarly.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
        txEarly.nLockTime = nLockExpiry - 1;

        uint256 sighash = SignatureHash(htlcScript, txEarly, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
        std::vector<unsigned char> sig;
        BOOST_CHECK(refundKey.Sign(sighash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        txEarly.vin[0].scriptSig = CScript() << OP_1 << sig << OP_1 << OP_0;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txEarly);
        BOOST_CHECK(!VerifyScript(txEarly.vin[0].scriptSig, htlcScript, flags,
                                  TransactionSignatureChecker(&txConst, 0, 10 * COIN), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
    }

    // Case 2: Attempting refund AT nLockExpiry (tx.nLockTime = 1200) but with finalized sequence (SEQUENCE_FINAL) -> MUST FAIL
    {
        CMutableTransaction txFinalSeq;
        txFinalSeq.nVersion = 1;
        CTxIn txIn(COutPoint(uint256S("01"), 0));
        txIn.nSequence = CTxIn::SEQUENCE_FINAL;
        txFinalSeq.vin.push_back(txIn);
        txFinalSeq.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
        txFinalSeq.nLockTime = nLockExpiry;

        uint256 sighash = SignatureHash(htlcScript, txFinalSeq, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
        std::vector<unsigned char> sig;
        BOOST_CHECK(refundKey.Sign(sighash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        txFinalSeq.vin[0].scriptSig = CScript() << OP_1 << sig << OP_1 << OP_0;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txFinalSeq);
        BOOST_CHECK(!VerifyScript(txFinalSeq.vin[0].scriptSig, htlcScript, flags,
                                  TransactionSignatureChecker(&txConst, 0, 10 * COIN), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
    }

    // Case 3: Attempting refund AT nLockExpiry (tx.nLockTime = 1200) with wrong signer key -> MUST FAIL
    {
        CMutableTransaction txWrongSigner;
        txWrongSigner.nVersion = 1;
        CTxIn txIn(COutPoint(uint256S("01"), 0));
        txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
        txWrongSigner.vin.push_back(txIn);
        txWrongSigner.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
        txWrongSigner.nLockTime = nLockExpiry;

        uint256 sighash = SignatureHash(htlcScript, txWrongSigner, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
        std::vector<unsigned char> sig;
        BOOST_CHECK(wrongKey.Sign(sighash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        txWrongSigner.vin[0].scriptSig = CScript() << OP_1 << sig << OP_1 << OP_0;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txWrongSigner);
        BOOST_CHECK(!VerifyScript(txWrongSigner.vin[0].scriptSig, htlcScript, flags,
                                  TransactionSignatureChecker(&txConst, 0, 10 * COIN), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_CHECKSIGVERIFY);
    }

    // Case 4: Valid refund AT nLockExpiry (tx.nLockTime = 1200, non-final sequence, valid refundKey sig) -> MUST PASS
    {
        CMutableTransaction txValidRefund;
        txValidRefund.nVersion = 1;
        CTxIn txIn(COutPoint(uint256S("01"), 0));
        txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
        txValidRefund.vin.push_back(txIn);
        txValidRefund.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
        txValidRefund.nLockTime = nLockExpiry;

        uint256 sighash = SignatureHash(htlcScript, txValidRefund, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);
        std::vector<unsigned char> sig;
        BOOST_CHECK(refundKey.Sign(sighash, sig));
        sig.push_back((unsigned char)SIGHASH_ALL);
        txValidRefund.vin[0].scriptSig = CScript() << OP_1 << sig << OP_1 << OP_0;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txValidRefund);
        BOOST_CHECK(VerifyScript(txValidRefund.vin[0].scriptSig, htlcScript, flags,
                                 TransactionSignatureChecker(&txConst, 0, 10 * COIN), &serror));
    }
}

BOOST_AUTO_TEST_CASE(mescal_taproot_ptlc_script_path_boundary_test)
{
    // Compile a MESCAL timelock + numeric secret (lock-time + number + num-equal) contract and commit it inside a Taproot (BIP341) leaf
    CKey internalKey;
    internalKey.MakeNewKey(true);

    const int64_t tapLockHeight = 25000;
    const int64_t expectedSecret = 987654;

    std::string leafJson = strprintf(R"({
        "basic": {
            "CheckCLTV": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %d}]},
            "PushExpectedSecret": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": %d}]},
            "CheckSecretEq": {"role": "num-equal"}
        },
        "contract": {
            "TapLeafMESCAL": {
                "actions": [
                    {"type": "basic", "name": "CheckCLTV"},
                    {"type": "basic", "name": "PushExpectedSecret"},
                    {"type": "basic", "name": "CheckSecretEq"}
                ]
            }
        },
        "active_contract": "TapLeafMESCAL"
    })", (int)tapLockHeight, (int)expectedSecret);

    std::string err;
    CScript leafScript = CMescal::Compile(leafJson, err);
    BOOST_REQUIRE_MESSAGE(err.empty(), err);

    CXOnlyPubKey internalXOnly(internalKey.GetPubKey());
    BOOST_REQUIRE(internalXOnly.IsFullyValid());

    // Compute BIP341 TapLeaf hash: TaggedHash("TapLeaf", 0xc0 || compact_size(script) || script)
    uint8_t leafVersion = 0xc0;
    unsigned char tagHash[32];
    CSHA256 shaTag;
    shaTag.Write((const unsigned char*)"TapLeaf", 7);
    shaTag.Finalize(tagHash);

    CDataStream ssLeaf(SER_NETWORK, PROTOCOL_VERSION);
    ssLeaf << leafVersion;
    ssLeaf << leafScript;

    CSHA256 shaLeaf;
    shaLeaf.Write(tagHash, 32);
    shaLeaf.Write(tagHash, 32);
    if (ssLeaf.size() > 0) {
        shaLeaf.Write((const unsigned char*)&ssLeaf[0], ssLeaf.size());
    }
    uint256 merkleRoot;
    shaLeaf.Finalize(merkleRoot.begin());

    bool parity = false;
    CPubKey outputPubKey = internalXOnly.CreateTapTweak(&merkleRoot, &parity);
    BOOST_REQUIRE(outputPubKey.IsValid());
    CXOnlyPubKey outputXOnly(outputPubKey);

    CScript taprootScriptPubKey = CScript() << OP_1 << ToByteVector(outputXOnly);

    // Control block: [leafVersion | parityBit] || 32-byte internalXOnly
    std::vector<unsigned char> controlBlock(33);
    controlBlock[0] = leafVersion | (parity ? 1 : 0);
    memcpy(controlBlock.data() + 1, internalXOnly.begin(), 32);

    std::vector<unsigned char> scriptBytes(leafScript.begin(), leafScript.end());
    const unsigned int tapFlags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_TAPROOT;

    // 1. Valid MESCAL Tapscript spend with valid secret and satisfied nLockTime -> MUST PASS
    {
        CMutableTransaction txSpend;
        txSpend.nVersion = 1;
        txSpend.nLockTime = tapLockHeight;
        txSpend.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txSpend.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        txSpend.vin[0].scriptSig = CScript() << CScriptNum(expectedSecret) << scriptBytes << controlBlock;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txSpend);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, taprootScriptPubKey)});
        BOOST_CHECK(VerifyScript(txSpend.vin[0].scriptSig, taprootScriptPubKey, tapFlags,
                                 TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror));
    }

    // 2. Pre-expiry inside Tapscript (tx.nLockTime < tapLockHeight) -> MUST FAIL with SCRIPT_ERR_UNSATISFIED_LOCKTIME
    {
        CMutableTransaction txTooEarly;
        txTooEarly.nVersion = 1;
        txTooEarly.nLockTime = tapLockHeight - 1;
        txTooEarly.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txTooEarly.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        txTooEarly.vin[0].scriptSig = CScript() << CScriptNum(expectedSecret) << scriptBytes << controlBlock;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txTooEarly);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, taprootScriptPubKey)});
        BOOST_CHECK(!VerifyScript(txTooEarly.vin[0].scriptSig, taprootScriptPubKey, tapFlags,
                                  TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_UNSATISFIED_LOCKTIME);
    }

    // 3. Wrong secret inside Tapscript -> MUST FAIL with SCRIPT_ERR_EVAL_FALSE
    {
        CMutableTransaction txWrongSecret;
        txWrongSecret.nVersion = 1;
        txWrongSecret.nLockTime = tapLockHeight;
        txWrongSecret.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txWrongSecret.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        txWrongSecret.vin[0].scriptSig = CScript() << CScriptNum(expectedSecret + 1) << scriptBytes << controlBlock;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txWrongSecret);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, taprootScriptPubKey)});
        BOOST_CHECK(!VerifyScript(txWrongSecret.vin[0].scriptSig, taprootScriptPubKey, tapFlags,
                                  TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_EVAL_FALSE);
    }

    // 4. Tampered control block (flipped parity bit) -> MUST FAIL with SCRIPT_ERR_EVAL_FALSE
    {
        std::vector<unsigned char> badControlBlock = controlBlock;
        badControlBlock[0] ^= 0x01; // flip parity bit

        CMutableTransaction txBadControl;
        txBadControl.nVersion = 1;
        txBadControl.nLockTime = tapLockHeight;
        txBadControl.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txBadControl.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        txBadControl.vin[0].scriptSig = CScript() << CScriptNum(expectedSecret) << scriptBytes << badControlBlock;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txBadControl);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, taprootScriptPubKey)});
        BOOST_CHECK(!VerifyScript(txBadControl.vin[0].scriptSig, taprootScriptPubKey, tapFlags,
                                  TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_EVAL_FALSE);
    }

    // 5. Unsupported TapLeaf version (0xc2 instead of 0xc0) -> MUST FAIL with SCRIPT_ERR_BAD_OPCODE
    {
        std::vector<unsigned char> badLeafVerControl = controlBlock;
        badLeafVerControl[0] = 0xc2 | (parity ? 1 : 0);

        CMutableTransaction txBadLeafVer;
        txBadLeafVer.nVersion = 1;
        txBadLeafVer.nLockTime = tapLockHeight;
        txBadLeafVer.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txBadLeafVer.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        txBadLeafVer.vin[0].scriptSig = CScript() << CScriptNum(expectedSecret) << scriptBytes << badLeafVerControl;

        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txBadLeafVer);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, taprootScriptPubKey)});
        BOOST_CHECK(!VerifyScript(txBadLeafVer.vin[0].scriptSig, taprootScriptPubKey, tapFlags,
                                  TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_BAD_OPCODE);
    }
}

BOOST_AUTO_TEST_SUITE_END()
