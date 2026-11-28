// Copyright (c) 2026 The KristaTech Core Developers
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
struct PinnedBLSEntry {
    CBLSPubKey blsPubKey;
    int nHeight;
    uint256 blockHash;
    PinnedBLSEntry() : nHeight(-1) {}
    PinnedBLSEntry(const CBLSPubKey& pkIn, int hIn, const uint256& hashIn)
        : blsPubKey(pkIn), nHeight(hIn), blockHash(hashIn) {}
};
std::mutex g_pinned_bls_mutex;
std::map<CPubKey, PinnedBLSEntry> g_mapPinnedBLSPubKeys;
} // namespace

const std::string BLS_DST = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_";

const std::string& GetBLSDomainTag(BLSDomain domain)
{
    static const std::string kPopDst = "BLS_POP_BLS12381G2_XMD:SHA-256_SSWU_RO_POP_KRISTA_DIP3_POP_V1_";
    if (domain == BLSDomain::PROTX_POP) {
        return kPopDst;
    }
    static const std::string kAdamVrfDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_ADAM_VRF_V1_";
    static const std::string kAdamCoordDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_ADAM_COORD_V1_";
    static const std::string kAdamSolDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_ADAM_SOL_V1_";
    static const std::string kLlmqBlockDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_LLMQ_BLOCK_V1_";
    static const std::string kLlmqTreasuryDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_LLMQ_TREASURY_V1_";
    static const std::string kPosBlockDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_POS_BLOCK_V1_";
    static const std::string kPosCoordReqDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_POS_COORD_REQ_V1_";
    static const std::string kProUpServDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_PROTX_UPSERV_V1_";
    static const std::string kProUpRevDst = "BLS_SIG_BLS12381G2_XMD:SHA-256_SSWU_RO_NUL_KRISTA_PROTX_UPREV_V1_";
    switch (domain) {
        case BLSDomain::ADAM_VRF: return kAdamVrfDst;
        case BLSDomain::ADAM_COORD: return kAdamCoordDst;
        case BLSDomain::ADAM_SOL: return kAdamSolDst;
        case BLSDomain::LLMQ_BLOCK: return kLlmqBlockDst;
        case BLSDomain::LLMQ_TREASURY: return kLlmqTreasuryDst;
        case BLSDomain::POS_BLOCK: return kPosBlockDst;
        case BLSDomain::POS_COORD_REQ: return kPosCoordReqDst;
        case BLSDomain::PROTX_UPSERV: return kProUpServDst;
        case BLSDomain::PROTX_UPREV: return kProUpRevDst;
        case BLSDomain::LEGACY_DEFAULT:
        default:
            return BLS_DST;
    }
}

// --- CBLSSecretKey ---

CBLSSecretKey::CBLSSecretKey() : fValid(false) {
    Reset();
}

CBLSSecretKey::~CBLSSecretKey() {
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
    memory_cleanse(&sk, sizeof(sk));
    memory_cleanse(seed, sizeof(seed));
    fValid = true;
}

bool CBLSSecretKey::SetBuf(const unsigned char* pbegin, size_t size) {
    if (size != 32 || pbegin == nullptr) {
        Reset();
        return false;
    }
    memcpy(data, pbegin, 32);
    blst::blst_scalar sc;
    blst::blst_scalar_from_bendian(&sc, data);
    const bool validScalar = blst::blst_sk_check(&sc);
    memory_cleanse(&sc, sizeof(sc));
    if (!validScalar) {
        Reset();
        return false;
    }
    fValid = true;
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
    memory_cleanse(&sk, sizeof(sk));
    fValid = true;
}

bool CBLSPubKey::SetBuf(const unsigned char* pbegin, size_t size) {
    if (size != 48) {
        Reset();
        return false;
    }
    memcpy(data, pbegin, 48);
    
    // Quick validation: deserialize and verify non-infinity valid G1 subgroup point
    try {
        blst::P1_Affine pk_affine(data, 48);
        if (!pk_affine.is_inf() && pk_affine.on_curve() && pk_affine.in_group()) {
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
    
    // Quick validation: deserialize and verify non-infinity valid G2 subgroup point
    try {
        blst::P2_Affine sig_affine(data, 96);
        if (!sig_affine.is_inf() && sig_affine.on_curve() && sig_affine.in_group()) {
            fValid = true;
        } else {
            Reset();
        }
    } catch (...) {
        Reset();
    }
    return fValid;
}

bool CBLSSignature::Sign(const CBLSSecretKey& sk, const uint256& hash, const std::string& dst) {
    if (!sk.IsValid()) return false;
    
    try {
        blst::SecretKey bls_sk;
        bls_sk.from_bendian(sk.begin());
        
        blst::P2 sig;
        sig.hash_to((const unsigned char*)hash.begin(), 32, dst);
        sig.sign_with(bls_sk);
        sig.compress(data);
        memory_cleanse(&bls_sk, sizeof(bls_sk));
        fValid = true;
        return true;
    } catch (...) {
        Reset();
        return false;
    }
}

bool CBLSSignature::Verify(const CBLSPubKey& pk, const uint256& hash, const std::string& dst) const {
    if (!fValid || !pk.IsValid()) return false;
    
    try {
        blst::P2_Affine sig_affine(data, 96);
        blst::P1_Affine pk_affine(pk.begin(), 48);
        
        blst::BLST_ERROR err = sig_affine.core_verify(pk_affine, true, (const unsigned char*)hash.begin(), 32, dst);
        return err == blst::BLST_SUCCESS;
    } catch (...) {
        return false;
    }
}

bool CBLSSignature::SignPoP(const CBLSSecretKey& sk) {
    if (!sk.IsValid()) return false;
    const CBLSPubKey pk(sk);
    if (!pk.IsValid()) return false;
    try {
        blst::SecretKey bls_sk;
        bls_sk.from_bendian(sk.begin());

        blst::P2 sig;
        sig.hash_to(pk.begin(), 48, GetBLSDomainTag(BLSDomain::PROTX_POP));
        sig.sign_with(bls_sk);
        sig.compress(data);
        memory_cleanse(&bls_sk, sizeof(bls_sk));
        fValid = true;
        return true;
    } catch (...) {
        Reset();
        return false;
    }
}

bool CBLSSignature::VerifyPoP(const CBLSPubKey& pk) const {
    if (!fValid || !pk.IsValid()) return false;
    try {
        blst::P2_Affine sig_affine(data, 96);
        blst::P1_Affine pk_affine(pk.begin(), 48);

        blst::BLST_ERROR err = sig_affine.core_verify(pk_affine, true, pk.begin(), 48, GetBLSDomainTag(BLSDomain::PROTX_POP));
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
            if (!sig.IsValid()) {
                out.Reset();
                return out;
            }
            blst::P2_Affine sig_affine(sig.begin(), 96);
            if (first) {
                agg_sig = blst::P2(sig_affine);
                first = false;
            } else {
                agg_sig.aggregate(sig_affine);
            }
        }
        if (!first) {
            unsigned char aggBytes[96];
            agg_sig.compress(aggBytes);
            out.SetBuf(aggBytes, sizeof(aggBytes));
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
            if (!pk.IsValid()) {
                out.Reset();
                return out;
            }
            blst::P1_Affine pk_affine(pk.begin(), 48);
            if (first) {
                agg_pk = blst::P1(pk_affine);
                first = false;
            } else {
                agg_pk.aggregate(pk_affine);
            }
        }
        if (!first) {
            unsigned char aggBytes[48];
            agg_pk.compress(aggBytes);
            out.SetBuf(aggBytes, sizeof(aggBytes));
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
        memory_cleanse(&sk, sizeof(sk));
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

bool SignBLSWithECDSAFallback(const uint256& hash, const CKey& ecdsaKey, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut, const std::string& dst) {
    if (!ecdsaKey.IsValid() || !blsKey.IsValid()) return false;

    CBLSSignedData signedData;
    signedData.blsPubKey = CBLSPubKey(blsKey);
    
    // Sign hash using BLS
    if (!signedData.blsSig.Sign(blsKey, hash, dst)) return false;

    // Sign the BLS public key using the ECDSA private key to prove ownership/authorization
    uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
    if (!ecdsaKey.Sign(hashPubKey, signedData.ecdsaSig)) return false;

    // Serialize signedData into vchSigOut
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << signedData;
    vchSigOut.assign(ss.begin(), ss.end());
    return true;
}

bool SignBLSOperatorOnly(const uint256& hash, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut, const std::string& dst) {
    if (!blsKey.IsValid()) return false;

    CBLSSignedData signedData;
    signedData.blsPubKey = CBLSPubKey(blsKey);
    if (!signedData.blsPubKey.IsValid()) return false;
    if (!signedData.blsSig.Sign(blsKey, hash, dst)) return false;
    signedData.ecdsaSig.clear();

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << signedData;
    vchSigOut.assign(ss.begin(), ss.end());
    return true;
}

bool HasExactBLSSignedDataEncoding(const std::vector<unsigned char>& vchData) {
    if (vchData.empty()) return false;
    try {
        CDataStream ss(vchData, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        return ss.empty();
    } catch (...) {
        return false;
    }
}

bool VerifyDip3OperatorBLS(const uint256& hash, const CBLSPubKey& expectedOperatorBlsPubKey, const std::vector<unsigned char>& vchSig, CBLSPubKey* pBlsPubKeyOut, CBLSSignature* pBlsSigOut, const std::string& dst) {
    if (vchSig.empty() || !expectedOperatorBlsPubKey.IsValid()) return false;
    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        if (!ss.empty()) {
            return false;
        }
        if (!signedData.ecdsaSig.empty()) {
            return false;
        }
        if (!signedData.blsPubKey.IsValid() || !signedData.blsSig.IsValid()) {
            return false;
        }
        if (signedData.blsPubKey != expectedOperatorBlsPubKey) {
            return false;
        }
        if (!signedData.blsSig.Verify(signedData.blsPubKey, hash, dst)) {
            return false;
        }
        if (pBlsPubKeyOut) *pBlsPubKeyOut = signedData.blsPubKey;
        if (pBlsSigOut) *pBlsSigOut = signedData.blsSig;
        return true;
    } catch (...) {
    }
    return false;
}

bool VerifySelfContainedBLSSignedData(const uint256& hash, const std::vector<unsigned char>& vchSig, CBLSPubKey& blsPubKeyOut, const std::string& dst, bool fRequireEmptyEcdsa) {
    if (vchSig.empty()) return false;
    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        if (!ss.empty()) {
            return false;
        }
        if (fRequireEmptyEcdsa && !signedData.ecdsaSig.empty()) {
            return false;
        }
        if (!signedData.blsPubKey.IsValid() || !signedData.blsSig.IsValid()) {
            return false;
        }
        if (!signedData.blsSig.Verify(signedData.blsPubKey, hash, dst)) {
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
        if (!ss.empty()) {
            return false;
        }
        if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
            blsPubKeyOut = signedData.blsPubKey;
            return true;
        }
    } catch (...) {
    }
    return false;
}

bool GetPinnedBLSPubKey(const CPubKey& ecdsaPubKey, CBLSPubKey& blsPubKeyOut, const PinActivePredicate& fnIsPinActive) {
    if (!ecdsaPubKey.IsValid()) return false;
    PinnedBLSEntry entry;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
        auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
        if (it != g_mapPinnedBLSPubKeys.end() && it->second.blsPubKey.IsValid()) {
            entry = it->second;
            found = true;
        }
    }
    if (!found) {
        return false;
    }
    // Evaluate fork/branch activity predicate outside g_pinned_bls_mutex to prevent lock-order inversion with cs_main.
    if (fnIsPinActive && !fnIsPinActive(entry.nHeight, entry.blockHash)) {
        return false;
    }
    blsPubKeyOut = entry.blsPubKey;
    return true;
}

bool IsBLSPubKeyPinnedToOther(const CPubKey& ownerPubKey, const CBLSPubKey& blsPubKey, const PinActivePredicate& fnIsPinActive) {
    if (!blsPubKey.IsValid()) return false;
    std::vector<std::pair<int, uint256>> matches;
    {
        std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
        for (const auto& pair : g_mapPinnedBLSPubKeys) {
            if (pair.second.blsPubKey == blsPubKey && pair.first != ownerPubKey) {
                matches.emplace_back(pair.second.nHeight, pair.second.blockHash);
            }
        }
    }
    for (const auto& m : matches) {
        if (!fnIsPinActive || fnIsPinActive(m.first, m.second)) {
            return true;
        }
    }
    return false;
}

bool PinBLSPubKey(const CPubKey& ecdsaPubKey, const CBLSPubKey& blsPubKey, int nHeight, const uint256& blockHash, const PinActivePredicate& fnIsPinActive) {
    if (!ecdsaPubKey.IsValid() || !blsPubKey.IsValid()) return false;
    struct ConflictCandidate {
        CPubKey key;
        int nHeight;
        uint256 blockHash;
    };
    std::vector<ConflictCandidate> conflicts;
    bool hasSelf = false;
    PinnedBLSEntry selfEntry;
    {
        std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
        for (const auto& pair : g_mapPinnedBLSPubKeys) {
            if (pair.second.blsPubKey == blsPubKey && pair.first != ecdsaPubKey) {
                conflicts.push_back(ConflictCandidate{pair.first, pair.second.nHeight, pair.second.blockHash});
            }
        }
        auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
        if (it != g_mapPinnedBLSPubKeys.end()) {
            hasSelf = true;
            selfEntry = it->second;
        }
    }
    if (!fnIsPinActive) {
        if (!conflicts.empty()) return false;
        if (hasSelf) return selfEntry.blsPubKey == blsPubKey;
        std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
        for (const auto& pair : g_mapPinnedBLSPubKeys) {
            if (pair.second.blsPubKey == blsPubKey && pair.first != ecdsaPubKey) {
                return false;
            }
        }
        auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
        if (it != g_mapPinnedBLSPubKeys.end()) {
            return it->second.blsPubKey == blsPubKey;
        }
        g_mapPinnedBLSPubKeys.emplace(ecdsaPubKey, PinnedBLSEntry(blsPubKey, nHeight, blockHash));
        return true;
    }

    std::vector<ConflictCandidate> staleConflicts;
    for (const auto& c : conflicts) {
        if (fnIsPinActive(c.nHeight, c.blockHash)) {
            return false;
        }
        staleConflicts.push_back(c);
    }
    bool replaceStaleSelf = false;
    if (hasSelf) {
        if (fnIsPinActive(selfEntry.nHeight, selfEntry.blockHash)) {
            return selfEntry.blsPubKey == blsPubKey;
        }
        replaceStaleSelf = true;
    }

    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    for (const auto& c : staleConflicts) {
        auto itC = g_mapPinnedBLSPubKeys.find(c.key);
        if (itC != g_mapPinnedBLSPubKeys.end() && itC->second.blsPubKey == blsPubKey &&
            itC->second.nHeight == c.nHeight && itC->second.blockHash == c.blockHash) {
            g_mapPinnedBLSPubKeys.erase(itC);
        }
    }
    for (const auto& pair : g_mapPinnedBLSPubKeys) {
        if (pair.second.blsPubKey == blsPubKey && pair.first != ecdsaPubKey) {
            return false;
        }
    }
    auto it = g_mapPinnedBLSPubKeys.find(ecdsaPubKey);
    if (it != g_mapPinnedBLSPubKeys.end()) {
        if (replaceStaleSelf && it->second.nHeight == selfEntry.nHeight &&
            it->second.blockHash == selfEntry.blockHash && it->second.blsPubKey == selfEntry.blsPubKey) {
            it->second = PinnedBLSEntry(blsPubKey, nHeight, blockHash);
            return true;
        }
        return it->second.blsPubKey == blsPubKey;
    }
    g_mapPinnedBLSPubKeys.emplace(ecdsaPubKey, PinnedBLSEntry(blsPubKey, nHeight, blockHash));
    return true;
}

void UnpinBLSPubKeysAtHeight(int nHeight, const uint256& blockHash) {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    for (auto it = g_mapPinnedBLSPubKeys.begin(); it != g_mapPinnedBLSPubKeys.end(); ) {
        if (it->second.nHeight == nHeight &&
            (blockHash.IsNull() || it->second.blockHash.IsNull() || it->second.blockHash == blockHash)) {
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
    mapOut.clear();
    for (const auto& pair : g_mapPinnedBLSPubKeys) {
        mapOut.emplace(pair.first, std::make_pair(pair.second.blsPubKey, pair.second.nHeight));
    }
}

void ImportPinnedBLSPubKeysWithHashes(const std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapIn, const std::map<CPubKey, uint256>& mapBlockHashes, int nMaxHeight) {
    std::lock_guard<std::mutex> lock(g_pinned_bls_mutex);
    g_mapPinnedBLSPubKeys.clear();
    for (const auto& pair : mapIn) {
        if (pair.first.IsValid() && pair.second.first.IsValid() && pair.second.second <= nMaxHeight) {
            uint256 bh;
            auto itHash = mapBlockHashes.find(pair.first);
            if (itHash != mapBlockHashes.end()) {
                bh = itHash->second;
            }
            g_mapPinnedBLSPubKeys.emplace(pair.first, PinnedBLSEntry(pair.second.first, pair.second.second, bh));
        }
    }
}

void ImportPinnedBLSPubKeys(const std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapIn, int nMaxHeight) {
    ImportPinnedBLSPubKeysWithHashes(mapIn, std::map<CPubKey, uint256>(), nMaxHeight);
}

bool VerifyBLSWithECDSAFallback(const uint256& hash, const CPubKey& ecdsaPubKey, const std::vector<unsigned char>& vchSig, const std::string& dst, const PinActivePredicate& fnIsPinActive) {
    if (vchSig.empty() || !ecdsaPubKey.IsValid()) {
        return false;
    }

    try {
        CDataStream ss(vchSig, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignedData signedData;
        ss >> signedData;
        if (!ss.empty()) {
            return false;
        }

        // Verify BLS12-381 signature with secp256k1 ownership authorization and on-chain BLS key pinning
        if (signedData.blsPubKey.IsValid() && signedData.blsSig.IsValid()) {
            uint256 hashPubKey = Hash(signedData.blsPubKey.begin(), signedData.blsPubKey.end());
            if (!ecdsaPubKey.VerifyStrictDER(hashPubKey, signedData.ecdsaSig)) {
                return false;
            }
            CBLSPubKey pinnedBlsPubKey;
            if (GetPinnedBLSPubKey(ecdsaPubKey, pinnedBlsPubKey, fnIsPinActive)) {
                if (signedData.blsPubKey != pinnedBlsPubKey) {
                    LogPrintf("VerifyBLSWithECDSAFallback: Rejecting unpinned BLS pubkey for ECDSA key %s\n", ecdsaPubKey.GetID().ToString());
                    return false;
                }
            }
            if (signedData.blsSig.Verify(signedData.blsPubKey, hash, dst)) {
                return true;
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

