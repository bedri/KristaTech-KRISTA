// Copyright (c) 2009-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "psbt.h"

#include "script/standard.h"
#include "utilstrencodings.h"

#include <algorithm>

bool PSBTInput::IsNull() const
{
    return !has_non_witness_utxo && witness_utxo.IsNull() && partial_sigs.empty() &&
           unknown.empty() && hd_keypaths.empty() && redeem_script.empty() &&
           final_script_sig.empty() && tap_key_sig.empty() && tap_internal_key.IsNull() &&
           sighash_type == 0;
}

void PSBTInput::FillSignatureData(SignatureData& sigdata) const
{
    if (!final_script_sig.empty()) {
        sigdata.scriptSig = final_script_sig;
        return;
    }
}

void PSBTInput::FromSignatureData(const SignatureData& sigdata)
{
    if (!sigdata.scriptSig.empty()) {
        final_script_sig = sigdata.scriptSig;
    }
}

void PSBTInput::Merge(const PSBTInput& input)
{
    if (!has_non_witness_utxo && input.has_non_witness_utxo) {
        non_witness_utxo = input.non_witness_utxo;
        has_non_witness_utxo = true;
    }
    if (witness_utxo.IsNull() && !input.witness_utxo.IsNull()) {
        witness_utxo = input.witness_utxo;
    }
    partial_sigs.insert(input.partial_sigs.begin(), input.partial_sigs.end());
    hd_keypaths.insert(input.hd_keypaths.begin(), input.hd_keypaths.end());
    unknown.insert(input.unknown.begin(), input.unknown.end());
    if (redeem_script.empty() && !input.redeem_script.empty()) redeem_script = input.redeem_script;
    if (final_script_sig.empty() && !input.final_script_sig.empty()) final_script_sig = input.final_script_sig;
    if (tap_key_sig.empty() && !input.tap_key_sig.empty()) tap_key_sig = input.tap_key_sig;
    if (tap_internal_key.IsNull() && !input.tap_internal_key.IsNull()) tap_internal_key = input.tap_internal_key;
    if (sighash_type == 0 && input.sighash_type != 0) sighash_type = input.sighash_type;
}

bool PSBTInput::IsSane() const
{
    return !(has_non_witness_utxo && !witness_utxo.IsNull());
}

bool PSBTOutput::IsNull() const
{
    return redeem_script.empty() && hd_keypaths.empty() && tap_internal_key.IsNull() && unknown.empty();
}

void PSBTOutput::Merge(const PSBTOutput& output)
{
    hd_keypaths.insert(output.hd_keypaths.begin(), output.hd_keypaths.end());
    unknown.insert(output.unknown.begin(), output.unknown.end());
    if (redeem_script.empty() && !output.redeem_script.empty()) redeem_script = output.redeem_script;
    if (tap_internal_key.IsNull() && !output.tap_internal_key.IsNull()) tap_internal_key = output.tap_internal_key;
}

PartiallySignedTransaction::PartiallySignedTransaction(const CMutableTransaction& tx_in)
    : tx(tx_in), has_tx(true)
{
    for (CTxIn& txin : tx.vin) {
        txin.scriptSig.clear();
    }
    inputs.resize(tx.vin.size());
    outputs.resize(tx.vout.size());
}

bool PartiallySignedTransaction::Merge(const PartiallySignedTransaction& psbt)
{
    if (!has_tx || !psbt.has_tx || tx.GetHash() != psbt.tx.GetHash()) {
        return false;
    }
    unknown.insert(psbt.unknown.begin(), psbt.unknown.end());
    for (size_t i = 0; i < inputs.size(); ++i) {
        inputs[i].Merge(psbt.inputs[i]);
    }
    for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i].Merge(psbt.outputs[i]);
    }
    return true;
}

bool PartiallySignedTransaction::IsSane() const
{
    if (!has_tx) return false;
    for (const PSBTInput& in : inputs) {
        if (!in.IsSane()) return false;
    }
    return true;
}

bool PartiallySignedTransaction::GetInputUTXO(CTxOut& utxo, int input_index) const
{
    if (input_index < 0 || static_cast<size_t>(input_index) >= inputs.size()) return false;
    const PSBTInput& input = inputs[input_index];
    if (input.has_non_witness_utxo) {
        const uint32_t prevout_n = tx.vin[input_index].prevout.n;
        if (input.non_witness_utxo.GetHash() != tx.vin[input_index].prevout.hash) return false;
        if (prevout_n >= input.non_witness_utxo.vout.size()) return false;
        utxo = input.non_witness_utxo.vout[prevout_n];
        return true;
    }
    if (!input.witness_utxo.IsNull()) {
        utxo = input.witness_utxo;
        return true;
    }
    return false;
}

bool DecodeBase64PSBT(PartiallySignedTransaction& psbt, const std::string& base64_tx, std::string& error)
{
    if (base64_tx.find('\0') != std::string::npos) {
        error = "Invalid base64 encoding (embedded NUL)";
        return false;
    }
    if (base64_tx.size() > 100000000) {
        error = "PSBT exceeds maximum size";
        return false;
    }
    bool invalid = false;
    std::vector<unsigned char> raw = DecodeBase64(base64_tx.c_str(), &invalid);
    if (invalid || raw.empty()) {
        error = "Invalid base64 encoding";
        return false;
    }
    try {
        CDataStream ds(raw, SER_NETWORK, PROTOCOL_VERSION);
        ds >> psbt;
        if (!ds.empty()) {
            error = "Trailing data after PSBT";
            return false;
        }
        if (!psbt.IsSane()) {
            error = "PSBT is not sane";
            return false;
        }
    } catch (const std::exception& e) {
        error = std::string("TX decode failed: ") + e.what();
        return false;
    }
    return true;
}

std::string EncodeBase64PSBT(const PartiallySignedTransaction& psbt)
{
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << psbt;
    return EncodeBase64(reinterpret_cast<const unsigned char*>(&ds[0]), ds.size());
}

bool FinalizePSBTInput(const PartiallySignedTransaction& psbt, size_t index, SignatureData& sigdata)
{
    if (index >= psbt.inputs.size()) return false;
    const PSBTInput& input = psbt.inputs[index];
    CTxOut utxo;
    if (!psbt.GetInputUTXO(utxo, static_cast<int>(index))) {
        return false;
    }
    CScript candidateScriptSig = input.final_script_sig;
    if (candidateScriptSig.empty() && !input.tap_key_sig.empty()) {
        candidateScriptSig << input.tap_key_sig;
    }
    if (candidateScriptSig.empty() && !input.partial_sigs.empty()) {
        const auto& firstSig = input.partial_sigs.begin()->second;
        candidateScriptSig << firstSig.second << ToByteVector(firstSig.first);
    }
    if (candidateScriptSig.empty()) return false;

    const CTransaction tx_const(psbt.tx);
    PrecomputedTransactionData txdata(tx_const);
    std::vector<CTxOut> spentOutputs(psbt.tx.vin.size());
    bool haveAllUtxos = true;
    for (size_t j = 0; j < psbt.tx.vin.size(); ++j) {
        if (!psbt.GetInputUTXO(spentOutputs[j], static_cast<int>(j))) {
            haveAllUtxos = false;
            break;
        }
    }
    if (haveAllUtxos) {
        txdata.Init(tx_const, std::move(spentOutputs));
    }
    TransactionSignatureChecker checker(&tx_const, static_cast<unsigned int>(index), utxo.nValue, txdata);
    ScriptError serror = SCRIPT_ERR_OK;
    const unsigned int verifyFlags = SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_STRICTENC |
                                     SCRIPT_VERIFY_MINIMALDATA | SCRIPT_VERIFY_NULLDUMMY |
                                     SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY |
                                     SCRIPT_VERIFY_LOW_S | SCRIPT_VERIFY_TAPROOT;
    if (!VerifyScript(candidateScriptSig, utxo.scriptPubKey, verifyFlags, checker, &serror)) {
        return false;
    }
    sigdata.scriptSig = candidateScriptSig;
    return true;
}

bool FinalizePSBT(PartiallySignedTransaction& psbt)
{
    bool complete = true;
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        SignatureData sigdata;
        if (FinalizePSBTInput(psbt, i, sigdata)) {
            psbt.inputs[i].final_script_sig = sigdata.scriptSig;
            psbt.inputs[i].partial_sigs.clear();
            psbt.inputs[i].redeem_script.clear();
            psbt.inputs[i].hd_keypaths.clear();
            psbt.inputs[i].sighash_type = 0;
        } else {
            complete = false;
        }
    }
    return complete;
}

bool FinalizeAndExtractPSBT(PartiallySignedTransaction& psbt, CMutableTransaction& result)
{
    if (!FinalizePSBT(psbt)) return false;
    result = psbt.tx;
    for (size_t i = 0; i < result.vin.size(); ++i) {
        result.vin[i].scriptSig = psbt.inputs[i].final_script_sig;
    }
    return true;
}

bool SignPSBTInput(const CKeyStore& keystore, PartiallySignedTransaction& psbt, size_t index, int sighash, SignatureData* out_sigdata)
{
    if (index >= psbt.inputs.size()) return false;
    CTxOut utxo;
    if (!psbt.GetInputUTXO(utxo, static_cast<int>(index))) {
        return false;
    }
    PSBTInput& input = psbt.inputs[index];
    if (!input.final_script_sig.empty()) {
        if (out_sigdata) input.FillSignatureData(*out_sigdata);
        return true;
    }

    SignatureData sigdata;
    input.FillSignatureData(sigdata);

    const int use_sighash = (input.sighash_type != 0) ? input.sighash_type : sighash;
    const MutableTransactionSignatureCreator creator(&keystore, &psbt.tx, static_cast<unsigned int>(index), utxo.nValue, use_sighash);
    const bool ok = ProduceSignature(creator, utxo.scriptPubKey, sigdata);
    if (ok && !sigdata.scriptSig.empty()) {
        input.final_script_sig = sigdata.scriptSig;
    }
    if (out_sigdata) *out_sigdata = sigdata;
    return ok;
}
