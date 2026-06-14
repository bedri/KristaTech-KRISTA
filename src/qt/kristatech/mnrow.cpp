// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
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
    ui->lblDivisory->setStyleSheet("background-color:#bababa;");
}

void MNRow::updateView(QString address, QString label, QString status, bool wasCollateralAccepted, QString txId)
{
    ui->labelName->setText(label);
    ui->labelAddress->setText(address);
    if (!wasCollateralAccepted) status = tr("Collateral tx not found");
    ui->labelDate->setText(status);
    
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
