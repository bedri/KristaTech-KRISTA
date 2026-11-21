// Copyright (c) 2023-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CRYPTO_CHACHA20POLY1305_H
#define BITCOIN_CRYPTO_CHACHA20POLY1305_H

#include <array>
#include <cstdint>
#include <vector>
#include <cstddef>
#include <vector>

/** RFC 8439 ChaCha20 with 32-byte key and 96-bit (12-byte) nonce. */
class ChaCha20Nonce96
{
private:
    uint32_t state[16]{};

public:
    ChaCha20Nonce96() = default;
    ~ChaCha20Nonce96();
    void SetKey(const uint8_t key[32]);
    void Crypt(const uint8_t nonce[12], uint32_t counter, const uint8_t* in, uint8_t* out, size_t len) const;
};

/** RFC 8439 Poly1305 one-time authenticator. */
class Poly1305
{
public:
    static constexpr size_t TAGLEN = 16;

    explicit Poly1305(const uint8_t key[32]);
    ~Poly1305();
    void Update(const uint8_t* data, size_t len);
    void Finalize(uint8_t tag[TAGLEN]);

private:
    uint32_t r[5]{};
    uint32_t h[5]{};
    uint32_t pad[4]{};
    uint8_t buf[16]{};
    size_t leftover{0};

    void Blocks(const uint8_t* m, size_t bytes, uint32_t hibit);
};

/** Forward-secure ChaCha20 stream cipher (BIP324 length cipher, rekeys every 224 packets). */
class FSChaCha20
{
private:
    uint8_t m_key[32]{};
    uint32_t m_rekey_interval;
    uint32_t m_chunk_counter{0};
    uint64_t m_rekey_counter{0};
    uint32_t m_block_counter{0};
    uint8_t m_keystream[64]{};
    size_t m_keystream_pos{64};

public:
    explicit FSChaCha20(const uint8_t key[32], uint32_t rekey_interval = 224);
    ~FSChaCha20();
    void Crypt(const uint8_t* in, uint8_t* out, size_t len);
};

/** Forward-secure AEAD ChaCha20-Poly1305 (BIP324 payload cipher, rekeys every 224 packets). */
class FSChaCha20Poly1305
{
private:
    uint8_t m_key[32]{};
    uint32_t m_rekey_interval;
    uint64_t m_packet_counter{0};

public:
    static constexpr size_t EXPANSION = Poly1305::TAGLEN;

    explicit FSChaCha20Poly1305(const uint8_t key[32], uint32_t rekey_interval = 224);
    ~FSChaCha20Poly1305();
    void Encrypt(const uint8_t* plain, size_t plain_len, const uint8_t* aad, size_t aad_len, uint8_t* cipher);
    bool Decrypt(const uint8_t* cipher, size_t cipher_len, const uint8_t* aad, size_t aad_len, uint8_t* plain);
    bool DecryptPacket(const uint8_t* cipher, size_t cipher_len, const uint8_t* aad, size_t aad_len, uint8_t& header_out, std::vector<uint8_t>& payload_out);
};

#endif // BITCOIN_CRYPTO_CHACHA20POLY1305_H
