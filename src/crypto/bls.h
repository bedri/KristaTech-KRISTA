// Copyright (c) 2026 The KRISTA Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTA_CRYPTO_BLS_H
#define KRISTA_CRYPTO_BLS_H

#include "uint256.h"
#include "serialize.h"
#include <vector>
#include <string>
#include "key.h"
#include "pubkey.h"

// Global Domain Separation Tag (DST) for BLS signatures
extern const std::string BLS_DST;

/** Wrapper around BLS12-381 Secret Key (32 bytes) */
class CBLSSecretKey {
private:
    unsigned char data[32];
    bool fValid;

public:
    CBLSSecretKey();
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
            fValid = true; // Simple check, actual key validation can be done if needed
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
            fValid = true; // Will check validity during verify/actual usage or decompression
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

    bool Sign(const CBLSSecretKey& sk, const uint256& hash);
    bool Verify(const CBLSPubKey& pk, const uint256& hash) const;

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
            fValid = true;
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

bool SignBLSWithECDSAFallback(const uint256& hash, const CKey& ecdsaKey, const CBLSSecretKey& blsKey, std::vector<unsigned char>& vchSigOut);
bool VerifyBLSWithECDSAFallback(const uint256& hash, const CPubKey& ecdsaPubKey, const std::vector<unsigned char>& vchSig);
CBLSSecretKey DeriveBLSFromSeed(const unsigned char* seed, size_t seed_size);
CBLSSecretKey DeriveBLSFromCKey(const CKey& key);

#endif // KRISTA_CRYPTO_BLS_H
