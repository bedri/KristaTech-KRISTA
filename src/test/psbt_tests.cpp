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

BOOST_AUTO_TEST_SUITE_END()
