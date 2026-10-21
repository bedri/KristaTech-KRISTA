// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coincontroldialog.h"
#include "ui_coincontroldialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "guiutil.h"
#include "init.h"
#include "optionsmodel.h"
#include "walletmodel.h"

#include "coincontrol.h"
#include "main.h"
#include "wallet/wallet.h"

#include "qt/kristatech/qtutils.h"

#include <boost/assign/list_of.hpp> // for 'map_list_of()'

#include <QApplication>
#include <QCheckBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFlags>
#include <QIcon>
#include <QScrollBar>
#include <QSettings>
#include <QString>
#include <QTreeWidget>
#include <algorithm>
#include <set>

namespace {
constexpr int ROLE_ITEM_TYPE = Qt::UserRole + 1;
constexpr int ROLE_INDEX = Qt::UserRole + 2;

constexpr int ITEM_TYPE_OUTPUT = 0;
constexpr int ITEM_TYPE_GROUP = 1;
constexpr int ITEM_TYPE_LOAD_MORE = 2;

constexpr size_t COIN_CONTROL_BATCH_SIZE = 1000;
} // namespace


bool CCoinControlWidgetItem::operator<(const QTreeWidgetItem &other) const {
    const int thisType = data(CoinControlDialog::COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt();
    const int otherType = other.data(CoinControlDialog::COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt();
    if (thisType == ITEM_TYPE_LOAD_MORE || otherType == ITEM_TYPE_LOAD_MORE) {
        const bool asc = (treeWidget() && treeWidget()->header()->sortIndicatorOrder() == Qt::AscendingOrder);
        if (thisType == ITEM_TYPE_LOAD_MORE && otherType == ITEM_TYPE_LOAD_MORE) return false;
        if (thisType == ITEM_TYPE_LOAD_MORE) return !asc;
        return asc;
    }
    int column = treeWidget()->sortColumn();
    if (column == CoinControlDialog::COLUMN_AMOUNT || column == CoinControlDialog::COLUMN_DATE || column == CoinControlDialog::COLUMN_CONFIRMATIONS)
        return data(column, Qt::UserRole).toLongLong() < other.data(column, Qt::UserRole).toLongLong();
    return QTreeWidgetItem::operator<(other);
}


CoinControlDialog::CoinControlDialog(QWidget* parent) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                        ui(new Ui::CoinControlDialog),
                                                        model(0)
{
    coinControl = new CCoinControl();
    ui->setupUi(this);

    /* Open CSS when configured */
    this->setStyleSheet(GUIUtil::loadStyleSheet());
    setCssProperty({ui->frameContainer,
                    ui->layoutAmount,
                    ui->layoutAfter,
                    ui->layoutBytes,
                    ui->layoutChange,
                    ui->layoutDust,
                    ui->layoutFee,
                    ui->layoutQuantity
                    }, "container-border-purple");

    // Title
    ui->labelTitle->setProperty("cssClass", "text-title-dialog");

    // Label Style
    setCssProperty({ui->labelCoinControlAfterFeeText,
                    ui->labelCoinControlAmountText,
                    ui->labelCoinControlBytesText,
                    ui->labelCoinControlChangeText,
                    ui->labelCoinControlLowOutputText,
                    ui->labelCoinControlFeeText,
                    ui->labelCoinControlQuantityText
                    }, "text-main-purple");

    // Value Style
    setCssProperty({ui->labelCoinControlAfterFee,
                    ui->labelCoinControlAmount,
                    ui->labelCoinControlBytes,
                    ui->labelCoinControlChange,
                    ui->labelCoinControlLowOutput,
                    ui->labelCoinControlFee,
                    ui->labelCoinControlQuantity
                    }, "text-main-purple");

    ui->groupBox_2->setProperty("cssClass", "group-box");
    ui->treeWidget->setProperty("cssClass", "table-tree");
    ui->labelLocked->setProperty("cssClass", "text-main-purple");

    // Buttons
    setCssProperty({ui->pushButtonSelectAll, ui->pushButtonToggleLock}, "btn-check");
    ui->pushButtonOk->setProperty("cssClass", "btn-primary");

    // context menu actions
    QAction* copyAddressAction = new QAction(tr("Copy address"), this);
    QAction* copyLabelAction = new QAction(tr("Copy label"), this);
    QAction* copyAmountAction = new QAction(tr("Copy amount"), this);
    copyTransactionHashAction = new QAction(tr("Copy transaction ID"), this); // we need to enable/disable this
    lockAction = new QAction(tr("Lock unspent"), this);                       // we need to enable/disable this
    unlockAction = new QAction(tr("Unlock unspent"), this);                   // we need to enable/disable this

    // context menu
    contextMenu = new QMenu();
    contextMenu->addAction(copyAddressAction);
    contextMenu->addAction(copyLabelAction);
    contextMenu->addAction(copyAmountAction);
    contextMenu->addAction(copyTransactionHashAction);
    contextMenu->addSeparator();
    contextMenu->addAction(lockAction);
    contextMenu->addAction(unlockAction);

    // context menu signals
    connect(ui->treeWidget, &QWidget::customContextMenuRequested, this, &CoinControlDialog::showMenu);
    connect(copyAddressAction, &QAction::triggered, this, &CoinControlDialog::copyAddress);
    connect(copyLabelAction, &QAction::triggered, this, &CoinControlDialog::copyLabel);
    connect(copyAmountAction, &QAction::triggered, this, &CoinControlDialog::copyAmount);
    connect(copyTransactionHashAction, &QAction::triggered, this, &CoinControlDialog::copyTransactionHash);
    connect(lockAction, &QAction::triggered, this, &CoinControlDialog::lockCoin);
    connect(unlockAction, &QAction::triggered, this, &CoinControlDialog::unlockCoin);

    // clipboard actions
    setCssProperty({
        ui->pushButtonAmount,
        ui->pushButtonQuantity,
        ui->pushButtonFee,
        ui->pushButtonAlterFee,
        ui->pushButtonBytes,
        ui->pushButtonChange,
        ui->pushButtonDust
        }, "ic-copy-big"
    );

    connect(ui->pushButtonQuantity, &QPushButton::clicked, this, &CoinControlDialog::clipboardQuantity);
    connect(ui->pushButtonAmount, &QPushButton::clicked, this, &CoinControlDialog::clipboardAmount);
    connect(ui->pushButtonFee, &QPushButton::clicked, this, &CoinControlDialog::clipboardFee);
    connect(ui->pushButtonAlterFee, &QPushButton::clicked, this, &CoinControlDialog::clipboardAfterFee);
    connect(ui->pushButtonBytes, &QPushButton::clicked, this, &CoinControlDialog::clipboardBytes);
    connect(ui->pushButtonDust, &QPushButton::clicked, this, &CoinControlDialog::clipboardLowOutput);
    connect(ui->pushButtonChange, &QPushButton::clicked, this, &CoinControlDialog::clipboardChange);

    if (ui->pushButtonSelectAll->isChecked()) {
        ui->pushButtonSelectAll->setText(tr("Unselect all"));
    } else {
        ui->pushButtonSelectAll->setText(tr("Select all"));
    }

    // toggle tree/list mode
    connect(ui->radioTreeMode, &QRadioButton::toggled, this, &CoinControlDialog::radioTreeMode);
    connect(ui->radioListMode, &QRadioButton::toggled, this, &CoinControlDialog::radioListMode);

    // click on checkbox
    connect(ui->treeWidget, &QTreeWidget::itemChanged, this, &CoinControlDialog::viewItemChanged);
    connect(ui->treeWidget, &QTreeWidget::itemExpanded, this, &CoinControlDialog::onTreeItemExpanded);
    connect(ui->treeWidget, &QTreeWidget::itemClicked, this, &CoinControlDialog::onTreeItemClicked);
    connect(ui->treeWidget->verticalScrollBar(), &QScrollBar::valueChanged, this, &CoinControlDialog::onScrollBarValueChanged);

    // click on header
    ui->treeWidget->header()->setSectionsClickable(true);
    connect(ui->treeWidget->header(), &QHeaderView::sectionClicked, this, &CoinControlDialog::headerSectionClicked);

    // ok button
    connect(ui->pushButtonOk, &QPushButton::clicked, this, &CoinControlDialog::accept);

    // (un)select all
    connect(ui->pushButtonSelectAll, &QPushButton::clicked, this, &CoinControlDialog::buttonSelectAllClicked);

    // Toggle lock state
    connect(ui->pushButtonToggleLock, &QPushButton::clicked, this, &CoinControlDialog::buttonToggleLockClicked);

    // change coin control first column label due Qt4 bug.
    // see https://github.com/bitcoin/bitcoin/issues/5716
    ui->treeWidget->headerItem()->setText(COLUMN_CHECKBOX, QString());

    ui->treeWidget->setUniformRowHeights(true);
    ui->treeWidget->setColumnWidth(COLUMN_CHECKBOX, 84);
    ui->treeWidget->setColumnWidth(COLUMN_AMOUNT, 120);
    ui->treeWidget->setColumnWidth(COLUMN_LABEL, 170);
    ui->treeWidget->setColumnWidth(COLUMN_ADDRESS, 310);
    ui->treeWidget->setColumnWidth(COLUMN_DATE, 100);
    ui->treeWidget->setColumnWidth(COLUMN_CONFIRMATIONS, 100);
    ui->treeWidget->setColumnHidden(COLUMN_TXHASH, true);         // store transacton hash in this column, but dont show it
    ui->treeWidget->setColumnHidden(COLUMN_VOUT_INDEX, true);     // store vout index in this column, but dont show it

    ui->treeWidget->header()->setStretchLastSection(true);
    // default view is sorted by amount desc
    sortColumn = COLUMN_AMOUNT;
    sortOrder = Qt::DescendingOrder;

    // restore list mode and sortorder as a convenience feature
    QSettings settings;
    if (settings.contains("nCoinControlMode") && !settings.value("nCoinControlMode").toBool()) {
        ui->radioTreeMode->setChecked(true);
        ui->treeWidget->setRootIsDecorated(true);
    } else {
        ui->radioListMode->setChecked(true);
        ui->treeWidget->setRootIsDecorated(false);
    }
    if (settings.contains("nCoinControlSortColumn") && settings.contains("nCoinControlSortOrder")) {
        sortColumn = settings.value("nCoinControlSortColumn").toInt();
        sortOrder = (Qt::SortOrder)settings.value("nCoinControlSortOrder").toInt();
    }
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
}

CoinControlDialog::~CoinControlDialog()
{
    QSettings settings;
    settings.setValue("nCoinControlMode", ui->radioListMode->isChecked());
    settings.setValue("nCoinControlSortColumn", sortColumn);
    settings.setValue("nCoinControlSortOrder", (int)sortOrder);

    delete ui;
    delete coinControl;
}

void CoinControlDialog::setModel(WalletModel* model)
{
    this->model = model;

    if (model && model->getOptionsModel() && model->getAddressTableModel()) {
        updateLabelLocked();
        updateLabels();
        updateDialogLabels();
    }
}

// (un)select all
void CoinControlDialog::buttonSelectAllClicked()
{
    const bool fSelectAll = ui->pushButtonSelectAll->isChecked();
    coinControl->UnSelectAll();
    if (fSelectAll) {
        for (const OutputEntry& entry : m_allEntries) {
            if (!entry.fLocked) {
                coinControl->Select(entry.outpoint);
            }
        }
    }
    syncVisibleCheckStates();
    updateLabels();
    updateDialogLabels();
}

// Toggle lock state
void CoinControlDialog::buttonToggleLockClicked()
{
    // Works in list-mode only
    if (ui->radioListMode->isChecked()) {
        ui->treeWidget->setEnabled(false);
        m_bInternalUpdate = true;
        for (OutputEntry& entry : m_allEntries) {
            COutPoint outpt = entry.outpoint;
            if (entry.fLocked) {
                model->unlockCoin(outpt);
                entry.fLocked = false;
                ++nSelectableInputs;
            } else {
                coinControl->UnSelect(outpt);
                model->lockCoin(outpt);
                entry.fLocked = true;
                if (nSelectableInputs > 0) --nSelectableInputs;
            }
        }
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++) {
            QTreeWidgetItem* item = ui->treeWidget->topLevelItem(i);
            if (item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_OUTPUT)
                continue;
            const size_t entryIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
            if (entryIdx >= m_allEntries.size()) continue;
            const OutputEntry& entry = m_allEntries[entryIdx];
            if (entry.fLocked) {
                item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
                item->setDisabled(true);
                item->setIcon(COLUMN_CHECKBOX, QIcon(":/icons/lock_closed"));
            } else {
                item->setDisabled(false);
                item->setIcon(COLUMN_CHECKBOX, QIcon());
            }
        }
        m_bInternalUpdate = false;
        ui->treeWidget->setEnabled(true);
        updateLabelLocked();
        updateLabels();
        updateDialogLabels();
    } else {
        QMessageBox msgBox;
        msgBox.setObjectName("lockMessageBox");
        msgBox.setStyleSheet(GUIUtil::loadStyleSheet());
        msgBox.setText(tr("Please switch to \"List mode\" to use this function."));
        msgBox.exec();
    }
}

// context menu
void CoinControlDialog::showMenu(const QPoint& point)
{
    QTreeWidgetItem* item = ui->treeWidget->itemAt(point);
    if (item) {
        if (item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() == ITEM_TYPE_LOAD_MORE)
            return;
        contextMenuItem = item;

        // disable some items (like Copy Transaction ID, lock, unlock) for tree roots in context menu
        if (item->text(COLUMN_TXHASH).length() == 64) // transaction hash is 64 characters (this means its a child node, so its not a parent node in tree mode)
        {
            copyTransactionHashAction->setEnabled(true);
            const size_t entryIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
            const bool isLocked = (entryIdx < m_allEntries.size())
                ? m_allEntries[entryIdx].fLocked
                : model->isLockedCoin(uint256(item->text(COLUMN_TXHASH).toStdString()), item->text(COLUMN_VOUT_INDEX).toUInt());
            if (isLocked) {
                lockAction->setEnabled(false);
                unlockAction->setEnabled(true);
            } else {
                lockAction->setEnabled(true);
                unlockAction->setEnabled(false);
            }
        } else // this means click on parent node in tree mode -> disable all
        {
            copyTransactionHashAction->setEnabled(false);
            lockAction->setEnabled(false);
            unlockAction->setEnabled(false);
        }

        // show context menu
        contextMenu->exec(QCursor::pos());
    }
}

// context menu action: copy amount
void CoinControlDialog::copyAmount()
{
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(contextMenuItem->text(COLUMN_AMOUNT)));
}

// context menu action: copy label
void CoinControlDialog::copyLabel()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_LABEL).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_LABEL));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_LABEL));
}

// context menu action: copy address
void CoinControlDialog::copyAddress()
{
    if (ui->radioTreeMode->isChecked() && contextMenuItem->text(COLUMN_ADDRESS).length() == 0 && contextMenuItem->parent())
        GUIUtil::setClipboard(contextMenuItem->parent()->text(COLUMN_ADDRESS));
    else
        GUIUtil::setClipboard(contextMenuItem->text(COLUMN_ADDRESS));
}

// context menu action: copy transaction id
void CoinControlDialog::copyTransactionHash()
{
    GUIUtil::setClipboard(contextMenuItem->text(COLUMN_TXHASH));
}

// context menu action: lock coin
void CoinControlDialog::lockCoin()
{
    if (contextMenuItem->checkState(COLUMN_CHECKBOX) == Qt::Checked)
        contextMenuItem->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);

    COutPoint outpt(uint256(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->lockCoin(outpt);
    const size_t entryIdx = contextMenuItem->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
    if (entryIdx < m_allEntries.size() && !m_allEntries[entryIdx].fLocked) {
        m_allEntries[entryIdx].fLocked = true;
        if (nSelectableInputs > 0) --nSelectableInputs;
    }
    contextMenuItem->setDisabled(true);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon(":/icons/lock_closed"));
    if (ui->radioTreeMode->isChecked() && contextMenuItem->parent()) {
        updateGroupCheckState(contextMenuItem->parent()->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong());
    }
    updateLabelLocked();
    updateLabels();
}

// context menu action: unlock coin
void CoinControlDialog::unlockCoin()
{
    COutPoint outpt(uint256(contextMenuItem->text(COLUMN_TXHASH).toStdString()), contextMenuItem->text(COLUMN_VOUT_INDEX).toUInt());
    model->unlockCoin(outpt);
    const size_t entryIdx = contextMenuItem->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
    if (entryIdx < m_allEntries.size() && m_allEntries[entryIdx].fLocked) {
        m_allEntries[entryIdx].fLocked = false;
        ++nSelectableInputs;
    }
    contextMenuItem->setDisabled(false);
    contextMenuItem->setIcon(COLUMN_CHECKBOX, QIcon());
    if (ui->radioTreeMode->isChecked() && contextMenuItem->parent()) {
        updateGroupCheckState(contextMenuItem->parent()->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong());
    }
    updateLabelLocked();
    updateLabels();
}

// copy label "Quantity" to clipboard
void CoinControlDialog::clipboardQuantity()
{
    GUIUtil::setClipboard(ui->labelCoinControlQuantity->text());
    inform(tr("Quantity Copied"));
}

// copy label "Amount" to clipboard
void CoinControlDialog::clipboardAmount()
{
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(ui->labelCoinControlAmount->text().left(ui->labelCoinControlAmount->text().indexOf(" "))));
    inform(tr("Amount Copied"));
}

// copy label "Fee" to clipboard
void CoinControlDialog::clipboardFee()
{
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(ui->labelCoinControlFee->text().left(ui->labelCoinControlFee->text().indexOf(" ")).replace("~", "")));
    inform(tr("Fee Copied"));
}

// copy label "After fee" to clipboard
void CoinControlDialog::clipboardAfterFee()
{
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(ui->labelCoinControlAfterFee->text().left(ui->labelCoinControlAfterFee->text().indexOf(" ")).replace("~", "")));
    inform(tr("After Fee Copied"));
}

// copy label "Bytes" to clipboard
void CoinControlDialog::clipboardBytes()
{
    GUIUtil::setClipboard(ui->labelCoinControlBytes->text().replace("~", ""));
    inform(tr("Bytes Copied"));
}

// copy label "Dust" to clipboard
void CoinControlDialog::clipboardLowOutput()
{
    GUIUtil::setClipboard(ui->labelCoinControlLowOutput->text());
    inform(tr("Dust Copied"));
}

// copy label "Change" to clipboard
void CoinControlDialog::clipboardChange()
{
    GUIUtil::setClipboard(BitcoinUnits::removeSpaces(ui->labelCoinControlChange->text().left(ui->labelCoinControlChange->text().indexOf(" ")).replace("~", "")));
    inform(tr("Change Copied"));
}

void CoinControlDialog::sortEntryIndices(std::vector<size_t>& indices) const
{
    const int col = sortColumn;
    const bool asc = (sortOrder == Qt::AscendingOrder);

    std::sort(indices.begin(), indices.end(), [this, col, asc](size_t a, size_t b) {
        const OutputEntry& ea = m_allEntries[a];
        const OutputEntry& eb = m_allEntries[b];
        int cmp = 0;
        switch (col) {
        case COLUMN_AMOUNT:
            cmp = (ea.nValue < eb.nValue) ? -1 : ((ea.nValue > eb.nValue) ? 1 : 0);
            break;
        case COLUMN_DATE:
            cmp = (ea.nTime < eb.nTime) ? -1 : ((ea.nTime > eb.nTime) ? 1 : 0);
            break;
        case COLUMN_CONFIRMATIONS:
            cmp = (ea.nDepth < eb.nDepth) ? -1 : ((ea.nDepth > eb.nDepth) ? 1 : 0);
            break;
        case COLUMN_LABEL:
            cmp = QString::localeAwareCompare(ea.sLabel, eb.sLabel);
            break;
        case COLUMN_ADDRESS:
            cmp = QString::localeAwareCompare(ea.sAddress, eb.sAddress);
            break;
        default:
            cmp = (ea.nValue < eb.nValue) ? -1 : ((ea.nValue > eb.nValue) ? 1 : 0);
            break;
        }
        if (cmp == 0) {
            return asc ? (a < b) : (a > b);
        }
        return asc ? (cmp < 0) : (cmp > 0);
    });
}

// treeview: sort
void CoinControlDialog::sortView(int column, Qt::SortOrder order)
{
    sortColumn = column;
    sortOrder = order;
    ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);

    if (!model || m_allEntries.empty())
        return;

    const bool treeMode = ui->radioTreeMode->isChecked();
    if (treeMode) {
        ui->treeWidget->setUpdatesEnabled(false);
        m_bInternalUpdate = true;
        ui->treeWidget->sortItems(column, order);
        for (size_t groupIdx = 0; groupIdx < m_addressGroups.size(); ++groupIdx) {
            AddressGroup& group = m_addressGroups[groupIdx];
            if (group.nLoadedChildren > 0 && group.treeItem) {
                const size_t targetCount = group.nLoadedChildren;
                qDeleteAll(group.treeItem->takeChildren());
                group.nLoadedChildren = 0;
                sortEntryIndices(group.entryIndices);
                loadMoreGroupChildren(groupIdx, targetCount);
            }
        }
        m_bInternalUpdate = false;
        ui->treeWidget->setUpdatesEnabled(true);
    } else {
        const size_t targetCount = std::max<size_t>(m_nLoadedListItems, COIN_CONTROL_BATCH_SIZE);
        ui->treeWidget->setUpdatesEnabled(false);
        m_bInternalUpdate = true;
        ui->treeWidget->clear();
        m_nLoadedListItems = 0;
        sortEntryIndices(m_sortedListIndices);
        loadMoreListItems(targetCount);
        m_bInternalUpdate = false;
        ui->treeWidget->setUpdatesEnabled(true);
    }
}

// treeview: clicked on header
void CoinControlDialog::headerSectionClicked(int logicalIndex)
{
    if (logicalIndex == COLUMN_CHECKBOX) // click on most left column -> do nothing
    {
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    } else {
        if (sortColumn == logicalIndex)
            sortOrder = ((sortOrder == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder);
        else {
            sortColumn = logicalIndex;
            sortOrder = ((sortColumn == COLUMN_LABEL || sortColumn == COLUMN_ADDRESS) ? Qt::AscendingOrder : Qt::DescendingOrder); // if label or address then default => asc, else default => desc
        }

        sortView(sortColumn, sortOrder);
    }
}

// toggle tree mode
void CoinControlDialog::radioTreeMode(bool checked)
{
    if (checked && model)
        updateView();
}

// toggle list mode
void CoinControlDialog::radioListMode(bool checked)
{
    if (checked && model)
        updateView();
}

// checkbox clicked by user
void CoinControlDialog::viewItemChanged(QTreeWidgetItem* item, int column)
{
    if (m_bInternalUpdate || column != COLUMN_CHECKBOX || !item)
        return;

    const int itemType = item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt();
    if (itemType == ITEM_TYPE_GROUP) {
        const size_t groupIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
        if (groupIdx >= m_addressGroups.size())
            return;

        AddressGroup& group = m_addressGroups[groupIdx];
        const bool selectGroup = (item->checkState(COLUMN_CHECKBOX) == Qt::Checked);
        for (size_t entryIdx : group.entryIndices) {
            const OutputEntry& entry = m_allEntries[entryIdx];
            if (entry.fLocked) continue;
            if (selectGroup) {
                coinControl->Select(entry.outpoint);
            } else {
                coinControl->UnSelect(entry.outpoint);
            }
        }

        m_bInternalUpdate = true;
        const Qt::CheckState childState = selectGroup ? Qt::Checked : Qt::Unchecked;
        for (int i = 0; i < item->childCount(); ++i) {
            QTreeWidgetItem* child = item->child(i);
            if (child->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_OUTPUT)
                continue;
            if (!child->isDisabled()) {
                child->setCheckState(COLUMN_CHECKBOX, childState);
            }
        }
        m_bInternalUpdate = false;
        updateGroupCheckState(groupIdx);

        if (ui->treeWidget->isEnabled()) {
            updateLabels();
            updateDialogLabels();
        }
        return;
    }

    if (itemType == ITEM_TYPE_OUTPUT) {
        const size_t entryIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
        if (entryIdx >= m_allEntries.size())
            return;

        const OutputEntry& entry = m_allEntries[entryIdx];
        if (item->checkState(COLUMN_CHECKBOX) == Qt::Unchecked) {
            coinControl->UnSelect(entry.outpoint);
        } else if (entry.fLocked || item->isDisabled()) {
            m_bInternalUpdate = true;
            item->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
            m_bInternalUpdate = false;
        } else {
            coinControl->Select(entry.outpoint);
        }

        if (ui->radioTreeMode->isChecked() && item->parent()) {
            updateGroupCheckState(item->parent()->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong());
        }

        if (ui->treeWidget->isEnabled()) {
            updateLabels();
            updateDialogLabels();
        }
    }
}

// return human readable label for priority number
QString CoinControlDialog::getPriorityLabel(double dPriority, double mempoolEstimatePriority)
{
    double dPriorityMedium = mempoolEstimatePriority;

    if (dPriorityMedium <= 0)
        dPriorityMedium = AllowFreeThreshold(); // not enough data, back to hard-coded

    if (dPriority / 1000000 > dPriorityMedium)
        return tr("highest");
    else if (dPriority / 100000 > dPriorityMedium)
        return tr("higher");
    else if (dPriority / 10000 > dPriorityMedium)
        return tr("high");
    else if (dPriority / 1000 > dPriorityMedium)
        return tr("medium-high");
    else if (dPriority > dPriorityMedium)
        return tr("medium");
    else if (dPriority * 10 > dPriorityMedium)
        return tr("low-medium");
    else if (dPriority * 100 > dPriorityMedium)
        return tr("low");
    else if (dPriority * 1000 > dPriorityMedium)
        return tr("lower");
    else
        return tr("lowest");
}

// shows count of locked unspent outputs
void CoinControlDialog::updateLabelLocked()
{
    std::vector<COutPoint> vOutpts;
    model->listLockedCoins(vOutpts);
    if (vOutpts.size() > 0) {
        ui->labelLocked->setText(tr("(%1 locked)").arg(vOutpts.size()));
        ui->labelLocked->setVisible(true);
    } else
        ui->labelLocked->setVisible(false);
}

void CoinControlDialog::updateDialogLabels()
{
    if (this->parentWidget() == nullptr) {
        return;
    }

    std::vector<COutPoint> vCoinControl;
    std::vector<COutput> vOutputs;
    coinControl->ListSelected(vCoinControl);
    model->getOutputs(vCoinControl, vOutputs);

    CAmount nAmount = 0;
    unsigned int nQuantity = 0;
    for (const COutput& out : vOutputs) {
        uint256 txhash = out.tx->GetHash();
        COutPoint outpt(txhash, out.i);
        if(model->isSpent(outpt)) {
            coinControl->UnSelect(outpt);
            continue;
        }

        // Quantity
        nQuantity++;

        // Amount
        nAmount += out.tx->vout[out.i].nValue;
    }
}

void CoinControlDialog::updateLabels()
{
    if (!model)
        return;

    // nPayAmount
    CAmount nPayAmount = 0;
    bool fDust = false;
    Q_FOREACH (const CAmount& amount, payAmounts) {
        nPayAmount += amount;
        if (amount > 0) {
            CTxOut txout(amount, (CScript)std::vector<unsigned char>(24, 0));
            if (txout.IsDust(::minRelayTxFee))
                fDust = true;
        }
    }

    QString sPriorityLabel = tr("none");
    CAmount nAmount = 0;
    CAmount nPayFee = 0;
    CAmount nAfterFee = 0;
    CAmount nChange = 0;
    unsigned int nBytes = 0;
    unsigned int nBytesInputs = 0;
    double dPriority = 0;
    double dPriorityInputs = 0;
    unsigned int nQuantity = 0;
    int nQuantityUncompressed = 0;
    bool fAllowFree = false;

    const unsigned int nSelectedCount = coinControl->QuantitySelected();
    if (nSelectedCount > 0) {
        if (!m_allEntries.empty()) {
            if (nSelectedCount < 1024) {
                std::vector<COutPoint> vCoinControl;
                coinControl->ListSelected(vCoinControl);
                for (const COutPoint& outpt : vCoinControl) {
                    auto it = m_outpointToEntryIdx.find(outpt);
                    if (it == m_outpointToEntryIdx.end()) {
                        if (model->isSpent(outpt)) {
                            coinControl->UnSelect(outpt);
                        }
                        continue;
                    }
                    const OutputEntry& entry = m_allEntries[it->second];
                    nQuantity++;
                    nAmount += entry.nValue;
                    dPriorityInputs += (double)entry.nValue * (entry.nDepth + 1);
                    nBytesInputs += entry.nInputBytes;
                    if (entry.nInputBytes == 180)
                        nQuantityUncompressed++;
                }
            } else {
                for (const OutputEntry& entry : m_allEntries) {
                    if (!coinControl->IsSelected(entry.outpoint))
                        continue;
                    nQuantity++;
                    nAmount += entry.nValue;
                    dPriorityInputs += (double)entry.nValue * (entry.nDepth + 1);
                    nBytesInputs += entry.nInputBytes;
                    if (entry.nInputBytes == 180)
                        nQuantityUncompressed++;
                }
            }
        } else {
            std::vector<COutPoint> vCoinControl;
            std::vector<COutput> vOutputs;
            coinControl->ListSelected(vCoinControl);
            model->getOutputs(vCoinControl, vOutputs);

            std::map<CScript, int> mapScriptInputBytes;
            for (const COutput& out : vOutputs) {
                uint256 txhash = out.tx->GetHash();
                COutPoint outpt(txhash, out.i);
                if (model->isSpent(outpt)) {
                    coinControl->UnSelect(outpt);
                    continue;
                }

                nQuantity++;
                nAmount += out.tx->vout[out.i].nValue;
                dPriorityInputs += (double)out.tx->vout[out.i].nValue * (out.nDepth + 1);

                const CScript& scriptPubKey = out.tx->vout[out.i].scriptPubKey;
                auto cacheIt = mapScriptInputBytes.find(scriptPubKey);
                int inputBytes = 148;
                if (cacheIt != mapScriptInputBytes.end()) {
                    inputBytes = cacheIt->second;
                } else {
                    CTxDestination address;
                    if (ExtractDestination(scriptPubKey, address)) {
                        CPubKey pubkey;
                        CKeyID* keyid = boost::get<CKeyID>(&address);
                        if (keyid && model->getPubKey(*keyid, pubkey) && !pubkey.IsCompressed()) {
                            inputBytes = 180;
                        }
                    }
                    mapScriptInputBytes.emplace(scriptPubKey, inputBytes);
                }
                nBytesInputs += inputBytes;
                if (inputBytes == 180)
                    nQuantityUncompressed++;
            }
        }
    }

    // update SelectAll button state
    // if inputs selected > inputs unselected, set checked (label "Unselect All")
    // if inputs selected <= inputs unselected, set unchecked (label "Select All")
    updatePushButtonSelectAll(coinControl->QuantitySelected() * 2 > nSelectableInputs);

    // calculation
    const int P2PKH_OUT_SIZE = 34;
    if (nQuantity > 0) {
        // Bytes: nBytesInputs + (num_of_outputs * bytes_per_output)
        nBytes = nBytesInputs + std::max(1, payAmounts.size()) * P2PKH_OUT_SIZE;
        // always assume +1 (p2pkh) output for change here
        nBytes += P2PKH_OUT_SIZE;
        // nVersion, nLockTime and vin/vout len sizes
        nBytes += 10;

        // Priority
        double mempoolEstimatePriority = mempool.estimateSmartPriority(nTxConfirmTarget);
        dPriority = dPriorityInputs / (nBytes - nBytesInputs + (nQuantityUncompressed * 29)); // 29 = 180 - 151 (uncompressed public keys are over the limit. max 151 bytes of the input are ignored for priority)
        sPriorityLabel = CoinControlDialog::getPriorityLabel(dPriority, mempoolEstimatePriority);

        // Fee
        nPayFee = CWallet::GetMinimumFee(nBytes, nTxConfirmTarget, mempool);

        // Allow free? (require at least hard-coded threshold and default to that if no estimate)
        double dPriorityNeeded = std::max(mempoolEstimatePriority, AllowFreeThreshold());
        if (dPriorityNeeded <= 0)
            dPriorityNeeded = AllowFreeThreshold(); // not enough data, back to hard-coded
        fAllowFree = (dPriority >= dPriorityNeeded);

        if (fSendFreeTransactions)
            if (fAllowFree && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE)
                nPayFee = 0;

        if (nPayAmount > 0) {
            nChange = nAmount - nPayFee - nPayAmount;

            // Never create dust outputs; if we would, just add the dust to the fee.
            if (nChange > 0 && nChange < CENT) {
                CTxOut txout(nChange, (CScript)std::vector<unsigned char>(24, 0));
                if (txout.IsDust(::minRelayTxFee)) {
                    nPayFee += nChange;
                    nChange = 0;
                }
            }

            if (nChange == 0)
                nBytes -= P2PKH_OUT_SIZE;
        }

        // after fee
        nAfterFee = nAmount - nPayFee;
        if (nAfterFee < 0)
            nAfterFee = 0;
    }

    // actually update labels
    int nDisplayUnit = BitcoinUnits::PIV;
    if (model && model->getOptionsModel())
        nDisplayUnit = model->getOptionsModel()->getDisplayUnit();

    // enable/disable "dust" and "change"
    const bool hasPayAmount = nPayAmount > 0;
    ui->labelCoinControlLowOutputText->setEnabled(hasPayAmount);
    ui->labelCoinControlLowOutput->setEnabled(hasPayAmount);
    ui->labelCoinControlChangeText->setEnabled(hasPayAmount);
    ui->labelCoinControlChange->setEnabled(hasPayAmount);

    // stats
    ui->labelCoinControlQuantity->setText(QString::number(nQuantity));
    ui->labelCoinControlAmount->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAmount));
    ui->labelCoinControlFee->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nPayFee));
    ui->labelCoinControlAfterFee->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nAfterFee));
    ui->labelCoinControlBytes->setText(((nBytes > 0) ? "~" : "") + QString::number(nBytes));
    ui->labelCoinControlLowOutput->setText(fDust ? tr("yes") : tr("no"));
    ui->labelCoinControlChange->setText(BitcoinUnits::formatWithUnit(nDisplayUnit, nChange));
    if (nPayFee > 0 && !(payTxFee.GetFeePerK() > 0 && fPayAtLeastCustomFee && nBytes < 1000)) {
        ui->labelCoinControlFee->setText("~" + ui->labelCoinControlFee->text());
        ui->labelCoinControlAfterFee->setText("~" + ui->labelCoinControlAfterFee->text());
        if (nChange > 0)
            ui->labelCoinControlChange->setText("~" + ui->labelCoinControlChange->text());
    }

    // turn labels "red"
    ui->labelCoinControlBytes->setStyleSheet((nBytes >= MAX_FREE_TRANSACTION_CREATE_SIZE) ? "color:red;" : "");     // Bytes >= 1000
    ui->labelCoinControlLowOutput->setStyleSheet((fDust) ? "color:red;" : "");                                      // Dust = "yes"

    // tool tips
    QString toolTip1 = tr("This label turns red, if the transaction size is greater than 1000 bytes.") + "<br /><br />";
    toolTip1 += tr("This means a fee of at least %1 per kB is required.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CWallet::GetRequiredFee(1000))) + "<br /><br />";
    toolTip1 += tr("Can vary +/- 1 byte per input.");

    QString toolTip2 = tr("Transactions with higher priority are more likely to get included into a block.") + "<br /><br />";
    toolTip2 += tr("This label turns red, if the priority is smaller than \"medium\".") + "<br /><br />";
    toolTip2 += tr("This means a fee of at least %1 per kB is required.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, CWallet::GetRequiredFee(1000)));

    QString toolTip3 = tr("This label turns red, if any recipient receives an amount smaller than %1.").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, ::minRelayTxFee.GetFee(546)));

    // how many satoshis the estimated fee can vary per byte we guess wrong
    double dFeeVary;
    if (payTxFee.GetFeePerK() > 0)
        dFeeVary = (double)std::max(CWallet::GetRequiredFee(1000), payTxFee.GetFeePerK()) / 1000;
    else
        dFeeVary = (double)std::max(CWallet::GetRequiredFee(1000), mempool.estimateSmartFee(nTxConfirmTarget).GetFeePerK()) / 1000;
    QString toolTip4 = tr("Can vary +/- %1 u%2 per input.").arg(dFeeVary).arg(CURRENCY_UNIT.c_str());

    ui->labelCoinControlFee->setToolTip(toolTip4);
    ui->labelCoinControlAfterFee->setToolTip(toolTip4);
    ui->labelCoinControlBytes->setToolTip(toolTip1);
    ui->labelCoinControlLowOutput->setToolTip(toolTip3);
    ui->labelCoinControlChange->setToolTip(toolTip4);
    ui->labelCoinControlFeeText->setToolTip(ui->labelCoinControlFee->toolTip());
    ui->labelCoinControlAfterFeeText->setToolTip(ui->labelCoinControlAfterFee->toolTip());
    ui->labelCoinControlBytesText->setToolTip(ui->labelCoinControlBytes->toolTip());
    ui->labelCoinControlLowOutputText->setToolTip(ui->labelCoinControlLowOutput->toolTip());
    ui->labelCoinControlChangeText->setToolTip(ui->labelCoinControlChange->toolTip());

    // Insufficient funds
    QLabel* label = findChild<QLabel*>("labelCoinControlInsuffFunds");
    if (label)
        label->setVisible(nChange < 0);
}

void CoinControlDialog::loadCoinsFromWallet()
{
    m_allEntries.clear();
    m_outpointToEntryIdx.clear();
    m_addressGroups.clear();
    m_sortedListIndices.clear();
    m_nLoadedListItems = 0;
    nSelectableInputs = 0;

    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    std::map<QString, std::vector<COutput>> mapCoins;
    model->listCoins(mapCoins);

    std::vector<COutPoint> vLockedCoins;
    model->listLockedCoins(vLockedCoins);
    std::set<COutPoint> setLockedCoins(vLockedCoins.begin(), vLockedCoins.end());

    size_t totalCoins = 0;
    for (const auto& kv : mapCoins) {
        totalCoins += kv.second.size();
    }
    m_allEntries.reserve(totalCoins);
    m_sortedListIndices.reserve(totalCoins);
    m_addressGroups.reserve(mapCoins.size());

    struct ScriptUiMeta {
        QString sAddress;
        QString sLabel;
        int nInputBytes{148};
    };
    std::map<CScript, ScriptUiMeta> mapScriptMeta;
    std::map<const CWalletTx*, int64_t> mapTxTime;

    const QString sNoLabel = tr("(no label)");
    const QString sChangeText = tr("(change)");

    for (const auto& coins : mapCoins) {
        AddressGroup group;
        group.sWalletAddress = coins.first;
        group.sWalletLabel = model->getAddressTableModel()->labelForAddress(group.sWalletAddress);
        if (group.sWalletLabel.isEmpty())
            group.sWalletLabel = sNoLabel;
        group.entryIndices.reserve(coins.second.size());

        for (const COutput& out : coins.second) {
            const CTxOut& txout = out.tx->vout[out.i];
            auto scriptIt = mapScriptMeta.find(txout.scriptPubKey);
            if (scriptIt == mapScriptMeta.end()) {
                ScriptUiMeta meta;
                CTxDestination outputAddress;
                if (ExtractDestination(txout.scriptPubKey, outputAddress)) {
                    meta.sAddress = QString::fromStdString(EncodeDestination(outputAddress));
                    meta.sLabel = model->getAddressTableModel()->labelForAddress(meta.sAddress);
                    if (meta.sLabel.isEmpty())
                        meta.sLabel = sNoLabel;
                    CPubKey pubkey;
                    CKeyID* keyid = boost::get<CKeyID>(&outputAddress);
                    if (keyid && model->getPubKey(*keyid, pubkey) && !pubkey.IsCompressed()) {
                        meta.nInputBytes = 180;
                    }
                }
                scriptIt = mapScriptMeta.emplace(txout.scriptPubKey, std::move(meta)).first;
            }
            const ScriptUiMeta& meta = scriptIt->second;

            int64_t txTime = 0;
            auto timeIt = mapTxTime.find(out.tx);
            if (timeIt != mapTxTime.end()) {
                txTime = timeIt->second;
            } else {
                txTime = out.tx->GetTxTime();
                mapTxTime.emplace(out.tx, txTime);
            }

            OutputEntry entry;
            entry.outpoint = COutPoint(out.tx->GetHash(), out.i);
            entry.nValue = txout.nValue;
            entry.nDepth = out.nDepth;
            entry.nTime = txTime;
            entry.nInputBytes = meta.nInputBytes;
            entry.fLocked = !setLockedCoins.empty() && (setLockedCoins.count(entry.outpoint) > 0);
            entry.fIsChange = (meta.sAddress != group.sWalletAddress);
            entry.sAddress = meta.sAddress;
            entry.sLabel = entry.fIsChange ? sChangeText : meta.sLabel;
            entry.sWalletAddress = group.sWalletAddress;
            entry.sWalletLabel = group.sWalletLabel;

            if (entry.fLocked) {
                coinControl->UnSelect(entry.outpoint);
            } else {
                ++nSelectableInputs;
            }

            const size_t entryIdx = m_allEntries.size();
            m_allEntries.push_back(std::move(entry));
            m_outpointToEntryIdx.emplace(m_allEntries.back().outpoint, entryIdx);
            m_sortedListIndices.push_back(entryIdx);
            group.entryIndices.push_back(entryIdx);
            group.nSum += txout.nValue;
        }

        m_addressGroups.push_back(std::move(group));
    }
}

CCoinControlWidgetItem* CoinControlDialog::createOutputItem(size_t entryIdx, int nDisplayUnit, bool treeMode) const
{
    const OutputEntry& entry = m_allEntries[entryIdx];
    const QFlags<Qt::ItemFlag> flgCheckbox = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;

    CCoinControlWidgetItem* itemOutput = new CCoinControlWidgetItem();
    itemOutput->setFlags(flgCheckbox);
    itemOutput->setData(COLUMN_CHECKBOX, ROLE_ITEM_TYPE, ITEM_TYPE_OUTPUT);
    itemOutput->setData(COLUMN_CHECKBOX, ROLE_INDEX, QVariant::fromValue<qulonglong>(entryIdx));
    itemOutput->setCheckState(COLUMN_CHECKBOX, coinControl->IsSelected(entry.outpoint) ? Qt::Checked : Qt::Unchecked);

    // address
    if (!entry.sAddress.isEmpty()) {
        if (!treeMode || entry.fIsChange)
            itemOutput->setText(COLUMN_ADDRESS, entry.sAddress);
        else
            itemOutput->setToolTip(COLUMN_ADDRESS, entry.sAddress);
    }

    // label
    if (entry.fIsChange) {
        itemOutput->setToolTip(COLUMN_LABEL, tr("change from %1 (%2)").arg(entry.sWalletLabel).arg(entry.sWalletAddress));
        itemOutput->setText(COLUMN_LABEL, entry.sLabel);
    } else if (!treeMode) {
        itemOutput->setText(COLUMN_LABEL, entry.sLabel);
    }

    // amount
    const QString sFormattedAmount = BitcoinUnits::format(nDisplayUnit, entry.nValue);
    itemOutput->setText(COLUMN_AMOUNT, sFormattedAmount);
    itemOutput->setToolTip(COLUMN_AMOUNT, sFormattedAmount);
    itemOutput->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)entry.nValue));

    // date
    const QString sDateTime = GUIUtil::dateTimeStr(entry.nTime);
    itemOutput->setText(COLUMN_DATE, sDateTime);
    itemOutput->setToolTip(COLUMN_DATE, sDateTime);
    itemOutput->setData(COLUMN_DATE, Qt::UserRole, QVariant((qlonglong)entry.nTime));

    // confirmations
    itemOutput->setText(COLUMN_CONFIRMATIONS, QString::number(entry.nDepth));
    itemOutput->setData(COLUMN_CONFIRMATIONS, Qt::UserRole, QVariant((qlonglong)entry.nDepth));

    // transaction hash & vout index
    itemOutput->setText(COLUMN_TXHASH, QString::fromStdString(entry.outpoint.hash.GetHex()));
    itemOutput->setText(COLUMN_VOUT_INDEX, QString::number(entry.outpoint.n));

    // disable locked coins
    if (entry.fLocked) {
        itemOutput->setCheckState(COLUMN_CHECKBOX, Qt::Unchecked);
        itemOutput->setDisabled(true);
        itemOutput->setIcon(COLUMN_CHECKBOX, QIcon(":/icons/lock_closed"));
    }

    return itemOutput;
}

void CoinControlDialog::updateGroupCheckState(size_t groupIdx)
{
    if (groupIdx >= m_addressGroups.size())
        return;
    AddressGroup& group = m_addressGroups[groupIdx];
    if (!group.treeItem)
        return;

    size_t nSelectable = 0;
    size_t nSelected = 0;
    for (size_t entryIdx : group.entryIndices) {
        const OutputEntry& entry = m_allEntries[entryIdx];
        if (!entry.fLocked) {
            ++nSelectable;
            if (coinControl->IsSelected(entry.outpoint))
                ++nSelected;
        }
    }

    Qt::CheckState state = Qt::Unchecked;
    if (nSelected > 0) {
        state = (nSelected == nSelectable) ? Qt::Checked : Qt::PartiallyChecked;
    }

    const bool prevInternal = m_bInternalUpdate;
    m_bInternalUpdate = true;
    group.treeItem->setCheckState(COLUMN_CHECKBOX, state);
    m_bInternalUpdate = prevInternal;
}

void CoinControlDialog::syncVisibleCheckStates()
{
    const bool prevInternal = m_bInternalUpdate;
    m_bInternalUpdate = true;
    ui->treeWidget->setUpdatesEnabled(false);

    if (ui->radioTreeMode->isChecked()) {
        for (size_t groupIdx = 0; groupIdx < m_addressGroups.size(); ++groupIdx) {
            updateGroupCheckState(groupIdx);
            AddressGroup& group = m_addressGroups[groupIdx];
            if (!group.treeItem || group.nLoadedChildren == 0)
                continue;
            for (int i = 0; i < group.treeItem->childCount(); ++i) {
                QTreeWidgetItem* child = group.treeItem->child(i);
                if (child->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_OUTPUT)
                    continue;
                const size_t entryIdx = child->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
                if (entryIdx >= m_allEntries.size()) continue;
                const OutputEntry& entry = m_allEntries[entryIdx];
                child->setCheckState(COLUMN_CHECKBOX, (!entry.fLocked && coinControl->IsSelected(entry.outpoint)) ? Qt::Checked : Qt::Unchecked);
            }
        }
    } else {
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = ui->treeWidget->topLevelItem(i);
            if (item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_OUTPUT)
                continue;
            const size_t entryIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
            if (entryIdx >= m_allEntries.size()) continue;
            const OutputEntry& entry = m_allEntries[entryIdx];
            item->setCheckState(COLUMN_CHECKBOX, (!entry.fLocked && coinControl->IsSelected(entry.outpoint)) ? Qt::Checked : Qt::Unchecked);
        }
    }

    ui->treeWidget->setUpdatesEnabled(true);
    m_bInternalUpdate = prevInternal;
}

void CoinControlDialog::loadMoreGroupChildren(size_t groupIdx, size_t batchSize)
{
    if (!model || !model->getOptionsModel() || groupIdx >= m_addressGroups.size())
        return;

    AddressGroup& group = m_addressGroups[groupIdx];
    if (!group.treeItem || group.nLoadedChildren >= group.entryIndices.size())
        return;

    const bool prevInternal = m_bInternalUpdate;
    m_bInternalUpdate = true;

    // Remove trailing "Load more..." child if present
    if (group.treeItem->childCount() > 0) {
        QTreeWidgetItem* lastChild = group.treeItem->child(group.treeItem->childCount() - 1);
        if (lastChild->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() == ITEM_TYPE_LOAD_MORE) {
            delete group.treeItem->takeChild(group.treeItem->childCount() - 1);
        }
    }

    const int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();
    const size_t endIdx = std::min(group.nLoadedChildren + batchSize, group.entryIndices.size());
    QList<QTreeWidgetItem*> newChildren;
    newChildren.reserve(static_cast<int>(endIdx - group.nLoadedChildren + 1));

    for (size_t i = group.nLoadedChildren; i < endIdx; ++i) {
        newChildren.append(createOutputItem(group.entryIndices[i], nDisplayUnit, true));
    }
    group.nLoadedChildren = endIdx;

    if (group.nLoadedChildren < group.entryIndices.size()) {
        CCoinControlWidgetItem* itemMore = new CCoinControlWidgetItem();
        itemMore->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        itemMore->setData(COLUMN_CHECKBOX, ROLE_ITEM_TYPE, ITEM_TYPE_LOAD_MORE);
        itemMore->setData(COLUMN_CHECKBOX, ROLE_INDEX, QVariant::fromValue<qlonglong>(static_cast<qlonglong>(groupIdx)));
        itemMore->setText(COLUMN_ADDRESS, tr("Load more... (%1 / %2 shown)").arg(group.nLoadedChildren).arg(group.entryIndices.size()));
        newChildren.append(itemMore);
    }

    group.treeItem->addChildren(newChildren);
    m_bInternalUpdate = prevInternal;
}

void CoinControlDialog::loadMoreListItems(size_t batchSize)
{
    if (!model || !model->getOptionsModel() || m_nLoadedListItems >= m_sortedListIndices.size())
        return;

    const bool prevInternal = m_bInternalUpdate;
    m_bInternalUpdate = true;

    // Remove trailing "Load more..." top-level item if present
    if (ui->treeWidget->topLevelItemCount() > 0) {
        QTreeWidgetItem* lastItem = ui->treeWidget->topLevelItem(ui->treeWidget->topLevelItemCount() - 1);
        if (lastItem->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() == ITEM_TYPE_LOAD_MORE) {
            delete ui->treeWidget->takeTopLevelItem(ui->treeWidget->topLevelItemCount() - 1);
        }
    }

    const int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();
    const size_t endIdx = std::min(m_nLoadedListItems + batchSize, m_sortedListIndices.size());
    QList<QTreeWidgetItem*> newItems;
    newItems.reserve(static_cast<int>(endIdx - m_nLoadedListItems + 1));

    for (size_t i = m_nLoadedListItems; i < endIdx; ++i) {
        newItems.append(createOutputItem(m_sortedListIndices[i], nDisplayUnit, false));
    }
    m_nLoadedListItems = endIdx;

    if (m_nLoadedListItems < m_sortedListIndices.size()) {
        CCoinControlWidgetItem* itemMore = new CCoinControlWidgetItem();
        itemMore->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        itemMore->setData(COLUMN_CHECKBOX, ROLE_ITEM_TYPE, ITEM_TYPE_LOAD_MORE);
        itemMore->setData(COLUMN_CHECKBOX, ROLE_INDEX, QVariant::fromValue<qlonglong>(-1));
        itemMore->setText(COLUMN_ADDRESS, tr("Load more... (%1 / %2 shown)").arg(m_nLoadedListItems).arg(m_sortedListIndices.size()));
        newItems.append(itemMore);
    }

    ui->treeWidget->addTopLevelItems(newItems);
    m_bInternalUpdate = prevInternal;
}

void CoinControlDialog::onTreeItemExpanded(QTreeWidgetItem* item)
{
    if (!item || item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_GROUP)
        return;

    const size_t groupIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toULongLong();
    if (groupIdx >= m_addressGroups.size())
        return;

    AddressGroup& group = m_addressGroups[groupIdx];
    if (group.nLoadedChildren == 0 && !group.entryIndices.empty()) {
        ui->treeWidget->setUpdatesEnabled(false);
        sortEntryIndices(group.entryIndices);
        loadMoreGroupChildren(groupIdx, COIN_CONTROL_BATCH_SIZE);
        ui->treeWidget->setUpdatesEnabled(true);
    }
}

void CoinControlDialog::onTreeItemClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (!item || item->data(COLUMN_CHECKBOX, ROLE_ITEM_TYPE).toInt() != ITEM_TYPE_LOAD_MORE)
        return;

    const qlonglong groupIdx = item->data(COLUMN_CHECKBOX, ROLE_INDEX).toLongLong();
    ui->treeWidget->setUpdatesEnabled(false);
    if (groupIdx >= 0) {
        loadMoreGroupChildren(static_cast<size_t>(groupIdx), COIN_CONTROL_BATCH_SIZE);
    } else {
        loadMoreListItems(COIN_CONTROL_BATCH_SIZE);
    }
    ui->treeWidget->setUpdatesEnabled(true);
}

void CoinControlDialog::onScrollBarValueChanged(int value)
{
    QScrollBar* bar = ui->treeWidget->verticalScrollBar();
    if (!bar || bar->maximum() <= 0 || value < bar->maximum() - 50)
        return;

    if (!ui->radioTreeMode->isChecked() && m_nLoadedListItems < m_sortedListIndices.size()) {
        ui->treeWidget->setUpdatesEnabled(false);
        loadMoreListItems(COIN_CONTROL_BATCH_SIZE);
        ui->treeWidget->setUpdatesEnabled(true);
    }
}

void CoinControlDialog::updateView()
{
    if (!model || !model->getOptionsModel() || !model->getAddressTableModel())
        return;

    if (m_allEntries.empty()) {
        loadCoinsFromWallet();
    }

    const bool treeMode = ui->radioTreeMode->isChecked();
    ui->treeWidget->setRootIsDecorated(treeMode);

    ui->treeWidget->setUpdatesEnabled(false);
    ui->treeWidget->setEnabled(false);
    m_bInternalUpdate = true;
    ui->treeWidget->clear();

    const int nDisplayUnit = model->getOptionsModel()->getDisplayUnit();
    const QFlags<Qt::ItemFlag> flgGroup = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;

    if (treeMode) {
        QList<QTreeWidgetItem*> topItems;
        topItems.reserve(static_cast<int>(m_addressGroups.size()));

        for (size_t groupIdx = 0; groupIdx < m_addressGroups.size(); ++groupIdx) {
            AddressGroup& group = m_addressGroups[groupIdx];
            group.nLoadedChildren = 0;

            CCoinControlWidgetItem* itemWalletAddress = new CCoinControlWidgetItem();
            group.treeItem = itemWalletAddress;
            itemWalletAddress->setFlags(flgGroup);
            itemWalletAddress->setData(COLUMN_CHECKBOX, ROLE_ITEM_TYPE, ITEM_TYPE_GROUP);
            itemWalletAddress->setData(COLUMN_CHECKBOX, ROLE_INDEX, QVariant::fromValue<qulonglong>(groupIdx));
            if (!group.entryIndices.empty()) {
                itemWalletAddress->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            }

            itemWalletAddress->setText(COLUMN_LABEL, group.sWalletLabel);
            itemWalletAddress->setToolTip(COLUMN_LABEL, group.sWalletLabel);
            itemWalletAddress->setText(COLUMN_ADDRESS, group.sWalletAddress);
            itemWalletAddress->setToolTip(COLUMN_ADDRESS, group.sWalletAddress);

            const QString sFormattedSum = BitcoinUnits::format(nDisplayUnit, group.nSum);
            itemWalletAddress->setText(COLUMN_CHECKBOX, "(" + QString::number(group.entryIndices.size()) + ")");
            itemWalletAddress->setText(COLUMN_AMOUNT, sFormattedSum);
            itemWalletAddress->setToolTip(COLUMN_AMOUNT, sFormattedSum);
            itemWalletAddress->setData(COLUMN_AMOUNT, Qt::UserRole, QVariant((qlonglong)group.nSum));

            updateGroupCheckState(groupIdx);
            topItems.append(itemWalletAddress);
        }

        ui->treeWidget->addTopLevelItems(topItems);
        ui->treeWidget->sortItems(sortColumn, sortOrder);
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);

        // Expand any partially selected groups (triggers lazy child batch load)
        for (int i = 0; i < ui->treeWidget->topLevelItemCount(); i++) {
            if (ui->treeWidget->topLevelItem(i)->checkState(COLUMN_CHECKBOX) == Qt::PartiallyChecked) {
                ui->treeWidget->topLevelItem(i)->setExpanded(true);
            }
        }
    } else {
        for (AddressGroup& group : m_addressGroups) {
            group.treeItem = nullptr;
            group.nLoadedChildren = 0;
        }
        m_nLoadedListItems = 0;
        sortEntryIndices(m_sortedListIndices);
        loadMoreListItems(COIN_CONTROL_BATCH_SIZE);
        ui->treeWidget->header()->setSortIndicator(sortColumn, sortOrder);
    }

    m_bInternalUpdate = false;
    ui->treeWidget->setEnabled(true);
    ui->treeWidget->setUpdatesEnabled(true);
}

void CoinControlDialog::refreshDialog()
{
    loadCoinsFromWallet();
    updateView();
    updateLabelLocked();
    updateLabels();
    updateDialogLabels();
}

void CoinControlDialog::inform(const QString& text)
{
    if (!snackBar) snackBar = new SnackBar(nullptr, this);
    snackBar->setText(text);
    snackBar->resize(this->width(), snackBar->height());
    openDialog(snackBar, this);
}

void CoinControlDialog::clearPayAmounts()
{
    payAmounts.clear();
}

void CoinControlDialog::addPayAmount(const CAmount& amount)
{
    payAmounts.push_back(amount);
}

void CoinControlDialog::updatePushButtonSelectAll(bool checked)
{
    ui->pushButtonSelectAll->setChecked(checked);
    ui->pushButtonSelectAll->setText(checked ? tr("Unselect all") : tr("Select All"));
}
