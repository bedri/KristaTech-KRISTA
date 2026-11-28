// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_PAYMENTS_H
#define MASTERNODE_PAYMENTS_H

#include "amount.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "pubkey.h"

#include <vector>

class CBlockIndex;
class CCoinsViewCache;

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight, const CCoinsViewCache* pView = nullptr);
void ClearPrevLlmqCache();
bool IsBlockValueValid(int nHeight, CAmount nExpectedValue, CAmount nMinted);
void FillBlockPayee(CMutableTransaction& txNew, const CBlockIndex* pindexPrev, bool fProofOfStake, const std::vector<CPubKey>* pvValidMiners = nullptr, const CPubKey* pCoordinatorKey = nullptr);

#endif // MASTERNODE_PAYMENTS_H
