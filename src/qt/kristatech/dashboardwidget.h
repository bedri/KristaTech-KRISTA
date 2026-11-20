// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include "qt/kristatech/furabstractlistitemdelegate.h"
#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/txviewholder.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"

#include <QComboBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QPoint>
#include <QPushButton>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

class KRISTATECHGUI;
class MinerHubWidget;
class WalletModel;
class HomeSplitBar;
class HomeCardDragGrip;
class HomeDragGhostWidget;
class HomeDragSlotMask;

QT_BEGIN_NAMESPACE
class QModelIndex;
class QVBoxLayout;
QT_END_NAMESPACE

enum SortTx {
    DATE_DESC = 0,
    DATE_ASC = 1,
    AMOUNT_DESC = 2,
    AMOUNT_ASC = 3
};

class DashboardWidget : public PWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(KRISTATECHGUI* _window);
    ~DashboardWidget();

    void loadClientModel() override;
    void loadWalletModel() override;
    void shutdownMinerWorkers();
    QWidget* getMinerHubPage() const;
    void setMinerHubFullScreenMode(bool enable);
    bool isMinerHubFullScreenMode() const;

public Q_SLOTS:
    void walletSynced(bool isSync);
    void processNewTransaction(const QModelIndex& parent, int start, int /*end*/);

Q_SIGNALS:
    void incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address);

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private Q_SLOTS:
    void handleTransactionClicked(const QModelIndex &index);
    void changeTheme(bool isLightTheme, QString &theme) override;
    void onSortChanged(const QString&);
    void onSortTypeChanged(const QString& value);
    void onSearchTxChanged(const QString& query);
    void updateDisplayUnit();
    void showList();
    void onTxArrived(const QString& hash, const bool& isCoinStake);
    void refreshOverviewStats();

private:
    FurAbstractListItemDelegate* txViewDelegate = nullptr;
    TransactionFilterProxy* filter = nullptr;
    TxViewHolder* txHolder = nullptr;
    TransactionTableModel* txModel = nullptr;
    int nDisplayUnit = 0;
    bool isSync = false;
    bool lastStakingActive = false;
    bool overviewDirty = true;
    bool hasCachedNodeSnapshot = false;
    int cachedMnEnabled = 0;
    size_t cachedPoolTotal = 0;
    size_t cachedMnPoolCount = 0;
    size_t cachedClPoolCount = 0;
    size_t cachedPowPoolCount = 0;
    bool hasCachedMyMnSnapshot = false;
    bool cachedMyMnHasDip3 = false;
    int cachedMyMnTotal = 0;
    int cachedMyMnActive = 0;
    int cachedMyMnBanned = 0;
    int cachedMyMnPenalized = 0;

    MinerHubWidget* minerHubWidget = nullptr;

    // Header Title
    QLabel* lblHeaderTitle = nullptr;
    QLabel* lblHeaderSubtitle = nullptr;

    // Sync Warning Banner
    QFrame* warningBanner = nullptr;
    QLabel* lblWarning = nullptr;

    // 4-Card KPI Strip
    QFrame* cardKpiTotal = nullptr;
    QLabel* lblKpiTotalTitle = nullptr;
    QLabel* lblKpiTotalVal = nullptr;
    QLabel* lblKpiTotalSub = nullptr;

    QFrame* cardKpiSpendable = nullptr;
    QLabel* lblKpiSpendableTitle = nullptr;
    QLabel* lblKpiSpendableVal = nullptr;
    QLabel* lblKpiSpendableSub = nullptr;

    QFrame* cardKpiLocked = nullptr;
    QLabel* lblKpiLockedTitle = nullptr;
    QLabel* lblKpiLockedVal = nullptr;
    QLabel* lblKpiLockedSub = nullptr;

    QFrame* cardKpiImmature = nullptr;
    QLabel* lblKpiImmatureTitle = nullptr;
    QLabel* lblKpiImmatureVal = nullptr;
    QLabel* lblKpiImmatureSub = nullptr;

    // Left Card: Categorized Recent Transactions
    QFrame* txCard = nullptr;
    QLabel* lblTxTitle = nullptr;
    QLabel* lblTxCount = nullptr;
    QPushButton* btnTxFilterAll = nullptr;
    QPushButton* btnTxFilterRewards = nullptr;
    QPushButton* btnTxFilterReceived = nullptr;
    QPushButton* btnTxFilterSent = nullptr;
    QComboBox* comboBoxSortType = nullptr;
    QComboBox* comboBoxSort = nullptr;
    QLineEdit* lineEditSearchTx = nullptr;
    QListView* listTransactions = nullptr;
    QWidget* emptyContainer = nullptr;
    QLabel* lblEmpty = nullptr;
    QPushButton* btnLearnMore = nullptr;
    int quickTxFilterMode = 0; // 0 = All, 1 = Rewards, 2 = Received, 3 = Sent

    // Right Column Card 1: Earnings Snapshot
    QFrame* earningsCard = nullptr;
    QLabel* lblEarningsTitle = nullptr;
    QLabel* lblEarningsPeriodBadge = nullptr;
    QLabel* lblEarn24hTitle = nullptr;
    QLabel* lblEarn24hVal = nullptr;
    QLabel* lblEarn7dTitle = nullptr;
    QLabel* lblEarn7dVal = nullptr;
    HomeSplitBar* earningsSplitBar = nullptr;
    QLabel* lblSplitPosDot = nullptr;
    QLabel* lblSplitPosKey = nullptr;
    QLabel* lblSplitPosVal = nullptr;
    QLabel* lblSplitMnDot = nullptr;
    QLabel* lblSplitMnKey = nullptr;
    QLabel* lblSplitMnVal = nullptr;
    QLabel* lblSplitAdamDot = nullptr;
    QLabel* lblSplitAdamKey = nullptr;
    QLabel* lblSplitAdamVal = nullptr;
    QPushButton* btnOpenAnalytics = nullptr;

    // Right Column Card 2: Node & Consensus Status
    QFrame* nodeCard = nullptr;
    QLabel* lblNodeTitle = nullptr;
    QLabel* lblNodeSyncPill = nullptr;
    QLabel* lblNodeHeightKey = nullptr;
    QLabel* lblNodeHeightVal = nullptr;
    QLabel* lblNodePeersKey = nullptr;
    QLabel* lblNodePeersVal = nullptr;
    QLabel* lblNodeMnsKey = nullptr;
    QLabel* lblNodeMnsVal = nullptr;
    QLabel* lblNodePoolKey = nullptr;
    QLabel* lblNodePoolVal = nullptr;
    QLabel* lblNodeStakingKey = nullptr;
    QLabel* lblNodeStakingVal = nullptr;
    QPushButton* btnNodeTelemetry = nullptr;

    // Right Column Card 3: My Masternodes Summary
    QFrame* masternodesCard = nullptr;
    QLabel* lblMnCardTitle = nullptr;
    QLabel* lblMnCardPill = nullptr;
    QLabel* lblMnCountKey = nullptr;
    QLabel* lblMnCountVal = nullptr;
    QLabel* lblMnPoseKey = nullptr;
    QLabel* lblMnPoseVal = nullptr;
    QLabel* lblMnColKey = nullptr;
    QLabel* lblMnColVal = nullptr;
    QLabel* lblMnNextColKey = nullptr;
    QLabel* lblMnNextColVal = nullptr;
    QLabel* lblMnNextBlocksKey = nullptr;
    QLabel* lblMnNextBlocksVal = nullptr;
    QPushButton* btnOpenMasternodes = nullptr;

    // Right Column Container & Drag-and-Drop Reorder State
    QWidget* rightCol = nullptr;
    QVBoxLayout* rightLayout = nullptr;
    QVector<QFrame*> rightCards;
    QVector<HomeCardDragGrip*> rightCardGrips;
    QFrame* draggingCard = nullptr;
    HomeDragGhostWidget* dragGhost = nullptr;
    HomeDragSlotMask* dragSlotMask = nullptr;
    QPoint dragPressGlobalPos;
    int dragCardStartY = 0;
    bool dragActive = false;
    bool dragOrderChanged = false;
    QStringList dragOriginalOrder;

    QTimer* coalesceTimer = nullptr;
    QTimer* liveAgeTimer = nullptr;

    void initMinerHub();
    void applyTheme(bool isLight);
    void applyOverviewThemeColors(bool isLight);
    void refreshLiveNodeStatus();
    void scheduleOverviewRefresh();
    void applyQuickTxFilter(int mode);
    void changeSort(int nSortIndex);
    void registerRightCardDragTarget(QFrame* card);
    QFrame* findRightCardForObject(QObject* obj) const;
    QStringList currentRightCardOrder() const;
    void applyRightCardOrder(const QStringList& order);
    void saveRightCardOrder() const;
    void beginCardDrag();
    void updateCardDrag(int globalY);
    void finishCardDrag(bool cancel);
    QString formatAmountPlain(CAmount amount, bool compactZero = false) const;
};

#endif // DASHBOARDWIDGET_H
