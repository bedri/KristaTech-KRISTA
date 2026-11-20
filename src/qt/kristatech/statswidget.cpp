// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/statswidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "transactionrecord.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"

#include "adam.h"
#include "chainparams.h"
#include "evo/deterministicmns.h"
#include "main.h"
#include "masternode.h"
#include "masternodeman.h"
#include "miner.h"
#include "txmempool.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif

#include <QCursor>
#include <QDate>
#include <QDateTime>
#include <QGraphicsLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPainterPath>
#include <QPair>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

class SegmentedBarWidget : public QWidget
{
public:
    explicit SegmentedBarWidget(int barHeight = 6, QWidget* parent = nullptr)
        : QWidget(parent), m_barHeight(std::max(4, barHeight))
    {
        setFixedHeight(m_barHeight);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void setSegments(const QVector<QPair<double, QColor>>& segments, const QColor& trackColor)
    {
        m_segments = segments;
        m_trackColor = trackColor;
        update();
    }

    void setTrackColor(const QColor& trackColor)
    {
        if (m_trackColor != trackColor) {
            m_trackColor = trackColor;
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r(0.0, 0.0, static_cast<double>(width()), static_cast<double>(height()));
        if (r.width() <= 0.0 || r.height() <= 0.0) return;

        const double radius = r.height() * 0.5;
        QPainterPath clipPath;
        clipPath.addRoundedRect(r, radius, radius);
        p.setClipPath(clipPath);
        p.fillRect(r, m_trackColor.isValid() ? m_trackColor : QColor("#1F2D46"));

        double total = 0.0;
        int lastPositiveIdx = -1;
        for (int i = 0; i < m_segments.size(); ++i) {
            if (m_segments.at(i).first > 0.0) {
                total += m_segments.at(i).first;
                lastPositiveIdx = i;
            }
        }
        if (total <= 0.0 || lastPositiveIdx < 0) return;

        double x = 0.0;
        for (int i = 0; i < m_segments.size(); ++i) {
            const double val = m_segments.at(i).first;
            if (val <= 0.0) continue;
            const double w = (i == lastPositiveIdx)
                ? (r.width() - x)
                : (r.width() * (val / total));
            if (w > 0.0) {
                p.fillRect(QRectF(x, 0.0, w, r.height()), m_segments.at(i).second);
                x += w;
            }
        }
    }

private:
    int m_barHeight;
    QColor m_trackColor;
    QVector<QPair<double, QColor>> m_segments;
};

namespace {

const QColor COLOR_POS(245, 137, 124);     // Salmon (#F5897C)
const QColor COLOR_MN(118, 149, 205);      // Krista Blue (#7695CD)
const QColor COLOR_ADAM(139, 127, 214);    // Soft Indigo (#8B7FD6)
const QColor COLOR_LLMQ(180, 134, 184);    // Muted Mauve (#B486B8)
const QColor COLOR_DEV(100, 116, 139);     // Slate (#64748B)

double DifficultyFromBits(uint32_t nBits)
{
    const uint32_t mantissa = nBits & 0x00ffffffU;
    if (mantissa == 0U) return 1.0;

    int nShift = static_cast<int>((nBits >> 24) & 0xffU);
    double dDiff = static_cast<double>(0x0000ffffU) / static_cast<double>(mantissa);
    while (nShift < 29) {
        dDiff *= 256.0;
        ++nShift;
    }
    while (nShift > 29) {
        dDiff /= 256.0;
        --nShift;
    }
    return dDiff;
}

QString FormatDifficultyLocal(double diff)
{
    if (diff <= 0.0) return QStringLiteral("0.00");
    if (diff < 0.001) return QString::number(diff, 'e', 3);
    if (diff < 1.0) return QString::number(diff, 'f', 5);
    if (diff < 1000.0) return QString::number(diff, 'f', 3);
    return QLocale().toString(diff, 'f', 2);
}

int NiceAxisCeil(int maxVal)
{
    if (maxVal <= 4) return 4;
    if (maxVal <= 8) return 8;
    const int step = std::max(1, static_cast<int>(std::ceil(static_cast<double>(maxVal) / 4.0)));
    return step * 4;
}

QFrame* CreateHairlineDivider(QWidget* parent, QVector<QFrame*>& registry)
{
    QFrame* div = new QFrame(parent);
    div->setFrameShape(QFrame::NoFrame);
    div->setFixedHeight(1);
    registry.append(div);
    return div;
}

QFrame* AddKeyValueRow(
    QVBoxLayout* parentCol,
    QWidget* card,
    const QString& keyText,
    QLabel*& valOut,
    QVector<QLabel*>& keyReg,
    QVector<QLabel*>& valReg,
    QVector<QFrame*>& divReg,
    bool addDividerBelow = true,
    QLabel** keyOutPtr = nullptr)
{
    QHBoxLayout* row = new QHBoxLayout();
    row->setContentsMargins(0, 4, 0, 4);
    row->setSpacing(10);

    QLabel* k = new QLabel(keyText, card);
    valOut = new QLabel(QStringLiteral("--"), card);
    valOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    keyReg.append(k);
    valReg.append(valOut);
    if (keyOutPtr) *keyOutPtr = k;

    row->addWidget(k, 1);
    row->addWidget(valOut, 0, Qt::AlignRight | Qt::AlignVCenter);
    parentCol->addLayout(row);

    if (addDividerBelow) {
        QFrame* div = CreateHairlineDivider(card, divReg);
        parentCol->addWidget(div);
        return div;
    }
    return nullptr;
}

#ifdef USE_QTCHARTS
void StyleCartesianChart(QChart* ch, QAbstractAxis* axX, QValueAxis* axY, bool light)
{
    if (!ch) return;
    const QColor bg = light ? QColor("#FFFFFF") : QColor("#131C2E");
    const QColor grid = light ? QColor("#1A000000") : QColor("#30FFFFFF");
    const QColor labelCol = light ? QColor("#475569") : QColor("#CBD5E1");

    ch->setBackgroundBrush(QBrush(bg));
    QFont yf = axY ? axY->labelsFont() : QFont();
    yf.setPointSize(9);
    if (axX) {
        axX->setGridLineColor(bg);
        axX->setLabelsColor(labelCol);
        axX->setLinePenColor(grid);
        QFont xf = axX->labelsFont();
        xf.setPointSize(9);
        axX->setLabelsFont(xf);
    }
    if (axY) {
        axY->setGridLineColor(grid);
        axY->setLabelsColor(labelCol);
        axY->setLinePenColor(bg);
        axY->setLabelsFont(yf);
    }
}
#endif

} // namespace

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

    retryTimer = new QTimer(this);
    retryTimer->setSingleShot(true);
    connect(retryTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) {
            refreshStats();
        }
    });
}

StatsWidget::~StatsWidget()
{
    // Note: QChart objects are parented/owned by their respective QChartView scenes.
}

void StatsWidget::setupUiLayout()
{
    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 14, 20, 14);
    rootLayout->setSpacing(12);

    // Header Row: Title on left, Segmented View Switcher + Refresh on right
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    lblTitle = new QLabel(tr("Wallet & Network Statistics"), this);
    lblSubtitle = new QLabel(tr("Live portfolio earnings, Model D emission schedule, and real-time chain telemetry"), this);
    titleCol->addWidget(lblTitle);
    titleCol->addWidget(lblSubtitle);
    headerRow->addLayout(titleCol, 1);

    viewSwitchFrame = new QFrame(this);
    viewSwitchFrame->setObjectName(QStringLiteral("statsViewSwitchFrame"));
    QHBoxLayout* switchLay = new QHBoxLayout(viewSwitchFrame);
    switchLay->setContentsMargins(3, 3, 3, 3);
    switchLay->setSpacing(3);

    btnTabOverview = new QPushButton(tr("Overview"), viewSwitchFrame);
    btnTabOverview->setCheckable(true);
    btnTabOverview->setChecked(true);
    btnTabOverview->setCursor(Qt::PointingHandCursor);
    btnTabOverview->setFocusPolicy(Qt::TabFocus);
    btnTabOverview->setFixedHeight(28);

    btnTabTelemetry = new QPushButton(tr("Network Telemetry"), viewSwitchFrame);
    btnTabTelemetry->setCheckable(true);
    btnTabTelemetry->setChecked(false);
    btnTabTelemetry->setCursor(Qt::PointingHandCursor);
    btnTabTelemetry->setFocusPolicy(Qt::TabFocus);
    btnTabTelemetry->setFixedHeight(28);

    switchLay->addWidget(btnTabOverview);
    switchLay->addWidget(btnTabTelemetry);
    headerRow->addWidget(viewSwitchFrame, 0, Qt::AlignVCenter);

    connect(btnTabOverview, &QPushButton::clicked, this, [this]() { showView(0); });
    connect(btnTabTelemetry, &QPushButton::clicked, this, [this]() { showView(1); });

    btnRefresh = new QPushButton(tr("Refresh"), this);
    btnRefresh->setCursor(Qt::PointingHandCursor);
    btnRefresh->setFocusPolicy(Qt::TabFocus);
    btnRefresh->setFixedHeight(32);
    btnRefresh->setToolTip(tr("Refresh wallet and network telemetry statistics"));
    connect(btnRefresh, &QPushButton::clicked, this, &StatsWidget::refreshStats);
    headerRow->addWidget(btnRefresh, 0, Qt::AlignVCenter);

    rootLayout->addLayout(headerRow);

    viewStack = new QStackedWidget(this);
    viewStack->addWidget(buildOverviewPage());
    viewStack->addWidget(buildTelemetryPage());
    viewStack->setCurrentIndex(0);
    rootLayout->addWidget(viewStack, 1);
}

void StatsWidget::showView(int viewIdx)
{
    const int clamped = (viewIdx == 1) ? 1 : 0;
    activeView = clamped;
    if (btnTabOverview) btnTabOverview->setChecked(clamped == 0);
    if (btnTabTelemetry) btnTabTelemetry->setChecked(clamped == 1);
    if (viewStack) viewStack->setCurrentIndex(clamped);
    if (isVisible()) {
        refreshStats();
    }
}

QWidget* StatsWidget::buildOverviewPage()
{
    scrollOverview = new QScrollArea(this);
    scrollOverview->setWidgetResizable(true);
    scrollOverview->setFrameShape(QFrame::NoFrame);
    scrollOverview->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    overviewContent = new QWidget(scrollOverview);
    QVBoxLayout* bodyLayout = new QVBoxLayout(overviewContent);
    bodyLayout->setContentsMargins(0, 0, 4, 4);
    bodyLayout->setSpacing(12);

    // Row 1: Top 4 KPI Cards
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(10);

    auto makeKpiCard = [&](QFrame*& card, QLabel*& titleLbl, QLabel*& valLbl, QLabel*& subLbl,
                           const QString& objName, const QString& title, bool withBar = false) {
        card = new QFrame(overviewContent);
        card->setObjectName(objName);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        themedCards.append(card);

        QVBoxLayout* col = new QVBoxLayout(card);
        col->setContentsMargins(14, 11, 14, 11);
        col->setSpacing(3);
        titleLbl = new QLabel(title, card);
        valLbl = new QLabel(QStringLiteral("--"), card);
        subLbl = new QLabel(QStringLiteral("--"), card);
        subLbl->setWordWrap(true);
        kpiTitles.append(titleLbl);
        kpiVals.append(valLbl);
        kpiSubs.append(subLbl);
        col->addWidget(titleLbl);
        col->addWidget(valLbl);
        col->addWidget(subLbl);
        if (withBar) {
            barKpiBalance = new SegmentedBarWidget(5, card);
            col->addSpacing(3);
            col->addWidget(barKpiBalance);
        }
        kpiRow->addWidget(card, 1);
    };

    makeKpiCard(cardKpiBalance, lblKpiBalanceTitle, lblKpiBalanceVal, lblKpiBalanceSub,
                QStringLiteral("cardKpiBalance"), tr("My Total Balance"), true);
    makeKpiCard(cardKpiRewards, lblKpiRewardsTitle, lblKpiRewardsVal, lblKpiRewardsSub,
                QStringLiteral("cardKpiRewards"), tr("My Total Earnings"));
    makeKpiCard(cardKpiNetwork, lblKpiNetworkTitle, lblKpiNetworkVal, lblKpiNetworkSub,
                QStringLiteral("cardKpiNetwork"), tr("Chain & MPA Pool"));
    makeKpiCard(cardKpiEmission, lblKpiEmissionTitle, lblKpiEmissionVal, lblKpiEmissionSub,
                QStringLiteral("cardKpiEmission"), tr("Block Emission (Model D)"));

    bodyLayout->addLayout(kpiRow);

    // Row 2: Clean 2-Column Split (Left 60% = Wallet & Earnings, Right 40% = Network & Economics)
    QHBoxLayout* mainCols = new QHBoxLayout();
    mainCols->setSpacing(12);

    // ================= LEFT COLUMN (60%) =================
    QVBoxLayout* leftCol = new QVBoxLayout();
    leftCol->setSpacing(12);

    // Reward Analytics Chart Card
    cardChart = new QFrame(overviewContent);
    cardChart->setObjectName(QStringLiteral("cardStatsChart"));
    cardChart->setMinimumHeight(340);
    themedCards.append(cardChart);

    QVBoxLayout* chartCol = new QVBoxLayout(cardChart);
    chartCol->setContentsMargins(14, 12, 14, 12);
    chartCol->setSpacing(8);

    QHBoxLayout* chartToolbar = new QHBoxLayout();
    chartToolbar->setSpacing(5);
    lblChartSectionTitle = new QLabel(tr("Reward Analytics"), cardChart);
    sectionTitles.append(lblChartSectionTitle);
    chartToolbar->addWidget(lblChartSectionTitle);
    chartToolbar->addStretch(1);

    auto makePill = [&](const QString& text, const QString& tip) -> QPushButton* {
        QPushButton* b = new QPushButton(text, cardChart);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        b->setFixedHeight(25);
        b->setToolTip(tip);
        return b;
    };

    btnPeriod24H = makePill(tr("24H"), tr("Hourly rewards over the last 24 hours"));
    btnPeriod7D  = makePill(tr("7D"),  tr("Daily rewards over the last 7 days"));
    btnPeriod30D = makePill(tr("30D"), tr("Daily rewards over the last 30 days"));
    btnPeriod12M = makePill(tr("12M"), tr("Monthly rewards over the last 12 months"));
    btnPeriodAll = makePill(tr("All"), tr("Yearly rewards across all wallet history"));
    btnPeriod7D->setChecked(true);

    btnViewBars  = makePill(tr("Bars"),  tr("3-series stacked bar breakdown"));
    btnViewTrend = makePill(tr("Trend"), tr("Cumulative earnings curve"));
    btnViewSplit = makePill(tr("Split"), tr("Proportional source donut"));
    btnViewBars->setChecked(true);

    chartToolbar->addWidget(btnPeriod24H);
    chartToolbar->addWidget(btnPeriod7D);
    chartToolbar->addWidget(btnPeriod30D);
    chartToolbar->addWidget(btnPeriod12M);
    chartToolbar->addWidget(btnPeriodAll);
    chartToolbar->addSpacing(8);
    chartToolbar->addWidget(btnViewBars);
    chartToolbar->addWidget(btnViewTrend);
    chartToolbar->addWidget(btnViewSplit);
    chartCol->addLayout(chartToolbar);

    connect(btnPeriod24H, &QPushButton::clicked, this, [this]() { setPeriodMode(0); });
    connect(btnPeriod7D,  &QPushButton::clicked, this, [this]() { setPeriodMode(1); });
    connect(btnPeriod30D, &QPushButton::clicked, this, [this]() { setPeriodMode(2); });
    connect(btnPeriod12M, &QPushButton::clicked, this, [this]() { setPeriodMode(3); });
    connect(btnPeriodAll, &QPushButton::clicked, this, [this]() { setPeriodMode(4); });

    auto setView = [this](int v) {
        chartMode = v;
        btnViewBars->setChecked(v == 0);
        btnViewTrend->setChecked(v == 1);
        btnViewSplit->setChecked(v == 2);
        refreshStats();
    };
    connect(btnViewBars,  &QPushButton::clicked, this, [setView]() { setView(0); });
    connect(btnViewTrend, &QPushButton::clicked, this, [setView]() { setView(1); });
    connect(btnViewSplit, &QPushButton::clicked, this, [setView]() { setView(2); });

    // Inline 3-series Legend Row
    QHBoxLayout* legendRow = new QHBoxLayout();
    legendRow->setContentsMargins(0, 0, 0, 2);
    legendRow->setSpacing(14);
    lblLegPos  = new QLabel(tr("● PoS Staking"), cardChart);
    lblLegMn   = new QLabel(tr("● MN & LLMQ"), cardChart);
    lblLegAdam = new QLabel(tr("● ADAM / PoW"), cardChart);
    lblChartPeriodTotal = new QLabel(QStringLiteral("--"), cardChart);
    lblChartPeriodTotal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    kpiSubs.append(lblChartPeriodTotal);

    legendRow->addWidget(lblLegPos);
    legendRow->addWidget(lblLegMn);
    legendRow->addWidget(lblLegAdam);
    legendRow->addStretch(1);
    legendRow->addWidget(lblChartPeriodTotal);
    chartCol->addLayout(legendRow);

#ifdef USE_QTCHARTS
    chart = new QChart();
    chart->legend()->setVisible(false);
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    chart->setMargins({0, 2, 0, 0});
    chart->setBackgroundRoundness(0);
    chart->setAnimationOptions(QChart::NoAnimation);

    axisX = new QBarCategoryAxis();
    axisY = new QValueAxis();
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignRight);

    chartView = new QChartView(chart, cardChart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setRubberBand(QChartView::NoRubberBand);
    chartView->setMinimumHeight(215);
    chartCol->addWidget(chartView, 1);
#else
    lblChartFallback = new QLabel(tr("QtCharts not enabled in this build"), cardChart);
    lblChartFallback->setAlignment(Qt::AlignCenter);
    chartCol->addWidget(lblChartFallback, 1);
#endif

    // Bottom 4-window summary strip (clickable synchronized period toggles)
    QHBoxLayout* winRow = new QHBoxLayout();
    winRow->setSpacing(8);
    auto makeWinBox = [&](QPushButton*& btnOut, QLabel*& tLbl, QLabel*& vLbl,
                          const QString& title, const QString& tip, const QString& objName) {
        btnOut = new QPushButton(cardChart);
        btnOut->setObjectName(objName);
        btnOut->setCheckable(true);
        btnOut->setCursor(Qt::PointingHandCursor);
        btnOut->setFocusPolicy(Qt::TabFocus);
        btnOut->setMinimumHeight(46);
        btnOut->setToolTip(tip);
        QVBoxLayout* c = new QVBoxLayout(btnOut);
        c->setContentsMargins(8, 6, 8, 6);
        c->setSpacing(2);
        tLbl = new QLabel(title, btnOut);
        tLbl->setAlignment(Qt::AlignCenter);
        tLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        vLbl = new QLabel(QStringLiteral("--"), btnOut);
        vLbl->setAlignment(Qt::AlignCenter);
        vLbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        c->addWidget(tLbl);
        c->addWidget(vLbl);
        winRow->addWidget(btnOut, 1);
    };
    makeWinBox(btnWin24h, lblWin24hTitle, lblWin24hVal, tr("Last 24h"), tr("Switch chart to hourly rewards (24H)"), QStringLiteral("winBox24h"));
    makeWinBox(btnWin7d,  lblWin7dTitle,  lblWin7dVal,  tr("Last 7 Days"), tr("Switch chart to daily rewards (7D)"), QStringLiteral("winBox7d"));
    makeWinBox(btnWin30d, lblWin30dTitle, lblWin30dVal, tr("Last 30 Days"), tr("Switch chart to daily rewards (30D)"), QStringLiteral("winBox30d"));
    makeWinBox(btnWinAll, lblWinAllTitle, lblWinAllVal, tr("All Time"), tr("Switch chart to yearly rewards (All)"), QStringLiteral("winBoxAll"));
    btnWin7d->setChecked(true);
    connect(btnWin24h, &QPushButton::clicked, this, [this]() { setPeriodMode(0); });
    connect(btnWin7d,  &QPushButton::clicked, this, [this]() { setPeriodMode(1); });
    connect(btnWin30d, &QPushButton::clicked, this, [this]() { setPeriodMode(2); });
    connect(btnWinAll, &QPushButton::clicked, this, [this]() { setPeriodMode(4); });
    chartCol->addLayout(winRow);

    leftCol->addWidget(cardChart, 1);

    // Bottom Left Pair: Balance & Collateral Split | Income by Source
    QHBoxLayout* leftBottomRow = new QHBoxLayout();
    leftBottomRow->setSpacing(12);

    cardBalanceBreakdown = new QFrame(overviewContent);
    cardBalanceBreakdown->setObjectName(QStringLiteral("cardStatsBalance"));
    cardBalanceBreakdown->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    themedCards.append(cardBalanceBreakdown);

    QVBoxLayout* balCol = new QVBoxLayout(cardBalanceBreakdown);
    balCol->setContentsMargins(14, 12, 14, 12);
    balCol->setSpacing(4);
    lblBalSectionTitle = new QLabel(tr("Balance & Collateral Split"), cardBalanceBreakdown);
    sectionTitles.append(lblBalSectionTitle);
    balCol->addWidget(lblBalSectionTitle);
    balCol->addSpacing(4);

    AddKeyValueRow(balCol, cardBalanceBreakdown, tr("Available (Spendable):"), lblBalAvailVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(balCol, cardBalanceBreakdown, tr("MN Collateral (4,200×N):"), lblBalMnColVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(balCol, cardBalanceBreakdown, tr("Miner CLTV Locks (250/10):"), lblBalCltvVal, infoRowKeys, infoRowVals, rowDividers, true);
    dividerOtherLock = AddKeyValueRow(balCol, cardBalanceBreakdown, tr("Manual Locked Coins:"), lblBalOtherLockVal, infoRowKeys, infoRowVals, rowDividers, true, &lblBalOtherLockKey);
    AddKeyValueRow(balCol, cardBalanceBreakdown, tr("Immature (<%1 conf):").arg(Params().GetConsensus().nCoinbaseMaturity + 1), lblBalImmatureVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(balCol, cardBalanceBreakdown, tr("Pending (Unconfirmed):"), lblBalPendingVal, infoRowKeys, infoRowVals, rowDividers, false);
    balCol->addStretch(1);

    cardActivity = new QFrame(overviewContent);
    cardActivity->setObjectName(QStringLiteral("cardStatsActivity"));
    cardActivity->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    themedCards.append(cardActivity);

    QVBoxLayout* actCol = new QVBoxLayout(cardActivity);
    actCol->setContentsMargins(14, 12, 14, 12);
    actCol->setSpacing(4);
    lblActSectionTitle = new QLabel(tr("Income by Source"), cardActivity);
    sectionTitles.append(lblActSectionTitle);
    actCol->addWidget(lblActSectionTitle);
    actCol->addSpacing(4);

    AddKeyValueRow(actCol, cardActivity, tr("PoS Staking:"), lblActPosVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(actCol, cardActivity, tr("Masternode & LLMQ:"), lblActMnVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(actCol, cardActivity, tr("ADAM Validator / Mining:"), lblActAdamVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(actCol, cardActivity, tr("PoW Block Producer (Gen):"), lblActGenVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(actCol, cardActivity, tr("External Sent / Received:"), lblActSentRecvVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(actCol, cardActivity, tr("Valid Wallet Transactions:"), lblActTxCountVal, infoRowKeys, infoRowVals, rowDividers, false);
    actCol->addStretch(1);

    leftBottomRow->addWidget(cardBalanceBreakdown, 1);
    leftBottomRow->addWidget(cardActivity, 1);
    leftCol->addLayout(leftBottomRow);

    mainCols->addLayout(leftCol, 3);

    // ================= RIGHT COLUMN (40%) =================
    QVBoxLayout* rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);

    // Card 1: Supply, Decay & Model D Block Split
    cardSupplyEmission = new QFrame(overviewContent);
    cardSupplyEmission->setObjectName(QStringLiteral("cardStatsSupply"));
    themedCards.append(cardSupplyEmission);

    QVBoxLayout* supCol = new QVBoxLayout(cardSupplyEmission);
    supCol->setContentsMargins(14, 12, 14, 12);
    supCol->setSpacing(7);

    lblSupplySectionTitle = new QLabel(tr("Supply & Emission Schedule"), cardSupplyEmission);
    sectionTitles.append(lblSupplySectionTitle);
    supCol->addWidget(lblSupplySectionTitle);

    lblSupplyText = new QLabel(tr("Circulating Supply: --"), cardSupplyEmission);
    barSupply = new QProgressBar(cardSupplyEmission);
    barSupply->setRange(0, 1000);
    barSupply->setTextVisible(false);
    barSupply->setFixedHeight(7);
    supCol->addWidget(lblSupplyText);
    supCol->addWidget(barSupply);

    lblDecayText = new QLabel(tr("Next Emission Decay: --"), cardSupplyEmission);
    barDecay = new QProgressBar(cardSupplyEmission);
    barDecay->setRange(0, 1000);
    barDecay->setTextVisible(false);
    barDecay->setFixedHeight(7);
    supCol->addWidget(lblDecayText);
    supCol->addWidget(barDecay);

    supCol->addSpacing(2);
    supCol->addWidget(CreateHairlineDivider(cardSupplyEmission, rowDividers));
    supCol->addSpacing(2);

    lblModelDSplitTitle = new QLabel(tr("Model D Block Split (Net 93%)"), cardSupplyEmission);
    supCol->addWidget(lblModelDSplitTitle);

    barModelDSplit = new SegmentedBarWidget(8, cardSupplyEmission);
    supCol->addWidget(barModelDSplit);

    AddKeyValueRow(supCol, cardSupplyEmission, tr("ADAM Validators (40% net):"), lblSplitAdamVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(supCol, cardSupplyEmission, tr("Passive Masternode (35% net):"), lblSplitMnVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(supCol, cardSupplyEmission, tr("Block Producer (15% net):"), lblSplitPosVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(supCol, cardSupplyEmission, tr("LLMQ Quorum (10% net):"), lblSplitLlmqVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(supCol, cardSupplyEmission, tr("Developer Treasury (7% gross):"), lblSplitDevVal, infoRowKeys, infoRowVals, rowDividers, false);

    rightCol->addWidget(cardSupplyEmission);

    // Card 2: Consensus Pool & Local Node Status
    cardPoolNode = new QFrame(overviewContent);
    cardPoolNode->setObjectName(QStringLiteral("cardStatsPoolNode"));
    themedCards.append(cardPoolNode);

    QVBoxLayout* poolCol = new QVBoxLayout(cardPoolNode);
    poolCol->setContentsMargins(14, 12, 14, 12);
    poolCol->setSpacing(4);

    lblPoolSectionTitle = new QLabel(tr("Consensus Pool & Node Status"), cardPoolNode);
    sectionTitles.append(lblPoolSectionTitle);
    poolCol->addWidget(lblPoolSectionTitle);
    poolCol->addSpacing(4);

    AddKeyValueRow(poolCol, cardPoolNode, tr("Dual-Track MPA Pool:"), lblPoolMpaVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(poolCol, cardPoolNode, tr("Active KTDIP3 Masternodes:"), lblPoolMnCountVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(poolCol, cardPoolNode, tr("Network Work Rate (120b):"), lblNetHashVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(poolCol, cardPoolNode, tr("Local Internal CPU:"), lblLocalCpuVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(poolCol, cardPoolNode, tr("External RPC Miners:"), lblExternalMinersVal, infoRowKeys, infoRowVals, rowDividers, true);
    AddKeyValueRow(poolCol, cardPoolNode, tr("PoS Staking Status:"), lblStakingVal, infoRowKeys, infoRowVals, rowDividers, false);
    poolCol->addStretch(1);

    rightCol->addWidget(cardPoolNode, 1);
    mainCols->addLayout(rightCol, 2);

    bodyLayout->addLayout(mainCols, 1);

    scrollOverview->setWidget(overviewContent);
    return scrollOverview;
}

QWidget* StatsWidget::buildTelemetryPage()
{
    scrollTelemetry = new QScrollArea(this);
    scrollTelemetry->setWidgetResizable(true);
    scrollTelemetry->setFrameShape(QFrame::NoFrame);
    scrollTelemetry->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    telemetryContent = new QWidget(scrollTelemetry);
    QVBoxLayout* bodyLayout = new QVBoxLayout(telemetryContent);
    bodyLayout->setContentsMargins(0, 0, 4, 4);
    bodyLayout->setSpacing(12);

    // Row 1: 4 Telemetry KPI Cards
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setSpacing(10);

    auto makeTelKpi = [&](QFrame*& card, QLabel*& titleLbl, QLabel*& valLbl, QLabel*& subLbl,
                          const QString& objName, const QString& title) {
        card = new QFrame(telemetryContent);
        card->setObjectName(objName);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        themedCards.append(card);

        QVBoxLayout* col = new QVBoxLayout(card);
        col->setContentsMargins(14, 11, 14, 11);
        col->setSpacing(3);
        titleLbl = new QLabel(title, card);
        valLbl = new QLabel(QStringLiteral("--"), card);
        subLbl = new QLabel(QStringLiteral("--"), card);
        subLbl->setWordWrap(true);
        kpiTitles.append(titleLbl);
        kpiVals.append(valLbl);
        kpiSubs.append(subLbl);
        col->addWidget(titleLbl);
        col->addWidget(valLbl);
        col->addWidget(subLbl);
        kpiRow->addWidget(card, 1);
    };

    makeTelKpi(cardTelBlockTime, lblTelBlockTimeTitle, lblTelBlockTimeVal, lblTelBlockTimeSub,
               QStringLiteral("cardTelBlockTime"), tr("Avg Block Time (120 blk)"));
    makeTelKpi(cardTelDiff, lblTelDiffTitle, lblTelDiffVal, lblTelDiffSub,
               QStringLiteral("cardTelDiff"), tr("Current Difficulty"));
    makeTelKpi(cardTelMempool, lblTelMempoolTitle, lblTelMempoolVal, lblTelMempoolSub,
               QStringLiteral("cardTelMempool"), tr("Mempool"));
    makeTelKpi(cardTelThroughput, lblTelThroughputTitle, lblTelThroughputVal, lblTelThroughputSub,
               QStringLiteral("cardTelThroughput"), tr("Recent Throughput (60 blk)"));

    bodyLayout->addLayout(kpiRow);

    // Row 2: Difficulty Curve (Full Width)
    cardDiffChart = new QFrame(telemetryContent);
    cardDiffChart->setObjectName(QStringLiteral("cardTelDiffChart"));
    cardDiffChart->setMinimumHeight(255);
    themedCards.append(cardDiffChart);

    QVBoxLayout* diffCol = new QVBoxLayout(cardDiffChart);
    diffCol->setContentsMargins(14, 12, 14, 12);
    diffCol->setSpacing(6);

    QHBoxLayout* diffHead = new QHBoxLayout();
    lblDiffChartTitle = new QLabel(tr("Difficulty Curve (Last 120 Blocks)"), cardDiffChart);
    lblDiffChartMeta = new QLabel(QStringLiteral("--"), cardDiffChart);
    lblDiffChartMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sectionTitles.append(lblDiffChartTitle);
    kpiSubs.append(lblDiffChartMeta);
    diffHead->addWidget(lblDiffChartTitle);
    diffHead->addStretch(1);
    diffHead->addWidget(lblDiffChartMeta);
    diffCol->addLayout(diffHead);

#ifdef USE_QTCHARTS
    diffChart = new QChart();
    diffChart->legend()->setVisible(false);
    diffChart->layout()->setContentsMargins(0, 0, 0, 0);
    diffChart->setMargins({0, 2, 0, 0});
    diffChart->setBackgroundRoundness(0);
    diffChart->setAnimationOptions(QChart::NoAnimation);

    diffAxisX = new QValueAxis();
    diffAxisY = new QValueAxis();
    diffChart->addAxis(diffAxisX, Qt::AlignBottom);
    diffChart->addAxis(diffAxisY, Qt::AlignRight);

    diffChartView = new QChartView(diffChart, cardDiffChart);
    diffChartView->setRenderHint(QPainter::Antialiasing);
    diffChartView->setRubberBand(QChartView::NoRubberBand);
    diffChartView->setMinimumHeight(200);
    diffCol->addWidget(diffChartView, 1);
#else
    lblTelDiffFallback = new QLabel(tr("QtCharts not enabled in this build"), cardDiffChart);
    lblTelDiffFallback->setAlignment(Qt::AlignCenter);
    diffCol->addWidget(lblTelDiffFallback, 1);
#endif

    bodyLayout->addWidget(cardDiffChart, 1);

    // Row 3: Block Time Distribution (Left 50%) & Mempool Fee-Rate / TX Activity (Right 50%)
    QHBoxLayout* botTelRow = new QHBoxLayout();
    botTelRow->setSpacing(12);

    // Left: Block Time Distribution Histogram
    cardBlockTimeChart = new QFrame(telemetryContent);
    cardBlockTimeChart->setObjectName(QStringLiteral("cardTelBlockTimeChart"));
    cardBlockTimeChart->setMinimumHeight(265);
    themedCards.append(cardBlockTimeChart);

    QVBoxLayout* btCol = new QVBoxLayout(cardBlockTimeChart);
    btCol->setContentsMargins(14, 12, 14, 12);
    btCol->setSpacing(6);

    QHBoxLayout* btHead = new QHBoxLayout();
    lblBlockTimeChartTitle = new QLabel(tr("Block Time Distribution (120 Blocks)"), cardBlockTimeChart);
    lblBlockTimeChartMeta = new QLabel(QStringLiteral("--"), cardBlockTimeChart);
    lblBlockTimeChartMeta->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sectionTitles.append(lblBlockTimeChartTitle);
    kpiSubs.append(lblBlockTimeChartMeta);
    btHead->addWidget(lblBlockTimeChartTitle);
    btHead->addStretch(1);
    btHead->addWidget(lblBlockTimeChartMeta);
    btCol->addLayout(btHead);

#ifdef USE_QTCHARTS
    blockTimeChart = new QChart();
    blockTimeChart->legend()->setVisible(false);
    blockTimeChart->layout()->setContentsMargins(0, 0, 0, 0);
    blockTimeChart->setMargins({0, 2, 0, 0});
    blockTimeChart->setBackgroundRoundness(0);
    blockTimeChart->setAnimationOptions(QChart::NoAnimation);

    blockTimeAxisX = new QBarCategoryAxis();
    blockTimeAxisY = new QValueAxis();
    blockTimeChart->addAxis(blockTimeAxisX, Qt::AlignBottom);
    blockTimeChart->addAxis(blockTimeAxisY, Qt::AlignRight);

    blockTimeChartView = new QChartView(blockTimeChart, cardBlockTimeChart);
    blockTimeChartView->setRenderHint(QPainter::Antialiasing);
    blockTimeChartView->setRubberBand(QChartView::NoRubberBand);
    blockTimeChartView->setMinimumHeight(205);
    btCol->addWidget(blockTimeChartView, 1);
#else
    lblTelTimeFallback = new QLabel(tr("QtCharts not enabled in this build"), cardBlockTimeChart);
    lblTelTimeFallback->setAlignment(Qt::AlignCenter);
    btCol->addWidget(lblTelTimeFallback, 1);
#endif

    // Right: Mempool Fee-Rate Histogram / Recent Block TX Activity
    cardMempoolChart = new QFrame(telemetryContent);
    cardMempoolChart->setObjectName(QStringLiteral("cardTelMempoolChart"));
    cardMempoolChart->setMinimumHeight(265);
    themedCards.append(cardMempoolChart);

    QVBoxLayout* mpCol = new QVBoxLayout(cardMempoolChart);
    mpCol->setContentsMargins(14, 12, 14, 12);
    mpCol->setSpacing(6);

    QHBoxLayout* mpHead = new QHBoxLayout();
    lblMempoolChartTitle = new QLabel(tr("Mempool Fee-Rate Histogram"), cardMempoolChart);
    lblMempoolChartBadge = new QLabel(tr("Idle"), cardMempoolChart);
    lblMempoolChartBadge->setAlignment(Qt::AlignCenter);
    lblMempoolChartBadge->setFixedHeight(22);
    sectionTitles.append(lblMempoolChartTitle);
    mpHead->addWidget(lblMempoolChartTitle);
    mpHead->addStretch(1);
    mpHead->addWidget(lblMempoolChartBadge);
    mpCol->addLayout(mpHead);

#ifdef USE_QTCHARTS
    mempoolChart = new QChart();
    mempoolChart->legend()->setVisible(false);
    mempoolChart->layout()->setContentsMargins(0, 0, 0, 0);
    mempoolChart->setMargins({0, 2, 0, 0});
    mempoolChart->setBackgroundRoundness(0);
    mempoolChart->setAnimationOptions(QChart::NoAnimation);

    mempoolBarAxisX = new QBarCategoryAxis();
    mempoolValAxisX = new QValueAxis();
    mempoolAxisY = new QValueAxis();
    mempoolChart->addAxis(mempoolBarAxisX, Qt::AlignBottom);
    mempoolChart->addAxis(mempoolValAxisX, Qt::AlignBottom);
    mempoolChart->addAxis(mempoolAxisY, Qt::AlignRight);
    mempoolValAxisX->setVisible(false);

    mempoolChartView = new QChartView(mempoolChart, cardMempoolChart);
    mempoolChartView->setRenderHint(QPainter::Antialiasing);
    mempoolChartView->setRubberBand(QChartView::NoRubberBand);
    mempoolChartView->setMinimumHeight(205);
    mpCol->addWidget(mempoolChartView, 1);
#else
    lblTelMempoolFallback = new QLabel(tr("QtCharts not enabled in this build"), cardMempoolChart);
    lblTelMempoolFallback->setAlignment(Qt::AlignCenter);
    mpCol->addWidget(lblTelMempoolFallback, 1);
#endif

    botTelRow->addWidget(cardBlockTimeChart, 1);
    botTelRow->addWidget(cardMempoolChart, 1);
    bodyLayout->addLayout(botTelRow, 1);

    scrollTelemetry->setWidget(telemetryContent);
    return scrollTelemetry;
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

void StatsWidget::setPeriodMode(int p)
{
    periodMode = p;
    if (btnPeriod24H) btnPeriod24H->setChecked(p == 0);
    if (btnPeriod7D)  btnPeriod7D->setChecked(p == 1);
    if (btnPeriod30D) btnPeriod30D->setChecked(p == 2);
    if (btnPeriod12M) btnPeriod12M->setChecked(p == 3);
    if (btnPeriodAll) btnPeriodAll->setChecked(p == 4);
    if (btnWin24h)    btnWin24h->setChecked(p == 0);
    if (btnWin7d)     btnWin7d->setChecked(p == 1);
    if (btnWin30d)    btnWin30d->setChecked(p == 2);
    if (btnWinAll)    btnWinAll->setChecked(p == 4);
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
    if (!isFlushingOnShow()) {
        refreshStats();
    }
}

void StatsWidget::updateThemeStyles()
{
    const bool light = isLightTheme();
    const QString pageBg      = light ? QStringLiteral("#F4F6FA") : QStringLiteral("#0B111E");
    const QString cardBg      = light ? QStringLiteral("#FFFFFF") : QStringLiteral("#131C2E");
    const QString cardBorder  = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#23324D");
    const QString dividerBg   = light ? QStringLiteral("#EDF2F7") : QStringLiteral("#1D2A42");
    const QString textPrimary = light ? QStringLiteral("#0F172A") : QStringLiteral("#F8FAFC");
    const QString textLabel   = light ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString textSubtle  = light ? QStringLiteral("#475569") : QStringLiteral("#B8C7DC");
    const QString pillBg      = light ? QStringLiteral("#F8FAFC") : QStringLiteral("#19253B");
    const QString trackBg     = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#1F2D46");

    this->setStyleSheet(QString("StatsWidget { background-color: %1; }").arg(pageBg));
    if (scrollOverview)   scrollOverview->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    if (overviewContent)  overviewContent->setStyleSheet("QWidget { background: transparent; }");
    if (scrollTelemetry)  scrollTelemetry->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    if (telemetryContent) telemetryContent->setStyleSheet("QWidget { background: transparent; }");

    if (lblTitle) lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 20px; font-weight: bold; }").arg(textPrimary));
    if (lblSubtitle) lblSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textSubtle));

    if (viewSwitchFrame) {
        viewSwitchFrame->setStyleSheet(QString(
            "QFrame#statsViewSwitchFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(pillBg, cardBorder));
    }

    const QString activeBg   = light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString activeFg   = light ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString activeHov  = light ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860");
    const QString activeHovB = light ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0");
    const QString focusRing  = light ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");

    const QString segTabCss = QString(
        "QPushButton { background-color: transparent; color: %1; border: 1px solid transparent; border-radius: 6px; padding: 0px 14px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { color: %2; }"
        "QPushButton:checked { background-color: %3; color: %4; border: 1px solid #7695CD; }"
        "QPushButton:checked:hover { background-color: %5; border-color: %6; }"
        "QPushButton:focus { border: 2px solid %7; outline: none; }"
    ).arg(textLabel, textPrimary, activeBg, activeFg, activeHov, activeHovB, focusRing);
    for (QPushButton* b : {btnTabOverview, btnTabTelemetry}) {
        if (b) b->setStyleSheet(segTabCss);
    }

    if (btnRefresh) {
        btnRefresh->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 7px; padding: 0px 14px; font-size: 12px; font-weight: 600; }"
            "QPushButton:hover { background-color: %3; color: %4; border-color: %5; }"
            "QPushButton:pressed { background-color: %6; color: %2; }"
            "QPushButton:focus { border: 2px solid %7; outline: none; }"
        ).arg(activeBg, activeFg, activeHov,
              light ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA"),
              activeHovB,
              light ? QStringLiteral("#CEDEF4") : QStringLiteral("#16233D"),
              focusRing));
    }

    for (QFrame* f : themedCards) {
        if (f && !f->objectName().isEmpty()) {
            f->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
            ).arg(f->objectName(), cardBg, cardBorder));
        }
    }

    for (QFrame* d : rowDividers) {
        if (d) d->setStyleSheet(QString("QFrame { background-color: %1; border: none; }").arg(dividerBg));
    }

    for (QFrame* box : subtleBoxes) {
        if (box && !box->objectName().isEmpty()) {
            box->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 8px; }"
            ).arg(box->objectName(), pillBg, cardBorder));
        }
    }

    for (QLabel* t : kpiTitles) {
        if (t) t->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(textLabel));
    }
    for (QLabel* v : kpiVals) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 18px; font-weight: bold; }").arg(textPrimary));
    }
    for (QLabel* s : kpiSubs) {
        if (s) s->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(textSubtle));
    }

    for (QLabel* sec : sectionTitles) {
        if (sec) sec->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: bold; }").arg(textPrimary));
    }

    if (lblLegPos)  lblLegPos->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(light ? QStringLiteral("#D95F52") : QStringLiteral("#FF9B8E")));
    if (lblLegMn)   lblLegMn->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(light ? QStringLiteral("#4B6EB1") : QStringLiteral("#93B4F0")));
    if (lblLegAdam) lblLegAdam->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; }").arg(light ? QStringLiteral("#6356B5") : QStringLiteral("#ACA3EA")));

    const QString winBoxCss = QString(
        "QPushButton { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 0px; }"
        "QPushButton:hover { border-color: #7695CD; }"
        "QPushButton:checked { background-color: %3; border: 1.5px solid #7695CD; }"
        "QPushButton:checked:hover { background-color: %4; border-color: %5; }"
        "QPushButton:focus { border: 2px solid %6; outline: none; }"
    ).arg(pillBg, cardBorder, activeBg, activeHov, activeHovB, focusRing);
    for (QPushButton* wb : {btnWin24h, btnWin7d, btnWin30d, btnWinAll}) {
        if (wb) wb->setStyleSheet(winBoxCss);
    }

    for (QLabel* t : {lblWin24hTitle, lblWin7dTitle, lblWin30dTitle, lblWinAllTitle}) {
        if (t) t->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: bold; background: transparent; }").arg(textLabel));
    }
    for (QLabel* v : {lblWin24hVal, lblWin7dVal, lblWin30dVal, lblWinAllVal}) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; background: transparent; }").arg(textPrimary));
    }

    for (QLabel* k : infoRowKeys) {
        if (k) k->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: 500; }").arg(textLabel));
    }
    for (QLabel* v : infoRowVals) {
        if (v) v->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textPrimary));
    }
    for (QLabel* pLbl : {lblSupplyText, lblDecayText, lblModelDSplitTitle}) {
        if (pLbl) pLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textPrimary));
    }

    const QString pillCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0px 9px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { border-color: #7695CD; color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; border: 1px solid #7695CD; }"
        "QPushButton:checked:hover { background-color: %7; border-color: %8; }"
        "QPushButton:focus { border: 2px solid %9; outline: none; }"
    ).arg(pillBg, textLabel, cardBorder, textPrimary, activeBg, activeFg, activeHov, activeHovB, focusRing);
    for (QPushButton* b : {btnPeriod24H, btnPeriod7D, btnPeriod30D, btnPeriod12M, btnPeriodAll,
                           btnViewBars, btnViewTrend, btnViewSplit}) {
        if (b) b->setStyleSheet(pillCss);
    }

    auto styleBar = [&](QProgressBar* bar, const QString& chunkColor) {
        if (!bar) return;
        bar->setStyleSheet(QString(
            "QProgressBar { background-color: %1; border: none; border-radius: 3px; }"
            "QProgressBar::chunk { background-color: %2; border-radius: 3px; }"
        ).arg(trackBg, chunkColor));
    };
    styleBar(barSupply, QStringLiteral("#7695CD"));
    styleBar(barDecay,  QStringLiteral("#F5897C"));

    const QColor trackCol(trackBg);
    if (barKpiBalance)  barKpiBalance->setTrackColor(trackCol);
    if (barModelDSplit) barModelDSplit->setTrackColor(trackCol);

#ifdef USE_QTCHARTS
    StyleCartesianChart(chart, axisX, axisY, light);
    StyleCartesianChart(diffChart, diffAxisX, diffAxisY, light);
    StyleCartesianChart(blockTimeChart, blockTimeAxisX, blockTimeAxisY, light);
    StyleCartesianChart(mempoolChart, mempoolBarAxisX, mempoolAxisY, light);
    if (mempoolValAxisX) {
        const QColor grid = light ? QColor("#1A000000") : QColor("#30FFFFFF");
        const QColor labelCol = light ? QColor("#475569") : QColor("#CBD5E1");
        const QColor bg = light ? QColor("#FFFFFF") : QColor("#131C2E");
        mempoolValAxisX->setGridLineColor(bg);
        mempoolValAxisX->setLabelsColor(labelCol);
        mempoolValAxisX->setLinePenColor(grid);
        QFont xf = mempoolValAxisX->labelsFont();
        xf.setPointSize(9);
        mempoolValAxisX->setLabelsFont(xf);
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

QString StatsWidget::formatHashRate(double hashesPerSec) const
{
    if (hashesPerSec <= 0.0) {
        return QStringLiteral("0.00 H/s");
    }
    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"};
    int unitIdx = 0;
    double val = hashesPerSec;
    while (val >= 1000.0 && unitIdx < 5) {
        val /= 1000.0;
        ++unitIdx;
    }
    return QString("%1 %2").arg(val, 0, 'f', 3).arg(units[unitIdx]);
}

QString StatsWidget::formatDifficulty(double diff) const
{
    return FormatDifficultyLocal(diff);
}

QString StatsWidget::formatBytes(uint64_t bytes) const
{
    if (bytes < 1024ULL) return QString("%1 B").arg(bytes);
    if (bytes < 1024ULL * 1024ULL) {
        return QString("%1 kB").arg(static_cast<double>(bytes) / 1024.0, 0, 'f', 1);
    }
    return QString("%1 MB").arg(static_cast<double>(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
}

QString StatsWidget::formatDurationSeconds(qint64 seconds) const
{
    if (seconds < 0) seconds = 0;
    const qint64 days = seconds / 86400;
    const qint64 hours = (seconds % 86400) / 3600;
    const qint64 mins = (seconds % 3600) / 60;
    const qint64 secs = seconds % 60;
    if (days > 0) return tr("%1d %2h").arg(days).arg(hours);
    if (hours > 0) return tr("%1h %2m").arg(hours).arg(mins);
    if (mins > 0) return tr("%1m").arg(mins);
    return tr("%1s").arg(secs);
}

StatsWidget::RewardStatsSnapshot StatsWidget::collectRewardSnapshot() const
{
    RewardStatsSnapshot snap;
    if (!walletModel) {
        return snap;
    }

    WalletModel::WalletActivitySummary activity;
    if (!walletModel->getWalletActivitySummary(activity, false)) {
        return snap;
    }

    snap.countTotalTx = activity.countTotalTx;
    snap.countExcludedOrphan = activity.countExcludedOrphan;
    snap.totalSent = activity.totalSent;
    snap.totalReceived = activity.totalReceived;

    const QDateTime nowDt = QDateTime::currentDateTime();
    const QDate today = nowDt.date();
    const QDateTime currentHourTop(today, QTime(nowDt.time().hour(), 0, 0));
    const QDateTime dt24h = nowDt.addDays(-1);
    const QDateTime dt7d  = nowDt.addDays(-7);
    const QDateTime dt30d = nowDt.addDays(-30);

    QMap<QString, qint64> bucketPos;
    QMap<QString, qint64> bucketMnLlmq;
    QMap<QString, qint64> bucketAdamPow;
    QStringList orderedKeys;
    QSet<QString> seenShortLabels;

    auto pushUniqueAxisLabel = [&](const QString& preferred, const QString& fallbackSuffix) {
        QString candidate = preferred;
        if (seenShortLabels.contains(candidate)) {
            candidate = preferred + fallbackSuffix;
        }
        int salt = 1;
        while (seenShortLabels.contains(candidate)) {
            candidate = QString("%1.%2").arg(preferred).arg(++salt);
        }
        seenShortLabels.insert(candidate);
        snap.labels << candidate;
    };

    if (periodMode == 0) { // 24H hourly
        for (int i = 23; i >= 0; --i) {
            const QDateTime h = currentHourTop.addSecs(-3600LL * i);
            const QString key = h.toString(QStringLiteral("yyyy-MM-dd HH"));
            orderedKeys << key;
            pushUniqueAxisLabel(h.toString(QStringLiteral("HH")), QStringLiteral("*"));
            snap.tooltipLabels << h.toString(QStringLiteral("dd/MM HH:00"));
        }
    } else if (periodMode == 1) { // 7D daily
        for (int i = 6; i >= 0; --i) {
            const QDate d = today.addDays(-i);
            orderedKeys << d.toString(QStringLiteral("yyyy-MM-dd"));
            pushUniqueAxisLabel(d.toString(QStringLiteral("ddd dd")), d.toString(QStringLiteral("/MM")));
            snap.tooltipLabels << d.toString(QStringLiteral("ddd, dd MMM yyyy"));
        }
    } else if (periodMode == 2) { // 30D daily
        for (int i = 29; i >= 0; --i) {
            const QDate d = today.addDays(-i);
            orderedKeys << d.toString(QStringLiteral("yyyy-MM-dd"));
            pushUniqueAxisLabel(d.toString(QStringLiteral("d/M")), d.toString(QStringLiteral("/yy")));
            snap.tooltipLabels << d.toString(QStringLiteral("dd MMM yyyy"));
        }
    } else if (periodMode == 3) { // 12M monthly
        for (int i = 11; i >= 0; --i) {
            const QDate m = today.addMonths(-i);
            orderedKeys << m.toString(QStringLiteral("yyyy-MM"));
            pushUniqueAxisLabel(m.toString(QStringLiteral("MMM yy")), QStringLiteral("b"));
            snap.tooltipLabels << m.toString(QStringLiteral("MMMM yyyy"));
        }
    }

    int minYear = today.year();
    int maxYear = today.year();

    for (const WalletModel::WalletActivityEntry& entry : activity.rewards) {
        const qint64 absAmt = entry.amount;
        const QDateTime txDt = QDateTime::fromSecsSinceEpoch(entry.time);

        const bool isPos    = (entry.type == TransactionRecord::StakeMint);
        const bool isMnLlmq = (entry.type == TransactionRecord::MNReward || entry.type == TransactionRecord::LlmqReward);
        const bool isAdam   = (entry.type == TransactionRecord::AdamReward);

        if (isPos) {
            snap.totalPos += absAmt;
            ++snap.countPos;
        } else if (isMnLlmq) {
            snap.totalMnLlmq += absAmt;
            ++snap.countMnLlmq;
        } else if (isAdam) {
            snap.totalAdam += absAmt;
            ++snap.countAdam;
        } else {
            snap.totalGenerated += absAmt;
            ++snap.countGenerated;
        }

        if (txDt >= dt24h) snap.last24h += absAmt;
        if (txDt >= dt7d)  snap.last7d  += absAmt;
        if (txDt >= dt30d) snap.last30d += absAmt;

        QString k;
        if (periodMode == 0) {
            k = txDt.toString(QStringLiteral("yyyy-MM-dd HH"));
        } else if (periodMode == 1 || periodMode == 2) {
            k = txDt.date().toString(QStringLiteral("yyyy-MM-dd"));
        } else if (periodMode == 3) {
            k = txDt.date().toString(QStringLiteral("yyyy-MM"));
        } else {
            const int y = txDt.date().year();
            if (y > 0) {
                minYear = std::min(minYear, y);
                maxYear = std::max(maxYear, y);
                k = QString::number(y);
            }
        }

        if (!k.isEmpty()) {
            if (isPos) bucketPos[k] += absAmt;
            else if (isMnLlmq) bucketMnLlmq[k] += absAmt;
            else bucketAdamPow[k] += absAmt;
        }
    }

    if (periodMode == 4) {
        for (int y = minYear; y <= maxYear; ++y) {
            const QString k = QString::number(y);
            orderedKeys << k;
            snap.labels << k;
            snap.tooltipLabels << k;
        }
    }

    for (const QString& k : orderedKeys) {
        const qint64 p = bucketPos.value(k, 0);
        const qint64 m = bucketMnLlmq.value(k, 0);
        const qint64 a = bucketAdamPow.value(k, 0);
        snap.periodPos += p;
        snap.periodMnLlmq += m;
        snap.periodAdamPow += a;
        snap.posSats.append(static_cast<CAmount>(p));
        snap.mnLlmqSats.append(static_cast<CAmount>(m));
        snap.adamPowSats.append(static_cast<CAmount>(a));
        snap.posSeries.append(static_cast<double>(p) / static_cast<double>(COIN));
        snap.mnLlmqSeries.append(static_cast<double>(m) / static_cast<double>(COIN));
        snap.adamPowSeries.append(static_cast<double>(a) / static_cast<double>(COIN));
    }

    return snap;
}

StatsWidget::LockedBreakdown StatsWidget::collectLockedBreakdown(CAmount expectedLockedTotal) const
{
    LockedBreakdown out;
#ifdef ENABLE_WALLET
    TRY_LOCK(cs_main, lockMain);
    if (!lockMain || !pwalletMain) {
        return out;
    }
    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet) {
        return out;
    }

    out.resolved = true;
    std::set<COutPoint> countedOutpoints;
    const int nChainHeight = chainActive.Height();
    const int64_t nMedianTimePast = chainActive.Tip() ? chainActive.Tip()->GetMedianTimePast() : GetAdjustedTime();
    const CAmount collateralAmount = CMasternode::GetMasternodeNodeCollateral(nChainHeight);

    auto isCltvActive = [&](int64_t lockTime) -> bool {
        return (lockTime < LOCKTIME_THRESHOLD)
            ? (nChainHeight < lockTime)
            : (nMedianTimePast < lockTime);
    };

    auto classifyLockedOutput = [&](const CTxOut& txout, CAmount credit) {
        CPubKey pubkey;
        int64_t lockTime = 0;
        CKeyID pubkeyHash;
        std::vector<unsigned char> nonce;
        uint256 challenge;
        if (MatchCoinLockRegistration(txout.scriptPubKey, pubkey, lockTime, pubkeyHash)) {
            if (isCltvActive(lockTime)) {
                out.minerCltvAmount += credit;
                ++out.coinLock250Count;
            } else {
                out.otherManualAmount += credit;
                ++out.otherManualCount;
            }
        } else if (MatchPoWLockRegistration(txout.scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash)) {
            if (isCltvActive(lockTime)) {
                out.minerCltvAmount += credit;
                ++out.powLock10Count;
            } else {
                out.otherManualAmount += credit;
                ++out.otherManualCount;
            }
        } else if (txout.nValue == collateralAmount) {
            out.mnCollateralAmount += credit;
            ++out.mnCollateralCount;
        } else {
            out.otherManualAmount += credit;
            ++out.otherManualCount;
        }
    };

    // 1. Explicitly locked coins (Coin Control locks or MN/registration manual locks)
    std::vector<COutPoint> vLocked;
    pwalletMain->ListLockedCoins(vLocked);
    for (const COutPoint& outpoint : vLocked) {
        if (pwalletMain->IsSpent(outpoint.hash, outpoint.n)) continue;
        if (pcoinsTip && !pcoinsTip->HaveCoin(outpoint)) continue;

        const CWalletTx* wtx = pwalletMain->GetWalletTx(outpoint.hash);
        if (wtx && outpoint.n < wtx->vout.size()) {
            const CTxOut& txout = wtx->vout[outpoint.n];
            const CAmount credit = pwalletMain->GetCredit(txout, ISMINE_SPENDABLE_ALL);
            if (credit > 0) {
                classifyLockedOutput(txout, credit);
            }
            countedOutpoints.insert(outpoint);
        } else if (pcoinsTip && pcoinsTip->HaveCoin(outpoint)) {
            Coin coin;
            if (pcoinsTip->GetCoin(outpoint, coin)) {
                const CAmount credit = pwalletMain->GetCredit(coin.out, ISMINE_SPENDABLE_ALL);
                if (credit > 0) {
                    classifyLockedOutput(coin.out, credit);
                }
                countedOutpoints.insert(outpoint);
            }
        }
    }

    // 2. Implicit Masternode 4,200 KRISTA collaterals when fMasterNode is enabled
    if (fMasterNode) {
        for (const auto& item : pwalletMain->mapWallet) {
            const CWalletTx& wtx = item.second;
            if (wtx.IsCoinBase() || wtx.IsCoinStake()) continue;
            bool hasCandidate = false;
            for (const CTxOut& txout : wtx.vout) {
                if (txout.nValue == collateralAmount) {
                    hasCandidate = true;
                    break;
                }
            }
            if (!hasCandidate) continue;
            if (!wtx.IsTrusted() || wtx.GetDepthInMainChain() == 0) continue;
            for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
                if (wtx.vout[i].nValue != collateralAmount) continue;
                const COutPoint outpoint(wtx.GetHash(), i);
                if (countedOutpoints.count(outpoint)) continue;
                if (!pwalletMain->IsSpent(outpoint.hash, outpoint.n)) {
                    if (pcoinsTip && !pcoinsTip->HaveCoin(outpoint)) continue;
                    const CAmount credit = pwalletMain->GetCredit(wtx.vout[i], ISMINE_SPENDABLE);
                    if (credit > 0) {
                        out.mnCollateralAmount += credit;
                        ++out.mnCollateralCount;
                    }
                    countedOutpoints.insert(outpoint);
                }
            }
        }
    }

    // 3. Implicit consensus-locked ADAM Coin-Lock / PoW-Lock CLTV registrations
    for (const auto& item : pwalletMain->mapWallet) {
        const CWalletTx& wtx = item.second;
        if (wtx.IsCoinBase() || wtx.IsCoinStake()) continue;
        bool hasCandidate = false;
        for (const CTxOut& txout : wtx.vout) {
            if (txout.scriptPubKey.size() >= 60 && txout.nValue > 0) {
                hasCandidate = true;
                break;
            }
        }
        if (!hasCandidate) continue;
        if (!wtx.IsTrusted() || wtx.GetDepthInMainChain() == 0) continue;
        for (unsigned int i = 0; i < wtx.vout.size(); ++i) {
            if (wtx.vout[i].scriptPubKey.size() < 60 || wtx.vout[i].nValue <= 0) continue;
            const COutPoint outpoint(wtx.GetHash(), i);
            if (countedOutpoints.count(outpoint)) continue;
            if (pwalletMain->IsSpent(outpoint.hash, outpoint.n)) continue;
            if (pcoinsTip && !pcoinsTip->HaveCoin(outpoint)) continue;

            CPubKey pubkey;
            int64_t lockTime = 0;
            CKeyID pubkeyHash;
            std::vector<unsigned char> nonce;
            uint256 challenge;
            const bool isCoinLock = MatchCoinLockRegistration(wtx.vout[i].scriptPubKey, pubkey, lockTime, pubkeyHash);
            const bool isPowLock  = !isCoinLock && MatchPoWLockRegistration(wtx.vout[i].scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash);
            if (isCoinLock || isPowLock) {
                const bool isLocked = (lockTime < LOCKTIME_THRESHOLD)
                    ? (nChainHeight < lockTime)
                    : (nMedianTimePast < lockTime);
                if (isLocked) {
                    const CAmount credit = pwalletMain->GetCredit(wtx.vout[i], ISMINE_SPENDABLE);
                    if (credit > 0) {
                        out.minerCltvAmount += credit;
                        if (isCoinLock) ++out.coinLock250Count;
                        else ++out.powLock10Count;
                    }
                    countedOutpoints.insert(outpoint);
                }
            }
        }
    }

    const CAmount accounted = out.mnCollateralAmount + out.minerCltvAmount + out.otherManualAmount;
    if (expectedLockedTotal > accounted) {
        out.otherManualAmount += (expectedLockedTotal - accounted);
    }
#else
    Q_UNUSED(expectedLockedTotal);
#endif
    return out;
}

StatsWidget::TelemetrySnapshot StatsWidget::collectTelemetrySnapshot() const
{
    TelemetrySnapshot tel;
    tel.targetSpacingSec = std::max<qint64>(1, Params().GetConsensus().nTargetSpacing);

    // 1. Chain Telemetry (120-block curve & spacing, 60-block throughput) — RAM CBlockIndex only
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain && chainActive.Tip()) {
            const int windowCount = std::min(121, chainActive.Height() + 1);
            std::vector<const CBlockIndex*> chainSlice;
            chainSlice.reserve(windowCount);

            const CBlockIndex* pb = chainActive.Tip();
            for (int i = 0; i < windowCount && pb != nullptr; ++i) {
                chainSlice.push_back(pb);
                pb = pb->pprev;
            }
            std::reverse(chainSlice.begin(), chainSlice.end());

            if (!chainSlice.empty()) {
                tel.hasChain = true;
                const size_t startIdx = (chainSlice.size() > 120) ? 1 : 0;
                tel.diffMin = DifficultyFromBits(chainSlice[startIdx]->nBits);
                tel.diffMax = tel.diffMin;

                std::vector<qint64> sortedDeltas;
                qint64 sumDeltas = 0;

                for (size_t i = startIdx; i < chainSlice.size(); ++i) {
                    const CBlockIndex* cur = chainSlice[i];
                    const double d = DifficultyFromBits(cur->nBits);
                    const qint64 t = static_cast<qint64>(cur->GetBlockTime());
                    const qint64 delta = (i > 0)
                        ? std::max<qint64>(0, t - static_cast<qint64>(chainSlice[i - 1]->GetBlockTime()))
                        : tel.targetSpacingSec;

                    tel.heights.append(cur->nHeight);
                    tel.difficulties.append(d);
                    tel.blockTimesSec.append(t);
                    tel.blockDeltasSec.append(delta);
                    tel.blockTxCounts.append(static_cast<int>(cur->nTx));

                    tel.diffMin = std::min(tel.diffMin, d);
                    tel.diffMax = std::max(tel.diffMax, d);

                    if (i > 0) {
                        sortedDeltas.push_back(delta);
                        sumDeltas += delta;
                        if (delta < 15)       ++tel.deltaBuckets[0];
                        else if (delta < 25)  ++tel.deltaBuckets[1];
                        else if (delta <= 35) ++tel.deltaBuckets[2];
                        else if (delta <= 50) ++tel.deltaBuckets[3];
                        else if (delta <= 90) ++tel.deltaBuckets[4];
                        else                  ++tel.deltaBuckets[5];
                    }
                }

                tel.currentDifficulty = tel.difficulties.last();
                if (tel.difficulties.first() > 0.0) {
                    tel.diffChangePct = 100.0 * (tel.currentDifficulty - tel.difficulties.first()) / tel.difficulties.first();
                }

                if (!sortedDeltas.empty()) {
                    tel.avgBlockTimeSec = static_cast<double>(sumDeltas) / static_cast<double>(sortedDeltas.size());
                    std::sort(sortedDeltas.begin(), sortedDeltas.end());
                    const size_t mid = sortedDeltas.size() / 2;
                    if (sortedDeltas.size() % 2 == 1) {
                        tel.medianBlockTimeSec = static_cast<double>(sortedDeltas[mid]);
                    } else {
                        tel.medianBlockTimeSec = 0.5 * static_cast<double>(sortedDeltas[mid - 1] + sortedDeltas[mid]);
                    }
                } else {
                    tel.avgBlockTimeSec = static_cast<double>(tel.targetSpacingSec);
                    tel.medianBlockTimeSec = tel.avgBlockTimeSec;
                }

                const int totalPts = tel.blockTxCounts.size();
                const int thrStart = std::max(0, totalPts - 60);
                tel.recent60Blocks = totalPts - thrStart;
                for (int i = thrStart; i < totalPts; ++i) {
                    const int ntx = tel.blockTxCounts.at(i);
                    tel.recent60TxTotal += ntx;
                    tel.recent60PeakTx = std::max(tel.recent60PeakTx, ntx);
                }
                if (tel.recent60Blocks >= 2) {
                    tel.recent60SpanSec = std::max<qint64>(1,
                        tel.blockTimesSec.last() - tel.blockTimesSec.at(thrStart));
                }
            }
        }
    }

    // 2. Mempool Telemetry — strictly standalone TRY_LOCK(mempool.cs)
    {
        TRY_LOCK(mempool.cs, lockMempool);
        if (lockMempool) {
            tel.hasMempool = true;
            tel.mempoolTxCount = static_cast<int>(mempool.mapTx.size());
            tel.mempoolDynamicBytes = mempool.DynamicMemoryUsage();

            std::vector<double> feeRates;
            feeRates.reserve(mempool.mapTx.size());
            uint64_t totalBytes = 0;

            for (const auto& entry : mempool.mapTx) {
                const size_t txBytes = entry.GetTxSize();
                totalBytes += txBytes;
                const double rate = (txBytes > 0)
                    ? (static_cast<double>(entry.GetFee()) / static_cast<double>(txBytes))
                    : 0.0;
                feeRates.push_back(rate);

                if (rate < 1.0)       ++tel.feeBuckets[0];
                else if (rate < 5.0)  ++tel.feeBuckets[1];
                else if (rate < 20.0) ++tel.feeBuckets[2];
                else if (rate < 50.0) ++tel.feeBuckets[3];
                else                  ++tel.feeBuckets[4];
            }

            tel.mempoolTotalBytes = totalBytes;
            if (!feeRates.empty()) {
                std::sort(feeRates.begin(), feeRates.end());
                const size_t mid = feeRates.size() / 2;
                if (feeRates.size() % 2 == 1) {
                    tel.medianFeeSatPerByte = feeRates[mid];
                } else {
                    tel.medianFeeSatPerByte = 0.5 * (feeRates[mid - 1] + feeRates[mid]);
                }
            }
        }
    }

    return tel;
}

static double niceAxisMax(double rawMax)
{
    if (rawMax <= 0.0) return 1.0;
    const double rawStep = (rawMax * 1.12) / 4.0;
    const double mag = std::pow(10.0, std::floor(std::log10(rawStep)));
    const double norm = rawStep / mag;
    double niceNorm = 10.0;
    if (norm <= 1.0) niceNorm = 1.0;
    else if (norm <= 2.0) niceNorm = 2.0;
    else if (norm <= 2.5) niceNorm = 2.5;
    else if (norm <= 5.0) niceNorm = 5.0;
    return niceNorm * mag * 4.0;
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
    const QStringList tipLabelsCopy = snap.tooltipLabels;
    const bool light = isLightTheme();

    const CAmount periodSum = snap.periodPos + snap.periodMnLlmq + snap.periodAdamPow;
    if (lblChartPeriodTotal) {
        const CAmount allTimeSum = snap.totalPos + snap.totalMnLlmq + snap.totalAdam + snap.totalGenerated;
        const CAmount shown = (chartMode == 2 && periodSum == 0) ? allTimeSum : periodSum;
        lblChartPeriodTotal->setText(tr("Shown: +%1").arg(formatAmountPlain(shown, true)));
    }

    if (chartMode == 2) {
        axisX->setVisible(false);
        axisY->setVisible(false);
        chart->setMargins({12, 14, 12, 10});

        QPieSeries* pie = new QPieSeries(chart);
        pie->setHoleSize(0.52);
        pie->setPieSize(0.64);

        const bool usePeriod = (periodSum > 0);
        const CAmount posVal  = usePeriod ? snap.periodPos     : snap.totalPos;
        const CAmount mnVal   = usePeriod ? snap.periodMnLlmq  : snap.totalMnLlmq;
        const CAmount adamVal = usePeriod ? snap.periodAdamPow : (snap.totalAdam + snap.totalGenerated);
        const CAmount total   = posVal + mnVal + adamVal;

        const QColor borderCol = light ? QColor("#FFFFFF") : QColor("#131C2E");
        const QColor labelCol  = light ? QColor("#334155") : QColor("#E2E8F0");

        auto addSlice = [&](const QString& shortLabel, const QString& fullTitle, CAmount amt, const QColor& col) {
            if (amt <= 0) return;
            const double pct = (total > 0) ? (100.0 * static_cast<double>(amt) / static_cast<double>(total)) : 0.0;
            QPieSlice* s = pie->append(QString("%1 (%2%)").arg(shortLabel).arg(pct, 0, 'f', 1),
                                       static_cast<double>(amt) / static_cast<double>(COIN));
            s->setColor(col);
            s->setBorderColor(borderCol);
            s->setBorderWidth(2);
            s->setLabelColor(labelCol);
            s->setLabelArmLengthFactor(0.10);
            s->setLabelVisible(true);
            s->setProperty("exactAmount", static_cast<qint64>(amt));
            s->setProperty("trackTitle", fullTitle);
        };

        addSlice(tr("PoS"), tr("PoS Staking"), posVal, COLOR_POS);
        addSlice(tr("MN & LLMQ"), tr("Masternode & LLMQ"), mnVal, COLOR_MN);
        addSlice(tr("ADAM / PoW"), tr("ADAM Validator & PoW Mining"), adamVal, COLOR_ADAM);

        if (total <= 0) {
            QPieSlice* emptySlice = pie->append(tr("No Rewards Yet"), 1.0);
            emptySlice->setColor(light ? QColor("#E2E8F0") : QColor("#1F2D46"));
            emptySlice->setBorderColor(borderCol);
            emptySlice->setBorderWidth(2);
            emptySlice->setLabelColor(labelCol);
            emptySlice->setLabelVisible(true);
            emptySlice->setProperty("exactAmount", 0);
            emptySlice->setProperty("trackTitle", tr("No Rewards"));
        }

        connect(pie, &QPieSeries::hovered, pie, [unitCopy, usePeriod, total](QPieSlice* slice, bool state) {
            if (!state || !slice || total <= 0) {
                QToolTip::hideText();
                return;
            }
            const CAmount amt = static_cast<CAmount>(slice->property("exactAmount").toLongLong());
            const QString title = slice->property("trackTitle").toString();
            const QString scopeNote = usePeriod ? QString() : QObject::tr("\n(All-time fallback: 0 in selected window)");
            QToolTip::showText(QCursor::pos(),
                QString("%1 (%2%)\n%3%4").arg(title)
                    .arg(slice->percentage() * 100.0, 0, 'f', 1)
                    .arg(BitcoinUnits::formatWithUnit(unitCopy, amt, false, BitcoinUnits::separatorStandard), scopeNote));
        });

        chart->addSeries(pie);
    } else if (chartMode == 1) {
        axisX->setVisible(true);
        axisY->setVisible(true);
        chart->setMargins({0, 2, 0, 0});
        axisX->append(snap.labels);

        QFont xf = axisX->labelsFont();
        xf.setPointSize(periodMode == 2 ? 8 : 9);
        axisX->setLabelsFont(xf);

        const double unitScale = (unitCopy == BitcoinUnits::mPIV) ? 1000.0 : (unitCopy == BitcoinUnits::uPIV ? 1000000.0 : 1.0);
        QLineSeries* upper = new QLineSeries();
        const int count = std::min({snap.posSats.size(), snap.mnLlmqSats.size(), snap.adamPowSats.size()});
        CAmount runningSat = 0;
        double running = 0.0;
        QVector<CAmount> deltaSats;
        QVector<CAmount> cumSats;
        deltaSats.reserve(count);
        cumSats.reserve(count);
        for (int i = 0; i < count; ++i) {
            const CAmount dSat = snap.posSats.at(i) + snap.mnLlmqSats.at(i) + snap.adamPowSats.at(i);
            runningSat += dSat;
            running = (static_cast<double>(runningSat) / static_cast<double>(COIN)) * unitScale;
            deltaSats.append(dSat);
            cumSats.append(runningSat);
            upper->append(i, running);
        }

        const QColor lineCol = COLOR_MN;
        QColor fillCol = lineCol;
        fillCol.setAlpha(60);

        QAreaSeries* area = new QAreaSeries(upper, nullptr);
        upper->setParent(area);
        area->setPen(QPen(lineCol, 2.4));
        area->setBrush(QBrush(fillCol));
        area->setPointsVisible(count <= 31);

        connect(area, &QAreaSeries::hovered, area, [labelsCopy, tipLabelsCopy, cumSats, deltaSats, unitCopy](const QPointF& pt, bool state) {
            if (!state || labelsCopy.isEmpty()) {
                QToolTip::hideText();
                return;
            }
            const int idx = std::max(0, std::min(static_cast<int>(std::llround(pt.x())), labelsCopy.size() - 1));
            const QString period = (idx >= 0 && idx < tipLabelsCopy.size())
                ? tipLabelsCopy.at(idx)
                : ((idx >= 0 && idx < labelsCopy.size()) ? labelsCopy.at(idx) : QString::number(idx + 1));
            const CAmount cumSat = (idx >= 0 && idx < cumSats.size()) ? cumSats.at(idx) : 0;
            const CAmount perSat = (idx >= 0 && idx < deltaSats.size()) ? deltaSats.at(idx) : 0;
            QToolTip::showText(QCursor::pos(),
                QString("%1\nCumulative: %2\nPeriod: +%3")
                    .arg(period,
                         BitcoinUnits::formatWithUnit(unitCopy, cumSat, false, BitcoinUnits::separatorStandard),
                         BitcoinUnits::formatWithUnit(unitCopy, perSat, false, BitcoinUnits::separatorStandard)));
        });

        chart->addSeries(area);
        area->attachAxis(axisX);
        area->attachAxis(axisY);

        const double yMax = niceAxisMax(running);
        axisY->setLabelFormat(yMax >= 20.0 ? "%.0f" : (yMax >= 0.4 ? "%.2f" : "%.4f"));
        axisY->setRange(0.0, yMax);
    } else {
        axisX->setVisible(true);
        axisY->setVisible(true);
        chart->setMargins({0, 2, 0, 0});
        axisX->append(snap.labels);

        QFont xf = axisX->labelsFont();
        xf.setPointSize(periodMode == 2 ? 8 : 9);
        axisX->setLabelsFont(xf);

        QBarSet* setPos  = new QBarSet(tr("PoS Staking"));
        QBarSet* setMn   = new QBarSet(tr("MN & LLMQ"));
        QBarSet* setAdam = new QBarSet(tr("ADAM / PoW"));
        setPos->setColor(COLOR_POS);
        setMn->setColor(COLOR_MN);
        setAdam->setColor(COLOR_ADAM);

        const QColor barBorder = light ? QColor("#FFFFFF") : QColor("#131C2E");
        setPos->setBorderColor(barBorder);
        setMn->setBorderColor(barBorder);
        setAdam->setBorderColor(barBorder);

        auto bindHover = [labelsCopy, tipLabelsCopy, unitCopy](QBarSet* bs, const QVector<CAmount>& exactSats, const QString& trackName) {
            connect(bs, &QBarSet::hovered, bs, [exactSats, labelsCopy, tipLabelsCopy, unitCopy, trackName](bool status, int index) {
                if (!status || index < 0 || index >= exactSats.size()) {
                    QToolTip::hideText();
                    return;
                }
                const QString period = (index < tipLabelsCopy.size())
                    ? tipLabelsCopy.at(index)
                    : ((index < labelsCopy.size()) ? labelsCopy.at(index) : QString::number(index + 1));
                const CAmount sat = exactSats.at(index);
                QToolTip::showText(QCursor::pos(),
                    QString("%1 — %2\n%3").arg(period, trackName,
                        BitcoinUnits::formatWithUnit(unitCopy, sat, false, BitcoinUnits::separatorStandard)));
            });
        };
        bindHover(setPos,  snap.posSats,     tr("PoS Staking"));
        bindHover(setMn,   snap.mnLlmqSats,  tr("MN & LLMQ"));
        bindHover(setAdam, snap.adamPowSats, tr("ADAM / PoW"));

        const double unitScale = (unitCopy == BitcoinUnits::mPIV) ? 1000.0 : (unitCopy == BitcoinUnits::uPIV ? 1000000.0 : 1.0);
        double maxStack = 0.0;
        const int count = std::min({snap.posSats.size(), snap.mnLlmqSats.size(), snap.adamPowSats.size()});
        for (int i = 0; i < count; ++i) {
            const double p = (static_cast<double>(snap.posSats.at(i)) / static_cast<double>(COIN)) * unitScale;
            const double m = (static_cast<double>(snap.mnLlmqSats.at(i)) / static_cast<double>(COIN)) * unitScale;
            const double a = (static_cast<double>(snap.adamPowSats.at(i)) / static_cast<double>(COIN)) * unitScale;
            *setPos << p;
            *setMn << m;
            *setAdam << a;
            maxStack = std::max(maxStack, p + m + a);
        }

        QStackedBarSeries* stacked = new QStackedBarSeries(chart);
        stacked->append(setPos);
        stacked->append(setMn);
        stacked->append(setAdam);
        stacked->setBarWidth(periodMode == 0 || periodMode == 2 ? 0.66 : 0.56);

        chart->addSeries(stacked);
        stacked->attachAxis(axisX);
        stacked->attachAxis(axisY);

        const double yMax = niceAxisMax(maxStack);
        axisY->setLabelFormat(yMax >= 20.0 ? "%.0f" : (yMax >= 0.4 ? "%.2f" : "%.4f"));
        axisY->setRange(0.0, yMax);
    }
#endif
}

void StatsWidget::renderTelemetryCharts(const TelemetrySnapshot& tel)
{
#ifndef USE_QTCHARTS
    Q_UNUSED(tel);
    return;
#else
    const bool light = isLightTheme();

    // 1. Difficulty Curve (Last 120 Blocks)
    if (diffChart && diffAxisX && diffAxisY) {
        diffChart->removeAllSeries();
        if (!tel.heights.isEmpty()) {
            QLineSeries* line = new QLineSeries();
            for (int i = 0; i < tel.heights.size(); ++i) {
                line->append(tel.heights.at(i), tel.difficulties.at(i));
            }
            const QColor strokeCol = COLOR_MN;
            QColor fillCol = strokeCol;
            fillCol.setAlpha(55);

            QAreaSeries* area = new QAreaSeries(line, nullptr);
            line->setParent(area);
            area->setPen(QPen(strokeCol, 2.2));
            area->setBrush(QBrush(fillCol));
            area->setPointsVisible(tel.heights.size() <= 30);

            const QVector<int> hCopy = tel.heights;
            const QVector<double> dCopy = tel.difficulties;
            const QVector<qint64> tCopy = tel.blockTimesSec;
            const QVector<qint64> dtCopy = tel.blockDeltasSec;
            const QVector<int> txCopy = tel.blockTxCounts;

            connect(area, &QAreaSeries::hovered, area, [hCopy, dCopy, tCopy, dtCopy, txCopy](const QPointF& pt, bool state) {
                if (!state || hCopy.isEmpty()) {
                    QToolTip::hideText();
                    return;
                }
                const int targetH = static_cast<int>(std::llround(pt.x()));
                auto it = std::lower_bound(hCopy.begin(), hCopy.end(), targetH);
                int idx = static_cast<int>(it - hCopy.begin());
                if (idx >= hCopy.size()) idx = hCopy.size() - 1;
                if (idx > 0 && std::abs(hCopy.at(idx - 1) - targetH) < std::abs(hCopy.at(idx) - targetH)) {
                    --idx;
                }
                const QString ts = QDateTime::fromSecsSinceEpoch(tCopy.at(idx)).toString(QStringLiteral("HH:mm:ss"));
                const QLocale loc;
                QToolTip::showText(QCursor::pos(),
                    QObject::tr("Block #%1 (%2)\nDifficulty: %3\nSpacing: %4s • Txs: %5")
                        .arg(loc.toString(hCopy.at(idx)))
                        .arg(ts)
                        .arg(FormatDifficultyLocal(dCopy.at(idx)))
                        .arg(dtCopy.at(idx))
                        .arg(loc.toString(txCopy.at(idx))));
            });

            diffChart->addSeries(area);
            area->attachAxis(diffAxisX);
            area->attachAxis(diffAxisY);

            const int hMin = tel.heights.first();
            const int hMax = tel.heights.last();
            if (hMin == hMax) {
                diffAxisX->setRange(std::max(0, hMin - 1), hMax + 1);
                diffAxisX->setTickCount(3);
            } else {
                diffAxisX->setRange(hMin, hMax);
                diffAxisX->setTickCount(std::min(6, std::max(2, tel.heights.size())));
            }
            diffAxisX->setLabelFormat("%d");

            double yMin = tel.diffMin * 0.94;
            double yMax = tel.diffMax * 1.06;
            if (yMax <= yMin) {
                const double base = (tel.diffMax > 0.0) ? tel.diffMax : 1.0;
                yMin = std::max(0.0, base * 0.90);
                yMax = base * 1.10;
            } else {
                yMin = std::max(0.0, yMin);
            }
            diffAxisY->setRange(yMin, yMax);
            diffAxisY->setLabelFormat(yMax >= 100.0 ? "%.1f" : (yMax >= 1.0 ? "%.3f" : "%.5f"));

            if (lblDiffChartMeta) {
                lblDiffChartMeta->setText(tr("Min: %1 • Max: %2 • Latest: %3")
                    .arg(formatDifficulty(tel.diffMin), formatDifficulty(tel.diffMax), formatDifficulty(tel.currentDifficulty)));
            }
        } else {
            diffAxisX->setRange(0, 1);
            diffAxisY->setRange(0, 1);
            if (lblDiffChartMeta) lblDiffChartMeta->setText(tr("Waiting for chain data"));
        }
    }

    // 2. Block Time Distribution Histogram (Last 120 Blocks)
    if (blockTimeChart && blockTimeAxisX && blockTimeAxisY) {
        blockTimeChart->removeAllSeries();
        blockTimeAxisX->clear();

        static const QStringList timeLabels = {
            QStringLiteral("<15s"), QStringLiteral("15–25s"), QStringLiteral("25–35s*"),
            QStringLiteral("35–50s"), QStringLiteral("50–90s"), QStringLiteral(">90s")
        };
        blockTimeAxisX->append(timeLabels);

        QBarSet* setFast   = new QBarSet(tr("Fast (<25s, 35–90s)"));
        QBarSet* setTarget = new QBarSet(tr("On Target (25–35s)"));
        QBarSet* setSlow   = new QBarSet(tr("Slow Tail (>90s)"));
        setFast->setColor(COLOR_MN);
        setTarget->setColor(COLOR_ADAM);
        setSlow->setColor(COLOR_POS);

        const QColor barBorder = light ? QColor("#FFFFFF") : QColor("#131C2E");
        setFast->setBorderColor(barBorder);
        setTarget->setBorderColor(barBorder);
        setSlow->setBorderColor(barBorder);

        int maxBucket = 0;
        int totalSampled = 0;
        for (int i = 0; i < 6; ++i) {
            const int c = tel.deltaBuckets[i];
            totalSampled += c;
            maxBucket = std::max(maxBucket, c);
            *setFast   << ((i != 2 && i != 5) ? c : 0);
            *setTarget << ((i == 2) ? c : 0);
            *setSlow   << ((i == 5) ? c : 0);
        }

        auto bindTimeHover = [totalSampled](QBarSet* bs) {
            connect(bs, &QBarSet::hovered, bs, [bs, totalSampled](bool status, int index) {
                if (!status || index < 0 || index >= bs->count() || index >= timeLabels.size()) {
                    QToolTip::hideText();
                    return;
                }
                const int cnt = static_cast<int>(std::llround(bs->at(index)));
                const double pct = (totalSampled > 0) ? (100.0 * static_cast<double>(cnt) / static_cast<double>(totalSampled)) : 0.0;
                const QString tipTpl = (cnt == 1)
                    ? QObject::tr("%1 window\n%2 block (%3%)")
                    : QObject::tr("%1 window\n%2 blocks (%3%)");
                QToolTip::showText(QCursor::pos(),
                    tipTpl.arg(timeLabels.at(index)).arg(QLocale().toString(cnt)).arg(pct, 0, 'f', 1));
            });
        };
        bindTimeHover(setFast);
        bindTimeHover(setTarget);
        bindTimeHover(setSlow);

        QStackedBarSeries* stacked = new QStackedBarSeries(blockTimeChart);
        stacked->append(setFast);
        stacked->append(setTarget);
        stacked->append(setSlow);
        stacked->setBarWidth(0.62);

        blockTimeChart->addSeries(stacked);
        stacked->attachAxis(blockTimeAxisX);
        stacked->attachAxis(blockTimeAxisY);

        const int yTop = NiceAxisCeil(maxBucket);
        blockTimeAxisY->setRange(0, yTop);
        blockTimeAxisY->setTickCount(5);
        blockTimeAxisY->setLabelFormat("%d");

        if (lblBlockTimeChartMeta) {
            const QLocale loc;
            const double targetPct = (totalSampled > 0)
                ? (100.0 * static_cast<double>(tel.deltaBuckets[2]) / static_cast<double>(totalSampled))
                : 0.0;
            lblBlockTimeChartMeta->setText(tr("25–35s Target: %1% (%2/%3)")
                .arg(targetPct, 0, 'f', 1)
                .arg(loc.toString(tel.deltaBuckets[2]), loc.toString(totalSampled)));
        }
    }

    // 3. Mempool Fee-Rate Histogram OR Graceful Empty Mempool Recent 60-Block TX Activity
    if (mempoolChart && mempoolBarAxisX && mempoolValAxisX && mempoolAxisY) {
        mempoolChart->removeAllSeries();
        mempoolBarAxisX->clear();
        if (lblMempoolChartBadge) {
            lblMempoolChartBadge->setStyleSheet(QString(
                "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 0px 10px; font-size: 11px; font-weight: bold; }"
            ).arg(light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
                  light ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"),
                  light ? QStringLiteral("#B8CBE8") : QStringLiteral("#355184")));
        }

        if (tel.mempoolTxCount > 0) {
            mempoolBarAxisX->setVisible(true);
            mempoolValAxisX->setVisible(false);

            if (lblMempoolChartTitle) lblMempoolChartTitle->setText(tr("Mempool Fee-Rate Histogram"));
            if (lblMempoolChartBadge) {
                lblMempoolChartBadge->setText(tr("%1 tx • Med %2 sat/B")
                    .arg(QLocale().toString(tel.mempoolTxCount)).arg(tel.medianFeeSatPerByte, 0, 'f', 1));
            }

            static const QStringList feeLabels = {
                QStringLiteral("<1"), QStringLiteral("1–5"), QStringLiteral("5–20"),
                QStringLiteral("20–50"), QStringLiteral("50+ sat/B")
            };
            mempoolBarAxisX->append(feeLabels);

            QBarSet* setFees = new QBarSet(tr("Pending Transactions"));
            setFees->setColor(COLOR_MN);
            setFees->setBorderColor(light ? QColor("#FFFFFF") : QColor("#131C2E"));

            int maxFeeCnt = 0;
            for (int i = 0; i < 5; ++i) {
                *setFees << tel.feeBuckets[i];
                maxFeeCnt = std::max(maxFeeCnt, tel.feeBuckets[i]);
            }
            const int totalMp = tel.mempoolTxCount;
            connect(setFees, &QBarSet::hovered, setFees, [setFees, totalMp](bool status, int index) {
                if (!status || index < 0 || index >= setFees->count() || index >= feeLabels.size()) {
                    QToolTip::hideText();
                    return;
                }
                const int cnt = static_cast<int>(std::llround(setFees->at(index)));
                const double pct = (totalMp > 0) ? (100.0 * static_cast<double>(cnt) / static_cast<double>(totalMp)) : 0.0;
                QToolTip::showText(QCursor::pos(),
                    QString("Fee Bucket: %1 sat/B\n%2 tx (%3%)").arg(feeLabels.at(index)).arg(QLocale().toString(cnt)).arg(pct, 0, 'f', 1));
            });

            QBarSeries* barSeries = new QBarSeries(mempoolChart);
            barSeries->append(setFees);
            barSeries->setBarWidth(0.58);
            mempoolChart->addSeries(barSeries);
            barSeries->attachAxis(mempoolBarAxisX);
            barSeries->attachAxis(mempoolAxisY);

            const int yTop = NiceAxisCeil(maxFeeCnt);
            mempoolAxisY->setRange(0, yTop);
            mempoolAxisY->setTickCount(5);
            mempoolAxisY->setLabelFormat("%d");
        } else {
            mempoolBarAxisX->setVisible(false);
            mempoolValAxisX->setVisible(true);

            if (lblMempoolChartTitle) lblMempoolChartTitle->setText(tr("Recent Block TX Activity (Last 60 Blocks)"));
            if (lblMempoolChartBadge) {
                lblMempoolChartBadge->setText(tr("Mempool Empty — Instant Next-Block Confirmation"));
            }

            const int totalPts = tel.heights.size();
            const int startIdx = std::max(0, totalPts - 60);
            QLineSeries* line = new QLineSeries();
            int maxTx = 1;
            QVector<int> hSlice;
            QVector<int> txSlice;
            for (int i = startIdx; i < totalPts; ++i) {
                const int h = tel.heights.at(i);
                const int ntx = tel.blockTxCounts.at(i);
                line->append(h, ntx);
                hSlice.append(h);
                txSlice.append(ntx);
                maxTx = std::max(maxTx, ntx);
            }

            QColor fillCol = COLOR_ADAM;
            fillCol.setAlpha(55);
            QAreaSeries* area = new QAreaSeries(line, nullptr);
            line->setParent(area);
            area->setPen(QPen(COLOR_ADAM, 2.2));
            area->setBrush(QBrush(fillCol));
            area->setPointsVisible(hSlice.size() <= 30);

            connect(area, &QAreaSeries::hovered, area, [hSlice, txSlice](const QPointF& pt, bool state) {
                if (!state || hSlice.isEmpty()) {
                    QToolTip::hideText();
                    return;
                }
                const int targetH = static_cast<int>(std::llround(pt.x()));
                auto it = std::lower_bound(hSlice.begin(), hSlice.end(), targetH);
                int idx = static_cast<int>(it - hSlice.begin());
                if (idx >= hSlice.size()) idx = hSlice.size() - 1;
                if (idx > 0 && std::abs(hSlice.at(idx - 1) - targetH) < std::abs(hSlice.at(idx) - targetH)) {
                    --idx;
                }
                const QLocale loc;
                QToolTip::showText(QCursor::pos(),
                    QString("Block #%1\nTransactions: %2").arg(loc.toString(hSlice.at(idx)), loc.toString(txSlice.at(idx))));
            });

            mempoolChart->addSeries(area);
            area->attachAxis(mempoolValAxisX);
            area->attachAxis(mempoolAxisY);

            if (!hSlice.isEmpty()) {
                const int hMin = hSlice.first();
                const int hMax = hSlice.last();
                if (hMin == hMax) {
                    mempoolValAxisX->setRange(std::max(0, hMin - 1), hMax + 1);
                    mempoolValAxisX->setTickCount(3);
                } else {
                    mempoolValAxisX->setRange(hMin, hMax);
                    mempoolValAxisX->setTickCount(std::min(6, std::max(2, hSlice.size())));
                }
            } else {
                mempoolValAxisX->setRange(0, 1);
                mempoolValAxisX->setTickCount(2);
            }
            mempoolValAxisX->setLabelFormat("%d");

            const int yTop = NiceAxisCeil(maxTx);
            mempoolAxisY->setRange(0, yTop);
            mempoolAxisY->setTickCount(5);
            mempoolAxisY->setLabelFormat("%d");
        }
    }
#endif
}

void StatsWidget::refreshStats()
{
    const bool light = isLightTheme();
    const QString textPrimary = light ? QStringLiteral("#0F172A") : QStringLiteral("#F8FAFC");
    const QString textLabel   = light ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString activeCol   = light ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString trackBg     = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#1F2D46");
    const QString unitName    = BitcoinUnits::name(displayUnit);

    if (activeView == 1) {
        TelemetrySnapshot tel = collectTelemetrySnapshot();
        bool needsRetry = false;

        auto copyChainFields = [](const TelemetrySnapshot& src, TelemetrySnapshot& dst) {
            dst.hasChain = src.hasChain;
            dst.targetSpacingSec = src.targetSpacingSec;
            dst.heights = src.heights;
            dst.difficulties = src.difficulties;
            dst.blockTimesSec = src.blockTimesSec;
            dst.blockDeltasSec = src.blockDeltasSec;
            dst.blockTxCounts = src.blockTxCounts;
            dst.avgBlockTimeSec = src.avgBlockTimeSec;
            dst.medianBlockTimeSec = src.medianBlockTimeSec;
            dst.currentDifficulty = src.currentDifficulty;
            dst.diffMin = src.diffMin;
            dst.diffMax = src.diffMax;
            dst.diffChangePct = src.diffChangePct;
            for (int i = 0; i < 6; ++i) dst.deltaBuckets[i] = src.deltaBuckets[i];
            dst.recent60TxTotal = src.recent60TxTotal;
            dst.recent60Blocks = src.recent60Blocks;
            dst.recent60PeakTx = src.recent60PeakTx;
            dst.recent60SpanSec = src.recent60SpanSec;
        };

        auto copyMempoolFields = [](const TelemetrySnapshot& src, TelemetrySnapshot& dst) {
            dst.hasMempool = src.hasMempool;
            dst.mempoolTxCount = src.mempoolTxCount;
            dst.mempoolTotalBytes = src.mempoolTotalBytes;
            dst.mempoolDynamicBytes = src.mempoolDynamicBytes;
            dst.medianFeeSatPerByte = src.medianFeeSatPerByte;
            for (int i = 0; i < 5; ++i) dst.feeBuckets[i] = src.feeBuckets[i];
        };

        if (tel.hasChain) {
            copyChainFields(tel, m_lastGoodTelemetry);
        } else {
            needsRetry = true;
            if (m_lastGoodTelemetry.hasChain) {
                copyChainFields(m_lastGoodTelemetry, tel);
            }
        }

        if (tel.hasMempool) {
            copyMempoolFields(tel, m_lastGoodTelemetry);
        } else {
            needsRetry = true;
            if (m_lastGoodTelemetry.hasMempool) {
                copyMempoolFields(m_lastGoodTelemetry, tel);
            }
        }

        if (tel.hasChain) {
            const double deltaPct = 100.0 * (tel.avgBlockTimeSec - static_cast<double>(tel.targetSpacingSec))
                                    / static_cast<double>(tel.targetSpacingSec);
            const QString sign = (deltaPct >= 0.0) ? QStringLiteral("+") : QString();
            lblTelBlockTimeVal->setText(tr("%1 s").arg(tel.avgBlockTimeSec, 0, 'f', 1));
            lblTelBlockTimeSub->setText(tr("Median: %1 s • Target: %2s (%3%4%)")
                .arg(tel.medianBlockTimeSec, 0, 'f', 1)
                .arg(tel.targetSpacingSec)
                .arg(sign)
                .arg(deltaPct, 0, 'f', 1));

            const QString dSign = (tel.diffChangePct >= 0.0) ? QStringLiteral("+") : QString();
            lblTelDiffVal->setText(formatDifficulty(tel.currentDifficulty));
            lblTelDiffSub->setText(tr("120b range: %1–%2 (%3%4%)")
                .arg(formatDifficulty(tel.diffMin), formatDifficulty(tel.diffMax), dSign)
                .arg(tel.diffChangePct, 0, 'f', 1));

            const double txPerBlk = (tel.recent60Blocks > 0)
                ? (static_cast<double>(tel.recent60TxTotal) / static_cast<double>(tel.recent60Blocks))
                : 0.0;
            const double txPerMin = (tel.recent60SpanSec > 0)
                ? (60.0 * static_cast<double>(tel.recent60TxTotal) / static_cast<double>(tel.recent60SpanSec))
                : 0.0;
            lblTelThroughputVal->setText(tr("%1 tx").arg(QLocale().toString(tel.recent60TxTotal)));
            lblTelThroughputSub->setText(tr("Avg: %1 tx/blk • %2 tx/min • Peak: %3")
                .arg(txPerBlk, 0, 'f', 2)
                .arg(txPerMin, 0, 'f', 1)
                .arg(QLocale().toString(tel.recent60PeakTx)));
        } else {
            lblTelBlockTimeVal->setText(QStringLiteral("--"));
            lblTelBlockTimeSub->setText(tr("Chain data unavailable"));
            lblTelDiffVal->setText(QStringLiteral("--"));
            lblTelDiffSub->setText(tr("Chain data unavailable"));
            lblTelThroughputVal->setText(QStringLiteral("--"));
            lblTelThroughputSub->setText(tr("Chain data unavailable"));
        }

        if (tel.hasMempool) {
            lblTelMempoolVal->setText(tr("%1 tx").arg(QLocale().toString(tel.mempoolTxCount)));
            const QString medFee = (tel.mempoolTxCount > 0)
                ? tr("%1 sat/B").arg(tel.medianFeeSatPerByte, 0, 'f', 1)
                : QStringLiteral("--");
            lblTelMempoolSub->setText(tr("%1 • RAM: %2 • Med Fee: %3")
                .arg(formatBytes(tel.mempoolTotalBytes),
                     formatBytes(static_cast<uint64_t>(tel.mempoolDynamicBytes)),
                     medFee));
        } else {
            lblTelMempoolVal->setText(QStringLiteral("--"));
            lblTelMempoolSub->setText(tr("Mempool busy / unavailable"));
        }

        renderTelemetryCharts(tel);
        if (needsRetry && isVisible() && retryTimer && !retryTimer->isActive()) {
            retryTimer->start(600);
        }
        return;
    }

    // ================= OVERVIEW PAGE =================
    bool overviewNeedsRetry = false;
    const RewardStatsSnapshot snap = collectRewardSnapshot();
    const CAmount allAdamPow = snap.totalAdam + snap.totalGenerated;
    const CAmount totalRewards = snap.totalPos + snap.totalMnLlmq + allAdamPow;

    // Balances
    CAmount avail = 0;
    CAmount unconfirmed = 0;
    CAmount immature = 0;
    CAmount locked = 0;
    bool stakingActive = false;
    if (walletModel) {
        const interfaces::WalletBalances bal = walletModel->GetWalletBalances();
        locked = bal.locked_balance;
        avail = std::max<CAmount>(0, bal.balance - locked);
        unconfirmed = bal.unconfirmed_balance;
        immature = bal.immature_balance;
        stakingActive = !walletModel->isWalletLocked() && walletModel->isStakingStatusActive();
    }
    const CAmount totalWallet = avail + locked + immature;
    LockedBreakdown lockedInfo = collectLockedBreakdown(locked);
    if (lockedInfo.resolved) {
        m_lastGoodLocked = lockedInfo;
    } else {
        overviewNeedsRetry = true;
        if (m_lastGoodLocked.resolved) {
            lockedInfo = m_lastGoodLocked;
        }
    }

    auto formatNumOnly = [this](CAmount amt) -> QString {
        double coins = static_cast<double>(amt) / static_cast<double>(COIN);
        if (displayUnit == BitcoinUnits::mPIV) coins *= 1000.0;
        else if (displayUnit == BitcoinUnits::uPIV) coins *= 1000000.0;
        return QLocale().toString(coins, 'f', 2);
    };

    // KPI 1: My Total Balance
    lblKpiBalanceVal->setText(formatAmountPlain(totalWallet, true));
    lblKpiBalanceVal->setToolTip(tr("Available: %1\nLocked: %2\nImmature: %3\nTotal: %4")
        .arg(formatAmountPlain(avail, false),
             formatAmountPlain(locked, false),
             formatAmountPlain(immature, false),
             formatAmountPlain(totalWallet, false)));
    lblKpiBalanceSub->setText(tr("Avail: %1 • Lock: %2 • Imm: %3")
        .arg(formatNumOnly(avail),
             formatNumOnly(locked),
             formatNumOnly(immature)));
    if (barKpiBalance) {
        barKpiBalance->setSegments({
            {static_cast<double>(avail), COLOR_MN},
            {static_cast<double>(locked), COLOR_ADAM},
            {static_cast<double>(immature), COLOR_POS}
        }, QColor(trackBg));
    }

    // KPI 2: My Total Earnings
    const QLocale loc;
    lblKpiRewardsVal->setText(QString("+%1").arg(formatAmountPlain(totalRewards, true)));
    lblKpiRewardsVal->setToolTip(tr("All-Time Valid Rewards: %1\n• PoS Staking: %2 (%3 tx)\n• MN & LLMQ: %4 (%5 tx)\n• ADAM Mining: %6 (%7 tx)\n• PoW Producer (Gen): %8 (%9 tx)")
        .arg(formatAmountPlain(totalRewards, false))
        .arg(formatAmountPlain(snap.totalPos, false), loc.toString(snap.countPos))
        .arg(formatAmountPlain(snap.totalMnLlmq, false), loc.toString(snap.countMnLlmq))
        .arg(formatAmountPlain(snap.totalAdam, false), loc.toString(snap.countAdam))
        .arg(formatAmountPlain(snap.totalGenerated, false), loc.toString(snap.countGenerated)));
    lblKpiRewardsSub->setText(tr("24h: +%1 • 7d: +%2")
        .arg(formatAmountPlain(snap.last24h, true), formatAmountPlain(snap.last7d, true)));

    // Chain, MPA Pool, KTDIP3 MNs
    int height = 0;
    int peers = 0;
    qint64 lastBlockAgeSec = 0;
    CAmount moneySupply = 0;
    double networkHashPs = 0.0;
    bool hasPoolSnapshot = false;
    size_t poolTotal = 0;
    size_t mnPoolCount = 0;
    size_t coinLockCount = 0;
    size_t powPoolCount = 0;
    int mnEnabled = 0;

    if (clientModel) {
        height = clientModel->getNumBlocks();
        peers = clientModel->getNumConnections();
        const QDateTime lastBlockDt = clientModel->getLastBlockDate();
        if (lastBlockDt.isValid()) {
            lastBlockAgeSec = std::max<qint64>(0, lastBlockDt.secsTo(QDateTime::currentDateTimeUtc()));
        }
    }
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain && chainActive.Tip()) {
            CBlockIndex* pb = chainActive.Tip();
            height = std::max(height, chainActive.Height());
            moneySupply = nMoneySupply;

            const int lookup = std::min(120, pb->nHeight);
            if (lookup > 0) {
                CBlockIndex* pb0 = pb;
                int64_t minTime = pb0->GetBlockTime();
                int64_t maxTime = minTime;
                for (int i = 0; i < lookup && pb0->pprev; ++i) {
                    pb0 = pb0->pprev;
                    const int64_t t = pb0->GetBlockTime();
                    minTime = std::min(minTime, t);
                    maxTime = std::max(maxTime, t);
                }
                if (maxTime > minTime) {
                    const uint256 workDiff = pb->nChainWork - pb0->nChainWork;
                    networkHashPs = workDiff.getdouble() / static_cast<double>(maxTime - minTime);
                }
            }

            std::set<CPubKey> mnKeys;
            std::set<CPubKey> coinLockKeys;
            std::set<CPubKey> fundedPowKeys;
            std::set<CPubKey> bootstrapKeys;
            const auto poolKeys = GetAdamMinerPool(height, nullptr, &mnKeys, &coinLockKeys, nullptr, nullptr, &fundedPowKeys, &bootstrapKeys);
            poolTotal = poolKeys.size();
            mnPoolCount = mnKeys.size();
            coinLockCount = coinLockKeys.size();
            powPoolCount = fundedPowKeys.size() + bootstrapKeys.size();

            int dip3Valid = 0;
            if (deterministicMNManager) {
                const auto dip3List = deterministicMNManager->GetListAtChainTip();
                for (const auto& kv : dip3List.mapMNs) {
                    if (kv.second.state.IsValid()) ++dip3Valid;
                }
            }
            mnEnabled = (dip3Valid > 0) ? dip3Valid : mnodeman.CountEnabled();
            hasPoolSnapshot = true;

            m_lastGoodPool.valid = true;
            m_lastGoodPool.height = height;
            m_lastGoodPool.moneySupply = moneySupply;
            m_lastGoodPool.networkHashPs = networkHashPs;
            m_lastGoodPool.poolTotal = poolTotal;
            m_lastGoodPool.mnPoolCount = mnPoolCount;
            m_lastGoodPool.coinLockCount = coinLockCount;
            m_lastGoodPool.powPoolCount = powPoolCount;
            m_lastGoodPool.mnEnabled = mnEnabled;
        } else {
            overviewNeedsRetry = true;
            if (m_lastGoodPool.valid) {
                height = std::max(height, m_lastGoodPool.height);
                moneySupply = m_lastGoodPool.moneySupply;
                networkHashPs = m_lastGoodPool.networkHashPs;
                poolTotal = m_lastGoodPool.poolTotal;
                mnPoolCount = m_lastGoodPool.mnPoolCount;
                coinLockCount = m_lastGoodPool.coinLockCount;
                powPoolCount = m_lastGoodPool.powPoolCount;
                mnEnabled = m_lastGoodPool.mnEnabled;
                hasPoolSnapshot = true;
            }
        }
    }

    if (overviewNeedsRetry && isVisible() && retryTimer && !retryTimer->isActive()) {
        retryTimer->start(600);
    }

    // KPI 3: Chain & MPA Pool
    lblKpiNetworkVal->setText(QString("#%1").arg(QLocale().toString(height)));
    const QString peerTxt = (peers == 1)
        ? tr("%1 peer").arg(loc.toString(peers))
        : tr("%1 peers").arg(loc.toString(peers));
    lblKpiNetworkSub->setText(hasPoolSnapshot
        ? tr("%1 • %2 ago • Pool: %3").arg(peerTxt, formatDurationSeconds(lastBlockAgeSec), loc.toString(static_cast<qulonglong>(poolTotal)))
        : tr("%1 • %2 ago").arg(peerTxt, formatDurationSeconds(lastBlockAgeSec)));

    // KPI 4: Block Emission (Model D)
    const int evalHeight = (height > 0) ? height : 1;
    const CAmount blockReward = CMasternode::GetBlockValue(evalHeight);
    const CAmount devShare = (evalHeight > 1) ? ((blockReward * 7) / 100) : 0;
    const CAmount netPool = blockReward - devShare;
    lblKpiEmissionVal->setText(formatAmountPlain(blockReward, true));
    lblKpiEmissionSub->setText(tr("7% Dev: %1 • Pool: %2")
        .arg(formatAmountPlain(devShare, true), formatAmountPlain(netPool, true)));

    // Window Summary Strip inside Chart Card
    lblWin24hVal->setText(formatAmountPlain(snap.last24h, true));
    lblWin7dVal->setText(formatAmountPlain(snap.last7d, true));
    lblWin30dVal->setText(formatAmountPlain(snap.last30d, true));
    lblWinAllVal->setText(formatAmountPlain(totalRewards, true));

    // Bottom Left Card 1: Balance & Collateral Split
    auto pctStr = [totalWallet](CAmount part) -> QString {
        if (totalWallet <= 0) return QStringLiteral("0.0%");
        return QString("%1%").arg(100.0 * static_cast<double>(part) / static_cast<double>(totalWallet), 0, 'f', 1);
    };

    lblBalAvailVal->setText(QString("%1 (%2)").arg(formatAmountPlain(avail, true), pctStr(avail)));
    lblBalAvailVal->setToolTip(formatAmountPlain(avail, false));

    if (lockedInfo.resolved) {
        lblBalMnColVal->setText(tr("%1× MN • %2 (%3)")
            .arg(loc.toString(lockedInfo.mnCollateralCount))
            .arg(formatAmountPlain(lockedInfo.mnCollateralAmount, true), pctStr(lockedInfo.mnCollateralAmount)));
        lblBalMnColVal->setToolTip((lockedInfo.mnCollateralCount == 1
                ? tr("%1 active/locked collateral (%2)")
                : tr("%1 active/locked collaterals (%2)"))
            .arg(loc.toString(lockedInfo.mnCollateralCount))
            .arg(formatAmountPlain(lockedInfo.mnCollateralAmount, false)));

        const int totalCltvCount = lockedInfo.coinLock250Count + lockedInfo.powLock10Count;
        lblBalCltvVal->setText((totalCltvCount == 1 ? tr("%1 lock • %2 (%3)") : tr("%1 locks • %2 (%3)"))
            .arg(loc.toString(totalCltvCount))
            .arg(formatAmountPlain(lockedInfo.minerCltvAmount, true), pctStr(lockedInfo.minerCltvAmount)));
        lblBalCltvVal->setToolTip(tr("Coin-Lock (250): %1\nPoW-Lock (10): %2\nTotal: %3")
            .arg(loc.toString(lockedInfo.coinLock250Count))
            .arg(loc.toString(lockedInfo.powLock10Count))
            .arg(formatAmountPlain(lockedInfo.minerCltvAmount, false)));

        const bool showOther = (lockedInfo.otherManualAmount > 0);
        if (lblBalOtherLockKey) lblBalOtherLockKey->setVisible(showOther);
        if (lblBalOtherLockVal) {
            lblBalOtherLockVal->setVisible(showOther);
            lblBalOtherLockVal->setText(tr("%1 (%2)")
                .arg(formatAmountPlain(lockedInfo.otherManualAmount, true), pctStr(lockedInfo.otherManualAmount)));
            lblBalOtherLockVal->setToolTip(formatAmountPlain(lockedInfo.otherManualAmount, false));
        }
        if (dividerOtherLock) dividerOtherLock->setVisible(showOther);
    } else {
        lblBalMnColVal->setText(tr("Total Locked: %1 (%2)").arg(formatAmountPlain(locked, true), pctStr(locked)));
        lblBalCltvVal->setText(QStringLiteral("--"));
        if (lblBalOtherLockKey) lblBalOtherLockKey->setVisible(false);
        if (lblBalOtherLockVal) lblBalOtherLockVal->setVisible(false);
        if (dividerOtherLock) dividerOtherLock->setVisible(false);
    }

    lblBalImmatureVal->setText(QString("%1 (%2)").arg(formatAmountPlain(immature, true), pctStr(immature)));
    lblBalImmatureVal->setToolTip(formatAmountPlain(immature, false));
    lblBalPendingVal->setText(formatAmountPlain(unconfirmed, true));
    lblBalPendingVal->setToolTip(formatAmountPlain(unconfirmed, false));

    // Bottom Left Card 2: Income by Source
    auto rewPct = [totalRewards](CAmount part) -> QString {
        if (totalRewards <= 0) return QStringLiteral("0%");
        return QString("%1%").arg(100.0 * static_cast<double>(part) / static_cast<double>(totalRewards), 0, 'f', 1);
    };
    lblActPosVal->setText(tr("%1 tx • %2 (%3)").arg(QLocale().toString(snap.countPos)).arg(formatAmountPlain(snap.totalPos, true), rewPct(snap.totalPos)));
    lblActMnVal->setText(tr("%1 tx • %2 (%3)").arg(QLocale().toString(snap.countMnLlmq)).arg(formatAmountPlain(snap.totalMnLlmq, true), rewPct(snap.totalMnLlmq)));
    lblActAdamVal->setText(tr("%1 tx • %2 (%3)").arg(QLocale().toString(snap.countAdam)).arg(formatAmountPlain(snap.totalAdam, true), rewPct(snap.totalAdam)));
    lblActGenVal->setText(tr("%1 tx • %2 (%3)").arg(QLocale().toString(snap.countGenerated)).arg(formatAmountPlain(snap.totalGenerated, true), rewPct(snap.totalGenerated)));
    const QString sentStr = (snap.totalSent > 0)
        ? tr("-%1").arg(formatAmountPlain(snap.totalSent, true))
        : formatAmountPlain(0, true);
    lblActSentRecvVal->setText(QString("%1 / +%2")
        .arg(sentStr, formatAmountPlain(snap.totalReceived, true)));
    lblActTxCountVal->setText(snap.countExcludedOrphan > 0
        ? tr("%1 (%2 orphan excl.)").arg(QLocale().toString(snap.countTotalTx)).arg(QLocale().toString(snap.countExcludedOrphan))
        : QLocale().toString(snap.countTotalTx));

    // Right Card 1: Supply, Decay & Model D Block Split
    const CAmount maxMoney = Params().GetConsensus().nMaxMoneyOut;
    const double supplyPct = (maxMoney > 0)
        ? std::min(100.0, 100.0 * static_cast<double>(moneySupply) / static_cast<double>(maxMoney))
        : 0.0;
    lblSupplyText->setText(tr("Supply: %1 / %2 (%3%)")
        .arg(formatAmountPlain(moneySupply, true), formatAmountPlain(maxMoney, true))
        .arg(supplyPct, 0, 'f', 2));
    barSupply->setValue(static_cast<int>(supplyPct * 10.0));

    const qint64 targetSec = std::max<qint64>(1, Params().GetConsensus().nTargetSpacing);
    const int decayInterval = 259200;
    const int bootstrapEnd = 10000;
    if (height <= 2200) {
        const int rem = 2201 - height;
        lblDecayText->setText(tr("Bootstrap 1 (40 KRISTA): %1 blk to 30 (~%2)")
            .arg(QLocale().toString(rem)).arg(formatDurationSeconds(static_cast<qint64>(rem) * targetSec)));
        barDecay->setValue(static_cast<int>((1000.0 * std::max(0, height)) / bootstrapEnd));
    } else if (height <= 5000) {
        const int rem = 5001 - height;
        lblDecayText->setText(tr("Bootstrap 2 (30 KRISTA): %1 blk to 20 (~%2)")
            .arg(QLocale().toString(rem)).arg(formatDurationSeconds(static_cast<qint64>(rem) * targetSec)));
        barDecay->setValue(static_cast<int>((1000.0 * height) / bootstrapEnd));
    } else if (height < bootstrapEnd) {
        const int rem = bootstrapEnd - height;
        lblDecayText->setText(tr("Bootstrap 3 (20 KRISTA): %1 blk to 15 base (~%2)")
            .arg(QLocale().toString(rem)).arg(formatDurationSeconds(static_cast<qint64>(rem) * targetSec)));
        barDecay->setValue(static_cast<int>((1000.0 * height) / bootstrapEnd));
    } else {
        const int epoch = 1 + (height - bootstrapEnd) / decayInterval;
        const int blocksIntoPeriod = (height - bootstrapEnd) % decayInterval;
        const int blocksRemaining = decayInterval - blocksIntoPeriod;
        lblDecayText->setText(tr("Decay Epoch %1 (-1.9%): %2 blk (~%3)")
            .arg(epoch).arg(QLocale().toString(blocksRemaining))
            .arg(formatDurationSeconds(static_cast<qint64>(blocksRemaining) * targetSec)));
        barDecay->setValue(static_cast<int>((1000.0 * blocksIntoPeriod) / decayInterval));
    }

    if (IsModelDActive(evalHeight)) {
        const CAmount mnShare   = (netPool * 35) / 100;
        const CAmount llmqShare = (netPool * 10) / 100;
        const CAmount adamShare = (netPool * 40) / 100;
        const CAmount posShare  = netPool - mnShare - llmqShare - adamShare;

        const CAmount adamMnPool   = (netPool * 20) / 100;
        const CAmount adamLockPool = (netPool * 8) / 100;
        const CAmount adamPowPool  = (netPool * 12) / 100;

        lblModelDSplitTitle->setText(tr("Model D Block Split (Net 93% + 7% Dev)"));
        if (barModelDSplit) {
            barModelDSplit->setSegments({
                {static_cast<double>(adamShare), COLOR_ADAM},
                {static_cast<double>(mnShare),   COLOR_MN},
                {static_cast<double>(posShare),  COLOR_POS},
                {static_cast<double>(llmqShare), COLOR_LLMQ},
                {static_cast<double>(devShare),  COLOR_DEV}
            }, QColor(trackBg));
        }

        lblSplitAdamVal->setText(tr("%1 (20% MN • 8% L250 • 12% PoW)").arg(formatAmountPlain(adamShare, true)));
        lblSplitAdamVal->setToolTip(tr("7 MN seats: %1\n1 Coin-Lock seat: %2\n3 Open PoW seats: %3")
            .arg(formatAmountPlain(adamMnPool, false),
                 formatAmountPlain(adamLockPool, false),
                 formatAmountPlain(adamPowPool, false)));
        lblSplitMnVal->setText(formatAmountPlain(mnShare, true));
        lblSplitPosVal->setText(formatAmountPlain(posShare, true));
        lblSplitLlmqVal->setText(formatAmountPlain(llmqShare, true));
        lblSplitDevVal->setText(formatAmountPlain(devShare, true));
    } else {
        const CAmount validatorPool = IsAdamValidatorSplitActive(evalHeight) ? ((netPool * 40) / 100) : 0;
        const CAmount producerPool  = netPool - validatorPool;
        lblModelDSplitTitle->setText(tr("Pre-Model D Block Split"));
        if (barModelDSplit) {
            barModelDSplit->setSegments({
                {static_cast<double>(validatorPool), COLOR_ADAM},
                {static_cast<double>(producerPool),  COLOR_POS},
                {static_cast<double>(devShare),      COLOR_DEV}
            }, QColor(trackBg));
        }
        lblSplitAdamVal->setText(formatAmountPlain(validatorPool, true));
        lblSplitMnVal->setText(QStringLiteral("0.00"));
        lblSplitPosVal->setText(formatAmountPlain(producerPool, true));
        lblSplitLlmqVal->setText(QStringLiteral("0.00"));
        lblSplitDevVal->setText(formatAmountPlain(devShare, true));
    }

    // Right Card 2: Consensus Pool & Node Status
    if (hasPoolSnapshot) {
        lblPoolMpaVal->setText(tr("%1 total (%2 MN • %3 L250 • %4 PoW)")
            .arg(loc.toString(static_cast<qulonglong>(poolTotal)))
            .arg(loc.toString(static_cast<qulonglong>(mnPoolCount)))
            .arg(loc.toString(static_cast<qulonglong>(coinLockCount)))
            .arg(loc.toString(static_cast<qulonglong>(powPoolCount))));
        const CAmount mnCol = CMasternode::GetMasternodeNodeCollateral(evalHeight);
        lblPoolMnCountVal->setText(tr("%1 enabled (%2)")
            .arg(loc.toString(mnEnabled))
            .arg(formatAmountPlain(mnCol, true)));
    } else {
        lblPoolMpaVal->setText(QStringLiteral("--"));
        lblPoolMnCountVal->setText(QStringLiteral("--"));
    }

    lblNetHashVal->setText(formatHashRate(networkHashPs));

    const bool internalActive = fGenerateBitcoins.load();
    const double rawHashPs = internalActive ? std::max(0.0, dHashesPerSec.load()) : 0.0;
    const int minerThreads = std::max(1, GetConfiguredMinerThreads());
    if (internalActive && rawHashPs > 0.0) {
        lblLocalCpuVal->setText((minerThreads == 1 ? tr("%1 (%2 thread)") : tr("%1 (%2 threads)"))
            .arg(formatHashRate(rawHashPs), loc.toString(minerThreads)));
        lblLocalCpuVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textPrimary));
    } else if (internalActive) {
        lblLocalCpuVal->setText((minerThreads == 1 ? tr("Standby (%1 thread)") : tr("Standby (%1 threads)"))
            .arg(loc.toString(minerThreads)));
        lblLocalCpuVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textPrimary));
    } else {
        lblLocalCpuVal->setText(tr("Idle (Off)"));
        lblLocalCpuVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    }

    const auto extMiners = GetActiveExternalAdamMiners(65);
    if (extMiners.empty()) {
        lblExternalMinersVal->setText(tr("None detected"));
        lblExternalMinersVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    } else {
        int cpuCnt = 0;
        int gpuCnt = 0;
        int unkCnt = 0;
        for (const auto& info : extMiners) {
            if (info.kind == ExternalMinerKind::CPUMINER) ++cpuCnt;
            else if (info.kind == ExternalMinerKind::CCMINER) ++gpuCnt;
            else ++unkCnt;
        }
        QStringList parts;
        if (cpuCnt > 0) parts << tr("%1 CPU").arg(loc.toString(cpuCnt));
        if (gpuCnt > 0) parts << tr("%1 GPU").arg(loc.toString(gpuCnt));
        if (unkCnt > 0) parts << tr("%1 RPC").arg(loc.toString(unkCnt));
        lblExternalMinersVal->setText(tr("%1 active (%2)").arg(loc.toString(static_cast<qulonglong>(extMiners.size()))).arg(parts.join(QStringLiteral(", "))));
        lblExternalMinersVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(activeCol));
    }

    if (stakingActive) {
        lblStakingVal->setText(tr("Active (Staking)"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(activeCol));
    } else if (!GetBoolArg("-staking", true)) {
        lblStakingVal->setText(tr("Disabled (-staking=0)"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    } else if (walletModel && walletModel->getEncryptionStatus() == WalletModel::Locked) {
        lblStakingVal->setText(tr("Wallet Locked"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    } else if (peers == 0 || IsInitialBlockDownload()) {
        lblStakingVal->setText(tr("Waiting for Sync / Peers"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    } else if (avail <= 0) {
        lblStakingVal->setText(tr("Waiting for Mature Coins"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    } else {
        const bool isUnenc = walletModel && walletModel->getEncryptionStatus() == WalletModel::Unencrypted;
        lblStakingVal->setText(isUnenc ? tr("Standby (Unencrypted)") : tr("Standby (Ready)"));
        lblStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: bold; }").arg(textLabel));
    }

    renderRewardChart(snap);
}
