// Copyright (c) 2023-2026 The Bitcoin Core & KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BIP324_H
#define BITCOIN_BIP324_H

#include "crypto/chacha20poly1305.h"
#include "protocol.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

/** BIP324 v2 encrypted P2P transport cipher state. */
class BIP324Cipher
{
public:
    static constexpr size_t ELLSWIFT_PUBKEY_LEN = 64;
    static constexpr size_t GARBAGE_TERMINATOR_LEN = 16;
    static constexpr size_t SESSION_ID_LEN = 32;
    static constexpr size_t LENGTH_LEN = 3;
    static constexpr size_t HEADER_LEN = 1;
    static constexpr size_t EXPANSION = LENGTH_LEN + HEADER_LEN + FSChaCha20Poly1305::EXPANSION;
    static constexpr uint8_t IGNORE_BIT = 0x80;

    BIP324Cipher();
    ~BIP324Cipher();

    /** Generate ephemeral secret key + 64-byte ElligatorSwift public key. */
    bool GenerateKey();

    const std::array<uint8_t, ELLSWIFT_PUBKEY_LEN>& GetOurPubKey() const { return m_our_pubkey; }

    /** Derive session keys once peer's 64-byte ElligatorSwift pubkey is received. */
    bool Initialize(const uint8_t their_pubkey[ELLSWIFT_PUBKEY_LEN], bool initiator, const CMessageHeader::MessageStartChars& magic);

    bool IsInitialized() const { return m_initialized; }

    const std::array<uint8_t, GARBAGE_TERMINATOR_LEN>& GetSendGarbageTerminator() const { return m_send_garbage_terminator; }
    const std::array<uint8_t, GARBAGE_TERMINATOR_LEN>& GetRecvGarbageTerminator() const { return m_recv_garbage_terminator; }
    const std::array<uint8_t, SESSION_ID_LEN>& GetSessionID() const { return m_session_id; }

    /** Encrypt a BIP324 packet (contents -> 3-byte enc len + AEAD(header || contents)). */
    void Encrypt(const std::vector<uint8_t>& contents, const std::vector<uint8_t>& aad, bool ignore, std::vector<uint8_t>& output);

    /** Decrypt 3-byte length header. */
    uint32_t DecryptLength(const uint8_t input[LENGTH_LEN]);

    /** Decrypt packet body (header + contents + tag). */
    bool Decrypt(const uint8_t* input, size_t len, const std::vector<uint8_t>& aad, bool& ignore, std::vector<uint8_t>& contents);

private:
    std::array<uint8_t, 32> m_privkey{};
    std::array<uint8_t, ELLSWIFT_PUBKEY_LEN> m_our_pubkey{};
    std::array<uint8_t, GARBAGE_TERMINATOR_LEN> m_send_garbage_terminator{};
    std::array<uint8_t, GARBAGE_TERMINATOR_LEN> m_recv_garbage_terminator{};
    std::array<uint8_t, SESSION_ID_LEN> m_session_id{};

    std::unique_ptr<FSChaCha20> m_send_l_cipher;
    std::unique_ptr<FSChaCha20> m_recv_l_cipher;
    std::unique_ptr<FSChaCha20Poly1305> m_send_p_cipher;
    std::unique_ptr<FSChaCha20Poly1305> m_recv_p_cipher;
    bool m_initialized{false};
};

#endif // BITCOIN_BIP324_H
