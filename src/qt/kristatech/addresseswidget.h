// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ADDRESSESWIDGET_H
#define ADDRESSESWIDGET_H

#include "addresstablemodel.h"
#include "qt/kristatech/addressfilterproxymodel.h"
#include "qt/kristatech/pwidget.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>

class KRISTATECHGUI;

class AddressesWidget : public PWidget
{
    Q_OBJECT

public:
    explicit AddressesWidget(KRISTATECHGUI* parent);
    ~AddressesWidget();

    void loadWalletModel() override;
    void onNewContactClicked();

protected:
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void onStoreContactClicked();
    void onPasteAddressClicked();
    void onClearFormClicked();
    void onEditClicked();
    void onDeleteClicked();
    void onCopyClicked();
    void onSortChanged(int idx);
    void onSortOrderChanged(int idx);
    void changeTheme(bool isLightTheme, QString& theme) override;

private:
    // Left Column: Header + Searchable Contacts Table Card
    QLabel* lblTitle = nullptr;
    QLabel* lblSubtitle = nullptr;
    QFrame* listCard = nullptr;
    QLabel* lblListTitle = nullptr;
    QLabel* lblContactCount = nullptr;
    QComboBox* comboBoxSort = nullptr;
    QComboBox* comboBoxSortOrder = nullptr;
    QLineEdit* lineEditSearchContact = nullptr;
    QTableWidget* tableContacts = nullptr;
    QLabel* lblEmpty = nullptr;

    // Right Column: Fixed Add New Contact Card
    QFrame* addCard = nullptr;
    QLabel* lblAddTitle = nullptr;
    QLabel* lblAddSub = nullptr;
    QLabel* lblAddrStatus = nullptr;
    QLabel* lblNameKey = nullptr;
    QLineEdit* lineEditName = nullptr;
    QLabel* lblAddressKey = nullptr;
    QLineEdit* lineEditAddress = nullptr;
    QPushButton* btnPasteAddr = nullptr;
    QPushButton* btnClearForm = nullptr;
    QPushButton* btnSave = nullptr;

    AddressTableModel* addressTablemodel = nullptr;
    AddressFilterProxyModel* filter = nullptr;
    QPersistentModelIndex index;

    AddressTableModel::ColumnIndex sortType = AddressTableModel::Label;
    Qt::SortOrder sortOrder = Qt::AscendingOrder;

    QTimer* rebuildTimer = nullptr;
    bool tableDirty = true;
    bool addrInvalid = false;

    void applyTheme(bool isLight);
    void refreshInputStyles();
    void updateAddressValidationUi(const QString& rawText);
    void scheduleTableRebuild();
    void flushTableRebuild();
    void updateListView();
    void rebuildContactsTable();
    void sortAddresses();
    void sendToContact(const QString& addr, const QString& label);
};

#endif // ADDRESSESWIDGET_H
