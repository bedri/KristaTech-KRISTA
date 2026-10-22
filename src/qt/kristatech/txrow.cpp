// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/txrow.h"
#include "qt/kristatech/forms/ui_txrow.h"

#include "guiutil.h"
#include "qt/kristatech/qtutils.h"
#include <QHash>
#include <QIcon>

TxRow::TxRow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::TxRow)
{
    ui->setupUi(this);
}

void TxRow::init(bool isLightTheme)
{
    setConfirmStatus(true);
    updateStatus(isLightTheme, false, false);
}

void TxRow::setConfirmStatus(bool isConfirm)
{
    if (fConfirmInitialized && this->isConfirmed == isConfirm) {
        return;
    }
    fConfirmInitialized = true;
    this->isConfirmed = isConfirm;
    if (isConfirm) {
        setCssProperty(ui->lblAddress, "text-list-body1", true);
        setCssProperty(ui->lblDate, "text-list-caption", true);
    } else {
        setCssProperty(ui->lblAddress, "text-list-body-unconfirmed", true);
        setCssProperty(ui->lblDate, "text-list-caption-unconfirmed", true);
    }
}

void TxRow::updateStatus(bool isLightTheme, bool isHover, bool isSelected)
{
    const int themeState = isLightTheme ? 1 : 0;
    if (lastThemeState == themeState) {
        return;
    }
    lastThemeState = themeState;
    if (isLightTheme)
        ui->lblDivisory->setStyleSheet("background-color:#bababa");
    else
        ui->lblDivisory->setStyleSheet("background-color:#40ffffff");
}

void TxRow::setDate(QDateTime date)
{
    const QString dateStr = GUIUtil::dateTimeStr(date);
    if (ui->lblDate->text() != dateStr) {
        ui->lblDate->setText(dateStr);
    }
}

void TxRow::setLabel(QString str)
{
    if (ui->lblAddress->text() != str) {
        ui->lblAddress->setText(str);
    }
}

void TxRow::setAmount(QString str)
{
    if (ui->lblAmount->text() != str) {
        ui->lblAmount->setText(str);
    }
}

void TxRow::setStatus(QString statusStr, int statusType)
{
    if (ui->lblStatus->text() != statusStr) {
        ui->lblStatus->setText(statusStr);
    }
    if (lastStatusType == statusType) {
        return;
    }
    lastStatusType = statusType;
    QString css;
    switch (statusType) {
        case TransactionStatus::Confirmed:
            css = "text-list-status-confirmed";
            break;
        case TransactionStatus::Conflicted:
        case TransactionStatus::NotAccepted:
            css = "text-list-status-conflicted";
            break;
        default:
            css = "text-list-status-pending";
            break;
    }
    setCssProperty(ui->lblStatus, css, true);
}

void TxRow::setType(bool isLightTheme, int type, bool isConfirmed)
{
    QString path;
    QString css;
    bool sameIcon = false;
    switch (type) {
        case TransactionRecord::Generated:
        case TransactionRecord::MNReward:
        case TransactionRecord::LlmqReward:
        case TransactionRecord::AdamReward:
        case TransactionRecord::StakeMint:
            path = "://ic-transaction-staked";
            css = "text-list-amount-receive";
            break;
        case TransactionRecord::RecvWithAddress:
        case TransactionRecord::RecvFromOther:
            path = "://ic-transaction-received";
            css = "text-list-amount-receive";
            break;
        case TransactionRecord::SendToAddress:
        case TransactionRecord::SendToOther:
            path = "://ic-transaction-sent";
            css = "text-list-amount-send";
            break;
        case TransactionRecord::SendToSelf:
        case TransactionRecord::MinerRegistration:
            path = "://ic-transaction-mint";
            css = "text-list-amount-send";
            break;
        default:
            path = "://ic-pending";
            sameIcon = true;
            css = "text-list-amount-unconfirmed";
            break;
    }

    if (!isLightTheme && !sameIcon){
        path += "-dark";
    }

    if (!isConfirmed){
        css = "text-list-amount-unconfirmed";
        path += "-inactive";
        setConfirmStatus(false);
    } else {
        setConfirmStatus(true);
    }
    if (lastAmountCss != css) {
        lastAmountCss = css;
        setCssProperty(ui->lblAmount, css, true);
    }
    if (lastIconPath != path) {
        lastIconPath = path;
        static QHash<QString, QIcon> iconCache;
        auto it = iconCache.find(path);
        if (it == iconCache.end()) {
            it = iconCache.insert(path, QIcon(path));
        }
        ui->icon->setIcon(it.value());
    }
}

TxRow::~TxRow()
{
    delete ui;
}
