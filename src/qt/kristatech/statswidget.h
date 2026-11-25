// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef STATSWIDGET_H
#define STATSWIDGET_H

#include "qt/kristatech/pwidget.h"
#include "amount.h"

#include <QFrame>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStringList>
#include <QTimer>
#include <QVector>

#if defined(HAVE_CONFIG_H)
#include "config/kristatech-config.h"
#endif

#ifdef USE_QTCHARTS
#include <QtCharts/QAreaSeries>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QValueAxis>
QT_CHARTS_USE_NAMESPACE
using namespace QtCharts;
#endif

class KRISTATECHGUI;
class WalletModel;
class ClientModel;
class SegmentedBarWidget;

class StatsWidget : public PWidget
{
    Q_OBJECT

public:
    explicit StatsWidget(KRISTATECHGUI* parent = nullptr);
    ~StatsWidget();

    void loadWalletModel() override;
    void loadClientModel() override;
    void showView(int viewIdx);

public Q_SLOTS:
    void refreshStats();
    void changeTheme(bool isLightTheme, QString& theme) override;

protected:
    void showEvent(QShowEvent* event) override;

private:
    struct RewardStatsSnapshot {
        CAmount totalPos = 0;
        CAmount totalMnLlmq = 0;
        CAmount totalAdam = 0;
        CAmount totalGenerated = 0;

        CAmount periodPos = 0;
        CAmount periodMnLlmq = 0;
        CAmount periodAdamPow = 0;

        CAmount last24h = 0;
        CAmount last7d = 0;
        CAmount last30d = 0;
        CAmount totalSent = 0;
        CAmount totalReceived = 0;
        CAmount totalFees = 0;

        int countTotalTx = 0;
        int countExcludedOrphan = 0;
        int countPos = 0;
        int countMnLlmq = 0;
        int countAdam = 0;
        int countGenerated = 0;

        QStringList labels;
        QStringList tooltipLabels;
        QVector<double> posSeries;
        QVector<double> mnLlmqSeries;
        QVector<double> adamPowSeries;
        QVector<CAmount> posSats;
        QVector<CAmount> mnLlmqSats;
        QVector<CAmount> adamPowSats;
    };

    struct LockedBreakdown {
        bool resolved = false;
        CAmount mnCollateralAmount = 0;
        int mnCollateralCount = 0;
        CAmount minerCltvAmount = 0;
        int coinLock250Count = 0;
        int powLock10Count = 0;
        CAmount otherManualAmount = 0;
        int otherManualCount = 0;
    };

    struct TelemetrySnapshot {
        bool hasChain = false;
        qint64 targetSpacingSec = 30;
        QVector<int> heights;
        QVector<double> difficulties;
        QVector<qint64> blockTimesSec;
        QVector<qint64> blockDeltasSec;
        QVector<int> blockTxCounts;

        double avgBlockTimeSec = 0.0;
        double medianBlockTimeSec = 0.0;
        double currentDifficulty = 0.0;
        double diffMin = 0.0;
        double diffMax = 0.0;
        double diffChangePct = 0.0;
        int deltaBuckets[6] = {0, 0, 0, 0, 0, 0}; // <15s, 15-25s, 25-35s, 35-50s, 50-90s, >90s

        int recent60TxTotal = 0;
        int recent60Blocks = 0;
        int recent60PeakTx = 0;
        qint64 recent60SpanSec = 0;

        bool hasMempool = false;
        int mempoolTxCount = 0;
        uint64_t mempoolTotalBytes = 0;
        size_t mempoolDynamicBytes = 0;
        double medianFeeSatPerByte = 0.0;
        int feeBuckets[5] = {0, 0, 0, 0, 0}; // <1, 1-5, 5-20, 20-50, 50+ sat/B
    };

    struct OverviewPoolSnapshot {
        bool valid = false;
        int height = 0;
        CAmount moneySupply = 0;
        double networkHashPs = 0.0;
        size_t poolTotal = 0;
        size_t mnPoolCount = 0;
        size_t coinLockCount = 0;
        size_t powPoolCount = 0;
        int mnEnabled = 0;
    };

    void setupUiLayout();
    QWidget* buildOverviewPage();
    QWidget* buildTelemetryPage();
    void updateThemeStyles();
    void setPeriodMode(int p);

    RewardStatsSnapshot collectRewardSnapshot() const;
    LockedBreakdown collectLockedBreakdown(CAmount expectedLockedTotal) const;
    TelemetrySnapshot collectTelemetrySnapshot() const;

    void renderRewardChart(const RewardStatsSnapshot& snap);
    void renderTelemetryCharts(const TelemetrySnapshot& tel);

    QString formatAmountPlain(CAmount amount, bool compact = false) const;
    QString formatHashRate(double hashesPerSec) const;
    QString formatDifficulty(double diff) const;
    QString formatBytes(uint64_t bytes) const;
    QString formatDurationSeconds(qint64 seconds) const;

    int activeView = 0; // 0 = Overview, 1 = Network Telemetry
    int periodMode = 1; // 0 = 24H, 1 = 7D (default), 2 = 30D, 3 = 12M, 4 = All
    int chartMode = 0;  // 0 = Bars, 1 = Trend, 2 = Split
    int displayUnit = 0;

    QTimer* refreshTimer = nullptr;
    QTimer* retryTimer = nullptr;
    TelemetrySnapshot m_lastGoodTelemetry;
    LockedBreakdown m_lastGoodLocked;
    OverviewPoolSnapshot m_lastGoodPool;

    // Header
    QLabel* lblTitle = nullptr;
    QLabel* lblSubtitle = nullptr;
    QFrame* viewSwitchFrame = nullptr;
    QPushButton* btnTabOverview = nullptr;
    QPushButton* btnTabTelemetry = nullptr;
    QPushButton* btnRefresh = nullptr;

    QStackedWidget* viewStack = nullptr;
    QScrollArea* scrollOverview = nullptr;
    QWidget* overviewContent = nullptr;
    QScrollArea* scrollTelemetry = nullptr;
    QWidget* telemetryContent = nullptr;

    // Overview Top 4 KPI Cards
    QFrame* cardKpiBalance = nullptr;
    QLabel* lblKpiBalanceTitle = nullptr;
    QLabel* lblKpiBalanceVal = nullptr;
    QLabel* lblKpiBalanceSub = nullptr;
    SegmentedBarWidget* barKpiBalance = nullptr;

    QFrame* cardKpiRewards = nullptr;
    QLabel* lblKpiRewardsTitle = nullptr;
    QLabel* lblKpiRewardsVal = nullptr;
    QLabel* lblKpiRewardsSub = nullptr;

    QFrame* cardKpiNetwork = nullptr;
    QLabel* lblKpiNetworkTitle = nullptr;
    QLabel* lblKpiNetworkVal = nullptr;
    QLabel* lblKpiNetworkSub = nullptr;

    QFrame* cardKpiEmission = nullptr;
    QLabel* lblKpiEmissionTitle = nullptr;
    QLabel* lblKpiEmissionVal = nullptr;
    QLabel* lblKpiEmissionSub = nullptr;

    // Overview Left Column: Reward Analytics Chart
    QFrame* cardChart = nullptr;
    QLabel* lblChartSectionTitle = nullptr;
    QPushButton* btnPeriod24H = nullptr;
    QPushButton* btnPeriod7D = nullptr;
    QPushButton* btnPeriod30D = nullptr;
    QPushButton* btnPeriod12M = nullptr;
    QPushButton* btnPeriodAll = nullptr;
    QPushButton* btnViewBars = nullptr;
    QPushButton* btnViewTrend = nullptr;
    QPushButton* btnViewSplit = nullptr;

    QLabel* lblLegPos = nullptr;
    QLabel* lblLegMn = nullptr;
    QLabel* lblLegAdam = nullptr;
    QLabel* lblChartPeriodTotal = nullptr;

    QPushButton* btnWin24h = nullptr;
    QPushButton* btnWin7d = nullptr;
    QPushButton* btnWin30d = nullptr;
    QPushButton* btnWinAll = nullptr;
    QLabel* lblWin24hTitle = nullptr;
    QLabel* lblWin24hVal = nullptr;
    QLabel* lblWin7dTitle = nullptr;
    QLabel* lblWin7dVal = nullptr;
    QLabel* lblWin30dTitle = nullptr;
    QLabel* lblWin30dVal = nullptr;
    QLabel* lblWinAllTitle = nullptr;
    QLabel* lblWinAllVal = nullptr;

#ifdef USE_QTCHARTS
    QChart* chart = nullptr;
    QChartView* chartView = nullptr;
    QBarCategoryAxis* axisX = nullptr;
    QValueAxis* axisY = nullptr;
#endif
    QLabel* lblChartFallback = nullptr;

    // Overview Left Bottom Cards
    QFrame* cardBalanceBreakdown = nullptr;
    QLabel* lblBalSectionTitle = nullptr;
    QLabel* lblBalAvailVal = nullptr;
    QLabel* lblBalMnColVal = nullptr;
    QLabel* lblBalCltvVal = nullptr;
    QLabel* lblBalOtherLockKey = nullptr;
    QLabel* lblBalOtherLockVal = nullptr;
    QFrame* dividerOtherLock = nullptr;
    QLabel* lblBalImmatureVal = nullptr;
    QLabel* lblBalPendingVal = nullptr;

    QFrame* cardActivity = nullptr;
    QLabel* lblActSectionTitle = nullptr;
    QLabel* lblActPosVal = nullptr;
    QLabel* lblActMnVal = nullptr;
    QLabel* lblActAdamVal = nullptr;
    QLabel* lblActGenVal = nullptr;
    QLabel* lblActSentRecvVal = nullptr;
    QLabel* lblActTxCountVal = nullptr;

    // Overview Right Column: Supply, Emission & Model D
    QFrame* cardSupplyEmission = nullptr;
    QLabel* lblSupplySectionTitle = nullptr;
    QLabel* lblSupplyText = nullptr;
    QProgressBar* barSupply = nullptr;
    QLabel* lblDecayText = nullptr;
    QProgressBar* barDecay = nullptr;
    QLabel* lblModelDSplitTitle = nullptr;
    SegmentedBarWidget* barModelDSplit = nullptr;
    QLabel* lblSplitAdamKey = nullptr;
    QLabel* lblSplitMnKey = nullptr;
    QLabel* lblSplitPosKey = nullptr;
    QLabel* lblSplitLlmqKey = nullptr;
    QLabel* lblSplitDevKey = nullptr;
    QLabel* lblSplitAdamVal = nullptr;
    QLabel* lblSplitMnVal = nullptr;
    QLabel* lblSplitPosVal = nullptr;
    QLabel* lblSplitLlmqVal = nullptr;
    QLabel* lblSplitDevVal = nullptr;

    // Overview Right Column: Consensus Pool & Local Node
    QFrame* cardPoolNode = nullptr;
    QLabel* lblPoolSectionTitle = nullptr;
    QLabel* lblPoolMpaVal = nullptr;
    QLabel* lblPoolMnCountVal = nullptr;
    QLabel* lblNetHashVal = nullptr;
    QLabel* lblLocalCpuKey = nullptr;
    QLabel* lblLocalCpuVal = nullptr;
    QLabel* lblExternalMinersVal = nullptr;
    QLabel* lblStakingVal = nullptr;

    // Network Telemetry Page: Top 4 KPIs
    QFrame* cardTelBlockTime = nullptr;
    QLabel* lblTelBlockTimeTitle = nullptr;
    QLabel* lblTelBlockTimeVal = nullptr;
    QLabel* lblTelBlockTimeSub = nullptr;

    QFrame* cardTelDiff = nullptr;
    QLabel* lblTelDiffTitle = nullptr;
    QLabel* lblTelDiffVal = nullptr;
    QLabel* lblTelDiffSub = nullptr;

    QFrame* cardTelMempool = nullptr;
    QLabel* lblTelMempoolTitle = nullptr;
    QLabel* lblTelMempoolVal = nullptr;
    QLabel* lblTelMempoolSub = nullptr;

    QFrame* cardTelThroughput = nullptr;
    QLabel* lblTelThroughputTitle = nullptr;
    QLabel* lblTelThroughputVal = nullptr;
    QLabel* lblTelThroughputSub = nullptr;

    // Network Telemetry Charts
    QFrame* cardDiffChart = nullptr;
    QLabel* lblDiffChartTitle = nullptr;
    QLabel* lblDiffChartMeta = nullptr;
    QFrame* cardBlockTimeChart = nullptr;
    QLabel* lblBlockTimeChartTitle = nullptr;
    QLabel* lblBlockTimeChartMeta = nullptr;
    QFrame* cardMempoolChart = nullptr;
    QLabel* lblMempoolChartTitle = nullptr;
    QLabel* lblMempoolChartBadge = nullptr;

#ifdef USE_QTCHARTS
    QChart* diffChart = nullptr;
    QChartView* diffChartView = nullptr;
    QValueAxis* diffAxisX = nullptr;
    QValueAxis* diffAxisY = nullptr;

    QChart* blockTimeChart = nullptr;
    QChartView* blockTimeChartView = nullptr;
    QBarCategoryAxis* blockTimeAxisX = nullptr;
    QValueAxis* blockTimeAxisY = nullptr;

    QChart* mempoolChart = nullptr;
    QChartView* mempoolChartView = nullptr;
    QBarCategoryAxis* mempoolBarAxisX = nullptr;
    QValueAxis* mempoolValAxisX = nullptr;
    QValueAxis* mempoolAxisY = nullptr;
#endif
    QLabel* lblTelDiffFallback = nullptr;
    QLabel* lblTelTimeFallback = nullptr;
    QLabel* lblTelMempoolFallback = nullptr;

    // Theme-tracked collections
    QVector<QFrame*> themedCards;
    QVector<QFrame*> rowDividers;
    QVector<QFrame*> subtleBoxes;
    QVector<QLabel*> kpiTitles;
    QVector<QLabel*> kpiVals;
    QVector<QLabel*> kpiSubs;
    QVector<QLabel*> sectionTitles;
    QVector<QLabel*> infoRowKeys;
    QVector<QLabel*> infoRowVals;
};

#endif // STATSWIDGET_H
