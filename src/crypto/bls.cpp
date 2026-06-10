// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/bls.h"
#include "random.h"
#include "blst.hpp"
#include "util.h"
#include "hash.h"
#include "streams.h"
#include "utilstrencodings.h"

const std::string BLS_DST = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_";

// --- CBLSSecretKey ---

CBLSSecretKey::CBLSSecretKey() : fValid(false) {
    Reset();
}

void CBLSSecretKey::Reset() {
    memset(data, 0, 32);
    fValid = false;
}

void CBLSSecretKey::MakeNewKey() {
    unsigned char seed[32];
    GetRandBytes(seed, 32);
    
    blst::SecretKey sk;
    sk.keygen(seed, 32);
    sk.to_bendian(data);
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
    } catch (...) {
        blsKey.Reset();
    }
    return blsKey;
}

CBLSSecretKey DeriveBLSFromCKey(const CKey& key) {
    if (!key.IsValid()) return CBLSSecretKey();
    // Hash the ECDSA private key bytes to get a deterministic 32-byte seed
    uint256 seed = Hash(key.begin(), key.end());
    return DeriveBLSFromSeed(seed.begin(), 32);
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

bool VerifyBLSWithECDSAFallback(const uint256& hash, const CPubKey& ecdsaPubKey, const std::vector<unsigned char>& vchSig) {
    if (vchSig.empty() || !ecdsaPubKey.IsValid()) {
        LogPrintf("VerifyBLSWithECDSAFallback: Empty sig or invalid ecdsaPubKey! size=%d, valid=%d\n", vchSig.size(), ecdsaPubKey.IsValid());
        return false;
    }

    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;

        if (!signedData.blsPubKey.IsValid() || !signedData.blsSig.IsValid()) {
            LogPrintf("VerifyBLSWithECDSAFallback: Invalid blsPubKey=%d or blsSig=%d!\n", 
                signedData.blsPubKey.IsValid(), signedData.blsSig.IsValid());
            return false;
        }

        // 1. Verify ECDSA signature of BLS public key
        uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
        bool ecdsaVerify = ecdsaPubKey.Verify(hashPubKey, signedData.ecdsaSig);
        LogPrintf("VerifyBLSWithECDSAFallback: ecdsaPubKey=%s, hashPubKey=%s, ecdsaVerify=%d\n",
            ecdsaPubKey.GetID().ToString(), hashPubKey.ToString(), ecdsaVerify);
        if (!ecdsaVerify) {
            return false;
        }

        // 2. Verify BLS signature of hash
        bool blsVerify = signedData.blsSig.Verify(signedData.blsPubKey, hash);
        LogPrintf("VerifyBLSWithECDSAFallback: blsPubKey=%s, hash=%s, blsVerify=%d\n",
            HexStr(signedData.blsPubKey.begin(), signedData.blsPubKey.end()), hash.ToString(), blsVerify);
        return blsVerify;
    } catch (const std::exception& e) {
        LogPrintf("VerifyBLSWithECDSAFallback: Exception: %s\n", e.what());
        return false;
    } catch (...) {
        LogPrintf("VerifyBLSWithECDSAFallback: Unknown exception!\n");
        return false;
    }
}

