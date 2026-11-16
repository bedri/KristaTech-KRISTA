// Copyright (c) 2009-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PSBT_H
#define BITCOIN_PSBT_H

#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/keyorigin.h"
#include "script/script.h"
#include "script/sign.h"
#include "serialize.h"
#include "streams.h"
#include "uint256.h"

#include <map>
#include <set>
#include <string>
#include <vector>

static constexpr uint8_t PSBT_MAGIC_BYTES[5] = {'p', 's', 'b', 't', 0xff};

// Global types
static constexpr uint8_t PSBT_GLOBAL_UNSIGNED_TX = 0x00;
static constexpr uint8_t PSBT_GLOBAL_XPUB = 0x01;
static constexpr uint8_t PSBT_GLOBAL_VERSION = 0xFB;
static constexpr uint8_t PSBT_GLOBAL_PROPRIETARY = 0xFC;

// Per-input types
static constexpr uint8_t PSBT_IN_NON_WITNESS_UTXO = 0x00;
static constexpr uint8_t PSBT_IN_WITNESS_UTXO = 0x01;
static constexpr uint8_t PSBT_IN_PARTIAL_SIG = 0x02;
static constexpr uint8_t PSBT_IN_SIGHASH = 0x03;
static constexpr uint8_t PSBT_IN_REDEEMSCRIPT = 0x04;
static constexpr uint8_t PSBT_IN_BIP32_DERIVATION = 0x06;
static constexpr uint8_t PSBT_IN_SCRIPTSIG = 0x07;
static constexpr uint8_t PSBT_IN_TAP_KEY_SIG = 0x13;
static constexpr uint8_t PSBT_IN_TAP_INTERNAL_KEY = 0x17;
static constexpr uint8_t PSBT_IN_PROPRIETARY = 0xFC;

// Per-output types
static constexpr uint8_t PSBT_OUT_REDEEMSCRIPT = 0x00;
static constexpr uint8_t PSBT_OUT_BIP32_DERIVATION = 0x02;
static constexpr uint8_t PSBT_OUT_TAP_INTERNAL_KEY = 0x05;
static constexpr uint8_t PSBT_OUT_PROPRIETARY = 0xFC;

static constexpr uint8_t PSBT_SEPARATOR = 0x00;
static constexpr uint32_t PSBT_HIGHEST_VERSION = 0;

struct PSBTInput {
    CTransaction non_witness_utxo;
    bool has_non_witness_utxo{false};
    CTxOut witness_utxo;
    CScript redeem_script;
    CScript final_script_sig;
    std::map<CKeyID, std::pair<CPubKey, KeyOriginInfo>> hd_keypaths;
    std::map<CKeyID, std::pair<CPubKey, std::vector<unsigned char>>> partial_sigs;
    std::vector<unsigned char> tap_key_sig;
    uint256 tap_internal_key;
    int sighash_type{0};
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown;

    bool IsNull() const;
    void FillSignatureData(SignatureData& sigdata) const;
    void FromSignatureData(const SignatureData& sigdata);
    void Merge(const PSBTInput& input);
    bool IsSane() const;

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        auto write_kv = [&](uint8_t type, const std::vector<unsigned char>& key_data, const std::vector<unsigned char>& val) {
            std::vector<unsigned char> key;
            key.push_back(type);
            key.insert(key.end(), key_data.begin(), key_data.end());
            s << key << val;
        };
        if (has_non_witness_utxo) {
            CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
            ds << non_witness_utxo;
            write_kv(PSBT_IN_NON_WITNESS_UTXO, {}, std::vector<unsigned char>(ds.begin(), ds.end()));
        }
        if (!witness_utxo.IsNull()) {
            CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
            ds << witness_utxo;
            write_kv(PSBT_IN_WITNESS_UTXO, {}, std::vector<unsigned char>(ds.begin(), ds.end()));
        }
        if (final_script_sig.empty()) {
            for (const auto& kv : partial_sigs) {
                std::vector<unsigned char> pub(kv.second.first.begin(), kv.second.first.end());
                write_kv(PSBT_IN_PARTIAL_SIG, pub, kv.second.second);
            }
            if (sighash_type > 0) {
                CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
                ds << static_cast<uint32_t>(sighash_type);
                write_kv(PSBT_IN_SIGHASH, {}, std::vector<unsigned char>(ds.begin(), ds.end()));
            }
            if (!redeem_script.empty()) {
                write_kv(PSBT_IN_REDEEMSCRIPT, {}, std::vector<unsigned char>(redeem_script.begin(), redeem_script.end()));
            }
            for (const auto& kv : hd_keypaths) {
                std::vector<unsigned char> pub(kv.second.first.begin(), kv.second.first.end());
                CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
                ds.write(reinterpret_cast<const char*>(kv.second.second.fingerprint), sizeof(kv.second.second.fingerprint));
                for (uint32_t idx : kv.second.second.path) ds << idx;
                write_kv(PSBT_IN_BIP32_DERIVATION, pub, std::vector<unsigned char>(ds.begin(), ds.end()));
            }
            if (!tap_key_sig.empty()) {
                write_kv(PSBT_IN_TAP_KEY_SIG, {}, tap_key_sig);
            }
            if (!tap_internal_key.IsNull()) {
                write_kv(PSBT_IN_TAP_INTERNAL_KEY, {}, std::vector<unsigned char>(tap_internal_key.begin(), tap_internal_key.end()));
            }
        } else {
            write_kv(PSBT_IN_SCRIPTSIG, {}, std::vector<unsigned char>(final_script_sig.begin(), final_script_sig.end()));
        }
        for (const auto& kv : unknown) {
            s << kv.first << kv.second;
        }
        s << PSBT_SEPARATOR;
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        std::set<std::vector<unsigned char>> key_lookup;
        std::vector<unsigned char> key;
        while (true) {
            s >> key;
            if (key.empty()) break;
            if (!key_lookup.insert(key).second) {
                throw std::ios_base::failure("Duplicate Key in PSBTInput");
            }
            std::vector<unsigned char> val;
            s >> val;
            switch (key[0]) {
                case PSBT_IN_NON_WITNESS_UTXO: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_IN_NON_WITNESS_UTXO key");
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    CMutableTransaction mtx;
                    ds >> mtx;
                    non_witness_utxo = CTransaction(mtx);
                    has_non_witness_utxo = true;
                    break;
                }
                case PSBT_IN_WITNESS_UTXO: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_IN_WITNESS_UTXO key");
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    ds >> witness_utxo;
                    break;
                }
                case PSBT_IN_PARTIAL_SIG: {
                    CPubKey pub(key.begin() + 1, key.end());
                    if (!pub.IsValid()) throw std::ios_base::failure("Invalid pubkey in PSBT_IN_PARTIAL_SIG");
                    partial_sigs[pub.GetID()] = std::make_pair(pub, val);
                    break;
                }
                case PSBT_IN_SIGHASH: {
                    if (key.size() != 1 || val.size() != 4) throw std::ios_base::failure("Invalid PSBT_IN_SIGHASH");
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    uint32_t sh = 0;
                    ds >> sh;
                    sighash_type = static_cast<int>(sh);
                    break;
                }
                case PSBT_IN_REDEEMSCRIPT: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_IN_REDEEMSCRIPT key");
                    redeem_script.assign(val.begin(), val.end());
                    break;
                }
                case PSBT_IN_BIP32_DERIVATION: {
                    CPubKey pub(key.begin() + 1, key.end());
                    if (!pub.IsValid() || val.size() < 4 || (val.size() % 4) != 0) {
                        throw std::ios_base::failure("Invalid PSBT_IN_BIP32_DERIVATION");
                    }
                    KeyOriginInfo origin;
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    ds.read(reinterpret_cast<char*>(origin.fingerprint), sizeof(origin.fingerprint));
                    while (!ds.empty()) {
                        uint32_t idx = 0;
                        ds >> idx;
                        origin.path.push_back(idx);
                    }
                    hd_keypaths[pub.GetID()] = std::make_pair(pub, origin);
                    break;
                }
                case PSBT_IN_SCRIPTSIG: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_IN_SCRIPTSIG key");
                    final_script_sig.assign(val.begin(), val.end());
                    break;
                }
                case PSBT_IN_TAP_KEY_SIG: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_IN_TAP_KEY_SIG key");
                    tap_key_sig = val;
                    break;
                }
                case PSBT_IN_TAP_INTERNAL_KEY: {
                    if (key.size() != 1 || val.size() != 32) throw std::ios_base::failure("Invalid PSBT_IN_TAP_INTERNAL_KEY");
                    tap_internal_key = uint256(val);
                    break;
                }
                default:
                    unknown[key] = val;
                    break;
            }
        }
    }
};

struct PSBTOutput {
    CScript redeem_script;
    std::map<CKeyID, std::pair<CPubKey, KeyOriginInfo>> hd_keypaths;
    uint256 tap_internal_key;
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown;

    bool IsNull() const;
    void Merge(const PSBTOutput& output);

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        auto write_kv = [&](uint8_t type, const std::vector<unsigned char>& key_data, const std::vector<unsigned char>& val) {
            std::vector<unsigned char> key;
            key.push_back(type);
            key.insert(key.end(), key_data.begin(), key_data.end());
            s << key << val;
        };
        if (!redeem_script.empty()) {
            write_kv(PSBT_OUT_REDEEMSCRIPT, {}, std::vector<unsigned char>(redeem_script.begin(), redeem_script.end()));
        }
        for (const auto& kv : hd_keypaths) {
            std::vector<unsigned char> pub(kv.second.first.begin(), kv.second.first.end());
            CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
            ds.write(reinterpret_cast<const char*>(kv.second.second.fingerprint), sizeof(kv.second.second.fingerprint));
            for (uint32_t idx : kv.second.second.path) ds << idx;
            write_kv(PSBT_OUT_BIP32_DERIVATION, pub, std::vector<unsigned char>(ds.begin(), ds.end()));
        }
        if (!tap_internal_key.IsNull()) {
            write_kv(PSBT_OUT_TAP_INTERNAL_KEY, {}, std::vector<unsigned char>(tap_internal_key.begin(), tap_internal_key.end()));
        }
        for (const auto& kv : unknown) {
            s << kv.first << kv.second;
        }
        s << PSBT_SEPARATOR;
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        std::set<std::vector<unsigned char>> key_lookup;
        std::vector<unsigned char> key;
        while (true) {
            s >> key;
            if (key.empty()) break;
            if (!key_lookup.insert(key).second) {
                throw std::ios_base::failure("Duplicate Key in PSBTOutput");
            }
            std::vector<unsigned char> val;
            s >> val;
            switch (key[0]) {
                case PSBT_OUT_REDEEMSCRIPT: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_OUT_REDEEMSCRIPT key");
                    redeem_script.assign(val.begin(), val.end());
                    break;
                }
                case PSBT_OUT_BIP32_DERIVATION: {
                    CPubKey pub(key.begin() + 1, key.end());
                    if (!pub.IsValid() || val.size() < 4 || (val.size() % 4) != 0) {
                        throw std::ios_base::failure("Invalid PSBT_OUT_BIP32_DERIVATION");
                    }
                    KeyOriginInfo origin;
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    ds.read(reinterpret_cast<char*>(origin.fingerprint), sizeof(origin.fingerprint));
                    while (!ds.empty()) {
                        uint32_t idx = 0;
                        ds >> idx;
                        origin.path.push_back(idx);
                    }
                    hd_keypaths[pub.GetID()] = std::make_pair(pub, origin);
                    break;
                }
                case PSBT_OUT_TAP_INTERNAL_KEY: {
                    if (key.size() != 1 || val.size() != 32) throw std::ios_base::failure("Invalid PSBT_OUT_TAP_INTERNAL_KEY");
                    tap_internal_key = uint256(val);
                    break;
                }
                default:
                    unknown[key] = val;
                    break;
            }
        }
    }
};

struct PartiallySignedTransaction {
    CMutableTransaction tx;
    bool has_tx{false};
    uint32_t m_version{0};
    std::vector<PSBTInput> inputs;
    std::vector<PSBTOutput> outputs;
    std::map<std::vector<unsigned char>, std::vector<unsigned char>> unknown;

    PartiallySignedTransaction() = default;
    explicit PartiallySignedTransaction(const CMutableTransaction& tx_in);

    bool IsNull() const { return !has_tx && inputs.empty() && outputs.empty() && unknown.empty(); }
    bool Merge(const PartiallySignedTransaction& psbt);
    bool IsSane() const;
    bool GetInputUTXO(CTxOut& utxo, int input_index) const;

    template <typename Stream>
    inline void Serialize(Stream& s) const {
        s.write(reinterpret_cast<const char*>(PSBT_MAGIC_BYTES), sizeof(PSBT_MAGIC_BYTES));
        if (has_tx) {
            std::vector<unsigned char> key{PSBT_GLOBAL_UNSIGNED_TX};
            CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
            ds << tx;
            s << key << std::vector<unsigned char>(ds.begin(), ds.end());
        }
        if (m_version > 0) {
            std::vector<unsigned char> key{PSBT_GLOBAL_VERSION};
            CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
            ds << m_version;
            s << key << std::vector<unsigned char>(ds.begin(), ds.end());
        }
        for (const auto& kv : unknown) {
            s << kv.first << kv.second;
        }
        s << PSBT_SEPARATOR;

        for (const PSBTInput& input : inputs) s << input;
        for (const PSBTOutput& output : outputs) s << output;
    }

    template <typename Stream>
    inline void Unserialize(Stream& s) {
        uint8_t magic[sizeof(PSBT_MAGIC_BYTES)];
        s.read(reinterpret_cast<char*>(magic), sizeof(magic));
        if (std::memcmp(magic, PSBT_MAGIC_BYTES, sizeof(magic)) != 0) {
            throw std::ios_base::failure("Invalid PSBT magic bytes");
        }

        std::set<std::vector<unsigned char>> key_lookup;
        std::vector<unsigned char> key;
        while (true) {
            s >> key;
            if (key.empty()) break;
            if (!key_lookup.insert(key).second) {
                throw std::ios_base::failure("Duplicate Key in PSBT global map");
            }
            std::vector<unsigned char> val;
            s >> val;
            switch (key[0]) {
                case PSBT_GLOBAL_UNSIGNED_TX: {
                    if (key.size() != 1) throw std::ios_base::failure("Invalid PSBT_GLOBAL_UNSIGNED_TX key");
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    ds >> tx;
                    has_tx = true;
                    for (const CTxIn& txin : tx.vin) {
                        if (!txin.scriptSig.empty()) {
                            throw std::ios_base::failure("Unsigned tx has non-empty scriptSig");
                        }
                    }
                    break;
                }
                case PSBT_GLOBAL_VERSION: {
                    if (key.size() != 1 || val.size() != 4) throw std::ios_base::failure("Invalid PSBT_GLOBAL_VERSION");
                    CDataStream ds(val, SER_NETWORK, PROTOCOL_VERSION);
                    ds >> m_version;
                    if (m_version > PSBT_HIGHEST_VERSION) {
                        throw std::ios_base::failure("Unsupported PSBT version");
                    }
                    break;
                }
                default:
                    unknown[key] = val;
                    break;
            }
        }
        if (!has_tx) {
            throw std::ios_base::failure("PSBT missing global unsigned transaction");
        }

        inputs.resize(tx.vin.size());
        for (size_t i = 0; i < tx.vin.size(); ++i) s >> inputs[i];
        outputs.resize(tx.vout.size());
        for (size_t i = 0; i < tx.vout.size(); ++i) s >> outputs[i];
    }
};

bool DecodeBase64PSBT(PartiallySignedTransaction& psbt, const std::string& base64_tx, std::string& error);
std::string EncodeBase64PSBT(const PartiallySignedTransaction& psbt);

bool FinalizePSBTInput(const PartiallySignedTransaction& psbt, size_t index, SignatureData& sigdata);
bool FinalizePSBT(PartiallySignedTransaction& psbt);
bool FinalizeAndExtractPSBT(PartiallySignedTransaction& psbt, CMutableTransaction& result);
bool SignPSBTInput(const CKeyStore& keystore, PartiallySignedTransaction& psbt, size_t index, int sighash = SIGHASH_ALL, SignatureData* out_sigdata = nullptr);

#endif // BITCOIN_PSBT_H
