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
    setMinimumWidth(620);
    setCssProperty(ui->frame, "container-dialog");
    setCssProperty(ui->labelTitle, "text-title-dialog");
    setCssTextBodyDialog({ui->labelAmount, ui->labelSend, ui->labelInputs, ui->labelFee, ui->labelId});
    setCssProperty({ui->labelDivider1, ui->labelDivider4, ui->labelDivider6, ui->labelDivider7, ui->labelDivider8, ui->labelDivider9}, "container-divider");
    setCssTextBodyDialog({ui->textAmount, ui->textAddress, ui->textInputs, ui->textStatus, ui->textId, ui->textExport});
    setCssProperty({ui->pushCopy, ui->pushCopyId, ui->pushExport}, "ic-copy-big");
    setCssProperty(ui->btnEsc, "ic-close");
    ui->pushCopy->setCursor(Qt::PointingHandCursor);
    ui->pushCopyId->setCursor(Qt::PointingHandCursor);
    ui->pushExport->setCursor(Qt::PointingHandCursor);
    ui->pushCopy->setToolTip(tr("Copy full Public Key"));
    ui->pushCopyId->setToolTip(tr("Copy full Collateral TxID"));
    ui->pushExport->setToolTip(tr("Export activemasternode.conf operator entry"));
    ui->textExport->setWordWrap(true);
    ui->textExport->setText(tr("Export Remote Operator entry for activemasternode.conf (<alias> <privkey> <bls_privkey_hex>)"));

    if (ui->frame && ui->frame->layout()) {
        ui->frame->layout()->setContentsMargins(24, 18, 24, 20);
    }
    if (ui->scrollArea) {
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->scrollArea->setWidgetResizable(true);
    }
    if (ui->content && ui->content->layout()) {
        ui->content->layout()->setContentsMargins(4, 4, 4, 4);
    }

    // Add 1-click Cold Controller masternode.conf / ProTx reference copy row
    if (ui->content && ui->content->layout()) {
        QWidget* rowCtrl = new QWidget(ui->content);
        QHBoxLayout* layoutCtrl = new QHBoxLayout(rowCtrl);
        layoutCtrl->setContentsMargins(0, 8, 0, 8);
        QLabel* lblCtrl = new QLabel(tr("Copy Controller / ProTx reference (masternode.conf / txid:vout)"), rowCtrl);
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
    ui->labelTitle->setText(name.isEmpty() ? tr("Masternode Information") : tr("Masternode Information — %1").arg(name));
    QString cleanAlias = name.simplified().replace(' ', '_');
    if (cleanAlias.isEmpty()) cleanAlias = "mn1";
    const bool hasPrivKey = (!privKey.isEmpty() && privKey != QStringLiteral("Not available"));
    if (hasPrivKey) {
        this->controllerConfLine = QString("%1 %2 %3 %4 %5").arg(cleanAlias, address, privKey, txId, outputIndex);
    } else if (!txId.isEmpty()) {
        this->controllerConfLine = QString("%1 %2 %3 %4").arg(cleanAlias, address, txId, outputIndex);
    } else {
        this->controllerConfLine.clear();
    }
    if (ui->contentExport) ui->contentExport->setVisible(hasPrivKey);
    if (ui->labelDivider7) ui->labelDivider7->setVisible(hasPrivKey);

    QString shortPubKey = pubKey;
    QString shortTxId = txId;
    if (shortPubKey.length() > 28) {
        shortPubKey = shortPubKey.left(16) + "..." + shortPubKey.right(16);
    }
    if (shortTxId.length() > 28) {
        shortTxId = shortTxId.left(16) + "..." + shortTxId.right(16);
    }
    ui->textId->setText(shortPubKey);
    ui->textAddress->setText(address);
    ui->textAmount->setText(shortTxId);
    ui->textInputs->setText(outputIndex);
    ui->textStatus->setText(status);

    const bool light = isLightTheme();
    QString statusColor = light ? "#475569" : "#CBD5E1";
    if (status == "ENABLED") statusColor = light ? "#3E5B94" : "#A8C1EE";
    else if (status == "PRE_ENABLED") statusColor = light ? "#2563EB" : "#60A5FA";
    else if (status == "EXPIRED" || status == "NEW_START_REQUIRED") statusColor = light ? "#B45309" : "#FBBF24";
    else if (status == "MISSING" || status == "POSE_BAN") statusColor = light ? "#DC2626" : "#F87171";
    ui->textStatus->setStyleSheet(QString("color: %1; font-weight: 700;").arg(statusColor));

    if (ui->content && ui->scrollArea) {
        ui->content->adjustSize();
        ui->scrollArea->setMinimumHeight(ui->content->sizeHint().height() + 12);
    }
    adjustSize();
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
