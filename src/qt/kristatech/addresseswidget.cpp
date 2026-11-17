// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/addresseswidget.h"
#include "qt/kristatech/forms/ui_addresseswidget.h"
#include "qt/kristatech/addresslabelrow.h"
#include "qt/kristatech/addnewaddressdialog.h"
#include "qt/kristatech/tooltipmenu.h"

#include "qt/kristatech/addnewcontactdialog.h"
#include "qt/kristatech/kristatechgui.h"
#include "guiutil.h"
#include "qt/kristatech/qtutils.h"
#include "walletmodel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QModelIndex>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QRegExpValidator>
#include <QTableWidget>
#include <QTableWidgetItem>

#define DECORATION_SIZE 60
#define NUM_ITEMS 3

class ContactsHolder : public FurListRow<QWidget*>
{
public:
    ContactsHolder();

    explicit ContactsHolder(bool _isLightTheme) : FurListRow(), isLightTheme(_isLightTheme){}

    AddressLabelRow* createHolder(int pos) override{
        if (!cachedRow) cachedRow = new AddressLabelRow();
        cachedRow->init(isLightTheme, false);
        return cachedRow;
    }

    void init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const override
    {
        AddressLabelRow* row = static_cast<AddressLabelRow*>(holder);

        row->updateState(isLightTheme, isHovered, isSelected);

        QString address = index.data(Qt::DisplayRole).toString();
        QModelIndex sibling = index.sibling(index.row(), AddressTableModel::Label);
        QString label = sibling.data(Qt::DisplayRole).toString();

        row->updateView(address, label);
    }

    QColor rectColor(bool isHovered, bool isSelected) override
    {
        return getRowColor(isLightTheme, isHovered, isSelected);
    }

    ~ContactsHolder() override{}

    bool isLightTheme;
    AddressLabelRow* cachedRow = nullptr;
};


AddressesWidget::AddressesWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::AddressesWidget)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    delegate = new FurAbstractListItemDelegate(
                DECORATION_SIZE,
                new ContactsHolder(isLightTheme()),
                this
    );

    /* Containers */
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0,20,0,20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20,10,20,20);
    setCssProperty(ui->listAddresses, "container");

    // Title
    setCssTitleScreen(ui->labelTitle);
    setCssSubtitleScreen(ui->labelSubtitle1);
    ui->labelSubtitle1->setText(tr("Use Copy, Edit, or Delete directly on any contact row, or double-click a row to copy its address."));

    // Change address option
    ui->btnAddContact->setTitleClassAndText("btn-title-grey", tr("Add new contact"));
    ui->btnAddContact->setSubTitleClassAndText("text-subtitle", tr("Save a new contact address."));
    ui->btnAddContact->setRightIconClass("ic-arrow-down");

    // Search Contacts
    lineEditSearchContact = new QLineEdit(ui->left);
    lineEditSearchContact->setPlaceholderText(tr("Search contacts by label or address..."));
    lineEditSearchContact->setClearButtonEnabled(true);
    lineEditSearchContact->setFixedHeight(34);
    setCssEditLine(lineEditSearchContact, true);
    styleLineEditClearButton(lineEditSearchContact);
    ui->verticalLayout_6->setContentsMargins(20, 10, 20, 0);
    ui->verticalLayout_6->addWidget(lineEditSearchContact);
    connect(lineEditSearchContact, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (filter) {
            filter->setFilterFixedString(text.trimmed());
            updateListView();
        }
    });

    // List Addresses
    ui->listAddresses->setItemDelegate(delegate);
    ui->listAddresses->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listAddresses->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listAddresses->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listAddresses->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->listAddresses->setUniformItemSizes(true);

    // Sort Controls
    connect(ui->comboBoxSort, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &AddressesWidget::onSortChanged);
    connect(ui->comboBoxSortOrder, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, &AddressesWidget::onSortOrderChanged);
    fillAddressSortControls(ui->comboBoxSort, ui->comboBoxSortOrder);

    //Empty List
    ui->emptyContainer->setVisible(false);
    setCssProperty(ui->pushImgEmpty, "img-empty-contacts");
    setCssProperty(ui->labelEmpty, "text-empty");

    // Add Contact
    setCssProperty(ui->layoutNewContact, "container-options");

    // Name
    setCssProperty(ui->labelName, "text-title");
    setCssEditLine(ui->lineEditName, true);
    ui->lineEditName->setClearButtonEnabled(true);
    styleLineEditClearButton(ui->lineEditName);

    // Address
    setCssProperty(ui->labelAddress, "text-title");
    setCssEditLine(ui->lineEditAddress, true);
    ui->lineEditAddress->setClearButtonEnabled(true);
    styleLineEditClearButton(ui->lineEditAddress);
    ui->lineEditAddress->setValidator(new QRegExpValidator(QRegExp("^[A-Za-z0-9]+"), ui->lineEditAddress));

    // Buttons
    setCssBtnPrimary(ui->btnSave);

    ui->listAddresses->setVisible(false);

    // Direct interactive Contacts table (no hidden menu or bottom bar needed)
    tableContacts = new QTableWidget(0, 3, ui->left);
    tableContacts->setObjectName("tableContacts");
    tableContacts->setHorizontalHeaderLabels({tr("Contact"), tr("Address"), tr("Actions")});
    tableContacts->verticalHeader()->setVisible(false);
    tableContacts->setSelectionMode(QAbstractItemView::NoSelection);
    tableContacts->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableContacts->setShowGrid(false);
    tableContacts->setFocusPolicy(Qt::NoFocus);
    tableContacts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    tableContacts->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableContacts->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    tableContacts->setColumnWidth(2, 224);
    tableContacts->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableContacts, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (!filter || row < 0 || row >= filter->rowCount()) return;
        this->index = filter->mapToSource(filter->index(row, AddressTableModel::Address));
        onCopyClicked();
    });
    connect(tableContacts, &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        int row = tableContacts->rowAt(pt.y());
        if (!filter || row < 0 || row >= filter->rowCount()) return;
        this->index = filter->mapToSource(filter->index(row, AddressTableModel::Address));
        QMenu ctxMenu(this);
        QAction* actCopy = ctxMenu.addAction(tr("Copy Address"));
        QAction* actEdit = ctxMenu.addAction(tr("Edit Contact"));
        QAction* actDelete = ctxMenu.addAction(tr("Delete Contact"));
        QAction* chosen = ctxMenu.exec(tableContacts->viewport()->mapToGlobal(pt));
        if (chosen == actCopy) onCopyClicked();
        else if (chosen == actEdit) onEditClicked();
        else if (chosen == actDelete) onDeleteClicked();
    });
    if (QVBoxLayout* leftLayout = qobject_cast<QVBoxLayout*>(ui->left->layout())) {
        int listIdx = leftLayout->indexOf(ui->listAddresses);
        leftLayout->insertWidget(listIdx >= 0 ? listIdx : leftLayout->count(), tableContacts, 1);
    }
    updateContactsTableStyle();

    connect(ui->btnSave, &QPushButton::clicked, this, &AddressesWidget::onStoreContactClicked);
    connect(ui->btnAddContact, &OptionButton::clicked, this, &AddressesWidget::onAddContactShowHideClicked);
}

void AddressesWidget::handleAddressClicked(const QModelIndex &index)
{
    if (!filter) return;
    this->index = filter->mapToSource(index);
}

void AddressesWidget::loadWalletModel()
{
    if (walletModel) {
        addressTablemodel = walletModel->getAddressTableModel();
        this->filter = new AddressFilterProxyModel(QStringList({AddressTableModel::Send}), this);
        this->filter->setSourceModel(addressTablemodel);
        this->filter->sort(sortType, sortOrder);
        ui->listAddresses->setModel(this->filter);
        ui->listAddresses->setModelColumn(AddressTableModel::Address);

        connect(this->filter, &QAbstractItemModel::rowsInserted, this, &AddressesWidget::updateListView);
        connect(this->filter, &QAbstractItemModel::rowsRemoved, this, &AddressesWidget::updateListView);
        connect(this->filter, &QAbstractItemModel::modelReset, this, &AddressesWidget::updateListView);
        connect(this->filter, &QAbstractItemModel::dataChanged, this, [this]() { updateListView(); });
        connect(this->filter, &QAbstractItemModel::layoutChanged, this, [this]() { updateListView(); });

        updateListView();
    }
}

void AddressesWidget::updateListView()
{
    bool empty = filter ? (filter->rowCount() == 0) : (addressTablemodel->sizeSend() == 0);
    ui->emptyContainer->setVisible(empty);
    ui->listAddresses->setVisible(false);
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
    tableContacts->setRowCount(nRows);
    for (int r = 0; r < nRows; ++r) {
        QModelIndex addrProxyIdx = filter->index(r, AddressTableModel::Address);
        QModelIndex labelProxyIdx = filter->index(r, AddressTableModel::Label);
        QString addr = addrProxyIdx.data(Qt::DisplayRole).toString();
        QString label = labelProxyIdx.data(Qt::DisplayRole).toString();
        QPersistentModelIndex sourceIdx(filter->mapToSource(addrProxyIdx));

        QTableWidgetItem* nameItem = new QTableWidgetItem(label.isEmpty() ? tr("(No Label)") : label);
        QFont nameFont = nameItem->font();
        nameFont.setWeight(QFont::DemiBold);
        nameItem->setFont(nameFont);
        tableContacts->setItem(r, 0, nameItem);

        QTableWidgetItem* addrItem = new QTableWidgetItem(addr);
        tableContacts->setItem(r, 1, addrItem);

        QWidget* actionsCell = new QWidget(tableContacts);
        QHBoxLayout* actionsLayout = new QHBoxLayout(actionsCell);
        actionsLayout->setContentsMargins(6, 4, 6, 4);
        actionsLayout->setSpacing(6);

        QPushButton* btnCopy = new QPushButton(tr("Copy"), actionsCell);
        btnCopy->setObjectName("btnRowCopy");
        btnCopy->setCursor(Qt::PointingHandCursor);
        connect(btnCopy, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onCopyClicked();
        });

        QPushButton* btnEdit = new QPushButton(tr("Edit"), actionsCell);
        btnEdit->setObjectName("btnRowEdit");
        btnEdit->setCursor(Qt::PointingHandCursor);
        connect(btnEdit, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onEditClicked();
        });

        QPushButton* btnDelete = new QPushButton(tr("Delete"), actionsCell);
        btnDelete->setObjectName("btnRowDelete");
        btnDelete->setCursor(Qt::PointingHandCursor);
        connect(btnDelete, &QPushButton::clicked, this, [this, sourceIdx]() {
            if (!sourceIdx.isValid()) return;
            this->index = sourceIdx;
            onDeleteClicked();
        });

        actionsLayout->addWidget(btnCopy);
        actionsLayout->addWidget(btnEdit);
        actionsLayout->addWidget(btnDelete);
        tableContacts->setCellWidget(r, 2, actionsCell);
        tableContacts->setRowHeight(r, 48);
    }
}

void AddressesWidget::onStoreContactClicked()
{
    if (walletModel) {
        QString label = ui->lineEditName->text();
        QString address = ui->lineEditAddress->text();

        if (!walletModel->validateAddress(address)) {
            setCssEditLine(ui->lineEditAddress, false, true);
            inform(tr("Invalid Contact Address"));
            return;
        }

        CTxDestination pivAdd = DecodeDestination(address.toUtf8().constData());
        if (walletModel->isMine(pivAdd)) {
            setCssEditLine(ui->lineEditAddress, false, true);
            inform(tr("Cannot store your own address as contact"));
            return;
        }

        QString storedLabel = walletModel->getAddressTableModel()->labelForAddress(address);

        if (!storedLabel.isEmpty()) {
            inform(tr("Address already stored, label: %1").arg("\'"+storedLabel+"\'"));
            return;
        }

        if (walletModel->updateAddressBookLabels(
            pivAdd, label.toUtf8().constData(),AddressBook::AddressBookPurpose::SEND
        )) {
            ui->lineEditAddress->setText("");
            ui->lineEditName->setText("");
            setCssEditLine(ui->lineEditAddress, true, true);
            setCssEditLine(ui->lineEditName, true, true);

            if (ui->emptyContainer->isVisible()) {
                ui->emptyContainer->setVisible(false);
            }
            updateListView();
            inform(tr("New Contact Stored"));
        } else {
            inform(tr("Error Storing Contact"));
        }
    }
}

void AddressesWidget::onEditClicked()
{
    QString address = index.data(Qt::DisplayRole).toString();
    QString currentLabel = index.sibling(index.row(), AddressTableModel::Label).data(Qt::DisplayRole).toString();
    showHideOp(true);
    AddNewContactDialog *dialog = new AddNewContactDialog(window);
    dialog->setData(address, currentLabel);
    if (openDialogWithOpaqueBackground(dialog, window)) {
        if (walletModel->updateAddressBookLabels(
                DecodeDestination(address.toStdString()), dialog->getLabel().toStdString(), addressTablemodel->purposeForAddress(address.toStdString()))){
            inform(tr("Contact edited"));
        } else {
            inform(tr("Contact edit failed"));
        }
    }
    dialog->deleteLater();
}

void AddressesWidget::onDeleteClicked()
{
    if (walletModel) {
        if (ask(tr("Delete Contact"), tr("You are just about to remove the contact:\n\n%1\n\nAre you sure?").arg(index.data(Qt::DisplayRole).toString().toUtf8().constData()))
        ) {
            if (this->walletModel->getAddressTableModel()->removeRows(index.row(), 1, index)) {
                updateListView();
                inform(tr("Contact Deleted"));
            } else {
                inform(tr("Error deleting a contact"));
            }
        }
    }
}

void AddressesWidget::onCopyClicked()
{
    GUIUtil::setClipboard(index.data(Qt::DisplayRole).toString());
    inform(tr("Address copied"));
}

void AddressesWidget::onAddContactShowHideClicked()
{
    if (!ui->layoutNewContact->isVisible()){
        ui->btnAddContact->setRightIconClass("btn-dropdown", true);
        ui->layoutNewContact->setVisible(true);
    } else {
        ui->btnAddContact->setRightIconClass("ic-arrow", true);
        ui->layoutNewContact->setVisible(false);
    }
}

void AddressesWidget::onSortChanged(int idx)
{
    sortType = (AddressTableModel::ColumnIndex) ui->comboBoxSort->itemData(idx).toInt();
    sortAddresses();
}

void AddressesWidget::onSortOrderChanged(int idx)
{
    sortOrder = (Qt::SortOrder) ui->comboBoxSortOrder->itemData(idx).toInt();
    sortAddresses();
}

void AddressesWidget::sortAddresses()
{
    if (this->filter)
        this->filter->sort(sortType, sortOrder);
}

void AddressesWidget::updateContactsTableStyle()
{
    if (!tableContacts) return;
    const bool light = isLightTheme();
    const QString bg = light ? "#FFFFFF" : "#131B2E";
    const QString altBg = light ? "#F8FAFC" : "#182238";
    const QString border = light ? "#E2E8F0" : "#26334D";
    const QString btnBorder = light ? "#CBD5E1" : "#334155";
    const QString text = light ? "#1E293B" : "#F1F5F9";
    const QString muted = light ? "#64748B" : "#94A3B8";
    const QString hoverBg = light ? "#F8FAFC" : "#1C2434";
    const QString btnBg = light ? "#FFFFFF" : "#1C2434";
    const QString copyHoverBg = light ? "rgba(85, 118, 178, 0.08)" : "rgba(118, 149, 205, 0.16)";
    tableContacts->setStyleSheet(QString(
        "QTableWidget#tableContacts { background-color: %1; border: none; color: %2; font-size: 13px; margin: 6px 20px 10px 20px; selection-background-color: transparent; }"
        "QTableWidget#tableContacts::item { padding: 8px 10px; border-bottom: 1px solid %3; }"
        "QTableWidget#tableContacts::item:hover { background-color: %4; color: %2; }"
        "QHeaderView::section { background-color: %5; color: %6; border: none; border-bottom: 1px solid %3; padding: 8px 10px; font-size: 12px; font-weight: 600; text-align: left; }"
        "QPushButton#btnRowCopy, QPushButton#btnRowEdit { background-color: %7; color: %2; border: 1px solid %8; border-radius: 5px; padding: 4px 10px; font-size: 12px; font-weight: 500; }"
        "QPushButton#btnRowCopy:hover, QPushButton#btnRowEdit:hover { border-color: #5576B2; color: #5576B2; background-color: %9; }"
        "QPushButton#btnRowDelete { background-color: %7; color: #D96558; border: 1px solid rgba(229, 122, 109, 0.55); border-radius: 5px; padding: 4px 10px; font-size: 12px; font-weight: 500; }"
        "QPushButton#btnRowDelete:hover { border-color: #D96558; background-color: rgba(229, 122, 109, 0.12); }"
    ).arg(bg, text, border, hoverBg, altBg, muted, btnBg, btnBorder, copyHoverBg));
}

void AddressesWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<ContactsHolder*>(this->delegate->getRowFactory())->isLightTheme = isLightTheme;
    if (lineEditSearchContact) styleLineEditClearButton(lineEditSearchContact);
    styleLineEditClearButton(ui->lineEditName);
    styleLineEditClearButton(ui->lineEditAddress);
    updateContactsTableStyle();
}

AddressesWidget::~AddressesWidget()
{
    delete ui;
}
