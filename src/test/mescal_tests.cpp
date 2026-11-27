// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "adam.h"
#include "chainparams.h"
#include "coins.h"
#include "core_io.h"
#include "hash.h"
#include "key.h"
#include "pubkey.h"
#include "primitives/transaction.h"
#include "policy/policy.h"
#include "script/interpreter.h"
#include "script/mescal.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "test/test_kristatech.h"
#include "utilstrencodings.h"

#include <secp256k1.h>
#include <secp256k1_extrakeys.h>
#include <secp256k1_schnorrsig.h>

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

BOOST_AUTO_TEST_CASE(mescal_hash_equalverify_and_locktime_bounds_test)
{
    CKey claimKey, refundKey;
    claimKey.MakeNewKey(true);
    refundKey.MakeNewKey(true);
    const std::string claimPkHex = HexStr(claimKey.GetPubKey());
    const std::string refundPkHex = HexStr(refundKey.GetPubKey());

    const std::string secretStr = "krista-htlc-preimage-test";
    const std::vector<unsigned char> preimage(secretStr.begin(), secretStr.end());
    const std::vector<unsigned char> wrongPreimage = {'w', 'r', 'o', 'n', 'g'};
    const uint160 secretHash = Hash160(preimage);
    const std::string hashHex = HexStr(std::vector<unsigned char>(secretHash.begin(), secretHash.end()));

    // 1. Standalone Hash-Lock (hash-equalverify) compile, decompile, Solver, and script execution
    {
        std::string jsonStr = strprintf(R"({
            "basic": {
                "Hash-Check": {
                    "role": "hash160",
                    "mode": "hash-equalverify",
                    "inputs": [{"name": "hash160", "type": "hash160", "value": "%s"}]
                },
                "Recipient-Sig": {
                    "role": "check-signature-verification",
                    "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]
                }
            },
            "contract": {
                "HashLockedClaim": {
                    "actions": [
                        {"type": "basic", "name": "Hash-Check"},
                        {"type": "basic", "name": "Recipient-Sig"}
                    ]
                }
            },
            "active_contract": "HashLockedClaim"
        })", hashHex, claimPkHex);

        std::string err;
        CScript script = CMescal::Compile(jsonStr, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);

        CScript expected;
        expected << OP_HASH160 << ParseHex(hashHex) << OP_EQUALVERIFY << ParseHex(claimPkHex) << OP_CHECKSIGVERIFY;
        BOOST_CHECK(script == expected);

        txnouttype whichType = TX_NONSTANDARD;
        std::vector<std::vector<unsigned char>> vSolutions;
        BOOST_CHECK(Solver(script, whichType, vSolutions));
        BOOST_CHECK_EQUAL(whichType, TX_MESCAL_CONTRACT);

        UniValue decomp = CMescal::Decompile(script, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);
        CScript recompiled = CMescal::Compile(decomp.write(), err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);
        BOOST_CHECK(script == recompiled);

        CMutableTransaction txSpend;
        txSpend.nVersion = 1;
        txSpend.vin.push_back(CTxIn(COutPoint(uint256S("01"), 0)));
        txSpend.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 sighash = SignatureHash(script, txSpend, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> vchSig;
        BOOST_REQUIRE(claimKey.Sign(sighash, vchSig));
        vchSig.push_back((unsigned char)SIGHASH_ALL);

        const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        ScriptError serror = SCRIPT_ERR_OK;
        CTransaction txConst(txSpend);

        txSpend.vin[0].scriptSig = CScript() << OP_1 << vchSig << preimage;
        txConst = CTransaction(txSpend);
        BOOST_CHECK_MESSAGE(VerifyScript(txSpend.vin[0].scriptSig, script, flags,
                                         TransactionSignatureChecker(&txConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));

        txSpend.vin[0].scriptSig = CScript() << OP_1 << vchSig << wrongPreimage;
        txConst = CTransaction(txSpend);
        BOOST_CHECK(!VerifyScript(txSpend.vin[0].scriptSig, script, flags,
                                  TransactionSignatureChecker(&txConst, 0, 5 * COIN), &serror));
    }

    // 2. HTLC (htlc-equal inside if-condition expressions) TRUE and FALSE branches
    {
        const int64_t lockExpiry = 1500;
        std::string htlcJson = strprintf(R"({
            "basic": {
                "SecretLock": {
                    "role": "hash160",
                    "mode": "htlc-equal",
                    "inputs": [{"name": "hash160", "type": "hash160", "value": "%s"}]
                },
                "ClaimSig": {
                    "role": "check-signature-verification",
                    "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]
                },
                "RefundLock": {
                    "role": "lock-time",
                    "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %d}]
                },
                "RefundSig": {
                    "role": "check-signature-verification",
                    "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]
                }
            },
            "condition": {
                "HTLCCond": {
                    "role": "if-condition",
                    "expressions": [{"type": "basic", "name": "SecretLock"}],
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
        })", hashHex, claimPkHex, (int)lockExpiry, refundPkHex);

        std::string err;
        CScript htlcScript = CMescal::Compile(htlcJson, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);

        const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY;
        ScriptError serror = SCRIPT_ERR_OK;

        // TRUE branch: preimage + claimKey signature
        {
            CMutableTransaction txTrue;
            txTrue.nVersion = 1;
            txTrue.vin.push_back(CTxIn(COutPoint(uint256S("02"), 0)));
            txTrue.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
            uint256 sighash = SignatureHash(htlcScript, txTrue, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
            std::vector<unsigned char> sigTrue;
            BOOST_REQUIRE(claimKey.Sign(sighash, sigTrue));
            sigTrue.push_back((unsigned char)SIGHASH_ALL);
            txTrue.vin[0].scriptSig = CScript() << OP_1 << sigTrue << preimage;

            CTransaction txConst(txTrue);
            BOOST_CHECK_MESSAGE(VerifyScript(txTrue.vin[0].scriptSig, htlcScript, flags,
                                             TransactionSignatureChecker(&txConst, 0, 5 * COIN), &serror),
                                ScriptErrorString(serror));
        }

        // FALSE branch: OP_0 + refundKey signature after lockExpiry
        {
            CMutableTransaction txFalse;
            txFalse.nVersion = 1;
            txFalse.nLockTime = lockExpiry;
            txFalse.vin.push_back(CTxIn(COutPoint(uint256S("03"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
            txFalse.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
            uint256 sighash = SignatureHash(htlcScript, txFalse, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
            std::vector<unsigned char> sigFalse;
            BOOST_REQUIRE(refundKey.Sign(sighash, sigFalse));
            sigFalse.push_back((unsigned char)SIGHASH_ALL);
            txFalse.vin[0].scriptSig = CScript() << OP_1 << sigFalse << OP_0;

            CTransaction txConst(txFalse);
            BOOST_CHECK_MESSAGE(VerifyScript(txFalse.vin[0].scriptSig, htlcScript, flags,
                                             TransactionSignatureChecker(&txConst, 0, 5 * COIN), &serror),
                                ScriptErrorString(serror));
        }
    }

    // 3. Reject invalid hash160 lengths for htlc-equal and hash-equalverify
    for (const char* mode : {"htlc-equal", "hash-equalverify"}) {
        std::string badHashJson = strprintf(R"({
            "basic": {
                "H": {"role": "hash160", "mode": "%s", "inputs": [{"name": "hash160", "type": "hash160", "value": "deadbeef"}]}
            },
            "contract": {"Bad": {"actions": [{"type": "basic", "name": "H"}]}},
            "active_contract": "Bad"
        })", mode);
        std::string err;
        CScript badScript = CMescal::Compile(badHashJson, err);
        BOOST_CHECK(!err.empty());
        BOOST_CHECK(badScript.empty());
    }

    // 4. Reject out-of-range and non-numeric lock-time values
    for (const std::string& valToken : {std::string("-1"), std::string("4294967296"), std::string("\"2026-12-31\"")}) {
        std::string badLockJson = strprintf(R"({
            "basic": {
                "L": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %s}]}
            },
            "contract": {"BadLock": {"actions": [{"type": "basic", "name": "L"}]}},
            "active_contract": "BadLock"
        })", valToken);
        std::string err;
        CScript badScript = CMescal::Compile(badLockJson, err);
        BOOST_CHECK(!err.empty());
        BOOST_CHECK(badScript.empty());
    }
}

BOOST_AUTO_TEST_CASE(mescal_faz2_h04_compiler_and_branch_reachability_test)
{
    std::vector<CKey> keys(5);
    std::vector<std::string> pkHex(5);
    for (size_t i = 0; i < 5; ++i) {
        keys[i].MakeNewKey(true);
        pkHex[i] = HexStr(keys[i].GetPubKey());
    }

    const unsigned int flags = STANDARD_SCRIPT_VERIFY_FLAGS;
    ScriptError serror = SCRIPT_ERR_OK;

    // 1. IF-condition defaulting expr checksig to verify=false (OP_CHECKSIG), TRUE=[number 1],
    //    FALSE=[lock-time, 2-of-3 multisig] reachable both WITH and WITHOUT primary key.
    {
        const uint32_t lockHeight = 150;
        std::string vaultJson = strprintf(R"({
            "basic": {
                "PrimarySig": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]},
                "PassOne": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": 1}]},
                "Delay": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %u}]},
                "BackupMulti": {"role": "multi-signature", "inputs": [
                    {"name": "m", "type": "number", "value": 2},
                    {"name": "n", "type": "number", "value": 3},
                    {"name": "Signatures", "type": "array", "value": ["%s", "%s", "%s"]}
                ]}
            },
            "condition": {
                "Vault": {
                    "role": "if-condition",
                    "expressions": [{"type": "basic", "name": "PrimarySig"}],
                    "true": [{"type": "basic", "name": "PassOne"}],
                    "false": [{"type": "basic", "name": "Delay"}, {"type": "basic", "name": "BackupMulti"}]
                }
            },
            "contract": {"Vault": {"actions": [{"type": "condition", "name": "Vault"}]}},
            "active_contract": "Vault"
        })", pkHex[0], lockHeight, pkHex[1], pkHex[2], pkHex[3]);

        std::string err;
        CScript vaultScript = CMescal::Compile(vaultJson, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);
        BOOST_REQUIRE(!vaultScript.empty());

        // Verify OP_CHECKSIG precedes OP_IF (never OP_CHECKSIGVERIFY)
        CScript::const_iterator pc = vaultScript.begin();
        opcodetype prevOp = OP_INVALIDOPCODE, curOp = OP_INVALIDOPCODE;
        bool checkedExprChecksig = false;
        while (pc < vaultScript.end()) {
            BOOST_REQUIRE(vaultScript.GetOp(pc, curOp));
            if (curOp == OP_IF) {
                BOOST_CHECK_EQUAL(prevOp, OP_CHECKSIG);
                checkedExprChecksig = true;
                break;
            }
            prevOp = curOp;
        }
        BOOST_CHECK(checkedExprChecksig);

        // TRUE spend (primary key immediate, nLockTime = 0)
        CMutableTransaction txTrue;
        txTrue.vin.push_back(CTxIn(COutPoint(uint256S("11"), 0), CScript(), CTxIn::SEQUENCE_FINAL));
        txTrue.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 sighashTrue = SignatureHash(vaultScript, txTrue, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> sig0;
        BOOST_REQUIRE(keys[0].Sign(sighashTrue, sig0));
        sig0.push_back((unsigned char)SIGHASH_ALL);
        txTrue.vin[0].scriptSig = CScript() << sig0;
        CTransaction txTrueConst(txTrue);
        BOOST_CHECK_MESSAGE(VerifyScript(txTrue.vin[0].scriptSig, vaultScript, flags,
                                         TransactionSignatureChecker(&txTrueConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));

        // FALSE spend before lock-time must fail CLTV
        CMutableTransaction txEarly;
        txEarly.nLockTime = lockHeight - 1;
        txEarly.vin.push_back(CTxIn(COutPoint(uint256S("12"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txEarly.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 sighashEarly = SignatureHash(vaultScript, txEarly, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> eSig1, eSig2;
        BOOST_REQUIRE(keys[1].Sign(sighashEarly, eSig1));
        BOOST_REQUIRE(keys[2].Sign(sighashEarly, eSig2));
        eSig1.push_back((unsigned char)SIGHASH_ALL);
        eSig2.push_back((unsigned char)SIGHASH_ALL);
        txEarly.vin[0].scriptSig = CScript() << OP_0 << eSig1 << eSig2 << OP_0;
        CTransaction txEarlyConst(txEarly);
        BOOST_CHECK(!VerifyScript(txEarly.vin[0].scriptSig, vaultScript, flags,
                                  TransactionSignatureChecker(&txEarlyConst, 0, 5 * COIN), &serror));

        // FALSE recovery spend at lockHeight (primary key missing; uses OP_0 for OP_IF FALSE branch)
        CMutableTransaction txRecovery;
        txRecovery.nLockTime = lockHeight;
        txRecovery.vin.push_back(CTxIn(COutPoint(uint256S("13"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txRecovery.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 sighashRec = SignatureHash(vaultScript, txRecovery, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> rSig1, rSig2;
        BOOST_REQUIRE(keys[1].Sign(sighashRec, rSig1));
        BOOST_REQUIRE(keys[2].Sign(sighashRec, rSig2));
        rSig1.push_back((unsigned char)SIGHASH_ALL);
        rSig2.push_back((unsigned char)SIGHASH_ALL);
        txRecovery.vin[0].scriptSig = CScript() << OP_0 << rSig1 << rSig2 << OP_0;
        CTransaction txRecoveryConst(txRecovery);
        BOOST_CHECK_MESSAGE(VerifyScript(txRecovery.vin[0].scriptSig, vaultScript, flags,
                                         TransactionSignatureChecker(&txRecoveryConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));
    }

    // 2. Dead Man's Switch pattern: expr=Heir checksig, TRUE=[lock-time, number 1], FALSE=[Owner checksigverify]
    {
        const uint32_t lockHeight = 250;
        std::string dmsJson = strprintf(R"({
            "basic": {
                "Heir": {"role": "check-signature-verification", "verify": false, "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]},
                "Wait": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %u}]},
                "Ok": {"role": "number", "inputs": [{"name": "Value", "type": "number", "value": 1}]},
                "Owner": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]}
            },
            "condition": {
                "DMS": {
                    "role": "if-condition",
                    "expressions": [{"type": "basic", "name": "Heir"}],
                    "true": [{"type": "basic", "name": "Wait"}, {"type": "basic", "name": "Ok"}],
                    "false": [{"type": "basic", "name": "Owner"}]
                }
            },
            "contract": {"DMS": {"actions": [{"type": "condition", "name": "DMS"}]}},
            "active_contract": "DMS"
        })", pkHex[0], lockHeight, pkHex[1]);

        std::string err;
        CScript dmsScript = CMescal::Compile(dmsJson, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);

        // Owner immediate cancel (FALSE branch, nLockTime=0)
        CMutableTransaction txOwner;
        txOwner.vin.push_back(CTxIn(COutPoint(uint256S("21"), 0), CScript(), CTxIn::SEQUENCE_FINAL));
        txOwner.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 hOwner = SignatureHash(dmsScript, txOwner, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> ownerSig;
        BOOST_REQUIRE(keys[1].Sign(hOwner, ownerSig));
        ownerSig.push_back((unsigned char)SIGHASH_ALL);
        txOwner.vin[0].scriptSig = CScript() << OP_1 << ownerSig << OP_0;
        CTransaction txOwnerConst(txOwner);
        BOOST_CHECK_MESSAGE(VerifyScript(txOwner.vin[0].scriptSig, dmsScript, flags,
                                         TransactionSignatureChecker(&txOwnerConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));

        // Heir before lockHeight must fail
        CMutableTransaction txHeirEarly;
        txHeirEarly.nLockTime = lockHeight - 1;
        txHeirEarly.vin.push_back(CTxIn(COutPoint(uint256S("22"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txHeirEarly.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 hEarly = SignatureHash(dmsScript, txHeirEarly, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> heirSigEarly;
        BOOST_REQUIRE(keys[0].Sign(hEarly, heirSigEarly));
        heirSigEarly.push_back((unsigned char)SIGHASH_ALL);
        txHeirEarly.vin[0].scriptSig = CScript() << heirSigEarly;
        CTransaction txHeirEarlyConst(txHeirEarly);
        BOOST_CHECK(!VerifyScript(txHeirEarly.vin[0].scriptSig, dmsScript, flags,
                                  TransactionSignatureChecker(&txHeirEarlyConst, 0, 5 * COIN), &serror));

        // Heir at lockHeight succeeds
        CMutableTransaction txHeirOk;
        txHeirOk.nLockTime = lockHeight;
        txHeirOk.vin.push_back(CTxIn(COutPoint(uint256S("23"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txHeirOk.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 hHeir = SignatureHash(dmsScript, txHeirOk, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> heirSig;
        BOOST_REQUIRE(keys[0].Sign(hHeir, heirSig));
        heirSig.push_back((unsigned char)SIGHASH_ALL);
        txHeirOk.vin[0].scriptSig = CScript() << heirSig;
        CTransaction txHeirOkConst(txHeirOk);
        BOOST_CHECK_MESSAGE(VerifyScript(txHeirOk.vin[0].scriptSig, dmsScript, flags,
                                         TransactionSignatureChecker(&txHeirOkConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));
    }

    // 3. HTLC with lock-time inside FALSE branch (immediate claim allowed; refund locked until expiry)
    {
        const std::string preimageStr = "h04-atomic-secret";
        const std::vector<unsigned char> preimageVec(preimageStr.begin(), preimageStr.end());
        uint160 h160 = Hash160(preimageVec);
        std::vector<unsigned char> h160Vec(h160.begin(), h160.end());
        const uint32_t lockHeight = 350;

        std::string htlcJson = strprintf(R"({
            "basic": {
                "Hash": {"role": "hash160", "mode": "htlc-equal", "inputs": [{"name": "hash160", "type": "hash160", "value": "%s"}]},
                "Recip": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]},
                "Wait": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": %u}]},
                "Sender": {"role": "check-signature-verification", "inputs": [{"name": "Pubkey", "type": "pubkey", "value": "%s"}]}
            },
            "condition": {
                "HTLC": {
                    "role": "if-condition",
                    "expressions": [{"type": "basic", "name": "Hash"}],
                    "true": [{"type": "basic", "name": "Recip"}],
                    "false": [{"type": "basic", "name": "Wait"}, {"type": "basic", "name": "Sender"}]
                }
            },
            "contract": {"HTLC": {"actions": [{"type": "condition", "name": "HTLC"}]}},
            "active_contract": "HTLC"
        })", HexStr(h160Vec), pkHex[0], lockHeight, pkHex[1]);

        std::string err;
        CScript htlcScript = CMescal::Compile(htlcJson, err);
        BOOST_REQUIRE_MESSAGE(err.empty(), err);

        // Recipient immediate claim with nLockTime = 0 (< 350) MUST succeed
        CMutableTransaction txClaim;
        txClaim.vin.push_back(CTxIn(COutPoint(uint256S("31"), 0), CScript(), CTxIn::SEQUENCE_FINAL));
        txClaim.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 hClaim = SignatureHash(htlcScript, txClaim, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> claimSig;
        BOOST_REQUIRE(keys[0].Sign(hClaim, claimSig));
        claimSig.push_back((unsigned char)SIGHASH_ALL);
        txClaim.vin[0].scriptSig = CScript() << OP_1 << claimSig << preimageVec;
        CTransaction txClaimConst(txClaim);
        BOOST_CHECK_MESSAGE(VerifyScript(txClaim.vin[0].scriptSig, htlcScript, flags,
                                         TransactionSignatureChecker(&txClaimConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));

        // Sender refund at 349 must fail; at 350 succeeds
        CMutableTransaction txRefund;
        txRefund.nLockTime = lockHeight - 1;
        txRefund.vin.push_back(CTxIn(COutPoint(uint256S("32"), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1));
        txRefund.vout.push_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        uint256 hRefundEarly = SignatureHash(htlcScript, txRefund, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        std::vector<unsigned char> refSig;
        BOOST_REQUIRE(keys[1].Sign(hRefundEarly, refSig));
        refSig.push_back((unsigned char)SIGHASH_ALL);
        txRefund.vin[0].scriptSig = CScript() << OP_1 << refSig << OP_0;
        CTransaction txRefundEarlyConst(txRefund);
        BOOST_CHECK(!VerifyScript(txRefund.vin[0].scriptSig, htlcScript, flags,
                                  TransactionSignatureChecker(&txRefundEarlyConst, 0, 5 * COIN), &serror));

        txRefund.nLockTime = lockHeight;
        uint256 hRefundOk = SignatureHash(htlcScript, txRefund, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
        refSig.clear();
        BOOST_REQUIRE(keys[1].Sign(hRefundOk, refSig));
        refSig.push_back((unsigned char)SIGHASH_ALL);
        txRefund.vin[0].scriptSig = CScript() << OP_1 << refSig << OP_0;
        CTransaction txRefundOkConst(txRefund);
        BOOST_CHECK_MESSAGE(VerifyScript(txRefund.vin[0].scriptSig, htlcScript, flags,
                                         TransactionSignatureChecker(&txRefundOkConst, 0, 5 * COIN), &serror),
                            ScriptErrorString(serror));
    }

    // 4. Reject malformed keys, m>n / n>20 / sigs!=n, out-of-range numbers, forbidden IF expressions, empty branches/actions
    const std::string offCurvePk = "03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237";
    const std::vector<std::string> invalidContracts = {
        // Explicit verify=true inside IF expression
        strprintf(R"({"basic":{"S":{"role":"check-signature-verification","verify":true,"inputs":[{"name":"Pubkey","type":"pubkey","value":"%s"}]},"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[{"type":"basic","name":"S"}],"true":[{"type":"basic","name":"O"}]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})", pkHex[0]),
        // Off-curve pubkey in check-signature-verification
        strprintf(R"({"basic":{"S":{"role":"check-signature-verification","inputs":[{"name":"Pubkey","type":"pubkey","value":"%s"}]}},"contract":{"X":{"actions":[{"type":"basic","name":"S"}]}},"active_contract":"X"})", offCurvePk),
        // Invalid pubkey hex / short pubkey
        R"({"basic":{"S":{"role":"check-signature-verification","inputs":[{"name":"Pubkey","type":"pubkey","value":"nothex00"}]}},"contract":{"X":{"actions":[{"type":"basic","name":"S"}]}},"active_contract":"X"})",
        // Invalid pubkeyhash in equalverify-checksig
        R"({"basic":{"P":{"role":"equalverify-checksig","inputs":[{"name":"PubkeyHash","type":"pubkeyhash","value":"abcd"}]}},"contract":{"X":{"actions":[{"type":"basic","name":"P"}]}},"active_contract":"X"})",
        // Multisig m > n (3-of-2)
        strprintf(R"({"basic":{"M":{"role":"multi-signature","inputs":[{"name":"m","type":"number","value":3},{"name":"n","type":"number","value":2},{"name":"Signatures","type":"array","value":["%s","%s"]}]}},"contract":{"X":{"actions":[{"type":"basic","name":"M"}]}},"active_contract":"X"})", pkHex[0], pkHex[1]),
        // Multisig Signatures length mismatch (n=3, 2 keys)
        strprintf(R"({"basic":{"M":{"role":"multi-signature","inputs":[{"name":"m","type":"number","value":2},{"name":"n","type":"number","value":3},{"name":"Signatures","type":"array","value":["%s","%s"]}]}},"contract":{"X":{"actions":[{"type":"basic","name":"M"}]}},"active_contract":"X"})", pkHex[0], pkHex[1]),
        // Number outside 4-byte CScriptNum range
        R"({"basic":{"N":{"role":"number","inputs":[{"name":"Value","type":"number","value":2147483648}]}},"contract":{"X":{"actions":[{"type":"basic","name":"N"}]}},"active_contract":"X"})",
        R"({"basic":{"N":{"role":"number","inputs":[{"name":"Value","type": "number","value":-2147483648}]}},"contract":{"X":{"actions":[{"type":"basic","name":"N"}]}},"active_contract":"X"})",
        // Non-boolean roles inside IF expressions: lock-time, drop, hash-equalverify
        R"({"basic":{"L":{"role":"lock-time","inputs":[{"name":"Lock-Until","type":"timestamp-or-block-height","value":100}]},"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[{"type":"basic","name":"L"}],"true":[{"type":"basic","name":"O"}]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})",
        R"({"basic":{"D":{"role":"drop"},"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[{"type":"basic","name":"D"}],"true":[{"type":"basic","name":"O"}]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})",
        R"({"basic":{"H":{"role":"hash160","mode":"hash-equalverify","inputs":[{"name":"hash160","type":"hash160","value":"b5a9c9f285d893ce71ab9de8f5c09d765ee982ba"}]},"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[{"type":"basic","name":"H"}],"true":[{"type":"basic","name":"O"}]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})",
        // Empty actions, empty expressions, both empty true+false branches
        R"({"contract":{"X":{"actions":[]}},"active_contract":"X"})",
        R"({"basic":{"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[],"true":[{"type":"basic","name":"O"}]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})",
        R"({"basic":{"O":{"role":"number","inputs":[{"name":"Value","type":"number","value":1}]}},"condition":{"C":{"role":"if-condition","expressions":[{"type":"basic","name":"O"}],"true":[],"false":[]}},"contract":{"X":{"actions":[{"type":"condition","name":"C"}]}},"active_contract":"X"})"
    };

    for (const std::string& badJson : invalidContracts) {
        std::string err;
        CScript badScript = CMescal::Compile(badJson, err);
        BOOST_CHECK_MESSAGE(!err.empty(), "Expected error for: " + badJson);
        BOOST_CHECK_MESSAGE(badScript.empty(), "Expected empty script for: " + badJson);
    }

    // Valid max 4-byte CScriptNum (2147483647) compiles cleanly
    {
        std::string err;
        CScript okScript = CMescal::Compile(R"({"basic":{"N":{"role":"number","inputs":[{"name":"Value","type":"number","value":2147483647}]}},"contract":{"X":{"actions":[{"type":"basic","name":"N"}]}},"active_contract":"X"})", err);
        BOOST_CHECK_MESSAGE(err.empty(), err);
        BOOST_CHECK(!okScript.empty());
    }
}

namespace {
class CodesepProbeChecker : public TapscriptSignatureChecker {
public:
    using TapscriptSignatureChecker::TapscriptSignatureChecker;
    mutable std::vector<uint32_t> observedCodesepPositions;
    bool CheckSig(const std::vector<unsigned char>& vchSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion) const override {
        return CheckSig(vchSig, vchPubKey, scriptCode, sigversion, SCRIPT_VERIFY_NONE);
    }
    bool CheckSig(const std::vector<unsigned char>& vchSig, const std::vector<unsigned char>& vchPubKey, const CScript& scriptCode, SigVersion sigversion, unsigned int flags) const override {
        if (sigversion == SIGVERSION_TAPSCRIPT && vchPubKey.size() == 32 && !vchSig.empty()) {
            observedCodesepPositions.push_back(GetCodesepPos());
            return true;
        }
        return TapscriptSignatureChecker::CheckSig(vchSig, vchPubKey, scriptCode, sigversion, flags);
    }
};
} // namespace

BOOST_AUTO_TEST_CASE(tapscript_bip342_hardening_test)
{
    ECCVerifyHandle handle;
    CKey ecdsaKey;
    ecdsaKey.MakeNewKey(true);
    std::vector<unsigned char> xonlyP(32, 0x07);
    CXOnlyPubKey internalXOnly(xonlyP.data(), 32);
    uint8_t leafVersion = 0xc0;

    auto makeTaprootForLeaf = [&](const CScript& leafScript, std::vector<unsigned char>& leafBytesOut, std::vector<unsigned char>& ctrlOut, CScript& spkOut) {
        leafBytesOut.assign(leafScript.begin(), leafScript.end());
        unsigned char tagHash[32];
        CSHA256 shaTag;
        shaTag.Write((const unsigned char*)"TapLeaf", 7);
        shaTag.Finalize(tagHash);

        CDataStream ssLeaf(SER_NETWORK, PROTOCOL_VERSION);
        ssLeaf << leafVersion << leafScript;
        CSHA256 shaLeaf;
        shaLeaf.Write(tagHash, 32);
        shaLeaf.Write(tagHash, 32);
        shaLeaf.Write((const unsigned char*)&ssLeaf[0], ssLeaf.size());
        uint256 merkleRoot;
        shaLeaf.Finalize(merkleRoot.begin());

        bool parity = false;
        CPubKey outPk = internalXOnly.CreateTapTweak(&merkleRoot, &parity);
        BOOST_REQUIRE(outPk.IsValid());
        CXOnlyPubKey outXOnly(outPk);
        spkOut = CScript() << OP_1 << ToByteVector(outXOnly);
        ctrlOut.assign(33, 0);
        ctrlOut[0] = leafVersion | (parity ? 1 : 0);
        memcpy(ctrlOut.data() + 1, internalXOnly.begin(), 32);
    };

    const unsigned int flags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_TAPROOT;
    ScriptError serror = SCRIPT_ERR_OK;

    // 1. M-07: Empty pubkey in Tapscript (OP_CHECKSIG and OP_CHECKSIGADD) must fail with SCRIPT_ERR_PUBKEYTYPE
    {
        CScript leafEmptyPk = CScript() << OP_DROP << OP_CHECKSIG;
        std::vector<unsigned char> leafBytes, ctrl;
        CScript spk;
        makeTaprootForLeaf(leafEmptyPk, leafBytes, ctrl, spk);

        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(uint256S("01"), 0));
        mtx.vin[0].scriptSig = CScript() << OP_0 << OP_0 << OP_0 << leafBytes << ctrl;
        mtx.vout.emplace_back(COIN, CScript() << OP_TRUE);
        CTransaction txConst(mtx);
        PrecomputedTransactionData precom(txConst);
        precom.Init(txConst, {CTxOut(COIN, spk)});
        BOOST_CHECK(!VerifyScript(mtx.vin[0].scriptSig, spk, flags,
                                  TransactionSignatureChecker(&txConst, 0, COIN, precom), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_PUBKEYTYPE);

        CScript leafCsaEmptyPk = CScript() << OP_0 << OP_SWAP << OP_0 << OP_CHECKSIGADD << OP_EQUAL;
        makeTaprootForLeaf(leafCsaEmptyPk, leafBytes, ctrl, spk);
        CMutableTransaction mtx2;
        mtx2.vin.emplace_back(COutPoint(uint256S("02"), 0));
        mtx2.vin[0].scriptSig = CScript() << OP_0 << OP_0 << leafBytes << ctrl;
        mtx2.vout.emplace_back(COIN, CScript() << OP_TRUE);
        CTransaction tx2Const(mtx2);
        PrecomputedTransactionData precom2(tx2Const);
        precom2.Init(tx2Const, {CTxOut(COIN, spk)});
        BOOST_CHECK(!VerifyScript(mtx2.vin[0].scriptSig, spk, flags,
                                  TransactionSignatureChecker(&tx2Const, 0, COIN, precom2), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_PUBKEYTYPE);
    }

    // 2. M-07: OP_CHECKSIGADD enforces CheckSignatureEncoding (SCRIPT_VERIFY_DERSIG) on 33-byte ECDSA pubkey
    {
        CScript leafCsaEcdsa = CScript() << OP_0 << ToByteVector(ecdsaKey.GetPubKey()) << OP_CHECKSIGADD << OP_0 << OP_EQUAL;
        std::vector<unsigned char> leafBytes, ctrl;
        CScript spk;
        makeTaprootForLeaf(leafCsaEcdsa, leafBytes, ctrl, spk);

        std::vector<unsigned char> nonDerSig{0xde, 0xad, 0xbe, 0xef, 0x01};
        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(uint256S("03"), 0));
        mtx.vin[0].scriptSig = CScript() << nonDerSig << leafBytes << ctrl;
        mtx.vout.emplace_back(COIN, CScript() << OP_TRUE);
        CTransaction txConst(mtx);
        PrecomputedTransactionData precom(txConst);
        precom.Init(txConst, {CTxOut(COIN, spk)});
        BOOST_CHECK(!VerifyScript(mtx.vin[0].scriptSig, spk, flags,
                                  TransactionSignatureChecker(&txConst, 0, COIN, precom), &serror));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_SIG_DER);
    }

    // 3. H-05: BIP342 validation weight budget (50 + scriptSig.size()) rejects excessive non-empty OP_CHECKSIGADD ops
    {
        std::vector<unsigned char> tinyNonEmptySig{0x01};
        std::vector<unsigned char> xonly32(32, 0x22);
        CScript leafManyOps = CScript() << OP_0 << tinyNonEmptySig << OP_0 << xonly32 << OP_CHECKSIGADD;
        // Build a compact script (all args pushed inside leaf so scriptSig stays small) that executes 4 non-empty OP_CHECKSIGADDs (~200 budget cost) while witness scriptSig is < 150 bytes
        CScript denseLeaf = CScript() << OP_0;
        for (int i = 0; i < 4; ++i) {
            denseLeaf << tinyNonEmptySig << OP_SWAP << xonly32 << OP_CHECKSIGADD;
        }
        denseLeaf << OP_DROP << OP_TRUE;
        std::vector<unsigned char> leafBytes, ctrl;
        CScript spk;
        makeTaprootForLeaf(denseLeaf, leafBytes, ctrl, spk);
        // Wait: denseLeaf is around 4*37 + 3 = ~151B, so scriptSig is ~186B => budget = 236 (< 4*50 if 5 non-empty ops!)
        denseLeaf = CScript() << OP_0;
        for (int i = 0; i < 6; ++i) {
            denseLeaf << tinyNonEmptySig << OP_SWAP << xonly32 << OP_CHECKSIGADD;
        }
        denseLeaf << OP_DROP << OP_TRUE;
        // 6*37 = 222B + 33B ctrl = 255B => budget 305 >= 300! With OP_DUP of one pubkey on stack, keep leaf small and trigger 400 cost vs ~120 budget:
        denseLeaf = CScript() << xonly32 << OP_0;
        // stack: [pk, acc]
        for (int i = 0; i < 5; ++i) {
            denseLeaf << tinyNonEmptySig << OP_SWAP << OP_2 << OP_PICK << OP_CHECKSIGADD; // costs 50 each; only ~8 bytes per loop in leaf!
        }
        denseLeaf << OP_2DROP << OP_TRUE;
        makeTaprootForLeaf(denseLeaf, leafBytes, ctrl, spk);
        CScript scriptSigBudget = CScript() << leafBytes << ctrl;
        BOOST_REQUIRE_LT(50 + (int)scriptSigBudget.size(), 5 * 50);

        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(uint256S("04"), 0));
        mtx.vin[0].scriptSig = scriptSigBudget;
        mtx.vout.emplace_back(COIN, CScript() << OP_TRUE);
        CTransaction txConst(mtx);
        PrecomputedTransactionData precom(txConst);
        precom.Init(txConst, {CTxOut(COIN, spk)});
        CodesepProbeChecker checker(&txConst, 0, COIN, precom, uint256(), 0xffffffff);
        std::vector<std::vector<unsigned char>> stack;
        BOOST_CHECK(!EvalScript(stack, denseLeaf, flags, checker, SIGVERSION_TAPSCRIPT, &serror, 50 + (int64_t)scriptSigBudget.size()));
        BOOST_CHECK_EQUAL(serror, SCRIPT_ERR_SIG_COUNT);
    }

    // 4. M-07: OP_CODESEPARATOR inside SIGVERSION_TAPSCRIPT updates codesep_pos to exact 0-based opcode position
    {
        std::vector<unsigned char> sig64(64, 0xaa);
        std::vector<unsigned char> xonly32(32, 0x33);
        CScript sepLeaf = CScript() << OP_NOP << OP_CODESEPARATOR << xonly32 << OP_CHECKSIGVERIFY
                                    << OP_NOP << OP_CODESEPARATOR << xonly32 << OP_CHECKSIG;
        CMutableTransaction mtx;
        mtx.vin.emplace_back(COutPoint(uint256S("05"), 0));
        mtx.vout.emplace_back(COIN, CScript() << OP_TRUE);
        CTransaction txConst(mtx);
        PrecomputedTransactionData precom(txConst);
        CodesepProbeChecker checker(&txConst, 0, COIN, precom, uint256S("77"), 0xffffffff);
        std::vector<std::vector<unsigned char>> stack{sig64, sig64};
        BOOST_CHECK(EvalScript(stack, sepLeaf, flags, checker, SIGVERSION_TAPSCRIPT, &serror, 1000));
        BOOST_REQUIRE_EQUAL(checker.observedCodesepPositions.size(), 2U);
        BOOST_CHECK_EQUAL(checker.observedCodesepPositions[0], 1U);
        BOOST_CHECK_EQUAL(checker.observedCodesepPositions[1], 5U);
    }
}

BOOST_AUTO_TEST_CASE(mescal_taproot_bip341_and_policy_phase2_test)
{
    ECCVerifyHandle handle;

    // 1. H-15: BIP 341 Strict Taproot Sighash (SCRIPT_VERIFY_TAPROOT_BIP341) vs Legacy Sighash
    {
        secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        BOOST_REQUIRE(ctx != nullptr);

        CKey privKey;
        privKey.MakeNewKey(true);
        secp256k1_keypair keypair;
        BOOST_REQUIRE_EQUAL(secp256k1_keypair_create(ctx, &keypair, privKey.begin()), 1);
        secp256k1_xonly_pubkey xonlyPk;
        BOOST_REQUIRE_EQUAL(secp256k1_keypair_xonly_pub(ctx, &xonlyPk, nullptr, &keypair), 1);
        unsigned char xonly32[32];
        BOOST_REQUIRE_EQUAL(secp256k1_xonly_pubkey_serialize(ctx, xonly32, &xonlyPk), 1);
        CXOnlyPubKey outXOnly(xonly32, 32);
        const CScript tapSpk = CScript() << OP_1 << ToByteVector(outXOnly);

        auto computeKeyPathBip341Sighash = [&](const CTransaction& tx, unsigned int nIn, uint8_t hashType, const PrecomputedTransactionData& cache, bool fIncludeInputIdx) -> uint256 {
            unsigned char tagHash[32];
            CSHA256 shaTag;
            shaTag.Write((const unsigned char*)"TapSighash", 10);
            shaTag.Finalize(tagHash);

            CSHA256 sha;
            sha.Write(tagHash, 32);
            sha.Write(tagHash, 32);
            uint8_t epoch = 0;
            sha.Write(&epoch, 1);
            sha.Write(&hashType, 1);
            unsigned char buf[4];
            WriteLE32(buf, tx.nVersion);
            sha.Write(buf, 4);
            WriteLE32(buf, tx.nLockTime);
            sha.Write(buf, 4);

            const bool acp = (hashType & SIGHASH_ANYONECANPAY) != 0;
            const uint8_t outType = (hashType == SIGHASH_DEFAULT) ? SIGHASH_ALL : (hashType & 0x1f);
            if (!acp) {
                sha.Write(cache.hashPrevoutsSingle.begin(), 32);
                sha.Write(cache.hashSpentAmounts.begin(), 32);
                sha.Write(cache.hashSpentScripts.begin(), 32);
                sha.Write(cache.hashSequenceSingle.begin(), 32);
            }
            if (outType != SIGHASH_NONE && outType != SIGHASH_SINGLE) {
                sha.Write(cache.hashOutputsSingle.begin(), 32);
            }
            uint8_t spendType = 0; // key-path
            sha.Write(&spendType, 1);
            if (!acp && fIncludeInputIdx) {
                WriteLE32(buf, nIn);
                sha.Write(buf, 4);
            }
            if (acp) {
                sha.Write(tx.vin[nIn].prevout.hash.begin(), 32);
                WriteLE32(buf, tx.vin[nIn].prevout.n);
                sha.Write(buf, 4);
                unsigned char amt[8];
                WriteLE64(amt, cache.spentOutputs[nIn].nValue);
                sha.Write(amt, 8);
                CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
                stream << static_cast<const CScriptBase&>(cache.spentOutputs[nIn].scriptPubKey);
                sha.Write((const unsigned char*)&stream[0], stream.size());
                WriteLE32(buf, tx.vin[nIn].nSequence);
                sha.Write(buf, 4);
            }
            if (outType == SIGHASH_SINGLE) {
                CSHA256 ssOut;
                CDataStream stream(SER_NETWORK, PROTOCOL_VERSION);
                stream << tx.vout[nIn];
                ssOut.Write((const unsigned char*)&stream[0], stream.size());
                uint256 hOut;
                ssOut.Finalize(hOut.begin());
                sha.Write(hOut.begin(), 32);
            }
            uint256 res;
            sha.Finalize(res.begin());
            return res;
        };

        auto signKeyPath = [&](const uint256& msg) -> std::vector<unsigned char> {
            std::vector<unsigned char> sig(64);
            BOOST_REQUIRE_EQUAL(secp256k1_schnorrsig_sign32(ctx, sig.data(), msg.begin(), &keypair, nullptr), 1);
            return sig;
        };

        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.vin.emplace_back(COutPoint(GetRandHash(), 0));
        mtx.vin.emplace_back(COutPoint(GetRandHash(), 1));
        mtx.vout.emplace_back(CTxOut(5 * COIN, CScript() << OP_TRUE));
        const CTransaction tx(mtx);
        PrecomputedTransactionData precom(tx);
        precom.Init(tx, {CTxOut(3 * COIN, tapSpk), CTxOut(3 * COIN, tapSpk)});

        const unsigned int flagsBip341 = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_TAPROOT_BIP341;
        const unsigned int flagsPreV3 = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_TAPROOT;
        ScriptError serror = SCRIPT_ERR_OK;

        // 1a. 64-byte SIGHASH_DEFAULT (0x00) passes under BIP 341
        {
            const uint256 hDef = computeKeyPathBip341Sighash(tx, 1, SIGHASH_DEFAULT, precom, true);
            const std::vector<unsigned char> sig64 = signKeyPath(hDef);
            CMutableTransaction mtxRun(tx);
            mtxRun.vin[1].scriptSig = CScript() << sig64;
            CTransaction txRun(mtxRun);
            BOOST_CHECK(VerifyScript(txRun.vin[1].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txRun, 1, 3 * COIN, precom), &serror));
        }

        // 1b. 65-byte SIGHASH_ALL (0x01) passes under BIP 341, whereas 65-byte explicit 0x00 MUST fail
        {
            const uint256 hAll = computeKeyPathBip341Sighash(tx, 0, SIGHASH_ALL, precom, true);
            std::vector<unsigned char> sigAll = signKeyPath(hAll);
            sigAll.push_back((unsigned char)SIGHASH_ALL);
            CMutableTransaction mtxRun(tx);
            mtxRun.vin[0].scriptSig = CScript() << sigAll;
            CTransaction txRun(mtxRun);
            BOOST_CHECK(VerifyScript(txRun.vin[0].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txRun, 0, 3 * COIN, precom), &serror));

            // Invalid explicit 0x00 in 65-byte sig must fail BIP 341
            const uint256 hDef = computeKeyPathBip341Sighash(tx, 0, SIGHASH_DEFAULT, precom, true);
            std::vector<unsigned char> sigBad65 = signKeyPath(hDef);
            sigBad65.push_back((unsigned char)SIGHASH_DEFAULT);
            mtxRun.vin[0].scriptSig = CScript() << sigBad65;
            CTransaction txBad65(mtxRun);
            BOOST_CHECK(!VerifyScript(txBad65.vin[0].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txBad65, 0, 3 * COIN, precom), &serror));
        }

        // 1c. 65-byte SIGHASH_ALL|ANYONECANPAY (0x81) passes (input_index omitted per BIP 341), whereas 0x04 fails
        {
            const uint256 hAcp = computeKeyPathBip341Sighash(tx, 0, SIGHASH_ALL | SIGHASH_ANYONECANPAY, precom, true);
            std::vector<unsigned char> sigAcp = signKeyPath(hAcp);
            sigAcp.push_back((unsigned char)(SIGHASH_ALL | SIGHASH_ANYONECANPAY));
            CMutableTransaction mtxRun(tx);
            mtxRun.vin[0].scriptSig = CScript() << sigAcp;
            CTransaction txRun(mtxRun);
            BOOST_CHECK(VerifyScript(txRun.vin[0].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txRun, 0, 3 * COIN, precom), &serror));

            std::vector<unsigned char> sigInvalidType = sigAcp;
            sigInvalidType.back() = 0x04;
            mtxRun.vin[0].scriptSig = CScript() << sigInvalidType;
            CTransaction txInvalidType(mtxRun);
            BOOST_CHECK(!VerifyScript(txInvalidType.vin[0].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txInvalidType, 0, 3 * COIN, precom), &serror));
        }

        // 1d. SIGHASH_SINGLE with nIn >= vout.size() fails, and missing m_spent_outputs_ready fails
        {
            const uint256 hSingle = computeKeyPathBip341Sighash(tx, 0, SIGHASH_SINGLE, precom, true);
            std::vector<unsigned char> sigSingle = signKeyPath(hSingle);
            sigSingle.push_back((unsigned char)SIGHASH_SINGLE);
            CMutableTransaction mtxRun(tx);
            mtxRun.vin[1].scriptSig = CScript() << sigSingle; // input 1 >= vout.size() (1)
            CTransaction txRun(mtxRun);
            BOOST_CHECK(!VerifyScript(txRun.vin[1].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txRun, 1, 3 * COIN, precom), &serror));

            PrecomputedTransactionData uninitPrecom(tx); // m_spent_outputs_ready == false
            const uint256 hDef = computeKeyPathBip341Sighash(tx, 0, SIGHASH_DEFAULT, precom, true);
            mtxRun.vin[0].scriptSig = CScript() << signKeyPath(hDef);
            CTransaction txNoSpent(mtxRun);
            BOOST_CHECK(!VerifyScript(txNoSpent.vin[0].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txNoSpent, 0, 3 * COIN, uninitPrecom), &serror));
        }

        // 1e. Pre-V3 legacy sighash (hashType=0x01 with input_index) on 64-byte sig passes without SCRIPT_VERIFY_TAPROOT_BIP341 and fails with it
        {
            const uint256 hLeg = computeKeyPathBip341Sighash(tx, 1, SIGHASH_ALL, precom, true);
            CMutableTransaction mtxRun(tx);
            mtxRun.vin[1].scriptSig = CScript() << signKeyPath(hLeg);
            CTransaction txLeg(mtxRun);
            BOOST_CHECK(VerifyScript(txLeg.vin[1].scriptSig, tapSpk, flagsPreV3,
                TransactionSignatureChecker(&txLeg, 1, 3 * COIN, precom), &serror));
            BOOST_CHECK(!VerifyScript(txLeg.vin[1].scriptSig, tapSpk, flagsBip341,
                TransactionSignatureChecker(&txLeg, 1, 3 * COIN, precom), &serror));
        }

        secp256k1_context_destroy(ctx);
    }

    // 2. M-12: Remove 0xb1 dust bypass, enforce MAX_P2SH_SIGOPS on P2SH MESCAL scripts, and 5-byte CLTV CScriptNum
    {
        CKey k;
        k.MakeNewKey(true);
        const CPubKey pk = k.GetPubKey();

        // 2a. Arbitrary 0xb1 (OP_CLTV) non-ADAM dust output MUST be rejected as "dust"
        CMutableTransaction mtxDust;
        mtxDust.vin.emplace_back(COutPoint(GetRandHash(), 0));
        CScript nonAdamCltv = CScript() << CScriptNum(500000000LL) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                        << ToByteVector(pk) << OP_CHECKSIG;
        BOOST_REQUIRE_GT(nonAdamCltv.size(), 10U);
        mtxDust.vout.emplace_back(100, nonAdamCltv); // 100 sat < GetDustThreshold
        std::string dustReason;
        BOOST_CHECK(!IsStandardTx(CTransaction(mtxDust), dustReason));
        BOOST_CHECK_EQUAL(dustReason, "dust");

        // 2b. P2SH wrapping a TX_MESCAL_CONTRACT with > MAX_P2SH_SIGOPS (16 OP_CHECKSIGs) rejected by AreInputsStandard
        CScript heavyMescalRedeem;
        for (int i = 0; i < 15; ++i) {
            heavyMescalRedeem << ToByteVector(pk) << OP_CHECKSIGVERIFY;
        }
        heavyMescalRedeem << ToByteVector(pk) << OP_CHECKSIG;
        std::string heavyErr;
        CMescal::Decompile(heavyMescalRedeem, heavyErr);
        BOOST_REQUIRE_MESSAGE(heavyErr.empty(), heavyErr);
        txnouttype mType = TX_NONSTANDARD;
        std::vector<std::vector<unsigned char>> mSols;
        BOOST_REQUIRE(Solver(heavyMescalRedeem, mType, mSols));
        BOOST_REQUIRE_EQUAL(mType, TX_MESCAL_CONTRACT);
        BOOST_REQUIRE_GT(heavyMescalRedeem.GetSigOpCount(true), MAX_P2SH_SIGOPS);

        const COutPoint p2shOut(GetRandHash(), 0);
        CCoinsView coinsDummy;
        CCoinsViewCache view(&coinsDummy);
        view.AddCoin(p2shOut, Coin(CTxOut(COIN, GetScriptForDestination(CScriptID(heavyMescalRedeem))), 1, false, false), false);
        CMutableTransaction mtxSpendP2SH;
        std::vector<unsigned char> redeemBytes(heavyMescalRedeem.begin(), heavyMescalRedeem.end());
        CScript sigScript;
        for (int i = 0; i < 16; ++i) sigScript << std::vector<unsigned char>(64, 0x01);
        sigScript << redeemBytes;
        mtxSpendP2SH.vin.emplace_back(p2shOut, sigScript);
        mtxSpendP2SH.vout.emplace_back(COIN - 1000, CScript() << OP_TRUE);
        BOOST_CHECK(!AreInputsStandard(CTransaction(mtxSpendP2SH), view));

        // 2c. 5-byte CLTV CScriptNum (2500000000LL > INT32_MAX, year ~2049) parses cleanly across CoinLock, PoWLock, and MESCAL
        const int64_t lock5B = 2500000000LL;
        BOOST_REQUIRE_EQUAL(CScriptNum::serialize(lock5B).size(), 5U);

        CScript coinLock5B = CScript() << ToByteVector(pk) << OP_DROP
                                       << CScriptNum(lock5B) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                       << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        CPubKey parsedPk;
        int64_t parsedLock = 0;
        CKeyID parsedKeyId;
        BOOST_CHECK(MatchCoinLockRegistration(coinLock5B, parsedPk, parsedLock, parsedKeyId));
        BOOST_CHECK_EQUAL(parsedLock, lock5B);

        std::vector<unsigned char> nonce(4, 0x01);
        uint256 ch = GetRandHash();
        CScript powLock5B = CScript() << nonce << std::vector<unsigned char>(ch.begin(), ch.end())
                                      << ToByteVector(pk) << OP_DROP << OP_DROP << OP_DROP
                                      << CScriptNum(lock5B) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                      << OP_DUP << OP_HASH160 << ToByteVector(pk.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        std::vector<unsigned char> pNonce;
        uint256 pCh;
        int64_t powParsedLock = 0;
        BOOST_CHECK(MatchPoWLockRegistration(powLock5B, pNonce, pCh, parsedPk, powParsedLock, parsedKeyId));
        BOOST_CHECK_EQUAL(powParsedLock, lock5B);

        CScript mescalLock5B = CScript() << CScriptNum(lock5B) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                         << ToByteVector(pk) << OP_CHECKSIG;
        std::string decompErr;
        CMescal::Decompile(mescalLock5B, decompErr);
        BOOST_CHECK_MESSAGE(decompErr.empty(), decompErr);
    }
}

BOOST_AUTO_TEST_CASE(taproot_branch_lex_ecdsa_sigcache_and_mescal_hardening_test)
{
    // 1. Bip-341 ComputeTapBranchHash must use byte-order memcmp, not uint256::operator< (MSB-first vs LSB-first),
    //    and VerifyScript must preserve CachingTransactionSignatureChecker for ECDSA-in-Tapscript.
    CKey internalKey;
    internalKey.MakeNewKey(true);
    CXOnlyPubKey internalXOnly(internalKey.GetPubKey());
    BOOST_REQUIRE(internalXOnly.IsFullyValid());

    CKey ecdsaKey;
    ecdsaKey.MakeNewKey(true);
    const CPubKey ecdsaPk = ecdsaKey.GetPubKey();

    auto tapLeafHashFn = [](const CScript& leafScript) -> uint256 {
        unsigned char tag[32];
        CSHA256 sTag;
        sTag.Write((const unsigned char*)"TapLeaf", 7);
        sTag.Finalize(tag);
        CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
        ds << static_cast<uint8_t>(0xc0) << leafScript;
        unsigned char res[32];
        CSHA256 s;
        s.Write(tag, 32);
        s.Write(tag, 32);
        s.Write(reinterpret_cast<const unsigned char*>(&ds[0]), ds.size());
        s.Finalize(res);
        return uint256(std::vector<unsigned char>(res, res + 32));
    };

    CScript leafA = CScript() << ToByteVector(ecdsaPk) << OP_CHECKSIG;
    uint256 hashA = tapLeafHashFn(leafA);

    // Find sibling script leafB so hashA[0] vs hashB[0] disagrees with uint256::operator< (high byte hashA[31] vs hashB[31])
    CScript leafB;
    uint256 hashB;
    bool foundDisagreeingPair = false;
    for (int salt = 1; salt < 20000; ++salt) {
        CScript cand = CScript() << CScriptNum(salt) << OP_DROP << OP_TRUE;
        uint256 h = tapLeafHashFn(cand);
        const bool memLess = std::memcmp(hashA.begin(), h.begin(), 32) < 0;
        const bool arithLess = (hashA < h);
        if (memLess != arithLess) {
            leafB = cand;
            hashB = h;
            foundDisagreeingPair = true;
            break;
        }
    }
    BOOST_REQUIRE(foundDisagreeingPair);

    unsigned char tagBr[32];
    CSHA256 sTagBr;
    sTagBr.Write((const unsigned char*)"TapBranch", 9);
    sTagBr.Finalize(tagBr);
    CSHA256 sBr;
    sBr.Write(tagBr, 32);
    sBr.Write(tagBr, 32);
    if (std::memcmp(hashA.begin(), hashB.begin(), 32) < 0) {
        sBr.Write(hashA.begin(), 32);
        sBr.Write(hashB.begin(), 32);
    } else {
        sBr.Write(hashB.begin(), 32);
        sBr.Write(hashA.begin(), 32);
    }
    unsigned char brBytes[32];
    sBr.Finalize(brBytes);
    uint256 merkleRoot(std::vector<unsigned char>(brBytes, brBytes + 32));

    bool parity = false;
    CPubKey qFull = internalXOnly.CreateTapTweak(&merkleRoot, &parity);
    BOOST_REQUIRE(qFull.IsValid());
    CScript tapSpk = GetScriptForDestination(WitnessV1Taproot(CXOnlyPubKey(qFull)));

    std::vector<unsigned char> controlBlock(65);
    controlBlock[0] = 0xc0 | (parity ? 1 : 0);
    std::memcpy(controlBlock.data() + 1, internalXOnly.begin(), 32);
    std::memcpy(controlBlock.data() + 33, hashB.begin(), 32);

    CMutableTransaction txSpend;
    txSpend.nVersion = 2;
    txSpend.vin.emplace_back(COutPoint(GetRandHash(), 0), CScript(), CTxIn::SEQUENCE_FINAL - 1);
    txSpend.vout.emplace_back(4 * COIN, CScript() << OP_TRUE);
    const CTransaction txTmp(txSpend);
    PrecomputedTransactionData precom(txTmp);
    precom.Init(txTmp, {CTxOut(5 * COIN, tapSpk)});

    uint256 bip341TapSighash;
    BOOST_REQUIRE(ComputeTaprootSighash(txTmp, 0, SIGHASH_ALL, SIGVERSION_TAPSCRIPT, precom, bip341TapSighash, true, &hashA, 0xffffffff));
    std::vector<unsigned char> tapSig;
    BOOST_REQUIRE(ecdsaKey.Sign(bip341TapSighash, tapSig));
    tapSig.push_back(static_cast<unsigned char>(SIGHASH_ALL));

    txSpend.vin[0].scriptSig = CScript() << tapSig << std::vector<unsigned char>(leafA.begin(), leafA.end()) << controlBlock;
    const CTransaction txValid(txSpend);
    PrecomputedTransactionData precomValid(txValid);
    precomValid.Init(txValid, {CTxOut(5 * COIN, tapSpk)});

    const unsigned int flagsBip341 = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_STRICTENC |
                                     SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_TAPROOT_BIP341;
    ScriptError serror = SCRIPT_ERR_UNKNOWN_ERROR;
    CachingTransactionSignatureChecker cacheStore(&txValid, 0, 5 * COIN, true, precomValid);
    BOOST_CHECK_MESSAGE(VerifyScript(txValid.vin[0].scriptSig, tapSpk, flagsBip341, cacheStore, &serror), ScriptErrorString(serror));
    // Re-verify using CachingTransactionSignatureChecker store=false (consumes cached entry)
    CachingTransactionSignatureChecker cacheHit(&txValid, 0, 5 * COIN, false, precomValid);
    BOOST_CHECK(VerifyScript(txValid.vin[0].scriptSig, tapSpk, flagsBip341, cacheHit, &serror));

    // Legacy SIGVERSION_BASE ECDSA signature inside Tapscript MUST FAIL under SCRIPT_VERIFY_TAPROOT_BIP341
    uint256 legacySighash = SignatureHash(leafA, txValid, 0, SIGHASH_ALL, 5 * COIN, SIGVERSION_BASE);
    std::vector<unsigned char> legacySig;
    BOOST_REQUIRE(ecdsaKey.Sign(legacySighash, legacySig));
    legacySig.push_back(static_cast<unsigned char>(SIGHASH_ALL));
    CMutableTransaction txBad(txSpend);
    txBad.vin[0].scriptSig = CScript() << legacySig << std::vector<unsigned char>(leafA.begin(), leafA.end()) << controlBlock;
    const CTransaction txBadConst(txBad);
    PrecomputedTransactionData precomBad(txBadConst);
    precomBad.Init(txBadConst, {CTxOut(5 * COIN, tapSpk)});
    BOOST_CHECK(!VerifyScript(txBadConst.vin[0].scriptSig, tapSpk, flagsBip341,
                              TransactionSignatureChecker(&txBadConst, 0, 5 * COIN, precomBad), &serror));

    // 2. MESCAL compile/decompile hardening: empty-stack OP_IF preserves stack-input roundtrip,
    //    duplicate OP_ELSE / nested OP_NOTIF / uncompressed miner pubkeys / >520B hash160 are rejected.
    {
        CKey k;
        k.MakeNewKey(false); // 65-byte uncompressed pubkey
        const std::string uncompHex = HexStr(k.GetPubKey());
        const std::string h160Hex = HexStr(ToByteVector(k.GetPubKey().GetID()));
        std::string err;

        CScript badCl = CMescal::Compile(
            "{\"basic\":{\"M\":{\"role\":\"coin-lock-miner\",\"inputs\":[{\"value\":\"" + uncompHex + "\"},{\"value\":100},{\"value\":\"" + h160Hex + "\"}]}},\"contract\":{\"X\":{\"actions\":[{\"type\":\"basic\",\"name\":\"M\"}]}},\"active_contract\":\"X\"}",
            err);
        BOOST_CHECK(badCl.empty() && !err.empty());

        err.clear();
        CScript badPow = CMescal::Compile(
            "{\"basic\":{\"P\":{\"role\":\"pow-miner\",\"inputs\":[{\"value\":\"0102\"},{\"value\":\"abcd\"},{\"value\":\"" + HexStr(ecdsaPk) + "\"},{\"value\":100},{\"value\":\"" + h160Hex + "\"}]}},\"contract\":{\"X\":{\"actions\":[{\"type\":\"basic\",\"name\":\"P\"}]}},\"active_contract\":\"X\"}",
            err);
        BOOST_CHECK(badPow.empty() && !err.empty());

        err.clear();
        std::string hugeHex(1042, 'a'); // 521 bytes
        CScript badHash = CMescal::Compile(
            "{\"basic\":{\"H\":{\"role\":\"hash160\",\"inputs\":[{\"value\":\"" + hugeHex + "\"}]}},\"contract\":{\"X\":{\"actions\":[{\"type\":\"basic\",\"name\":\"H\"}]}},\"active_contract\":\"X\"}",
            err);
        BOOST_CHECK(badHash.empty() && !err.empty());

        err.clear();
        CScript dupElse = CScript() << OP_1 << OP_IF << OP_1 << OP_ELSE << OP_2 << OP_ELSE << OP_3 << OP_ENDIF;
        BOOST_CHECK(CMescal::Decompile(dupElse, err).isNull() && !err.empty());

        err.clear();
        CScript nestedNotIf = CScript() << OP_1 << OP_IF << OP_1 << OP_NOTIF << OP_2 << OP_ENDIF << OP_ENDIF;
        BOOST_CHECK(CMescal::Decompile(nestedNotIf, err).isNull() && !err.empty());

        // Empty-stack OP_IF decompiles to stack-input and compiles back identically without injecting OP_1
        err.clear();
        CScript stackIf = CScript() << OP_IF << OP_2 << OP_ELSE << OP_3 << OP_ENDIF;
        UniValue decomp = CMescal::Decompile(stackIf, err);
        BOOST_CHECK_MESSAGE(err.empty(), err);
        CScript recompiled = CMescal::Compile(decomp.write(), err);
        BOOST_CHECK_MESSAGE(err.empty(), err);
        BOOST_CHECK(recompiled == stackIf);
    }
}

BOOST_AUTO_TEST_SUITE_END()
