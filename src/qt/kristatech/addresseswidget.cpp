// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/addresseswidget.h"

#include "base58.h"
#include "guiutil.h"
#include "qt/kristatech/addnewcontactdialog.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "script/standard.h"
#include "walletmodel.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <boost/variant/get.hpp>

AddressesWidget::AddressesWidget(KRISTATECHGUI* parent) :
    PWidget(parent)
{
    if (parent) {
        this->setStyleSheet(parent->styleSheet());
    }

    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(22, 18, 22, 18);
    rootLayout->setSpacing(18);

    // =========================================================================
    // LEFT COLUMN: Header + Searchable Saved Contacts Card
    // =========================================================================
    QWidget* leftCol = new QWidget(this);
    QVBoxLayout* leftLayout = new QVBoxLayout(leftCol);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(12);

    lblTitle = new QLabel(tr("Address Book"), leftCol);
    lblSubtitle = new QLabel(tr("Manage saved KRISTA recipients for 1-click sending, copying, or editing."), leftCol);
    lblSubtitle->setWordWrap(true);
    leftLayout->addWidget(lblTitle);
    leftLayout->addWidget(lblSubtitle);

    listCard = new QFrame(leftCol);
    listCard->setObjectName("addressesListCard");
    listCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(16, 14, 16, 14);
    listLayout->setSpacing(10);

    QHBoxLayout* listHeader = new QHBoxLayout();
    listHeader->setContentsMargins(0, 0, 0, 0);
    listHeader->setSpacing(8);
    lblListTitle = new QLabel(tr("Saved Contacts"), listCard);
    lblContactCount = new QLabel(tr("0 contacts"), listCard);

    comboBoxSort = new QComboBox(listCard);
    comboBoxSort->setMinimumHeight(34);
    comboBoxSort->setMinimumWidth(108);
    comboBoxSortOrder = new QComboBox(listCard);
    comboBoxSortOrder->setMinimumHeight(34);
    comboBoxSortOrder->setMinimumWidth(82);
    {
        const QSignalBlocker blockType(comboBoxSort);
        const QSignalBlocker blockOrder(comboBoxSortOrder);
        fillAddressSortControls(comboBoxSort, comboBoxSortOrder);
    }

    listHeader->addWidget(lblListTitle);
    listHeader->addWidget(lblContactCount);
    listHeader->addStretch();
    listHeader->addWidget(comboBoxSort);
    listHeader->addWidget(comboBoxSortOrder);
    listLayout->addLayout(listHeader);

    lineEditSearchContact = new QLineEdit(listCard);
    lineEditSearchContact->setMinimumHeight(34);
    lineEditSearchContact->setMaximumHeight(34);
    lineEditSearchContact->setClearButtonEnabled(true);
    lineEditSearchContact->setPlaceholderText(tr("Search contacts by label or address..."));
    listLayout->addWidget(lineEditSearchContact);

    tableContacts = new QTableWidget(0, 3, listCard);
    tableContacts->setObjectName("tableContacts");
    tableContacts->setHorizontalHeaderLabels({tr("Contact"), tr("Address"), tr("Actions")});
    tableContacts->verticalHeader()->setVisible(false);
    tableContacts->setSelectionMode(QAbstractItemView::NoSelection);
    tableContacts->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableContacts->setShowGrid(false);
    tableContacts->setFocusPolicy(Qt::NoFocus);
    tableContacts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableContacts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    tableContacts->setColumnWidth(1, 208);
    tableContacts->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableContacts->setColumnWidth(2, 272);
    tableContacts->setContextMenuPolicy(Qt::CustomContextMenu);

    lblEmpty = new QLabel(tr("No saved contacts yet."), listCard);
    lblEmpty->setAlignment(Qt::AlignCenter);
    lblEmpty->setVisible(false);

    listLayout->addWidget(tableContacts, 1);
    listLayout->addWidget(lblEmpty, 1);
    leftLayout->addWidget(listCard, 1);

    // =========================================================================
    // RIGHT COLUMN: Fixed Add New Contact Card
    // =========================================================================
    QWidget* rightCol = new QWidget(this);
    rightCol->setMinimumWidth(320);
    rightCol->setMaximumWidth(390);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    addCard = new QFrame(rightCol);
    addCard->setObjectName("addressesAddCard");
    addCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* addLayout = new QVBoxLayout(addCard);
    addLayout->setContentsMargins(16, 16, 16, 16);
    addLayout->setSpacing(10);

    lblAddTitle = new QLabel(tr("Add New Contact"), addCard);
    lblAddSub = new QLabel(tr("Store Base58 (KT...) or Taproot/MESCAL (kt1p...) recipient addresses."), addCard);
    lblAddSub->setWordWrap(true);
    addLayout->addWidget(lblAddTitle);
    addLayout->addWidget(lblAddSub);

    lblAddrStatus = new QLabel(addCard);
    lblAddrStatus->setWordWrap(true);
    lblAddrStatus->setVisible(false);
    addLayout->addWidget(lblAddrStatus);

    lblNameKey = new QLabel(tr("Contact Name / Label"), addCard);
    lineEditName = new QLineEdit(addCard);
    lineEditName->setMinimumHeight(38);
    lineEditName->setMaximumHeight(38);
    lineEditName->setClearButtonEnabled(true);
    lineEditName->setPlaceholderText(tr("e.g. Exchange, Cold Storage, Alice..."));
    addLayout->addWidget(lblNameKey);
    addLayout->addWidget(lineEditName);

    lblAddressKey = new QLabel(tr("Recipient Address"), addCard);
    addLayout->addWidget(lblAddressKey);

    QHBoxLayout* addrRow = new QHBoxLayout();
    addrRow->setContentsMargins(0, 0, 0, 0);
    addrRow->setSpacing(8);

    lineEditAddress = new QLineEdit(addCard);
    lineEditAddress->setMinimumHeight(38);
    lineEditAddress->setMaximumHeight(38);
    lineEditAddress->setClearButtonEnabled(true);
    lineEditAddress->setPlaceholderText(tr("KT... / kt1p... or kristatech: URI"));

    btnPasteAddr = new QPushButton(tr("Paste"), addCard);
    btnPasteAddr->setCursor(Qt::PointingHandCursor);
    btnPasteAddr->setFocusPolicy(Qt::NoFocus);
    btnPasteAddr->setFixedHeight(38);
    btnPasteAddr->setMinimumWidth(68);
    btnPasteAddr->setToolTip(tr("Paste a KRISTA address or kristatech: payment URI from clipboard"));

    addrRow->addWidget(lineEditAddress, 1);
    addrRow->addWidget(btnPasteAddr);
    addLayout->addLayout(addrRow);

    QHBoxLayout* formBtns = new QHBoxLayout();
    formBtns->setContentsMargins(0, 6, 0, 0);
    formBtns->setSpacing(8);

    btnClearForm = new QPushButton(tr("Clear"), addCard);
    btnClearForm->setCursor(Qt::PointingHandCursor);
    btnClearForm->setFocusPolicy(Qt::NoFocus);
    btnClearForm->setFixedHeight(38);
    btnClearForm->setMinimumWidth(84);

    btnSave = new QPushButton(tr("Save Contact"), addCard);
    btnSave->setCursor(Qt::PointingHandCursor);
    btnSave->setFocusPolicy(Qt::NoFocus);
    btnSave->setFixedHeight(38);

    formBtns->addWidget(btnClearForm);
    formBtns->addWidget(btnSave, 1);
    addLayout->addLayout(formBtns);

    rightLayout->addWidget(addCard);
    rightLayout->addStretch(1);

    rootLayout->addWidget(leftCol, 15);
    rootLayout->addWidget(rightCol, 9);

    rebuildTimer = new QTimer(this);
    rebuildTimer->setSingleShot(true);
    rebuildTimer->setInterval(60);
    connect(rebuildTimer, &QTimer::timeout, this, &AddressesWidget::flushTableRebuild);

    connect(lineEditSearchContact, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (filter) {
            filter->setFilterFixedString(text.trimmed());
            scheduleTableRebuild();
        }
    });
    connect(comboBoxSort, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &AddressesWidget::onSortChanged);
    connect(comboBoxSortOrder, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &AddressesWidget::onSortOrderChanged);

    connect(lineEditAddress, &QLineEdit::textChanged, this, &AddressesWidget::updateAddressValidationUi);
    connect(btnPasteAddr, &QPushButton::clicked, this, &AddressesWidget::onPasteAddressClicked);
    connect(btnClearForm, &QPushButton::clicked, this, &AddressesWidget::onClearFormClicked);
    connect(btnSave, &QPushButton::clicked, this, &AddressesWidget::onStoreContactClicked);

    connect(tableContacts, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!tableContacts || row < 0 || row >= tableContacts->rowCount()) return;
        QTableWidgetItem* addrItem = tableContacts->item(row, 1);
        if (!addrItem) return;
        const QString fullAddr = addrItem->data(Qt::UserRole).toString();
        if (!fullAddr.isEmpty()) {
            GUIUtil::setClipboard(fullAddr);
            inform(tr("Address copied"));
        }
    });

    connect(tableContacts, &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        if (!tableContacts || !filter) return;
        const int row = tableContacts->rowAt(pt.y());
        if (row < 0 || row >= filter->rowCount()) return;
        const QModelIndex addrProxyIdx  = filter->index(row, AddressTableModel::Address);
        const QModelIndex labelProxyIdx = filter->index(row, AddressTableModel::Label);
        const QString addr  = addrProxyIdx.data(Qt::DisplayRole).toString();
        const QString label = labelProxyIdx.data(Qt::DisplayRole).toString();
        this->index = QPersistentModelIndex(filter->mapToSource(addrProxyIdx));

        QMenu ctxMenu(this);
        QAction* actSend   = ctxMenu.addAction(tr("Send KRISTA"));
        QAction* actCopy   = ctxMenu.addAction(tr("Copy Address"));
        QAction* actEdit   = ctxMenu.addAction(tr("Edit Contact"));
        ctxMenu.addSeparator();
        QAction* actDelete = ctxMenu.addAction(tr("Delete Contact"));
        QAction* chosen = ctxMenu.exec(tableContacts->viewport()->mapToGlobal(pt));
        if (chosen == actSend)        sendToContact(addr, label);
        else if (chosen == actCopy)   onCopyClicked();
        else if (chosen == actEdit)   onEditClicked();
        else if (chosen == actDelete) onDeleteClicked();
    });

    applyTheme(isLightTheme());
}

void AddressesWidget::applyTheme(bool isLight)
{
    const QString cardBg   = isLight ? "#FFFFFF" : "#131B2E";
    const QString cardBrd  = isLight ? "#CBD5E1" : "#253554";
    const QString titleCol = isLight ? "#0F172A" : "#F8FAFC";
    const QString subCol   = isLight ? "#475569" : "#CBD5E1";
    const QString keyCol   = isLight ? "#334155" : "#CBD5E1";
    const QString inputBg  = isLight ? "#F8FAFC" : "#0D1424";
    const QString inputBrd = isLight ? "#CBD5E1" : "#2D4066";
    const QString secBg    = isLight ? "#F1F5F9" : "#1C2942";
    const QString secFg    = isLight ? "#1E293B" : "#E2E8F0";
    const QString secBrd   = isLight ? "#CBD5E1" : "#33486E";
    const QString secHov   = isLight ? "#E2E8F0" : "#26385A";
    const QString rowHover = isLight ? "#F8FAFC" : "#1A2540";
    const QString altRowBg = isLight ? "#F8FAFC" : "#172238";
    const QString divBrd   = isLight ? "#E2E8F0" : "#243452";
    const QString delBrd   = isLight ? "#9E3329" : "#F08074";
    const QString delHovB  = isLight ? "#7A231B" : "#F5897C";
    const QString sendBg   = isLight ? "#EEF3FA" : "#1B2B4A";
    const QString sendFg   = isLight ? "#3E5B94" : "#A8C1EE";

    if (lblTitle)    lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 22px; font-weight: 700; background: transparent; }").arg(titleCol));
    if (lblSubtitle) lblSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(subCol));

    const QString cardCss = QString("QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }");
    if (listCard) listCard->setStyleSheet(cardCss.arg("addressesListCard", cardBg, cardBrd));
    if (addCard)  addCard->setStyleSheet(cardCss.arg("addressesAddCard", cardBg, cardBrd));

    const QString secTitleStyle = QString("QLabel { color: %1; font-size: 14px; font-weight: 700; background: transparent; }").arg(titleCol);
    if (lblListTitle) lblListTitle->setStyleSheet(secTitleStyle);
    if (lblAddTitle)  lblAddTitle->setStyleSheet(secTitleStyle);

    if (lblAddSub)    lblAddSub->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; background: transparent; }").arg(subCol));
    if (lblNameKey)   lblNameKey->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }").arg(keyCol));
    if (lblAddressKey) lblAddressKey->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }").arg(keyCol));
    if (lblEmpty)     lblEmpty->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; padding: 28px; background: transparent; }").arg(subCol));

    if (lblContactCount) {
        lblContactCount->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(secBg, subCol, secBrd));
    }

    const QString secBtnCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
    ).arg(secBg, secFg, secBrd, secHov);
    if (btnPasteAddr) btnPasteAddr->setStyleSheet(secBtnCss);
    if (btnClearForm) btnClearForm->setStyleSheet(secBtnCss);

    if (btnSave) {
        btnSave->setStyleSheet(
            "QPushButton { background-color: #5576B2; color: #FFFFFF; border: 1.5px solid #7695CD; border-radius: 7px; padding: 0 16px; font-size: 13px; font-weight: 700; }"
            "QPushButton:hover { background-color: #466399; border-color: #F5897C; }"
            "QPushButton:pressed { background-color: #3A5484; }"
        );
    }

    const QString comboCss = QString(
        "QComboBox { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 4px 10px; font-size: 12px; font-weight: 600; }"
        "QComboBox:hover { border-color: #7695CD; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: %4; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; }"
    ).arg(inputBg, titleCol, inputBrd, cardBg);
    if (comboBoxSort)      comboBoxSort->setStyleSheet(comboCss);
    if (comboBoxSortOrder) comboBoxSortOrder->setStyleSheet(comboCss);

    if (tableContacts) {
        QString css = QString(
            "QTableWidget#tableContacts { background-color: %1; border: none; color: %2; font-size: 12px; selection-background-color: transparent; }"
            "QTableWidget#tableContacts::item { padding: 7px 10px; border-bottom: 1px solid %3; }"
            "QTableWidget#tableContacts::item:hover { background-color: %4; color: %2; }"
            "QHeaderView::section { background-color: %5; color: %6; border: none; border-bottom: 1px solid %3; padding: 7px 10px; font-size: 11px; font-weight: 700; text-align: left; }"
            "QPushButton#btnRowSend { background-color: %7; color: %8; border: 1px solid #7695CD; border-radius: 5px; padding: 4px 10px; font-size: 11px; font-weight: 700; }"
            "QPushButton#btnRowSend:hover { background-color: #5576B2; color: #FFFFFF; border-color: #7695CD; }"
        ).arg(cardBg, titleCol, divBrd, rowHover, altRowBg, subCol, sendBg, sendFg);
        css += QString(
            "QPushButton#btnRowCopy, QPushButton#btnRowEdit { background-color: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 4px 9px; font-size: 11px; font-weight: 600; }"
            "QPushButton#btnRowCopy:hover, QPushButton#btnRowEdit:hover { border-color: #7695CD; background-color: %4; }"
            "QPushButton#btnRowDelete { background-color: #C94A3E; color: #FFFFFF; border: 1px solid %5; border-radius: 5px; padding: 4px 9px; font-size: 11px; font-weight: 600; }"
            "QPushButton#btnRowDelete:hover { background-color: #A6362B; border-color: %6; }"
            "QPushButton#btnRowDelete:pressed { background-color: #882920; }"
        ).arg(secBg, secFg, secBrd, secHov, delBrd, delHovB);
        tableContacts->setStyleSheet(css);
    }

    refreshInputStyles();
    if (lineEditAddress) {
        updateAddressValidationUi(lineEditAddress->text());
    }
    if (isVisible()) {
        tableDirty = true;
        flushTableRebuild();
    } else {
        tableDirty = true;
    }
}

void AddressesWidget::refreshInputStyles()
{
    const bool light = isLightTheme();
    const QString inputBg  = light ? "#F8FAFC" : "#0D1424";
    const QString titleCol = light ? "#0F172A" : "#F8FAFC";
    const QString inputBrd = light ? "#CBD5E1" : "#2D4066";
    const QString focusBrd = "#7695CD";
    const QString errBrd   = "#C94A3E";

    auto makeInputCss = [&](bool invalid, bool monospace) -> QString {
        return QString(
            "QLineEdit { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 13px; %5 }"
            "QLineEdit:focus { border-color: %4; }"
        ).arg(inputBg,
              titleCol,
              invalid ? errBrd : inputBrd,
              invalid ? errBrd : focusBrd,
              monospace ? QStringLiteral("font-family: monospace;") : QString());
    };

    if (lineEditSearchContact) {
        lineEditSearchContact->setStyleSheet(makeInputCss(false, false));
        styleLineEditClearButton(lineEditSearchContact);
    }
    if (lineEditName) {
        lineEditName->setStyleSheet(makeInputCss(false, false));
        styleLineEditClearButton(lineEditName);
    }
    if (lineEditAddress) {
        lineEditAddress->setStyleSheet(makeInputCss(addrInvalid, true));
        styleLineEditClearButton(lineEditAddress);
    }
}

void AddressesWidget::updateAddressValidationUi(const QString& rawText)
{
    if (!lblAddrStatus) return;
    QString trimmed = rawText.trimmed();
    if (trimmed.isEmpty()) {
        addrInvalid = false;
        lblAddrStatus->setVisible(false);
        refreshInputStyles();
        return;
    }

    if (!walletModel) {
        addrInvalid = false;
        lblAddrStatus->setVisible(false);
        return;
    }

    // Parse kristatech: URI automatically if pasted directly into address input
    if (!walletModel->validateAddress(trimmed)) {
        SendCoinsRecipient rcp;
        if (GUIUtil::parseBitcoinURI(trimmed, &rcp) && !rcp.address.isEmpty()) {
            const QSignalBlocker blockAddr(lineEditAddress);
            lineEditAddress->setText(rcp.address);
            if (lineEditName && lineEditName->text().trimmed().isEmpty()) {
                if (!rcp.label.isEmpty()) lineEditName->setText(rcp.label);
                else if (!rcp.message.isEmpty()) lineEditName->setText(rcp.message);
            }
            trimmed = rcp.address;
        }
    }

    const bool light = isLightTheme();
    QString badgeText;
    QString fg, bg, brd;

    if (!walletModel->validateAddress(trimmed)) {
        addrInvalid = true;
        badgeText = tr("\xE2\x9C\x97 Invalid KRISTA address");
        fg  = light ? "#B93E33" : "#F08074";
        bg  = "rgba(201, 74, 62, 0.12)";
        brd = "rgba(201, 74, 62, 0.42)";
    } else {
        addrInvalid = false;
        const CTxDestination dest = DecodeDestination(trimmed.toStdString());
        const bool isOwn = walletModel->isMine(dest);
        const bool alreadySaved = addressTablemodel && (addressTablemodel->lookupAddress(trimmed) >= 0);
        const QString savedLabel = addressTablemodel ? addressTablemodel->labelForAddress(trimmed) : QString();

        if (isOwn) {
            badgeText = tr("\xE2\x9A\xA0 Own wallet address (cannot store as contact)");
            fg  = light ? "#B5463A" : "#F5897C";
            bg  = "rgba(245, 137, 124, 0.13)";
            brd = "rgba(245, 137, 124, 0.42)";
        } else if (alreadySaved) {
            badgeText = savedLabel.isEmpty()
                ? tr("\xE2\x80\xA2 Already in Address Book")
                : tr("\xE2\x80\xA2 Already saved: %1").arg(savedLabel);
            fg  = light ? "#3E5B94" : "#93B4F0";
            bg  = light ? "rgba(85, 118, 178, 0.10)" : "rgba(118, 149, 205, 0.16)";
            brd = light ? "rgba(85, 118, 178, 0.34)" : "rgba(118, 149, 205, 0.42)";
        } else if (boost::get<WitnessV1Taproot>(&dest) != nullptr) {
            badgeText = tr("\xE2\x9C\x93 Taproot / MESCAL (Bech32m)");
            fg  = light ? "#5B4DB7" : "#C4B5FD";
            bg  = light ? "rgba(124, 108, 214, 0.11)" : "rgba(139, 127, 214, 0.18)";
            brd = light ? "rgba(124, 108, 214, 0.36)" : "rgba(139, 127, 214, 0.44)";
        } else if (boost::get<CScriptID>(&dest) != nullptr) {
            badgeText = tr("\xE2\x9C\x93 P2SH Script Address");
            fg  = light ? "#3B5D8F" : "#8AB4E8";
            bg  = light ? "rgba(70, 108, 165, 0.10)" : "rgba(118, 162, 224, 0.16)";
            brd = light ? "rgba(70, 108, 165, 0.34)" : "rgba(118, 162, 224, 0.42)";
        } else {
            badgeText = tr("\xE2\x9C\x93 Valid KRISTA Address");
            fg  = light ? "#3E5B94" : "#93B4F0";
            bg  = light ? "rgba(85, 118, 178, 0.10)" : "rgba(118, 149, 205, 0.16)";
            brd = light ? "rgba(85, 118, 178, 0.34)" : "rgba(118, 149, 205, 0.42)";
        }
    }

    lblAddrStatus->setText(badgeText);
    lblAddrStatus->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 8px; padding: 5px 10px; font-size: 11px; font-weight: 600; }"
    ).arg(fg, bg, brd));
    lblAddrStatus->setVisible(true);
    refreshInputStyles();
}

void AddressesWidget::scheduleTableRebuild()
{
    tableDirty = true;
    if (!isVisible()) return;
    if (rebuildTimer && !rebuildTimer->isActive()) {
        rebuildTimer->start();
    }
}

void AddressesWidget::flushTableRebuild()
{
    if (!tableDirty) return;
    tableDirty = false;
    updateListView();
}

void AddressesWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    flushTableRebuild();
}

void AddressesWidget::loadWalletModel()
{
    if (!walletModel) return;

    addressTablemodel = walletModel->getAddressTableModel();
    this->filter = new AddressFilterProxyModel(QStringList({AddressTableModel::Send}), this);
    this->filter->setSourceModel(addressTablemodel);
    this->filter->sort(sortType, sortOrder);

    connect(this->filter, &QAbstractItemModel::rowsInserted,  this, [this]() { scheduleTableRebuild(); });
    connect(this->filter, &QAbstractItemModel::rowsRemoved,   this, [this]() { scheduleTableRebuild(); });
    connect(this->filter, &QAbstractItemModel::modelReset,    this, [this]() { scheduleTableRebuild(); });
    connect(this->filter, &QAbstractItemModel::dataChanged,   this, [this]() { scheduleTableRebuild(); });
    connect(this->filter, &QAbstractItemModel::layoutChanged, this, [this]() { scheduleTableRebuild(); });

    tableDirty = true;
    if (isVisible()) {
        flushTableRebuild();
    }
    if (lineEditAddress) {
        updateAddressValidationUi(lineEditAddress->text());
    }
}

void AddressesWidget::updateListView()
{
    const int nRows = filter ? filter->rowCount() : 0;
    const int totalContacts = addressTablemodel ? addressTablemodel->sizeSend() : 0;
    if (lblContactCount) {
        lblContactCount->setText(tr("%1 contacts").arg(nRows));
    }

    const bool empty = (nRows == 0);
    if (lblEmpty) {
        lblEmpty->setText(totalContacts == 0 ? tr("No saved contacts yet.") : tr("No matching contacts found."));
        lblEmpty->setVisible(empty);
    }
    if (tableContacts) {
        tableContacts->setVisible(!empty);
        if (!empty) {
            rebuildContactsTable();
        } else {
            tableContacts->setRowCount(0);
        }
    }
}

void AddressesWidget::rebuildContactsTable()
{
    if (!tableContacts || !filter) return;
    const int nRows = filter->rowCount();
    const bool light = isLightTheme();
    const QColor mutedFg = light ? QColor("#64748B") : QColor("#94A3B8");
    const QColor monoFg  = light ? QColor("#334155") : QColor("#CBD5E1");

    tableContacts->setUpdatesEnabled(false);
    tableContacts->setRowCount(nRows);
    for (int r = 0; r < nRows; ++r) {
        const QModelIndex addrProxyIdx  = filter->index(r, AddressTableModel::Address);
        const QModelIndex labelProxyIdx = filter->index(r, AddressTableModel::Label);
        const QString addr  = addrProxyIdx.data(Qt::DisplayRole).toString();
        const QString label = labelProxyIdx.data(Qt::DisplayRole).toString();
        const QPersistentModelIndex sourceIdx(filter->mapToSource(addrProxyIdx));

        QTableWidgetItem* nameItem = new QTableWidgetItem(label.isEmpty() ? tr("(no label)") : label);
        QFont nameFont = nameItem->font();
        nameFont.setWeight(label.isEmpty() ? QFont::Normal : QFont::DemiBold);
        nameFont.setItalic(label.isEmpty());
        nameItem->setFont(nameFont);
        if (label.isEmpty()) nameItem->setForeground(mutedFg);
        tableContacts->setItem(r, 0, nameItem);

        const QString compactAddr = addr.length() > 26 ? (addr.left(12) + "..." + addr.right(10)) : addr;
        QTableWidgetItem* addrItem = new QTableWidgetItem(compactAddr);
        QFont monoFont("Monospace");
        monoFont.setStyleHint(QFont::TypeWriter);
        monoFont.setPointSize(9);
        addrItem->setFont(monoFont);
        addrItem->setForeground(monoFg);
        addrItem->setData(Qt::UserRole, addr);
        addrItem->setToolTip(addr);
        tableContacts->setItem(r, 1, addrItem);

        QWidget* actionsCell = new QWidget(tableContacts);
        actionsCell->setStyleSheet("background: transparent;");
        QHBoxLayout* actionsLayout = new QHBoxLayout(actionsCell);
        actionsLayout->setContentsMargins(4, 4, 4, 4);
        actionsLayout->setSpacing(6);

        QPushButton* btnSend = new QPushButton(tr("Send"), actionsCell);
        btnSend->setObjectName("btnRowSend");
        btnSend->setCursor(Qt::PointingHandCursor);
        btnSend->setFocusPolicy(Qt::NoFocus);
        btnSend->setToolTip(tr("Pre-fill this recipient on the Send screen"));
        connect(btnSend, &QPushButton::clicked, this, [this, addr, label]() {
            sendToContact(addr, label);
        });

        QPushButton* btnCopy = new QPushButton(tr("Copy"), actionsCell);
        btnCopy->setObjectName("btnRowCopy");
        btnCopy->setCursor(Qt::PointingHandCursor);
        btnCopy->setFocusPolicy(Qt::NoFocus);
        connect(btnCopy, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onCopyClicked();
        });

        QPushButton* btnEdit = new QPushButton(tr("Edit"), actionsCell);
        btnEdit->setObjectName("btnRowEdit");
        btnEdit->setCursor(Qt::PointingHandCursor);
        btnEdit->setFocusPolicy(Qt::NoFocus);
        connect(btnEdit, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onEditClicked();
        });

        QPushButton* btnDelete = new QPushButton(tr("Delete"), actionsCell);
        btnDelete->setObjectName("btnRowDelete");
        btnDelete->setCursor(Qt::PointingHandCursor);
        btnDelete->setFocusPolicy(Qt::NoFocus);
        connect(btnDelete, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onDeleteClicked();
        });

        actionsLayout->addWidget(btnSend);
        actionsLayout->addWidget(btnCopy);
        actionsLayout->addWidget(btnEdit);
        actionsLayout->addWidget(btnDelete);
        tableContacts->setCellWidget(r, 2, actionsCell);
        tableContacts->setRowHeight(r, 46);
    }
    tableContacts->setUpdatesEnabled(true);
}

void AddressesWidget::sendToContact(const QString& addr, const QString& label)
{
    if (!window || addr.trimmed().isEmpty()) return;
    window->goToSendWithAddress(addr.trimmed(), label);
}

void AddressesWidget::onNewContactClicked()
{
    if (lineEditName) lineEditName->setFocus();
}

void AddressesWidget::onPasteAddressClicked()
{
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.isEmpty() || !lineEditAddress) return;
    lineEditAddress->setText(clip);
    if (lineEditName && lineEditName->text().trimmed().isEmpty()) {
        lineEditName->setFocus();
    }
}

void AddressesWidget::onClearFormClicked()
{
    if (lineEditName) lineEditName->clear();
    if (lineEditAddress) lineEditAddress->clear();
    addrInvalid = false;
    if (lblAddrStatus) lblAddrStatus->setVisible(false);
    refreshInputStyles();
}

void AddressesWidget::onStoreContactClicked()
{
    if (!walletModel || !lineEditName || !lineEditAddress) return;

    const QString label   = lineEditName->text().trimmed();
    const QString address = lineEditAddress->text().trimmed();

    if (!walletModel->validateAddress(address)) {
        addrInvalid = true;
        refreshInputStyles();
        inform(tr("Invalid Contact Address"));
        return;
    }

    const CTxDestination dest = DecodeDestination(address.toUtf8().constData());
    if (walletModel->isMine(dest)) {
        addrInvalid = true;
        refreshInputStyles();
        inform(tr("Cannot store your own address as contact"));
        return;
    }

    if (addressTablemodel && addressTablemodel->lookupAddress(address) >= 0) {
        const QString storedLabel = addressTablemodel->labelForAddress(address);
        inform(storedLabel.isEmpty()
            ? tr("Address already stored in Address Book")
            : tr("Address already stored, label: %1").arg("'" + storedLabel + "'"));
        return;
    }

    if (walletModel->updateAddressBookLabels(dest, label.toUtf8().constData(), AddressBook::AddressBookPurpose::SEND)) {
        onClearFormClicked();
        tableDirty = true;
        flushTableRebuild();
        inform(tr("New Contact Stored"));
    } else {
        inform(tr("Error Storing Contact"));
    }
}

void AddressesWidget::onEditClicked()
{
    if (!walletModel || !addressTablemodel || !index.isValid()) return;
    const QString address = index.data(Qt::DisplayRole).toString();
    const QString currentLabel = index.sibling(index.row(), AddressTableModel::Label).data(Qt::DisplayRole).toString();
    showHideOp(true);
    AddNewContactDialog* dialog = new AddNewContactDialog(window);
    dialog->setData(address, currentLabel);
    if (openDialogWithOpaqueBackground(dialog, window)) {
        const std::string purpose = addressTablemodel->purposeForAddress(address.toStdString());
        if (walletModel->updateAddressBookLabels(
                DecodeDestination(address.toStdString()),
                dialog->getLabel().trimmed().toStdString(),
                purpose)) {
            tableDirty = true;
            flushTableRebuild();
            inform(tr("Contact edited"));
        } else {
            inform(tr("Contact edit failed"));
        }
    }
    dialog->deleteLater();
}

void AddressesWidget::onDeleteClicked()
{
    if (!walletModel || !addressTablemodel || !index.isValid()) return;
    const QString addr = index.data(Qt::DisplayRole).toString();
    if (ask(tr("Delete Contact"), tr("You are just about to remove the contact:\n\n%1\n\nAre you sure?").arg(addr))) {
        if (addressTablemodel->removeRows(index.row(), 1, index)) {
            tableDirty = true;
            flushTableRebuild();
            inform(tr("Contact Deleted"));
        } else {
            inform(tr("Error deleting a contact"));
        }
    }
}

void AddressesWidget::onCopyClicked()
{
    if (!index.isValid()) return;
    GUIUtil::setClipboard(index.data(Qt::DisplayRole).toString());
    inform(tr("Address copied"));
}

void AddressesWidget::onSortChanged(int idx)
{
    if (!comboBoxSort) return;
    sortType = static_cast<AddressTableModel::ColumnIndex>(comboBoxSort->itemData(idx).toInt());
    sortAddresses();
}

void AddressesWidget::onSortOrderChanged(int idx)
{
    if (!comboBoxSortOrder) return;
    sortOrder = static_cast<Qt::SortOrder>(comboBoxSortOrder->itemData(idx).toInt());
    sortAddresses();
}

void AddressesWidget::sortAddresses()
{
    if (this->filter) {
        this->filter->sort(sortType, sortOrder);
        tableDirty = true;
        flushTableRebuild();
    }
}

void AddressesWidget::changeTheme(bool isLightTheme, QString&)
{
    applyTheme(isLightTheme);
}

AddressesWidget::~AddressesWidget()
{
}
