// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/bls.h"
#include "random.h"
#include "blst.hpp"
#include "util.h"
#include "hash.h"
#include "streams.h"
#include "support/cleanse.h"
#include "utilstrencodings.h"
#include <map>
#include <mutex>

namespace {
std::mutex g_pinned_bls_mutex;
std::map<CPubKey, std::pair<CBLSPubKey, int>> g_mapPinnedBLSPubKeys;
} // namespace

const std::string BLS_DST = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_";

// --- CBLSSecretKey ---

CBLSSecretKey::CBLSSecretKey() : fValid(false) {
    Reset();
}

void CBLSSecretKey::Reset() {
    memory_cleanse(data, 32);
    fValid = false;
}

void CBLSSecretKey::MakeNewKey() {
    unsigned char seed[32];
    GetRandBytes(seed, 32);
    
    blst::SecretKey sk;
    sk.keygen(seed, 32);
    sk.to_bendian(data);
    memory_cleanse(seed, sizeof(seed));
    fValid = true;
}

bool CBLSSecretKey::SetBuf(const unsigned char* pbegin, size_t size) {
    if (size != 32) {
        Reset();
        return false;
    }
    memcpy(data, pbegin, 32);
    fValid = true; // Scalar validation is implicitly handled during key derivation/usage
    return true;
}

// --- CBLSPubKey ---

CBLSPubKey::CBLSPubKey() : fValid(false) {
    Reset();
}

void CBLSPubKey::Reset() {
    memset(data, 0, 48);
    fValid = false;
}

CBLSPubKey::CBLSPubKey(const CBLSSecretKey& secretKey) : fValid(false) {
    Reset();
    if (!secretKey.IsValid()) return;
    
    blst::SecretKey sk;
    sk.from_bendian(secretKey.begin());
    
    blst::P1 pk(sk);
    pk.compress(data);
    fValid = true;
}

bool CBLSPubKey::SetBuf(const unsigned char* pbegin, size_t size) {
    if (size != 48) {
        Reset();
        return false;
    }
    memcpy(data, pbegin, 48);
    
    // Quick validation: try to deserialize to verify it's a valid G1 point
    try {
        blst::P1_Affine pk_affine(data, 48);
        if (pk_affine.on_curve() && pk_affine.in_group()) {
            fValid = true;
        } else {
            Reset();
        }
    } catch (...) {
        Reset();
    }
    return fValid;
}

// --- CBLSSignature ---

CBLSSignature::CBLSSignature() : fValid(false) {
    Reset();
}

void CBLSSignature::Reset() {
    memset(data, 0, 96);
    fValid = false;
}

bool CBLSSignature::SetBuf(const unsigned char* pbegin, size_t size) {
    if (size != 96) {
        Reset();
        return false;
    }
    memcpy(data, pbegin, 96);
    
    // Quick validation: try to deserialize to verify it's a valid G2 point
    try {
        blst::P2_Affine sig_affine(data, 96);
        if (sig_affine.on_curve() && sig_affine.in_group()) {
            fValid = true;
        } else {
            Reset();
        }
    } catch (...) {
        Reset();
    }
    return fValid;
}

bool CBLSSignature::Sign(const CBLSSecretKey& sk, const uint256& hash) {
    if (!sk.IsValid()) return false;
    
    try {
        blst::SecretKey bls_sk;
        bls_sk.from_bendian(sk.begin());
        
        blst::P2 sig;
        sig.hash_to((const unsigned char*)hash.begin(), 32, BLS_DST);
        sig.sign_with(bls_sk);
        sig.compress(data);
        fValid = true;
        return true;
    } catch (...) {
        Reset();
        return false;
    }
}

bool CBLSSignature::Verify(const CBLSPubKey& pk, const uint256& hash) const {
    if (!fValid || !pk.IsValid()) return false;
    
    try {
        blst::P2_Affine sig_affine(data, 96);
        blst::P1_Affine pk_affine(pk.begin(), 48);
        
        blst::BLST_ERROR err = sig_affine.core_verify(pk_affine, true, (const unsigned char*)hash.begin(), 32, BLS_DST);
        return err == blst::BLST_SUCCESS;
    } catch (...) {
        return false;
    }
}

CBLSSignature CBLSSignature::Aggregate(const std::vector<CBLSSignature>& sigs) {
    CBLSSignature out;
    if (sigs.empty()) return out;
    
    try {
        blst::P2 agg_sig;
        bool first = true;
        for (const auto& sig : sigs) {
            if (!sig.IsValid()) continue;
            blst::P2_Affine sig_affine(sig.begin(), 96);
            if (first) {
                agg_sig = blst::P2(sig_affine);
                first = false;
            } else {
                agg_sig.aggregate(sig_affine);
            }
        }
        if (!first) {
            agg_sig.compress(out.data);
            out.fValid = true;
        }
    } catch (...) {
        out.Reset();
    }
    return out;
}

CBLSPubKey CBLSPubKey::Aggregate(const std::vector<CBLSPubKey>& pks) {
    CBLSPubKey out;
    if (pks.empty()) return out;
    
    try {
        blst::P1 agg_pk;
        bool first = true;
        for (const auto& pk : pks) {
            if (!pk.IsValid()) continue;
            blst::P1_Affine pk_affine(pk.begin(), 48);
            if (first) {
                agg_pk = blst::P1(pk_affine);
                first = false;
            } else {
                agg_pk.aggregate(pk_affine);
            }
        }
        if (!first) {
            agg_pk.compress(out.data);
            out.fValid = true;
        }
    } catch (...) {
        out.Reset();
    }
    return out;
}

CBLSSecretKey DeriveBLSFromSeed(const unsigned char* seed, size_t seed_size) {
    CBLSSecretKey blsKey;
    if (seed == nullptr || seed_size == 0) return blsKey;
    try {
        blst::SecretKey sk;
        sk.keygen(seed, seed_size);
        unsigned char data[32];
        sk.to_bendian(data);
        blsKey.SetBuf(data, 32);
        memory_cleanse(data, sizeof(data));
    } catch (...) {
        blsKey.Reset();
    }
    return blsKey;
}

CBLSSecretKey DeriveBLSFromCKey(const CKey& key) {
    if (!key.IsValid()) return CBLSSecretKey();
    return DeriveBLSFromSeed(key.begin(), 32);
}

CBLSSecretKey DeriveEphemeralBLSFromCKey(const CKey& key, const uint256& contextHash) {
    if (!key.IsValid()) return CBLSSecretKey();
    if (contextHash.IsNull()) return DeriveBLSFromCKey(key);
    CHashWriter ss(SER_GETHASH, 0);
    ss.write((const char*)key.begin(), 32);
    ss << contextHash;
    uint256 derivedSeed = ss.GetHash();
    CBLSSecretKey blsKey = DeriveBLSFromSeed(derivedSeed.begin(), 32);
    memory_cleanse(derivedSeed.begin(), 32);
    return blsKey;
}

bool SignBLSWithECDSAFallback(const uint256& hash, const CKey& ecdsaKey, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut) {
    if (!ecdsaKey.IsValid() || !blsKey.IsValid()) return false;

    CBLSSignedData signedData;
    signedData.blsPubKey = CBLSPubKey(blsKey);
    
    // Sign hash using BLS
    if (!signedData.blsSig.Sign(blsKey, hash)) return false;

    // Sign the BLS public key using the ECDSA private key to prove ownership/authorization
    uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
    if (!ecdsaKey.Sign(hashPubKey, signedData.ecdsaSig)) return false;

    // Serialize signedData into vchSigOut
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << signedData;
    vchSigOut.assign(ss.begin(), ss.end());
    return true;
}

bool VerifySelfContainedBLSSignedData(const uint256& hash, const std::vector<unsigned char>& vchSig, CBLSPubKey& blsPubKeyOut) {
    if (vchSig.empty()) return false;
    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        if (!signedData.blsPubKey.IsValid() || !signedData.blsSig.IsValid() || signedData.ecdsaSig.empty()) {
            return false;
        }
        if (!signedData.blsSig.Verify(signedData.blsPubKey, hash)) {
            return false;
        }
        blsPubKeyOut = signedData.blsPubKey;
        return true;
    } catch (...) {
    }
    return false;
}

bool ExtractBLSPubKeyFromSignedData(const std::vector<unsigned char>& vchSig, CBLSPubKey& blsPubKeyOut) {
    if (vchSig.empty()) return false;
    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
            blsPubKeyOut = signedData.blsPubKey;
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool GetPinnedBLSPubKey(const CPubKey& ecdsaPubKey, CBLSPubKey& blsPubKeyOut) {
    if (!ecdsaPubKey.IsValid()) return false;
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
    if (it != g_mapPinnedBLSPubKeys.end() && it->second.first.IsValid()) {
        blsPubKeyOut = it->second.first;
        return true;
    }
    return false;
}

bool PinBLSPubKey(const CPubKey& ecdsaPubKey, const CBLSPubKey& blsPubKey, int nHeight) {
    if (!ecdsaPubKey.IsValid() || !blsPubKey.IsValid()) return false;
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
    if (it != g_mapPinnedBLSPubKeys.end()) {
        return it->second.first == blsPubKey;
    }
    g_mapPinnedBLSPubKeys.emplace(ecdsaPubKey, std::make_pair(blsPubKey, nHeight));
    return true;
}

void UnpinBLSPubKeysAtHeight(int nHeight) {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    for (auto it = g_mapPinnedBLSPubKeys.begin(); it != g_mapPinnedBLSPubKeys.end(); ) {
        if (it->second.second == nHeight) {
            it = g_mapPinnedBLSPubKeys.erase(it);
        } else {
            ++it;
        }
    }
}

void ClearPinnedBLSPubKeys() {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    g_mapPinnedBLSPubKeys.clear();
}

void ExportPinnedBLSPubKeys(std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapOut) {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    mapOut = g_mapPinnedBLSPubKeys;
}

void ImportPinnedBLSPubKeys(const std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapIn, int nMaxHeight) {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    g_mapPinnedBLSPubKeys.clear();
    for (const auto& pair : mapIn) {
        if (pair.first.IsValid() && pair.second.first.IsValid() && pair.second.second <= nMaxHeight) {
            g_mapPinnedBLSPubKeys.emplace(pair.first, pair.second);
        }
    }
}

bool VerifyBLSWithECDSAFallback(const uint256& hash, const CPubKey& ecdsaPubKey, const std::vector<unsigned char>& vchSig) {
    if (vchSig.empty() || !ecdsaPubKey.IsValid()) {
        return false;
    }

    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;

        // Verify BLS12-381 signature with secp256k1 ownership authorization and on-chain BLS key pinning
        if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
            CBLSPubKey pinnedBlsPubKey;
            if (GetPinnedBLSPubKey(ecdsaPubKey, pinnedBlsPubKey)) {
                if (signedData.blsPubKey != pinnedBlsPubKey) {
                    LogPrintf("VerifyBLSWithECDSAFallback: Rejecting unpinned BLS pubkey for ECDSA key %s\n", ecdsaPubKey.GetID().ToString());
                    return false;
                }
            }
            if (signedData.blsSig.Verify(signedData.blsPubKey, hash)) {
                uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
                if (ecdsaPubKey.Verify(hashPubKey, signedData.ecdsaSig)) {
                    return true;
                }
            }
        }
        return false;
    } catch (const std::exception& e) {
        LogPrintf("VerifyBLSWithECDSAFallback: Exception: %s\n", e.what());
        return false;
    } catch (...) {
        LogPrintf("VerifyBLSWithECDSAFallback: Unknown exception!\n");
        return false;
    }
}

