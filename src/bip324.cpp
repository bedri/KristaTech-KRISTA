// Copyright (c) 2023-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip324.h"

#include "crypto/hmac_sha256.h"
#include "random.h"
#include "secp256k1.h"
#include "secp256k1_ellswift.h"
#include "support/cleanse.h"

#include <cassert>
#include <cstring>

namespace {

secp256k1_context* GetBIP324Secp256k1Context()
{
    static secp256k1_context* ctx = []() {
        secp256k1_context* c = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
        assert(c != nullptr);
        uint8_t seed32[32];
        GetStrongRandBytes(seed32, sizeof(seed32));
        (void)secp256k1_context_randomize(c, seed32);
        memory_cleanse(seed32, sizeof(seed32));
        return c;
    }();
    return ctx;
}

void HKDF_SHA256_Extract(const uint8_t* salt, size_t salt_len, const uint8_t* ikm, size_t ikm_len, uint8_t prk[32])
{
    CHMAC_SHA256(salt, salt_len).Write(ikm, ikm_len).Finalize(prk);
}

void HKDF_SHA256_Expand32(const uint8_t prk[32], const std::string& info, uint8_t out[32])
{
    const uint8_t one = 1;
    CHMAC_SHA256(prk, 32)
        .Write(reinterpret_cast<const uint8_t*>(info.data()), info.size())
        .Write(&one, 1)
        .Finalize(out);
}

} // namespace

constexpr size_t BIP324Cipher::ELLSWIFT_PUBKEY_LEN;
constexpr size_t BIP324Cipher::GARBAGE_TERMINATOR_LEN;
constexpr size_t BIP324Cipher::SESSION_ID_LEN;
constexpr size_t BIP324Cipher::LENGTH_LEN;
constexpr size_t BIP324Cipher::HEADER_LEN;
constexpr size_t BIP324Cipher::EXPANSION;
constexpr uint8_t BIP324Cipher::IGNORE_BIT;

BIP324Cipher::BIP324Cipher() = default;

BIP324Cipher::~BIP324Cipher()
{
    memory_cleanse(m_privkey.data(), m_privkey.size());
}

bool BIP324Cipher::GenerateKey(const CMessageHeader::MessageStartChars* pAvoidMagic)
{
    uint8_t ent32[32];
    bool ok = false;
    do {
        GetStrongRandBytes(m_privkey.data(), m_privkey.size());
        GetStrongRandBytes(ent32, sizeof(ent32));
        ok = secp256k1_ellswift_create(GetBIP324Secp256k1Context(), m_our_pubkey.data(), m_privkey.data(), ent32) == 1;
        if (ok && pAvoidMagic && std::memcmp(m_our_pubkey.data(), *pAvoidMagic, MESSAGE_START_SIZE) == 0) {
            ok = false;
        }
    } while (!ok);
    memory_cleanse(ent32, sizeof(ent32));
    return true;
}

bool BIP324Cipher::Initialize(const uint8_t their_pubkey[ELLSWIFT_PUBKEY_LEN], bool initiator, const CMessageHeader::MessageStartChars& magic)
{
    const uint8_t* ell_a64 = initiator ? m_our_pubkey.data() : their_pubkey;
    const uint8_t* ell_b64 = initiator ? their_pubkey : m_our_pubkey.data();
    const int party = initiator ? 0 : 1;

    uint8_t ecdh_secret[32];
    if (secp256k1_ellswift_xdh(
            GetBIP324Secp256k1Context(),
            ecdh_secret,
            ell_a64,
            ell_b64,
            m_privkey.data(),
            party,
            secp256k1_ellswift_xdh_hash_function_bip324,
            nullptr) != 1) {
        memory_cleanse(ecdh_secret, sizeof(ecdh_secret));
        return false;
    }

    std::string salt = "bitcoin_v2_shared_secret";
    salt.append(reinterpret_cast<const char*>(magic), MESSAGE_START_SIZE);

    uint8_t prk[32];
    HKDF_SHA256_Extract(reinterpret_cast<const uint8_t*>(salt.data()), salt.size(), ecdh_secret, sizeof(ecdh_secret), prk);
    memory_cleanse(ecdh_secret, sizeof(ecdh_secret));

    uint8_t init_l[32], init_p[32], resp_l[32], resp_p[32], garbage_key[32];
    HKDF_SHA256_Expand32(prk, "initiator_L", init_l);
    HKDF_SHA256_Expand32(prk, "initiator_P", init_p);
    HKDF_SHA256_Expand32(prk, "responder_L", resp_l);
    HKDF_SHA256_Expand32(prk, "responder_P", resp_p);
    HKDF_SHA256_Expand32(prk, "garbage_terminators", garbage_key);
    HKDF_SHA256_Expand32(prk, "session_id", m_session_id.data());
    memory_cleanse(prk, sizeof(prk));

    if (initiator) {
        m_send_l_cipher.reset(new FSChaCha20(init_l));
        m_send_p_cipher.reset(new FSChaCha20Poly1305(init_p));
        m_recv_l_cipher.reset(new FSChaCha20(resp_l));
        m_recv_p_cipher.reset(new FSChaCha20Poly1305(resp_p));
        std::memcpy(m_send_garbage_terminator.data(), garbage_key, GARBAGE_TERMINATOR_LEN);
        std::memcpy(m_recv_garbage_terminator.data(), garbage_key + GARBAGE_TERMINATOR_LEN, GARBAGE_TERMINATOR_LEN);
    } else {
        m_send_l_cipher.reset(new FSChaCha20(resp_l));
        m_send_p_cipher.reset(new FSChaCha20Poly1305(resp_p));
        m_recv_l_cipher.reset(new FSChaCha20(init_l));
        m_recv_p_cipher.reset(new FSChaCha20Poly1305(init_p));
        std::memcpy(m_send_garbage_terminator.data(), garbage_key + GARBAGE_TERMINATOR_LEN, GARBAGE_TERMINATOR_LEN);
        std::memcpy(m_recv_garbage_terminator.data(), garbage_key, GARBAGE_TERMINATOR_LEN);
    }

    memory_cleanse(init_l, sizeof(init_l));
    memory_cleanse(init_p, sizeof(init_p));
    memory_cleanse(resp_l, sizeof(resp_l));
    memory_cleanse(resp_p, sizeof(resp_p));
    memory_cleanse(garbage_key, sizeof(garbage_key));
    memory_cleanse(m_privkey.data(), m_privkey.size());

    m_initialized = true;
    return true;
}

bool BIP324Cipher::Encrypt(const std::vector<uint8_t>& contents, const std::vector<uint8_t>& aad, bool ignore, std::vector<uint8_t>& output)
{
    if (!m_initialized || !m_send_l_cipher || !m_send_p_cipher || contents.size() > 0xFFFFFFU) {
        return false;
    }
    const uint32_t plain_len = static_cast<uint32_t>(contents.size());
    uint8_t len_bytes[LENGTH_LEN] = {
        static_cast<uint8_t>(plain_len & 0xff),
        static_cast<uint8_t>((plain_len >> 8) & 0xff),
        static_cast<uint8_t>((plain_len >> 16) & 0xff)
    };

    output.resize(EXPANSION + contents.size());
    m_send_l_cipher->Crypt(len_bytes, output.data(), LENGTH_LEN);

    std::vector<uint8_t> plain(HEADER_LEN + contents.size());
    plain[0] = ignore ? IGNORE_BIT : 0;
    if (!contents.empty()) {
        std::memcpy(plain.data() + HEADER_LEN, contents.data(), contents.size());
    }

    m_send_p_cipher->Encrypt(
        plain.data(), plain.size(),
        aad.empty() ? nullptr : aad.data(), aad.size(),
        output.data() + LENGTH_LEN);
    memory_cleanse(plain.data(), plain.size());
    return true;
}

uint32_t BIP324Cipher::DecryptLength(const uint8_t input[LENGTH_LEN])
{
    uint8_t len_bytes[LENGTH_LEN];
    m_recv_l_cipher->Crypt(input, len_bytes, LENGTH_LEN);
    const uint32_t out = static_cast<uint32_t>(len_bytes[0]) |
           (static_cast<uint32_t>(len_bytes[1]) << 8) |
           (static_cast<uint32_t>(len_bytes[2]) << 16);
    memory_cleanse(len_bytes, sizeof(len_bytes));
    return out;
}

bool BIP324Cipher::Decrypt(const uint8_t* input, size_t len, const std::vector<uint8_t>& aad, bool& ignore, std::vector<uint8_t>& contents)
{
    if (!m_recv_p_cipher || len < HEADER_LEN + FSChaCha20Poly1305::EXPANSION) return false;
    uint8_t header_byte = 0;
    if (!m_recv_p_cipher->DecryptPacket(input, len, aad.empty() ? nullptr : aad.data(), aad.size(), header_byte, contents)) {
        return false;
    }
    ignore = (header_byte & IGNORE_BIT) != 0;
    memory_cleanse(&header_byte, sizeof(header_byte));
    return true;
}
