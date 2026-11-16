// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/mninfodialog.h"
#include "qt/kristatech/forms/ui_mninfodialog.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "guiutil.h"
#include "qt/kristatech/qtutils.h"
#include <QDateTime>

MnInfoDialog::MnInfoDialog(QWidget *parent) :
    FocusedDialog(parent),
    ui(new Ui::MnInfoDialog)
{
    ui->setupUi(this);
    this->setStyleSheet(parent->styleSheet());
    setCssProperty(ui->frame, "container-dialog");
    setCssProperty(ui->labelTitle, "text-title-dialog");
    setCssTextBodyDialog({ui->labelAmount, ui->labelSend, ui->labelInputs, ui->labelFee, ui->labelId});
    setCssProperty({ui->labelDivider1, ui->labelDivider4, ui->labelDivider6, ui->labelDivider7, ui->labelDivider8, ui->labelDivider9}, "container-divider");
    setCssTextBodyDialog({ui->textAmount, ui->textAddress, ui->textInputs, ui->textStatus, ui->textId, ui->textExport});
    setCssProperty({ui->pushCopy, ui->pushCopyId, ui->pushExport}, "ic-copy-big");
    setCssProperty(ui->btnEsc, "ic-close");
    ui->textExport->setText(tr("Export Rule-8 Remote Operator entry for activemasternode.conf (<alias> <privkey>)"));

    // Add 1-click Cold Controller masternode.conf copy row
    if (ui->content && ui->content->layout()) {
        QWidget* rowCtrl = new QWidget(ui->content);
        QHBoxLayout* layoutCtrl = new QHBoxLayout(rowCtrl);
        layoutCtrl->setContentsMargins(0, 8, 0, 8);
        QLabel* lblCtrl = new QLabel(tr("Copy Cold Controller entry (masternode.conf line)"), rowCtrl);
        lblCtrl->setWordWrap(true);
        setCssTextBodyDialog(lblCtrl);
        QPushButton* btnCopyCtrl = new QPushButton(rowCtrl);
        btnCopyCtrl->setFixedSize(34, 34);
        btnCopyCtrl->setFocusPolicy(Qt::NoFocus);
        btnCopyCtrl->setCursor(Qt::PointingHandCursor);
        setCssProperty(btnCopyCtrl, "ic-copy-big");
        layoutCtrl->addWidget(lblCtrl, 3);
        layoutCtrl->addWidget(btnCopyCtrl, 0);
        ui->content->layout()->addWidget(rowCtrl);
        connect(btnCopyCtrl, &QPushButton::clicked, [this]() {
            if (!controllerConfLine.isEmpty()) {
                copyInform(controllerConfLine, tr("masternode.conf controller line copied"));
            }
        });
    }

    connect(ui->btnEsc, &QPushButton::clicked, this, &MnInfoDialog::close);
    connect(ui->pushCopy, &QPushButton::clicked, [this](){ copyInform(pubKey, tr("Masternode public key copied")); });
    connect(ui->pushCopyId, &QPushButton::clicked, [this](){ copyInform(txId, tr("Collateral tx id copied")); });
    connect(ui->pushExport, &QPushButton::clicked, [this](){ exportMN = true; accept(); });
}

void MnInfoDialog::setData(QString pubKey, QString name, QString address, QString txId, QString outputIndex, QString status, QString privKey)
{
    this->pubKey = pubKey;
    this->txId = txId;
    QString cleanAlias = name.simplified().replace(' ', '_');
    if (cleanAlias.isEmpty()) cleanAlias = "mn1";
    if (!privKey.isEmpty()) {
        this->controllerConfLine = QString("%1 %2 %3 %4 %5").arg(cleanAlias, address, privKey, txId, outputIndex);
    } else {
        this->controllerConfLine.clear();
    }
    QString shortPubKey = pubKey;
    QString shortTxId = txId;
    if (shortPubKey.length() > 20) {
        shortPubKey = shortPubKey.left(13) + "..." + shortPubKey.right(13);
    }
    if (shortTxId.length() > 20) {
        shortTxId = shortTxId.left(12) + "..." + shortTxId.right(12);
    }
    ui->textId->setText(shortPubKey);
    ui->textAddress->setText(address);
    ui->textAmount->setText(shortTxId);
    ui->textInputs->setText(outputIndex);
    ui->textStatus->setText(status);
}

void MnInfoDialog::copyInform(QString& copyStr, QString message)
{
    GUIUtil::setClipboard(copyStr);
    if (!snackBar) snackBar = new SnackBar(nullptr, this);
    snackBar->setText(tr(message.toStdString().c_str()));
    snackBar->resize(this->width(), snackBar->height());
    openDialog(snackBar, this);
}

void MnInfoDialog::reject()
{
    if (snackBar && snackBar->isVisible()) snackBar->hide();
    QDialog::reject();
}

MnInfoDialog::~MnInfoDialog()
{
    if (snackBar) delete snackBar;
    delete ui;
}
