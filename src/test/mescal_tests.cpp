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

    // 3. Test HTLC hash160 (htlc-equal: OP_HASH160 <20B> OP_EQUAL) & check-signature-verification (verify: false -> OP_CHECKSIG)
    // alongside legacy hash160 (<val> OP_HASH160) & check-signature-verification (verify: true -> OP_CHECKSIGVERIFY)
    std::string htlcEnterpriseJson = strprintf(R"({
        "basic": {
            "SecretLock": {
                "role": "hash160",
                "mode": "htlc-equal",
                "inputs": [{"name": "hash160", "type": "hash160", "value": "%s"}]
            },
            "ClaimSig": {
                "role": "check-signature-verification",
                "verify": false,
                "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]
            },
            "RefundTime": {
                "role": "lock-time",
                "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 1500}]
            },
            "RefundSigVerify": {
                "role": "check-signature-verification",
                "verify": true,
                "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]
            },
            "LegacyHash": {
                "role": "hash160",
                "inputs": [{"name": "String-or-Number-to-Hash", "type": "string-or-number", "value": "deadbeef"}]
            }
        },
        "condition": {
            "HTLCFlow": {
                "role": "if-condition",
                "expressions": [{"type": "basic", "name": "SecretLock"}],
                "true": [{"type": "basic", "name": "ClaimSig"}],
                "false": [
                    {"type": "basic", "name": "RefundTime"},
                    {"type": "basic", "name": "RefundSigVerify"},
                    {"type": "basic", "name": "LegacyHash"}
                ]
            }
        },
        "contract": {
            "EnterpriseHTLC": {
                "actions": [{"type": "condition", "name": "HTLCFlow"}]
            }
        },
        "active_contract": "EnterpriseHTLC"
    })", pkh1Hex, pk1Hex, pk2Hex);

    std::string htlcErr;
    CScript compiledEnterprise = CMescal::Compile(htlcEnterpriseJson, htlcErr);
    BOOST_CHECK_MESSAGE(htlcErr.empty(), htlcErr);

    CScript expectedEnterprise;
    expectedEnterprise << OP_HASH160 << ParseHex(pkh1Hex) << OP_EQUAL
                       << OP_IF
                       << ParseHex(pk1Hex) << OP_CHECKSIG
                       << OP_ELSE
                       << CScriptNum(1500) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                       << ParseHex(pk2Hex) << OP_CHECKSIGVERIFY
                       << ParseHex("deadbeef") << OP_HASH160
                       << OP_ENDIF;
    BOOST_CHECK(compiledEnterprise == expectedEnterprise);

    UniValue decompiledEnterprise = CMescal::Decompile(compiledEnterprise, htlcErr);
    BOOST_CHECK_MESSAGE(htlcErr.empty(), htlcErr);
    CScript recompiledEnterprise = CMescal::Compile(decompiledEnterprise.write(), htlcErr);
    BOOST_CHECK_MESSAGE(htlcErr.empty(), htlcErr);
    BOOST_CHECK(compiledEnterprise == recompiledEnterprise);
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

    // 6. MESCAL Tapscript leaf with 33-byte ECDSA pubkey + DER signature (equalverify-checksig + lock-time)
    {
        CKey ecdsaKey;
        ecdsaKey.MakeNewKey(true);
        CPubKey ecdsaPubKey = ecdsaKey.GetPubKey();
        BOOST_REQUIRE_EQUAL(ecdsaPubKey.size(), 33U);
        std::string pubKeyHashHex = HexStr(ecdsaPubKey.GetID());

        std::string sigLeafJson = strprintf(R"({
            "basic": {
                "CheckCLTV": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %d}]},
                "CheckRecipientSig": {"role": "equalverify-checksig", "inputs": [{"name": "Recipient-PubkeyHash", "type": "pubkeyhash", "value": "%s"}]}
            },
            "contract": {
                "TapLeafECDSA": {
                    "actions": [
                        {"type": "basic", "name": "CheckCLTV"},
                        {"type": "basic", "name": "CheckRecipientSig"}
                    ]
                }
            },
            "active_contract": "TapLeafECDSA"
        })", (int)tapLockHeight, pubKeyHashHex);

        std::string sigErr;
        CScript sigLeafScript = CMescal::Compile(sigLeafJson, sigErr);
        BOOST_REQUIRE_MESSAGE(sigErr.empty(), sigErr);

        CDataStream ssSigLeaf(SER_NETWORK, PROTOCOL_VERSION);
        ssSigLeaf << leafVersion;
        ssSigLeaf << sigLeafScript;

        CSHA256 shaSigLeaf;
        shaSigLeaf.Write(tagHash, 32);
        shaSigLeaf.Write(tagHash, 32);
        shaSigLeaf.Write((const unsigned char*)&ssSigLeaf[0], ssSigLeaf.size());
        uint256 sigMerkleRoot;
        shaSigLeaf.Finalize(sigMerkleRoot.begin());

        bool sigParity = false;
        CPubKey sigOutputPubKey = internalXOnly.CreateTapTweak(&sigMerkleRoot, &sigParity);
        BOOST_REQUIRE(sigOutputPubKey.IsValid());
        CXOnlyPubKey sigOutputXOnly(sigOutputPubKey);
        CScript sigTaprootSpk = CScript() << OP_1 << ToByteVector(sigOutputXOnly);

        std::vector<unsigned char> sigControlBlock(33);
        sigControlBlock[0] = leafVersion | (sigParity ? 1 : 0);
        memcpy(sigControlBlock.data() + 1, internalXOnly.begin(), 32);
        std::vector<unsigned char> sigLeafBytes(sigLeafScript.begin(), sigLeafScript.end());

        CMutableTransaction txEcdsaTap;
        txEcdsaTap.nVersion = 1;
        txEcdsaTap.nLockTime = tapLockHeight;
        txEcdsaTap.vin.push_back(CTxIn(COutPoint(uint256S("03"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txEcdsaTap.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));

        uint256 sighash = SignatureHash(sigLeafScript, txEcdsaTap, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> vchDerSig;
        BOOST_REQUIRE(ecdsaKey.Sign(sighash, vchDerSig));
        vchDerSig.push_back((unsigned char)SIGHASH_ALL);

        txEcdsaTap.vin[0].scriptSig = CScript() << vchDerSig << ToByteVector(ecdsaPubKey) << sigLeafBytes << sigControlBlock;

        const unsigned int strictTapFlags = tapFlags | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC;
        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txEcdsaTap);
        PrecomputedTransactionData txdataVerify(txConst);
        txdataVerify.Init(txConst, {CTxOut(5 * COIN, sigTaprootSpk)});
        BOOST_CHECK_MESSAGE(VerifyScript(txEcdsaTap.vin[0].scriptSig, sigTaprootSpk, strictTapFlags,
                                         TransactionSignatureChecker(&txConst, 0, 5 * COIN, txdataVerify), &serror),
                            ScriptErrorString(serror));

        // Corrupt signature byte -> MUST FAIL
        std::vector<unsigned char> badDerSig = vchDerSig;
        badDerSig[badDerSig.size() / 2] ^= 0x01;
        txEcdsaTap.vin[0].scriptSig = CScript() << badDerSig << ToByteVector(ecdsaPubKey) << sigLeafBytes << sigControlBlock;
        CTransaction txBadConst(txEcdsaTap);
        PrecomputedTransactionData txdataBad(txBadConst);
        txdataBad.Init(txBadConst, {CTxOut(5 * COIN, sigTaprootSpk)});
        BOOST_CHECK(!VerifyScript(txEcdsaTap.vin[0].scriptSig, sigTaprootSpk, strictTapFlags,
                                  TransactionSignatureChecker(&txBadConst, 0, 5 * COIN, txdataBad), &serror));
    }
}

BOOST_AUTO_TEST_SUITE_END()
