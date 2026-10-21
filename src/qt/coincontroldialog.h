// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_COINCONTROLDIALOG_H
#define BITCOIN_QT_COINCONTROLDIALOG_H

#include "amount.h"
#include "primitives/transaction.h"
#include "qt/kristatech/snackbar.h"

#include <map>
#include <vector>

#include <QAbstractButton>
#include <QAction>
#include <QDialog>
#include <QList>
#include <QMenu>
#include <QPoint>
#include <QString>
#include <QTreeWidgetItem>

class WalletModel;

class MultisigDialog;
class CCoinControl;
class CTxMemPool;

namespace Ui
{
class CoinControlDialog;
}

class CCoinControlWidgetItem : public QTreeWidgetItem
{
public:
    explicit CCoinControlWidgetItem(QTreeWidget *parent, int type = Type) : QTreeWidgetItem(parent, type) {}
    explicit CCoinControlWidgetItem(int type = Type) : QTreeWidgetItem(type) {}
    explicit CCoinControlWidgetItem(QTreeWidgetItem *parent, int type = Type) : QTreeWidgetItem(parent, type) {}

    bool operator<(const QTreeWidgetItem &other) const;
};

class CoinControlDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CoinControlDialog(QWidget* parent = nullptr);
    ~CoinControlDialog();

    void setModel(WalletModel* model);
    void updateDialogLabels();
    void updateLabels();
    void updateView();
    void refreshDialog();
    void clearPayAmounts();
    void addPayAmount(const CAmount& amount);

    static QString getPriorityLabel(double dPriority, double mempoolEstimatePriority);

    CCoinControl* coinControl;

private:
    Ui::CoinControlDialog* ui;
    SnackBar *snackBar = nullptr;
    WalletModel* model;
    int sortColumn;
    Qt::SortOrder sortOrder;
    QList<CAmount> payAmounts{};
    unsigned int nSelectableInputs{0};

    QMenu* contextMenu;
    QTreeWidgetItem* contextMenuItem;
    QAction* copyTransactionHashAction;
    QAction* lockAction;
    QAction* unlockAction;

    struct OutputEntry {
        COutPoint outpoint;
        CAmount nValue{0};
        int nDepth{0};
        int64_t nTime{0};
        int nInputBytes{148};
        bool fLocked{false};
        bool fIsChange{false};
        QString sAddress;
        QString sLabel;
        QString sWalletAddress;
        QString sWalletLabel;
    };

    struct AddressGroup {
        QString sWalletAddress;
        QString sWalletLabel;
        CAmount nSum{0};
        std::vector<size_t> entryIndices;
        size_t nLoadedChildren{0};
        CCoinControlWidgetItem* treeItem{nullptr};
    };

    std::vector<OutputEntry> m_allEntries;
    std::map<COutPoint, size_t> m_outpointToEntryIdx;
    std::vector<AddressGroup> m_addressGroups;
    std::vector<size_t> m_sortedListIndices;
    size_t m_nLoadedListItems{0};
    bool m_bInternalUpdate{false};

    void loadCoinsFromWallet();
    void sortEntryIndices(std::vector<size_t>& indices) const;
    CCoinControlWidgetItem* createOutputItem(size_t entryIdx, int nDisplayUnit, bool treeMode) const;
    void loadMoreGroupChildren(size_t groupIdx, size_t batchSize = 1000);
    void loadMoreListItems(size_t batchSize = 1000);
    void updateGroupCheckState(size_t groupIdx);
    void syncVisibleCheckStates();

    void updatePushButtonSelectAll(bool checked);
    void sortView(int, Qt::SortOrder);
    void inform(const QString& text);

    enum {
        COLUMN_CHECKBOX,
        COLUMN_AMOUNT,
        COLUMN_LABEL,
        COLUMN_ADDRESS,
        COLUMN_DATE,
        COLUMN_CONFIRMATIONS,
        COLUMN_TXHASH,
        COLUMN_VOUT_INDEX,
    };
    friend class CCoinControlWidgetItem;

private Q_SLOTS:
    void showMenu(const QPoint&);
    void copyAmount();
    void copyLabel();
    void copyAddress();
    void copyTransactionHash();
    void lockCoin();
    void unlockCoin();
    void clipboardQuantity();
    void clipboardAmount();
    void clipboardFee();
    void clipboardAfterFee();
    void clipboardBytes();
    void clipboardLowOutput();
    void clipboardChange();
    void radioTreeMode(bool);
    void radioListMode(bool);
    void viewItemChanged(QTreeWidgetItem*, int);
    void headerSectionClicked(int);
    void buttonSelectAllClicked();
    void buttonToggleLockClicked();
    void updateLabelLocked();
    void onTreeItemExpanded(QTreeWidgetItem*);
    void onTreeItemClicked(QTreeWidgetItem*, int);
    void onScrollBarValueChanged(int);
};

#endif // BITCOIN_QT_COINCONTROLDIALOG_H
