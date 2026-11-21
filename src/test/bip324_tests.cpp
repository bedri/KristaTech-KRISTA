// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip324.h"
#include "chainparams.h"
#include "crypto/chacha20poly1305.h"
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
    initCipher.Encrypt({}, garbageAad, false, verPacket);
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
        initCipher.Encrypt(msg, {}, expectIgnore, enc);

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
    initCipher.Encrypt(payload, {}, false, goodEnc);
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

BOOST_AUTO_TEST_SUITE_END()
