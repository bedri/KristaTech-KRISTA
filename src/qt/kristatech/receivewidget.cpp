// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/receivewidget.h"

#include "base58.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "pairresult.h"
#include "qt/kristatech/addnewcontactdialog.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/requestdialog.h"
#include "walletmodel.h"

#include <QColor>
#include <QDateTime>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QTableWidgetItem>
#include <QVBoxLayout>

ReceiveWidget::ReceiveWidget(KRISTATECHGUI* parent) :
    PWidget(parent)
{
    if (parent) {
        this->setStyleSheet(parent->styleSheet());
    }

    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(22, 18, 22, 18);
    rootLayout->setSpacing(18);

    // =========================================================================
    // LEFT COLUMN: Header, Active Address + QR Card, Live Request Builder Card
    // =========================================================================
    QWidget* leftCol = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    lblTitle = new QLabel(tr("Receive KRISTA"), leftCol);
    lblSubtitle = new QLabel(tr("Share your address or QR code to receive KRISTA. Optional amount and label update the QR code live."), leftCol);
    lblSubtitle->setWordWrap(true);
    leftLayout->addWidget(lblTitle);
    leftLayout->addWidget(lblSubtitle);

    // Card 1: QR & Active Address
    qrCard = new QFrame(leftCol);
    qrCard->setObjectName("receiveQrCard");
    qrCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* qrCardLayout = new QVBoxLayout(qrCard);
    qrCardLayout->setContentsMargins(16, 14, 16, 14);
    qrCardLayout->setSpacing(10);

    QHBoxLayout* metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(8);
    lblActiveLabel = new QLabel(tr("Receive Address"), qrCard);
    lblUsageBadge = new QLabel(tr("Unused"), qrCard);
    lblUsageBadge->setVisible(false);
    lblDate = new QLabel(QString(), qrCard);
    lblDate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metaRow->addWidget(lblActiveLabel);
    metaRow->addWidget(lblUsageBadge);
    metaRow->addStretch();
    metaRow->addWidget(lblDate);
    qrCardLayout->addLayout(metaRow);

    lblQrImg = new QLabel(qrCard);
    lblQrImg->setFixedSize(212, 212);
    lblQrImg->setAlignment(Qt::AlignCenter);
    qrCardLayout->addWidget(lblQrImg, 0, Qt::AlignHCenter);

    lblAddress = new QLabel(tr("Loading..."), qrCard);
    lblAddress->setAlignment(Qt::AlignCenter);
    lblAddress->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lblAddress->setWordWrap(true);
    qrCardLayout->addWidget(lblAddress);

    lblLiveUriHint = new QLabel(qrCard);
    lblLiveUriHint->setAlignment(Qt::AlignCenter);
    lblLiveUriHint->setWordWrap(true);
    lblLiveUriHint->setVisible(false);
    qrCardLayout->addWidget(lblLiveUriHint);

    QHBoxLayout* qrActions = new QHBoxLayout();
    qrActions->setContentsMargins(0, 2, 0, 0);
    qrActions->setSpacing(8);

    btnCopyAddress = new QPushButton(tr("Copy Address"), qrCard);
    btnCopyAddress->setCursor(Qt::PointingHandCursor);
    btnCopyAddress->setFocusPolicy(Qt::NoFocus);
    btnCopyAddress->setMinimumHeight(36);

    btnCopyUri = new QPushButton(tr("Copy URI"), qrCard);
    btnCopyUri->setCursor(Qt::PointingHandCursor);
    btnCopyUri->setFocusPolicy(Qt::NoFocus);
    btnCopyUri->setMinimumHeight(36);
    btnCopyUri->setToolTip(tr("Copy kristatech: payment URI including label, amount, and message"));

    btnSaveQr = new QPushButton(tr("Save QR"), qrCard);
    btnSaveQr->setCursor(Qt::PointingHandCursor);
    btnSaveQr->setFocusPolicy(Qt::NoFocus);
    btnSaveQr->setMinimumHeight(36);
    btnSaveQr->setToolTip(tr("Export QR code as a high-contrast PNG image"));

    btnNewAddress = new QPushButton(QStringLiteral("+ ") + tr("New Address"), qrCard);
    btnNewAddress->setCursor(Qt::PointingHandCursor);
    btnNewAddress->setFocusPolicy(Qt::NoFocus);
    btnNewAddress->setMinimumHeight(36);
    btnNewAddress->setToolTip(tr("Generate a fresh receive address"));

    qrActions->addWidget(btnCopyAddress, 1);
    qrActions->addWidget(btnCopyUri, 1);
    qrActions->addWidget(btnSaveQr);
    qrActions->addWidget(btnNewAddress, 1);
    qrCardLayout->addLayout(qrActions);

    leftLayout->addWidget(qrCard);

    // Card 2: Live Payment Request Builder
    requestCard = new QFrame(leftCol);
    requestCard->setObjectName("receiveRequestCard");
    requestCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* reqLayout = new QVBoxLayout(requestCard);
    reqLayout->setContentsMargins(16, 12, 16, 14);
    reqLayout->setSpacing(8);

    QHBoxLayout* reqHeader = new QHBoxLayout();
    reqHeader->setContentsMargins(0, 0, 0, 0);
    reqHeader->setSpacing(8);
    lblReqTitle = new QLabel(tr("Payment Request Details (Live QR)"), requestCard);
    btnClearRequest = new QPushButton(tr("Clear"), requestCard);
    btnClearRequest->setCursor(Qt::PointingHandCursor);
    btnClearRequest->setFocusPolicy(Qt::NoFocus);
    btnClearRequest->setFixedHeight(26);
    reqHeader->addWidget(lblReqTitle);
    reqHeader->addStretch();
    reqHeader->addWidget(btnClearRequest);
    reqLayout->addLayout(reqHeader);

    QGridLayout* reqGrid = new QGridLayout();
    reqGrid->setContentsMargins(0, 0, 0, 0);
    reqGrid->setHorizontalSpacing(10);
    reqGrid->setVerticalSpacing(6);

    lblReqLabelKey = new QLabel(tr("Address Label"), requestCard);
    lblReqAmountKey = new QLabel(tr("Requested Amount (optional)"), requestCard);
    lblReqMessageKey = new QLabel(tr("Memo / Message (optional)"), requestCard);

    editLabel = new QLineEdit(requestCard);
    editLabel->setMinimumHeight(36);
    editLabel->setMaximumHeight(36);
    editLabel->setClearButtonEnabled(true);
    editLabel->setPlaceholderText(tr("Label for this receive address..."));

    btnSaveLabel = new QPushButton(tr("Save Label"), requestCard);
    btnSaveLabel->setCursor(Qt::PointingHandCursor);
    btnSaveLabel->setFocusPolicy(Qt::NoFocus);
    btnSaveLabel->setFixedHeight(36);
    btnSaveLabel->setMinimumWidth(96);

    QHBoxLayout* labelRow = new QHBoxLayout();
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(8);
    labelRow->addWidget(editLabel, 1);
    labelRow->addWidget(btnSaveLabel);

    editAmount = new QLineEdit(requestCard);
    editAmount->setMinimumHeight(36);
    editAmount->setMaximumHeight(36);
    editAmount->setMaxLength(17);
    editAmount->setPlaceholderText(QStringLiteral("0.00 KRISTA"));
    GUIUtil::setupAmountWidget(editAmount, this);

    editMessage = new QLineEdit(requestCard);
    editMessage->setMinimumHeight(36);
    editMessage->setMaximumHeight(36);
    editMessage->setClearButtonEnabled(true);
    editMessage->setPlaceholderText(tr("Invoice or note encoded in URI..."));

    reqGrid->addWidget(lblReqLabelKey, 0, 0, 1, 2);
    reqGrid->addLayout(labelRow, 1, 0, 1, 2);
    reqGrid->addWidget(lblReqAmountKey, 2, 0);
    reqGrid->addWidget(lblReqMessageKey, 2, 1);
    reqGrid->addWidget(editAmount, 3, 0);
    reqGrid->addWidget(editMessage, 3, 1);
    reqLayout->addLayout(reqGrid);

    leftLayout->addWidget(requestCard);
    leftLayout->addStretch(1);

    // =========================================================================
    // RIGHT COLUMN: Searchable My Receive Addresses Card
    // =========================================================================
    listCard = new QFrame(this);
    listCard->setObjectName("receiveListCard");
    listCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(16, 14, 16, 14);
    listLayout->setSpacing(10);

    QHBoxLayout* listHeader = new QHBoxLayout();
    listHeader->setContentsMargins(0, 0, 0, 0);
    listHeader->setSpacing(8);
    lblListTitle = new QLabel(tr("My Receive Addresses"), listCard);
    lblAddrCount = new QLabel(tr("0 addresses"), listCard);
    btnQuickNew = new QPushButton(QStringLiteral("+ ") + tr("New"), listCard);
    btnQuickNew->setCursor(Qt::PointingHandCursor);
    btnQuickNew->setFocusPolicy(Qt::NoFocus);
    btnQuickNew->setFixedHeight(30);
    listHeader->addWidget(lblListTitle);
    listHeader->addWidget(lblAddrCount);
    listHeader->addStretch();
    listHeader->addWidget(btnQuickNew);
    listLayout->addLayout(listHeader);

    QHBoxLayout* listFilterRow = new QHBoxLayout();
    listFilterRow->setContentsMargins(0, 0, 0, 0);
    listFilterRow->setSpacing(8);

    lineEditSearch = new QLineEdit(listCard);
    lineEditSearch->setMinimumHeight(34);
    lineEditSearch->setMaximumHeight(34);
    lineEditSearch->setClearButtonEnabled(true);
    lineEditSearch->setPlaceholderText(tr("Search by label or address..."));

    comboBoxSort = new QComboBox(listCard);
    comboBoxSort->setMinimumHeight(34);
    comboBoxSort->setMinimumWidth(100);
    comboBoxSortOrder = new QComboBox(listCard);
    comboBoxSortOrder->setMinimumHeight(34);
    comboBoxSortOrder->setMinimumWidth(78);
    fillAddressSortControls(comboBoxSort, comboBoxSortOrder);

    listFilterRow->addWidget(lineEditSearch, 1);
    listFilterRow->addWidget(comboBoxSort);
    listFilterRow->addWidget(comboBoxSortOrder);
    listLayout->addLayout(listFilterRow);

    tableAddresses = new QTableWidget(0, 3, listCard);
    tableAddresses->setObjectName("tableReceiveAddresses");
    tableAddresses->setHorizontalHeaderLabels({tr("Label"), tr("Address"), tr("Actions")});
    tableAddresses->verticalHeader()->setVisible(false);
    tableAddresses->setSelectionMode(QAbstractItemView::SingleSelection);
    tableAddresses->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableAddresses->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableAddresses->setShowGrid(false);
    tableAddresses->setFocusPolicy(Qt::NoFocus);
    tableAddresses->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableAddresses->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableAddresses->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableAddresses->setColumnWidth(2, 126);

    lblEmptyList = new QLabel(tr("No matching receive addresses found."), listCard);
    lblEmptyList->setAlignment(Qt::AlignCenter);
    lblEmptyList->setVisible(false);

    listLayout->addWidget(tableAddresses, 1);
    listLayout->addWidget(lblEmptyList);

    rootLayout->addWidget(leftCol, 11);
    rootLayout->addWidget(listCard, 10);

    connect(btnCopyAddress, &QPushButton::clicked, this, &ReceiveWidget::onCopyClicked);
    connect(btnCopyUri, &QPushButton::clicked, this, &ReceiveWidget::onCopyUriClicked);
    connect(btnSaveQr, &QPushButton::clicked, this, &ReceiveWidget::onSaveQrClicked);
    connect(btnNewAddress, &QPushButton::clicked, this, &ReceiveWidget::onNewAddressClicked);
    connect(btnQuickNew, &QPushButton::clicked, this, &ReceiveWidget::onNewAddressClicked);
    connect(btnSaveLabel, &QPushButton::clicked, this, &ReceiveWidget::onSaveInlineLabelClicked);
    connect(btnClearRequest, &QPushButton::clicked, this, &ReceiveWidget::onClearRequestClicked);

    connect(editLabel, &QLineEdit::textChanged, this, [this](const QString&) {
        if (!updatingRequestFields) updateQrFromFields();
    });
    connect(editAmount, &QLineEdit::textChanged, this, [this](const QString&) {
        if (!updatingRequestFields) updateQrFromFields();
    });
    connect(editMessage, &QLineEdit::textChanged, this, [this](const QString&) {
        if (!updatingRequestFields) updateQrFromFields();
    });

    connect(lineEditSearch, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (filter) {
            filter->setFilterFixedString(text.trimmed());
            rebuildAddressTable();
        }
    });
    connect(comboBoxSort, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ReceiveWidget::onSortChanged);
    connect(comboBoxSortOrder, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ReceiveWidget::onSortOrderChanged);

    connect(tableAddresses, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (!tableAddresses || row < 0 || row >= tableAddresses->rowCount()) return;
        QTableWidgetItem* item = tableAddresses->item(row, 1);
        if (!item) return;
        const QString fullAddr = item->data(Qt::UserRole).toString();
        if (!fullAddr.isEmpty()) refreshView(fullAddr);
    });
    connect(tableAddresses, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!tableAddresses || row < 0 || row >= tableAddresses->rowCount()) return;
        QTableWidgetItem* item = tableAddresses->item(row, 1);
        if (!item) return;
        const QString fullAddr = item->data(Qt::UserRole).toString();
        if (!fullAddr.isEmpty()) {
            GUIUtil::setClipboard(fullAddr);
            inform(tr("Address copied"));
        }
    });

    applyTheme(isLightTheme());
}

void ReceiveWidget::applyTheme(bool isLight)
{
    const QString cardBg     = isLight ? "#FFFFFF" : "#131B2E";
    const QString cardBrd    = isLight ? "#CBD5E1" : "#253554";
    const QString titleCol   = isLight ? "#0F172A" : "#F8FAFC";
    const QString subCol     = isLight ? "#475569" : "#CBD5E1";
    const QString inputBg    = isLight ? "#F8FAFC" : "#0D1424";
    const QString inputBrd   = isLight ? "#CBD5E1" : "#2D4066";
    const QString addrBg     = isLight ? "#F1F5F9" : "#0D1527";
    const QString addrBrd    = isLight ? "#CBD5E1" : "#2A3E63";
    const QString secBg      = isLight ? "#F1F5F9" : "#1C2942";
    const QString secFg      = isLight ? "#1E293B" : "#E2E8F0";
    const QString secBrd     = isLight ? "#CBD5E1" : "#33486E";
    const QString secHov     = isLight ? "#E2E8F0" : "#26385A";
    const QString rowHover   = isLight ? "#F8FAFC" : "#1A2540";
    const QString rowSel     = isLight ? "#EFF6FF" : "#1E3154";
    const QString altRowBg   = isLight ? "#F8FAFC" : "#172238";
    const QString divBrd     = isLight ? "#E2E8F0" : "#243452";

    if (lblTitle)    lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 22px; font-weight: 700; background: transparent; }").arg(titleCol));
    if (lblSubtitle) lblSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(subCol));

    const QString cardStyle = QString("QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }");
    if (qrCard)      qrCard->setStyleSheet(cardStyle.arg("receiveQrCard", cardBg, cardBrd));
    if (requestCard) requestCard->setStyleSheet(cardStyle.arg("receiveRequestCard", cardBg, cardBrd));
    if (listCard)    listCard->setStyleSheet(cardStyle.arg("receiveListCard", cardBg, cardBrd));

    if (lblActiveLabel) lblActiveLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: 700; background: transparent; }").arg(titleCol));
    if (lblDate)        lblDate->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 500; background: transparent; }").arg(subCol));
    if (lblLiveUriHint) lblLiveUriHint->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; background: transparent; }").arg(subCol));

    if (lblQrImg) {
        lblQrImg->setStyleSheet(QString("QLabel { background-color: #FFFFFF; color: #334155; border: 1.5px solid %1; border-radius: 10px; padding: 6px; }")
                                    .arg(isLight ? "#7695CD" : "#93B4F0"));
    }
    if (lblAddress) {
        lblAddress->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 8px 12px; font-family: monospace; font-size: 13px; font-weight: 600; }"
        ).arg(addrBg, titleCol, addrBrd));
    }

    const QString secTitleStyle = QString("QLabel { color: %1; font-size: 14px; font-weight: 700; background: transparent; }").arg(titleCol);
    if (lblReqTitle)  lblReqTitle->setStyleSheet(secTitleStyle);
    if (lblListTitle) lblListTitle->setStyleSheet(secTitleStyle);

    const QString keyStyle = QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }").arg(subCol);
    if (lblReqLabelKey)   lblReqLabelKey->setStyleSheet(keyStyle);
    if (lblReqAmountKey)  lblReqAmountKey->setStyleSheet(keyStyle);
    if (lblReqMessageKey) lblReqMessageKey->setStyleSheet(keyStyle);
    if (lblEmptyList)     lblEmptyList->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; padding: 24px; background: transparent; }").arg(subCol));

    if (lblAddrCount) {
        lblAddrCount->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(secBg, subCol, secBrd));
    }

    if (btnCopyAddress) {
        btnCopyAddress->setStyleSheet(
            "QPushButton { background-color: #5576B2; color: #FFFFFF; border: 1.5px solid #7695CD; border-radius: 7px; padding: 0 14px; font-size: 12px; font-weight: 700; }"
            "QPushButton:hover { background-color: #466399; border-color: #F5897C; }"
            "QPushButton:pressed { background-color: #3A5484; }"
        );
    }

    const QString secBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
    ).arg(secBg, secFg, secBrd, secHov);
    if (btnCopyUri)      btnCopyUri->setStyleSheet(secBtnStyle);
    if (btnSaveQr)       btnSaveQr->setStyleSheet(secBtnStyle);
    if (btnNewAddress)   btnNewAddress->setStyleSheet(secBtnStyle);
    if (btnSaveLabel)    btnSaveLabel->setStyleSheet(secBtnStyle);
    if (btnClearRequest) btnClearRequest->setStyleSheet(secBtnStyle);
    if (btnQuickNew)     btnQuickNew->setStyleSheet(secBtnStyle);

    const QString editStyle = QString(
        "QLineEdit { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #7695CD; }"
    ).arg(inputBg, titleCol, inputBrd);
    if (editLabel)      editLabel->setStyleSheet(editStyle);
    if (editAmount)     editAmount->setStyleSheet(editStyle);
    if (editMessage)    editMessage->setStyleSheet(editStyle);
    if (lineEditSearch) lineEditSearch->setStyleSheet(editStyle);

    styleLineEditClearButton(editLabel);
    styleLineEditClearButton(editMessage);
    styleLineEditClearButton(lineEditSearch);

    const QString comboStyle = QString(
        "QComboBox { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 4px 10px; font-size: 12px; font-weight: 600; }"
        "QComboBox:hover { border-color: #7695CD; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: %4; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; }"
    ).arg(inputBg, titleCol, inputBrd, cardBg);
    if (comboBoxSort)      comboBoxSort->setStyleSheet(comboStyle);
    if (comboBoxSortOrder) comboBoxSortOrder->setStyleSheet(comboStyle);

    if (tableAddresses) {
        tableAddresses->setStyleSheet(
            QString(
                "QTableWidget#tableReceiveAddresses { background-color: %1; border: none; color: %2; font-size: 12px; selection-background-color: %3; selection-color: %2; }"
                "QTableWidget#tableReceiveAddresses::item { padding: 6px 8px; border-bottom: 1px solid %4; }"
                "QTableWidget#tableReceiveAddresses::item:hover { background-color: %5; }"
                "QTableWidget#tableReceiveAddresses::item:selected { background-color: %3; color: %2; }"
                "QHeaderView::section { background-color: %6; color: %7; border: none; border-bottom: 1px solid %4; padding: 7px 8px; font-size: 11px; font-weight: 700; text-align: left; }"
                "QPushButton#btnRecvRowCopy, QPushButton#btnRecvRowEdit { background-color: %8; color: %9; border: 1px solid %10; border-radius: 5px; padding: 3px 8px; font-size: 11px; font-weight: 600; }"
                "QPushButton#btnRecvRowCopy:hover, QPushButton#btnRecvRowEdit:hover { border-color: #7695CD; background-color: %11; }"
            ).arg(cardBg, titleCol, rowSel, divBrd, rowHover, altRowBg, subCol, secBg, secFg, secBrd, secHov)
        );
    }

    updateActiveAddressMeta();
    rebuildAddressTable();
}

void ReceiveWidget::loadWalletModel()
{
    if (!walletModel) return;

    this->addressTableModel = walletModel->getAddressTableModel();
    this->filter = new AddressFilterProxyModel(AddressTableModel::Receive, this);
    this->filter->setSourceModel(addressTableModel);
    this->filter->sort(sortType, sortOrder);

    if (!info) info = new SendCoinsRecipient();

    updateAmountPlaceholder();
    if (walletModel->getOptionsModel()) {
        connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, [this](int) {
            updateAmountPlaceholder();
            updateQrFromFields();
        }, Qt::UniqueConnection);
    }

    connect(this->addressTableModel, &AddressTableModel::dataChanged, this,
            [this](const QModelIndex& tl, const QModelIndex& br) {
                refreshView(tl, br);
            });
    connect(this->filter, &QAbstractItemModel::rowsInserted, this, [this]() { rebuildAddressTable(); });
    connect(this->filter, &QAbstractItemModel::rowsRemoved,  this, [this]() { rebuildAddressTable(); });
    connect(this->filter, &QAbstractItemModel::modelReset,   this, [this]() { rebuildAddressTable(); });
    connect(this->filter, &QAbstractItemModel::layoutChanged,this, [this]() { rebuildAddressTable(); });
    connect(walletModel, &WalletModel::balanceChanged, this,
            [this](const interfaces::WalletBalances&) { updateActiveAddressMeta(); }, Qt::UniqueConnection);

    refreshView();
    rebuildAddressTable();
}

void ReceiveWidget::updateAmountPlaceholder()
{
    if (!editAmount) return;
    int unit = BitcoinUnits::PIV;
    if (walletModel && walletModel->getOptionsModel()) {
        unit = walletModel->getOptionsModel()->getDisplayUnit();
    }
    editAmount->setPlaceholderText(QStringLiteral("0.00 ") + BitcoinUnits::name(unit));
}

void ReceiveWidget::refreshView(const QModelIndex& tl, const QModelIndex&)
{
    if (!tl.isValid() || !addressTableModel) return;
    const QString rowType = tl.sibling(tl.row(), AddressTableModel::Label).data(AddressTableModel::TypeRole).toString();
    if (rowType != AddressTableModel::Receive) return;

    const QString changedAddr = tl.sibling(tl.row(), AddressTableModel::Address).data(Qt::DisplayRole).toString();
    rebuildAddressTable();
    if (currentAddress.isEmpty() || changedAddr == currentAddress) {
        refreshView(changedAddr);
    }
}

void ReceiveWidget::refreshView(QString refreshAddress)
{
    if (!walletModel || !addressTableModel) return;
    try {
        QString latestAddress = refreshAddress.isEmpty() ? this->addressTableModel->getAddressToShow() : refreshAddress;
        if (latestAddress.isEmpty()) {
            Destination newAddress;
            PairResult r = walletModel->getNewAddress(newAddress, "Default");
            if (!r.result) {
                lblQrImg->setText(tr("No available address, try unlocking the wallet"));
                inform(tr("Error generating address"));
                return;
            }
            latestAddress = QString::fromStdString(newAddress.ToString());
        }

        currentAddress = latestAddress;
        lblAddress->setText(latestAddress);
        int64_t time = walletModel->getKeyCreationTime(DecodeDestination(latestAddress.toStdString()));
        lblDate->setText(GUIUtil::dateTimeStr(QDateTime::fromTime_t(static_cast<uint>(time))));

        updatingRequestFields = true;
        const QString storedLabel = addressTableModel->labelForAddress(latestAddress);
        if (editLabel) editLabel->setText(storedLabel);
        updatingRequestFields = false;

        updateLabel();
        updateActiveAddressMeta();
        updateQrFromFields();
        rebuildAddressTable();
    } catch (const std::runtime_error&) {
        lblQrImg->setText(tr("No available address, try unlocking the wallet"));
        inform(tr("Error generating address"));
    }
}

void ReceiveWidget::updateLabel()
{
    if (!info || info->address.isEmpty() || !addressTableModel) return;
    QString label = addressTableModel->labelForAddress(info->address);
    if (lblActiveLabel) {
        lblActiveLabel->setText(label.isEmpty() ? tr("Unlabeled Receive Address") : label);
    }
}

void ReceiveWidget::updateActiveAddressMeta()
{
    if (!lblUsageBadge || !walletModel || currentAddress.isEmpty()) {
        if (lblUsageBadge) lblUsageBadge->setVisible(false);
        return;
    }

    const bool light = isLightTheme();
    const bool used = walletModel->isUsed(DecodeDestination(currentAddress.toStdString()));
    const QString text = used ? tr("Used") : tr("Unused");
    const QString fg   = used ? (light ? "#B45309" : "#FBBF24") : (light ? "#047857" : "#4ADE80");
    const QString bg   = used ? "rgba(245, 158, 11, 0.14)" : "rgba(16, 185, 129, 0.14)";
    const QString brd  = used ? "rgba(245, 158, 11, 0.42)" : "rgba(16, 185, 129, 0.42)";

    lblUsageBadge->setText(text);
    lblUsageBadge->setToolTip(used ? tr("This address has already received funds on-chain")
                                   : tr("Fresh address — better privacy for new incoming payments"));
    lblUsageBadge->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 700; }"
    ).arg(fg, bg, brd));
    lblUsageBadge->setVisible(true);
}

void ReceiveWidget::updateQrFromFields()
{
    if (!info) info = new SendCoinsRecipient();
    info->address = currentAddress;

    if (editLabel) {
        info->label = editLabel->text().trimmed();
    }
    if (editMessage) {
        info->message = editMessage->text().trimmed();
    }
    info->amount = 0;
    if (editAmount && !editAmount->text().trimmed().isEmpty() && walletModel && walletModel->getOptionsModel()) {
        bool validAmt = false;
        const int unit = walletModel->getOptionsModel()->getDisplayUnit();
        CAmount parsed = GUIUtil::parseValue(editAmount->text().trimmed(), unit, &validAmt);
        if (validAmt && parsed > 0) {
            info->amount = parsed;
        }
    }

    updateQr(currentAddress);

    if (lblLiveUriHint) {
        const bool hasReqParams = (info->amount > 0 || !info->label.isEmpty() || !info->message.isEmpty());
        if (hasReqParams && !currentAddress.isEmpty()) {
            const QString uri = GUIUtil::formatBitcoinURI(*info);
            const QString shortUri = uri.length() > 74 ? (uri.left(42) + "..." + uri.right(28)) : uri;
            lblLiveUriHint->setText(tr("Payment URI: %1").arg(shortUri));
            lblLiveUriHint->setToolTip(uri);
            lblLiveUriHint->setVisible(true);
        } else {
            lblLiveUriHint->setVisible(false);
        }
    }
}

void ReceiveWidget::updateQr(QString address)
{
    if (address.isEmpty()) {
        lblQrImg->clear();
        return;
    }
    if (!info) info = new SendCoinsRecipient();
    info->address = address;
    updateLabel();

    QString uri = GUIUtil::formatBitcoinURI(*info);
    lblQrImg->setText(QString());

    QString error;
    QColor qrColor("#0F172A");
    QPixmap pixmap = encodeToQr(uri, error, qrColor);
    if (!pixmap.isNull()) {
        qrImage = pixmap;
        const int qrSize = qMin(lblQrImg->width(), lblQrImg->height()) - 14;
        lblQrImg->setPixmap(qrImage.scaled(qrSize, qrSize, Qt::KeepAspectRatio, Qt::FastTransformation));
    } else {
        qrImage = QPixmap();
        lblQrImg->setText(!error.isEmpty() ? error : tr("Error encoding address"));
    }
}

void ReceiveWidget::rebuildAddressTable()
{
    if (!tableAddresses || !filter || !addressTableModel) return;

    const int nRows = filter->rowCount();
    if (lblAddrCount) {
        lblAddrCount->setText(tr("%1 addresses").arg(nRows));
    }

    const bool empty = (nRows == 0);
    tableAddresses->setVisible(!empty);
    if (lblEmptyList) lblEmptyList->setVisible(empty);
    if (empty) {
        tableAddresses->setRowCount(0);
        return;
    }

    const bool light = isLightTheme();
    const QColor mutedFg = light ? QColor("#64748B") : QColor("#94A3B8");
    const QColor monoFg  = light ? QColor("#334155") : QColor("#CBD5E1");
    int selectedRow = -1;

    tableAddresses->setUpdatesEnabled(false);
    tableAddresses->setRowCount(nRows);
    for (int r = 0; r < nRows; ++r) {
        const QModelIndex addrProxyIdx  = filter->index(r, AddressTableModel::Address);
        const QModelIndex labelProxyIdx = filter->index(r, AddressTableModel::Label);
        const QModelIndex dateProxyIdx  = filter->index(r, AddressTableModel::Date);

        const QString addr  = addrProxyIdx.data(Qt::DisplayRole).toString();
        const QString label = labelProxyIdx.data(Qt::DisplayRole).toString();
        const QString dateStr = dateProxyIdx.data(Qt::DisplayRole).toString();

        QTableWidgetItem* nameItem = new QTableWidgetItem(label.isEmpty() ? tr("(no label)") : label);
        QFont nameFont = nameItem->font();
        nameFont.setWeight(label.isEmpty() ? QFont::Normal : QFont::DemiBold);
        nameFont.setItalic(label.isEmpty());
        nameItem->setFont(nameFont);
        if (label.isEmpty()) nameItem->setForeground(mutedFg);
        if (!dateStr.isEmpty()) nameItem->setToolTip(tr("Created: %1").arg(dateStr));
        tableAddresses->setItem(r, 0, nameItem);

        const QString compactAddr = addr.length() > 26 ? (addr.left(12) + "..." + addr.right(10)) : addr;
        QTableWidgetItem* addrItem = new QTableWidgetItem(compactAddr);
        QFont monoFont("Monospace");
        monoFont.setStyleHint(QFont::TypeWriter);
        monoFont.setPointSize(9);
        addrItem->setFont(monoFont);
        addrItem->setForeground(monoFg);
        addrItem->setData(Qt::UserRole, addr);
        addrItem->setToolTip(addr);
        tableAddresses->setItem(r, 1, addrItem);

        QWidget* actionsCell = new QWidget(tableAddresses);
        actionsCell->setStyleSheet("background: transparent;");
        QHBoxLayout* actionsLayout = new QHBoxLayout(actionsCell);
        actionsLayout->setContentsMargins(4, 3, 4, 3);
        actionsLayout->setSpacing(6);

        QPushButton* btnRowCopy = new QPushButton(tr("Copy"), actionsCell);
        btnRowCopy->setObjectName("btnRecvRowCopy");
        btnRowCopy->setCursor(Qt::PointingHandCursor);
        btnRowCopy->setFocusPolicy(Qt::NoFocus);
        connect(btnRowCopy, &QPushButton::clicked, this, [this, addr]() {
            GUIUtil::setClipboard(addr);
            refreshView(addr);
            inform(tr("Address copied"));
        });

        QPushButton* btnRowEdit = new QPushButton(tr("Edit"), actionsCell);
        btnRowEdit->setObjectName("btnRecvRowEdit");
        btnRowEdit->setCursor(Qt::PointingHandCursor);
        btnRowEdit->setFocusPolicy(Qt::NoFocus);
        connect(btnRowEdit, &QPushButton::clicked, this, [this, addr]() {
            editLabelForAddress(addr);
        });

        actionsLayout->addWidget(btnRowCopy);
        actionsLayout->addWidget(btnRowEdit);
        tableAddresses->setCellWidget(r, 2, actionsCell);
        tableAddresses->setRowHeight(r, 42);

        if (addr == currentAddress) {
            selectedRow = r;
        }
    }

    if (selectedRow >= 0) {
        tableAddresses->selectRow(selectedRow);
    }
    tableAddresses->setUpdatesEnabled(true);
}

void ReceiveWidget::editLabelForAddress(const QString& address)
{
    if (!walletModel || !addressTableModel || address.isEmpty() || isShowingDialog) return;
    isShowingDialog = true;
    showHideOp(true);
    const QString oldLabel = addressTableModel->labelForAddress(address);
    AddNewContactDialog *dialog = new AddNewContactDialog(window);
    dialog->setTexts(tr("Edit Address Label"));
    dialog->setData(address, oldLabel);
    if (openDialogWithOpaqueBackgroundY(dialog, window, 3.5, 6)) {
        const QString newLabel = dialog->getLabel().trimmed();
        if (newLabel != oldLabel) {
            const CTxDestination dest = DecodeDestination(address.toUtf8().constData());
            if (walletModel->updateAddressBookLabels(dest, newLabel.toUtf8().constData(),
                                                     AddressBook::AddressBookPurpose::RECEIVE)) {
                if (address == currentAddress) {
                    updatingRequestFields = true;
                    if (editLabel) editLabel->setText(newLabel);
                    updatingRequestFields = false;
                    updateQrFromFields();
                }
                rebuildAddressTable();
                inform(tr("Address label saved"));
            } else {
                inform(tr("Error storing address label"));
            }
        }
    }
    dialog->deleteLater();
    isShowingDialog = false;
}

void ReceiveWidget::handleAddressClicked(const QModelIndex &index)
{
    if (!filter || !index.isValid()) return;
    QModelIndex rIndex = filter->mapToSource(index);
    refreshView(rIndex.sibling(rIndex.row(), AddressTableModel::Address).data(Qt::DisplayRole).toString());
}

void ReceiveWidget::onLabelClicked()
{
    editLabelForAddress(currentAddress);
}

void ReceiveWidget::onSaveInlineLabelClicked()
{
    if (!walletModel || !addressTableModel || currentAddress.isEmpty() || !editLabel) return;
    const QString newLabel = editLabel->text().trimmed();
    const QString oldLabel = addressTableModel->labelForAddress(currentAddress);
    if (newLabel == oldLabel) {
        inform(tr("Address label unchanged"));
        return;
    }

    const CTxDestination dest = DecodeDestination(currentAddress.toUtf8().constData());
    if (walletModel->updateAddressBookLabels(dest, newLabel.toUtf8().constData(),
                                             AddressBook::AddressBookPurpose::RECEIVE)) {
        updateLabel();
        updateQrFromFields();
        rebuildAddressTable();
        inform(tr("Address label saved"));
    } else {
        inform(tr("Error storing address label"));
    }
}

void ReceiveWidget::onClearRequestClicked()
{
    updatingRequestFields = true;
    if (editAmount) editAmount->clear();
    if (editMessage) editMessage->clear();
    if (editLabel && addressTableModel && !currentAddress.isEmpty()) {
        editLabel->setText(addressTableModel->labelForAddress(currentAddress));
    }
    updatingRequestFields = false;
    updateQrFromFields();
}

void ReceiveWidget::onNewAddressClicked()
{
    if (!walletModel) return;
    try {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            inform(tr("Cannot create new address, wallet locked"));
            return;
        }
        Destination address;
        PairResult r = walletModel->getNewAddress(address, "");
        if (!r.result) {
            inform(r.status ? r.status->c_str() : "Error generating address");
            return;
        }

        const QString newAddrStr = QString::fromStdString(address.ToString());
        updatingRequestFields = true;
        if (editLabel) editLabel->clear();
        if (editAmount) editAmount->clear();
        if (editMessage) editMessage->clear();
        updatingRequestFields = false;

        refreshView(newAddrStr);
        inform(tr("New address created"));
    } catch (const std::runtime_error&) {
        inform(tr("Error generating address"));
    }
}

void ReceiveWidget::onCopyClicked()
{
    if (currentAddress.isEmpty()) return;
    GUIUtil::setClipboard(currentAddress);
    inform(tr("Address copied"));
}

void ReceiveWidget::onCopyUriClicked()
{
    if (!info || info->address.isEmpty()) return;
    GUIUtil::setClipboard(GUIUtil::formatBitcoinURI(*info));
    inform(tr("Payment URI copied"));
}

void ReceiveWidget::onSaveQrClicked()
{
    if (qrImage.isNull()) {
        inform(tr("No QR code available to save"));
        return;
    }

    const QString defaultName = currentAddress.isEmpty()
        ? QStringLiteral("krista-qr.png")
        : QStringLiteral("krista-%1.png").arg(currentAddress.left(10));
    const QString fileName = QFileDialog::getSaveFileName(
        this,
        tr("Save QR Code Image"),
        defaultName,
        tr("PNG Image (*.png)")
    );
    if (fileName.isEmpty()) return;

    const int outSize = 480;
    const int pad = 28;
    QPixmap exportPix(outSize, outSize);
    exportPix.fill(Qt::white);
    {
        QPainter p(&exportPix);
        QPixmap scaled = qrImage.scaled(outSize - pad * 2, outSize - pad * 2, Qt::KeepAspectRatio, Qt::FastTransformation);
        const int x = (outSize - scaled.width()) / 2;
        const int y = (outSize - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
    }

    if (exportPix.save(fileName, "PNG")) {
        inform(tr("QR image saved"));
    } else {
        inform(tr("Failed to save QR image"));
    }
}

void ReceiveWidget::onRequestClicked()
{
    showAddressGenerationDialog();
}

void ReceiveWidget::showAddressGenerationDialog()
{
    if (walletModel && !isShowingDialog) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            inform(tr("Cannot perform operation, wallet locked"));
            return;
        }
        isShowingDialog = true;
        showHideOp(true);
        RequestDialog *dialog = new RequestDialog(window);
        dialog->setWalletModel(walletModel);
        openDialogWithOpaqueBackgroundY(dialog, window, 3.5, 12);
        if (dialog->res == 1) {
            inform(tr("URI copied to clipboard"));
        } else if (dialog->res == 2) {
            inform(tr("Address copied to clipboard"));
        }
        dialog->deleteLater();
        isShowingDialog = false;
    }
}

void ReceiveWidget::onMyAddressesClicked()
{
    if (lineEditSearch) lineEditSearch->setFocus();
}

void ReceiveWidget::onSortChanged(int idx)
{
    if (!comboBoxSort) return;
    sortType = static_cast<AddressTableModel::ColumnIndex>(comboBoxSort->itemData(idx).toInt());
    sortAddresses();
}

void ReceiveWidget::onSortOrderChanged(int idx)
{
    if (!comboBoxSortOrder) return;
    sortOrder = static_cast<Qt::SortOrder>(comboBoxSortOrder->itemData(idx).toInt());
    sortAddresses();
}

void ReceiveWidget::sortAddresses()
{
    if (this->filter) {
        this->filter->sort(sortType, sortOrder);
        rebuildAddressTable();
    }
}

void ReceiveWidget::changeTheme(bool isLightTheme, QString& theme)
{
    applyTheme(isLightTheme);
}

ReceiveWidget::~ReceiveWidget()
{
    delete info;
}
