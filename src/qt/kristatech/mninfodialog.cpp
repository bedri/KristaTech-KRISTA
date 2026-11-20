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
#include <QLocale>
#include <algorithm>

static QString elideCenterHex(const QString& val, int edge = 16)
{
    if (val.length() > edge * 2 + 3) {
        return val.left(edge) + QStringLiteral("...") + val.right(edge);
    }
    return val;
}

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
    ui->labelId->setText(tr("Owner Key Hash:"));
    ui->pushCopy->setCursor(Qt::PointingHandCursor);
    ui->pushCopy->setFocusPolicy(Qt::TabFocus);
    ui->pushCopyId->setCursor(Qt::PointingHandCursor);
    ui->pushCopyId->setFocusPolicy(Qt::TabFocus);
    ui->pushExport->setCursor(Qt::PointingHandCursor);
    ui->pushExport->setFocusPolicy(Qt::TabFocus);
    ui->pushCopy->setToolTip(tr("Copy full ProTx hash or owner key hash"));
    ui->pushCopyId->setToolTip(tr("Copy full Collateral TxID"));
    ui->pushExport->setToolTip(tr("Export activemasternode.conf operator entry"));
    ui->textExport->setWordWrap(true);
    ui->textExport->setText(tr("Export Remote Operator entry for activemasternode.conf (<alias> <privkey> <bls_privkey_hex>)"));

    if (ui->frame && ui->frame->layout()) {
        ui->frame->layout()->setContentsMargins(24, 18, 24, 20);
    }
    if (ui->scrollArea) {
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->scrollArea->setWidgetResizable(true);
    }
    if (ui->content && ui->content->layout()) {
        ui->content->layout()->setContentsMargins(4, 4, 4, 4);

        auto addMetaRow = [this](const QString& title, QWidget*& rowOut, QLabel*& valOut, QPushButton** copyBtnOut = nullptr) {
            rowOut = new QWidget(ui->content);
            QHBoxLayout* h = new QHBoxLayout(rowOut);
            h->setContentsMargins(0, 6, 0, 6);
            QLabel* lblKey = new QLabel(title, rowOut);
            valOut = new QLabel(QStringLiteral("—"), rowOut);
            valOut->setTextInteractionFlags(Qt::TextSelectableByMouse);
            setCssTextBodyDialog({lblKey, valOut});
            h->addWidget(lblKey, 0);
            h->addWidget(valOut, 1);
            if (copyBtnOut) {
                QPushButton* btn = new QPushButton(rowOut);
                btn->setFixedSize(34, 34);
                btn->setFocusPolicy(Qt::TabFocus);
                btn->setCursor(Qt::PointingHandCursor);
                setCssProperty(btn, "ic-copy-big");
                h->addWidget(btn, 0);
                *copyBtnOut = btn;
            }
            ui->content->layout()->addWidget(rowOut);
            rowOut->setVisible(false);
        };

        QPushButton* btnCopyBls = nullptr;
        QPushButton* btnCopyPayout = nullptr;
        addMetaRow(tr("Operator BLS Key:"), rowOperatorBls, valOperatorBls, &btnCopyBls);
        addMetaRow(tr("Payout Address:"), rowPayout, valPayout, &btnCopyPayout);
        addMetaRow(tr("PoSe Penalty:"), rowPose, valPose);
        addMetaRow(tr("Last Paid / Registered:"), rowHeights, valHeights);
        if (btnCopyBls) {
            btnCopyBls->setToolTip(tr("Copy Operator BLS Public Key"));
            connect(btnCopyBls, &QPushButton::clicked, this, [this]() {
                if (!dip3Meta.operatorBlsPubKey.isEmpty()) {
                    copyInform(dip3Meta.operatorBlsPubKey, tr("Operator BLS key copied"));
                }
            });
        }
        if (btnCopyPayout) {
            btnCopyPayout->setToolTip(tr("Copy Payout Address"));
            connect(btnCopyPayout, &QPushButton::clicked, this, [this]() {
                if (!dip3Meta.payoutAddress.isEmpty()) {
                    copyInform(dip3Meta.payoutAddress, tr("Payout address copied"));
                }
            });
        }
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
        btnCopyCtrl->setFocusPolicy(Qt::TabFocus);
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
    connect(ui->pushCopy, &QPushButton::clicked, [this]() {
        if (dip3Meta.isDip3 && !dip3Meta.proTxHash.isEmpty()) {
            copyInform(dip3Meta.proTxHash, tr("ProTx hash copied"));
        } else {
            copyInform(pubKey, tr("Owner key hash copied"));
        }
    });
    connect(ui->pushCopyId, &QPushButton::clicked, [this](){ copyInform(txId, tr("Collateral tx id copied")); });
    connect(ui->pushExport, &QPushButton::clicked, [this](){ exportMN = true; accept(); });
}

void MnInfoDialog::refreshScrollSize()
{
    if (ui->content && ui->scrollArea) {
        ui->content->adjustSize();
        const int targetH = std::min(ui->content->sizeHint().height() + 12, 520);
        ui->scrollArea->setMinimumHeight(targetH);
    }
    adjustSize();
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

    ui->labelId->setText(tr("Owner Key Hash:"));
    ui->textId->setText(elideCenterHex(pubKey));
    ui->textAddress->setText(address);
    ui->textAmount->setText(elideCenterHex(txId));
    ui->textInputs->setText(outputIndex);
    ui->textStatus->setText(status);

    const bool light = isLightTheme();
    QString statusColor = light ? "#475569" : "#CBD5E1";
    if (status == "ENABLED") statusColor = light ? "#3E5B94" : "#A8C1EE";
    else if (status == "PRE_ENABLED") statusColor = light ? "#2563EB" : "#60A5FA";
    else if (status == "EXPIRED" || status == "NEW_START_REQUIRED") statusColor = light ? "#B45309" : "#FBBF24";
    else if (status == "MISSING" || status == "POSE_BAN") statusColor = light ? "#DC2626" : "#F87171";
    ui->textStatus->setStyleSheet(QString("color: %1; font-weight: 700;").arg(statusColor));

    refreshScrollSize();
}

void MnInfoDialog::setDip3Meta(const MNModel::Dip3Meta& meta)
{
    dip3Meta = meta;
    if (!meta.isDip3) {
        if (rowOperatorBls) rowOperatorBls->setVisible(false);
        if (rowPayout) rowPayout->setVisible(false);
        if (rowPose) rowPose->setVisible(false);
        if (rowHeights) rowHeights->setVisible(false);
        refreshScrollSize();
        return;
    }

    if (!meta.proTxHash.isEmpty()) {
        ui->labelId->setText(tr("ProTx Hash:"));
        ui->textId->setText(elideCenterHex(meta.proTxHash));
        ui->textId->setToolTip(meta.proTxHash);
        ui->pushCopy->setToolTip(tr("Copy full ProTx Hash"));
    }

    const QLocale loc;
    if (rowOperatorBls && valOperatorBls) {
        valOperatorBls->setText(meta.operatorBlsPubKey.isEmpty() ? QStringLiteral("—") : elideCenterHex(meta.operatorBlsPubKey, 18));
        valOperatorBls->setToolTip(meta.operatorBlsPubKey);
        rowOperatorBls->setVisible(!meta.operatorBlsPubKey.isEmpty());
    }
    if (rowPayout && valPayout) {
        valPayout->setText(meta.payoutAddress.isEmpty() ? QStringLiteral("—") : meta.payoutAddress);
        valPayout->setToolTip(meta.payoutAddress);
        rowPayout->setVisible(!meta.payoutAddress.isEmpty());
    }
    if (rowPose && valPose) {
        QString poseText = loc.toString(meta.posePenalty);
        if (meta.poseBanHeight >= 0) {
            poseText += tr(" (Banned at #%1)").arg(loc.toString(meta.poseBanHeight));
        } else if (meta.posePenalty == 0) {
            poseText += tr(" (Healthy)");
        }
        valPose->setText(poseText);
        rowPose->setVisible(true);
    }
    if (rowHeights && valHeights) {
        const QString paidStr = (meta.lastPaidHeight > 0) ? QString("#%1").arg(loc.toString(meta.lastPaidHeight)) : tr("Never paid");
        const QString regStr = (meta.registeredHeight >= 0) ? QString("#%1").arg(loc.toString(meta.registeredHeight)) : QStringLiteral("—");
        valHeights->setText(tr("Paid: %1 • Reg: %2").arg(paidStr, regStr));
        rowHeights->setVisible(true);
    }
    refreshScrollSize();
}

void MnInfoDialog::copyInform(const QString& copyStr, const QString& message)
{
    GUIUtil::setClipboard(copyStr);
    if (!snackBar) snackBar = new SnackBar(nullptr, this);
    snackBar->setText(message);
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
