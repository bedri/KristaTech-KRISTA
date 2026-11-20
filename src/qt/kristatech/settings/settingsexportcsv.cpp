// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsexportcsv.h"
#include "qt/kristatech/settings/forms/ui_settingsexportcsv.h"
#include "csvmodelwriter.h"
#include "guiinterface.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "qt/kristatech/qtutils.h"

#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QVBoxLayout>

SettingsExportCSV::SettingsExportCSV(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsExportCSV)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    // Populate combo boxes first, then apply modern Settings styling (initComboBox sets legacy cssClass)
    setSortTx(ui->comboBoxSort);
    setSortTxTypeFilter(ui->comboBoxSortType);
    ui->comboBoxSortType->setCurrentIndex(0);
    setFilterAddressBook(ui->comboBoxSortAddressType);
    ui->comboBoxSortAddressType->setCurrentIndex(0);

    auto configureCombo = [](QComboBox* cb) {
        clearLegacySettingsConstraints(cb);
        cb->setView(new QListView());
        cb->setMaxVisibleItems(10);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setFixedSize(230, 36);
        cb->setProperty("settingsInput", true);
    };
    configureCombo(ui->comboBoxSortType);
    configureCombo(ui->comboBoxSort);
    configureCombo(ui->comboBoxSortAddressType);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Export History (CSV)"),
        tr("Export your transaction history and address book to CSV spreadsheets for accounting or archiving."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Card 1: Transaction History
    QVBoxLayout* txBody = nullptr;
    QFrame* cardTx = createSettingsCard(modernRoot, tr("Transaction History"), txBody);

    addSettingsControlRow(
        cardTx,
        txBody,
        tr("Transaction Type"),
        tr("Filter which transaction categories are included in the CSV export."),
        ui->comboBoxSortType,
        false);

    addSettingsControlRow(
        cardTx,
        txBody,
        tr("Sort Order"),
        tr("Choose chronological or amount sorting for exported rows."),
        ui->comboBoxSort,
        true);

    clearLegacySettingsConstraints(ui->pushButtonDocuments);
    ui->pushButtonDocuments->setProperty("settingsPrimary", true);
    ui->pushButtonDocuments->setText(tr("Export Transactions CSV..."));
    ui->pushButtonDocuments->setMinimumSize(215, 36);
    ui->pushButtonDocuments->setMaximumHeight(36);
    ui->pushButtonDocuments->setCursor(Qt::PointingHandCursor);
    ui->pushButtonDocuments->setFocusPolicy(Qt::NoFocus);

    addSettingsControlRow(
        cardTx,
        txBody,
        tr("Export Transactions"),
        tr("Choose a destination CSV file to export filtered wallet transactions."),
        ui->pushButtonDocuments,
        true);

    txBody->addWidget(createSettingsDivider(cardTx));
    lblTxExportPath = new QLabel(tr("No transaction CSV exported during this session."), cardTx);
    lblTxExportPath->setProperty("settingsRowDesc", true);
    lblTxExportPath->setWordWrap(true);
    lblTxExportPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lblTxExportPath->setContentsMargins(0, 10, 0, 10);
    txBody->addWidget(lblTxExportPath);

    scrollLayout->addWidget(cardTx);

    // Card 2: Address Book
    QVBoxLayout* addrBody = nullptr;
    QFrame* cardAddr = createSettingsCard(modernRoot, tr("Address Book"), addrBody);

    addSettingsControlRow(
        cardAddr,
        addrBody,
        tr("Address Category"),
        tr("Filter between all entries, receiving addresses, or saved contacts."),
        ui->comboBoxSortAddressType,
        false);

    clearLegacySettingsConstraints(ui->pushButtonAddressDocuments);
    ui->pushButtonAddressDocuments->setProperty("settingsPrimary", true);
    ui->pushButtonAddressDocuments->setText(tr("Export Address Book CSV..."));
    ui->pushButtonAddressDocuments->setMinimumSize(215, 36);
    ui->pushButtonAddressDocuments->setMaximumHeight(36);
    ui->pushButtonAddressDocuments->setCursor(Qt::PointingHandCursor);
    ui->pushButtonAddressDocuments->setFocusPolicy(Qt::NoFocus);

    addSettingsControlRow(
        cardAddr,
        addrBody,
        tr("Export Address Book"),
        tr("Choose a destination CSV file to export saved labels and addresses."),
        ui->pushButtonAddressDocuments,
        true);

    addrBody->addWidget(createSettingsDivider(cardAddr));
    lblAddrExportPath = new QLabel(tr("No address book CSV exported during this session."), cardAddr);
    lblAddrExportPath->setProperty("settingsRowDesc", true);
    lblAddrExportPath->setWordWrap(true);
    lblAddrExportPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lblAddrExportPath->setContentsMargins(0, 10, 0, 10);
    addrBody->addWidget(lblAddrExportPath);

    scrollLayout->addWidget(cardAddr);
    scrollLayout->addStretch(1);

    connect(ui->pushButtonDocuments, &QPushButton::clicked, [this](){selectFileOutput(true);});
    connect(ui->pushButtonAddressDocuments, &QPushButton::clicked, [this](){selectFileOutput(false);});

    applyTheme(isLightTheme());
}

void SettingsExportCSV::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsExportCSV::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsExportCSV::selectFileOutput(const bool& isTxExport)
{
    QString filename = GUIUtil::getSaveFileName(this,
                                        isTxExport ? tr("Export CSV") : tr("Export Address List"), QString(),
                                        isTxExport ? tr("KristaTech_tx_csv_export(*.csv)") : tr("KristaTech_addresses_csv_export(*.csv)"),
                                        nullptr);

    if (isTxExport) {
        if (!filename.isEmpty()) {
            exportTxes(filename);
        }
    } else {
        if (!filename.isEmpty()) {
            exportAddrs(filename);
        }
    }
}

void SettingsExportCSV::exportTxes(const QString& filename)
{
    if (filename.isNull()) {
        inform(tr("Please select a folder to export the csv file first."));
        return;
    }

    CSVModelWriter writer(filename);
    bool fExport = false;

    if (walletModel) {

        if (!txFilter) {
            // Set up transaction list
            txFilter = new TransactionFilterProxy();
            txFilter->setDynamicSortFilter(true);
            txFilter->setSortCaseSensitivity(Qt::CaseInsensitive);
            txFilter->setFilterCaseSensitivity(Qt::CaseInsensitive);
            txFilter->setSortRole(Qt::EditRole);
            txFilter->setSourceModel(walletModel->getTransactionTableModel());
        }

        // First type filter
        txFilter->setTypeFilter(ui->comboBoxSortType->itemData(ui->comboBoxSortType->currentIndex()).toInt());

        // Second tx filter.
        int columnIndex = TransactionTableModel::Date;
        Qt::SortOrder order = Qt::DescendingOrder;
        switch (ui->comboBoxSort->itemData(ui->comboBoxSort->currentIndex()).toInt()) {
            case SortTx::DATE_ASC:{
                columnIndex = TransactionTableModel::Date;
                order = Qt::AscendingOrder;
                break;
            }
            case SortTx::DATE_DESC:{
                columnIndex = TransactionTableModel::Date;
                break;
            }
            case SortTx::AMOUNT_ASC:{
                columnIndex = TransactionTableModel::Amount;
                order = Qt::AscendingOrder;
                break;
            }
            case SortTx::AMOUNT_DESC:{
                columnIndex = TransactionTableModel::Amount;
                break;
            }

        }
        txFilter->sort(columnIndex, order);

        // name, column, role
        writer.setModel(txFilter);
        writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
        if (walletModel->haveWatchOnly())
            writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
        writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
        writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
        writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
        writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
        writer.addColumn(BitcoinUnits::getAmountColumnTitle(walletModel->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
        writer.addColumn(tr("ID"), 0, TransactionTableModel::TxIDRole);
        fExport = writer.write();
    }

    if (fExport) {
        if (lblTxExportPath) {
            lblTxExportPath->setText(tr("Last transaction export saved to: %1").arg(filename));
        }
        inform(tr("Exporting Successful\nThe transaction history was successfully saved to %1.").arg(filename));
    } else {
        inform(tr("Exporting Failed\nThere was an error trying to save the transaction history to %1.").arg(filename));
    }
}

void SettingsExportCSV::exportAddrs(const QString& filename)
{
    if (filename.isNull()) {
        inform(tr("Please select a folder to export the csv file first."));
        return;
    }

    bool fExport = false;
    if (walletModel) {

        if (!addressFilter) {
            addressFilter = new QSortFilterProxyModel(this);
            addressFilter->setSourceModel(walletModel->getAddressTableModel());
            addressFilter->setDynamicSortFilter(true);
            addressFilter->setFilterRole(AddressTableModel::TypeRole);
        }

        // Filter by type
        QString filterBy = ui->comboBoxSortAddressType->itemData(ui->comboBoxSortAddressType->currentIndex()).toString();
        addressFilter->setFilterFixedString(filterBy);

        if (addressFilter->rowCount() == 0) {
            inform(tr("No available addresses to export under the selected filter"));
            return;
        }

        CSVModelWriter writer(filename);
        // name, column, role
        writer.setModel(addressFilter);
        writer.addColumn("Label", AddressTableModel::Label, Qt::EditRole);
        writer.addColumn("Address", AddressTableModel::Address, Qt::EditRole);
        writer.addColumn("Type", AddressTableModel::Type, Qt::DisplayRole);
        fExport = writer.write();
    }

    if (fExport) {
        if (lblAddrExportPath) {
            lblAddrExportPath->setText(tr("Last address export saved to: %1").arg(filename));
        }
        inform(tr("Exporting Successful\nThe address book was successfully saved to %1.").arg(filename));
    } else {
        inform(tr("Exporting Failed\nThere was an error trying to save the address list to %1. Please try again.").arg(filename));
    }
}

SettingsExportCSV::~SettingsExportCSV()
{
    delete ui;
}
