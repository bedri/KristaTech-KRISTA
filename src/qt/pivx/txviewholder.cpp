// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/pivx/txviewholder.h"
#include "qt/pivx/qtutils.h"
#include "transactiontablemodel.h"
#include <QModelIndex>

#define ADDRESS_SIZE 12

QWidget* TxViewHolder::createHolder(int pos)
{
    if (!txRow) txRow = new TxRow();
    txRow->init(isLightTheme);
    return txRow;
}

void TxViewHolder::init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const
{
    TxRow *txRow = static_cast<TxRow*>(holder);
    txRow->updateStatus(isLightTheme, isHovered, isSelected);

    QModelIndex rIndex = (filter) ? filter->mapToSource(index) : index;
    QDateTime date = rIndex.data(TransactionTableModel::DateRole).toDateTime();
    qint64 amount = rIndex.data(TransactionTableModel::AmountRole).toLongLong();
    QString amountText = BitcoinUnits::formatWithUnit(nDisplayUnit, amount, true, BitcoinUnits::separatorAlways);
    QModelIndex indexType = rIndex.sibling(rIndex.row(),TransactionTableModel::Type);
    QString label = indexType.data(Qt::DisplayRole).toString();
    int type = rIndex.data(TransactionTableModel::TypeRole).toInt();

    if (type != TransactionRecord::Other) {
        QString address = rIndex.data(Qt::DisplayRole).toString();
        if (address.length() > 20) {
            address = address.left(ADDRESS_SIZE) + "..." + address.right(ADDRESS_SIZE);
        }
        label += " " + address;
    } else {
        label += rIndex.data(Qt::DisplayRole).toString();
    }

    int status = rIndex.data(TransactionTableModel::StatusRole).toInt();
    bool isUnconfirmed = (status == TransactionStatus::Unconfirmed) || (status == TransactionStatus::Immature)
                         || (status == TransactionStatus::Conflicted) || (status == TransactionStatus::NotAccepted);

    TransactionRecord* rec = static_cast<TransactionRecord*>(rIndex.internalPointer());
    QString statusStr;
    if (rec) {
        switch (rec->status.status) {
            case TransactionStatus::Confirmed:
                statusStr = QObject::tr("Confirmed (%1)").arg(rec->status.depth);
                break;
            case TransactionStatus::Confirming:
                statusStr = QObject::tr("Confirming (%1/%2)").arg(rec->status.depth).arg(TransactionRecord::RecommendedNumConfirmations);
                break;
            case TransactionStatus::Unconfirmed:
                statusStr = QObject::tr("Unconfirmed");
                break;
            case TransactionStatus::Conflicted:
                statusStr = QObject::tr("Conflicted");
                break;
            case TransactionStatus::Immature:
                statusStr = QObject::tr("Immature (%1/%2)").arg(rec->status.depth).arg(rec->status.depth + rec->status.matures_in);
                break;
            case TransactionStatus::NotAccepted:
                statusStr = QObject::tr("Not Accepted");
                break;
            case TransactionStatus::OpenUntilDate:
            case TransactionStatus::OpenUntilBlock:
                statusStr = QObject::tr("Open");
                break;
            default:
                statusStr = QObject::tr("Unknown");
                break;
        }
    }

    txRow->setDate(date);
    txRow->setLabel(label);
    txRow->setAmount(amountText);
    txRow->setType(isLightTheme, type, !isUnconfirmed);
    txRow->setStatus(statusStr, status);
}

QColor TxViewHolder::rectColor(bool isHovered, bool isSelected)
{
    return getRowColor(isLightTheme, isHovered, isSelected);
}
