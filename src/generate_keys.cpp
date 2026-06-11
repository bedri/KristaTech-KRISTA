#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <secp256k1.h>

const char* ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

std::string Base58Encode(const std::vector<unsigned char>& vch) {
    int zeroes = 0;
    while (zeroes < vch.size() && vch[zeroes] == 0) {
        zeroes++;
    }
    std::vector<unsigned char> b58((vch.size() - zeroes) * 138 / 100 + 1, 0);
    for (size_t i = zeroes; i < vch.size(); i++) {
        int carry = vch[i];
        for (int j = b58.size() - 1; j >= 0; j--) {
            carry += 256 * b58[j];
            b58[j] = carry % 58;
            carry /= 58;
        }
    }
    std::string str = "";
    size_t it = 0;
    while (it < b58.size() && b58[it] == 0) {
        it++;
    }
    for (int i = 0; i < zeroes; i++) {
        str += ALPHABET[0];
    }
    for (; it < b58.size(); it++) {
        str += ALPHABET[b58[it]];
    }
    return str;
}

std::vector<unsigned char> DoubleSHA256(const std::string& data) {
    std::vector<unsigned char> hash1(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash1.data());
    std::vector<unsigned char> hash2(SHA256_DIGEST_LENGTH);
    SHA256(hash1.data(), hash1.size(), hash2.data());
    return hash2;
}

std::vector<unsigned char> DoubleSHA256(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> hash1(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash1.data());
    std::vector<unsigned char> hash2(SHA256_DIGEST_LENGTH);
    SHA256(hash1.data(), hash1.size(), hash2.data());
    return hash2;
}

std::vector<unsigned char> Hash160(const std::vector<unsigned char>& data) {
    std::vector<unsigned char> sha(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), sha.data());
    std::vector<unsigned char> rip(RIPEMD160_DIGEST_LENGTH);
    RIPEMD160(sha.data(), sha.size(), rip.data());
    return rip;
}

std::string HexStr(const std::vector<unsigned char>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char c : data) {
        ss << std::setw(2) << static_cast<int>(c);
    }
    return ss.str();
}

int main() {
    secp256k1_context* ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN | SECP256K1_CONTEXT_VERIFY);
    std::string seedBase = "adam_miner_seed_";

    for (int i = 0; i <= 2000; ++i) {
        std::string seed = seedBase + std::to_string(i);
        std::vector<unsigned char> secret = DoubleSHA256(seed);

        // Derive public key
        secp256k1_pubkey pubkey;
        if (!secp256k1_ec_pubkey_create(ctx, &pubkey, secret.data())) {
            std::cout << "Index: " << i << " | Failed to create pubkey" << std::endl;
            continue;
        }

        // Serialize compressed pubkey
        std::vector<unsigned char> pubkeySerialized(33);
        size_t len = pubkeySerialized.size();
        secp256k1_ec_pubkey_serialize(ctx, pubkeySerialized.data(), &len, &pubkey, SECP256K1_EC_COMPRESSED);

        // Get KeyID (hash160 of pubkey)
        std::vector<unsigned char> keyid = Hash160(pubkeySerialized);

        // Address: prefix (10, 100) + KeyID + Checksum
        std::vector<unsigned char> addrData;
        addrData.push_back(10);
        addrData.push_back(100);
        addrData.insert(addrData.end(), keyid.begin(), keyid.end());
        std::vector<unsigned char> addrChecksum = DoubleSHA256(addrData);
        addrData.insert(addrData.end(), addrChecksum.begin(), addrChecksum.begin() + 4);
        std::string address = Base58Encode(addrData);

        // WIF: prefix (43) + secret (32 bytes) + compressed flag (0x01) + Checksum
        std::vector<unsigned char> wifData;
        wifData.push_back(43);
        wifData.insert(wifData.end(), secret.begin(), secret.end());
        wifData.push_back(1); // compressed public key flag
        std::vector<unsigned char> wifChecksum = DoubleSHA256(wifData);
        wifData.insert(wifData.end(), wifChecksum.begin(), wifChecksum.begin() + 4);
        std::string wif = Base58Encode(wifData);

        std::cout << "Index: " << i << " | PubKey: " << HexStr(pubkeySerialized) << " | WIF: " << wif << " | Address: " << address << " | KeyID: " << HexStr(keyid) << std::endl;
    }

    secp256k1_context_destroy(ctx);
    return 0;
}
