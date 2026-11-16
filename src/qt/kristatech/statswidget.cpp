// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/statswidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "transactiontablemodel.h"
#include "transactionrecord.h"
#include "walletmodel.h"

#include "adam.h"
#include "chainparams.h"
#include "main.h"
#include "masternode.h"
#include "masternodeman.h"

#include <QDate>
#include <QDateTime>
#include <QGraphicsLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QMap>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

StatsWidget::StatsWidget(KRISTATECHGUI* parent) :
    PWidget(parent)
{
    setupUiLayout();
    updateThemeStyles();

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(10000);
    connect(refreshTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refreshStats();
        }
    });
    refreshTimer->start();
}

StatsWidget::~StatsWidget()
{
#ifdef USE_QTCHARTS
    delete chart;
#endif
}

void StatsWidget::setupUiLayout()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 14, 20, 14);
    rootLayout->setSpacing(12);

    // Header Row
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);
    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    lblTitle = new QLabel(tr("Wallet & Network Statistics"), this);
    lblSubtitle = new QLabel(tr("Live portfolio breakdown, reward analytics, Dual-Track MPA pool, and emission metrics"), this);
    titleCol->addWidget(lblTitle);
    titleCol->addWidget(lblSubtitle);
    headerRow->addLayout(titleCol, 1);

    lblSyncBadge = new QLabel(tr("Syncing"), this);
    lblSyncBadge->setAlignment(Qt::AlignCenter);
    lblSyncBadge->setFixedHeight(28);
    headerRow->addWidget(lblSyncBadge, 0, Qt::AlignVCenter);

    btnRefresh = new QPushButton(tr("Refresh"), this);
    btnRefresh->setCursor(Qt::PointingHandCursor);
    btnRefresh->setFocusPolicy(Qt::NoFocus);
    btnRefresh->setFixedHeight(32);
    btnRefresh->setToolTip(tr("Refresh all wallet and network statistics"));
    connect(btnRefresh, &QPushButton::clicked, this, &StatsWidget::refreshStats);
    headerRow->addWidget(btnRefresh, 0, Qt::AlignVCenter);

    rootLayout->addLayout(headerRow);

    // Scroll Area for responsive vertical fit
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    scrollContent = new QWidget(scrollArea);
    QVBoxLayout* bodyLayout = new QVBoxLayout(scrollContent);
    bodyLayout->setContentsMargins(0, 0, 4, 4);
    bodyLayout->setSpacing(12);

    // Row 1: 4 KPI Cards
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(10);

    auto makeKpiCard = [&](QFrame*& card, QLabel*& titleLbl, QLabel*& valLbl, QLabel*& subLbl,
                           const QString& objName, const QString& title) {
        card = new QFrame(scrollContent);
        card->setObjectName(objName);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QVBoxLayout* col = new QVBoxLayout(card);
        col->setContentsMargins(14, 11, 14, 11);
        col->setSpacing(3);
        titleLbl = new QLabel(title, card);
        valLbl = new QLabel("--", card);
        subLbl = new QLabel("--", card);
        subLbl->setWordWrap(true);
        col->addWidget(titleLbl);
        col->addWidget(valLbl);
        col->addWidget(subLbl);
        kpiRow->addWidget(card, 1);
    };

    makeKpiCard(cardKpiBalance, lblKpiBalanceTitle, lblKpiBalanceVal, lblKpiBalanceSub,
                "cardKpiBalance", tr("TOTAL WALLET BALANCE"));
    makeKpiCard(cardKpiRewards, lblKpiRewardsTitle, lblKpiRewardsVal, lblKpiRewardsSub,
                "cardKpiRewards", tr("TOTAL REWARDS EARNED"));
    makeKpiCard(cardKpiNetwork, lblKpiNetworkTitle, lblKpiNetworkVal, lblKpiNetworkSub,
                "cardKpiNetwork", tr("BLOCK HEIGHT & NETWORK"));
    makeKpiCard(cardKpiEmission, lblKpiEmissionTitle, lblKpiEmissionVal, lblKpiEmissionSub,
                "cardKpiEmission", tr("CURRENT BLOCK REWARD"));

    bodyLayout->addLayout(kpiRow);

    // Row 2: Middle Analytics (60% Chart / 40% Consensus & Supply)
    QHBoxLayout* midRow = new QHBoxLayout();
    midRow->setSpacing(12);

    // Left: Wide Reward Chart Card
    cardChart = new QFrame(scrollContent);
    cardChart->setObjectName("cardStatsChart");
    cardChart->setMinimumHeight(310);
    QVBoxLayout* chartCol = new QVBoxLayout(cardChart);
    chartCol->setContentsMargins(14, 12, 14, 12);
    chartCol->setSpacing(8);

    QHBoxLayout* chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(6);
    lblChartSectionTitle = new QLabel(tr("Reward Analytics"), cardChart);
    chartToolbar->addWidget(lblChartSectionTitle);
    chartToolbar->addStretch(1);

    auto makePill = [&](const QString& text, const QString& tip) -> QPushButton* {
        QPushButton* b = new QPushButton(text, cardChart);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(26);
        b->setToolTip(tip);
        return b;
    };

    btnPeriod30D = makePill(tr("30D"), tr("Daily rewards over the last 30 days"));
    btnPeriod12M = makePill(tr("12M"), tr("Monthly rewards over the last 12 months"));
    btnPeriodAll = makePill(tr("All"), tr("Yearly rewards across all wallet history"));
    btnPeriod12M->setChecked(true);

    btnViewBars = makePill(tr("Bars"), tr("Stacked bar breakdown"));
    btnViewTrend = makePill(tr("Trend"), tr("Cumulative earnings curve"));
    btnViewSplit = makePill(tr("Split"), tr("Proportional donut split"));
    btnViewBars->setChecked(true);

    chartToolbar->addWidget(btnPeriod30D);
    chartToolbar->addWidget(btnPeriod12M);
    chartToolbar->addWidget(btnPeriodAll);
    chartToolbar->addSpacing(8);
    chartToolbar->addWidget(btnViewBars);
    chartToolbar->addWidget(btnViewTrend);
    chartToolbar->addWidget(btnViewSplit);
    chartCol->addLayout(chartToolbar);

    auto setPeriod = [this](int p) {
        periodMode = p;
        btnPeriod30D->setChecked(p == 0);
        btnPeriod12M->setChecked(p == 1);
        btnPeriodAll->setChecked(p == 2);
        refreshStats();
    };
    connect(btnPeriod30D, &QPushButton::clicked, this, [setPeriod]() { setPeriod(0); });
    connect(btnPeriod12M, &QPushButton::clicked, this, [setPeriod]() { setPeriod(1); });
    connect(btnPeriodAll, &QPushButton::clicked, this, [setPeriod]() { setPeriod(2); });

    auto setView = [this](int v) {
        chartMode = v;
        btnViewBars->setChecked(v == 0);
        btnViewTrend->setChecked(v == 1);
        btnViewSplit->setChecked(v == 2);
        refreshStats();
    };
    connect(btnViewBars, &QPushButton::clicked, this, [setView]() { setView(0); });
    connect(btnViewTrend, &QPushButton::clicked, this, [setView]() { setView(1); });
    connect(btnViewSplit, &QPushButton::clicked, this, [setView]() { setView(2); });

#ifdef USE_QTCHARTS
    chart = new QChart();
    chart->legend()->setVisible(false);
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    chart->setMargins({0, 0, 0, 0});
    chart->setBackgroundRoundness(0);
    chart->setAnimationOptions(QChart::SeriesAnimations);

    axisX = new QBarCategoryAxis();
    axisY = new QValueAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignRight);

    chartView = new QChartView(chart, cardChart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setRubberBand(QChartView::NoRubberBand);
    chartView->setMinimumHeight(200);
    chartCol->addWidget(chartView, 1);
#else
    lblChartFallback = new QLabel(tr("QtCharts not enabled in this build"), cardChart);
    lblChartFallback->setAlignment(Qt::AlignCenter);
    chartCol->addWidget(lblChartFallback, 1);
#endif

    // Bottom 4-window summary strip inside Chart card
    QHBoxLayout* winRow = new QHBoxLayout();
    winRow->setSpacing(8);
    auto makeWinBox = [&](QLabel*& tLbl, QLabel*& vLbl, const QString& title) {
        QFrame* box = new QFrame(cardChart);
        QVBoxLayout* c = new QVBoxLayout(box);
        c->setContentsMargins(6, 4, 6, 4);
        c->setSpacing(1);
        tLbl = new QLabel(title, box);
        tLbl->setAlignment(Qt::AlignCenter);
        vLbl = new QLabel("--", box);
        vLbl->setAlignment(Qt::AlignCenter);
        c->addWidget(tLbl);
        c->addWidget(vLbl);
        winRow->addWidget(box, 1);
    };
    makeWinBox(lblWin24hTitle, lblWin24hVal, tr("Last 24h"));
    makeWinBox(lblWin7dTitle, lblWin7dVal, tr("Last 7 Days"));
    makeWinBox(lblWin30dTitle, lblWin30dVal, tr("Last 30 Days"));
    makeWinBox(lblWinAllTitle, lblWinAllVal, tr("All Time"));
    chartCol->addLayout(winRow);

    midRow->addWidget(cardChart, 3);

    // Right: Supply, Consensus & Pool Card
    cardNetworkConsensus = new QFrame(scrollContent);
    cardNetworkConsensus->setObjectName("cardStatsConsensus");
    QVBoxLayout* netCol = new QVBoxLayout(cardNetworkConsensus);
    netCol->setContentsMargins(14, 12, 14, 12);
    netCol->setSpacing(8);

    lblNetSectionTitle = new QLabel(tr("Supply, Consensus & Dual-Track Pool"), cardNetworkConsensus);
    netCol->addWidget(lblNetSectionTitle);

    lblSupplyText = new QLabel(tr("Circulating Supply: -- / 210,000,000 KRISTA"), cardNetworkConsensus);
    barSupply = new QProgressBar(cardNetworkConsensus);
    barSupply->setRange(0, 1000);
    barSupply->setTextVisible(false);
    barSupply->setFixedHeight(8);
    netCol->addWidget(lblSupplyText);
    netCol->addWidget(barSupply);

    lblDecayText = new QLabel(tr("Next Emission Decay: --"), cardNetworkConsensus);
    barDecay = new QProgressBar(cardNetworkConsensus);
    barDecay->setRange(0, 1000);
    barDecay->setTextVisible(false);
    barDecay->setFixedHeight(8);
    netCol->addWidget(lblDecayText);
    netCol->addWidget(barDecay);

    QGridLayout* netGrid = new QGridLayout();
    netGrid->setHorizontalSpacing(10);
    netGrid->setVerticalSpacing(6);
    const QStringList netKeys = {
        tr("Dual-Track MPA Pool:"),
        tr("Active Masternodes:"),
        tr("PoS Staking Status:"),
        tr("Coordinator VRF:"),
        tr("Model D Split (93%):"),
        tr("Protocol / Target:")
    };
    for (int i = 0; i < netKeys.size(); ++i) {
        QLabel* k = new QLabel(netKeys.at(i), cardNetworkConsensus);
        QLabel* v = new QLabel("--", cardNetworkConsensus);
        v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        infoRowKeys.append(k);
        infoRowVals.append(v);
        netGrid->addWidget(k, i, 0);
        netGrid->addWidget(v, i, 1);
    }
    netCol->addLayout(netGrid);
    netCol->addStretch(1);

    midRow->addWidget(cardNetworkConsensus, 2);
    bodyLayout->addLayout(midRow);

    // Row 3: Bottom 2-Column Wallet Breakdown & Activity
    QHBoxLayout* botRow = new QHBoxLayout();
    botRow->setSpacing(12);

    // Bottom Left: Balance & Collateral Breakdown
    cardBalanceBreakdown = new QFrame(scrollContent);
    cardBalanceBreakdown->setObjectName("cardStatsBalance");
    QVBoxLayout* balCol = new QVBoxLayout(cardBalanceBreakdown);
    balCol->setContentsMargins(14, 12, 14, 12);
    balCol->setSpacing(7);

    lblBalSectionTitle = new QLabel(tr("Wallet Balance & Collateral Breakdown"), cardBalanceBreakdown);
    balCol->addWidget(lblBalSectionTitle);

    QGridLayout* balGrid = new QGridLayout();
    balGrid->setHorizontalSpacing(10);
    balGrid->setVerticalSpacing(5);
    auto addBalRow = [&](int row, const QString& keyText, QLabel*& valLbl) {
        QLabel* k = new QLabel(keyText, cardBalanceBreakdown);
        valLbl = new QLabel("--", cardBalanceBreakdown);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        infoRowKeys.append(k);
        infoRowVals.append(valLbl);
        balGrid->addWidget(k, row, 0);
        balGrid->addWidget(valLbl, row, 1);
    };
    addBalRow(0, tr("Available (Spendable):"), lblBalAvailVal);
    addBalRow(1, tr("Locked (MN 4,200 / Miner Locks):"), lblBalLockedVal);
    addBalRow(2, tr("Immature (<100 Confirmations):"), lblBalImmatureVal);
    addBalRow(3, tr("Pending (Unconfirmed):"), lblBalPendingVal);
    balCol->addLayout(balGrid);

    barAvailPct = new QProgressBar(cardBalanceBreakdown);
    barAvailPct->setRange(0, 1000);
    barAvailPct->setTextVisible(false);
    barAvailPct->setFixedHeight(7);
    barAvailPct->setToolTip(tr("Share of wallet balance currently spendable"));
    balCol->addWidget(barAvailPct);

    barLockedPct = new QProgressBar(cardBalanceBreakdown);
    barLockedPct->setRange(0, 1000);
    barLockedPct->setTextVisible(false);
    barLockedPct->setFixedHeight(7);
    barLockedPct->setToolTip(tr("Share of wallet balance locked in MN / Miner collateral"));
    balCol->addWidget(barLockedPct);
    balCol->addStretch(1);

    botRow->addWidget(cardBalanceBreakdown, 1);

    // Bottom Right: Wallet Activity & Reward Counts
    cardActivity = new QFrame(scrollContent);
    cardActivity->setObjectName("cardStatsActivity");
    QVBoxLayout* actCol = new QVBoxLayout(cardActivity);
    actCol->setContentsMargins(14, 12, 14, 12);
    actCol->setSpacing(7);

    lblActSectionTitle = new QLabel(tr("Wallet Activity & Reward Breakdown"), cardActivity);
    actCol->addWidget(lblActSectionTitle);

    QGridLayout* actGrid = new QGridLayout();
    actGrid->setHorizontalSpacing(10);
    actGrid->setVerticalSpacing(5);
    auto addActRow = [&](int row, const QString& keyText, QLabel*& valLbl) {
        QLabel* k = new QLabel(keyText, cardActivity);
        valLbl = new QLabel("--", cardActivity);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        infoRowKeys.append(k);
        infoRowVals.append(valLbl);
        actGrid->addWidget(k, row, 0);
        actGrid->addWidget(valLbl, row, 1);
    };
    addActRow(0, tr("Total Wallet Transactions:"), lblActTxCountVal);
    addActRow(1, tr("PoS Staking Rewards:"), lblActPosVal);
    addActRow(2, tr("Masternode & LLMQ Rewards:"), lblActMnVal);
    addActRow(3, tr("ADAM PoW Mining Rewards:"), lblActAdamVal);
    addActRow(4, tr("Total Sent / Received:"), lblActSentRecvVal);
    actCol->addLayout(actGrid);
    actCol->addStretch(1);

    botRow->addWidget(cardActivity, 1);
    bodyLayout->addLayout(botRow);

    scrollArea->setWidget(scrollContent);
    rootLayout->addWidget(scrollArea, 1);
}

void StatsWidget::loadWalletModel()
{
    if (!walletModel) return;
    if (walletModel->getOptionsModel()) {
        displayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, [this](int unit) {
            displayUnit = unit;
            refreshStats();
        });
    }
    connect(walletModel, &WalletModel::balanceChanged, this, [this]() {
        if (isVisible()) refreshStats();
    });
    refreshStats();
}

void StatsWidget::loadClientModel()
{
    if (!clientModel) return;
    connect(clientModel, &ClientModel::numBlocksChanged, this, [this]() {
        if (isVisible()) refreshStats();
    });
    connect(clientModel, &ClientModel::numConnectionsChanged, this, [this]() {
        if (isVisible()) refreshStats();
    });
    refreshStats();
}

void StatsWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    refreshStats();
}

void StatsWidget::changeTheme(bool /*isLightTheme*/, QString& /*theme*/)
{
    updateThemeStyles();
    refreshStats();
}

void StatsWidget::updateThemeStyles()
{
    const bool light = isLightTheme();
    const QString pageBg = light ? QStringLiteral("#F4F6FA") : QStringLiteral("#0B111E");
    const QString cardBg = light ? QStringLiteral("#FFFFFF") : QStringLiteral("#131C2E");
    const QString cardBorder = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#23324D");
    const QString textPrimary = light ? QStringLiteral("#0F172A") : QStringLiteral("#F1F5F9");
    const QString textMuted = light ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString pillBg = light ? QStringLiteral("#F8FAFC") : QStringLiteral("#19253B");
    const QString trackBg = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#1F2D46");

    this->setStyleSheet(QString("StatsWidget { background-color: %1; }").arg(pageBg));
    if (scrollArea) scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    if (scrollContent) scrollContent->setStyleSheet("QWidget { background: transparent; }");

    if (lblTitle) lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 20px; font-weight: bold; }").arg(textPrimary));
    if (lblSubtitle) lblSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textMuted));

    if (btnRefresh) {
        btnRefresh->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1.5px solid #7695CD; border-radius: 6px; padding: 0px 14px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #7695CD; color: #FFFFFF; }"
            "QPushButton:pressed { background-color: #5B7BB8; color: #FFFFFF; }"
        ).arg(pillBg, light ? QStringLiteral("#3B5998") : QStringLiteral("#A9C3F0")));
    }

    auto styleCard = [&](QFrame* f, const QString& objName, const QString& accentTop) {
        if (!f) return;
        QString topRule = accentTop.isEmpty() ? QString() : QString("border-top: 3px solid %1;").arg(accentTop);
        f->setStyleSheet(QString(
            "QFrame#%1 { background-color: %2; border: 1px solid %3; %4 border-radius: 10px; }"
        ).arg(objName, cardBg, cardBorder, topRule));
    };

    styleCard(cardKpiBalance, "cardKpiBalance", "#7695CD");
    styleCard(cardKpiRewards, "cardKpiRewards", "#F5897C");
    styleCard(cardKpiNetwork, "cardKpiNetwork", "#2E9E6B");
    styleCard(cardKpiEmission, "cardKpiEmission", "#8B7CE8");
    styleCard(cardChart, "cardStatsChart", "");
    styleCard(cardNetworkConsensus, "cardStatsConsensus", "");
    styleCard(cardBalanceBreakdown, "cardStatsBalance", "");
    styleCard(cardActivity, "cardStatsActivity", "");

    for (QLabel* t : {lblKpiBalanceTitle, lblKpiRewardsTitle, lblKpiNetworkTitle, lblKpiEmissionTitle}) {
        if (t) t->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: bold; }").arg(textMuted));
    }
    for (QLabel* v : {lblKpiBalanceVal, lblKpiRewardsVal, lblKpiNetworkVal, lblKpiEmissionVal}) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 18px; font-weight: bold; }").arg(textPrimary));
    }
    for (QLabel* s : {lblKpiBalanceSub, lblKpiRewardsSub, lblKpiNetworkSub, lblKpiEmissionSub}) {
        if (s) s->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; }").arg(textMuted));
    }

    for (QLabel* sec : {lblChartSectionTitle, lblNetSectionTitle, lblBalSectionTitle, lblActSectionTitle}) {
        if (sec) sec->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: bold; }").arg(textPrimary));
    }

    for (QLabel* t : {lblWin24hTitle, lblWin7dTitle, lblWin30dTitle, lblWinAllTitle}) {
        if (t) t->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: bold; }").arg(textMuted));
    }
    for (QLabel* v : {lblWin24hVal, lblWin7dVal, lblWin30dVal, lblWinAllVal}) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textPrimary));
    }

    for (QLabel* k : infoRowKeys) {
        if (k) k->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textMuted));
    }
    for (QLabel* v : infoRowVals) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(textPrimary));
    }
    for (QLabel* pLbl : {lblSupplyText, lblDecayText}) {
        if (pLbl) pLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(textPrimary));
    }

    const QString pillCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0px 10px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { border-color: #7695CD; color: %4; }"
        "QPushButton:checked { background-color: #7695CD; color: #FFFFFF; border-color: #5B7BB8; }"
    ).arg(pillBg, textMuted, cardBorder, textPrimary);
    for (QPushButton* b : {btnPeriod30D, btnPeriod12M, btnPeriodAll, btnViewBars, btnViewTrend, btnViewSplit}) {
        if (b) b->setStyleSheet(pillCss);
    }

    auto styleBar = [&](QProgressBar* bar, const QString& chunkColor) {
        if (!bar) return;
        bar->setStyleSheet(QString(
            "QProgressBar { background-color: %1; border: none; border-radius: 4px; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 4px; }"
        ).arg(trackBg, chunkColor));
    };
    styleBar(barSupply, "#7695CD");
    styleBar(barDecay, "#8B7CE8");
    styleBar(barAvailPct, "#2E9E6B");
    styleBar(barLockedPct, "#F5897C");

#ifdef USE_QTCHARTS
    if (chart && axisX && axisY) {
        const QColor bg = light ? QColor("#FFFFFF") : QColor("#131C2E");
        const QColor grid = light ? QColor("#1A000000") : QColor("#30FFFFFF");
        const QColor labelCol = light ? QColor("#64748B") : QColor("#94A3B8");
        chart->setBackgroundBrush(QBrush(bg));
        axisX->setGridLineColor(bg);
        axisY->setGridLineColor(grid);
        axisX->setLabelsColor(labelCol);
        axisY->setLabelsColor(labelCol);
        axisX->setLinePenColor(bg);
        axisY->setLinePenColor(bg);
    }
#endif
}

QString StatsWidget::formatAmountPlain(CAmount amount, bool compact) const
{
    if (!compact) {
        return BitcoinUnits::formatWithUnit(displayUnit, amount, false, BitcoinUnits::separatorStandard);
    }
    const QString unitName = BitcoinUnits::name(displayUnit);
    if (amount == 0) return QString("0.00 %1").arg(unitName);
    double coins = static_cast<double>(amount) / static_cast<double>(COIN);
    if (displayUnit == BitcoinUnits::mPIV) coins *= 1000.0;
    else if (displayUnit == BitcoinUnits::uPIV) coins *= 1000000.0;
    if (std::abs(coins) < 0.01) {
        return QString("%1 %2").arg(QLocale().toString(coins, 'f', 4), unitName);
    }
    return QString("%1 %2").arg(QLocale().toString(coins, 'f', 2), unitName);
}

QString StatsWidget::formatDurationSeconds(qint64 seconds) const
{
    if (seconds < 0) seconds = 0;
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 mins = (seconds % 3600) / 60;
    if (days > 0) return tr("%1d %2h").arg(days).arg(hours);
    if (hours > 0) return tr("%1h %2m").arg(hours).arg(mins);
    return tr("%1m").arg(mins);
}

StatsWidget::RewardStatsSnapshot StatsWidget::collectRewardSnapshot() const
{
    RewardStatsSnapshot snap;
    if (!walletModel || !walletModel->getTransactionTableModel()) {
        return snap;
    }

    TransactionTableModel* txModel = walletModel->getTransactionTableModel();
    const int rows = txModel->rowCount(QModelIndex());
    snap.countTotalTx = rows;

    const QDateTime nowDt = QDateTime::currentDateTime();
    const QDate today = nowDt.date();
    const QDateTime dt24h = nowDt.addDays(-1);
    const QDateTime dt7d = nowDt.addDays(-7);
    const QDateTime dt30d = nowDt.addDays(-30);

    QMap<QString, qint64> bucketPos;
    QMap<QString, qint64> bucketMnAdam;
    QStringList orderedKeys;

    if (periodMode == 0) {
        for (int i = 29; i >= 0; --i) {
            QDate d = today.addDays(-i);
            QString key = d.toString("yyyy-MM-dd");
            orderedKeys << key;
            snap.labels << d.toString("dd/MM");
        }
    } else if (periodMode == 1) {
        for (int i = 11; i >= 0; --i) {
            QDate m = today.addMonths(-i);
            QString key = m.toString("yyyy-MM");
            orderedKeys << key;
            snap.labels << m.toString("MMM yy");
        }
    }

    int minYear = today.year();
    int maxYear = today.year();

    for (int i = 0; i < rows; ++i) {
        QModelIndex idx = txModel->index(i, TransactionTableModel::ToAddress);
        const int type = idx.data(TransactionTableModel::TypeRole).toInt();
        const qint64 rawAmt = idx.data(TransactionTableModel::AmountRole).toLongLong();
        const qint64 absAmt = std::llabs(rawAmt);
        const QDateTime txDt = idx.data(TransactionTableModel::DateRole).toDateTime();

        if (rawAmt < 0) {
            snap.totalSent += absAmt;
        } else if (type == TransactionRecord::RecvWithAddress || type == TransactionRecord::RecvFromOther) {
            snap.totalReceived += absAmt;
        }

        const bool isPos = (type == TransactionRecord::StakeMint);
        const bool isMnLlmq = (type == TransactionRecord::MNReward || type == TransactionRecord::LlmqReward);
        const bool isAdam = (type == TransactionRecord::AdamReward);
        if (!isPos && !isMnLlmq && !isAdam) continue;

        if (isPos) {
            snap.totalPos += absAmt;
            ++snap.countPos;
        } else if (isMnLlmq) {
            snap.totalMnLlmq += absAmt;
            ++snap.countMnLlmq;
        } else {
            snap.totalAdam += absAmt;
            ++snap.countAdam;
        }

        if (txDt >= dt24h) snap.last24h += absAmt;
        if (txDt >= dt7d) snap.last7d += absAmt;
        if (txDt >= dt30d) snap.last30d += absAmt;

        if (periodMode == 0) {
            QString k = txDt.date().toString("yyyy-MM-dd");
            if (isPos) bucketPos[k] += absAmt;
            else bucketMnAdam[k] += absAmt;
        } else if (periodMode == 1) {
            QString k = txDt.date().toString("yyyy-MM");
            if (isPos) bucketPos[k] += absAmt;
            else bucketMnAdam[k] += absAmt;
        } else {
            int y = txDt.date().year();
            if (y > 0) {
                minYear = std::min(minYear, y);
                maxYear = std::max(maxYear, y);
                QString k = QString::number(y);
                if (isPos) bucketPos[k] += absAmt;
                else bucketMnAdam[k] += absAmt;
            }
        }
    }

    if (periodMode == 2) {
        for (int y = minYear; y <= maxYear; ++y) {
            QString k = QString::number(y);
            orderedKeys << k;
            snap.labels << k;
        }
    }

    for (const QString& k : orderedKeys) {
        snap.posSeries.append(static_cast<double>(bucketPos.value(k, 0)) / static_cast<double>(COIN));
        snap.mnAdamSeries.append(static_cast<double>(bucketMnAdam.value(k, 0)) / static_cast<double>(COIN));
    }

    return snap;
}

void StatsWidget::renderRewardChart(const RewardStatsSnapshot& snap)
{
#ifndef USE_QTCHARTS
    Q_UNUSED(snap);
    return;
#else
    if (!chart || !axisX || !axisY) return;
    chart->removeAllSeries();
    axisX->clear();

    const int unitCopy = displayUnit;
    const QStringList labelsCopy = snap.labels;
    const bool light = isLightTheme();

    if (chartMode == 2) {
        axisX->setVisible(false);
        axisY->setVisible(false);

        QPieSeries* pie = new QPieSeries(chart);
        pie->setHoleSize(0.56);
        pie->setPieSize(0.82);

        const CAmount total = snap.totalPos + snap.totalMnLlmq + snap.totalAdam;
        const QColor borderCol = light ? QColor("#FFFFFF") : QColor("#131C2E");
        const QColor labelCol = light ? QColor("#334155") : QColor("#E2E8F0");

        auto addSlice = [&](const QString& title, CAmount amt, const QColor& col) {
            if (amt <= 0) return;
            double pct = (total > 0) ? (100.0 * static_cast<double>(amt) / static_cast<double>(total)) : 0.0;
            QPieSlice* s = pie->append(QString("%1 (%2%)").arg(title).arg(pct, 0, 'f', 1),
                                       static_cast<double>(amt) / static_cast<double>(COIN));
            s->setColor(col);
            s->setBorderColor(borderCol);
            s->setBorderWidth(2);
            s->setLabelColor(labelCol);
            s->setLabelVisible(true);
            s->setProperty("exactAmount", static_cast<qint64>(amt));
            s->setProperty("trackTitle", title);
        };

        addSlice(tr("Staking (PoS)"), snap.totalPos, QColor(245, 137, 124));
        addSlice(tr("MN & LLMQ"), snap.totalMnLlmq, QColor(118, 149, 205));
        addSlice(tr("ADAM Mining"), snap.totalAdam, QColor(46, 158, 107));

        connect(pie, &QPieSeries::hovered, pie, [unitCopy](QPieSlice* slice, bool state) {
            if (!state || !slice) {
                QToolTip::hideText();
                return;
            }
            const CAmount amt = static_cast<CAmount>(slice->property("exactAmount").toLongLong());
            const QString title = slice->property("trackTitle").toString();
            QToolTip::showText(QCursor::pos(),
                QString("%1 (%2%)\n%3").arg(title)
                    .arg(slice->percentage() * 100.0, 0, 'f', 1)
                    .arg(BitcoinUnits::formatWithUnit(unitCopy, amt, false, BitcoinUnits::separatorStandard)));
        });

        chart->addSeries(pie);
    } else if (chartMode == 1) {
        axisX->setVisible(true);
        axisY->setVisible(true);
        axisX->append(snap.labels);

        QLineSeries* upper = new QLineSeries();
        const int count = std::min(snap.posSeries.size(), snap.mnAdamSeries.size());
        double running = 0.0;
        QVector<double> deltas;
        deltas.reserve(count);
        for (int i = 0; i < count; ++i) {
            double d = snap.posSeries.at(i) + snap.mnAdamSeries.at(i);
            running += d;
            deltas.append(d);
            upper->append(i, running);
        }

        const QColor lineCol(118, 149, 205);
        QColor fillCol = lineCol;
        fillCol.setAlpha(60);

        QAreaSeries* area = new QAreaSeries(upper, nullptr);
        upper->setParent(area);
        area->setPen(QPen(lineCol, 2.5));
        area->setBrush(QBrush(fillCol));
        area->setPointsVisible(true);

        connect(area, &QAreaSeries::hovered, area, [labelsCopy, deltas, unitCopy](const QPointF& pt, bool state) {
            if (!state) {
                QToolTip::hideText();
                return;
            }
            const int idx = std::max(0, std::min(static_cast<int>(std::llround(pt.x())), labelsCopy.size() - 1));
            const QString period = (idx >= 0 && idx < labelsCopy.size()) ? labelsCopy.at(idx) : QString::number(idx + 1);
            const CAmount cumSat = static_cast<CAmount>(std::llround(pt.y() * COIN));
            const CAmount perSat = (idx >= 0 && idx < deltas.size())
                ? static_cast<CAmount>(std::llround(deltas.at(idx) * COIN)) : 0;
            QToolTip::showText(QCursor::pos(),
                QString("%1\nCumulative: %2\nPeriod: +%3")
                    .arg(period,
                         BitcoinUnits::formatWithUnit(unitCopy, cumSat, false, BitcoinUnits::separatorStandard),
                         BitcoinUnits::formatWithUnit(unitCopy, perSat, false, BitcoinUnits::separatorStandard)));
        });

        chart->addSeries(area);
        area->attachAxis(axisX);
        area->attachAxis(axisY);

        const double yMax = (running > 0.0) ? (running * 1.1) : 1.0;
        axisY->setLabelFormat(yMax >= 20.0 ? "%.0f" : "%.2f");
        axisY->setRange(0.0, yMax);
    } else {
        axisX->setVisible(true);
        axisY->setVisible(true);
        axisX->append(snap.labels);

        QBarSet* setPos = new QBarSet(tr("Staking (PoS)"));
        QBarSet* setMnAdam = new QBarSet(tr("MN & Mining"));
        setPos->setColor(QColor(245, 137, 124));
        setMnAdam->setColor(QColor(118, 149, 205));
        const QColor barBorder = light ? QColor("#FFFFFF") : QColor("#131C2E");
        setPos->setBorderColor(barBorder);
        setMnAdam->setBorderColor(barBorder);

        auto bindHover = [labelsCopy, unitCopy](QBarSet* bs, const QString& trackName) {
            connect(bs, &QBarSet::hovered, bs, [bs, labelsCopy, unitCopy, trackName](bool status, int index) {
                if (!status || index < 0 || index >= bs->count()) {
                    QToolTip::hideText();
                    return;
                }
                const QString period = (index < labelsCopy.size()) ? labelsCopy.at(index) : QString::number(index + 1);
                const CAmount sat = static_cast<CAmount>(std::llround(bs->at(index) * COIN));
                QToolTip::showText(QCursor::pos(),
                    QString("%1 — %2\n%3").arg(period, trackName,
                        BitcoinUnits::formatWithUnit(unitCopy, sat, false, BitcoinUnits::separatorStandard)));
            });
        };
        bindHover(setPos, tr("Staking (PoS)"));
        bindHover(setMnAdam, tr("MN & Mining"));

        double maxStack = 0.0;
        const int count = std::min(snap.posSeries.size(), snap.mnAdamSeries.size());
        for (int i = 0; i < count; ++i) {
            const double p = snap.posSeries.at(i);
            const double m = snap.mnAdamSeries.at(i);
            *setPos << p;
            *setMnAdam << m;
            if (p + m > maxStack) maxStack = p + m;
        }

        QStackedBarSeries* stacked = new QStackedBarSeries(chart);
        stacked->append(setPos);
        stacked->append(setMnAdam);
        stacked->setBarWidth(periodMode == 0 ? 0.65 : 0.55);

        chart->addSeries(stacked);
        stacked->attachAxis(axisX);
        stacked->attachAxis(axisY);

        const double yMax = (maxStack > 0.0) ? (maxStack * 1.1) : 1.0;
        axisY->setLabelFormat(yMax >= 20.0 ? "%.0f" : "%.2f");
        axisY->setRange(0.0, yMax);
    }
#endif
}

void StatsWidget::refreshStats()
{
    const RewardStatsSnapshot snap = collectRewardSnapshot();
    const CAmount totalRewards = snap.totalPos + snap.totalMnLlmq + snap.totalAdam;

    // Balances
    CAmount avail = 0;
    CAmount unconfirmed = 0;
    CAmount immature = 0;
    CAmount locked = 0;
    bool stakingActive = false;
    if (walletModel) {
        const interfaces::WalletBalances bal = walletModel->GetWalletBalances();
        locked = bal.locked_balance;
        avail = bal.balance - locked;
        unconfirmed = bal.unconfirmed_balance;
        immature = bal.immature_balance;
        stakingActive = !walletModel->isWalletLocked() && walletModel->isStakingStatusActive();
    }
    const CAmount totalWallet = avail + immature + locked;

    lblKpiBalanceVal->setText(formatAmountPlain(totalWallet, true));
    lblKpiBalanceVal->setToolTip(formatAmountPlain(totalWallet, false));
    lblKpiBalanceSub->setText(tr("Avail: %1 • Locked: %2")
        .arg(formatAmountPlain(avail, true), formatAmountPlain(locked, true)));

    lblKpiRewardsVal->setText(formatAmountPlain(totalRewards, true));
    lblKpiRewardsVal->setToolTip(formatAmountPlain(totalRewards, false));
    lblKpiRewardsSub->setText(tr("PoS: %1 • MN+ADAM: %2")
        .arg(formatAmountPlain(snap.totalPos, true), formatAmountPlain(snap.totalMnLlmq + snap.totalAdam, true)));

    // Chain & Network
    int height = 0;
    int peers = 0;
    qint64 lastBlockAgeSec = 0;
    bool synced = false;
    CAmount moneySupply = 0;

    if (clientModel) {
        height = clientModel->getNumBlocks();
        peers = clientModel->getNumConnections();
        synced = !clientModel->inInitialBlockDownload();
        QDateTime lastBlockDt = clientModel->getLastBlockDate();
        if (lastBlockDt.isValid()) {
            lastBlockAgeSec = std::max<qint64>(0, lastBlockDt.secsTo(QDateTime::currentDateTimeUtc()));
        }
    }
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain && chainActive.Tip()) {
            height = std::max(height, chainActive.Height());
            moneySupply = nMoneySupply;
        }
    }

    lblSyncBadge->setText(synced ? tr("Synced") : tr("Syncing"));
    lblSyncBadge->setStyleSheet(QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 14px; padding: 0px 12px; font-size: 11px; font-weight: bold; }"
    ).arg(synced ? QStringLiteral("#1F8A5B") : QStringLiteral("#D97706"),
          QStringLiteral("#FFFFFF"),
          synced ? QStringLiteral("#176B46") : QStringLiteral("#B45309")));

    lblKpiNetworkVal->setText(QLocale().toString(height));
    lblKpiNetworkSub->setText(tr("%1 peers • Last block %2 ago")
        .arg(peers).arg(formatDurationSeconds(lastBlockAgeSec)));

    const CAmount blockReward = CMasternode::GetBlockValue(height > 0 ? height : 1);
    const CAmount devShare = (blockReward * 7) / 100;
    const CAmount netPool = blockReward - devShare;
    lblKpiEmissionVal->setText(formatAmountPlain(blockReward, true));
    lblKpiEmissionSub->setText(tr("7% Dev: %1 • Pool: %2")
        .arg(formatAmountPlain(devShare, true), formatAmountPlain(netPool, true)));

    // Time window strip
    lblWin24hVal->setText(formatAmountPlain(snap.last24h, true));
    lblWin7dVal->setText(formatAmountPlain(snap.last7d, true));
    lblWin30dVal->setText(formatAmountPlain(snap.last30d, true));
    lblWinAllVal->setText(formatAmountPlain(totalRewards, true));

    // Supply & Decay progress
    const CAmount maxMoney = Params().GetConsensus().nMaxMoneyOut;
    const double supplyPct = (maxMoney > 0)
        ? std::min(100.0, 100.0 * static_cast<double>(moneySupply) / static_cast<double>(maxMoney))
        : 0.0;
    lblSupplyText->setText(tr("Supply: %1 / %2 (%3%)")
        .arg(formatAmountPlain(moneySupply, true), formatAmountPlain(maxMoney, true))
        .arg(supplyPct, 0, 'f', 2));
    barSupply->setValue(static_cast<int>(supplyPct * 10.0));

    const int decayInterval = 259200;
    const int bootstrapEnd = 10000;
    int blocksIntoPeriod = 0;
    int blocksRemaining = 0;
    if (height < bootstrapEnd) {
        blocksIntoPeriod = height;
        blocksRemaining = bootstrapEnd - height;
        lblDecayText->setText(tr("Bootstrap Phase: %1 blocks to 15 KRISTA base (~%2)")
            .arg(blocksRemaining).arg(formatDurationSeconds(static_cast<qint64>(blocksRemaining) * 30)));
        barDecay->setValue(static_cast<int>((1000.0 * blocksIntoPeriod) / bootstrapEnd));
    } else {
        blocksIntoPeriod = (height - bootstrapEnd) % decayInterval;
        blocksRemaining = decayInterval - blocksIntoPeriod;
        lblDecayText->setText(tr("Next 1.9% Decay: %1 blocks (~%2)")
            .arg(QLocale().toString(blocksRemaining))
            .arg(formatDurationSeconds(static_cast<qint64>(blocksRemaining) * 30)));
        barDecay->setValue(static_cast<int>((1000.0 * blocksIntoPeriod) / decayInterval));
    }

    // Consensus & Dual-Track pool
    std::set<CPubKey> mnKeys;
    std::set<CPubKey> coinLockKeys;
    std::set<CPubKey> fundedPowKeys;
    std::set<CPubKey> bootstrapKeys;
    size_t explicitCount = 0;
    size_t fundedCount = 0;
    const auto poolKeys = GetAdamMinerPool(height, nullptr, &mnKeys, &coinLockKeys, &explicitCount, &fundedCount, &fundedPowKeys, &bootstrapKeys);
    const int mnEnabled = mnodeman.CountEnabled();

    if (infoRowVals.size() >= 6) {
        infoRowVals[0]->setText(tr("%1 total (%2 MN • %3 Lock250 • %4 PoW)")
            .arg(poolKeys.size()).arg(mnKeys.size()).arg(coinLockKeys.size()).arg(fundedPowKeys.size() + bootstrapKeys.size()));
        infoRowVals[1]->setText(tr("%1 enabled (4,200 %2)").arg(mnEnabled).arg(BitcoinUnits::name(displayUnit)));
        infoRowVals[2]->setText(stakingActive ? tr("Active (Staking)") : tr("Idle / Locked"));
        infoRowVals[3]->setText(QStringLiteral("65 : 16 : 4 (45s / 60s)"));
        infoRowVals[4]->setText(QStringLiteral("35% MN / 10% LLMQ / 15% Prod / 40% Part"));
        infoRowVals[5]->setText(QStringLiteral("v1.5.7.0 (70983) • 30s Target"));
    }

    // Balance & Collateral Breakdown
    lblBalAvailVal->setText(formatAmountPlain(avail, false));
    lblBalLockedVal->setText(formatAmountPlain(locked, false));
    lblBalImmatureVal->setText(formatAmountPlain(immature, false));
    lblBalPendingVal->setText(formatAmountPlain(unconfirmed, false));

    const int availPermille = (totalWallet > 0) ? static_cast<int>((1000.0 * avail) / totalWallet) : 0;
    const int lockedPermille = (totalWallet > 0) ? static_cast<int>((1000.0 * locked) / totalWallet) : 0;
    barAvailPct->setValue(std::min(1000, std::max(0, availPermille)));
    barLockedPct->setValue(std::min(1000, std::max(0, lockedPermille)));

    // Wallet Activity Breakdown
    lblActTxCountVal->setText(QLocale().toString(snap.countTotalTx));
    lblActPosVal->setText(tr("%1 tx • %2").arg(snap.countPos).arg(formatAmountPlain(snap.totalPos, true)));
    lblActMnVal->setText(tr("%1 tx • %2").arg(snap.countMnLlmq).arg(formatAmountPlain(snap.totalMnLlmq, true)));
    lblActAdamVal->setText(tr("%1 tx • %2").arg(snap.countAdam).arg(formatAmountPlain(snap.totalAdam, true)));
    lblActSentRecvVal->setText(tr("-%1 / +%2")
        .arg(formatAmountPlain(snap.totalSent, true), formatAmountPlain(snap.totalReceived, true)));

    renderRewardChart(snap);
}
