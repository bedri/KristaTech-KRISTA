// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"
#include "keystore.h"
#include "policy/policy.h"
#include "psbt.h"
#include "script/interpreter.h"
#include "script/sign.h"
#include "script/standard.h"
#include "test/test_kristatech.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(psbt_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(psbt_non_taproot_requires_non_witness_utxo)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddKey(key));

    CMutableTransaction txPrev;
    txPrev.nVersion = 1;
    txPrev.vout.resize(1);
    txPrev.vout[0].nValue = 50 * COIN;
    txPrev.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    txSpend.vin.resize(1);
    txSpend.vin[0].prevout = COutPoint(txPrev.GetHash(), 0);
    txSpend.vout.resize(1);
    txSpend.vout[0].nValue = 49 * COIN;
    txSpend.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    // 1. Supplying only witness_utxo for a non-Taproot input must be refused (M-04 fee-overpayment defense)
    PartiallySignedTransaction psbtWitnessOnly(txSpend);
    psbtWitnessOnly.inputs[0].witness_utxo = txPrev.vout[0];
    BOOST_CHECK(!SignPSBTInput(keystore, psbtWitnessOnly, 0));

    // 2. Supplying a mismatched non_witness_utxo hash must also fail
    CMutableTransaction txWrong = txPrev;
    txWrong.nLockTime = 42;
    PartiallySignedTransaction psbtMismatch(txSpend);
    psbtMismatch.inputs[0].non_witness_utxo = CTransaction(txWrong);
    psbtMismatch.inputs[0].has_non_witness_utxo = true;
    BOOST_CHECK(!SignPSBTInput(keystore, psbtMismatch, 0));

    // 3. Supplying the matching full non_witness_utxo must sign, serialize cleanly, and finalize
    PartiallySignedTransaction psbtValid(txSpend);
    psbtValid.inputs[0].non_witness_utxo = CTransaction(txPrev);
    psbtValid.inputs[0].has_non_witness_utxo = true;
    BOOST_CHECK(SignPSBTInput(keystore, psbtValid, 0));

    const std::string encoded = EncodeBase64PSBT(psbtValid);
    PartiallySignedTransaction decoded;
    std::string err;
    BOOST_REQUIRE(DecodeBase64PSBT(decoded, encoded, err));

    CMutableTransaction extracted;
    BOOST_REQUIRE(FinalizeAndExtractPSBT(decoded, extracted));

    ScriptError serror = SCRIPT_ERR_OK;
    const CTransaction extractedConst(extracted);
    BOOST_CHECK(VerifyScript(
        extracted.vin[0].scriptSig,
        txPrev.vout[0].scriptPubKey,
        STANDARD_SCRIPT_VERIFY_FLAGS,
        TransactionSignatureChecker(&extractedConst, 0, txPrev.vout[0].nValue),
        &serror));
}

BOOST_AUTO_TEST_CASE(psbt_decode_rejects_embedded_nul_and_oversize)
{
    PartiallySignedTransaction psbt;
    std::string err;

    std::string withNul = "cHNidP8=";
    withNul.push_back('\0');
    withNul += "AAAA";
    BOOST_CHECK(!DecodeBase64PSBT(psbt, withNul, err));

    std::string huge(10000001, 'A');
    BOOST_CHECK(!DecodeBase64PSBT(psbt, huge, err));
}

BOOST_AUTO_TEST_CASE(psbt_sign_rejects_sighash_mismatch_and_accepts_match)
{
    CKey key;
    key.MakeNewKey(true);
    CBasicKeyStore keystore;
    BOOST_REQUIRE(keystore.AddKey(key));

    CMutableTransaction txPrev;
    txPrev.nVersion = 1;
    txPrev.vout.resize(1);
    txPrev.vout[0].nValue = 25 * COIN;
    txPrev.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    CMutableTransaction txSpend;
    txSpend.nVersion = 1;
    txSpend.vin.resize(1);
    txSpend.vin[0].prevout = COutPoint(txPrev.GetHash(), 0);
    txSpend.vout.resize(1);
    txSpend.vout[0].nValue = 24 * COIN;
    txSpend.vout[0].scriptPubKey = GetScriptForDestination(key.GetPubKey().GetID());

    // H-17: SignPSBTInput must refuse to sign if PSBT input specifies a different sighash_type
    PartiallySignedTransaction psbtMismatch(txSpend);
    psbtMismatch.inputs[0].non_witness_utxo = CTransaction(txPrev);
    psbtMismatch.inputs[0].has_non_witness_utxo = true;
    psbtMismatch.inputs[0].sighash_type = SIGHASH_NONE | SIGHASH_ANYONECANPAY;
    BOOST_CHECK(!SignPSBTInput(keystore, psbtMismatch, 0, SIGHASH_ALL));

    // H-17: Matching SIGHASH_ALL succeeds cleanly
    PartiallySignedTransaction psbtMatchAll(txSpend);
    psbtMatchAll.inputs[0].non_witness_utxo = CTransaction(txPrev);
    psbtMatchAll.inputs[0].has_non_witness_utxo = true;
    psbtMatchAll.inputs[0].sighash_type = SIGHASH_ALL;
    BOOST_CHECK(SignPSBTInput(keystore, psbtMatchAll, 0, SIGHASH_ALL));
    BOOST_CHECK(!psbtMatchAll.inputs[0].final_script_sig.empty());

    // Matching SIGHASH_NONE|ANYONECANPAY also succeeds when requested explicitly
    PartiallySignedTransaction psbtMatchCustom(txSpend);
    psbtMatchCustom.inputs[0].non_witness_utxo = CTransaction(txPrev);
    psbtMatchCustom.inputs[0].has_non_witness_utxo = true;
    psbtMatchCustom.inputs[0].sighash_type = SIGHASH_NONE | SIGHASH_ANYONECANPAY;
    BOOST_CHECK(SignPSBTInput(keystore, psbtMatchCustom, 0, SIGHASH_NONE | SIGHASH_ANYONECANPAY));
    BOOST_CHECK(!psbtMatchCustom.inputs[0].final_script_sig.empty());
}

BOOST_AUTO_TEST_CASE(psbt_taproot_p2sh_multisig_merge_and_cap_hardening)
{
    // 1. P2SH 2-of-3 Multisig partial signing across two wallets + Merge + Finalize + Extract
    CKey k1, k2, k3;
    k1.MakeNewKey(true);
    k2.MakeNewKey(true);
    k3.MakeNewKey(true);
    CScript redeemScript = GetScriptForMultisig(2, {k1.GetPubKey(), k2.GetPubKey(), k3.GetPubKey()});
    CScript p2shSpk = GetScriptForDestination(CScriptID(redeemScript));

    CBasicKeyStore ks1, ks2;
    BOOST_REQUIRE(ks1.AddKey(k1));
    BOOST_REQUIRE(ks2.AddKey(k2));

    CMutableTransaction txPrevMulti;
    txPrevMulti.nVersion = 1;
    txPrevMulti.vout.emplace_back(10 * COIN, p2shSpk);

    // 2. Taproot key-path input (BIP86 tweaked tap_internal_key)
    CKey tapInternalKey;
    tapInternalKey.MakeNewKey(true);
    CXOnlyPubKey internalXOnly(tapInternalKey.GetPubKey());
    CPubKey tweakedPub = internalXOnly.CreateTapTweak(nullptr, nullptr);
    BOOST_REQUIRE(tweakedPub.IsValid());
    CScript tapSpk = GetScriptForDestination(WitnessV1Taproot(CXOnlyPubKey(tweakedPub)));
    BOOST_REQUIRE(ks1.AddKey(tapInternalKey));
    BOOST_REQUIRE(ks2.AddKey(tapInternalKey));

    CMutableTransaction txPrevTap;
    txPrevTap.nVersion = 2;
    txPrevTap.vout.emplace_back(5 * COIN, tapSpk);

    CMutableTransaction txSpend;
    txSpend.nVersion = 2;
    txSpend.vin.emplace_back(COutPoint(txPrevMulti.GetHash(), 0));
    txSpend.vin.emplace_back(COutPoint(txPrevTap.GetHash(), 0));
    txSpend.vout.emplace_back(14 * COIN, GetScriptForDestination(k3.GetPubKey().GetID()));

    PartiallySignedTransaction psbtA(txSpend);
    psbtA.inputs[0].non_witness_utxo = CTransaction(txPrevMulti);
    psbtA.inputs[0].has_non_witness_utxo = true;
    psbtA.inputs[0].redeem_script = redeemScript;
    psbtA.inputs[1].witness_utxo = txPrevTap.vout[0];
    psbtA.inputs[1].tap_internal_key = uint256(std::vector<unsigned char>(internalXOnly.begin(), internalXOnly.end()));

    PartiallySignedTransaction psbtB = psbtA;

    // Wallet 1 signs input 0 partially (1 of 2 multisig -> returns false until threshold reached) and input 1 completely
    BOOST_CHECK(!SignPSBTInput(ks1, psbtA, 0, SIGHASH_ALL));
    BOOST_CHECK_EQUAL(psbtA.inputs[0].partial_sigs.size(), 1U);
    BOOST_CHECK(psbtA.inputs[0].final_script_sig.empty());
    BOOST_CHECK(SignPSBTInput(ks1, psbtA, 1, 0 /* SIGHASH_DEFAULT */));
    BOOST_CHECK(!psbtA.inputs[1].final_script_sig.empty());
    BOOST_CHECK_EQUAL(psbtA.inputs[1].tap_key_sig.size(), 64U);

    // Wallet 2 signs input 0 with second key
    BOOST_CHECK(!SignPSBTInput(ks2, psbtB, 0, SIGHASH_ALL));
    BOOST_CHECK_EQUAL(psbtB.inputs[0].partial_sigs.size(), 1U);

    // Reject mixing non_witness_utxo and witness_utxo during Merge
    PartiallySignedTransaction psbtMixed = psbtA;
    psbtMixed.inputs[0] = PSBTInput();
    psbtMixed.inputs[0].witness_utxo = txPrevMulti.vout[0];
    PartiallySignedTransaction psbtMergeTarget = psbtA;
    BOOST_CHECK(!psbtMergeTarget.Merge(psbtMixed));

    // Merge compatible PSBTs -> FinalizePSBT clears tap_key_sig/tap_internal_key and verifies scripts
    BOOST_REQUIRE(psbtA.Merge(psbtB));
    BOOST_CHECK_EQUAL(psbtA.inputs[0].partial_sigs.size(), 2U);
    BOOST_REQUIRE(FinalizePSBT(psbtA));
    BOOST_CHECK(!psbtA.inputs[0].final_script_sig.empty());
    BOOST_CHECK(psbtA.inputs[0].partial_sigs.empty());
    BOOST_CHECK(psbtA.inputs[0].redeem_script.empty());
    BOOST_CHECK(psbtA.inputs[1].tap_key_sig.empty());
    BOOST_CHECK(psbtA.inputs[1].tap_internal_key.IsNull());

    CMutableTransaction extracted;
    BOOST_REQUIRE(FinalizeAndExtractPSBT(psbtA, extracted));
    const CTransaction txConst(extracted);
    PrecomputedTransactionData precom(txConst);
    precom.Init(txConst, {txPrevMulti.vout[0], txPrevTap.vout[0]});
    ScriptError serror = SCRIPT_ERR_UNKNOWN_ERROR;
    BOOST_CHECK(VerifyScript(txConst.vin[0].scriptSig, p2shSpk, STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_SIGPUSHONLY,
                             TransactionSignatureChecker(&txConst, 0, 10 * COIN, precom), &serror));
    BOOST_CHECK(VerifyScript(txConst.vin[1].scriptSig, tapSpk, STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_SIGPUSHONLY,
                             TransactionSignatureChecker(&txConst, 1, 5 * COIN, precom), &serror));

    // 3. Reject PSBT with > 4096 inputs directly during Unserialize
    CMutableTransaction hugeTx;
    hugeTx.vin.resize(4097);
    PartiallySignedTransaction hugePsbt(hugeTx);
    CDataStream ssHuge(SER_NETWORK, PROTOCOL_VERSION);
    ssHuge << hugePsbt;
    PartiallySignedTransaction parsedHuge;
    BOOST_CHECK_THROW(ssHuge >> parsedHuge, std::ios_base::failure);

    // 4. Reject invalid PSBT_IN_TAP_KEY_SIG length != 64 && != 65 during Unserialize
    CMutableTransaction smallTx;
    smallTx.vin.resize(1);
    PartiallySignedTransaction badSigPsbt(smallTx);
    badSigPsbt.inputs[0].tap_key_sig.assign(10, 0xaa);
    CDataStream ssBadSig(SER_NETWORK, PROTOCOL_VERSION);
    ssBadSig << badSigPsbt;
    PartiallySignedTransaction parsedBadSig;
    BOOST_CHECK_THROW(ssBadSig >> parsedBadSig, std::ios_base::failure);
}

BOOST_AUTO_TEST_SUITE_END()
