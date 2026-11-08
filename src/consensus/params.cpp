// Copyright (c) 2019 The Zcash developers
// Copyright (c) 2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "util.h"

namespace Consensus {

constexpr int Params::ADAM_COLLATERALIZED_TRACK_QUOTA;
constexpr int Params::ADAM_MASTERNODE_TRACK_QUOTA;
constexpr int Params::ADAM_COIN_LOCK_TRACK_QUOTA;
constexpr int Params::ADAM_OPEN_POW_TRACK_QUOTA;
constexpr int Params::ADAM_MAX_CONSENSUS_THRESHOLD;
constexpr int64_t Params::ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS;
constexpr int64_t Params::ADAM_COMMITTEE_FALLBACK_SLOT_SECS;
constexpr int64_t Params::ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS;
constexpr int64_t Params::ADAM_THRESHOLD_RELAXATION_SLOT_SECS;
constexpr int Params::ADAM_MIN_RELAXED_THRESHOLD;
constexpr int Params::ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP;

bool Params::NetworkUpgradeActive(int nHeight, Consensus::UpgradeIndex idx) const
{
    if (idx >= Consensus::MAX_NETWORK_UPGRADES)
        return error("%s: Upgrade index out of bounds: %d >= %d",
                __func__, idx, Consensus::MAX_NETWORK_UPGRADES);

    if (nHeight < 0)
        return error("%s: Requested state for upgrade %s at negative height %d",
                __func__, NetworkUpgradeInfo[idx].strName, nHeight);

    return NetworkUpgradeState(nHeight, *this, idx) == UPGRADE_ACTIVE;
}

} // End consensus namespace
