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
#include <utility>
#include <vector>

static QString elideCenterHex(const QString& val, int edge = 14)
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
    setMinimumSize(660, 0);
    setMaximumWidth(720);
    setCssProperty(ui->frame, "container-dialog");
    setCssProperty(ui->labelTitle, "text-title-dialog");
    setCssTextBodyDialog({ui->labelAmount, ui->labelSend, ui->labelInputs, ui->labelFee, ui->labelId});
    setCssProperty({ui->labelDivider1, ui->labelDivider4, ui->labelDivider6, ui->labelDivider7, ui->labelDivider8, ui->labelDivider9}, "container-divider");
    setCssTextBodyDialog({ui->textAmount, ui->textAddress, ui->textInputs, ui->textStatus, ui->textId, ui->textExport});
    setCssProperty({ui->pushCopy, ui->pushCopyId, ui->pushExport}, "ic-copy-big");
    setCssProperty(ui->btnEsc, "ic-close");
    ui->labelId->setText(tr("Owner Key Hash:"));
    ui->labelSend->setText(tr("Address:"));
    ui->pushCopy->setFixedSize(30, 30);
    ui->pushCopyId->setFixedSize(30, 30);
    ui->pushExport->setFixedSize(30, 30);
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
    ui->textExport->setMinimumWidth(0);
    ui->textExport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->textExport->setText(tr("Export Remote Operator entry (activemasternode.conf)"));
    ui->textExport->setToolTip(tr("Copy <alias> <privkey> <bls_privkey_hex> for activemasternode.conf"));

    for (QLabel* keyLbl : {ui->labelId, ui->labelSend, ui->labelAmount, ui->labelInputs, ui->labelFee}) {
        keyLbl->setFixedWidth(176);
        keyLbl->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }
    for (QLabel* valLbl : {ui->textId, ui->textAddress, ui->textAmount, ui->textInputs, ui->textStatus}) {
        valLbl->setMinimumWidth(0);
        valLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        valLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }

    const QString dividerStyle = QStringLiteral("background-color: %1; border: none;")
        .arg(isLightTheme() ? QStringLiteral("#E2E8F0") : QStringLiteral("#243452"));
    for (QLabel* div : {ui->labelDivider1, ui->labelDivider4, ui->labelDivider6, ui->labelDivider7, ui->labelDivider8, ui->labelDivider9}) {
        div->setFixedHeight(1);
        div->setStyleSheet(dividerStyle);
    }
    auto makeDivider = [this, &dividerStyle]() {
        QLabel* div = new QLabel(ui->content);
        div->setFixedHeight(1);
        div->setStyleSheet(dividerStyle);
        return div;
    };

    if (ui->frame && ui->frame->layout()) {
        ui->frame->layout()->setContentsMargins(24, 16, 24, 18);
        ui->frame->layout()->setSpacing(6);
    }
    if (ui->horizontalLayout_3) {
        ui->horizontalLayout_3->setContentsMargins(0, 0, 0, 4);
    }
    if (ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(20, 2, QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (ui->verticalLayout_4) {
        ui->verticalLayout_4->setContentsMargins(0, 0, 0, 0);
        ui->verticalLayout_4->setSpacing(0);
    }
    if (ui->scrollArea) {
        ui->scrollArea->setFrameShape(QFrame::NoFrame);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        ui->scrollArea->setWidgetResizable(true);
    }
    if (ui->verticalLayout_7) {
        ui->verticalLayout_7->setContentsMargins(0, 0, 0, 0);
    }
    for (QHBoxLayout* rowLayout : {ui->horizontalLayout_12, ui->horizontalLayout_11, ui->horizontalLayout_4, ui->horizontalLayout, ui->horizontalLayout_7, ui->horizontalLayout_8}) {
        if (rowLayout) {
            rowLayout->setContentsMargins(4, 6, 4, 6);
            rowLayout->setSpacing(10);
        }
    }
    if (ui->content && ui->content->layout()) {
        ui->content->layout()->setContentsMargins(0, 0, 0, 0);
        ui->content->layout()->setSpacing(0);

        auto addMetaRow = [this, &makeDivider](const QString& title, QWidget*& rowOut, QLabel*& valOut, QLabel*& divOut, QPushButton** copyBtnOut = nullptr) {
            rowOut = new QWidget(ui->content);
            QHBoxLayout* h = new QHBoxLayout(rowOut);
            h->setContentsMargins(4, 6, 4, 6);
            h->setSpacing(10);
            QLabel* lblKey = new QLabel(title, rowOut);
            lblKey->setFixedWidth(176);
            lblKey->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
            valOut = new QLabel(QStringLiteral("—"), rowOut);
            valOut->setMinimumWidth(0);
            valOut->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            valOut->setTextInteractionFlags(Qt::TextSelectableByMouse);
            setCssTextBodyDialog({lblKey, valOut});
            h->addWidget(lblKey, 0);
            h->addWidget(valOut, 1);
            if (copyBtnOut) {
                QPushButton* btn = new QPushButton(rowOut);
                btn->setFixedSize(30, 30);
                btn->setFocusPolicy(Qt::TabFocus);
                btn->setCursor(Qt::PointingHandCursor);
                setCssProperty(btn, "ic-copy-big");
                h->addWidget(btn, 0);
                *copyBtnOut = btn;
            }
            ui->content->layout()->addWidget(rowOut);
            rowOut->setVisible(false);
            divOut = makeDivider();
            ui->content->layout()->addWidget(divOut);
            divOut->setVisible(false);
        };

        QPushButton* btnCopyBls = nullptr;
        QPushButton* btnCopyPayout = nullptr;
        addMetaRow(tr("Operator BLS Key:"), rowOperatorBls, valOperatorBls, divOperatorBls, &btnCopyBls);
        addMetaRow(tr("Payout Address:"), rowPayout, valPayout, divPayout, &btnCopyPayout);
        addMetaRow(tr("PoSe Penalty:"), rowPose, valPose, divPose);
        addMetaRow(tr("Last Paid / Registered:"), rowHeights, valHeights, divHeights);
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

    // Add 1-click Collateral Outpoint reference copy row
    if (ui->content && ui->content->layout()) {
        rowCtrl = new QWidget(ui->content);
        QHBoxLayout* layoutCtrl = new QHBoxLayout(rowCtrl);
        layoutCtrl->setContentsMargins(4, 6, 4, 6);
        layoutCtrl->setSpacing(10);
        QLabel* lblCtrl = new QLabel(tr("Collateral Outpoint (txid:vout)"), rowCtrl);
        lblCtrl->setToolTip(tr("Copy collateral txid:vout reference"));
        lblCtrl->setWordWrap(true);
        lblCtrl->setMinimumWidth(0);
        lblCtrl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        setCssTextBodyDialog(lblCtrl);
        QPushButton* btnCopyCtrl = new QPushButton(rowCtrl);
        btnCopyCtrl->setFixedSize(30, 30);
        btnCopyCtrl->setFocusPolicy(Qt::TabFocus);
        btnCopyCtrl->setCursor(Qt::PointingHandCursor);
        btnCopyCtrl->setToolTip(tr("Copy collateral outpoint"));
        setCssProperty(btnCopyCtrl, "ic-copy-big");
        layoutCtrl->addWidget(lblCtrl, 1);
        layoutCtrl->addWidget(btnCopyCtrl, 0);
        ui->content->layout()->addWidget(rowCtrl);
        connect(btnCopyCtrl, &QPushButton::clicked, [this]() {
            if (!collateralOutpointStr.isEmpty()) {
                copyInform(collateralOutpointStr, tr("Collateral outpoint copied"));
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

void MnInfoDialog::updateDividers()
{
    const std::vector<std::pair<QWidget*, QLabel*>> rows = {
        {ui->contentID, ui->labelDivider9},
        {ui->contentOutputs, ui->labelDivider8},
        {ui->contentAmount, ui->labelDivider6},
        {ui->contentInputs, ui->labelDivider1},
        {ui->contentFee, ui->labelDivider4},
        {ui->contentExport, ui->labelDivider7},
        {rowOperatorBls, divOperatorBls},
        {rowPayout, divPayout},
        {rowPose, divPose},
        {rowHeights, divHeights},
        {rowCtrl, nullptr},
    };
    bool hasVisibleBelow = false;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        QWidget* row = it->first;
        QLabel* divAfter = it->second;
        const bool rowActive = (row && !row->isHidden());
        if (divAfter) {
            divAfter->setVisible(rowActive && hasVisibleBelow);
        }
        if (rowActive) {
            hasVisibleBelow = true;
        }
    }
}

void MnInfoDialog::refreshScrollSize()
{
    const int dialogW = parentWidget()
        ? std::min(680, std::max(540, parentWidget()->width() - 48))
        : 680;
    setFixedWidth(dialogW);

    if (ui->content && ui->scrollArea) {
        if (ui->content->layout()) ui->content->layout()->activate();
        ui->content->adjustSize();
        const int contentH = ui->content->sizeHint().height();
        const int maxDialogH = parentWidget()
            ? std::max(440, parentWidget()->height() - 48)
            : 760;
        const int maxScrollH = std::max(320, maxDialogH - 96);
        if (contentH <= maxScrollH) {
            ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            ui->scrollArea->setFixedHeight(contentH + 2);
        } else {
            ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            ui->scrollArea->setFixedHeight(maxScrollH);
        }
    }
    if (ui->frame && ui->frame->layout()) {
        ui->frame->layout()->activate();
    }
    adjustSize();
}

void MnInfoDialog::setData(QString pubKey, QString name, QString address, QString txId, QString outputIndex, QString status, bool hasLocalOperatorKey)
{
    this->pubKey = pubKey;
    this->txId = txId;
    ui->labelTitle->setText(name.isEmpty() ? tr("Masternode Information") : tr("Masternode Information — %1").arg(name));
    if (!txId.isEmpty() && !outputIndex.isEmpty()) {
        this->collateralOutpointStr = QString("%1:%2").arg(txId, outputIndex);
    } else {
        this->collateralOutpointStr.clear();
    }
    if (ui->contentExport) ui->contentExport->setVisible(hasLocalOperatorKey);
    if (rowCtrl) rowCtrl->setVisible(!collateralOutpointStr.isEmpty());

    ui->labelId->setText(tr("Owner Key Hash:"));
    ui->textId->setText(elideCenterHex(pubKey, 14));
    ui->textId->setToolTip(pubKey);
    ui->textAddress->setText(address);
    ui->textAmount->setText(elideCenterHex(txId, 14));
    ui->textAmount->setToolTip(txId);
    ui->textInputs->setText(outputIndex);
    ui->textStatus->setText(status);

    const bool light = isLightTheme();
    QString statusColor = light ? "#475569" : "#CBD5E1";
    if (status == "ENABLED") statusColor = light ? "#3E5B94" : "#A8C1EE";
    else if (status == "PRE_ENABLED") statusColor = light ? "#2563EB" : "#60A5FA";
    else if (status == "EXPIRED" || status == "NEW_START_REQUIRED") statusColor = light ? "#B45309" : "#FBBF24";
    else if (status == "MISSING" || status == "POSE_BAN") statusColor = light ? "#DC2626" : "#F87171";
    ui->textStatus->setStyleSheet(QString("color: %1; font-weight: 700;").arg(statusColor));

    updateDividers();
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
        updateDividers();
        refreshScrollSize();
        return;
    }

    if (!meta.proTxHash.isEmpty()) {
        ui->labelId->setText(tr("ProTx Hash:"));
        ui->textId->setText(elideCenterHex(meta.proTxHash, 14));
        ui->textId->setToolTip(meta.proTxHash);
        ui->pushCopy->setToolTip(tr("Copy full ProTx Hash"));
    }

    const QLocale loc;
    if (rowOperatorBls && valOperatorBls) {
        valOperatorBls->setText(meta.operatorBlsPubKey.isEmpty() ? QStringLiteral("—") : elideCenterHex(meta.operatorBlsPubKey, 14));
        valOperatorBls->setToolTip(meta.operatorBlsPubKey);
        rowOperatorBls->setVisible(!meta.operatorBlsPubKey.isEmpty());
    }
    if (rowPayout && valPayout) {
        const QString displayPayout = (meta.payoutAddress.length() <= 39)
            ? meta.payoutAddress
            : elideCenterHex(meta.payoutAddress, 16);
        valPayout->setText(meta.payoutAddress.isEmpty() ? QStringLiteral("—") : displayPayout);
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
    updateDividers();
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
