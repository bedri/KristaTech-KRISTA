// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/mnrow.h"
#include "qt/kristatech/forms/ui_mnrow.h"
#include "qt/kristatech/qtutils.h"

MNRow::MNRow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MNRow)
{
    ui->setupUi(this);
    setCssProperty(ui->labelAddress, "text-list-body2");
    setCssProperty(ui->labelName, "text-list-title1");
    setCssProperty(ui->labelDate, "text-list-caption-medium");
    setCssProperty(ui->labelTxId, "text-list-body2");
    setCssProperty(ui->pushButtonInfo, "btn-list-action");
    setCssProperty(ui->pushButtonStart, "btn-list-action");
    setCssProperty(ui->pushButtonDelete, "btn-list-action");
    ui->pushButtonDelete->setStyleSheet(
        "QPushButton#pushButtonDelete:hover { color: #DC2626; border-color: #EF4444; background-color: rgba(239, 68, 68, 0.12); }"
    );
    ui->labelDate->setAlignment(Qt::AlignCenter);
    ui->labelDate->setMaximumHeight(24);
    ui->lblDivisory->setStyleSheet("background-color: rgba(148, 163, 184, 0.35);");
}

void MNRow::updateView(QString address, QString label, QString status, bool wasCollateralAccepted, QString txId, bool isWalletOwned)
{
    ui->labelName->setText(label);
    ui->labelAddress->setText(address);
    ui->pushButtonStart->setVisible(isWalletOwned);
    ui->pushButtonDelete->setVisible(isWalletOwned);
    if (!wasCollateralAccepted && status != "ENABLED" && status != "PRE_ENABLED") {
        status = tr("Collateral tx not found");
    }
    ui->labelDate->setText(status);

    const bool light = isLightTheme();
    QString pillColor = light ? "#475569" : "#CBD5E1";
    QString pillBg = "rgba(100, 116, 139, 0.14)";
    QString pillBorder = "rgba(100, 116, 139, 0.35)";
    if (status == "ENABLED") {
        pillColor = light ? "#3E5B94" : "#A8C1EE";
        pillBg = light ? "rgba(118, 149, 205, 0.14)" : "rgba(118, 149, 205, 0.18)";
        pillBorder = "rgba(118, 149, 205, 0.45)";
    } else if (status == "PRE_ENABLED") {
        pillColor = "#2563EB";
        pillBg = "rgba(59, 130, 246, 0.14)";
        pillBorder = "rgba(59, 130, 246, 0.40)";
    } else if (status == "EXPIRED" || status == "NEW_START_REQUIRED" || status == "WATCHDOG_EXPIRED") {
        pillColor = "#D97706";
        pillBg = "rgba(245, 158, 11, 0.14)";
        pillBorder = "rgba(245, 158, 11, 0.40)";
    } else {
        pillColor = "#DC2626";
        pillBg = "rgba(239, 68, 68, 0.14)";
        pillBorder = "rgba(239, 68, 68, 0.40)";
    }
    ui->labelDate->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; "
        "border-radius: 10px; padding: 2px 8px; font-size: 11px; font-weight: 600; }"
    ).arg(pillColor, pillBg, pillBorder));
    
    if (txId.length() > 16) {
        ui->labelTxId->setText(txId.left(8) + "..." + txId.right(8));
    } else {
        ui->labelTxId->setText(txId);
    }
}

MNRow::~MNRow()
{
    delete ui;
}
