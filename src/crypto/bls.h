// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTA_CRYPTO_BLS_H
#define KRISTA_CRYPTO_BLS_H

#include "uint256.h"
#include "serialize.h"
#include <functional>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include "key.h"
#include "pubkey.h"

// Global Domain Separation Tag (DST) for legacy pre-Hardening_v3 BLS signatures
extern const std::string BLS_DST;

enum class BLSDomain {
    LEGACY_DEFAULT,
    ADAM_VRF,
    ADAM_COORD,
    ADAM_SOL,
    LLMQ_BLOCK,
    LLMQ_TREASURY,
    POS_BLOCK,
    POS_COORD_REQ,
    PROTX_UPSERV,
    PROTX_UPREV,
    PROTX_POP
};

const std::string& GetBLSDomainTag(BLSDomain domain, bool fHardeningV3);

/** Wrapper around BLS12-381 Secret Key (32 bytes) */
class CBLSSecretKey {
private:
    unsigned char data[32];
    bool fValid;

public:
    CBLSSecretKey();
    CBLSSecretKey(const CBLSSecretKey&) = default;
    CBLSSecretKey& operator=(const CBLSSecretKey&) = default;
    ~CBLSSecretKey();
    void MakeNewKey();
    bool SetBuf(const unsigned char* pbegin, size_t size);
    const unsigned char* begin() const { return data; }
    const unsigned char* end() const { return data + 32; }
    bool IsValid() const { return fValid; }
    void Reset();

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(FLATDATA(data));
        if (ser_action.ForRead()) {
            unsigned char tmp[32];
            memcpy(tmp, data, 32);
            SetBuf(tmp, 32);
            memory_cleanse(tmp, sizeof(tmp));
        }
    }
};

/** Wrapper around BLS12-381 G1 Public Key (48 bytes, compressed) */
class CBLSPubKey {
private:
    unsigned char data[48];
    bool fValid;

public:
    CBLSPubKey();
    CBLSPubKey(const CBLSSecretKey& secretKey);
    bool SetBuf(const unsigned char* pbegin, size_t size);
    const unsigned char* begin() const { return data; }
    const unsigned char* end() const { return data + 48; }
    bool IsValid() const { return fValid; }
    void Reset();

    static CBLSPubKey Aggregate(const std::vector<CBLSPubKey>& pks);

    bool operator==(const CBLSPubKey& rhs) const {
        return fValid == rhs.fValid && memcmp(data, rhs.data, 48) == 0;
    }
    bool operator!=(const CBLSPubKey& rhs) const {
        return !(*this == rhs);
    }
    bool operator<(const CBLSPubKey& rhs) const {
        return memcmp(data, rhs.data, 48) < 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(FLATDATA(data));
        if (ser_action.ForRead()) {
            unsigned char tmp[48];
            memcpy(tmp, data, 48);
            SetBuf(tmp, 48);
        }
    }
};

/** Wrapper around BLS12-381 G2 Signature (96 bytes, compressed) */
class CBLSSignature {
private:
    unsigned char data[96];
    bool fValid;

public:
    CBLSSignature();
    bool SetBuf(const unsigned char* pbegin, size_t size);
    const unsigned char* begin() const { return data; }
    const unsigned char* end() const { return data + 96; }
    bool IsValid() const { return fValid; }
    void Reset();

    bool Sign(const CBLSSecretKey& sk, const uint256& hash, const std::string& dst = BLS_DST);
    bool Verify(const CBLSPubKey& pk, const uint256& hash, const std::string& dst = BLS_DST) const;
    bool SignPoP(const CBLSSecretKey& sk);
    bool VerifyPoP(const CBLSPubKey& pk) const;

    // Aggregated operations
    static CBLSSignature Aggregate(const std::vector<CBLSSignature>& sigs);

    bool operator==(const CBLSSignature& rhs) const {
        return fValid == rhs.fValid && memcmp(data, rhs.data, 96) == 0;
    }
    bool operator!=(const CBLSSignature& rhs) const {
        return !(*this == rhs);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(FLATDATA(data));
        if (ser_action.ForRead()) {
            unsigned char tmp[96];
            memcpy(tmp, data, 96);
            SetBuf(tmp, 96);
        }
    }
};

struct CBLSSignedData {
    CBLSPubKey blsPubKey;
    CBLSSignature blsSig;
    std::vector<unsigned char> ecdsaSig;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blsPubKey);
        READWRITE(blsSig);
        READWRITE(ecdsaSig);
    }
};

bool SignBLSWithECDSAFallback(const uint256& hash, const CKey& ecdsaKey, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut, const std::string& dst = BLS_DST);
bool SignBLSOperatorOnly(const uint256& hash, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut, const std::string& dst = BLS_DST);
bool HasExactBLSSignedDataEncoding(const std::vector<unsigned char>& vchData);
using PinActivePredicate = std::function<bool(int, const uint256&)>;
bool VerifyBLSWithECDSAFallback(const uint256& hash, const CPubKey& ecdsaPubKey, const std::vector<unsigned char>& vchSig, const std::string& dst = BLS_DST, bool fStrictDER = false, const PinActivePredicate& fnIsPinActive = nullptr);
bool VerifyDip3OperatorBLS(const uint256& hash, const CBLSPubKey& expectedOperatorBlsPubKey, const std::vector<unsigned char>& vchSig, CBLSPubKey* pBlsPubKeyOut = nullptr, CBLSSignature* pBlsSigOut = nullptr, const std::string& dst = BLS_DST, bool fRequireEmptyEcdsa = false);
bool VerifySelfContainedBLSSignedData(const uint256& hash, const std::vector<unsigned char>& vchSig, CBLSPubKey& blsPubKeyOut, const std::string& dst = BLS_DST, bool fRequireEmptyEcdsa = false);
bool ExtractBLSPubKeyFromSignedData(const std::vector<unsigned char>& vchSig, CBLSPubKey& blsPubKeyOut);
bool GetPinnedBLSPubKey(const CPubKey& ecdsaPubKey, CBLSPubKey& blsPubKeyOut, const PinActivePredicate& fnIsPinActive = nullptr);
bool IsBLSPubKeyPinnedToOther(const CPubKey& ownerPubKey, const CBLSPubKey& blsPubKey, const PinActivePredicate& fnIsPinActive = nullptr);
bool PinBLSPubKey(const CPubKey& ecdsaPubKey, const CBLSPubKey& blsPubKey, int nHeight, const uint256& blockHash = uint256(), const PinActivePredicate& fnIsPinActive = nullptr);
void UnpinBLSPubKeysAtHeight(int nHeight, const uint256& blockHash = uint256());
void ClearPinnedBLSPubKeys();
void ExportPinnedBLSPubKeys(std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapOut);
void ImportPinnedBLSPubKeys(const std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapIn, int nMaxHeight);
void ImportPinnedBLSPubKeysWithHashes(const std::map<CPubKey, std::pair<CBLSPubKey, int>>& mapIn, const std::map<CPubKey, uint256>& mapBlockHashes, int nMaxHeight);
CBLSSecretKey DeriveBLSFromSeed(const unsigned char* seed, size_t seed_size);
CBLSSecretKey DeriveBLSFromCKey(const CKey& key);
CBLSSecretKey DeriveEphemeralBLSFromCKey(const CKey& key, const uint256& contextHash);

#endif // KRISTA_CRYPTO_BLS_H
