// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"

#include "base58.h"
#include "timedata.h"
#include "wallet/wallet.h"
#include "main.h"
#include "masternodeman.h"
#include "adam.h"
#include "llmq.h"

#include <algorithm>
#include <stdint.h>

std::string TransactionRecord::getValueOrReturnEmpty(const std::map<std::string, std::string>& mapValue, const std::string& key)
{
    const auto& it = mapValue.find(key);
    return it != mapValue.end() ? it->second : "";
}

bool TransactionRecord::decomposeCoinStake(const CWallet* wallet, const CWalletTx& wtx,
        const CAmount& nCredit, const CAmount& nDebit, bool fZSpendFromMe,
        QList<TransactionRecord>& parts)
{
    // Return if it's not a coinstake
    if (!wtx.IsCoinStake()) {
        return false;
    }

    const uint256& hash = wtx.GetHash();
    bool foundMine = false;

    for (unsigned int nOut = 1; nOut < wtx.vout.size(); nOut++) {
        isminetype mine = wallet->IsMine(wtx.vout[nOut]);
        if (mine) {
            TransactionRecord sub(hash, wtx.GetTxTime(), wtx.GetTotalSize());
            sub.idx = (int)nOut;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;

            CTxDestination address;
            if (ExtractDestination(wtx.vout[nOut].scriptPubKey, address)) {
                sub.address = EncodeDestination(address);
            }

            if (nDebit > 0) {
                // We are the staker
                sub.type = TransactionRecord::StakeMint;
                sub.credit = nCredit - nDebit;
            } else {
                // We only received a split/masternode reward
                sub.type = TransactionRecord::MNReward;

                CBlockIndex* pindexPrev = nullptr;
                int nHeight = 0;
                BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
                if (mi != mapBlockIndex.end()) {
                    CBlockIndex* pindex = mi->second;
                    if (pindex) {
                        pindexPrev = pindex->pprev;
                        nHeight = pindex->nHeight;
                    }
                }

                if (pindexPrev && IsModelDActive(nHeight)) {
                    // Check if LLMQ reward
                    llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight);
                    std::vector<CScript> vLlmqPayees;
                    for (const auto& member : quorum.members) {
                        CMasternode* pmn = mnodeman.Find(member.pubKeyMasternode);
                        if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                            vLlmqPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                        }
                    }
                    if (!vLlmqPayees.empty()) {
                        CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
                        CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
                        CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
                        CAmount nTotalTreasuryFaucet = nTreasurySplit + nFaucetSplit;
                        CAmount nBlockVal = nBlockValActual - nTotalTreasuryFaucet;
                        CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
                        CAmount nLLMQPaymentPerMember = nLLMQSplitTotal / vLlmqPayees.size();
                        CAmount nLLMQRemainder = nLLMQSplitTotal % vLlmqPayees.size();
                        for (size_t idx = 0; idx < vLlmqPayees.size(); ++idx) {
                            CAmount amt = nLLMQPaymentPerMember + (idx == vLlmqPayees.size() - 1 ? nLLMQRemainder : 0);
                            if (vLlmqPayees[idx] == wtx.vout[nOut].scriptPubKey && amt == wtx.vout[nOut].nValue) {
                                sub.type = TransactionRecord::LlmqReward;
                                break;
                            }
                        }
                    }

                    // Check if ADAM miner reward
                    if (sub.type == TransactionRecord::MNReward) {
                        uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
                        std::vector<CPubKey> vMiners;
                        CPubKey coordinator;
                        SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator);

                        std::vector<CScript> vPartPayees;
                        CScript producerScript = wtx.vout[1].scriptPubKey;
                        for (const auto& minerKey : vMiners) {
                            CMasternode* pmn = mnodeman.Find(minerKey);
                            if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                                CScript minerScript = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
                                if (minerScript != producerScript) {
                                    vPartPayees.push_back(minerScript);
                                }
                            }
                        }
                        if (!vPartPayees.empty()) {
                            CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
                            CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
                            CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
                            CAmount nTotalTreasuryFaucet = nTreasurySplit + nFaucetSplit;
                            CAmount nBlockVal = nBlockValActual - nTotalTreasuryFaucet;
                            CAmount nPartSplitTotal = nBlockVal * 25 / 100;
                            CAmount nPartPaymentPerMember = nPartSplitTotal / vPartPayees.size();
                            CAmount nPartRemainder = nPartSplitTotal % vPartPayees.size();
                            for (size_t idx = 0; idx < vPartPayees.size(); ++idx) {
                                CAmount amt = nPartPaymentPerMember + (idx == vPartPayees.size() - 1 ? nPartRemainder : 0);
                                if (vPartPayees[idx] == wtx.vout[nOut].scriptPubKey && amt == wtx.vout[nOut].nValue) {
                                    sub.type = TransactionRecord::AdamReward;
                                    break;
                                }
                            }
                        }
                    }
                }

                sub.credit = wtx.vout[nOut].nValue;
            }
            parts.append(sub);
            foundMine = true;

            // If we are the staker, one record representing the net reward is sufficient.
            if (nDebit > 0) {
                break;
            }
        }
    }

    // Fallback if none of the outputs is mine, but we somehow have this transaction in the wallet
    if (!foundMine) {
        TransactionRecord sub(hash, wtx.GetTxTime(), wtx.GetTotalSize());
        if (nDebit > 0) {
            sub.type = TransactionRecord::StakeMint;
            sub.credit = nCredit - nDebit;
        } else {
            sub.type = TransactionRecord::MNReward;
            sub.credit = nCredit;
        }
        parts.append(sub);
    }

    return true;
}

bool TransactionRecord::decomposeCoinBase(const CWallet* wallet, const CWalletTx& wtx,
        QList<TransactionRecord>& parts)
{
    // Return if it's not a coinbase
    if (!wtx.IsCoinBase()) {
        return false;
    }

    const uint256& hash = wtx.GetHash();
    
    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut& txout = wtx.vout[nOut];
        isminetype mine = wallet->IsMine(txout);
        if (mine) {
            TransactionRecord sub(hash, wtx.GetTxTime(), wtx.GetTotalSize());
            sub.idx = (int) nOut; // vout index
            sub.credit = txout.nValue;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address)) {
                sub.address = EncodeDestination(address);
            }
            
            if (nOut == 0) {
                // Miner reward
                sub.type = TransactionRecord::Generated;
            } else {
                // Masternode or other block reward splits
                sub.type = TransactionRecord::MNReward;

                CBlockIndex* pindexPrev = nullptr;
                int nHeight = 0;
                BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
                if (mi != mapBlockIndex.end()) {
                    CBlockIndex* pindex = mi->second;
                    if (pindex) {
                        pindexPrev = pindex->pprev;
                        nHeight = pindex->nHeight;
                    }
                }

                if (pindexPrev && IsModelDActive(nHeight)) {
                    // Check if LLMQ reward
                    llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight);
                    std::vector<CScript> vLlmqPayees;
                    for (const auto& member : quorum.members) {
                        CMasternode* pmn = mnodeman.Find(member.pubKeyMasternode);
                        if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                            vLlmqPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                        }
                    }
                    if (!vLlmqPayees.empty()) {
                        CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
                        CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
                        CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
                        CAmount nTotalTreasuryFaucet = nTreasurySplit + nFaucetSplit;
                        CAmount nBlockVal = nBlockValActual - nTotalTreasuryFaucet;
                        CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
                        CAmount nLLMQPaymentPerMember = nLLMQSplitTotal / vLlmqPayees.size();
                        CAmount nLLMQRemainder = nLLMQSplitTotal % vLlmqPayees.size();
                        for (size_t idx = 0; idx < vLlmqPayees.size(); ++idx) {
                            CAmount amt = nLLMQPaymentPerMember + (idx == vLlmqPayees.size() - 1 ? nLLMQRemainder : 0);
                            if (vLlmqPayees[idx] == txout.scriptPubKey && amt == txout.nValue) {
                                sub.type = TransactionRecord::LlmqReward;
                                break;
                            }
                        }
                    }

                    // Check if ADAM miner reward
                    if (sub.type == TransactionRecord::MNReward) {
                        uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
                        std::vector<CPubKey> vMiners;
                        CPubKey coordinator;
                        SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator);

                        std::vector<CScript> vPartPayees;
                        for (const auto& minerKey : vMiners) {
                            CMasternode* pmn = mnodeman.Find(minerKey);
                            if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                                vPartPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                            }
                        }
                        if (!vPartPayees.empty()) {
                            CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
                            CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
                            CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
                            CAmount nTotalTreasuryFaucet = nTreasurySplit + nFaucetSplit;
                            CAmount nBlockVal = nBlockValActual - nTotalTreasuryFaucet;
                            CAmount nPoWMinersSplitTotal = nBlockVal * 25 / 100;
                            CAmount nPartPaymentPerMember = nPoWMinersSplitTotal / vPartPayees.size();
                            CAmount nPartRemainder = nPoWMinersSplitTotal % vPartPayees.size();
                            for (size_t idx = 0; idx < vPartPayees.size(); ++idx) {
                                CAmount amt = nPartPaymentPerMember + (idx == vPartPayees.size() - 1 ? nPartRemainder : 0);
                                if (vPartPayees[idx] == txout.scriptPubKey && amt == txout.nValue) {
                                    sub.type = TransactionRecord::AdamReward;
                                    break;
                                }
                            }
                        }
                    }
                }
            }
            parts.append(sub);
        }
    }
    
    return true;
}

/**
 * Decompose a credit transaction into a record for each received output.
 */
bool TransactionRecord::decomposeCreditTransaction(const CWallet* wallet, const CWalletTx& wtx, QList<TransactionRecord>& parts)
{
    TransactionRecord sub(wtx.GetHash(), wtx.GetTxTime(), wtx.GetTotalSize());
    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut& txout = wtx.vout[nOut];
        isminetype mine = wallet->IsMine(txout);
        if (mine) {
            CTxDestination address;
            sub.idx = (int) nOut; // vout index
            sub.credit = txout.nValue;
            sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
            if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address)) {
                // Received by KRISTA Address
                sub.type = TransactionRecord::RecvWithAddress;
                sub.address = EncodeDestination(address);
            } else {
                // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
                sub.type = TransactionRecord::RecvFromOther;
                sub.address = getValueOrReturnEmpty(wtx.mapValue, "from");
            }
            if (wtx.IsCoinBase()) {
                // Generated
                sub.type = TransactionRecord::Generated;
            }

            parts.append(sub);
        }
    }
    return true;
}

bool TransactionRecord::decomposeSendToSelfTransaction(const CWalletTx& wtx, const CAmount& nCredit,
                                                       const CAmount& nDebit, bool involvesWatchAddress,
                                                       QList<TransactionRecord>& parts)
{
    // Payment to self tx is presented as a single record.
    TransactionRecord sub(wtx.GetHash(), wtx.GetTxTime(), wtx.GetTotalSize());
    // Payment to self by default
    sub.type = TransactionRecord::SendToSelf;
    sub.address = "";

    // Label for payment to self
    CTxDestination address;
    if (ExtractDestination(wtx.vout[0].scriptPubKey, address)) {
        sub.address = EncodeDestination(address);
    }

    CAmount nChange = wtx.GetChange();

    sub.debit = -(nDebit - nChange);
    sub.credit = nCredit - nChange;
    sub.involvesWatchAddress = involvesWatchAddress;
    parts.append(sub);
    return true;
}

/**
 * Decompose wtx outputs in records.
 */
bool TransactionRecord::decomposeDebitTransaction(const CWallet* wallet, const CWalletTx& wtx,
                                                  const CAmount& nDebit, bool involvesWatchAddress,
                                                  QList<TransactionRecord>& parts)
{
    // Return early if there are no outputs.
    if (wtx.vout.empty()) {
        return false;
    }

    CAmount nTxFee = nDebit - wtx.GetValueOut();
    unsigned int txSize = wtx.GetTotalSize();
    const uint256& txHash = wtx.GetHash();
    const int64_t txTime = wtx.GetTxTime();

    for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut& txout = wtx.vout[nOut];

        if (wallet->IsMine(txout)) {
            // Ignore parts sent to self, as this is usually the change
            // from a transaction sent back to our own address.
            continue;
        }

        // Create record
        TransactionRecord sub(txHash, txTime, txSize);
        sub.idx = (int) nOut;
        sub.involvesWatchAddress = involvesWatchAddress;

        CTxDestination address;
        if (ExtractDestination(txout.scriptPubKey, address)) {
            //This is most likely only going to happen when resyncing deterministic wallet without the knowledge of the
            //private keys that the change was sent to. Do not display a "sent to" here.
            
            // Sent to KRISTA Address
            sub.type = TransactionRecord::SendToAddress;
            sub.address = EncodeDestination(address);
        } else {
            // Sent to IP, or other non-address transaction like OP_EVAL
            sub.type = TransactionRecord::SendToOther;
            sub.address = getValueOrReturnEmpty(wtx.mapValue, "to");
        }
        
        CAmount nValue = txout.nValue;
        /* Add fee to first output */
        if (nTxFee > 0) {
            nValue += nTxFee;
            nTxFee = 0;
        }
        sub.debit = -nValue;

        parts.append(sub);
    }
    return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet* wallet, const CWalletTx& wtx)
{
    QList<TransactionRecord> parts;
    CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
    CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
    bool fZSpendFromMe = false;

    // Decompose coinstake if needed (if it's not a coinstake, the method will no perform any action).
    if (decomposeCoinStake(wallet, wtx, nCredit, nDebit, fZSpendFromMe, parts)) {
        return parts;
    }

    // Decompose coinbase if needed
    if (decomposeCoinBase(wallet, wtx, parts)) {
        return parts;
    }

    // Credit/Debit decomposing flow
    CAmount nNet = nCredit - nDebit;

    // Check if the tx is credit and decompose it.
    if (nNet > 0) {
        // Decouple credit tx
        if (decomposeCreditTransaction(wallet, wtx, parts)) {
            return parts;
        }
    }

    // As the tx is not credit, need to check if all the inputs and outputs are from and to this wallet.
    // If it's true, then it's a sendToSelf. If not, then it's an outgoing tx.

    bool involvesWatchAddress = false;
    isminetype fAllFromMe = ISMINE_SPENDABLE;
    for (const CTxIn& txin : wtx.vin) {
        isminetype mine = wallet->IsMine(txin);
        if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if (fAllFromMe > mine) fAllFromMe = mine;
    }

    isminetype fAllToMe = ISMINE_SPENDABLE;
    for (const CTxOut& txout : wtx.vout) {
        isminetype mine = wallet->IsMine(txout);
        if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
        if (fAllToMe > mine) fAllToMe = mine;
    }

    // Check if this tx is purely a payment to self.
    if (fAllFromMe && fAllToMe) {
        // Single record for sendToSelf.
        if (decomposeSendToSelfTransaction(wtx, nCredit, nDebit, involvesWatchAddress, parts)) {
            return parts;
        }
    }

    // Check if the tx is debit and decompose it.
    if (fAllFromMe) {
        if (decomposeDebitTransaction(wallet, wtx, nDebit, involvesWatchAddress, parts)) {
            return parts;
        }
    }

    // if we get to this point, we have a mixed debit transaction, can't break down payees.
    TransactionRecord record(wtx.GetHash(), wtx.GetTxTime(), wtx.GetTotalSize(), TransactionRecord::Other, "", nNet, 0);
    record.involvesWatchAddress = involvesWatchAddress;
    parts.append(record);
    return parts;
}

bool TransactionRecord::ExtractAddress(const CScript& scriptPubKey, std::string& addressStr) {
    CTxDestination address;
    if (!ExtractDestination(scriptPubKey, address)) {
        // this shouldn't happen..
        addressStr = "No available address";
        return false;
    } else {
        addressStr = EncodeDestination(
                address,
                CChainParams::PUBKEY_ADDRESS
        );
        return true;
    }
}


void TransactionRecord::updateStatus(const CWalletTx& wtx)
{
    AssertLockHeld(cs_main);
    int chainHeight = chainActive.Height();

    CBlockIndex *pindex = nullptr;
    // Find the block the tx is in
    BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
    if (mi != mapBlockIndex.end())
        pindex = (*mi).second;

    // Determine transaction status

    // Update time if needed
    int64_t nTxTime = wtx.GetTxTime();
    if (time != nTxTime) time = nTxTime;

    // Sort order, unrecorded transactions sort to the top
    status.sortKey = strprintf("%010d-%01d-%010u-%03d",
        (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
        (wtx.IsCoinBase() ? 1 : 0),
        time,
        idx);

    bool fConflicted = false;
    int depth = 0;
    bool isTrusted = wtx.IsTrusted(depth, fConflicted);
    int nBlocksToMaturity = (wtx.IsCoinBase() || wtx.IsCoinStake()) ? std::max(0, (Params().GetConsensus().nCoinbaseMaturity + 1) - depth) : 0;

    status.countsForBalance = isTrusted && !(nBlocksToMaturity > 0);
    status.cur_num_blocks = chainHeight;
    status.depth = depth;

    if (!IsFinalTx(wtx, chainHeight + 1)) {
        if (wtx.nLockTime < LOCKTIME_THRESHOLD) {
            status.status = TransactionStatus::OpenUntilBlock;
            status.open_for = wtx.nLockTime - chainHeight;
        } else {
            status.status = TransactionStatus::OpenUntilDate;
            status.open_for = wtx.nLockTime;
        }
    }
    // For generated transactions, determine maturity
    else if (type == TransactionRecord::Generated ||
            type == TransactionRecord::StakeMint ||
            type == TransactionRecord::MNReward ||
            type == TransactionRecord::LlmqReward ||
            type == TransactionRecord::AdamReward) {

        if (nBlocksToMaturity > 0) {
            status.status = TransactionStatus::Immature;
            status.matures_in = nBlocksToMaturity;

            if (status.depth < 0 || fConflicted) {
                status.status = TransactionStatus::NotAccepted;
            }
        } else {
            status.status = TransactionStatus::Confirmed;
            status.matures_in = 0;
        }
    } else {
        if (status.depth < 0 || fConflicted) {
            status.status = TransactionStatus::Conflicted;
        } else if (status.depth == 0) {
            status.status = TransactionStatus::Unconfirmed;
        } else if (status.depth < RecommendedNumConfirmations) {
            status.status = TransactionStatus::Confirming;
        } else {
            status.status = TransactionStatus::Confirmed;
        }
    }
}

bool TransactionRecord::statusUpdateNeeded()
{
    AssertLockHeld(cs_main);
    return status.cur_num_blocks != chainActive.Height();
}

QString TransactionRecord::getTxID() const
{
    return QString::fromStdString(hash.ToString());
}

int TransactionRecord::getOutputIndex() const
{
    return idx;
}

bool TransactionRecord::isCoinStake() const
{
    return (type == TransactionRecord::StakeMint || type == TransactionRecord::Generated || type == TransactionRecord::MNReward || type == TransactionRecord::LlmqReward || type == TransactionRecord::AdamReward);
}

bool TransactionRecord::isNull() const
{
    return hash.IsNull() || size == 0;
}

std::string TransactionRecord::statusToString(){
    switch (status.status){
        case TransactionStatus::Confirmed:
            return "Confirmed";
        case TransactionStatus::OpenUntilDate:
            return "OpenUntilDate";
        case TransactionStatus::OpenUntilBlock:
            return "OpenUntilBlock";
        case TransactionStatus::Unconfirmed:
            return "Unconfirmed";
        case TransactionStatus::Confirming:
            return "Confirming";
        case TransactionStatus::Conflicted:
            return "Conflicted";
        case TransactionStatus::Immature:
            return "Immature";
        case TransactionStatus::NotAccepted:
            return "Not Accepted";
        default:
            return "No status";
    }
}
