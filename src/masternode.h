// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODE_H
#define MASTERNODE_H

#include "amount.h"
#include "uint256.h"

#include <utility>

#define MASTERNODE_MIN_CONFIRMATIONS 15
#define MASTERNODE_PING_SECONDS (5 * 60)

bool GetBlockHash(uint256& hash, int nBlockHeight);

/** Static Masternode collateral schedule and block reward/payment economy helpers.
 */
class CMasternode
{
public:
    static CAmount GetMasternodeNodeCollateral(int nHeight);
    static CAmount GetBlockValue(int nHeight);
    static CAmount GetMasternodePayment(int nHeight);
    static void InitMasternodeCollateralList();
    static std::pair<int, CAmount> GetNextMasternodeCollateral(int nHeight);
};

#endif // MASTERNODE_H
