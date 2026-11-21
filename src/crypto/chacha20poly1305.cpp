// Copyright (c) 2023-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "crypto/chacha20poly1305.h"
#include "crypto/common.h"
#include "support/cleanse.h"

#include <cassert>
#include <cstring>

namespace {

inline uint32_t RotL32(uint32_t v, int c)
{
    return (v << c) | (v >> (32 - c));
}

inline void QuarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d)
{
    a += b; d = RotL32(d ^ a, 16);
    c += d; b = RotL32(b ^ c, 12);
    a += b; d = RotL32(d ^ a, 8);
    c += d; b = RotL32(b ^ c, 7);
}

void ChaCha20Block(const uint32_t input[16], uint8_t out[64])
{
    uint32_t x[16];
    std::memcpy(x, input, sizeof(x));
    for (int i = 0; i < 10; ++i) {
        QuarterRound(x[0], x[4], x[8], x[12]);
        QuarterRound(x[1], x[5], x[9], x[13]);
        QuarterRound(x[2], x[6], x[10], x[14]);
        QuarterRound(x[3], x[7], x[11], x[15]);
        QuarterRound(x[0], x[5], x[10], x[15]);
        QuarterRound(x[1], x[6], x[11], x[12]);
        QuarterRound(x[2], x[7], x[8], x[13]);
        QuarterRound(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; ++i) {
        WriteLE32(out + i * 4, x[i] + input[i]);
    }
    memory_cleanse(x, sizeof(x));
}

bool ConstantTimeEqual16(const uint8_t a[16], const uint8_t b[16])
{
    volatile uint8_t diff = 0;
    for (int i = 0; i < 16; ++i) {
        diff |= a[i] ^ b[i];
    }
    return diff == 0;
}

void MakeBIP324Nonce(uint64_t counter, uint32_t rekey_interval, uint8_t nonce[12])
{
    WriteLE32(nonce, static_cast<uint32_t>(counter % rekey_interval));
    WriteLE64(nonce + 4, counter / rekey_interval);
}

} // namespace

ChaCha20Nonce96::~ChaCha20Nonce96()
{
    memory_cleanse(state, sizeof(state));
}

void ChaCha20Nonce96::SetKey(const uint8_t key[32])
{
    state[0] = 0x61707865U;
    state[1] = 0x3320646eU;
    state[2] = 0x79622d32U;
    state[3] = 0x6b206574U;
    for (int i = 0; i < 8; ++i) {
        state[4 + i] = ReadLE32(key + i * 4);
    }
    state[12] = 0;
    state[13] = 0;
    state[14] = 0;
    state[15] = 0;
}

void ChaCha20Nonce96::Crypt(const uint8_t nonce[12], uint32_t counter, const uint8_t* in, uint8_t* out, size_t len) const
{
    uint32_t block_input[16];
    std::memcpy(block_input, state, sizeof(block_input));
    block_input[12] = counter;
    block_input[13] = ReadLE32(nonce);
    block_input[14] = ReadLE32(nonce + 4);
    block_input[15] = ReadLE32(nonce + 8);

    uint8_t keystream[64];
    size_t offset = 0;
    while (offset < len) {
        ChaCha20Block(block_input, keystream);
        ++block_input[12];
        size_t chunk = std::min<size_t>(64, len - offset);
        for (size_t i = 0; i < chunk; ++i) {
            out[offset + i] = (in ? in[offset + i] : 0) ^ keystream[i];
        }
        offset += chunk;
    }
    memory_cleanse(keystream, sizeof(keystream));
    memory_cleanse(block_input, sizeof(block_input));
}

Poly1305::Poly1305(const uint8_t key[32])
{
    r[0] = (ReadLE32(key + 0)) & 0x3ffffffU;
    r[1] = (ReadLE32(key + 3) >> 2) & 0x3ffff03U;
    r[2] = (ReadLE32(key + 6) >> 4) & 0x3ffc0ffU;
    r[3] = (ReadLE32(key + 9) >> 6) & 0x3f03fffU;
    r[4] = (ReadLE32(key + 12) >> 8) & 0x00fffffU;

    std::memset(h, 0, sizeof(h));
    for (int i = 0; i < 4; ++i) {
        pad[i] = ReadLE32(key + 16 + i * 4);
    }
}

Poly1305::~Poly1305()
{
    memory_cleanse(r, sizeof(r));
    memory_cleanse(h, sizeof(h));
    memory_cleanse(pad, sizeof(pad));
    memory_cleanse(buf, sizeof(buf));
}

void Poly1305::Blocks(const uint8_t* m, size_t bytes, uint32_t hibit)
{
    const uint32_t r0 = r[0], r1 = r[1], r2 = r[2], r3 = r[3], r4 = r[4];
    const uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    uint32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];

    while (bytes >= 16) {
        h0 += (ReadLE32(m + 0)) & 0x3ffffffU;
        h1 += (ReadLE32(m + 3) >> 2) & 0x3ffffffU;
        h2 += (ReadLE32(m + 6) >> 4) & 0x3ffffffU;
        h3 += (ReadLE32(m + 9) >> 6) & 0x3ffffffU;
        h4 += (ReadLE32(m + 12) >> 8) | (hibit << 24);

        uint64_t d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
        uint64_t d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
        uint64_t d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
        uint64_t d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
        uint64_t d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;

        uint32_t c;
        c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffffU; d1 += c;
        c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffffU; d2 += c;
        c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffffU; d3 += c;
        c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffffU; d4 += c;
        c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffffU; h0 += c * 5;
        c = (h0 >> 26); h0 &= 0x3ffffffU; h1 += c;

        m += 16;
        bytes -= 16;
    }

    h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3; h[4] = h4;
}

void Poly1305::Update(const uint8_t* data, size_t len)
{
    if (leftover > 0) {
        size_t want = std::min<size_t>(16 - leftover, len);
        std::memcpy(buf + leftover, data, want);
        data += want;
        len -= want;
        leftover += want;
        if (leftover < 16) return;
        Blocks(buf, 16, 1);
        leftover = 0;
    }
    if (len >= 16) {
        size_t want = len & ~size_t(15);
        Blocks(data, want, 1);
        data += want;
        len -= want;
    }
    if (len > 0) {
        std::memcpy(buf, data, len);
        leftover = len;
    }
}

void Poly1305::Finalize(uint8_t tag[TAGLEN])
{
    if (leftover > 0) {
        buf[leftover++] = 1;
        while (leftover < 16) buf[leftover++] = 0;
        Blocks(buf, 16, 0);
    }

    uint32_t h0 = h[0], h1 = h[1], h2 = h[2], h3 = h[3], h4 = h[4];
    uint32_t c;
    c = h1 >> 26; h1 &= 0x3ffffffU; h2 += c;
    c = h2 >> 26; h2 &= 0x3ffffffU; h3 += c;
    c = h3 >> 26; h3 &= 0x3ffffffU; h4 += c;
    c = h4 >> 26; h4 &= 0x3ffffffU; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x3ffffffU; h1 += c;

    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffffU;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffffU;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffffU;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffffU;
    uint32_t g4 = h4 + c - (1U << 26);

    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    uint32_t f0 = ((h0) | (h1 << 26));
    uint32_t f1 = ((h1 >> 6) | (h2 << 20));
    uint32_t f2 = ((h2 >> 12) | (h3 << 14));
    uint32_t f3 = ((h3 >> 18) | (h4 << 8));

    uint64_t t = (uint64_t)f0 + pad[0]; WriteLE32(tag + 0, (uint32_t)t); t >>= 32;
    t += (uint64_t)f1 + pad[1]; WriteLE32(tag + 4, (uint32_t)t); t >>= 32;
    t += (uint64_t)f2 + pad[2]; WriteLE32(tag + 8, (uint32_t)t); t >>= 32;
    t += (uint64_t)f3 + pad[3]; WriteLE32(tag + 12, (uint32_t)t);
}

FSChaCha20::FSChaCha20(const uint8_t key[32], uint32_t rekey_interval)
    : m_rekey_interval(rekey_interval)
{
    assert(m_rekey_interval > 0);
    std::memcpy(m_key, key, 32);
}

FSChaCha20::~FSChaCha20()
{
    memory_cleanse(m_key, sizeof(m_key));
    memory_cleanse(m_keystream, sizeof(m_keystream));
}

void FSChaCha20::Crypt(const uint8_t* in, uint8_t* out, size_t len)
{
    auto next_byte = [&]() -> uint8_t {
        if (m_keystream_pos >= 64) {
            uint8_t nonce[12];
            WriteLE32(nonce, 0);
            WriteLE64(nonce + 4, m_rekey_counter);
            ChaCha20Nonce96 cipher;
            cipher.SetKey(m_key);
            cipher.Crypt(nonce, m_block_counter++, nullptr, m_keystream, 64);
            m_keystream_pos = 0;
        }
        return m_keystream[m_keystream_pos++];
    };

    for (size_t i = 0; i < len; ++i) {
        out[i] = (in ? in[i] : 0) ^ next_byte();
    }

    ++m_chunk_counter;
    if (m_chunk_counter == m_rekey_interval) {
        uint8_t new_key[32];
        for (size_t i = 0; i < 32; ++i) {
            new_key[i] = next_byte();
        }
        std::memcpy(m_key, new_key, 32);
        memory_cleanse(new_key, sizeof(new_key));
        memory_cleanse(m_keystream, sizeof(m_keystream));
        m_chunk_counter = 0;
        m_block_counter = 0;
        m_keystream_pos = 64;
        ++m_rekey_counter;
    }
}

constexpr size_t FSChaCha20Poly1305::EXPANSION;

FSChaCha20Poly1305::FSChaCha20Poly1305(const uint8_t key[32], uint32_t rekey_interval)
    : m_rekey_interval(rekey_interval)
{
    assert(m_rekey_interval > 0);
    std::memcpy(m_key, key, 32);
}

FSChaCha20Poly1305::~FSChaCha20Poly1305()
{
    memory_cleanse(m_key, sizeof(m_key));
}

static void ComputeAEADTag(const ChaCha20Nonce96& cipher, const uint8_t nonce[12],
                           const uint8_t* aad, size_t aad_len,
                           const uint8_t* ciphertext, size_t ct_len,
                           uint8_t tag[Poly1305::TAGLEN])
{
    uint8_t poly_key[32];
    cipher.Crypt(nonce, 0, nullptr, poly_key, 32);
    Poly1305 poly(poly_key);
    memory_cleanse(poly_key, sizeof(poly_key));

    static const uint8_t zeros[16] = {};
    if (aad_len > 0) {
        poly.Update(aad, aad_len);
        if (aad_len % 16 != 0) {
            poly.Update(zeros, 16 - (aad_len % 16));
        }
    }
    if (ct_len > 0) {
        poly.Update(ciphertext, ct_len);
        if (ct_len % 16 != 0) {
            poly.Update(zeros, 16 - (ct_len % 16));
        }
    }
    uint8_t lens[16];
    WriteLE64(lens, aad_len);
    WriteLE64(lens + 8, ct_len);
    poly.Update(lens, 16);
    poly.Finalize(tag);
}

void FSChaCha20Poly1305::Encrypt(const uint8_t* plain, size_t plain_len, const uint8_t* aad, size_t aad_len, uint8_t* cipher_out)
{
    uint8_t nonce[12];
    MakeBIP324Nonce(m_packet_counter, m_rekey_interval, nonce);
    ChaCha20Nonce96 cipher;
    cipher.SetKey(m_key);
    cipher.Crypt(nonce, 1, plain, cipher_out, plain_len);
    ComputeAEADTag(cipher, nonce, aad, aad_len, cipher_out, plain_len, cipher_out + plain_len);

    ++m_packet_counter;
    if (m_packet_counter % m_rekey_interval == 0) {
        uint8_t rekey_nonce[12];
        WriteLE32(rekey_nonce, 0xffffffffU);
        WriteLE64(rekey_nonce + 4, (m_packet_counter / m_rekey_interval) - 1);
        uint8_t new_key[32];
        cipher.Crypt(rekey_nonce, 1, nullptr, new_key, 32);
        std::memcpy(m_key, new_key, 32);
        memory_cleanse(new_key, sizeof(new_key));
        memory_cleanse(rekey_nonce, sizeof(rekey_nonce));
    }
    memory_cleanse(nonce, sizeof(nonce));
}

bool FSChaCha20Poly1305::Decrypt(const uint8_t* cipher_in, size_t cipher_len, const uint8_t* aad, size_t aad_len, uint8_t* plain_out)
{
    if (cipher_len < EXPANSION) return false;
    const size_t ct_len = cipher_len - EXPANSION;

    uint8_t nonce[12];
    MakeBIP324Nonce(m_packet_counter, m_rekey_interval, nonce);
    ChaCha20Nonce96 cipher;
    cipher.SetKey(m_key);

    uint8_t expected_tag[Poly1305::TAGLEN];
    ComputeAEADTag(cipher, nonce, aad, aad_len, cipher_in, ct_len, expected_tag);
    if (!ConstantTimeEqual16(expected_tag, cipher_in + ct_len)) {
        memory_cleanse(nonce, sizeof(nonce));
        return false;
    }
    cipher.Crypt(nonce, 1, cipher_in, plain_out, ct_len);

    ++m_packet_counter;
    if (m_packet_counter % m_rekey_interval == 0) {
        uint8_t rekey_nonce[12];
        WriteLE32(rekey_nonce, 0xffffffffU);
        WriteLE64(rekey_nonce + 4, (m_packet_counter / m_rekey_interval) - 1);
        uint8_t new_key[32];
        cipher.Crypt(rekey_nonce, 1, nullptr, new_key, 32);
        std::memcpy(m_key, new_key, 32);
        memory_cleanse(new_key, sizeof(new_key));
        memory_cleanse(rekey_nonce, sizeof(rekey_nonce));
    }
    memory_cleanse(nonce, sizeof(nonce));
    return true;
}

bool FSChaCha20Poly1305::DecryptPacket(const uint8_t* cipher_in, size_t cipher_len, const uint8_t* aad, size_t aad_len, uint8_t& header_out, std::vector<uint8_t>& payload_out)
{
    if (cipher_len < 1 + EXPANSION) return false;
    const size_t ct_len = cipher_len - EXPANSION;

    uint8_t nonce[12];
    MakeBIP324Nonce(m_packet_counter, m_rekey_interval, nonce);
    ChaCha20Nonce96 cipher;
    cipher.SetKey(m_key);

    uint8_t expected_tag[Poly1305::TAGLEN];
    ComputeAEADTag(cipher, nonce, aad, aad_len, cipher_in, ct_len, expected_tag);
    if (!ConstantTimeEqual16(expected_tag, cipher_in + ct_len)) {
        memory_cleanse(nonce, sizeof(nonce));
        return false;
    }

    uint8_t first_block[64];
    const size_t first_len = std::min<size_t>(64, ct_len);
    cipher.Crypt(nonce, 1, cipher_in, first_block, first_len);
    header_out = first_block[0];

    const size_t payload_len = ct_len - 1;
    payload_out.resize(payload_len);
    if (first_len > 1) {
        std::memcpy(payload_out.data(), first_block + 1, first_len - 1);
    }
    memory_cleanse(first_block, sizeof(first_block));
    if (ct_len > 64) {
        cipher.Crypt(nonce, 2, cipher_in + 64, payload_out.data() + 63, ct_len - 64);
    }

    ++m_packet_counter;
    if (m_packet_counter % m_rekey_interval == 0) {
        uint8_t rekey_nonce[12];
        WriteLE32(rekey_nonce, 0xffffffffU);
        WriteLE64(rekey_nonce + 4, (m_packet_counter / m_rekey_interval) - 1);
        uint8_t new_key[32];
        cipher.Crypt(rekey_nonce, 1, nullptr, new_key, 32);
        std::memcpy(m_key, new_key, 32);
        memory_cleanse(new_key, sizeof(new_key));
        memory_cleanse(rekey_nonce, sizeof(rekey_nonce));
    }
    memory_cleanse(nonce, sizeof(nonce));
    return true;
}
