// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/guitransactionsutils.h"
#include "qt/kristatech/defaultdialog.h"
#include "bitcoinunits.h"
#include "optionsmodel.h"

namespace GuiTransactionsUtils {

    QString ProcessSendCoinsReturn(PWidget::Translator *parent, const WalletModel::SendCoinsReturn &sendCoinsReturn,
                                WalletModel *walletModel, CClientUIInterface::MessageBoxFlags& informType, const QString &msgArg,
                                bool fPrepare)
    {
        QString retStr;
        informType = CClientUIInterface::MSG_WARNING;
        // This comment is specific to SendCoinsDialog usage of WalletModel::SendCoinsReturn.
        // WalletModel::TransactionCheckFailed and WalletModel::TransactionCommitFailed
        // are used only in WalletModel::sendCoins(). All others are used only in WalletModel::prepareTransaction()
        switch (sendCoinsReturn.status) {
            case WalletModel::InvalidAddress:
                retStr = parent->translate("The recipient address is not valid, please recheck.");
                break;
            case WalletModel::InvalidAmount:
                retStr = parent->translate("The amount to pay must be larger than 0.");
                break;
            case WalletModel::AmountExceedsBalance:
                retStr = parent->translate("The amount to pay exceeds the available balance.");
                break;
            case WalletModel::AmountWithFeeExceedsBalance:
                retStr = parent->translate(
                        "The total amount to pay exceeds the available balance when the %1 transaction fee is included.").arg(msgArg);
                break;
            case WalletModel::DuplicateAddress:
                retStr = parent->translate(
                        "Duplicate address found, can only send to each address once per send operation.");
                break;
            case WalletModel::TransactionCreationFailed:
                informType = CClientUIInterface::MSG_ERROR;
                break;
            case WalletModel::TransactionTooLarge:
                retStr = parent->translate(
                        "The size of the transaction is too big.\nConsolidate small UTXOs or select fewer inputs with coin control.");
                informType = CClientUIInterface::MSG_ERROR;
                break;
            case WalletModel::TransactionCheckFailed:
                retStr = parent->translate("The transaction is not valid!");
                informType = CClientUIInterface::MSG_ERROR;
                break;
            case WalletModel::TransactionCommitFailed:
                retStr = QString::fromStdString(sendCoinsReturn.commitRes.ToString());
                informType = CClientUIInterface::MSG_ERROR;
                break;
            case WalletModel::StakingOnlyUnlocked:
                // Unlock is only need when the coins are send
                if (!fPrepare) {
                    // Unlock wallet if it wasn't fully unlocked already
                    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
                    if (!ctx.isValid()) {
                        retStr = parent->translate(
                                "Error: The wallet was unlocked for staking only. Unlock canceled.");
                    }
                } else
                    retStr = parent->translate("Error: The wallet is unlocked for staking only. Fully unlock the wallet to send the transaction.");
                break;
            case WalletModel::InsaneFee:
                retStr = parent->translate(
                        "A fee %1 times higher than %2 per kB is considered an insanely high fee.").arg(10000).arg(
                        BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(),
                                                     ::minRelayTxFee.GetFeePerK()));
                break;
            // included to prevent a compiler warning.
            case WalletModel::OK:
            default:
                return retStr; // No issue
        }

        return retStr;
    }

    void ProcessSendCoinsReturnAndInform(PWidget* parent, const WalletModel::SendCoinsReturn& sendCoinsReturn, WalletModel* walletModel, const QString& msgArg, bool fPrepare)
    {
        CClientUIInterface::MessageBoxFlags informType;
        QString informMsg = ProcessSendCoinsReturn(parent, sendCoinsReturn, walletModel, informType, msgArg, fPrepare);
        if (!informMsg.isEmpty()) parent->emitMessage(parent->translate("Send Coins"), informMsg, informType, 0);
    }

    bool OfferUtxoConsolidation(QWidget* parent, WalletModel* walletModel, CAmount targetAmount, QString& errorOut, bool* awaitingConfirmationOut)
    {
        if (awaitingConfirmationOut) {
            *awaitingConfirmationOut = false;
        }
        if (!walletModel) {
            errorOut = QObject::tr("Wallet is not available.");
            return false;
        }
        WalletModel::ConsolidationPlan plan;
        if (!walletModel->previewConsolidation(targetAmount, plan, errorOut)) {
            return false;
        }
        static constexpr int MAX_ZERO_CONF_CONSOLIDATION_BYTES = 90000;
        const bool needsConfirmation = !bSpendZeroConfChange || plan.totalBytes > MAX_ZERO_CONF_CONSOLIDATION_BYTES;
        const int totalKb = (plan.totalBytes + 512) / 1024;
        const int limitKb = MAX_ZERO_CONF_CONSOLIDATION_BYTES / 1024;
        const QString followUpNote = needsConfirmation
            ? QObject::tr("\n\nNote: This consolidation (%1 KB) exceeds the instant zero-conf mempool limit (%2 KB) or requires a block confirmation, so you will need to wait 1 confirmation (~30–60s) before retrying.")
                  .arg(totalKb)
                  .arg(limitKb)
            : QObject::tr("\n\nOnce consolidated, your Masternode/Send transaction will automatically continue.");
        const int dispUnit = walletModel->getOptionsModel() ? walletModel->getOptionsModel()->getDisplayUnit() : BitcoinUnits::PIV;
        DefaultDialog* dlg = new DefaultDialog(parent);
        dlg->setText(
            QObject::tr("Consolidate Fragmented UTXOs"),
            QObject::tr("This transaction exceeds the maximum standard transaction size because your wallet has many small UTXOs.\n\n"
                        "Would you like to reorganize and combine %1 small UTXOs in %2 batch(es) (%3 total, est. fee %4) to your own wallet address first?%5")
                .arg(plan.inputCount)
                .arg(plan.batchCount)
                .arg(BitcoinUnits::formatWithUnit(dispUnit, plan.totalInputAmount, false, BitcoinUnits::separatorAlways))
                .arg(BitcoinUnits::formatWithUnit(dispUnit, plan.estimatedFee, false, BitcoinUnits::separatorAlways))
                .arg(followUpNote),
            QObject::tr("CONSOLIDATE"),
            QObject::tr("CANCEL")
        );
        dlg->adjustSize();
        const int res = dlg->exec();
        dlg->deleteLater();
        if (res != QDialog::Accepted) {
            errorOut = QObject::tr("UTXO consolidation cancelled by user.");
            return false;
        }
        if (!walletModel->executeConsolidation(targetAmount, plan, errorOut)) {
            return false;
        }
        if (!bSpendZeroConfChange) {
            if (awaitingConfirmationOut) {
                *awaitingConfirmationOut = true;
            }
            errorOut = QObject::tr("UTXOs consolidated in %1 transaction(s). Because -spendzeroconfchange=0 is enabled, please wait 1 confirmation before retrying.")
                           .arg(plan.batchCount);
            return false;
        }
        if (plan.totalBytes > MAX_ZERO_CONF_CONSOLIDATION_BYTES) {
            if (awaitingConfirmationOut) {
                *awaitingConfirmationOut = true;
            }
            errorOut = QObject::tr("Successfully consolidated %1 UTXOs into %2 transaction(s) (%3 total, %4 KB). Please wait 1 confirmation (~30–60 seconds) and retry to complete the transaction.")
                           .arg(plan.inputCount)
                           .arg(plan.batchCount)
                           .arg(BitcoinUnits::formatWithUnit(dispUnit, plan.totalInputAmount, false, BitcoinUnits::separatorAlways))
                           .arg((plan.totalBytes + 512) / 1024);
            return false;
        }
        return true;
    }

}
