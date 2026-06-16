// Copyright (c) 2017 Pieter Wuille
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

// Bech32 is a string encoding format used in newer address types.
// The output consists of a human-readable part (alphanumeric), a
// separator character (1), and a base32 data section, the last
// 6 characters of which are a checksum.
//
// For more information, see BIP 173.
#ifndef BITCOIN_BECH32_H
#define BITCOIN_BECH32_H

#include <stdint.h>
#include <string>
#include <vector>

namespace bech32
{

enum class Encoding {
    INVALID,
    BECH32,
    BECH32M,
};

/** Encode a Bech32 or Bech32m string. Returns the empty string in case of failure. */
std::string Encode(const std::string& hrp, const std::vector<uint8_t>& values, Encoding encoding);

struct DecodeResult {
    Encoding encoding;
    std::string hrp;
    std::vector<uint8_t> data;
};

/** Decode a Bech32 or Bech32m string. */
DecodeResult Decode(const std::string& str);

} // namespace bech32

#endif // BITCOIN_BECH32_H
