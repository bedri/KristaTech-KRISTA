// Copyright (c) 2018-2020 The Bitcoin Core developers
// Copyright (c) 2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "interface/wallet.h"
#include "wallet.h"

namespace interfaces {

    WalletBalances Wallet::getBalances() {
        WalletBalances result;
        LOCK2(cs_main, m_wallet.cs_wallet);
        result.have_watch_only = m_wallet.HaveWatchOnly();
        for (const auto& it : m_wallet.mapWallet) {
            const CWalletTx& pcoin = it.second;
            bool fConflicted = false;
            int depth = 0;
            const bool isTrusted = pcoin.IsTrusted(depth, fConflicted);
            if (isTrusted && depth >= 0) {
                result.balance += pcoin.GetAvailableCredit(true, ISMINE_SPENDABLE);
                if (result.have_watch_only) {
                    result.watch_only_balance += pcoin.GetAvailableWatchOnlyCredit(true);
                }
            } else if (!isTrusted && depth == 0 && pcoin.InMempool()) {
                result.unconfirmed_balance += pcoin.GetAvailableCredit(true, ISMINE_SPENDABLE);
                if (result.have_watch_only) {
                    result.unconfirmed_watch_only_balance += pcoin.GetAvailableWatchOnlyCredit(true);
                }
            }
            if ((pcoin.IsCoinBase() || pcoin.IsCoinStake()) && pcoin.GetBlocksToMaturity() > 0) {
                result.immature_balance += pcoin.GetImmatureCredit(true);
                if (result.have_watch_only) {
                    result.immature_watch_only_balance += pcoin.GetImmatureWatchOnlyCredit(true);
                }
            }
        }
        return result;
    }

} // namespace interfaces