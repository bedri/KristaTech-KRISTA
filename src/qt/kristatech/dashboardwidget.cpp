// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/dashboardwidget.h"

#include "adam.h"
#include "bitcoinunits.h"
#include "chainparams.h"
#include "clientmodel.h"
#include "evo/deterministicmns.h"
#include "guiutil.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternode.h"
#include "masternodeman.h"
#include "optionsmodel.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/minerhubwidget.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/sendconfirmdialog.h"
#include "qt/kristatech/txrow.h"
#include "utiltime.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <set>

#define DECORATION_SIZE 62

static quint32 rewardsFilterMask()
{
    return TransactionFilterProxy::TYPE(TransactionRecord::StakeMint)
         | TransactionFilterProxy::TYPE(TransactionRecord::MNReward)
         | TransactionFilterProxy::TYPE(TransactionRecord::LlmqReward)
         | TransactionFilterProxy::TYPE(TransactionRecord::AdamReward)
         | TransactionFilterProxy::TYPE(TransactionRecord::Generated);
}

static QString formatBlocksEtaCompact(int blocks, int64_t targetSpacingSec)
{
    if (blocks <= 0 || targetSpacingSec <= 0) return QStringLiteral("~0m");
    const qint64 secs = static_cast<qint64>(blocks) * static_cast<qint64>(targetSpacingSec);
    const qint64 days = secs / 86400;
    const qint64 hrs = (secs % 86400) / 3600;
    const qint64 mins = (secs % 3600) / 60;
    if (days > 0) return QStringLiteral("~%1d %2h").arg(days).arg(hrs);
    if (hrs > 0)  return QStringLiteral("~%1h %2m").arg(hrs).arg(mins);
    return QStringLiteral("~%1m").arg(std::max<qint64>(1, mins));
}

class HomeSplitBar : public QWidget
{
public:
    struct Seg {
        double value;
        QColor color;
    };

    explicit HomeSplitBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(8);
    }

    void setSegments(const QVector<Seg>& s, const QColor& trackBg)
    {
        segments = s;
        track = trackBg;
        update();
    }

    void setTrackColor(const QColor& trackBg)
    {
        track = trackBg;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(0, 0, width(), height());
        QPainterPath clip;
        clip.addRoundedRect(r, 4.0, 4.0);
        p.setClipPath(clip);
        p.fillRect(r, track);

        double total = 0.0;
        for (const Seg& s : segments) {
            if (s.value > 0.0) total += s.value;
        }
        if (total <= 0.0) return;

        double x = 0.0;
        for (int i = 0; i < segments.size(); ++i) {
            if (segments[i].value <= 0.0) continue;
            const double w = (i == segments.size() - 1)
                ? (r.width() - x)
                : (r.width() * (segments[i].value / total));
            if (w > 0.5) {
                p.fillRect(QRectF(x, 0.0, w, r.height()), segments[i].color);
                x += w;
            }
        }
    }

private:
    QVector<Seg> segments;
    QColor track = QColor("#1E293B");
};

class HomeCardDragGrip : public QWidget
{
public:
    explicit HomeCardDragGrip(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(12, 16);
        setCursor(Qt::OpenHandCursor);
        setToolTip(QObject::tr("Drag to reorder card"));
    }

    void setColors(const QColor& normal, const QColor& hover)
    {
        dotNormal = normal;
        dotHover = hover;
        update();
    }

    void setActive(bool on)
    {
        if (active != on) {
            active = on;
            update();
        }
    }

protected:
    void enterEvent(QEvent*) override
    {
        hovered = true;
        update();
    }

    void leaveEvent(QEvent*) override
    {
        hovered = false;
        update();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush((active || hovered) ? dotHover : dotNormal);

        const double xs[2] = {3.5, 8.5};
        const double ys[3] = {3.5, 8.0, 12.5};
        for (double y : ys) {
            for (double x : xs) {
                p.drawEllipse(QPointF(x, y), 1.25, 1.25);
            }
        }
    }

private:
    QColor dotNormal = QColor("#94A3B8");
    QColor dotHover = QColor("#3E5B94");
    bool hovered = false;
    bool active = false;
};

class HomeDragGhostWidget : public QWidget
{
public:
    HomeDragGhostWidget(const QPixmap& pix, const QColor& border, QWidget* parent = nullptr)
        : QWidget(parent), pixmap(pix), borderColor(border)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r(0.0, 0.0, width(), height());

        QPainterPath clip;
        clip.addRoundedRect(r, 10.0, 10.0);
        p.save();
        p.setClipPath(clip);
        p.setOpacity(0.92);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(rect(), pixmap);
        p.restore();

        p.setPen(QPen(borderColor, 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.75, 0.75, -0.75, -0.75), 10.0, 10.0);
    }

private:
    QPixmap pixmap;
    QColor borderColor;
};

class HomeDragSlotMask : public QWidget
{
public:
    HomeDragSlotMask(const QColor& fill, const QColor& border, QWidget* parent = nullptr)
        : QWidget(parent), fillColor(fill), borderColor(border)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void setColors(const QColor& fill, const QColor& border)
    {
        fillColor = fill;
        borderColor = border;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        p.setBrush(fillColor);
        QPen pen(borderColor, 1.5, Qt::CustomDashLine);
        pen.setDashPattern({4.0, 3.0});
        p.setPen(pen);
        p.drawRoundedRect(r, 10.0, 10.0);
    }

private:
    QColor fillColor;
    QColor borderColor;
};

static QString formatAgeCompact(qint64 secs)
{
    if (secs < 0) secs = 0;
    if (secs < 60) return QString("%1s").arg(secs);
    if (secs < 3600) return QString("%1m %2s").arg(secs / 60).arg(secs % 60);
    if (secs < 86400) return QString("%1h %2m").arg(secs / 3600).arg((secs % 3600) / 60);
    return QString("%1d %2h").arg(secs / 86400).arg((secs % 86400) / 3600);
}

DashboardWidget::DashboardWidget(KRISTATECHGUI* parent) :
    PWidget(parent)
{
    txHolder = new TxViewHolder(isLightTheme());
    txViewDelegate = new FurAbstractListItemDelegate(
        DECORATION_SIZE,
        txHolder,
        this
    );

    QVBoxLayout* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(22, 16, 22, 16);
    rootLayout->setSpacing(12);

    // =========================================================================
    // ROW 1: Header Title
    // =========================================================================
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(10);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(2);
    lblHeaderTitle = new QLabel(tr("Wallet Overview"), this);
    lblHeaderSubtitle = new QLabel(tr("Live portfolio summary, consensus status, and categorized wallet activity."), this);
    titleCol->addWidget(lblHeaderTitle);
    titleCol->addWidget(lblHeaderSubtitle);

    headerRow->addLayout(titleCol, 1);
    rootLayout->addLayout(headerRow);

    // =========================================================================
    // ROW 2: Sync Warning Banner (shown only while syncing)
    // =========================================================================
    warningBanner = new QFrame(this);
    warningBanner->setObjectName("homeWarningBanner");
    warningBanner->setAttribute(Qt::WA_StyledBackground, true);
    QHBoxLayout* warnLayout = new QHBoxLayout(warningBanner);
    warnLayout->setContentsMargins(14, 8, 14, 8);
    warnLayout->setSpacing(8);
    lblWarning = new QLabel(QString::fromUtf8("\xE2\x86\xBB ") + tr("Wallet is still synchronizing — balances and recent transactions will update automatically once synced."), warningBanner);
    lblWarning->setWordWrap(true);
    warnLayout->addWidget(lblWarning);
    rootLayout->addWidget(warningBanner);

    // =========================================================================
    // ROW 3: 4-Card KPI Strip
    // =========================================================================
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setContentsMargins(0, 0, 0, 0);
    kpiRow->setSpacing(12);

    auto makeKpiCard = [this](const QString& objName, const QString& title, QFrame*& cardOut, QLabel*& titleOut, QLabel*& valOut, QLabel*& subOut) {
        cardOut = new QFrame(this);
        cardOut->setObjectName(objName);
        cardOut->setAttribute(Qt::WA_StyledBackground, true);
        cardOut->setMinimumWidth(0);
        QVBoxLayout* l = new QVBoxLayout(cardOut);
        l->setContentsMargins(14, 11, 14, 11);
        l->setSpacing(3);
        titleOut = new QLabel(title, cardOut);
        titleOut->setMinimumWidth(0);
        valOut = new QLabel(QStringLiteral("--"), cardOut);
        valOut->setMinimumWidth(0);
        subOut = new QLabel(QStringLiteral("--"), cardOut);
        subOut->setMinimumWidth(0);
        l->addWidget(titleOut);
        l->addWidget(valOut);
        l->addWidget(subOut);
    };

    makeKpiCard("homeKpiTotal", tr("Total Balance"), cardKpiTotal, lblKpiTotalTitle, lblKpiTotalVal, lblKpiTotalSub);
    makeKpiCard("homeKpiSpend", tr("Spendable"), cardKpiSpendable, lblKpiSpendableTitle, lblKpiSpendableVal, lblKpiSpendableSub);
    makeKpiCard("homeKpiLock", tr("Locked Collateral"), cardKpiLocked, lblKpiLockedTitle, lblKpiLockedVal, lblKpiLockedSub);
    makeKpiCard("homeKpiImm", tr("Immature Rewards"), cardKpiImmature, lblKpiImmatureTitle, lblKpiImmatureVal, lblKpiImmatureSub);

    kpiRow->addWidget(cardKpiTotal, 1);
    kpiRow->addWidget(cardKpiSpendable, 1);
    kpiRow->addWidget(cardKpiLocked, 1);
    kpiRow->addWidget(cardKpiImmature, 1);
    rootLayout->addLayout(kpiRow);

    // =========================================================================
    // ROW 4: Main 2-Column Command Center (Transactions List | Earnings + Node)
    // =========================================================================
    QHBoxLayout* mainRow = new QHBoxLayout();
    mainRow->setContentsMargins(0, 0, 0, 0);
    mainRow->setSpacing(16);

    // Left Card: Categorized Recent Transactions
    txCard = new QFrame(this);
    txCard->setObjectName("homeTxCard");
    txCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* txLayout = new QVBoxLayout(txCard);
    txLayout->setContentsMargins(16, 14, 16, 12);
    txLayout->setSpacing(10);

    QHBoxLayout* txHeader = new QHBoxLayout();
    txHeader->setContentsMargins(0, 0, 0, 0);
    txHeader->setSpacing(8);
    lblTxTitle = new QLabel(tr("Recent Transactions"), txCard);
    lblTxCount = new QLabel(tr("0 tx"), txCard);

    comboBoxSortType = new QComboBox(txCard);
    comboBoxSortType->setMinimumHeight(30);
    comboBoxSortType->setMinimumWidth(135);
    setSortTxTypeFilter(comboBoxSortType);
    comboBoxSortType->insertItem(1, tr("All Rewards"), QVariant::fromValue<quint32>(rewardsFilterMask()));

    comboBoxSort = new QComboBox(txCard);
    comboBoxSort->setMinimumHeight(30);
    comboBoxSort->setMinimumWidth(135);
    setSortTx(comboBoxSort);

    txHeader->addWidget(lblTxTitle);
    txHeader->addWidget(lblTxCount);
    txHeader->addStretch();
    txHeader->addWidget(comboBoxSortType);
    txHeader->addWidget(comboBoxSort);
    txLayout->addLayout(txHeader);

    QHBoxLayout* txFilterSearchRow = new QHBoxLayout();
    txFilterSearchRow->setContentsMargins(0, 0, 0, 0);
    txFilterSearchRow->setSpacing(6);

    auto makeTxPill = [this](const QString& text) -> QPushButton* {
        QPushButton* b = new QPushButton(text, txCard);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::TabFocus);
        b->setFixedHeight(32);
        return b;
    };

    btnTxFilterAll      = makeTxPill(tr("All"));
    btnTxFilterRewards  = makeTxPill(tr("Rewards"));
    btnTxFilterReceived = makeTxPill(tr("Received"));
    btnTxFilterSent     = makeTxPill(tr("Sent"));
    btnTxFilterAll->setChecked(true);

    lineEditSearchTx = new QLineEdit(txCard);
    lineEditSearchTx->setClearButtonEnabled(true);
    lineEditSearchTx->setMinimumHeight(32);
    lineEditSearchTx->setMaximumHeight(32);
    lineEditSearchTx->setPlaceholderText(tr("Search by address, label, or TxID..."));

    txFilterSearchRow->addWidget(btnTxFilterAll);
    txFilterSearchRow->addWidget(btnTxFilterRewards);
    txFilterSearchRow->addWidget(btnTxFilterReceived);
    txFilterSearchRow->addWidget(btnTxFilterSent);
    txFilterSearchRow->addSpacing(4);
    txFilterSearchRow->addWidget(lineEditSearchTx, 1);
    txLayout->addLayout(txFilterSearchRow);

    listTransactions = new QListView(txCard);
    listTransactions->setObjectName("homeListTransactions");
    listTransactions->setItemDelegate(txViewDelegate);
    listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    listTransactions->setSelectionBehavior(QAbstractItemView::SelectRows);
    listTransactions->setUniformItemSizes(true);
    listTransactions->setFrameShape(QFrame::NoFrame);
    listTransactions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    txLayout->addWidget(listTransactions, 1);

    emptyContainer = new QWidget(txCard);
    emptyContainer->setVisible(false);
    QVBoxLayout* emptyLayout = new QVBoxLayout(emptyContainer);
    emptyLayout->setContentsMargins(20, 40, 20, 40);
    emptyLayout->setSpacing(12);
    emptyLayout->setAlignment(Qt::AlignCenter);
    lblEmpty = new QLabel(tr("No transactions yet"), emptyContainer);
    lblEmpty->setAlignment(Qt::AlignCenter);
    btnLearnMore = new QPushButton(tr("Help && FAQ"), emptyContainer);
    btnLearnMore->setCursor(Qt::PointingHandCursor);
    btnLearnMore->setFocusPolicy(Qt::TabFocus);
    btnLearnMore->setFixedHeight(34);
    emptyLayout->addWidget(lblEmpty);
    emptyLayout->addWidget(btnLearnMore, 0, Qt::AlignHCenter);
    txLayout->addWidget(emptyContainer, 1);

    // Right Column: Reorderable Earnings Snapshot, Node & Consensus, and My Masternodes
    rightCol = new QWidget(this);
    rightCol->setMinimumWidth(315);
    rightCol->setMaximumWidth(375);
    rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(12);

    // Right Card 1: Earnings Snapshot
    earningsCard = new QFrame(rightCol);
    earningsCard->setObjectName("homeEarningsCard");
    earningsCard->setProperty("homeCardId", QStringLiteral("earnings"));
    earningsCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* earnLayout = new QVBoxLayout(earningsCard);
    earnLayout->setContentsMargins(16, 14, 16, 14);
    earnLayout->setSpacing(10);

    QHBoxLayout* earnHeader = new QHBoxLayout();
    earnHeader->setContentsMargins(0, 0, 0, 0);
    earnHeader->setSpacing(7);
    HomeCardDragGrip* earnGrip = new HomeCardDragGrip(earningsCard);
    rightCardGrips.append(earnGrip);
    lblEarningsTitle = new QLabel(tr("Earnings Snapshot"), earningsCard);
    lblEarningsTitle->setCursor(Qt::OpenHandCursor);
    lblEarningsTitle->setToolTip(tr("Drag to reorder card"));
    lblEarningsPeriodBadge = new QLabel(tr("Last 7d"), earningsCard);
    earnHeader->addWidget(earnGrip, 0, Qt::AlignVCenter);
    earnHeader->addWidget(lblEarningsTitle);
    earnHeader->addStretch();
    earnHeader->addWidget(lblEarningsPeriodBadge);
    earnLayout->addLayout(earnHeader);

    QHBoxLayout* earnTotalsRow = new QHBoxLayout();
    earnTotalsRow->setContentsMargins(0, 0, 0, 0);
    earnTotalsRow->setSpacing(14);

    QVBoxLayout* col24h = new QVBoxLayout();
    col24h->setSpacing(2);
    lblEarn24hTitle = new QLabel(tr("Last 24 Hours"), earningsCard);
    lblEarn24hVal = new QLabel(QStringLiteral("+0.00 KRISTA"), earningsCard);
    col24h->addWidget(lblEarn24hTitle);
    col24h->addWidget(lblEarn24hVal);

    QVBoxLayout* col7d = new QVBoxLayout();
    col7d->setSpacing(2);
    lblEarn7dTitle = new QLabel(tr("Last 7 Days"), earningsCard);
    lblEarn7dVal = new QLabel(QStringLiteral("+0.00 KRISTA"), earningsCard);
    col7d->addWidget(lblEarn7dTitle);
    col7d->addWidget(lblEarn7dVal);

    earnTotalsRow->addLayout(col24h, 1);
    earnTotalsRow->addLayout(col7d, 1);
    earnLayout->addLayout(earnTotalsRow);

    earningsSplitBar = new HomeSplitBar(earningsCard);
    earnLayout->addWidget(earningsSplitBar);

    QGridLayout* splitGrid = new QGridLayout();
    splitGrid->setContentsMargins(0, 0, 0, 0);
    splitGrid->setHorizontalSpacing(8);
    splitGrid->setVerticalSpacing(6);

    auto initSplitRow = [&](int rowIdx, const QString& label, QLabel*& dotOut, QLabel*& keyOut, QLabel*& valOut) {
        dotOut = new QLabel(earningsCard);
        dotOut->setFixedSize(10, 10);
        keyOut = new QLabel(label, earningsCard);
        valOut = new QLabel(QStringLiteral("0.00"), earningsCard);
        valOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        splitGrid->addWidget(dotOut, rowIdx, 0, Qt::AlignVCenter);
        splitGrid->addWidget(keyOut, rowIdx, 1);
        splitGrid->addWidget(valOut, rowIdx, 2);
    };

    initSplitRow(0, tr("Staking Rewards"), lblSplitPosDot, lblSplitPosKey, lblSplitPosVal);
    initSplitRow(1, tr("Masternode Rewards"), lblSplitMnDot, lblSplitMnKey, lblSplitMnVal);
    initSplitRow(2, tr("Mining Rewards"), lblSplitAdamDot, lblSplitAdamKey, lblSplitAdamVal);
    lblSplitPosKey->setToolTip(tr("Proof-of-Stake (PoS) coinstake block rewards"));
    lblSplitMnKey->setToolTip(tr("Masternode Passive Pool (5%), Validator & LLMQ Quorum (10%) rewards"));
    lblSplitAdamKey->setToolTip(tr("Cooperative ADAM puzzle & Proof-of-Work mining rewards"));
    splitGrid->setColumnStretch(1, 1);
    earnLayout->addLayout(splitGrid);

    btnOpenAnalytics = new QPushButton(tr("Open Full Reward Analytics") + QString::fromUtf8(" \xE2\x86\x92"), earningsCard);
    btnOpenAnalytics->setCursor(Qt::PointingHandCursor);
    btnOpenAnalytics->setFocusPolicy(Qt::TabFocus);
    btnOpenAnalytics->setFixedHeight(34);
    earnLayout->addWidget(btnOpenAnalytics);

    // Right Card 2: Node & Consensus Status
    nodeCard = new QFrame(rightCol);
    nodeCard->setObjectName("homeNodeCard");
    nodeCard->setProperty("homeCardId", QStringLiteral("node"));
    nodeCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* nodeLayout = new QVBoxLayout(nodeCard);
    nodeLayout->setContentsMargins(16, 14, 16, 14);
    nodeLayout->setSpacing(10);

    QHBoxLayout* nodeHeader = new QHBoxLayout();
    nodeHeader->setContentsMargins(0, 0, 0, 0);
    nodeHeader->setSpacing(7);
    HomeCardDragGrip* nodeGrip = new HomeCardDragGrip(nodeCard);
    rightCardGrips.append(nodeGrip);
    lblNodeTitle = new QLabel(tr("Network Status"), nodeCard);
    lblNodeTitle->setCursor(Qt::OpenHandCursor);
    lblNodeTitle->setToolTip(tr("Node & Consensus Status — Drag to reorder card"));
    lblNodeSyncPill = new QLabel(tr("Syncing"), nodeCard);
    nodeHeader->addWidget(nodeGrip, 0, Qt::AlignVCenter);
    nodeHeader->addWidget(lblNodeTitle);
    nodeHeader->addStretch();
    nodeHeader->addWidget(lblNodeSyncPill);
    nodeLayout->addLayout(nodeHeader);

    QGridLayout* nodeGrid = new QGridLayout();
    nodeGrid->setContentsMargins(0, 0, 0, 0);
    nodeGrid->setHorizontalSpacing(8);
    nodeGrid->setVerticalSpacing(7);

    auto initNodeRow = [&](int rowIdx, const QString& label, QLabel*& keyOut, QLabel*& valOut) {
        keyOut = new QLabel(label, nodeCard);
        valOut = new QLabel(QStringLiteral("--"), nodeCard);
        valOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        nodeGrid->addWidget(keyOut, rowIdx, 0);
        nodeGrid->addWidget(valOut, rowIdx, 1);
    };

    initNodeRow(0, tr("Block Height"), lblNodeHeightKey, lblNodeHeightVal);
    initNodeRow(1, tr("Network Peers"), lblNodePeersKey, lblNodePeersVal);
    initNodeRow(2, tr("Active Masternodes"), lblNodeMnsKey, lblNodeMnsVal);
    initNodeRow(3, tr("Active Miner Pool"), lblNodePoolKey, lblNodePoolVal);
    initNodeRow(4, tr("Staking"), lblNodeStakingKey, lblNodeStakingVal);
    lblNodePoolKey->setToolTip(tr("Active ADAM MPA registered miners & validators"));
    lblNodeStakingKey->setToolTip(tr("Proof-of-Stake (PoS) wallet staking status"));
    nodeGrid->setColumnStretch(0, 1);
    nodeLayout->addLayout(nodeGrid);

    btnNodeTelemetry = new QPushButton(tr("Open Network Telemetry") + QString::fromUtf8(" \xE2\x86\x92"), nodeCard);
    btnNodeTelemetry->setCursor(Qt::PointingHandCursor);
    btnNodeTelemetry->setFocusPolicy(Qt::TabFocus);
    btnNodeTelemetry->setFixedHeight(34);
    nodeLayout->addWidget(btnNodeTelemetry);

    // Right Card 3: My Masternodes Summary
    masternodesCard = new QFrame(rightCol);
    masternodesCard->setObjectName("homeMasternodesCard");
    masternodesCard->setProperty("homeCardId", QStringLiteral("masternodes"));
    masternodesCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* mnLayout = new QVBoxLayout(masternodesCard);
    mnLayout->setContentsMargins(16, 14, 16, 14);
    mnLayout->setSpacing(10);

    QHBoxLayout* mnHeader = new QHBoxLayout();
    mnHeader->setContentsMargins(0, 0, 0, 0);
    mnHeader->setSpacing(7);
    HomeCardDragGrip* mnGrip = new HomeCardDragGrip(masternodesCard);
    rightCardGrips.append(mnGrip);
    lblMnCardTitle = new QLabel(tr("My Masternodes"), masternodesCard);
    lblMnCardTitle->setCursor(Qt::OpenHandCursor);
    lblMnCardTitle->setToolTip(tr("Drag to reorder card"));
    lblMnCardPill = new QLabel(tr("Syncing"), masternodesCard);
    mnHeader->addWidget(mnGrip, 0, Qt::AlignVCenter);
    mnHeader->addWidget(lblMnCardTitle);
    mnHeader->addStretch();
    mnHeader->addWidget(lblMnCardPill);
    mnLayout->addLayout(mnHeader);

    QGridLayout* mnGrid = new QGridLayout();
    mnGrid->setContentsMargins(0, 0, 0, 0);
    mnGrid->setHorizontalSpacing(8);
    mnGrid->setVerticalSpacing(7);

    auto initMnRow = [&](int rowIdx, const QString& label, QLabel*& keyOut, QLabel*& valOut) {
        keyOut = new QLabel(label, masternodesCard);
        valOut = new QLabel(QStringLiteral("--"), masternodesCard);
        valOut->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        mnGrid->addWidget(keyOut, rowIdx, 0);
        mnGrid->addWidget(valOut, rowIdx, 1);
    };

    initMnRow(0, tr("Masternodes"), lblMnCountKey, lblMnCountVal);
    initMnRow(1, tr("PoSe Health"), lblMnPoseKey, lblMnPoseVal);
    initMnRow(2, tr("Collateral"), lblMnColKey, lblMnColVal);
    initMnRow(3, tr("Next Collateral"), lblMnNextColKey, lblMnNextColVal);
    initMnRow(4, tr("Next Step In"), lblMnNextBlocksKey, lblMnNextBlocksVal);
    mnGrid->setColumnStretch(0, 1);
    mnLayout->addLayout(mnGrid);

    btnOpenMasternodes = new QPushButton(tr("Open Masternodes") + QString::fromUtf8(" \xE2\x86\x92"), masternodesCard);
    btnOpenMasternodes->setCursor(Qt::PointingHandCursor);
    btnOpenMasternodes->setFocusPolicy(Qt::TabFocus);
    btnOpenMasternodes->setFixedHeight(34);
    mnLayout->addWidget(btnOpenMasternodes);

    registerRightCardDragTarget(earningsCard);
    registerRightCardDragTarget(nodeCard);
    registerRightCardDragTarget(masternodesCard);

    const QStringList savedOrder = getSettings()->value(QStringLiteral("homeRightCardOrder")).toStringList();
    applyRightCardOrder(savedOrder);

    mainRow->addWidget(txCard, 17);
    mainRow->addWidget(rightCol, 8);
    rootLayout->addLayout(mainRow, 1);

    coalesceTimer = new QTimer(this);
    coalesceTimer->setSingleShot(true);
    coalesceTimer->setInterval(250);
    connect(coalesceTimer, &QTimer::timeout, this, &DashboardWidget::refreshOverviewStats);

    liveAgeTimer = new QTimer(this);
    liveAgeTimer->setInterval(5000);
    connect(liveAgeTimer, &QTimer::timeout, this, [this]() {
        if (isVisible()) refreshLiveNodeStatus();
    });
    liveAgeTimer->start();

    connect(listTransactions, &QListView::clicked, this, &DashboardWidget::handleTransactionClicked);
    connect(comboBoxSort, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged),
            this, &DashboardWidget::onSortChanged);
    connect(comboBoxSortType, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged),
            this, &DashboardWidget::onSortTypeChanged);
    connect(lineEditSearchTx, &QLineEdit::textChanged, this, &DashboardWidget::onSearchTxChanged);

    connect(btnTxFilterAll,      &QPushButton::clicked, this, [this]() { applyQuickTxFilter(0); });
    connect(btnTxFilterRewards,  &QPushButton::clicked, this, [this]() { applyQuickTxFilter(1); });
    connect(btnTxFilterReceived, &QPushButton::clicked, this, [this]() { applyQuickTxFilter(2); });
    connect(btnTxFilterSent,     &QPushButton::clicked, this, [this]() { applyQuickTxFilter(3); });

    if (parent) {
        connect(btnOpenAnalytics,   &QPushButton::clicked, parent, &KRISTATECHGUI::goToStats);
        connect(btnNodeTelemetry,   &QPushButton::clicked, parent, &KRISTATECHGUI::goToTelemetry);
        connect(btnOpenMasternodes, &QPushButton::clicked, parent, &KRISTATECHGUI::goToMasterNodes);
        connect(btnLearnMore,       &QPushButton::clicked, parent, [parent]() { parent->openFAQ(0); });
    }

    initMinerHub();
    applyTheme(isLightTheme());
}

void DashboardWidget::initMinerHub()
{
    minerHubWidget = new MinerHubWidget(window, window);
    minerHubWidget->refreshMinerHubStatus();
}

QWidget* DashboardWidget::getMinerHubPage() const
{
    return minerHubWidget;
}

void DashboardWidget::shutdownMinerWorkers()
{
    if (minerHubWidget) minerHubWidget->shutdownMinerWorkers();
}

void DashboardWidget::setMinerHubFullScreenMode(bool enable)
{
    if (minerHubWidget) minerHubWidget->setMinerHubFullScreenMode(enable);
}

bool DashboardWidget::isMinerHubFullScreenMode() const
{
    return minerHubWidget ? minerHubWidget->isMinerHubFullScreenMode() : false;
}

void DashboardWidget::applyQuickTxFilter(int mode)
{
    quickTxFilterMode = mode;
    if (btnTxFilterAll)      btnTxFilterAll->setChecked(mode == 0);
    if (btnTxFilterRewards)  btnTxFilterRewards->setChecked(mode == 1);
    if (btnTxFilterReceived) btnTxFilterReceived->setChecked(mode == 2);
    if (btnTxFilterSent)     btnTxFilterSent->setChecked(mode == 3);

    if (!filter) return;

    quint32 mask = TransactionFilterProxy::ALL_TYPES;
    if (mode == 1) {
        mask = rewardsFilterMask();
    } else if (mode == 2) {
        mask = TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress)
             | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther);
    } else if (mode == 3) {
        mask = TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress)
             | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther);
    }

    filter->setTypeFilter(mask);
    if (comboBoxSortType) {
        const QSignalBlocker blocker(comboBoxSortType);
        const int idx = comboBoxSortType->findData(QVariant::fromValue<quint32>(mask));
        comboBoxSortType->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    listTransactions->update();
    showList();
}

void DashboardWidget::applyTheme(bool isLight)
{
    const QString cardBg     = isLight ? "#FFFFFF" : "#131B2E";
    const QString cardBrd    = isLight ? "#CBD5E1" : "#253554";
    const QString titleCol   = isLight ? "#1E293B" : "#F1F5F9";
    const QString subCol     = isLight ? "#475569" : "#CBD5E1";
    const QString keyCol     = isLight ? "#475569" : "#B8C7DC";
    const QString inputBg    = isLight ? "#F8FAFC" : "#0D1424";
    const QString inputBrd   = isLight ? "#CBD5E1" : "#2D4066";
    const QString secBg      = isLight ? "#F1F5F9" : "#1C2942";
    const QString secFg      = isLight ? "#1E293B" : "#E2E8F0";
    const QString secBrd     = isLight ? "#CBD5E1" : "#33486E";
    const QString secHov     = isLight ? "#E2E8F0" : "#26385A";
    const QString warnBg     = isLight ? "#EEF3FA" : "#1B2B4A";
    const QString warnFg     = isLight ? "#3E5B94" : "#A8C1EE";
    const QString warnBrd    = isLight ? "#B8CBE8" : "#355184";

    if (lblHeaderTitle)    lblHeaderTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 22px; font-weight: 500; background: transparent; }").arg(titleCol));
    if (lblHeaderSubtitle) lblHeaderSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(subCol));

    const QString focusOutline = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString secBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
        "QPushButton:focus { border: 2px solid %5; outline: none; }"
    ).arg(secBg, secFg, secBrd, secHov, focusOutline);
    for (QPushButton* b : {btnOpenAnalytics, btnNodeTelemetry, btnOpenMasternodes, btnLearnMore}) {
        if (b) b->setStyleSheet(secBtnStyle);
    }

    if (warningBanner) {
        warningBanner->setStyleSheet(QString("QFrame#homeWarningBanner { background-color: %1; border: 1px solid %2; border-radius: 8px; }").arg(warnBg, warnBrd));
    }
    if (lblWarning) {
        lblWarning->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }").arg(warnFg));
    }

    auto styleKpi = [&](QFrame* card, const QString& objName, QLabel* titleLbl, QLabel* valLbl, QLabel* subLbl, const QString& accentValCol) {
        if (card) {
            card->setStyleSheet(QString("QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }").arg(objName, cardBg, cardBrd));
        }
        if (titleLbl) titleLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }").arg(keyCol));
        if (valLbl)   valLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 18px; font-weight: 500; background: transparent; }").arg(accentValCol));
        if (subLbl)   subLbl->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 500; background: transparent; }").arg(subCol));
    };

    styleKpi(cardKpiTotal,     "homeKpiTotal", lblKpiTotalTitle,     lblKpiTotalVal,     lblKpiTotalSub,     titleCol);
    styleKpi(cardKpiSpendable, "homeKpiSpend", lblKpiSpendableTitle, lblKpiSpendableVal, lblKpiSpendableSub, titleCol);
    styleKpi(cardKpiLocked,    "homeKpiLock",  lblKpiLockedTitle,    lblKpiLockedVal,    lblKpiLockedSub,    titleCol);
    styleKpi(cardKpiImmature,  "homeKpiImm",   lblKpiImmatureTitle,  lblKpiImmatureVal,  lblKpiImmatureSub,  titleCol);

    const QString cardStyle = QString("QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }");
    if (txCard)          txCard->setStyleSheet(cardStyle.arg("homeTxCard", cardBg, cardBrd));
    if (earningsCard)    earningsCard->setStyleSheet(cardStyle.arg("homeEarningsCard", cardBg, cardBrd));
    if (nodeCard)        nodeCard->setStyleSheet(cardStyle.arg("homeNodeCard", cardBg, cardBrd));
    if (masternodesCard) masternodesCard->setStyleSheet(cardStyle.arg("homeMasternodesCard", cardBg, cardBrd));

    const QString secTitleStyle = QString("QLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; }").arg(titleCol);
    if (lblTxTitle)       lblTxTitle->setStyleSheet(secTitleStyle);
    if (lblEarningsTitle) lblEarningsTitle->setStyleSheet(secTitleStyle);
    if (lblNodeTitle)     lblNodeTitle->setStyleSheet(secTitleStyle);
    if (lblMnCardTitle)   lblMnCardTitle->setStyleSheet(secTitleStyle);

    const QColor gripNormal = isLight ? QColor("#94A3B8") : QColor("#526585");
    const QColor gripHover  = isLight ? QColor("#3E5B94") : QColor("#93B4F0");
    for (HomeCardDragGrip* grip : rightCardGrips) {
        if (grip) grip->setColors(gripNormal, gripHover);
    }
    if (dragSlotMask) {
        dragSlotMask->setColors(
            isLight ? QColor(244, 248, 254, 218) : QColor(15, 23, 42, 215),
            QColor("#7695CD"));
    }

    if (lblTxCount) {
        lblTxCount->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(secBg, subCol, secBrd));
    }
    if (lblEarningsPeriodBadge) {
        lblEarningsPeriodBadge->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(secBg, subCol, secBrd));
    }

    const QString pillCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 14px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { border-color: #7695CD; background-color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; border: 1px solid #7695CD; }"
        "QPushButton:checked:hover { background-color: %7; border-color: %8; }"
        "QPushButton:focus { border: 2px solid %9; outline: none; }"
    ).arg(secBg, secFg, secBrd, secHov,
          isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
          isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"),
          isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860"),
          isLight ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0"),
          focusOutline);
    for (QPushButton* p : {btnTxFilterAll, btnTxFilterRewards, btnTxFilterReceived, btnTxFilterSent}) {
        if (p) p->setStyleSheet(pillCss);
    }

    const QString editStyle = QString(
        "QLineEdit { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 12px; }"
        "QLineEdit:focus { border-color: #7695CD; }"
    ).arg(inputBg, titleCol, inputBrd);
    if (lineEditSearchTx) {
        lineEditSearchTx->setStyleSheet(editStyle);
        styleLineEditClearButton(lineEditSearchTx);
    }

    const QString comboStyle = QString(
        "QComboBox { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 3px 10px; font-size: 12px; font-weight: 600; }"
        "QComboBox:hover, QComboBox:focus { border-color: %5; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox QAbstractItemView { background-color: %4; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; }"
    ).arg(inputBg, titleCol, inputBrd, cardBg, focusOutline);
    if (comboBoxSortType) comboBoxSortType->setStyleSheet(comboStyle);
    if (comboBoxSort)     comboBoxSort->setStyleSheet(comboStyle);

    if (listTransactions) {
        listTransactions->setStyleSheet("QListView#homeListTransactions { background: transparent; border: none; outline: none; }");
    }
    if (lblEmpty) lblEmpty->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; }").arg(subCol));

    const QString kStyle = QString("QLabel { color: %1; font-size: 12px; font-weight: 500; background: transparent; }").arg(keyCol);
    const QString vStyle = QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }").arg(titleCol);
    for (QLabel* k : {lblEarn24hTitle, lblEarn7dTitle, lblSplitPosKey, lblSplitMnKey, lblSplitAdamKey,
                      lblNodeHeightKey, lblNodePeersKey, lblNodeMnsKey, lblNodePoolKey, lblNodeStakingKey,
                      lblMnCountKey, lblMnPoseKey, lblMnColKey, lblMnNextColKey, lblMnNextBlocksKey}) {
        if (k) k->setStyleSheet(kStyle);
    }
    for (QLabel* v : {lblSplitPosVal, lblSplitMnVal, lblSplitAdamVal,
                      lblNodeHeightVal, lblNodePeersVal, lblNodeMnsVal, lblNodePoolVal, lblNodeStakingVal,
                      lblMnCountVal, lblMnPoseVal, lblMnColVal, lblMnNextColVal, lblMnNextBlocksVal}) {
        if (v) v->setStyleSheet(vStyle);
    }
    if (lblEarn24hVal) lblEarn24hVal->setStyleSheet(QString("QLabel { color: %1; font-size: 15px; font-weight: 500; background: transparent; }").arg(titleCol));
    if (lblEarn7dVal)  lblEarn7dVal->setStyleSheet(QString("QLabel { color: %1; font-size: 15px; font-weight: 500; background: transparent; }").arg(titleCol));

    if (lblSplitPosDot)  lblSplitPosDot->setStyleSheet("QLabel { background-color: #F5897C; border-radius: 5px; }");
    if (lblSplitMnDot)   lblSplitMnDot->setStyleSheet("QLabel { background-color: #7695CD; border-radius: 5px; }");
    if (lblSplitAdamDot) lblSplitAdamDot->setStyleSheet("QLabel { background-color: #8B7FD6; border-radius: 5px; }");

    applyOverviewThemeColors(isLight);
    if (listTransactions && listTransactions->viewport()) {
        listTransactions->viewport()->update();
    }
}

void DashboardWidget::loadClientModel()
{
    if (!clientModel) return;
    connect(clientModel, &ClientModel::numBlocksChanged, this, [this](int) {
        scheduleOverviewRefresh();
    }, Qt::UniqueConnection);
    connect(clientModel, &ClientModel::numConnectionsChanged, this, [this](int) {
        scheduleOverviewRefresh();
    }, Qt::UniqueConnection);
    scheduleOverviewRefresh();
}

void DashboardWidget::loadWalletModel()
{
    if (!walletModel || !walletModel->getOptionsModel()) return;

    txModel = walletModel->getTransactionTableModel();
    filter = new TransactionFilterProxy(this);
    filter->setDynamicSortFilter(true);
    filter->setSortCaseSensitivity(Qt::CaseInsensitive);
    filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filter->setSortRole(Qt::EditRole);
    filter->setSourceModel(txModel);

    filter->setTypeFilter(TransactionFilterProxy::ALL_TYPES);
    if (comboBoxSortType) {
        const int filterIndex = comboBoxSortType->findData(static_cast<quint32>(TransactionFilterProxy::ALL_TYPES));
        if (filterIndex >= 0) comboBoxSortType->setCurrentIndex(filterIndex);
    }

    changeSort(SortTx::DATE_DESC);

    txHolder->setFilter(filter);
    listTransactions->setModel(filter);
    listTransactions->setModelColumn(TransactionTableModel::ToAddress);

    connect(txModel, &TransactionTableModel::txArrived, this, &DashboardWidget::onTxArrived, Qt::UniqueConnection);
    connect(txModel, &QAbstractItemModel::modelReset, this, [this]() {
        showList();
        if (filter && filter->rowCount() > 0) {
            listTransactions->update();
        }
        scheduleOverviewRefresh();
    });
    connect(filter, &QAbstractItemModel::rowsInserted, this, [this]() {
        showList();
        scheduleOverviewRefresh();
    });
    connect(filter, &QAbstractItemModel::rowsRemoved, this, [this]() { showList(); });
    connect(txModel, &TransactionTableModel::rowsInserted, this, &DashboardWidget::processNewTransaction, Qt::UniqueConnection);

    connect(walletModel, &WalletModel::balanceChanged, this, [this](const interfaces::WalletBalances&) {
        scheduleOverviewRefresh();
    }, Qt::UniqueConnection);
    connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged,
            this, &DashboardWidget::updateDisplayUnit, Qt::UniqueConnection);

    updateDisplayUnit();
    showList();
    refreshOverviewStats();

    if (minerHubWidget) {
        minerHubWidget->setWalletModel(walletModel);
    }
}

void DashboardWidget::scheduleOverviewRefresh()
{
    overviewDirty = true;
    if (!isVisible()) return;
    if (coalesceTimer && !coalesceTimer->isActive()) {
        coalesceTimer->start();
    }
}

QString DashboardWidget::formatAmountPlain(CAmount amount, bool compactZero) const
{
    const int unit = (nDisplayUnit >= 0) ? nDisplayUnit : BitcoinUnits::PIV;
    if (amount == 0 && compactZero) {
        return QString("0.00 %1").arg(BitcoinUnits::name(unit));
    }
    return BitcoinUnits::formatWithUnit(unit, amount, false, BitcoinUnits::separatorAlways);
}

void DashboardWidget::refreshOverviewStats()
{
    if (!isVisible()) {
        overviewDirty = true;
        return;
    }
    overviewDirty = false;
    const bool light = isLightTheme();
    const int unit = (nDisplayUnit >= 0) ? nDisplayUnit : BitcoinUnits::PIV;

    // 1. Wallet Balances KPI
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
    auto setKpiBalance = [&](QFrame* card, QLabel* valLbl, CAmount amt) {
        const QString fullPrec = formatAmountPlain(amt, true);
        if (valLbl) {
            valLbl->setText(GUIUtil::formatBalance(amt, unit));
            valLbl->setToolTip(fullPrec);
        }
        if (card) {
            card->setToolTip(fullPrec);
        }
    };

    setKpiBalance(cardKpiTotal, lblKpiTotalVal, totalWallet);
    if (lblKpiTotalSub) {
        lblKpiTotalSub->setText(tr("Spendable + Locked + Immature"));
    }
    setKpiBalance(cardKpiSpendable, lblKpiSpendableVal, avail);
    if (lblKpiSpendableSub) {
        lblKpiSpendableSub->setText(unconfirmed > 0
            ? tr("Pending: +%1").arg(BitcoinUnits::floorWithUnit(unit, unconfirmed, false, BitcoinUnits::separatorAlways, true))
            : tr("Ready to send or stake"));
    }
    setKpiBalance(cardKpiLocked, lblKpiLockedVal, locked);
    if (lblKpiLockedSub) {
        lblKpiLockedSub->setText(locked > 0 ? tr("MN Collateral & CLTV locks") : tr("No active locks"));
    }
    setKpiBalance(cardKpiImmature, lblKpiImmatureVal, immature);
    lastStakingActive = stakingActive;
    if (lblKpiImmatureSub) {
        lblKpiImmatureSub->setText(stakingActive
            ? QString::fromUtf8("\xE2\x97\x8F ") + tr("PoS Staking Active")
            : QString::fromUtf8("\xE2\x97\x8B ") + tr("PoS Staking Inactive"));
    }

    // 2. Earnings Snapshot (full wallet history via cached aggregator: 24h, 7d, 3-series split)
    WalletModel::WalletActivitySummary activity;
    if (walletModel && walletModel->getWalletActivitySummary(activity, true)) {
        qint64 earn24h = 0, earn7d = 0;
        qint64 pos7d = 0, mn7d = 0, adamPow7d = 0;
        qint64 posAll = 0, mnAll = 0, adamPowAll = 0;

        const qint64 nowSec = QDateTime::currentSecsSinceEpoch();
        const qint64 cutoff24h = nowSec - 86400;
        const qint64 cutoff7d  = nowSec - 7LL * 86400;

        for (const WalletModel::WalletActivityEntry& e : activity.rewards) {
            const bool isPos     = (e.type == TransactionRecord::StakeMint);
            const bool isMnLlmq  = (e.type == TransactionRecord::MNReward || e.type == TransactionRecord::LlmqReward);
            const bool isAdamPow = (e.type == TransactionRecord::AdamReward || e.type == TransactionRecord::Generated);
            if (!isPos && !isMnLlmq && !isAdamPow) continue;

            const qint64 absAmt = e.amount;
            if (isPos) posAll += absAmt;
            else if (isMnLlmq) mnAll += absAmt;
            else adamPowAll += absAmt;

            if (e.time >= cutoff24h) earn24h += absAmt;
            if (e.time >= cutoff7d) {
                earn7d += absAmt;
                if (isPos) pos7d += absAmt;
                else if (isMnLlmq) mn7d += absAmt;
                else adamPow7d += absAmt;
            }
        }

        const bool use7d = (earn7d > 0);
        const qint64 showPos     = use7d ? pos7d     : posAll;
        const qint64 showMn      = use7d ? mn7d      : mnAll;
        const qint64 showAdamPow = use7d ? adamPow7d : adamPowAll;
        const qint64 showTotal   = showPos + showMn + showAdamPow;

        auto fmtCompact = [&](qint64 satoshis, bool withPlus) -> QString {
            const double val = static_cast<double>(satoshis) / static_cast<double>(BitcoinUnits::factor(unit));
            const QString num = QLocale().toString(val, 'f', 2);
            return QString("%1%2 %3").arg(withPlus ? QStringLiteral("+") : QString(), num, BitcoinUnits::name(unit));
        };
        auto fmtExact = [&](qint64 satoshis, bool withPlus) -> QString {
            return QString("%1%2 %3")
                .arg(withPlus ? QStringLiteral("+") : QString(),
                     BitcoinUnits::format(unit, satoshis, false, BitcoinUnits::separatorAlways),
                     BitcoinUnits::name(unit));
        };

        if (lblEarningsPeriodBadge) lblEarningsPeriodBadge->setText(use7d ? tr("Last 7d") : tr("All Time"));
        if (lblEarn24hVal) {
            lblEarn24hVal->setText(fmtCompact(earn24h, true));
            lblEarn24hVal->setToolTip(fmtExact(earn24h, true));
        }
        if (lblEarn7dVal) {
            lblEarn7dVal->setText(fmtCompact(earn7d, true));
            lblEarn7dVal->setToolTip(fmtExact(earn7d, true));
        }

        if (earningsSplitBar) {
            const QColor trackBg = light ? QColor("#E2E8F0") : QColor("#1E293B");
            earningsSplitBar->setSegments({
                {static_cast<double>(showPos),     QColor("#F5897C")},
                {static_cast<double>(showMn),      QColor("#7695CD")},
                {static_cast<double>(showAdamPow), QColor("#8B7FD6")}
            }, trackBg);
        }

        auto setSplitLabel = [&](QLabel* lbl, qint64 part) {
            if (!lbl) return;
            const double pct = showTotal > 0 ? (100.0 * static_cast<double>(part) / static_cast<double>(showTotal)) : 0.0;
            const QString pctStr = QString::number(pct, 'f', 1);
            lbl->setText(QString("%1 (%2%)").arg(fmtCompact(part, false), pctStr));
            lbl->setToolTip(QString("%1 (%2%)").arg(fmtExact(part, false), pctStr));
        };
        setSplitLabel(lblSplitPosVal, showPos);
        setSplitLabel(lblSplitMnVal, showMn);
        setSplitLabel(lblSplitAdamVal, showAdamPow);
    } else if (walletModel) {
        scheduleOverviewRefresh();
    }

    // 3. Node & Consensus Status
    int height = 0;
    int peers = 0;
    qint64 lastBlockAgeSec = -1;
    if (clientModel) {
        height = clientModel->getNumBlocks();
        peers = clientModel->getNumConnections();
        const QDateTime dt = clientModel->getLastBlockDate();
        if (dt.isValid()) {
            lastBlockAgeSec = std::max<qint64>(0, dt.secsTo(QDateTime::currentDateTimeUtc()));
        }
    }

    bool csMainBusy = false;
    dip3::CDeterministicMNList dip3List;
    bool hasDip3List = false;
    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) {
            csMainBusy = true;
        } else {
            if (deterministicMNManager) {
                dip3List = deterministicMNManager->GetListAtChainTip();
                hasDip3List = true;
            }
            if (chainActive.Tip()) {
                height = std::max(height, chainActive.Height());
                std::set<CPubKey> mnKeys, clKeys, fundedKeys, bootKeys;
                const auto poolKeys = GetAdamMinerPool(height, nullptr, &mnKeys, &clKeys, nullptr, nullptr, &fundedKeys, &bootKeys);
                cachedPoolTotal = poolKeys.size();
                cachedMnPoolCount = mnKeys.size();
                cachedClPoolCount = clKeys.size();
                cachedPowPoolCount = fundedKeys.size() + bootKeys.size();

                int mnEnabled = 0;
                if (hasDip3List) {
                    for (const auto& kv : dip3List.mapMNs) {
                        if (kv.second.state.IsValid()) ++mnEnabled;
                    }
                }
                if (mnEnabled == 0) mnEnabled = mnodeman.CountEnabled();
                cachedMnEnabled = mnEnabled;
                hasCachedNodeSnapshot = true;
            }
        }
    }

    bool myMnBusy = csMainBusy;
    if (!csMainBusy) {
        const bool mnSynced = masternodeSync.IsSynced();
        std::set<COutPoint> seenDip3Collaterals;
        int myTotal = 0;
        int myActive = 0;
        int myBanned = 0;
        int myPenalized = 0;
        bool myHasDip3 = false;

        if (pwalletMain) {
            TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
            if (!lockWallet) {
                myMnBusy = true;
            } else {
                for (const CMasternodeWalletRecord& rec : pwalletMain->GetAllMasternodeMeta()) {
                    if (rec.collateralOutpoint.IsNull()) continue;
                    ++myTotal;
                    const CTxIn txIn(rec.collateralOutpoint);
                    if (hasDip3List) {
                        if (const dip3::CDeterministicMN* dip3Mn = dip3List.GetMNByCollateral(rec.collateralOutpoint)) {
                            seenDip3Collaterals.insert(rec.collateralOutpoint);
                            myHasDip3 = true;
                            if (dip3Mn->state.IsBanned()) {
                                ++myBanned;
                            } else {
                                if (dip3Mn->state.IsValid()) ++myActive;
                                if (dip3Mn->state.nPoSePenalty > 0) ++myPenalized;
                            }
                            continue;
                        }
                    }
                    if (!mnSynced) continue;
                    CMasternode mn;
                    if (mnodeman.Get(txIn, mn)) {
                        if (mn.activeState == CMasternode::MASTERNODE_PRE_ENABLED ||
                            mn.activeState == CMasternode::MASTERNODE_ENABLED) {
                            ++myActive;
                        } else if (mn.activeState == CMasternode::MASTERNODE_POSE_BAN) {
                            ++myBanned;
                        }
                    }
                }

                if (hasDip3List && !dip3List.mapMNs.empty()) {
                    for (const auto& kv : dip3List.mapMNs) {
                        const dip3::CDeterministicMN& dmn = kv.second;
                        if (seenDip3Collaterals.count(dmn.collateralOutpoint)) continue;
                        const CWalletTx* colWtx = pwalletMain->GetWalletTx(dmn.collateralOutpoint.hash);
                        const bool hasColOut = (colWtx && dmn.collateralOutpoint.n < colWtx->vout.size() &&
                                                pwalletMain->IsMine(colWtx->vout[dmn.collateralOutpoint.n]) == ISMINE_SPENDABLE);
                        const bool isMine = pwalletMain->HaveKey(dmn.state.pubKeyOwner.GetID())
                                         || pwalletMain->HaveKey(dmn.state.keyIDVoting)
                                         || hasColOut
                                         || (::IsMine(*pwalletMain, dmn.state.scriptPayout) == ISMINE_SPENDABLE);
                        if (!isMine) continue;
                        myHasDip3 = true;
                        ++myTotal;
                        if (dmn.state.IsBanned()) {
                            ++myBanned;
                        } else {
                            if (dmn.state.IsValid()) ++myActive;
                            if (dmn.state.nPoSePenalty > 0) ++myPenalized;
                        }
                    }
                }
            }
        }

        if (!myMnBusy && (mnSynced || myHasDip3 || myTotal == 0)) {
            cachedMyMnTotal = myTotal;
            cachedMyMnActive = myActive;
            cachedMyMnBanned = myBanned;
            cachedMyMnPenalized = myPenalized;
            cachedMyMnHasDip3 = myHasDip3;
            hasCachedMyMnSnapshot = true;
        }
    }
    if (myMnBusy) {
        scheduleOverviewRefresh();
    }

    const QLocale loc;
    if (lblNodeSyncPill) {
        lblNodeSyncPill->setText(isSync ? tr("Synced") : tr("Syncing"));
    }

    if (lblNodeHeightVal) {
        const QString hStr = loc.toString(height);
        lblNodeHeightVal->setText(lastBlockAgeSec >= 0
            ? tr("#%1 (%2 ago)").arg(hStr, formatAgeCompact(lastBlockAgeSec))
            : tr("#%1").arg(hStr));
    }
    if (lblNodePeersVal) {
        lblNodePeersVal->setText(tr("%1 connected").arg(loc.toString(peers)));
    }
    if (lblNodeMnsVal) {
        if (hasCachedNodeSnapshot) {
            const CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(height);
            lblNodeMnsVal->setText(tr("%1 enabled (%2)")
                .arg(loc.toString(cachedMnEnabled))
                .arg(BitcoinUnits::floorWithUnit(nDisplayUnit, colAmt, false, BitcoinUnits::separatorAlways, true)));
        } else {
            lblNodeMnsVal->setText(QStringLiteral("--"));
        }
    }
    if (lblNodePoolVal) {
        lblNodePoolVal->setText(hasCachedNodeSnapshot
            ? tr("%1 (%2 MN • %3 CL • %4 PoW)")
                .arg(loc.toString(static_cast<qulonglong>(cachedPoolTotal)))
                .arg(loc.toString(static_cast<qulonglong>(cachedMnPoolCount)))
                .arg(loc.toString(static_cast<qulonglong>(cachedClPoolCount)))
                .arg(loc.toString(static_cast<qulonglong>(cachedPowPoolCount)))
            : QStringLiteral("--"));
    }
    if (lblNodeStakingVal) {
        lblNodeStakingVal->setText(stakingActive ? tr("Active (Staking)") : tr("Not Active"));
    }

    // 4. My Masternodes Summary Card
    const bool mnReady = hasCachedMyMnSnapshot || isSync || masternodeSync.IsSynced();
    if (lblMnCardPill) {
        if (!hasCachedMyMnSnapshot && !isSync) {
            lblMnCardPill->setText(tr("Syncing"));
        } else if (cachedMyMnTotal == 0) {
            lblMnCardPill->setText(tr("0 Configured"));
        } else {
            lblMnCardPill->setText(tr("%1 / %2 Active")
                .arg(loc.toString(cachedMyMnActive), loc.toString(cachedMyMnTotal)));
        }
    }
    if (lblMnCountVal) {
        if (cachedMyMnTotal > 0) {
            lblMnCountVal->setText(tr("%1 / %2 active")
                .arg(loc.toString(cachedMyMnActive), loc.toString(cachedMyMnTotal)));
            lblMnCountVal->setToolTip(tr("Your Wallet Masternodes:\n• Active: %1\n• Total: %2\n• PoSe Penalized: %3\n• PoSe Banned: %4")
                .arg(loc.toString(cachedMyMnActive), loc.toString(cachedMyMnTotal),
                     loc.toString(cachedMyMnPenalized), loc.toString(cachedMyMnBanned)));
        } else {
            lblMnCountVal->setText(mnReady ? tr("0 / 0") : tr("-- / 0"));
            lblMnCountVal->setToolTip(tr("No wallet-owned masternodes detected"));
        }
    }
    const bool showPoseRow = (cachedMyMnTotal > 0);
    if (lblMnPoseKey) lblMnPoseKey->setVisible(showPoseRow);
    if (lblMnPoseVal) {
        lblMnPoseVal->setVisible(showPoseRow);
        if (cachedMyMnBanned > 0 && cachedMyMnPenalized > 0) {
            lblMnPoseVal->setText(tr("%1 banned • %2 pen.")
                .arg(loc.toString(cachedMyMnBanned), loc.toString(cachedMyMnPenalized)));
        } else if (cachedMyMnBanned > 0) {
            lblMnPoseVal->setText(tr("%1 banned").arg(loc.toString(cachedMyMnBanned)));
        } else if (cachedMyMnPenalized > 0) {
            lblMnPoseVal->setText(tr("%1 penalized").arg(loc.toString(cachedMyMnPenalized)));
        } else if (cachedMyMnActive < cachedMyMnTotal) {
            lblMnPoseVal->setText(tr("%1 offline").arg(loc.toString(cachedMyMnTotal - cachedMyMnActive)));
        } else {
            lblMnPoseVal->setText(tr("All healthy"));
        }
    }
    if (lblMnColVal) {
        if (height > 0) {
            const CAmount curCol = CMasternode::GetMasternodeNodeCollateral(height);
            lblMnColVal->setText(GUIUtil::formatBalance(curCol, nDisplayUnit));
        } else {
            lblMnColVal->setText(QStringLiteral("--"));
        }
    }
    const std::pair<int, CAmount> nextCol = (height > 0)
        ? CMasternode::GetNextMasternodeCollateral(height)
        : std::make_pair(-1, CAmount(-1));
    const bool showNextCol = (nextCol.first > 0);
    if (lblMnNextColKey)    lblMnNextColKey->setVisible(showNextCol);
    if (lblMnNextColVal)    lblMnNextColVal->setVisible(showNextCol);
    if (lblMnNextBlocksKey) lblMnNextBlocksKey->setVisible(showNextCol);
    if (lblMnNextBlocksVal) lblMnNextBlocksVal->setVisible(showNextCol);
    if (showNextCol) {
        if (lblMnNextColVal) {
            lblMnNextColVal->setText(GUIUtil::formatBalance(nextCol.second, nDisplayUnit));
        }
        if (lblMnNextBlocksVal) {
            const QString blkWord = (nextCol.first == 1) ? tr("block") : tr("blocks");
            lblMnNextBlocksVal->setText(tr("%1 %2 (%3)")
                .arg(loc.toString(nextCol.first), blkWord,
                     formatBlocksEtaCompact(nextCol.first, Params().GetConsensus().nTargetSpacing)));
        }
    }

    applyOverviewThemeColors(light);
}

void DashboardWidget::applyOverviewThemeColors(bool isLight)
{
    if (lblKpiImmatureSub) {
        lblKpiImmatureSub->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; }")
            .arg(lastStakingActive ? (isLight ? "#466399" : "#93B4F0") : (isLight ? "#64748B" : "#94A3B8")));
    }
    if (earningsSplitBar) {
        earningsSplitBar->setTrackColor(isLight ? QColor("#E2E8F0") : QColor("#1E293B"));
    }
    if (lblNodeSyncPill) {
        const QString fg  = isSync ? (isLight ? "#3E5B94" : "#93B4F0") : (isLight ? "#B5463A" : "#F5897C");
        const QString bg  = isSync ? (isLight ? "#EEF3FA" : "rgba(118, 149, 205, 0.16)") : "rgba(245, 137, 124, 0.14)";
        const QString brd = isSync ? (isLight ? "#B8CBE8" : "rgba(118, 149, 205, 0.42)") : "rgba(245, 137, 124, 0.42)";
        lblNodeSyncPill->setStyleSheet(QString(
            "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(fg, bg, brd));
    }
    if (lblMnCardPill) {
        QString fg, bg, brd;
        const bool allHealthy = (cachedMyMnTotal > 0 && cachedMyMnActive == cachedMyMnTotal &&
                                 cachedMyMnBanned == 0 && cachedMyMnPenalized == 0);
        const bool hasWarn = (!hasCachedMyMnSnapshot && !isSync) ||
                             (cachedMyMnTotal > 0 && !allHealthy);
        if (allHealthy) {
            fg  = isLight ? "#3E5B94" : "#93B4F0";
            bg  = isLight ? "#EEF3FA" : "rgba(118, 149, 205, 0.16)";
            brd = isLight ? "#B8CBE8" : "rgba(118, 149, 205, 0.42)";
        } else if (hasWarn) {
            fg  = isLight ? "#B5463A" : "#F5897C";
            bg  = "rgba(245, 137, 124, 0.14)";
            brd = "rgba(245, 137, 124, 0.42)";
        } else {
            fg  = isLight ? "#475569" : "#CBD5E1";
            bg  = isLight ? "#F1F5F9" : "#1C2942";
            brd = isLight ? "#CBD5E1" : "#33486E";
        }
        lblMnCardPill->setStyleSheet(QString(
            "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
        ).arg(fg, bg, brd));
    }
    if (lblMnPoseVal) {
        const bool healthy = (cachedMyMnTotal > 0 && cachedMyMnActive == cachedMyMnTotal &&
                              cachedMyMnBanned == 0 && cachedMyMnPenalized == 0);
        lblMnPoseVal->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }")
            .arg(healthy ? (isLight ? "#466399" : "#93B4F0") : (isLight ? "#B5463A" : "#F5897C")));
    }
    if (lblNodeStakingVal) {
        lblNodeStakingVal->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; }")
            .arg(lastStakingActive ? (isLight ? "#466399" : "#93B4F0") : (isLight ? "#475569" : "#CBD5E1")));
    }
}

void DashboardWidget::refreshLiveNodeStatus()
{
    if (!isVisible()) return;

    if (clientModel && lblNodeHeightVal) {
        const int height = clientModel->getNumBlocks();
        qint64 lastBlockAgeSec = -1;
        const QDateTime dt = clientModel->getLastBlockDate();
        if (dt.isValid()) {
            lastBlockAgeSec = std::max<qint64>(0, dt.secsTo(QDateTime::currentDateTimeUtc()));
        }
        const QString hStr = QLocale().toString(height);
        lblNodeHeightVal->setText(lastBlockAgeSec >= 0
            ? tr("#%1 (%2 ago)").arg(hStr, formatAgeCompact(lastBlockAgeSec))
            : tr("#%1").arg(hStr));
    }

    const bool stakingActive = walletModel && !walletModel->isWalletLocked() && walletModel->isStakingStatusActive();
    if (stakingActive != lastStakingActive || (lblNodeStakingVal && lblNodeStakingVal->text().isEmpty())) {
        lastStakingActive = stakingActive;
        if (lblKpiImmatureSub) {
            lblKpiImmatureSub->setText(stakingActive
                ? QString::fromUtf8("\xE2\x97\x8F ") + tr("PoS Staking Active")
                : QString::fromUtf8("\xE2\x97\x8B ") + tr("PoS Staking Inactive"));
        }
        if (lblNodeStakingVal) {
            lblNodeStakingVal->setText(stakingActive ? tr("Active (Staking)") : tr("Not Active"));
        }
        applyOverviewThemeColors(isLightTheme());
    }
}

void DashboardWidget::handleTransactionClicked(const QModelIndex &index)
{
    if (!filter || !index.isValid()) return;
    listTransactions->setCurrentIndex(index);
    QModelIndex rIndex = filter->mapToSource(index);

    window->showHide(true);
    TxDetailDialog *dialog = new TxDetailDialog(window, false);
    dialog->setData(walletModel, rIndex);
    openDialogWithOpaqueBackgroundY(dialog, window, 3, 17);

    listTransactions->scrollTo(index);
    listTransactions->clearSelection();
    listTransactions->setFocus();
    dialog->deleteLater();
}

void DashboardWidget::onTxArrived(const QString&, const bool&)
{
    showList();
    if (minerHubWidget) {
        minerHubWidget->refreshMinerHubStatus();
    }
    if (filter && filter->rowCount() > 0) {
        if (listTransactions->verticalScrollBar()->value() == 0) {
            listTransactions->scrollTo(filter->index(0, 0));
        }
        listTransactions->update();
    }
    scheduleOverviewRefresh();
}

void DashboardWidget::showList()
{
    const bool hasAnyWalletTx = (txModel && txModel->size() > 0);
    const int shownRows = filter ? filter->rowCount() : 0;

    if (lblTxCount) {
        const bool truncated = (txModel && txModel->isTruncatedLoad());
        lblTxCount->setText(truncated
            ? tr("%1 shown (recent)").arg(QLocale().toString(shownRows))
            : tr("%1 shown").arg(QLocale().toString(shownRows)));
        lblTxCount->setToolTip(truncated
            ? tr("Only the most recent wallet transactions are loaded in this table for fast startup. Full all-time totals appear in Stats / Export CSV.")
            : QString());
    }

    if (!filter || shownRows == 0) {
        if (emptyContainer) emptyContainer->setVisible(true);
        if (listTransactions) listTransactions->setVisible(false);
        if (lblEmpty) lblEmpty->setText(hasAnyWalletTx ? tr("No matching transactions found") : tr("No transactions yet"));
    } else {
        if (emptyContainer) emptyContainer->setVisible(false);
        if (listTransactions) listTransactions->setVisible(true);
    }
}

void DashboardWidget::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        txHolder->setDisplayUnit(nDisplayUnit);
        listTransactions->update();
        refreshOverviewStats();
    }
}

void DashboardWidget::onSortChanged(const QString& value)
{
    if (!filter || !comboBoxSort) return;
    changeSort(!value.isNull() ? comboBoxSort->currentIndex() : SortTx::DATE_DESC);
}

void DashboardWidget::changeSort(int nSortIndex)
{
    int nColumnIndex = TransactionTableModel::Date;
    Qt::SortOrder order = Qt::DescendingOrder;

    switch (nSortIndex) {
        case SortTx::DATE_DESC:
            nColumnIndex = TransactionTableModel::Date;
            break;
        case SortTx::DATE_ASC:
            nColumnIndex = TransactionTableModel::Date;
            order = Qt::AscendingOrder;
            break;
        case SortTx::AMOUNT_DESC:
            nColumnIndex = TransactionTableModel::Amount;
            break;
        case SortTx::AMOUNT_ASC:
            nColumnIndex = TransactionTableModel::Amount;
            order = Qt::AscendingOrder;
            break;
    }

    if (comboBoxSort) {
        const QSignalBlocker blocker(comboBoxSort);
        comboBoxSort->setCurrentIndex(nSortIndex);
    }
    if (filter) filter->sort(nColumnIndex, order);
    if (listTransactions) listTransactions->update();
}

void DashboardWidget::onSortTypeChanged(const QString&)
{
    if (!filter || !comboBoxSortType) return;
    const int filterIndex = comboBoxSortType->currentIndex();
    const quint32 filterByType = comboBoxSortType->itemData(filterIndex).toUInt();

    filter->setTypeFilter(filterByType);

    const quint32 recvMask = TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) | TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther);
    const quint32 sentMask = TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) | TransactionFilterProxy::TYPE(TransactionRecord::SendToOther);
    const quint32 rewMask = rewardsFilterMask();

    if (filterByType == TransactionFilterProxy::ALL_TYPES) quickTxFilterMode = 0;
    else if (filterByType == rewMask) quickTxFilterMode = 1;
    else if (filterByType == recvMask) quickTxFilterMode = 2;
    else if (filterByType == sentMask) quickTxFilterMode = 3;
    else quickTxFilterMode = -1;

    if (btnTxFilterAll)      btnTxFilterAll->setChecked(quickTxFilterMode == 0);
    if (btnTxFilterRewards)  btnTxFilterRewards->setChecked(quickTxFilterMode == 1);
    if (btnTxFilterReceived) btnTxFilterReceived->setChecked(quickTxFilterMode == 2);
    if (btnTxFilterSent)     btnTxFilterSent->setChecked(quickTxFilterMode == 3);

    listTransactions->update();
    showList();
}

void DashboardWidget::onSearchTxChanged(const QString& query)
{
    if (!filter) return;
    filter->setAddressPrefix(query.trimmed());
    listTransactions->update();
    showList();
}

void DashboardWidget::walletSynced(bool sync)
{
    if (this->isSync != sync) {
        this->isSync = sync;
        if (warningBanner) warningBanner->setVisible(!this->isSync);
        scheduleOverviewRefresh();
    }
}

void DashboardWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    if (overviewDirty) {
        refreshOverviewStats();
    }
}

void DashboardWidget::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    if (!txModel || txModel->processingQueuedTransactions() || txModel->isSuppressingNotifications())
        return;

    QDateTime txDate = txModel->index(start, TransactionTableModel::Date, parent).data(TransactionTableModel::DateRole).toDateTime();
    if (txDate.isValid() && txDate.secsTo(QDateTime::currentDateTime()) > 15 * 60)
        return;

    QString date = txModel->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = txModel->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toLongLong();
    QString type = txModel->index(start, TransactionTableModel::Type, parent).data().toString();
    QString address = txModel->index(start, TransactionTableModel::ToAddress, parent).data().toString();

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address);
}

void DashboardWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    static_cast<TxViewHolder*>(this->txViewDelegate->getRowFactory())->isLightTheme = isLightTheme;
    applyTheme(isLightTheme);
}

void DashboardWidget::registerRightCardDragTarget(QFrame* card)
{
    if (!card) return;
    card->installEventFilter(this);
    const auto children = card->findChildren<QWidget*>();
    for (QWidget* w : children) {
        if (qobject_cast<QAbstractButton*>(w)) continue;
        bool underButton = false;
        for (QWidget* p = w->parentWidget(); p && p != card; p = p->parentWidget()) {
            if (qobject_cast<QAbstractButton*>(p)) {
                underButton = true;
                break;
            }
        }
        if (!underButton) {
            w->installEventFilter(this);
        }
    }
}

QFrame* DashboardWidget::findRightCardForObject(QObject* obj) const
{
    QWidget* w = qobject_cast<QWidget*>(obj);
    while (w) {
        if (qobject_cast<QAbstractButton*>(w)) return nullptr;
        for (QFrame* card : rightCards) {
            if (w == card) return card;
        }
        if (w == rightCol || w == this) break;
        w = w->parentWidget();
    }
    return nullptr;
}

QStringList DashboardWidget::currentRightCardOrder() const
{
    QStringList ids;
    ids.reserve(rightCards.size());
    for (QFrame* c : rightCards) {
        if (c) ids.append(c->property("homeCardId").toString());
    }
    return ids;
}

void DashboardWidget::applyRightCardOrder(const QStringList& order)
{
    if (!rightLayout) return;
    const QVector<QFrame*> defaults = {earningsCard, nodeCard, masternodesCard};
    QVector<QFrame*> nextOrder;
    nextOrder.reserve(defaults.size());

    for (const QString& id : order) {
        for (QFrame* c : defaults) {
            if (c && c->property("homeCardId").toString() == id && !nextOrder.contains(c)) {
                nextOrder.append(c);
                break;
            }
        }
    }
    for (QFrame* c : defaults) {
        if (c && !nextOrder.contains(c)) {
            nextOrder.append(c);
        }
    }

    while (QLayoutItem* item = rightLayout->takeAt(0)) {
        delete item;
    }
    for (QFrame* c : nextOrder) {
        rightLayout->addWidget(c);
    }
    rightLayout->addStretch(1);
    rightCards = nextOrder;
    rightLayout->activate();

    if (dragSlotMask && draggingCard) {
        dragSlotMask->setGeometry(draggingCard->rect());
        dragSlotMask->raise();
    }
}

void DashboardWidget::saveRightCardOrder() const
{
    const QStringList ids = currentRightCardOrder();
    if (!ids.isEmpty()) {
        getSettings()->setValue(QStringLiteral("homeRightCardOrder"), ids);
    }
}

void DashboardWidget::beginCardDrag()
{
    if (dragActive || !draggingCard || !rightCol) return;

    const QPixmap snap = draggingCard->grab();
    dragActive = true;
    dragCardStartY = draggingCard->y();

    for (HomeCardDragGrip* grip : rightCardGrips) {
        if (grip) grip->setActive(grip->parentWidget() == draggingCard);
    }

    const bool light = isLightTheme();
    dragSlotMask = new HomeDragSlotMask(
        light ? QColor(244, 248, 254, 218) : QColor(15, 23, 42, 215),
        QColor("#7695CD"),
        draggingCard);
    dragSlotMask->setGeometry(draggingCard->rect());
    dragSlotMask->show();
    dragSlotMask->raise();

    dragGhost = new HomeDragGhostWidget(snap, QColor("#7695CD"), rightCol);
    dragGhost->setGeometry(draggingCard->x(), dragCardStartY, draggingCard->width(), draggingCard->height());
    dragGhost->show();
    dragGhost->raise();

    QApplication::setOverrideCursor(Qt::ClosedHandCursor);
    if (qApp) qApp->installEventFilter(this);
}

void DashboardWidget::updateCardDrag(int globalY)
{
    if (!dragActive || !draggingCard || !rightCol) return;

    if (dragGhost) {
        const int maxY = std::max(0, rightCol->height() - draggingCard->height());
        const int ghostY = std::max(0, std::min(maxY, dragCardStartY + (globalY - dragPressGlobalPos.y())));
        dragGhost->move(draggingCard->x(), ghostY);
        dragGhost->raise();
    }

    const int localY = rightCol->mapFromGlobal(QPoint(dragPressGlobalPos.x(), globalY)).y();
    for (int step = 0; step < rightCards.size(); ++step) {
        const int idx = rightCards.indexOf(draggingCard);
        if (idx < 0) break;

        int targetIdx = idx;
        if (idx > 0 && rightCards[idx - 1] && localY < rightCards[idx - 1]->geometry().center().y()) {
            targetIdx = idx - 1;
        } else if (idx + 1 < rightCards.size() && rightCards[idx + 1] && localY > rightCards[idx + 1]->geometry().center().y()) {
            targetIdx = idx + 1;
        }
        if (targetIdx == idx) break;

        QStringList nextIds = currentRightCardOrder();
        nextIds.move(idx, targetIdx);
        applyRightCardOrder(nextIds);
        dragOrderChanged = true;
    }

    if (dragGhost) {
        dragGhost->raise();
    }
}

void DashboardWidget::finishCardDrag(bool cancel)
{
    if (qApp) qApp->removeEventFilter(this);
    if (dragActive) {
        QApplication::restoreOverrideCursor();
    }

    if (dragGhost) {
        dragGhost->hide();
        dragGhost->deleteLater();
        dragGhost = nullptr;
    }
    if (dragSlotMask) {
        dragSlotMask->hide();
        dragSlotMask->deleteLater();
        dragSlotMask = nullptr;
    }

    for (HomeCardDragGrip* grip : rightCardGrips) {
        if (grip) grip->setActive(false);
    }

    if (cancel && dragOrderChanged && !dragOriginalOrder.isEmpty()) {
        applyRightCardOrder(dragOriginalOrder);
    } else if (!cancel && dragOrderChanged) {
        saveRightCardOrder();
    }

    draggingCard = nullptr;
    dragActive = false;
    dragOrderChanged = false;
    dragOriginalOrder.clear();
}

bool DashboardWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (!event) return PWidget::eventFilter(obj, event);

    if (dragActive) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                finishCardDrag(true);
                return true;
            }
        } else if (event->type() == QEvent::ApplicationDeactivate || event->type() == QEvent::WindowDeactivate) {
            finishCardDrag(false);
            return false;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                finishCardDrag(false);
                return true;
            }
            return true;
        } else if (event->type() == QEvent::Resize && obj == draggingCard && dragSlotMask) {
            dragSlotMask->setGeometry(draggingCard->rect());
        }
    }

    QFrame* card = findRightCardForObject(obj);
    if (card) {
        switch (event->type()) {
            case QEvent::MouseButtonPress: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (me->button() == Qt::LeftButton && !dragActive) {
                    draggingCard = card;
                    dragPressGlobalPos = me->globalPos();
                    dragCardStartY = card->y();
                    dragOrderChanged = false;
                    dragOriginalOrder = currentRightCardOrder();
                }
                return false;
            }
            case QEvent::MouseMove: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (draggingCard == card) {
                    if (!(me->buttons() & Qt::LeftButton)) {
                        finishCardDrag(false);
                        return false;
                    }
                    if (!dragActive) {
                        if ((me->globalPos() - dragPressGlobalPos).manhattanLength() >= QApplication::startDragDistance()) {
                            beginCardDrag();
                        }
                    }
                    if (dragActive) {
                        updateCardDrag(me->globalPos().y());
                        return true;
                    }
                }
                return false;
            }
            case QEvent::MouseButtonRelease: {
                QMouseEvent* me = static_cast<QMouseEvent*>(event);
                if (draggingCard == card && me->button() == Qt::LeftButton) {
                    const bool wasDragging = dragActive;
                    finishCardDrag(false);
                    return wasDragging;
                }
                return false;
            }
            case QEvent::Hide: {
                if (draggingCard == card) {
                    finishCardDrag(true);
                }
                return false;
            }
            default:
                break;
        }
    }

    return PWidget::eventFilter(obj, event);
}

DashboardWidget::~DashboardWidget()
{
    if (qApp) qApp->removeEventFilter(this);
    if (dragActive) {
        QApplication::restoreOverrideCursor();
        dragActive = false;
    }
    shutdownMinerWorkers();
}
