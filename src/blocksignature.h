// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTATECH_BLOCKSIGNATURE_H
#define KRISTATECH_BLOCKSIGNATURE_H

#include "key.h"
#include "primitives/block.h"
#include "crypto/bls.h"

bool SignBlockWithKey(CBlock& block, const CKey& key, const CBLSSecretKey& blsKey, int nHeight = -1);
bool SignBlock(CBlock& block, const CKeyStore& keystore, int nHeight = -1);
bool CheckBlockSignature(const CBlock& block, const bool enableP2PKH, int nHeight = -1);

#endif //KRISTATECH_BLOCKSIGNATURE_H
