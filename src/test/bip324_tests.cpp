// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip324.h"
#include "chainparams.h"
#include "crypto/chacha20poly1305.h"
#include "net.h"
#include "netbase.h"
#include "test/test_kristatech.h"

#include <cstring>
#include <vector>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(bip324_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(bip324_roundtrip_aad_tamper_and_rekey)
{
    BIP324Cipher initCipher;
    BIP324Cipher respCipher;

    const auto& magic = Params().MessageStart();
    BOOST_REQUIRE(initCipher.GenerateKey(&magic));
    BOOST_REQUIRE(respCipher.GenerateKey(&magic));

    // Ephemeral ElligatorSwift public keys must never collide with V1 network magic
    BOOST_CHECK(std::memcmp(initCipher.GetOurPubKey().data(), magic, MESSAGE_START_SIZE) != 0);
    BOOST_CHECK(std::memcmp(respCipher.GetOurPubKey().data(), magic, MESSAGE_START_SIZE) != 0);

    BOOST_REQUIRE(initCipher.Initialize(respCipher.GetOurPubKey().data(), true, magic));
    BOOST_REQUIRE(respCipher.Initialize(initCipher.GetOurPubKey().data(), false, magic));
    BOOST_CHECK(initCipher.IsInitialized());
    BOOST_CHECK(respCipher.IsInitialized());
    BOOST_CHECK(initCipher.GetSessionID() == respCipher.GetSessionID());

    // 1. Version packet with non-empty garbage AAD (consumed by first packet only)
    const std::vector<uint8_t> garbageAad = {0x11, 0x22, 0x33, 0x44, 0x55};
    std::vector<uint8_t> verPacket;
    BOOST_REQUIRE(initCipher.Encrypt({}, garbageAad, false, verPacket));
    BOOST_CHECK_EQUAL(verPacket.size(), BIP324Cipher::EXPANSION);

    const uint32_t verLen = respCipher.DecryptLength(verPacket.data());
    BOOST_CHECK_EQUAL(verLen, 0U);
    bool ignore = true;
    std::vector<uint8_t> verPlain;
    BOOST_CHECK(respCipher.Decrypt(
        verPacket.data() + BIP324Cipher::LENGTH_LEN,
        verPacket.size() - BIP324Cipher::LENGTH_LEN,
        garbageAad, ignore, verPlain));
    BOOST_CHECK(!ignore);
    BOOST_CHECK(verPlain.empty());

    // 2. Subsequent decoy (ignore=true) and application packets spanning past rekey boundary (224 packets)
    for (int i = 0; i < 240; ++i) {
        const bool expectIgnore = (i % 7 == 0);
        std::vector<uint8_t> msg = {0x00, 'p', 'i', 'n', 'g', 0, 0, 0, 0, 0, 0, 0, 0,
                                    static_cast<uint8_t>(i & 0xff), static_cast<uint8_t>((i >> 8) & 0xff)};
        std::vector<uint8_t> enc;
        BOOST_REQUIRE(initCipher.Encrypt(msg, {}, expectIgnore, enc));

        const uint32_t decLen = respCipher.DecryptLength(enc.data());
        BOOST_CHECK_EQUAL(decLen, static_cast<uint32_t>(msg.size()));

        std::vector<uint8_t> out;
        bool gotIgnore = !expectIgnore;
        BOOST_REQUIRE(respCipher.Decrypt(
            enc.data() + BIP324Cipher::LENGTH_LEN,
            enc.size() - BIP324Cipher::LENGTH_LEN,
            {}, gotIgnore, out));
        BOOST_CHECK_EQUAL(gotIgnore, expectIgnore);
        BOOST_CHECK(out == msg);
    }

    // 3. Ciphertext/tag tampering must fail authentication
    std::vector<uint8_t> goodEnc;
    const std::vector<uint8_t> payload = {0xaa, 0xbb, 0xcc};
    BOOST_REQUIRE(initCipher.Encrypt(payload, {}, false, goodEnc));
    (void)respCipher.DecryptLength(goodEnc.data());
    std::vector<uint8_t> badEnc = goodEnc;
    badEnc.back() ^= 0x01;
    std::vector<uint8_t> tamperedOut = {0x42};
    bool badIgnore = false;
    BOOST_CHECK(!respCipher.Decrypt(
        badEnc.data() + BIP324Cipher::LENGTH_LEN,
        badEnc.size() - BIP324Cipher::LENGTH_LEN,
        {}, badIgnore, tamperedOut));
    BOOST_CHECK_EQUAL(tamperedOut.size(), 1U); // Did not overwrite output before MAC check
}

BOOST_AUTO_TEST_CASE(chacha20poly1305_decrypt_packet_rejects_bad_mac_without_alloc)
{
    uint8_t key[32];
    std::memset(key, 0x5a, sizeof(key));
    FSChaCha20Poly1305 enc(key);
    FSChaCha20Poly1305 dec(key);

    std::vector<uint8_t> plain(100, 0x17);
    plain[0] = 0x00;
    std::vector<uint8_t> cipher(plain.size() + FSChaCha20Poly1305::EXPANSION);
    enc.Encrypt(plain.data(), plain.size(), nullptr, 0, cipher.data());

    std::vector<uint8_t> tampered = cipher;
    tampered[5] ^= 0xff;
    uint8_t hdr = 0x7f;
    std::vector<uint8_t> out;
    BOOST_CHECK(!dec.DecryptPacket(tampered.data(), tampered.size(), nullptr, 0, hdr, out));
    BOOST_CHECK(out.empty());
    BOOST_CHECK_EQUAL(hdr, 0x7f);

    BOOST_CHECK(dec.DecryptPacket(cipher.data(), cipher.size(), nullptr, 0, hdr, out));
    BOOST_CHECK_EQUAL(hdr, 0x00);
    BOOST_CHECK_EQUAL(out.size(), 99U);
}

BOOST_AUTO_TEST_CASE(bip324_encrypt_uninitialized_and_oversize_returns_false)
{
    // C-1: Uninitialized cipher must return false rather than crashing
    BIP324Cipher uninitCipher;
    std::vector<uint8_t> out = {0x99};
    BOOST_CHECK(!uninitCipher.Encrypt({0x01, 0x02}, {}, false, out));
    BOOST_CHECK_EQUAL(out.size(), 1U);

    // C-1: Oversized payload (> 0xFFFFFFU) must return false cleanly without assert/abort
    BIP324Cipher initCipher;
    BIP324Cipher respCipher;
    const auto& magic = Params().MessageStart();
    BOOST_REQUIRE(initCipher.GenerateKey(&magic));
    BOOST_REQUIRE(respCipher.GenerateKey(&magic));
    BOOST_REQUIRE(initCipher.Initialize(respCipher.GetOurPubKey().data(), true, magic));

    std::vector<uint8_t> hugePayload(0xFFFFFFU + 1U, 0x41);
    std::vector<uint8_t> hugeOut = {0x77};
    BOOST_CHECK(!initCipher.Encrypt(hugePayload, {}, false, hugeOut));
    BOOST_CHECK_EQUAL(hugeOut.size(), 1U);
}

BOOST_AUTO_TEST_CASE(bip324_application_decoy_flood_disconnects)
{
    // H-11: Consecutive decoy limit (> 64) and 60s decoy rate limit (> 256) in V2RecvState::APPLICATION
    const auto& magic = Params().MessageStart();
    CAddress addr(LookupNumeric("127.0.0.1", 27999), NODE_NETWORK);

    auto completeInboundHandshake = [&](CNode& respNode, BIP324Cipher& initCipher) {
        respNode.fV2AutoDetect = true;
        BOOST_REQUIRE(initCipher.GenerateKey(&magic));
        const auto& initPub = initCipher.GetOurPubKey();
        bool complete = false;
        BOOST_REQUIRE(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(initPub.data()), static_cast<unsigned int>(initPub.size()), complete));
        BOOST_REQUIRE(respNode.fV2Transport);
        BOOST_REQUIRE(respNode.v2Cipher != nullptr);
        BOOST_REQUIRE(initCipher.Initialize(respNode.v2Cipher->GetOurPubKey().data(), true, magic));

        std::vector<uint8_t> verPkt;
        BOOST_REQUIRE(initCipher.Encrypt({}, {}, false, verPkt));
        const auto& term = initCipher.GetSendGarbageTerminator();
        std::vector<uint8_t> handshakeBytes;
        handshakeBytes.insert(handshakeBytes.end(), term.begin(), term.end());
        handshakeBytes.insert(handshakeBytes.end(), verPkt.begin(), verPkt.end());
        BOOST_REQUIRE(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(handshakeBytes.data()), static_cast<unsigned int>(handshakeBytes.size()), complete));
        BOOST_REQUIRE(!respNode.fDisconnect);
        BOOST_REQUIRE(respNode.v2RecvState == CNode::V2RecvState::APPLICATION);
    };

    // 1. Consecutive decoy flood (> 64) disconnects cleanly even after reset by valid packet
    {
        CNode respNode(501, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
        BIP324Cipher initCipher;
        completeInboundHandshake(respNode, initCipher);

        bool complete = false;
        for (int i = 0; i < 64; ++i) {
            std::vector<uint8_t> decoy;
            BOOST_REQUIRE(initCipher.Encrypt({0x01, 0x02}, {}, true, decoy));
            BOOST_CHECK(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(decoy.data()), static_cast<unsigned int>(decoy.size()), complete));
            BOOST_CHECK(!respNode.fDisconnect);
        }
        // Valid non-ignore packet resets consecutive counter
        std::vector<uint8_t> appPing = {0x00, 'p', 'i', 'n', 'g', 0, 0, 0, 0, 0, 0, 0, 0};
        std::vector<uint8_t> encApp;
        BOOST_REQUIRE(initCipher.Encrypt(appPing, {}, false, encApp));
        BOOST_CHECK(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(encApp.data()), static_cast<unsigned int>(encApp.size()), complete));
        BOOST_CHECK(!respNode.fDisconnect);
        BOOST_CHECK_EQUAL(respNode.v2ConsecutiveDecoys, 0U);

        for (int i = 0; i < 64; ++i) {
            std::vector<uint8_t> decoy;
            BOOST_REQUIRE(initCipher.Encrypt({0x03}, {}, true, decoy));
            BOOST_CHECK(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(decoy.data()), static_cast<unsigned int>(decoy.size()), complete));
            BOOST_CHECK(!respNode.fDisconnect);
        }
        std::vector<uint8_t> decoy65;
        BOOST_REQUIRE(initCipher.Encrypt({0x04}, {}, true, decoy65));
        BOOST_CHECK(!respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(decoy65.data()), static_cast<unsigned int>(decoy65.size()), complete));
        BOOST_CHECK(respNode.fDisconnect);
    }

    // 2. Sustained decoy flood (> 256 in 60s) disconnects even when interleaved with valid packets
    {
        CNode respNode(502, NODE_NETWORK, 0, INVALID_SOCKET, addr, 0, 0, "", true);
        BIP324Cipher initCipher;
        completeInboundHandshake(respNode, initCipher);

        bool complete = false;
        int decoysSent = 0;
        while (decoysSent < 256) {
            std::vector<uint8_t> decoy;
            BOOST_REQUIRE(initCipher.Encrypt({0x05}, {}, true, decoy));
            BOOST_REQUIRE(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(decoy.data()), static_cast<unsigned int>(decoy.size()), complete));
            BOOST_REQUIRE(!respNode.fDisconnect);
            decoysSent++;
            if (decoysSent % 8 == 0) {
                std::vector<uint8_t> appPing = {0x00, 'p', 'i', 'n', 'g', 0, 0, 0, 0, 0, 0, 0, 0};
                std::vector<uint8_t> encApp;
                BOOST_REQUIRE(initCipher.Encrypt(appPing, {}, false, encApp));
                BOOST_REQUIRE(respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(encApp.data()), static_cast<unsigned int>(encApp.size()), complete));
                BOOST_REQUIRE(!respNode.fDisconnect);
            }
        }
        std::vector<uint8_t> decoy257;
        BOOST_REQUIRE(initCipher.Encrypt({0x06}, {}, true, decoy257));
        BOOST_CHECK(!respNode.ReceiveMsgBytes(reinterpret_cast<const char*>(decoy257.data()), static_cast<unsigned int>(decoy257.size()), complete));
        BOOST_CHECK(respNode.fDisconnect);
    }
}

BOOST_AUTO_TEST_SUITE_END()
