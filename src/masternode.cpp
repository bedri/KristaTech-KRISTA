// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode.h"
#include "adam.h"
#include "chainparams.h"
#include "main.h"
#include "sync.h"
#include "util.h"

// cache collaterals
std::vector<std::pair<int, CAmount>> vecCollaterals;

// Get the block hash at a specific height.
bool GetBlockHash(uint256& hash, int nBlockHeight)
{
    LOCK(cs_main);
    const CBlockIndex* tipIndex = chainActive.Tip();
    if (!tipIndex || !tipIndex->nHeight) return false;

    if (nBlockHeight == 0)
        nBlockHeight = tipIndex->nHeight;

    if (nBlockHeight < 0 || nBlockHeight > tipIndex->nHeight)
        return false;

    const CBlockIndex* pindex = tipIndex->GetAncestor(nBlockHeight);
    if (!pindex) return false;
    hash = pindex->GetBlockHash();
    return true;
}

CAmount CMasternode::GetMasternodeNodeCollateral(int nHeight) 
{
    return 4200 * COIN;
}

static CAmount ComputeDecaySubsidyForPeriod(int period)
{
    if (period < 0) return 0;

    // 90 günde bir %2.15 azalma (Decay) - Her 259.200 blokta bir (Deterministic 128-bit Fixed-Point Integer Math)
    static const unsigned __int128 FIXED_SCALE = 1000000000000000000ULL; // 10^18
    static const unsigned __int128 DECAY_NUM   = 978500000000000000ULL;  // 0.9785 * 10^18 (-2.15%)
    unsigned __int128 factor = FIXED_SCALE;
    unsigned __int128 base = DECAY_NUM;
    int exp = period;
    while (exp > 0) {
        if (exp & 1) {
            factor = (factor * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
        }
        exp >>= 1;
        if (exp > 0) {
            base = (base * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
        }
    }
    unsigned __int128 rawSubsidy = (unsigned __int128)(17 * COIN) * factor;
    return (CAmount)((rawSubsidy + (FIXED_SCALE / 2)) / FIXED_SCALE);
}

CAmount CMasternode::GetBlockValue(int nHeight)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    CAmount maxMoneyOut = consensus.nMaxMoneyOut;

    if (nHeight < 0 || maxMoneyOut <= 0) {
        return 0;
    }
    if (nHeight == 0) {
        return std::min(17 * COIN, maxMoneyOut);
    }
    if (nHeight == 1) {
        return 0;
    }

    // L-1: Height-pure cumulative subsidy calculation (100% independent of mutable global nMoneySupply)
    unsigned __int128 cumEmitted = (unsigned __int128)(17 * COIN); // block 0 (block 1 = 0)
    if (cumEmitted >= (unsigned __int128)maxMoneyOut) {
        return 0;
    }

    const int blocksSinceStart = nHeight - 2;
    const int fullPeriods = blocksSinceStart / 259200;
    const int remBlocks = blocksSinceStart % 259200;

    for (int p = 0; p < fullPeriods; ++p) {
        const CAmount periodSubsidy = ComputeDecaySubsidyForPeriod(p);
        if (periodSubsidy <= 0) {
            return 0;
        }
        cumEmitted += (unsigned __int128)periodSubsidy * 259200ULL;
        if (cumEmitted >= (unsigned __int128)maxMoneyOut) {
            return 0;
        }
    }

    const CAmount nSubsidy = ComputeDecaySubsidyForPeriod(fullPeriods);
    if (nSubsidy <= 0) {
        return 0;
    }
    cumEmitted += (unsigned __int128)nSubsidy * (unsigned __int128)remBlocks;
    if (cumEmitted >= (unsigned __int128)maxMoneyOut) {
        return 0;
    }

    const CAmount nPriorSupply = (CAmount)cumEmitted;
    if (nPriorSupply + nSubsidy > maxMoneyOut) {
        return maxMoneyOut - nPriorSupply;
    }
    return nSubsidy;
}

CAmount CMasternode::GetMasternodePayment(int nHeight)
{
    // Model D activates at block 200 on Mainnet, Testnet, and Regtest.
    // When active, it overrides the legacy bootstrap rules below.
    if (IsModelDActive(nHeight)) {
        CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
        CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
        CAmount nBlockVal = nBlockValActual - nTreasurySplit;
        return nBlockVal * 5 / 100; // 5% MN passive pay (Balanced Model D) after 7% Treasury split
    }

    // Legacy/Fallback Rules (prior to Model D activation):
    // 1. Early Bootstrap (blocks 2 to 5,000): Masternodes receive no payment to allow initial setup.
    if (nHeight <= 5000) {
        return 0;
    }

    // 2. Late Bootstrap (blocks 5,001 to 100,000): Masternodes receive 80% of block value.
    if (nHeight <= 100000) {
        return CMasternode::GetBlockValue(nHeight) * 80 / 100; // %80 MN, %20 Miner-Staker
    }

    // 3. Maturation Phase (blocks 100,001+): Masternodes receive 60% of block value.
    return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Miner-Staker
}

void CMasternode::InitMasternodeCollateralList() {
    vecCollaterals.clear();
    CAmount c = GetMasternodeNodeCollateral(0);
    LogPrint(BCLog::MASTERNODE, "%s: Found collateral %d at block 0\n", __func__, c / COIN);
    vecCollaterals.push_back(std::make_pair(0, c));
}

std::pair<int, CAmount> CMasternode::GetNextMasternodeCollateral(int nHeight) {
    for(auto p : vecCollaterals) {
        if(p.first > nHeight) {
            return std::make_pair(p.first - nHeight, p.second);
        }
    }
    return std::make_pair(-1, -1);
}
