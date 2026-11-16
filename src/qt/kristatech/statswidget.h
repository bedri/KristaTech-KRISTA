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
#include <QTimer>
#include <QVector>

#if defined(HAVE_CONFIG_H)
#include "config/kristatech-config.h"
#endif

#ifdef USE_QTCHARTS
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QStackedBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
QT_CHARTS_USE_NAMESPACE
using namespace QtCharts;
#endif

class KRISTATECHGUI;
class WalletModel;
class ClientModel;

class StatsWidget : public PWidget
{
    Q_OBJECT

public:
    explicit StatsWidget(KRISTATECHGUI* parent = nullptr);
    ~StatsWidget();

    void loadWalletModel() override;
    void loadClientModel() override;

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
        CAmount last24h = 0;
        CAmount last7d = 0;
        CAmount last30d = 0;
        CAmount totalSent = 0;
        CAmount totalReceived = 0;
        int countTotalTx = 0;
        int countPos = 0;
        int countMnLlmq = 0;
        int countAdam = 0;

        QStringList labels;
        QVector<double> posSeries;
        QVector<double> mnAdamSeries;
    };

    void setupUiLayout();
    void updateThemeStyles();
    RewardStatsSnapshot collectRewardSnapshot() const;
    void renderRewardChart(const RewardStatsSnapshot& snap);
    QString formatAmountPlain(CAmount amount, bool compact = false) const;
    QString formatDurationSeconds(qint64 seconds) const;

    int periodMode = 1; // 0 = Daily (30D), 1 = Monthly (12M), 2 = All Years
    int chartMode = 0;  // 0 = Stacked Bars, 1 = Cumulative Trend, 2 = Donut Split
    int displayUnit = 0;

    QTimer* refreshTimer = nullptr;
    QScrollArea* scrollArea = nullptr;
    QWidget* scrollContent = nullptr;

    // Header
    QLabel* lblTitle = nullptr;
    QLabel* lblSubtitle = nullptr;
    QLabel* lblSyncBadge = nullptr;
    QPushButton* btnRefresh = nullptr;

    // Top 4 KPI Cards
    QFrame* cardKpiBalance = nullptr;
    QLabel* lblKpiBalanceTitle = nullptr;
    QLabel* lblKpiBalanceVal = nullptr;
    QLabel* lblKpiBalanceSub = nullptr;

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

    // Middle Left: Chart Card
    QFrame* cardChart = nullptr;
    QLabel* lblChartSectionTitle = nullptr;
    QPushButton* btnPeriod30D = nullptr;
    QPushButton* btnPeriod12M = nullptr;
    QPushButton* btnPeriodAll = nullptr;
    QPushButton* btnViewBars = nullptr;
    QPushButton* btnViewTrend = nullptr;
    QPushButton* btnViewSplit = nullptr;
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

    // Middle Right: Network & Consensus Card
    QFrame* cardNetworkConsensus = nullptr;
    QLabel* lblNetSectionTitle = nullptr;
    QLabel* lblSupplyText = nullptr;
    QProgressBar* barSupply = nullptr;
    QLabel* lblDecayText = nullptr;
    QProgressBar* barDecay = nullptr;
    QVector<QLabel*> infoRowKeys;
    QVector<QLabel*> infoRowVals;

    // Bottom Left: Balance Breakdown Card
    QFrame* cardBalanceBreakdown = nullptr;
    QLabel* lblBalSectionTitle = nullptr;
    QLabel* lblBalAvailVal = nullptr;
    QLabel* lblBalLockedVal = nullptr;
    QLabel* lblBalImmatureVal = nullptr;
    QLabel* lblBalPendingVal = nullptr;
    QProgressBar* barAvailPct = nullptr;
    QProgressBar* barLockedPct = nullptr;

    // Bottom Right: Wallet Activity Card
    QFrame* cardActivity = nullptr;
    QLabel* lblActSectionTitle = nullptr;
    QLabel* lblActTxCountVal = nullptr;
    QLabel* lblActPosVal = nullptr;
    QLabel* lblActMnVal = nullptr;
    QLabel* lblActAdamVal = nullptr;
    QLabel* lblActSentRecvVal = nullptr;
};

#endif // STATSWIDGET_H
