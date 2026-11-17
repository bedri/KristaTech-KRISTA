// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/furabstractlistitemdelegate.h"
#include "qt/kristatech/furlistrow.h"
#include "transactiontablemodel.h"
#include "qt/kristatech/txviewholder.h"
#include "transactionfilterproxy.h"

#include <QWidget>
#include <QLineEdit>
#include <QMap>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

#if defined(HAVE_CONFIG_H)
#include "config/kristatech-config.h" /* for USE_QTCHARTS */
#endif

#ifdef USE_QTCHARTS

#include <QtCharts/QChartView>
#include <QtCharts/QBarSeries>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QValueAxis>

QT_CHARTS_USE_NAMESPACE

using namespace QtCharts;

#endif

class KRISTATECHGUI;
class MinerHubWidget;
class WalletModel;

namespace Ui {
class DashboardWidget;
}

enum SortTx {
    DATE_DESC = 0,
    DATE_ASC = 1,
    AMOUNT_DESC = 2,
    AMOUNT_ASC = 3
};

enum ChartShowType {
    ALL,
    YEAR,
    MONTH,
    DAY
};

class ChartData {
public:
    ChartData() {}

	QMap<int, QMap<QString, qint64>> amountsByCache;
    qreal maxValue = 0;
    qint64 totalPiv = 0;
	qint64 totalMNRewards = 0;
    QList<qreal> valuesPiv;
	QList<qreal> valuesMNRewards;
    QStringList xLabels;
};

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class DashboardWidget : public PWidget
{
    Q_OBJECT

public:
    explicit DashboardWidget(KRISTATECHGUI* _window);
    ~DashboardWidget();

    void loadWalletModel() override;
    void loadChart();
    void shutdownMinerWorkers();
    QWidget* getMinerHubPage() const;
    void setMinerHubFullScreenMode(bool enable);
    bool isMinerHubFullScreenMode() const;

    void run(int type) override;
    void onError(QString error, int type) override;

public Q_SLOTS:
    void walletSynced(bool isSync);
    /**
     * Show incoming transaction notification for new transactions.
     * The new items are those between start and end inclusive, under the given parent item.
    */
    void processNewTransaction(const QModelIndex& parent, int start, int /*end*/);
Q_SIGNALS:
    /** Notify that a new transaction appeared */
    void incomingTransaction(const QString& date, int unit, const CAmount& amount, const QString& type, const QString& address);
private Q_SLOTS:
    void handleTransactionClicked(const QModelIndex &index);
    void changeTheme(bool isLightTheme, QString &theme) override;
    void onSortChanged(const QString&);
    void onSortTypeChanged(const QString& value);
    void onSearchTxChanged(const QString& query);
    void updateDisplayUnit();
    void showList();
    void onTxArrived(const QString& hash, const bool& isCoinStake);
    void onToggleMinerHubCollapseClicked();
    void onToggleMinerHubVisibilityClicked();

#ifdef USE_QTCHARTS
    void windowResizeEvent(QResizeEvent* event);
    void changeChartColors();
    void onChartYearChanged(const QString&);
    void onChartMonthChanged(const QString&);
    void onChartArrowClicked(bool goLeft);
#endif

private:
    Ui::DashboardWidget *ui;
    FurAbstractListItemDelegate* txViewDelegate;
    TransactionFilterProxy* filter;
    TxViewHolder* txHolder;
    TransactionTableModel* txModel;
    int nDisplayUnit = -1;
    bool isSync = false;
    void changeSort(int nSortIndex);

    // 1-Click Cooperative Miner & Validator Hub (Dedicated Top-Level Page)
    MinerHubWidget* minerHubWidget = nullptr;
    QLineEdit* lineEditSearchTx = nullptr;
    QPushButton* btnToggleMinerHubVisibility = nullptr;
    bool fMinerHubCollapsed = false;
    bool fMinerHubHidden = false;
    bool fChartEmptyDesired = true;
    QPushButton* btnCloseChart = nullptr;
    QPushButton* btnShowChart = nullptr;
    QFrame* frameChartCardStaking = nullptr;
    QFrame* frameChartCardMn = nullptr;
    QFrame* frameChartCardTotal = nullptr;
    QLabel* lblChartCardStakingTitle = nullptr;
    QLabel* lblChartCardMnTitle = nullptr;
    QLabel* lblChartCardTotalTitle = nullptr;
    QLabel* lblChartTotalAmount = nullptr;
    QLabel* lblChartCardStakingShare = nullptr;
    QLabel* lblChartCardMnShare = nullptr;
    QLabel* lblChartCardTotalMeta = nullptr;
    QFrame* chartTopToolbarFrame = nullptr;
    QPushButton* btnPeriodDaily = nullptr;
    QPushButton* btnPeriodMonthly = nullptr;
    QPushButton* btnPeriodAll = nullptr;
    QPushButton* btnModeBars = nullptr;
    QPushButton* btnModeTrend = nullptr;
    QPushButton* btnModeSplit = nullptr;
    int chartVisualMode = 0; // 0 = StackedBars, 1 = CumulativeTrend, 2 = DonutSplit
    void initMinerHub();
    void updateMinerHubLayoutState();
    int chartSeriesFilter = 0; // 0 = All, 1 = Staking (PoS), 2 = MN & Mining
    QString formatCompactKrista(CAmount amount, int unit) const;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void initRewardChartUx();
    void updateRewardChartTheme();
    void setChartVisible(bool visible, bool persist = true);

#ifdef USE_QTCHARTS

    int64_t lastRefreshTime = 0;
    std::atomic<bool> isLoading;

    // Chart
    TransactionFilterProxy* stakesFilter = nullptr;
    bool isChartInitialized = false;
    QChartView *chartView = nullptr;
    QBarSeries *series = nullptr;
    QStackedBarSeries *stackedSeries = nullptr;
    QBarSet *set0 = nullptr;
    QBarSet *set1 = nullptr;

    QBarCategoryAxis *axisX = nullptr;
    QValueAxis *axisY = nullptr;

    QChart *chart = nullptr;
    bool isChartMin = false;
    ChartShowType chartShow = YEAR;
    int yearFilter = 0;
    int monthFilter = 0;
    int dayStart = 1;
	bool hasMNRewards = false;

    ChartData* chartData = nullptr;
    bool hasStakes = false;
    bool fShowCharts = true;

    void initChart();
    void showHideEmptyChart(bool show, bool loading, bool forceView = false);
    bool refreshChart();
    void tryChartRefresh();
    void updateStakeFilter();
    const QMap<int, QMap<QString, qint64>> getAmountBy();
    bool loadChartData(bool withMonthNames);
    void updateAxisX(const QStringList *arg = nullptr);
    void setChartShow(ChartShowType type);
    std::pair<int, int> getChartRange(QMap<int, QMap<QString, qint64>> amountsBy);

private Q_SLOTS:
    void onChartRefreshed();
    void onHideChartsChanged(bool fHide);

#endif

};

#endif // DASHBOARDWIDGET_H
