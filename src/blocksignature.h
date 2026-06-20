// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTATECH_BLOCKSIGNATURE_H
#define KRISTATECH_BLOCKSIGNATURE_H

#include "key.h"
#include "primitives/block.h"
#include "crypto/bls.h"

bool SignBlockWithKey(CBlock& block, const CKey& key, const CBLSSecretKey& blsKey);
bool SignBlock(CBlock& block, const CKeyStore& keystore);
bool CheckBlockSignature(const CBlock& block, const bool enableP2PKH);

#endif //KRISTATECH_BLOCKSIGNATURE_H
