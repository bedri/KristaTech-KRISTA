// Copyright (c) 2009-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "psbt.h"

#include "hash.h"
#include "keystore.h"
#include "policy/policy.h"
#include "script/standard.h"
#include "utilstrencodings.h"

#include <algorithm>
#include <cstring>

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

bool PSBTInput::Merge(const PSBTInput& input)
{
    // Prevent mixing full non_witness_utxo with witness_utxo or conflicting metadata
    if ((has_non_witness_utxo && !input.witness_utxo.IsNull()) ||
        (!witness_utxo.IsNull() && input.has_non_witness_utxo)) {
        return false;
    }
    if (has_non_witness_utxo && input.has_non_witness_utxo &&
        non_witness_utxo.GetHash() != input.non_witness_utxo.GetHash()) {
        return false;
    }
    if (!witness_utxo.IsNull() && !input.witness_utxo.IsNull() &&
        !(witness_utxo == input.witness_utxo)) {
        return false;
    }
    if (!redeem_script.empty() && !input.redeem_script.empty() &&
        redeem_script != input.redeem_script) {
        return false;
    }
    if (sighash_type != 0 && input.sighash_type != 0 &&
        sighash_type != input.sighash_type) {
        return false;
    }
    if (!tap_internal_key.IsNull() && !input.tap_internal_key.IsNull() &&
        tap_internal_key != input.tap_internal_key) {
        return false;
    }

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
    return true;
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
    if (!has_tx || !psbt.has_tx || tx.GetHash() != psbt.tx.GetHash() ||
        inputs.size() != psbt.inputs.size() || outputs.size() != psbt.outputs.size()) {
        return false;
    }
    std::vector<PSBTInput> mergedInputs = inputs;
    for (size_t i = 0; i < mergedInputs.size(); ++i) {
        if (!mergedInputs[i].Merge(psbt.inputs[i])) {
            return false;
        }
    }
    inputs.swap(mergedInputs);
    unknown.insert(psbt.unknown.begin(), psbt.unknown.end());
    for (size_t i = 0; i < outputs.size(); ++i) {
        outputs[i].Merge(psbt.outputs[i]);
    }
    return IsSane();
}

bool PartiallySignedTransaction::IsSane() const
{
    if (!has_tx || inputs.size() != tx.vin.size() || outputs.size() != tx.vout.size()) return false;
    if (inputs.size() > 4096 || outputs.size() > 4096) return false;
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
    if (base64_tx.size() > 10000000) {
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

namespace {

bool BuildSpentOutputsPrecom(const PartiallySignedTransaction& psbt, const CTransaction& tx_const, PrecomputedTransactionData& txdataOut)
{
    std::vector<CTxOut> spentOutputs(psbt.tx.vin.size());
    for (size_t j = 0; j < psbt.tx.vin.size(); ++j) {
        if (!psbt.GetInputUTXO(spentOutputs[j], static_cast<int>(j))) {
            return false;
        }
    }
    txdataOut.Init(tx_const, std::move(spentOutputs));
    return true;
}

bool BuildScriptSigFromPSBTInput(const CScript& scriptPubKey, const PSBTInput& input, CScript& outScriptSig, int depth = 0)
{
    if (depth > 1) return false;
    txnouttype whichType = TX_NONSTANDARD;
    std::vector<std::vector<unsigned char>> vSolutions;
    if (!Solver(scriptPubKey, whichType, vSolutions)) {
        return false;
    }

    switch (whichType) {
    case TX_WITNESS_V1_TAPROOT:
        if (input.tap_key_sig.empty()) return false;
        outScriptSig = CScript() << input.tap_key_sig;
        return true;

    case TX_PUBKEY: {
        if (vSolutions.empty()) return false;
        const CKeyID keyID = CPubKey(vSolutions[0]).GetID();
        const auto it = input.partial_sigs.find(keyID);
        if (it == input.partial_sigs.end() || it->second.second.empty()) return false;
        outScriptSig = CScript() << it->second.second;
        return true;
    }

    case TX_PUBKEYHASH:
    case TX_ADAM_COINLOCK:
    case TX_ADAM_POWLOCK:
    case TX_CLTV_TIMELOCK: {
        if (vSolutions.empty() || vSolutions[0].size() != 20) return false;
        const CKeyID keyID = CKeyID(uint160(vSolutions[0]));
        const auto it = input.partial_sigs.find(keyID);
        if (it == input.partial_sigs.end() || it->second.second.empty()) return false;
        outScriptSig = CScript() << it->second.second << ToByteVector(it->second.first);
        return true;
    }

    case TX_MULTISIG: {
        if (vSolutions.size() < 3 || vSolutions.front().empty()) return false;
        const size_t nRequired = vSolutions.front()[0];
        const size_t nPubKeys = vSolutions.size() - 2;
        if (nRequired == 0 || nRequired > nPubKeys) return false;
        CScript script;
        script << OP_0;
        size_t nFound = 0;
        for (size_t k = 0; k < nPubKeys && nFound < nRequired; ++k) {
            const CKeyID keyID = CPubKey(vSolutions[k + 1]).GetID();
            const auto it = input.partial_sigs.find(keyID);
            if (it != input.partial_sigs.end() && !it->second.second.empty()) {
                script << it->second.second;
                ++nFound;
            }
        }
        if (nFound != nRequired) return false;
        outScriptSig = script;
        return true;
    }

    case TX_SCRIPTHASH: {
        if (depth > 0 || vSolutions.empty() || vSolutions[0].size() != 20 || input.redeem_script.empty()) {
            return false;
        }
        if (Hash160(input.redeem_script.begin(), input.redeem_script.end()) != uint160(vSolutions[0])) {
            return false;
        }
        CScript innerScriptSig;
        if (!BuildScriptSigFromPSBTInput(input.redeem_script, input, innerScriptSig, depth + 1)) {
            return false;
        }
        outScriptSig = innerScriptSig;
        outScriptSig << std::vector<unsigned char>(input.redeem_script.begin(), input.redeem_script.end());
        return true;
    }

    case TX_MESCAL_CONTRACT: {
        if ((scriptPubKey.size() == 36 && scriptPubKey[0] == 33 && scriptPubKey[34] == OP_CHECKSIGVERIFY && scriptPubKey[35] == OP_1) ||
            (scriptPubKey.size() == 68 && scriptPubKey[0] == 65 && scriptPubKey[66] == OP_CHECKSIGVERIFY && scriptPubKey[67] == OP_1)) {
            const CPubKey pubKey(scriptPubKey.begin() + 1, scriptPubKey.begin() + 1 + scriptPubKey[0]);
            if (!pubKey.IsValid()) return false;
            const auto it = input.partial_sigs.find(pubKey.GetID());
            if (it == input.partial_sigs.end() || it->second.second.empty()) return false;
            outScriptSig = CScript() << it->second.second;
            return true;
        }
        return false;
    }

    default:
        return false;
    }
}

bool FinalizePSBTInputWithContext(const PartiallySignedTransaction& psbt, size_t index, SignatureData& sigdata,
                                  const CTransaction& tx_const, const PrecomputedTransactionData& txdata)
{
    if (index >= psbt.inputs.size()) return false;
    const PSBTInput& input = psbt.inputs[index];
    CTxOut utxo;
    if (!psbt.GetInputUTXO(utxo, static_cast<int>(index))) {
        return false;
    }

    CScript candidateScriptSig = input.final_script_sig;
    if (candidateScriptSig.empty()) {
        if (!BuildScriptSigFromPSBTInput(utxo.scriptPubKey, input, candidateScriptSig)) {
            return false;
        }
    }
    if (candidateScriptSig.empty()) return false;

    const bool fIsTaproot = (utxo.scriptPubKey.size() == 34 && utxo.scriptPubKey[0] == OP_1 && utxo.scriptPubKey[1] == 32);
    if (fIsTaproot && !txdata.m_spent_outputs_ready) {
        // Allow ANYONECANPAY verification when only this input's UTXO is available
        std::vector<unsigned char> singleSig = input.tap_key_sig;
        if (singleSig.empty()) {
            txnouttype t = TX_NONSTANDARD;
            std::vector<std::vector<unsigned char>> vSol;
            CScript::const_iterator pc = candidateScriptSig.begin();
            opcodetype op;
            if (candidateScriptSig.GetOp(pc, op, singleSig) && pc == candidateScriptSig.end() &&
                (singleSig.size() == 65) && (singleSig.back() & SIGHASH_ANYONECANPAY)) {
                (void)t;
                (void)vSol;
            } else {
                return false;
            }
        }
    }

    TransactionSignatureChecker checker(&tx_const, static_cast<unsigned int>(index), utxo.nValue, txdata);
    ScriptError serror = SCRIPT_ERR_OK;
    const unsigned int verifyFlags = STANDARD_SCRIPT_VERIFY_FLAGS | SCRIPT_VERIFY_SIGPUSHONLY;
    if (!VerifyScript(candidateScriptSig, utxo.scriptPubKey, verifyFlags, checker, &serror)) {
        return false;
    }
    sigdata.scriptSig = candidateScriptSig;
    return true;
}

bool IsValidTaprootSighash(int sighash)
{
    if (sighash == 0) return true; // SIGHASH_DEFAULT
    if ((sighash & ~(0x03 | SIGHASH_ANYONECANPAY)) != 0) return false;
    const int base = sighash & (~SIGHASH_ANYONECANPAY);
    return base == SIGHASH_ALL || base == SIGHASH_NONE || base == SIGHASH_SINGLE;
}

bool LookupXOnlyKeyInStore(const CKeyStore& keystore, const CXOnlyPubKey& xonly, CKey& keyOut)
{
    std::vector<unsigned char> cand(33, 0x02);
    std::memcpy(cand.data() + 1, xonly.begin(), 32);
    if (keystore.GetKey(CPubKey(cand).GetID(), keyOut)) return true;
    cand[0] = 0x03;
    return keystore.GetKey(CPubKey(cand).GetID(), keyOut);
}

} // namespace

bool FinalizePSBTInput(const PartiallySignedTransaction& psbt, size_t index, SignatureData& sigdata)
{
    if (index >= psbt.inputs.size()) return false;
    const CTransaction tx_const(psbt.tx);
    PrecomputedTransactionData txdata(tx_const);
    if (!BuildSpentOutputsPrecom(psbt, tx_const, txdata)) {
        // Fallback single-input precom for ANYONECANPAY Taproot inputs
        CTxOut utxo;
        if (!psbt.GetInputUTXO(utxo, static_cast<int>(index))) return false;
        const PSBTInput& input = psbt.inputs[index];
        if (!input.tap_key_sig.empty() && input.tap_key_sig.size() == 65 && (input.tap_key_sig.back() & SIGHASH_ANYONECANPAY)) {
            std::vector<CTxOut> singleSpent(psbt.tx.vin.size());
            singleSpent[index] = utxo;
            txdata.Init(tx_const, std::move(singleSpent));
        }
    }
    return FinalizePSBTInputWithContext(psbt, index, sigdata, tx_const, txdata);
}

bool FinalizePSBT(PartiallySignedTransaction& psbt)
{
    const CTransaction tx_const(psbt.tx);
    PrecomputedTransactionData txdata(tx_const);
    BuildSpentOutputsPrecom(psbt, tx_const, txdata);

    bool complete = true;
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        SignatureData sigdata;
        const bool ok = txdata.m_spent_outputs_ready
            ? FinalizePSBTInputWithContext(psbt, i, sigdata, tx_const, txdata)
            : FinalizePSBTInput(psbt, i, sigdata);
        if (ok) {
            psbt.inputs[i].final_script_sig = sigdata.scriptSig;
            psbt.inputs[i].partial_sigs.clear();
            psbt.inputs[i].redeem_script.clear();
            psbt.inputs[i].hd_keypaths.clear();
            psbt.inputs[i].sighash_type = 0;
            psbt.inputs[i].tap_key_sig.clear();
            psbt.inputs[i].tap_internal_key.SetNull();
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
    if (index >= psbt.inputs.size() || index >= psbt.tx.vin.size()) return false;
    if (!psbt.IsSane()) return false;
    CTxOut utxo;
    if (!psbt.GetInputUTXO(utxo, static_cast<int>(index))) {
        return false;
    }
    PSBTInput& input = psbt.inputs[index];
    const bool fIsTaproot = (utxo.scriptPubKey.size() == 34 && utxo.scriptPubKey[0] == OP_1 && utxo.scriptPubKey[1] == 32);
    if (!input.has_non_witness_utxo && !fIsTaproot) {
        // Non-Taproot inputs must provide full non_witness_utxo to prevent fee-overpayment attacks (BIP-174).
        return false;
    }
    if (!input.final_script_sig.empty()) {
        SignatureData verifySig;
        if (!FinalizePSBTInput(psbt, index, verifySig)) {
            return false;
        }
        if (out_sigdata) *out_sigdata = verifySig;
        return true;
    }

    if (input.sighash_type != 0 && input.sighash_type != sighash) {
        return false;
    }
    const int use_sighash = sighash;
    if (fIsTaproot) {
        if (!IsValidTaprootSighash(use_sighash)) return false;
    } else {
        const int base = use_sighash & (~SIGHASH_ANYONECANPAY);
        if (use_sighash == 0 || (base != SIGHASH_ALL && base != SIGHASH_NONE && base != SIGHASH_SINGLE)) {
            return false;
        }
    }

    const CTransaction tx_const(psbt.tx);
    PrecomputedTransactionData txdata(tx_const);
    bool haveAllUtxos = BuildSpentOutputsPrecom(psbt, tx_const, txdata);
    if (!haveAllUtxos && fIsTaproot && (use_sighash & SIGHASH_ANYONECANPAY)) {
        std::vector<CTxOut> singleSpent(psbt.tx.vin.size());
        singleSpent[index] = utxo;
        txdata.Init(tx_const, std::move(singleSpent));
    }

    if (fIsTaproot) {
        if (!txdata.m_spent_outputs_ready) return false;
        const CXOnlyPubKey qXOnly(utxo.scriptPubKey.data() + 2, 32);
        uint256 tapSighash;
        if (!ComputeTaprootSighash(tx_const, static_cast<unsigned int>(index), use_sighash, SIGVERSION_TAPROOT, txdata, tapSighash)) {
            return false;
        }
        std::vector<unsigned char> sigBytes;
        CKey signKey;
        if (!input.tap_internal_key.IsNull()) {
            const CXOnlyPubKey internalXOnly(input.tap_internal_key.begin(), 32);
            const CPubKey tweaked = internalXOnly.CreateTapTweak(nullptr, nullptr);
            if (tweaked.IsValid() && std::memcmp(tweaked.begin() + 1, qXOnly.begin(), 32) == 0 &&
                LookupXOnlyKeyInStore(keystore, internalXOnly, signKey)) {
                if (!signKey.SignTaprootKeyPath(tapSighash, sigBytes, nullptr)) {
                    return false;
                }
            }
        }
        if (sigBytes.empty() && LookupXOnlyKeyInStore(keystore, qXOnly, signKey)) {
            if (!signKey.SignSchnorr(tapSighash, sigBytes)) {
                return false;
            }
        }
        if (sigBytes.empty()) return false;
        if (use_sighash != 0) {
            sigBytes.push_back(static_cast<unsigned char>(use_sighash));
        }
        input.tap_key_sig = sigBytes;
        SignatureData finSig;
        if (!FinalizePSBTInputWithContext(psbt, index, finSig, tx_const, txdata)) {
            return false;
        }
        input.final_script_sig = finSig.scriptSig;
        if (out_sigdata) *out_sigdata = finSig;
        return true;
    }

    txnouttype whichType = TX_NONSTANDARD;
    std::vector<std::vector<unsigned char>> vSolutions;
    if (!Solver(utxo.scriptPubKey, whichType, vSolutions)) {
        return false;
    }

    CScript signingScript = utxo.scriptPubKey;
    txnouttype innerType = whichType;
    std::vector<std::vector<unsigned char>> innerSolutions = vSolutions;
    if (whichType == TX_SCRIPTHASH) {
        if (vSolutions.size() != 1 || vSolutions[0].size() != 20) return false;
        if (input.redeem_script.empty()) {
            CScript rs;
            if (keystore.GetCScript(CScriptID(uint160(vSolutions[0])), rs)) {
                input.redeem_script = rs;
            }
        }
        if (input.redeem_script.empty() ||
            Hash160(input.redeem_script.begin(), input.redeem_script.end()) != uint160(vSolutions[0])) {
            return false;
        }
        signingScript = input.redeem_script;
        if (!Solver(signingScript, innerType, innerSolutions) || innerType == TX_SCRIPTHASH) {
            return false;
        }
    }

    const TransactionSignatureCreator creator = haveAllUtxos
        ? TransactionSignatureCreator(&keystore, &tx_const, static_cast<unsigned int>(index), utxo.nValue, txdata, use_sighash)
        : TransactionSignatureCreator(&keystore, &tx_const, static_cast<unsigned int>(index), utxo.nValue, use_sighash);

    if (innerType == TX_MULTISIG) {
        if (innerSolutions.size() < 3) return false;
        const size_t nPubKeys = innerSolutions.size() - 2;
        for (size_t k = 0; k < nPubKeys; ++k) {
            const CPubKey pk(innerSolutions[k + 1]);
            if (!pk.IsValid()) continue;
            const CKeyID kid = pk.GetID();
            if (input.partial_sigs.count(kid) || !keystore.HaveKey(kid)) continue;
            std::vector<unsigned char> vchSig;
            if (creator.CreateSig(vchSig, kid, signingScript, SIGVERSION_BASE) && !vchSig.empty()) {
                input.partial_sigs[kid] = std::make_pair(pk, vchSig);
            }
        }
        SignatureData finSig;
        if (FinalizePSBTInputWithContext(psbt, index, finSig, tx_const, txdata)) {
            input.final_script_sig = finSig.scriptSig;
            if (out_sigdata) *out_sigdata = finSig;
            return true;
        }
        return false;
    }

    if (whichType == TX_SCRIPTHASH) {
        SignatureData innerSigdata;
        if (!ProduceSignature(creator, signingScript, innerSigdata)) {
            return false;
        }
        CScript combined = innerSigdata.scriptSig;
        combined << std::vector<unsigned char>(input.redeem_script.begin(), input.redeem_script.end());
        input.final_script_sig = combined;
        SignatureData finSig;
        if (!FinalizePSBTInputWithContext(psbt, index, finSig, tx_const, txdata)) {
            input.final_script_sig.clear();
            return false;
        }
        if (out_sigdata) *out_sigdata = finSig;
        return true;
    }

    SignatureData sigdata;
    if (!ProduceSignature(creator, utxo.scriptPubKey, sigdata) || sigdata.scriptSig.empty()) {
        return false;
    }
    input.final_script_sig = sigdata.scriptSig;
    if (out_sigdata) *out_sigdata = sigdata;
    return true;
}
