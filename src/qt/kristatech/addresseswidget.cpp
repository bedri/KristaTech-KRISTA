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

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QModelIndex>
#include <QPushButton>
#include <QRegExpValidator>

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

    connect(ui->listAddresses, &QListView::clicked, this, &AddressesWidget::handleAddressClicked);
    connect(ui->listAddresses, &QListView::doubleClicked, this, [this](const QModelIndex& idx) {
        handleAddressClicked(idx);
        onCopyClicked();
    });
    ui->listAddresses->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listAddresses, &QWidget::customContextMenuRequested, this, [this](const QPoint& pt) {
        QModelIndex idx = ui->listAddresses->indexAt(pt);
        if (!idx.isValid()) return;
        handleAddressClicked(idx);
        QMenu ctxMenu(this);
        QAction* actCopy = ctxMenu.addAction(tr("Copy Address"));
        QAction* actEdit = ctxMenu.addAction(tr("Edit Contact"));
        QAction* actDelete = ctxMenu.addAction(tr("Delete Contact"));
        QAction* chosen = ctxMenu.exec(ui->listAddresses->viewport()->mapToGlobal(pt));
        if (chosen == actCopy) onCopyClicked();
        else if (chosen == actEdit) onEditClicked();
        else if (chosen == actDelete) onDeleteClicked();
    });

    // Selected Contact Action Bar (replaces misplaced popup menu)
    contactActionBar = new QFrame(ui->left);
    contactActionBar->setObjectName("contactActionBar");
    QHBoxLayout* actionBarLayout = new QHBoxLayout(contactActionBar);
    actionBarLayout->setContentsMargins(14, 10, 14, 10);
    actionBarLayout->setSpacing(8);

    lblSelectedContact = new QLabel(contactActionBar);
    lblSelectedContact->setObjectName("lblSelectedContact");
    lblSelectedContact->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    actionBarLayout->addWidget(lblSelectedContact);

    btnCopyContact = new QPushButton(tr("Copy Address"), contactActionBar);
    btnCopyContact->setObjectName("btnCopyContact");
    btnCopyContact->setCursor(Qt::PointingHandCursor);
    connect(btnCopyContact, &QPushButton::clicked, this, &AddressesWidget::onCopyClicked);
    actionBarLayout->addWidget(btnCopyContact);

    btnEditContact = new QPushButton(tr("Edit"), contactActionBar);
    btnEditContact->setObjectName("btnEditContact");
    btnEditContact->setCursor(Qt::PointingHandCursor);
    connect(btnEditContact, &QPushButton::clicked, this, &AddressesWidget::onEditClicked);
    actionBarLayout->addWidget(btnEditContact);

    btnDeleteContact = new QPushButton(tr("Delete"), contactActionBar);
    btnDeleteContact->setObjectName("btnDeleteContact");
    btnDeleteContact->setCursor(Qt::PointingHandCursor);
    connect(btnDeleteContact, &QPushButton::clicked, this, &AddressesWidget::onDeleteClicked);
    actionBarLayout->addWidget(btnDeleteContact);

    if (ui->left && ui->left->layout()) {
        ui->left->layout()->addWidget(contactActionBar);
    }
    contactActionBar->setVisible(false);
    updateContactActionBarStyle();

    connect(ui->btnSave, &QPushButton::clicked, this, &AddressesWidget::onStoreContactClicked);
    connect(ui->btnAddContact, &OptionButton::clicked, this, &AddressesWidget::onAddContactShowHideClicked);
}

void AddressesWidget::handleAddressClicked(const QModelIndex &index)
{
    ui->listAddresses->setCurrentIndex(index);
    QModelIndex rIndex = filter->mapToSource(index);
    this->index = rIndex;

    if (this->menu) {
        this->menu->hide();
    }

    if (contactActionBar && lblSelectedContact) {
        const QString addr = rIndex.data(Qt::DisplayRole).toString();
        const QString label = addressTablemodel ? addressTablemodel->labelForAddress(addr) : QString();
        const QString shortAddr = (addr.length() > 20) ? (addr.left(10) + "..." + addr.right(8)) : addr;
        lblSelectedContact->setText(label.isEmpty()
            ? shortAddr
            : QString("%1  ·  %2").arg(label, shortAddr));
        contactActionBar->setVisible(true);
    }
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

        updateListView();
    }
}

void AddressesWidget::updateListView()
{
    bool empty = filter ? (filter->rowCount() == 0) : (addressTablemodel->sizeSend() == 0);
    ui->emptyContainer->setVisible(empty);
    ui->listAddresses->setVisible(!empty);
    if (contactActionBar && (empty || !ui->listAddresses->currentIndex().isValid())) {
        contactActionBar->setVisible(false);
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
                ui->listAddresses->setVisible(true);
            }
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

void AddressesWidget::updateContactActionBarStyle()
{
    if (!contactActionBar) return;
    const bool light = isLightTheme();
    const QString barBg = light ? "#F8FAFC" : "#1C2434";
    const QString barBorder = light ? "#CBD5E1" : "#334155";
    const QString textCol = light ? "#1E293B" : "#F1F5F9";
    const QString secBg = light ? "#FFFFFF" : "#243044";
    contactActionBar->setStyleSheet(QString(
        "QFrame#contactActionBar { background-color: %1; border: 1px solid %2; border-radius: 8px; margin: 8px 20px 12px 20px; }"
        "QLabel#lblSelectedContact { color: %3; font-size: 12px; font-weight: 600; background: transparent; border: none; }"
        "QPushButton#btnCopyContact { background-color: #5576B2; color: #FFFFFF; border: none; border-radius: 6px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton#btnCopyContact:hover { background-color: #46649D; }"
        "QPushButton#btnEditContact { background-color: %4; color: %3; border: 1px solid %2; border-radius: 6px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton#btnEditContact:hover { border-color: #5576B2; }"
        "QPushButton#btnDeleteContact { background-color: transparent; color: #E57A6D; border: 1px solid #E57A6D; border-radius: 6px; padding: 6px 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton#btnDeleteContact:hover { background-color: rgba(229, 122, 109, 0.14); }"
    ).arg(barBg, barBorder, textCol, secBg));
}

void AddressesWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<ContactsHolder*>(this->delegate->getRowFactory())->isLightTheme = isLightTheme;
    if (lineEditSearchContact) styleLineEditClearButton(lineEditSearchContact);
    styleLineEditClearButton(ui->lineEditName);
    styleLineEditClearButton(ui->lineEditAddress);
    updateContactActionBarStyle();
}

AddressesWidget::~AddressesWidget()
{
    delete ui;
}
