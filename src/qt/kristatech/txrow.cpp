// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/txrow.h"

#include "guiutil.h"
#include <QHBoxLayout>
#include <QVBoxLayout>

TxRow::TxRow(QWidget *parent) :
    QWidget(parent)
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 0, 14, 0);
    rootLayout->setSpacing(0);

    QWidget* contentRow = new QWidget(this);
    contentRow->setStyleSheet("background: transparent;");
    QHBoxLayout* hLayout = new QHBoxLayout(contentRow);
    hLayout->setContentsMargins(0, 8, 0, 8);
    hLayout->setSpacing(12);

    lblTypeBadge = new QLabel(contentRow);
    lblTypeBadge->setFixedSize(68, 26);
    lblTypeBadge->setAlignment(Qt::AlignCenter);

    QVBoxLayout* midCol = new QVBoxLayout();
    midCol->setContentsMargins(0, 0, 0, 0);
    midCol->setSpacing(2);

    lblAddress = new QLabel(contentRow);
    lblDate = new QLabel(contentRow);
    midCol->addWidget(lblAddress);
    midCol->addWidget(lblDate);

    lblStatus = new QLabel(contentRow);
    lblStatus->setAlignment(Qt::AlignCenter);
    lblStatus->setFixedHeight(22);

    lblAmount = new QLabel(contentRow);
    lblAmount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lblAmount->setMinimumWidth(145);

    hLayout->addWidget(lblTypeBadge, 0, Qt::AlignVCenter);
    hLayout->addLayout(midCol, 1);
    hLayout->addWidget(lblStatus, 0, Qt::AlignVCenter);
    hLayout->addWidget(lblAmount, 0, Qt::AlignVCenter);

    lblDivisory = new QLabel(this);
    lblDivisory->setFixedHeight(1);

    rootLayout->addWidget(contentRow, 1);
    rootLayout->addWidget(lblDivisory);
}

void TxRow::init(bool isLightTheme)
{
    setConfirmStatus(true);
    updateStatus(isLightTheme, false, false);
}

void TxRow::applyTextTheme(bool isLightTheme)
{
    const QString titleCol = isConfirmed
        ? (isLightTheme ? QStringLiteral("#0F172A") : QStringLiteral("#F8FAFC"))
        : (isLightTheme ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8"));
    const QString dateCol = isLightTheme ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString divCol  = isLightTheme ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2C46");

    lblAddress->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }").arg(titleCol));
    lblDate->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 500; background: transparent; border: none; }").arg(dateCol));
    lblDivisory->setStyleSheet(QString("QLabel { background-color: %1; border: none; }").arg(divCol));
}

void TxRow::setConfirmStatus(bool isConfirm)
{
    if (fConfirmInitialized && this->isConfirmed == isConfirm) {
        return;
    }
    fConfirmInitialized = true;
    this->isConfirmed = isConfirm;
    const bool light = (lastThemeState == 1);
    applyTextTheme(light);
}

void TxRow::updateStatus(bool isLightTheme, bool isHover, bool isSelected)
{
    Q_UNUSED(isHover);
    Q_UNUSED(isSelected);
    const int themeState = isLightTheme ? 1 : 0;
    if (lastThemeState == themeState) {
        return;
    }
    lastThemeState = themeState;
    applyTextTheme(isLightTheme);
}

void TxRow::setDate(QDateTime date)
{
    const QString dateStr = GUIUtil::dateTimeStr(date);
    if (lblDate->text() != dateStr) {
        lblDate->setText(dateStr);
    }
}

void TxRow::setLabel(QString str)
{
    if (lblAddress->text() != str) {
        lblAddress->setText(str);
    }
}

void TxRow::setAmount(QString str)
{
    if (lblAmount->text() != str) {
        lblAmount->setText(str);
    }
}

void TxRow::setStatus(QString statusStr, int statusType)
{
    if (lblStatus->text() != statusStr) {
        lblStatus->setText(statusStr);
    }
    const int themeState = (lastThemeState == 1) ? 1 : 0;
    if (lastStatusType == statusType && lastStatusTheme == themeState) {
        return;
    }
    lastStatusType = statusType;
    lastStatusTheme = themeState;

    const bool light = (themeState == 1);
    QString fg, bg, brd;
    switch (statusType) {
        case TransactionStatus::Confirmed:
            fg  = light ? "#047857" : "#4ADE80";
            bg  = light ? "rgba(5, 150, 105, 0.10)" : "rgba(16, 185, 129, 0.14)";
            brd = light ? "rgba(5, 150, 105, 0.32)" : "rgba(16, 185, 129, 0.36)";
            break;
        case TransactionStatus::Immature:
            fg  = light ? "#B45309" : "#FBBF24";
            bg  = light ? "rgba(217, 119, 6, 0.10)" : "rgba(245, 158, 11, 0.14)";
            brd = light ? "rgba(217, 119, 6, 0.34)" : "rgba(245, 158, 11, 0.38)";
            break;
        case TransactionStatus::Conflicted:
        case TransactionStatus::NotAccepted:
            fg  = light ? "#B91C1C" : "#F87171";
            bg  = light ? "rgba(220, 38, 38, 0.10)" : "rgba(239, 68, 68, 0.14)";
            brd = light ? "rgba(220, 38, 38, 0.34)" : "rgba(239, 68, 68, 0.38)";
            break;
        default:
            fg  = light ? "#0369A1" : "#7DD3FC";
            bg  = light ? "rgba(2, 132, 199, 0.10)" : "rgba(56, 189, 248, 0.14)";
            brd = light ? "rgba(2, 132, 199, 0.32)" : "rgba(56, 189, 248, 0.36)";
            break;
    }

    lblStatus->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
    ).arg(fg, bg, brd));
}

void TxRow::setType(bool isLightTheme, int type, bool isConfirmed)
{
    setConfirmStatus(isConfirmed);

    const int themeState = isLightTheme ? 1 : 0;
    if (fTypeInitialized && lastTxType == type && lastTypeTheme == themeState && lastTypeConfirmed == isConfirmed) {
        return;
    }
    fTypeInitialized = true;
    lastTxType = type;
    lastTypeTheme = themeState;
    lastTypeConfirmed = isConfirmed;

    QString badgeText;
    QString fg, bg, brd;
    bool isInflow = false;
    bool isOutflow = false;

    switch (type) {
        case TransactionRecord::StakeMint:
            badgeText = QStringLiteral("PoS");
            fg  = isLightTheme ? "#BE123C" : "#F5897C";
            bg  = isLightTheme ? "rgba(225, 29, 72, 0.10)" : "rgba(245, 137, 124, 0.16)";
            brd = isLightTheme ? "rgba(225, 29, 72, 0.34)" : "rgba(245, 137, 124, 0.42)";
            isInflow = true;
            break;
        case TransactionRecord::MNReward:
            badgeText = QStringLiteral("MN");
            fg  = isLightTheme ? "#1D4ED8" : "#93B4F0";
            bg  = isLightTheme ? "rgba(37, 99, 235, 0.10)" : "rgba(118, 149, 205, 0.18)";
            brd = isLightTheme ? "rgba(37, 99, 235, 0.34)" : "rgba(118, 149, 205, 0.45)";
            isInflow = true;
            break;
        case TransactionRecord::LlmqReward:
            badgeText = QStringLiteral("LLMQ");
            fg  = isLightTheme ? "#6D28D9" : "#C4B5FD";
            bg  = isLightTheme ? "rgba(124, 58, 237, 0.10)" : "rgba(139, 92, 246, 0.18)";
            brd = isLightTheme ? "rgba(124, 58, 237, 0.34)" : "rgba(139, 92, 246, 0.45)";
            isInflow = true;
            break;
        case TransactionRecord::AdamReward:
            badgeText = QStringLiteral("ADAM");
            fg  = isLightTheme ? "#047857" : "#4ADE80";
            bg  = isLightTheme ? "rgba(5, 150, 105, 0.10)" : "rgba(16, 185, 129, 0.16)";
            brd = isLightTheme ? "rgba(5, 150, 105, 0.34)" : "rgba(16, 185, 129, 0.42)";
            isInflow = true;
            break;
        case TransactionRecord::Generated:
            badgeText = QStringLiteral("PoW");
            fg  = isLightTheme ? "#B45309" : "#FBBF24";
            bg  = isLightTheme ? "rgba(217, 119, 6, 0.10)" : "rgba(245, 158, 11, 0.16)";
            brd = isLightTheme ? "rgba(217, 119, 6, 0.34)" : "rgba(245, 158, 11, 0.42)";
            isInflow = true;
            break;
        case TransactionRecord::RecvWithAddress:
        case TransactionRecord::RecvFromOther:
            badgeText = QString::fromUtf8("\xE2\x86\x93 IN");
            fg  = isLightTheme ? "#047857" : "#4ADE80";
            bg  = isLightTheme ? "rgba(5, 150, 105, 0.10)" : "rgba(16, 185, 129, 0.16)";
            brd = isLightTheme ? "rgba(5, 150, 105, 0.34)" : "rgba(16, 185, 129, 0.42)";
            isInflow = true;
            break;
        case TransactionRecord::SendToAddress:
        case TransactionRecord::SendToOther:
            badgeText = QString::fromUtf8("\xE2\x86\x91 OUT");
            fg  = isLightTheme ? "#B91C1C" : "#F87171";
            bg  = isLightTheme ? "rgba(220, 38, 38, 0.10)" : "rgba(239, 68, 68, 0.16)";
            brd = isLightTheme ? "rgba(220, 38, 38, 0.34)" : "rgba(239, 68, 68, 0.42)";
            isOutflow = true;
            break;
        case TransactionRecord::SendToSelf:
            badgeText = QString::fromUtf8("\xE2\x86\xBB SELF");
            fg  = isLightTheme ? "#334155" : "#CBD5E1";
            bg  = isLightTheme ? "rgba(100, 116, 139, 0.12)" : "rgba(148, 163, 184, 0.16)";
            brd = isLightTheme ? "rgba(100, 116, 139, 0.30)" : "rgba(148, 163, 184, 0.36)";
            break;
        case TransactionRecord::MinerRegistration:
            badgeText = QString::fromUtf8("\xE2\x9A\x99 REG");
            fg  = isLightTheme ? "#0369A1" : "#7DD3FC";
            bg  = isLightTheme ? "rgba(2, 132, 199, 0.10)" : "rgba(56, 189, 248, 0.16)";
            brd = isLightTheme ? "rgba(2, 132, 199, 0.34)" : "rgba(56, 189, 248, 0.42)";
            break;
        default:
            badgeText = QStringLiteral("TX");
            fg  = isLightTheme ? "#475569" : "#94A3B8";
            bg  = isLightTheme ? "rgba(100, 116, 139, 0.10)" : "rgba(148, 163, 184, 0.14)";
            brd = isLightTheme ? "rgba(100, 116, 139, 0.28)" : "rgba(148, 163, 184, 0.32)";
            break;
    }

    lblTypeBadge->setText(badgeText);
    lblTypeBadge->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 6px; font-size: 11px; font-weight: 700; }"
    ).arg(fg, bg, brd));

    QString amtCol;
    if (!isConfirmed) {
        amtCol = isLightTheme ? "#64748B" : "#94A3B8";
    } else if (isInflow) {
        amtCol = isLightTheme ? "#047857" : "#4ADE80";
    } else if (isOutflow) {
        amtCol = isLightTheme ? "#DC2626" : "#F87171";
    } else {
        amtCol = isLightTheme ? "#1E293B" : "#E2E8F0";
    }

    lblAmount->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 13px; font-weight: 700; background: transparent; border: none; }"
    ).arg(amtCol));
}

TxRow::~TxRow()
{
}
