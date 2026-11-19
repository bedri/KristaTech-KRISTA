// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/minerhubwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/sendconfirmdialog.h"
#include "qt/kristatech/qtutils.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "utiltime.h"
#include "util.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "adam.h"
#include "key_io.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "activemasternodeman.h"
#include "evo/deterministicmns.h"
#include "llmq.h"
#include "wallet/wallet.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <thread>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QListView>
#include <QPointer>
#include <QShowEvent>

namespace {
struct ScopedBoolFlag {
    bool& flag;
    bool active = true;
    explicit ScopedBoolFlag(bool& f) : flag(f) { flag = true; }
    void release()
    {
        if (active) {
            flag = false;
            active = false;
        }
    }
    ~ScopedBoolFlag() { release(); }
};

class MinerTableItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant a = data(Qt::UserRole + 10);
        const QVariant b = other.data(Qt::UserRole + 10);
        if (a.isValid() && b.isValid()) {
            return a.toLongLong() < b.toLongLong();
        }
        return QTableWidgetItem::operator<(other);
    }
};
} // namespace
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSettings>
#include <QToolTip>
#include <QVBoxLayout>

namespace {
enum class MinerHubGlyph {
    Refresh,
    Unlock,
    Clock,
    ServerNode,
    LockClosed,
    CpuBolt,
    ZeroSpark
};

QPixmap MakeMinerHubVectorPixmap(MinerHubGlyph glyph, const QColor& color, int logicalSize = 14)
{
    const qreal dpr = 2.0;
    const int pxSize = static_cast<int>(logicalSize * dpr);
    QPixmap px(pxSize, pxSize);
    px.fill(Qt::transparent);
    px.setDevicePixelRatio(dpr);

    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (glyph == MinerHubGlyph::Refresh) {
        const QRectF r(2.4, 2.4, logicalSize - 4.8, logicalSize - 4.8);
        p.drawArc(r, 35 * 16, 130 * 16);
        p.drawArc(r, 215 * 16, 130 * 16);

        QPolygonF topArrow;
        topArrow << QPointF(logicalSize - 5.2, 4.9)
                 << QPointF(logicalSize - 2.6, 5.1)
                 << QPointF(logicalSize - 2.8, 2.5);
        p.drawPolyline(topArrow);

        QPolygonF botArrow;
        botArrow << QPointF(5.2, logicalSize - 4.9)
                 << QPointF(2.6, logicalSize - 5.1)
                 << QPointF(2.8, logicalSize - 2.5);
        p.drawPolyline(botArrow);
    } else if (glyph == MinerHubGlyph::Unlock) {
        p.drawRoundedRect(QRectF(3.0, 6.8, logicalSize - 6.0, 5.4), 1.4, 1.4);
        QPainterPath shackle;
        shackle.moveTo(4.8, 6.8);
        shackle.lineTo(4.8, 4.7);
        shackle.arcTo(QRectF(4.8, 2.2, logicalSize - 9.6, 4.4), 180.0, -145.0);
        p.drawPath(shackle);
    } else if (glyph == MinerHubGlyph::LockClosed) {
        p.drawRoundedRect(QRectF(3.0, 6.5, logicalSize - 6.0, 5.6), 1.5, 1.5);
        QPainterPath shackle;
        shackle.moveTo(4.8, 6.5);
        shackle.lineTo(4.8, 4.6);
        shackle.arcTo(QRectF(4.8, 2.1, logicalSize - 9.6, 4.6), 180.0, -180.0);
        shackle.lineTo(logicalSize - 4.8, 6.5);
        p.drawPath(shackle);
    } else if (glyph == MinerHubGlyph::ServerNode) {
        p.drawRoundedRect(QRectF(2.2, 2.4, logicalSize - 4.4, 4.0), 1.1, 1.1);
        p.drawRoundedRect(QRectF(2.2, logicalSize - 6.4, logicalSize - 4.4, 4.0), 1.1, 1.1);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(4.4, 4.4), 0.9, 0.9);
        p.drawEllipse(QPointF(4.4, logicalSize - 4.4), 0.9, 0.9);
    } else if (glyph == MinerHubGlyph::CpuBolt) {
        QPainterPath bolt;
        bolt.moveTo(logicalSize * 0.56, 1.8);
        bolt.lineTo(logicalSize * 0.24, logicalSize * 0.54);
        bolt.lineTo(logicalSize * 0.50, logicalSize * 0.54);
        bolt.lineTo(logicalSize * 0.42, logicalSize - 1.8);
        bolt.lineTo(logicalSize * 0.78, logicalSize * 0.46);
        bolt.lineTo(logicalSize * 0.52, logicalSize * 0.46);
        bolt.closeSubpath();
        p.drawPath(bolt);
    } else if (glyph == MinerHubGlyph::ZeroSpark) {
        const qreal c = logicalSize / 2.0;
        p.drawEllipse(QRectF(2.6, 2.6, logicalSize - 5.2, logicalSize - 5.2));
        p.drawLine(QPointF(c, 4.2), QPointF(c, logicalSize - 4.2));
        p.drawLine(QPointF(4.2, c), QPointF(logicalSize - 4.2, c));
    } else {
        const QRectF c(2.0, 2.0, logicalSize - 4.0, logicalSize - 4.0);
        p.drawEllipse(c);
        const qreal mid = logicalSize / 2.0;
        p.drawLine(QPointF(mid, mid - 2.9), QPointF(mid, mid));
        p.drawLine(QPointF(mid, mid), QPointF(mid + 2.2, mid + 1.4));
    }
    return px;
}

QIcon MakeMinerHubButtonIcon(MinerHubGlyph glyph, const QColor& activeColor, const QColor& disabledColor)
{
    QIcon icon;
    icon.addPixmap(MakeMinerHubVectorPixmap(glyph, activeColor, 14), QIcon::Normal, QIcon::Off);
    icon.addPixmap(MakeMinerHubVectorPixmap(glyph, activeColor, 14), QIcon::Active, QIcon::Off);
    icon.addPixmap(MakeMinerHubVectorPixmap(glyph, disabledColor, 14), QIcon::Disabled, QIcon::Off);
    return icon;
}
} // namespace

#define DECORATION_SIZE 65
#define NUM_ITEMS 3
#define REQUEST_LOAD_TASK 1


MinerHubWidget::MinerHubWidget(KRISTATECHGUI* _window, QWidget* parent) :
    PWidget(_window, parent)
{
    setObjectName("minerHubPage");
    setCssProperty(this, "container");
    initMinerHub();
}

MinerHubWidget::~MinerHubWidget()
{
    shutdownMinerWorkers();
}

void MinerHubWidget::loadWalletModel()
{
    refreshMinerHubStatus();
}

void MinerHubWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(isLightTheme);
    setStyleSheet(theme);
    updateStyle(this);
    updateMinerHubTheme();
    refreshMinerHubStatus();
}

void MinerHubWidget::initMinerHub()
{
    QVBoxLayout* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(22, 16, 22, 18);
    pageLayout->setSpacing(0);

    minerHubCard = new QFrame(this);
    minerHubCard->setObjectName("minerHubCard");
    minerHubCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* cardLayout = new QVBoxLayout(minerHubCard);
    cardLayout->setContentsMargins(20, 16, 20, 16);
    cardLayout->setSpacing(10);
    pageLayout->addWidget(minerHubCard, 1);

    // Header Row: Title + Subtitle + Live Status Badge + CPU Controls + Full Screen
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(12);
    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);
    lblMinerHubTitle = new QLabel(tr("Miner & Validator Hub"), minerHubCard);
    QFont titleFont = lblMinerHubTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(14);
    lblMinerHubTitle->setFont(titleFont);

    lblMinerHubSubtitle = new QLabel(tr("Dual-Track MPA (7 MN • 1 Coin-Lock • 3 Open PoW) & Coordinator VRF (65 : 8 : 2)"), minerHubCard);
    titleCol->addWidget(lblMinerHubTitle);
    titleCol->addWidget(lblMinerHubSubtitle);

    lblMinerStatusBadge = new QLabel(minerHubCard);
    lblMinerStatusBadge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    lblMinerThreads = new QLabel(tr("CPU Cores:"), minerHubCard);

    minerThreadsStepperFrame = new QFrame(minerHubCard);
    minerThreadsStepperFrame->setObjectName("minerThreadsStepperFrame");
    minerThreadsStepperFrame->setFixedHeight(36);
    QHBoxLayout* stepperLayout = new QHBoxLayout(minerThreadsStepperFrame);
    stepperLayout->setContentsMargins(0, 0, 0, 0);
    stepperLayout->setSpacing(0);

    btnMinerThreadsDec = new QPushButton(QString::fromUtf8("\xE2\x88\x92"), minerThreadsStepperFrame);
    btnMinerThreadsDec->setFixedSize(32, 34);
    btnMinerThreadsDec->setCursor(Qt::PointingHandCursor);
    btnMinerThreadsDec->setFocusPolicy(Qt::NoFocus);
    btnMinerThreadsDec->setAutoRepeat(true);
    btnMinerThreadsDec->setAutoRepeatDelay(350);
    btnMinerThreadsDec->setAutoRepeatInterval(80);
    btnMinerThreadsDec->setToolTip(tr("Decrease CPU mining threads"));

    spinMinerThreads = new QSpinBox(minerThreadsStepperFrame);
    int maxCores = std::max(1, (int)std::thread::hardware_concurrency());
    int defaultCores = (int)GetArg("-genproclimit", std::max(1, maxCores / 2));
    if (defaultCores <= 0) defaultCores = maxCores;
    spinMinerThreads->setButtonSymbols(QAbstractSpinBox::NoButtons);
    spinMinerThreads->setAlignment(Qt::AlignCenter);
    spinMinerThreads->setRange(1, maxCores);
    spinMinerThreads->setValue(std::min(defaultCores, maxCores));
    spinMinerThreads->setSuffix(tr(" threads"));
    spinMinerThreads->setFixedHeight(34);
    spinMinerThreads->setMinimumWidth(96);
    spinMinerThreads->setToolTip(tr("<qt>Number of CPU threads to use for cooperative ADAM puzzle solving and block coordination.<br/>Use − / + buttons, mouse wheel, or keyboard arrows.</qt>"));

    btnMinerThreadsInc = new QPushButton(QStringLiteral("+"), minerThreadsStepperFrame);
    btnMinerThreadsInc->setFixedSize(32, 34);
    btnMinerThreadsInc->setCursor(Qt::PointingHandCursor);
    btnMinerThreadsInc->setFocusPolicy(Qt::NoFocus);
    btnMinerThreadsInc->setAutoRepeat(true);
    btnMinerThreadsInc->setAutoRepeatDelay(350);
    btnMinerThreadsInc->setAutoRepeatInterval(80);
    btnMinerThreadsInc->setToolTip(tr("Increase CPU mining threads"));

    stepperLayout->addWidget(btnMinerThreadsDec);
    stepperLayout->addWidget(spinMinerThreads);
    stepperLayout->addWidget(btnMinerThreadsInc);

    btnToggleMining = new QPushButton(minerHubCard);
    btnToggleMining->setMinimumSize(190, 36);
    btnToggleMining->setCursor(Qt::PointingHandCursor);
    btnToggleMining->setFocusPolicy(Qt::NoFocus);

    btnFullScreenMinerHub = new QPushButton(tr("⛶ Full Screen"), minerHubCard);
    btnFullScreenMinerHub->setObjectName("btnFullScreenMinerHub");
    btnFullScreenMinerHub->setFixedHeight(36);
    btnFullScreenMinerHub->setCursor(Qt::PointingHandCursor);
    btnFullScreenMinerHub->setFocusPolicy(Qt::NoFocus);
    btnFullScreenMinerHub->setToolTip(tr("Expand Miner & Validator Hub to Full Screen (F11)"));

    lblAdamSolutionsLive = new QLabel(tr("ADAMSOL: —"), minerHubCard);
    lblAdamSolutionsLive->setObjectName("lblAdamSolutionsLive");
    lblAdamSolutionsLive->setAlignment(Qt::AlignCenter);

    headerRow->addLayout(titleCol, 1);
    headerRow->addWidget(lblAdamSolutionsLive);
    headerRow->addSpacing(6);
    headerRow->addWidget(lblMinerStatusBadge);
    headerRow->addSpacing(8);
    headerRow->addWidget(lblMinerThreads);
    headerRow->addWidget(minerThreadsStepperFrame);
    headerRow->addWidget(btnToggleMining);
    headerRow->addWidget(btnFullScreenMinerHub);
    cardLayout->addLayout(headerRow);

    // Quadratic Coordinator VRF (65 : 16 : 4) & Mature UTXO Readiness Strip
    coordVrfStripFrame = new QFrame(minerHubCard);
    coordVrfStripFrame->setObjectName("coordVrfStripFrame");
    QHBoxLayout* vrfLayout = new QHBoxLayout(coordVrfStripFrame);
    vrfLayout->setContentsMargins(14, 8, 14, 8);
    vrfLayout->setSpacing(14);
    lblCoordVrfSummary = new QLabel(tr("Coordinator VRF (65 : 8 : 2): Active MN = 65 tickets • Coin-Lock (250) = 8 tickets • PoW-Lock (10) = 2 tickets"), coordVrfStripFrame);
    lblCoordVrfSummary->setWordWrap(true);
    lblMatureUtxoSummary = new QLabel(tr("Mature Balance (100+ conf): —"), coordVrfStripFrame);
    lblMatureUtxoSummary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    vrfLayout->addWidget(lblCoordVrfSummary, 1);
    vrfLayout->addWidget(lblMatureUtxoSummary, 0);
    coordVrfStripFrame->setVisible(false);
    cardLayout->addWidget(coordVrfStripFrame);

    // Expiring / Expired Validator Lock Warning Banner + 1-Click Unlock & Renew
    lockExpiryBanner = new QFrame(minerHubCard);
    lockExpiryBanner->setObjectName("lockExpiryBanner");
    lockExpiryBanner->setVisible(false);
    QHBoxLayout* expiryLayout = new QHBoxLayout(lockExpiryBanner);
    expiryLayout->setContentsMargins(12, 8, 12, 8);
    expiryLayout->setSpacing(10);
    lblLockExpiryIcon = new QLabel(lockExpiryBanner);
    lblLockExpiryIcon->setFixedSize(16, 16);
    lblLockExpiryIcon->setAlignment(Qt::AlignCenter);
    lblLockExpiryWarning = new QLabel(lockExpiryBanner);
    lblLockExpiryWarning->setWordWrap(true);
    btnRenewLock = new QPushButton(tr("Renew Now"), lockExpiryBanner);
    btnRenewLock->setMinimumHeight(30);
    btnRenewLock->setIconSize(QSize(14, 14));
    btnRenewLock->setCursor(Qt::PointingHandCursor);
    btnRenewLock->setFocusPolicy(Qt::NoFocus);
    expiryLayout->addWidget(lblLockExpiryIcon, 0, Qt::AlignVCenter);
    expiryLayout->addWidget(lblLockExpiryWarning, 1);
    expiryLayout->addWidget(btnRenewLock);
    cardLayout->addWidget(lockExpiryBanner);

    // Row 2: 3 Side-by-Side Validator Track Registration Cards
    lblTrackSection = new QLabel(tr("VALIDATOR TRACK REGISTRATION (REFUNDABLE 2,880-BLOCK / ~24H CLTV SELF-LOCK — COINS NEVER LEAVE YOUR WALLET)"), minerHubCard);
    lblTrackSection->setVisible(false);
    cardLayout->addWidget(lblTrackSection);

    minerRegRowWidget = new QWidget(minerHubCard);
    minerRegRowWidget->setObjectName("minerRegRowWidget");
    minerRegRowWidget->setStyleSheet("QWidget#minerRegRowWidget { background-color: transparent; border: none; }");
    QHBoxLayout* regRow = new QHBoxLayout(minerRegRowWidget);
    regRow->setContentsMargins(0, 0, 0, 0);
    regRow->setSpacing(12);

    auto buildTrackCard = [&](QFrame*& cardOut,
                              const QString& objName,
                              const QString& titleStr,
                              const QString& specsStr,
                              QLabel*& metaOut,
                              QPushButton*& btnOut,
                              const QString& btnText,
                              const QString& btnTip) {
        cardOut = new QFrame(minerRegRowWidget);
        cardOut->setObjectName(objName);
        QVBoxLayout* cl = new QVBoxLayout(cardOut);
        cl->setContentsMargins(14, 10, 14, 10);
        cl->setSpacing(5);

        QLabel* lTitle = new QLabel(titleStr, cardOut);
        lTitle->setObjectName("trackCardTitle");
        lTitle->setWordWrap(true);

        QLabel* lSpecs = new QLabel(specsStr, cardOut);
        lSpecs->setObjectName("trackCardSpecs");
        lSpecs->setWordWrap(true);

        metaOut = new QLabel(tr("Checking UTXO maturity..."), cardOut);
        metaOut->setObjectName("trackCardMeta");
        metaOut->setWordWrap(true);

        btnOut = new QPushButton(btnText, cardOut);
        btnOut->setMinimumHeight(36);
        btnOut->setCursor(Qt::PointingHandCursor);
        btnOut->setFocusPolicy(Qt::NoFocus);
        btnOut->setToolTip(btnTip);

        cl->addWidget(lTitle);
        cl->addWidget(lSpecs);
        cl->addStretch(1);
        cl->addWidget(metaOut);
        cl->addWidget(btnOut);
        regRow->addWidget(cardOut, 1);
    };

    buildTrackCard(cardTrackCoinLock,
                   "cardTrackCoinLock",
                   tr("Track 1 — Dedicated Coin-Lock (250 KRISTA)"),
                   tr("8.00% Net Block Share (1 Seat) • 8 Coordinator VRF Tickets"),
                   lblTrackCoinLockMeta,
                   btnRegCoinLock,
                   tr("Register Coin-Lock (250 KRISTA)"),
                   tr("<qt><b>Dedicated Coin-Lock Validator Seat</b> (1 seat • 8.00% net block share • 8 Coordinator VRF tickets)<br/>"
                      "Locks 250 KRISTA (100+ conf UTXO) inside your own wallet via CLTV for 2,880 blocks (2,900 wallet buffer); renewable in the final 240 blocks.</qt>"));

    buildTrackCard(cardTrackPoWLock,
                   "cardTrackPoWLock",
                   tr("Track 2A — Funded PoW-Lock (10 KRISTA + PoW)"),
                   tr("4.00% Net Block Share (Up to 3 Seats • Priority) • 2 Coordinator VRF Tickets"),
                   lblTrackPoWLockMeta,
                   btnRegPoW,
                   tr("Register PoW-Lock (10 KRISTA)"),
                   tr("<qt><b>Funded Open PoW Validator Seat</b> (2 priority seats + 3rd when no 0-KRISTA newcomer holds Open-Entry • 4.00% net block share each • 2 Coordinator VRF tickets)<br/>"
                      "Combines an anti-Sybil registration PoW puzzle with a refundable 10 KRISTA (100+ conf UTXO) CLTV lock in your own wallet; prioritized ahead of 0-KRISTA Open-Entry.</qt>"));

    buildTrackCard(cardTrackZeroPoW,
                   "cardTrackZeroPoW",
                   tr("Track 2C — Zero-Coin PoW Open-Entry (0 KRISTA)"),
                   tr("4.00% Net Block Share (1 Permanent Open-Entry Seat) • 0 Coordinator VRF Tickets"),
                   lblTrackZeroPoWMeta,
                   btnRegZeroPoW,
                   tr("Register Zero-Coin PoW (0 KRISTA)"),
                   tr("<qt><b>Zero-Coin Open PoW Entry</b> (1 permanent Open-Entry seat for 0-KRISTA newcomers, plus fallback seats when Funded Pool &lt; 13)<br/>"
                      "Requires 0 KRISTA collateral (only a 240-block registration PoW puzzle + tiny tx fee when available; 0 Coordinator VRF tickets).</qt>"));

    cardLayout->addWidget(minerRegRowWidget);

    // Row 3: 4-Metric Summary Stats Strip
    statsStripFrame = new QFrame(minerHubCard);
    statsStripFrame->setObjectName("statsStripFrame");
    QHBoxLayout* statsLayout = new QHBoxLayout(statsStripFrame);
    statsLayout->setContentsMargins(14, 8, 14, 8);
    statsLayout->setSpacing(16);

    auto createStatBox = [&](QLabel*& lblTitleOut, QLabel*& lblValOut, const QString& titleText) {
        QVBoxLayout* box = new QVBoxLayout();
        box->setSpacing(2);
        box->setContentsMargins(0, 0, 0, 0);
        lblTitleOut = new QLabel(titleText, statsStripFrame);
        lblValOut = new QLabel("—", statsStripFrame);
        box->addWidget(lblTitleOut);
        box->addWidget(lblValOut);
        statsLayout->addLayout(box, 1);
    };

    createStatBox(lblStatMyMinersTitle, lblStatMyMinersVal, tr("My Registered Seats"));
    createStatBox(lblStatHitsTitle, lblStatHitsVal, tr("Rewards Won"));
    createStatBox(lblStatEarnedTitle, lblStatEarnedVal, tr("Total Earned"));
    createStatBox(lblStatImmatureTitle, lblStatImmatureVal, tr("Immature (100 Conf)"));
    cardLayout->addWidget(statsStripFrame);

    // Row 4: Registered Miners Roster Header + Filter Pills + Track Reg Toggle
    QHBoxLayout* rosterHeaderRow = new QHBoxLayout();
    rosterHeaderRow->setSpacing(8);
    lblRosterSection = new QLabel(tr("Registered Miners & Live Committee:"), minerHubCard);
    lblRosterCount = new QLabel(tr("0 / 0"), minerHubCard);
    rosterHeaderRow->addWidget(lblRosterSection);
    rosterHeaderRow->addWidget(lblRosterCount);
    rosterHeaderRow->addStretch(1);

    auto makeRosterPill = [&](const QString& text, const QString& tip) -> QPushButton* {
        QPushButton* b = new QPushButton(text, minerHubCard);
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedHeight(26);
        b->setToolTip(tip);
        return b;
    };
    btnFilterAll = makeRosterPill(tr("All"), tr("Show all registered addresses including expired locks"));
    btnFilterActive = makeRosterPill(tr("Active"), tr("Show only active Masternodes and valid locks"));
    btnFilterElected = makeRosterPill(tr("Elected"), tr("Show only addresses elected to the current block committee"));
    btnFilterExt = makeRosterPill(tr("Ext RPC"), tr("Show only active external cpuminer/ccminer RPC miners"));
    btnFilterVps = makeRosterPill(tr("VPS MN"), tr("Show only remote VPS Masternodes"));
    btnFilterAll->setChecked(true);
    rosterHeaderRow->addWidget(btnFilterAll);
    rosterHeaderRow->addWidget(btnFilterActive);
    rosterHeaderRow->addWidget(btnFilterElected);
    rosterHeaderRow->addWidget(btnFilterExt);
    rosterHeaderRow->addWidget(btnFilterVps);

    comboTrackFilter = new QComboBox(minerHubCard);
    comboTrackFilter->setObjectName("comboTrackFilter");
    comboTrackFilter->setView(new QListView(comboTrackFilter));
    comboTrackFilter->setFixedHeight(26);
    comboTrackFilter->setCursor(Qt::PointingHandCursor);
    comboTrackFilter->setFocusPolicy(Qt::NoFocus);
    comboTrackFilter->setToolTip(tr("Filter miners by Validator Track"));
    comboTrackFilter->addItem(tr("All Tracks"), -1);
    comboTrackFilter->addItem(tr("Masternode"), 4);
    comboTrackFilter->addItem(tr("Coin-Lock (250)"), 3);
    comboTrackFilter->addItem(tr("PoW-Lock (10)"), 2);
    comboTrackFilter->addItem(tr("Zero-Coin (0)"), 1);
    comboTrackFilter->addItem(tr("Unregistered"), 99);
    rosterHeaderRow->addWidget(comboTrackFilter);

    editMinerSearch = new QLineEdit(minerHubCard);
    editMinerSearch->setObjectName("editMinerSearch");
    editMinerSearch->setPlaceholderText(tr("Search address, alias..."));
    editMinerSearch->setClearButtonEnabled(true);
    editMinerSearch->setFixedHeight(26);
    editMinerSearch->setMinimumWidth(155);
    editMinerSearch->setMaximumWidth(220);
    rosterHeaderRow->addWidget(editMinerSearch);

    btnToggleTrackReg = new QPushButton(tr("Hide Track Cards"), minerHubCard);
    btnToggleTrackReg->setCursor(Qt::PointingHandCursor);
    btnToggleTrackReg->setFocusPolicy(Qt::NoFocus);
    btnToggleTrackReg->setFixedHeight(26);
    btnToggleTrackReg->setToolTip(tr("Show or hide the Validator Track Registration cards"));
    rosterHeaderRow->addSpacing(4);
    rosterHeaderRow->addWidget(btnToggleTrackReg);
    cardLayout->addLayout(rosterHeaderRow);

    auto setFilterMode = [this](int mode) {
        minerRosterFilter = mode;
        if (btnFilterAll) btnFilterAll->setChecked(mode == 0);
        if (btnFilterActive) btnFilterActive->setChecked(mode == 1);
        if (btnFilterElected) btnFilterElected->setChecked(mode == 2);
        if (btnFilterExt) btnFilterExt->setChecked(mode == 3);
        if (btnFilterVps) btnFilterVps->setChecked(mode == 4);
        applyRosterFilters();
    };
    connect(btnFilterAll, &QPushButton::clicked, this, [setFilterMode]() { setFilterMode(0); });
    connect(btnFilterActive, &QPushButton::clicked, this, [setFilterMode]() { setFilterMode(1); });
    connect(btnFilterElected, &QPushButton::clicked, this, [setFilterMode]() { setFilterMode(2); });
    connect(btnFilterExt, &QPushButton::clicked, this, [setFilterMode]() { setFilterMode(3); });
    connect(btnFilterVps, &QPushButton::clicked, this, [setFilterMode]() { setFilterMode(4); });
    connect(comboTrackFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { applyRosterFilters(); });
    connect(editMinerSearch, &QLineEdit::textChanged, this, [this](const QString&) { applyRosterFilters(); });
    connect(btnToggleTrackReg, &QPushButton::clicked, this, [this]() {
        fTrackRegVisible = !fTrackRegVisible;
        if (minerRegRowWidget) minerRegRowWidget->setVisible(fTrackRegVisible);
        if (btnToggleTrackReg) btnToggleTrackReg->setText(fTrackRegVisible ? tr("Hide Track Cards") : tr("Register Tracks"));
    });

    tableMyMiners = new QTableWidget(0, 5, minerHubCard);
    tableMyMiners->setObjectName("tableMyMiners");
    tableMyMiners->setHorizontalHeaderLabels({
        tr("Miner Address"),
        tr("Track & Remaining Lock"),
        tr("Live Committee Role"),
        tr("Rewards & Solved Puzzles"),
        tr("Actions")
    });
    tableMyMiners->verticalHeader()->setVisible(false);
    tableMyMiners->setSelectionMode(QAbstractItemView::NoSelection);
    tableMyMiners->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableMyMiners->setFocusPolicy(Qt::NoFocus);
    tableMyMiners->setShowGrid(false);
    tableMyMiners->setAlternatingRowColors(true);
    tableMyMiners->setIconSize(QSize(15, 15));
    tableMyMiners->horizontalHeader()->setStretchLastSection(false);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    tableMyMiners->setColumnWidth(4, 148);
    tableMyMiners->setSortingEnabled(true);
    tableMyMiners->horizontalHeader()->setSortIndicatorShown(true);
    tableMyMiners->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
    connect(tableMyMiners->horizontalHeader(), &QHeaderView::sortIndicatorChanged, this, [this](int, Qt::SortOrder) {
        applyRosterFilters();
    });
    tableMyMiners->setMinimumHeight(180);
    tableMyMiners->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cardLayout->addWidget(tableMyMiners, 1);

    shortcutMinerFullScreenEsc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    shortcutMinerFullScreenEsc->setContext(Qt::WindowShortcut);
    shortcutMinerFullScreenEsc->setEnabled(false);
    connect(shortcutMinerFullScreenEsc, &QShortcut::activated, this, [this]() {
        if (fMinerHubFullScreen) {
            setMinerHubFullScreenMode(false);
        }
    });

    shortcutMinerFullScreenF11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    shortcutMinerFullScreenF11->setContext(Qt::WindowShortcut);
    connect(shortcutMinerFullScreenF11, &QShortcut::activated, this, &MinerHubWidget::onToggleMinerHubFullScreenClicked);

    connect(btnToggleMining, &QPushButton::clicked, this, &MinerHubWidget::onToggleMiningClicked);
    connect(btnFullScreenMinerHub, &QPushButton::clicked, this, &MinerHubWidget::onToggleMinerHubFullScreenClicked);
    connect(btnRenewLock, &QPushButton::clicked, this, &MinerHubWidget::onRenewLockClicked);
    connect(btnMinerThreadsDec, &QPushButton::clicked, spinMinerThreads, &QSpinBox::stepDown);
    connect(btnMinerThreadsInc, &QPushButton::clicked, spinMinerThreads, &QSpinBox::stepUp);
    connect(spinMinerThreads, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &MinerHubWidget::onMinerThreadsChanged);
    connect(btnRegPoW, &QPushButton::clicked, this, &MinerHubWidget::onRegisterPoWClicked);
    connect(btnRegZeroPoW, &QPushButton::clicked, this, &MinerHubWidget::onRegisterPoWClicked);
    connect(btnRegCoinLock, &QPushButton::clicked, this, &MinerHubWidget::onRegisterCoinLockClicked);

    updateMinerStepperButtons();
    updateMinerHubTheme();
    refreshMinerHubStatus();

    minerStatusTimer = new QTimer(this);
    connect(minerStatusTimer, &QTimer::timeout, this, &MinerHubWidget::refreshMinerHubStatus);
    minerStatusTimer->start(3000);
}

void MinerHubWidget::updateMinerHubTheme()
{
    if (!minerHubCard) return;
    const bool light = isLightTheme();
    // 100% KristaTech Logo (#F5897C Salmon + #7695CD Slate-Blue) & Native Wallet Palette
    const QString pageBg = light ? "#F1F5F9" : "#080B15";
    const QString cardBg = light ? "#FFFFFF" : "#0D1321";
    const QString innerBg = light ? "#F8FAFC" : "#131B2E";
    const QString cardBorder = light ? "#CBD5E1" : "#2D4266";
    const QString textPrimary = light ? "#1C2B45" : "#F8FAFC";
    const QString textSecondary = light ? "#475569" : "#CBD5E1";
    const QString stepBtnBg = light ? "#EEF3FA" : "#213458";
    const QString stepBtnHover = light ? "#DCE7F7" : "#2F4A7D";
    const QString stepBtnPressed = light ? "#C4D6F0" : "#3E5B94";
    const QString stepBtnDisabled = light ? "#64748B" : "#A3B8D4";
    const QString disabledBtnBg = light ? "#F1F5F9" : "#18243B";
    const QString disabledBtnBorder = light ? "#CBD5E1" : "#354D73";
    const QString stepBtnText = light ? "#3E5B94" : "#F8FAFC";
    const QString tableAltBg = light ? "#F8FAFC" : "#101829";
    const QString headerBg = light ? "#EEF3FA" : "#1E3052";
    const QString headerText = light ? "#3E5B94" : "#F8FAFC";
    const QString salmonBtnBg = light ? "#FDF0EE" : "#221924";
    const QString salmonBtnText = light ? "#9E3B30" : "#F5897C";
    const QString slateBtnBg = light ? "#EEF3FA" : "#1B2B4A";
    const QString slateBtnText = light ? "#3E5B94" : "#A8C1EE";
    const QString slateBtnBorder = "#7695CD";
    const QString slateBtnHoverBg = light ? "#DCE7F7" : "#243860";
    const QString slateBtnHoverText = light ? "#2F497A" : "#D5E3FA";
    const QString slateBtnHoverBorder = light ? "#5576B2" : "#93B4F0";
    const QString slateBtnPressedBg = light ? "#CEDEF4" : "#16233D";
    const QString tooltipBg = light ? "#F8FAFC" : "#182642";
    const QString tooltipText = light ? "#1E293B" : "#F8FAFC";
    const QString tooltipRule = QString(
        "QToolTip { background-color: %1; color: %2; border: 1.5px solid #7695CD; padding: 6px 10px; font-size: 12px; font-weight: normal; }"
    ).arg(tooltipBg, tooltipText);

    {
        this->setStyleSheet(QString("QWidget#minerHubPage { background-color: %1; } %2").arg(pageBg, tooltipRule));
    }

    minerHubCard->setStyleSheet(QString(
        "QFrame#minerHubCard { background-color: %1; border: 1px solid %2; border-radius: 12px; }"
        "QFrame#coordVrfStripFrame { background-color: %3; border: 1px solid #7695CD; border-radius: 8px; }"
        "QFrame#cardTrackCoinLock { background-color: %3; border: 1px solid %2; border-radius: 9px; }"
        "QFrame#cardTrackPoWLock  { background-color: %3; border: 1px solid %2; border-radius: 9px; }"
        "QFrame#cardTrackZeroPoW  { background-color: %3; border: 1px solid %2; border-radius: 9px; }"
        "QLabel#trackCardTitle { color: %4; font-size: 13px; font-weight: bold; background: transparent; border: none; }"
        "QLabel#trackCardSpecs { color: %5; font-size: 12px; background: transparent; border: none; }"
        "QLabel#trackCardMeta  { color: %4; font-size: 12px; font-weight: bold; background: transparent; border: none; }"
        "%6"
    ).arg(cardBg, cardBorder, innerBg, textPrimary, textSecondary, tooltipRule));

    lblMinerHubTitle->setStyleSheet(QString(".QLabel { color: %1; background: transparent; border: none; }").arg(textPrimary));
    if (lblMinerHubSubtitle) {
        lblMinerHubSubtitle->setStyleSheet(QString(".QLabel { color: %1; font-size: 12px; background: transparent; border: none; }").arg(textSecondary));
    }
    if (lblCoordVrfSummary) {
        lblCoordVrfSummary->setStyleSheet(QString(".QLabel { color: %1; font-size: 12px; font-weight: bold; background: transparent; border: none; }").arg(textPrimary));
    }
    if (lblMatureUtxoSummary) {
        lblMatureUtxoSummary->setStyleSheet(QString(".QLabel { color: %1; font-size: 12px; font-weight: bold; background: transparent; border: none; }").arg(light ? "#3E5B94" : "#93B4F0"));
    }
    lblMinerThreads->setStyleSheet(QString(".QLabel { color: %1; background: transparent; border: none; font-size: 12px; font-weight: bold; }").arg(textSecondary));
    lblTrackSection->setStyleSheet(QString(".QLabel { color: %1; background: transparent; border: none; font-size: 12px; font-weight: bold; letter-spacing: 0.4px; }").arg(light ? "#3E5B94" : "#93B4F0"));
    lblRosterSection->setStyleSheet(QString(".QLabel { color: %1; background: transparent; border: none; font-size: 12px; font-weight: bold; }").arg(textPrimary));
    if (lblRosterCount) {
        lblRosterCount->setStyleSheet(QString(".QLabel { color: %1; background: %2; border: 1px solid %3; border-radius: 5px; padding: 2px 7px; font-size: 12px; font-weight: bold; }")
            .arg(textSecondary, innerBg, cardBorder));
    }

    if (minerThreadsStepperFrame) {
        minerThreadsStepperFrame->setStyleSheet(QString(
            "QFrame#minerThreadsStepperFrame { background-color: %1; border: 1px solid %2; border-radius: 7px; }"
        ).arg(innerBg, cardBorder));
    }

    spinMinerThreads->setStyleSheet(QString(
        "QSpinBox { background-color: transparent; color: %1; border: none; padding: 2px 6px; font-size: 12px; font-weight: bold; }"
    ).arg(textPrimary));

    if (btnMinerThreadsDec) {
        btnMinerThreadsDec->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: none; border-right: 1px solid %3; "
            "border-top-left-radius: 6px; border-bottom-left-radius: 6px; font-size: 16px; font-weight: bold; padding: 0px; }"
            "QPushButton:hover:enabled { background-color: %4; }"
            "QPushButton:pressed:enabled { background-color: %5; }"
            "QPushButton:disabled { background-color: transparent; color: %6; }"
        ).arg(stepBtnBg, stepBtnText, cardBorder, stepBtnHover, stepBtnPressed, stepBtnDisabled));
    }

    if (btnMinerThreadsInc) {
        btnMinerThreadsInc->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: none; border-left: 1px solid %3; "
            "border-top-right-radius: 6px; border-bottom-right-radius: 6px; font-size: 16px; font-weight: bold; padding: 0px; }"
            "QPushButton:hover:enabled { background-color: %4; }"
            "QPushButton:pressed:enabled { background-color: %5; }"
            "QPushButton:disabled { background-color: transparent; color: %6; }"
        ).arg(stepBtnBg, stepBtnText, cardBorder, stepBtnHover, stepBtnPressed, stepBtnDisabled));
    }

    if (btnRegCoinLock) {
        btnRegCoinLock->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1.5px solid #F5897C; border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover:enabled { background-color: #F5897C; color: #231817; border-color: #D96B5E; }"
            "QPushButton:pressed:enabled { background-color: #D96B5E; color: #FFFFFF; border-color: #A6362B; }"
            "QPushButton:disabled { background-color: %3; color: %4; border: 1px solid %5; }"
        ).arg(salmonBtnBg, salmonBtnText, disabledBtnBg, stepBtnDisabled, disabledBtnBorder));
    }
    if (btnRegPoW) {
        btnRegPoW->setStyleSheet(
            QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover:enabled { background-color: %3; color: %4; border-color: %5; }"
                "QPushButton:pressed:enabled { background-color: %6; color: %2; border-color: #7695CD; }"
            ).arg(slateBtnBg, slateBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder, slateBtnPressedBg) +
            QString("QPushButton:disabled { background-color: %1; color: %2; border: 1px solid %3; }")
                .arg(disabledBtnBg, stepBtnDisabled, disabledBtnBorder));
    }
    if (btnRegZeroPoW) {
        btnRegZeroPoW->setStyleSheet(
            QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 6px 10px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover:enabled { background-color: %3; color: %4; border-color: %5; }"
                "QPushButton:pressed:enabled { background-color: %6; color: %7; border-color: #7695CD; }"
            ).arg(stepBtnBg, stepBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder, slateBtnPressedBg, slateBtnText) +
            QString("QPushButton:disabled { background-color: %1; color: %2; border: 1px solid %3; }")
                .arg(disabledBtnBg, stepBtnDisabled, disabledBtnBorder));
    }

    if (btnFullScreenMinerHub) {
        if (fMinerHubFullScreen) {
            btnFullScreenMinerHub->setText(tr("Exit Full Screen (Esc)"));
            btnFullScreenMinerHub->setToolTip(tr("Exit Full Screen mode (Esc / F11)"));
            btnFullScreenMinerHub->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 0px 12px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover { background-color: %3; color: %4; border-color: %5; }"
                "QPushButton:pressed { background-color: %6; color: %2; }"
            ).arg(slateBtnBg, slateBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder, slateBtnPressedBg));
        } else {
            btnFullScreenMinerHub->setText(tr("Full Screen"));
            btnFullScreenMinerHub->setToolTip(tr("Expand Miner & Validator Hub to Full Screen (F11)"));
            btnFullScreenMinerHub->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 0px 12px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover { background-color: %3; color: %4; border-color: %5; }"
                "QPushButton:pressed { background-color: %6; color: %7; }"
            ).arg(stepBtnBg, stepBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder, slateBtnPressedBg, slateBtnText));
        }
    }

    const QString rosterPillCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0px 10px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { border-color: #7695CD; color: %4; }"
        "QPushButton:checked { background-color: %5; color: %6; border: 1px solid #7695CD; }"
        "QPushButton:checked:hover { background-color: %7; color: %8; border-color: %9; }"
    ).arg(innerBg, textSecondary, cardBorder, textPrimary, slateBtnBg, slateBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder);
    for (QPushButton* b : {btnFilterAll, btnFilterActive, btnFilterElected, btnFilterExt, btnFilterVps, btnToggleTrackReg}) {
        if (b) b->setStyleSheet(rosterPillCss);
    }
    if (comboTrackFilter) {
        comboTrackFilter->setStyleSheet(QString(
            "QComboBox#comboTrackFilter { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 0px 22px 0px 8px; font-size: 12px; font-weight: bold; }"
            "QComboBox#comboTrackFilter:hover { border-color: #7695CD; }"
            "QComboBox#comboTrackFilter::drop-down { border: none; width: 18px; }"
            "QComboBox#comboTrackFilter QAbstractItemView { background-color: %1; color: %2; "
            "border: 1px solid %3; selection-background-color: #7695CD; selection-color: #FFFFFF; outline: 0; }"
        ).arg(innerBg, textPrimary, cardBorder));
    }
    if (editMinerSearch) {
        editMinerSearch->setStyleSheet(QString(
            "QLineEdit#editMinerSearch { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 0px 8px; font-size: 12px; }"
            "QLineEdit#editMinerSearch:focus { border-color: #7695CD; }"
        ).arg(innerBg, textPrimary, cardBorder));
        QPalette pal = editMinerSearch->palette();
        pal.setColor(QPalette::PlaceholderText, light ? QColor("#64748B") : QColor("#94A3B8"));
        editMinerSearch->setPalette(pal);
    }

    if (lockExpiryBanner && lblLockExpiryWarning && btnRenewLock) {
        lockExpiryBanner->setStyleSheet(QString(
            "QFrame#lockExpiryBanner { background-color: %1; border-top: 1px solid %2; border-right: 1px solid %2; "
            "border-bottom: 1px solid %2; border-left: 4px solid #F5897C; border-radius: 8px; }"
        ).arg(innerBg, cardBorder));
        if (lblLockExpiryIcon) {
            lblLockExpiryIcon->setStyleSheet(".QLabel { background: transparent; border: none; }");
            lblLockExpiryIcon->setPixmap(MakeMinerHubVectorPixmap(MinerHubGlyph::Clock, QColor("#C94A3E"), 14));
        }
        lblLockExpiryWarning->setStyleSheet(QString(
            ".QLabel { color: %1; background: transparent; border: none; font-size: 12px; font-weight: bold; }"
        ).arg(textPrimary));
        btnRenewLock->setStyleSheet(
            QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 5px 12px; font-size: 12px; font-weight: 600; }"
                "QPushButton:hover:enabled { background-color: %3; color: %4; border-color: %5; }"
                "QPushButton:pressed:enabled { background-color: %6; color: %2; }"
            ).arg(slateBtnBg, slateBtnText, slateBtnHoverBg, slateBtnHoverText, slateBtnHoverBorder, slateBtnPressedBg) +
            QString("QPushButton:disabled { background-color: %1; color: %2; border-color: %3; }")
                .arg(disabledBtnBg, stepBtnDisabled, disabledBtnBorder));
    }

    statsStripFrame->setStyleSheet(QString(
        "QFrame#statsStripFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
    ).arg(innerBg, cardBorder));

    QString statTitleCss = QString(".QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; border: none; }").arg(textSecondary);
    QString statValCss = QString(".QLabel { color: %1; font-size: 14px; font-weight: bold; background: transparent; border: none; }").arg(textPrimary);
    for (QLabel* lbl : {lblStatMyMinersTitle, lblStatHitsTitle, lblStatEarnedTitle, lblStatImmatureTitle}) {
        if (lbl) lbl->setStyleSheet(statTitleCss);
    }
    for (QLabel* lbl : {lblStatMyMinersVal, lblStatHitsVal, lblStatEarnedVal, lblStatImmatureVal}) {
        if (lbl) lbl->setStyleSheet(statValCss);
    }

    tableMyMiners->setStyleSheet(QString(
        "QTableWidget#tableMyMiners { background-color: %1; alternate-background-color: %2; color: %3; border: 1px solid %4; border-radius: 8px; font-size: 13px; }"
        "QTableWidget#tableMyMiners::item { padding: 5px 8px; border: none; }"
        "QHeaderView::section { background-color: %5; color: %6; font-size: 12px; font-weight: bold; padding: 6px 8px; border: none; border-bottom: 2px solid #7695CD; }"
    ).arg(cardBg, tableAltBg, textPrimary, cardBorder, headerBg, headerText));

    fForceMinerTableRefresh = true;
    refreshMinerHubStatus();
}

void MinerHubWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    fForceMinerTableRefresh = true;
    refreshMinerHubStatus();
}

void MinerHubWidget::refreshMinerHubStatus()
{
    if (!minerHubCard || fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    bool fGenerating = fGenerateBitcoins.load();
    const bool fAnyExtActive = HasAnyActiveExternalAdamMiner(65);
    const uint64_t nExtDigest = GetExternalAdamMinerActivityDigest(65);
    int nConfiguredThreads = GetConfiguredMinerThreads();
    int nThreads = (fGenerating && nConfiguredThreads > 0)
        ? nConfiguredThreads
        : GetGenProcLimit();
    if (nThreads <= 0) nThreads = spinMinerThreads->value();
    if (nThreads <= 0) nThreads = std::max(1, (int)std::thread::hardware_concurrency());

    size_t nPoolSize = cachedMinerStatsHeight >= 0 ? (size_t)lblMinerStatusBadge->property("lastPoolSize").toInt() : 0;
    int nTipHeight = -1;
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain && chainActive.Tip()) {
            nTipHeight = chainActive.Height();
            nPoolSize = GetAdamMinerPool(nTipHeight).size();
            lblMinerStatusBadge->setProperty("lastPoolSize", (int)nPoolSize);
        }
    }

    if (fGenerating) {
        if (spinMinerThreads->value() != nThreads && !spinMinerThreads->hasFocus()) {
            spinMinerThreads->blockSignals(true);
            spinMinerThreads->setValue(std::min(nThreads, spinMinerThreads->maximum()));
            spinMinerThreads->blockSignals(false);
        }
        lblMinerStatusBadge->setText(tr("● ACTIVE (%1T) • Pool: %2").arg(nThreads).arg(nPoolSize));
        lblMinerStatusBadge->setStyleSheet(QString(".QLabel { color: %1; font-weight: bold; font-size: 13px; background: transparent; border: none; }")
                                               .arg(isLightTheme() ? "#1C2B45" : "#FFA296"));
        btnToggleMining->setText(tr("Stop CPU Mining"));
        btnToggleMining->setStyleSheet(
            "QPushButton { background-color: #C94A3E; color: #FFFFFF; border: 1px solid #A8382E; border-radius: 6px; padding: 6px 14px; font-weight: bold; font-size: 12px; }"
            "QPushButton:hover { background-color: #A8382E; }"
        );
    } else {
        if (fAnyExtActive) {
            lblMinerStatusBadge->setText(tr("● EXT MINER • Pool: %1").arg(nPoolSize));
            lblMinerStatusBadge->setStyleSheet(QString(".QLabel { color: %1; font-weight: bold; font-size: 13px; background: transparent; border: none; }")
                                                   .arg(isLightTheme() ? "#3E5B94" : "#93B4F0"));
        } else {
            lblMinerStatusBadge->setText(tr("○ STOPPED • Pool: %1").arg(nPoolSize));
            lblMinerStatusBadge->setStyleSheet(QString(".QLabel { color: %1; font-weight: bold; font-size: 13px; background: transparent; border: none; }")
                                                   .arg(isLightTheme() ? "#475569" : "#CBD5E1"));
        }
        btnToggleMining->setText(tr("Start 1-Click Mining"));
        const bool lightBtn = isLightTheme();
        btnToggleMining->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 6px 14px; font-weight: 600; font-size: 12px; }"
            "QPushButton:hover { background-color: %3; color: %4; border-color: %5; }"
            "QPushButton:pressed { background-color: %6; color: %7; border-color: %8; }"
        ).arg(lightBtn ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
              lightBtn ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"),
              lightBtn ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860"),
              lightBtn ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA"),
              lightBtn ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0"),
              lightBtn ? QStringLiteral("#CEDEF4") : QStringLiteral("#16233D"),
              lightBtn ? QStringLiteral("#263C66") : QStringLiteral("#A8C1EE"),
              lightBtn ? QStringLiteral("#4A679E") : QStringLiteral("#5576B2")));
    }
    updateMinerStepperButtons();

    if (!isVisible() || fModalOpen) return;

    const bool lightThemeNow = isLightTheme();
    const QColor renewIconActiveColor(lightThemeNow ? "#3E5B94" : "#A8C1EE");
    const QColor renewIconDisabledColor(lightThemeNow ? "#94A3B8" : "#64748B");
    auto updateRenewButtonAppearance = [&](bool fLockedWallet) {
        if (!btnRenewLock) return;
        const bool fBusy = isSolvingPoWReg.load();
        btnRenewLock->setEnabled(!fBusy);
        if (fBusy) {
            btnRenewLock->setIcon(MakeMinerHubButtonIcon(MinerHubGlyph::Clock, renewIconActiveColor, renewIconDisabledColor));
            btnRenewLock->setText(tr("Renewing..."));
        } else if (fLockedWallet) {
            btnRenewLock->setIcon(MakeMinerHubButtonIcon(MinerHubGlyph::Unlock, renewIconActiveColor, renewIconDisabledColor));
            btnRenewLock->setText(tr("Unlock && Renew"));
        } else {
            btnRenewLock->setIcon(MakeMinerHubButtonIcon(MinerHubGlyph::Refresh, renewIconActiveColor, renewIconDisabledColor));
            btnRenewLock->setText(tr("Renew Now"));
        }
    };

    if (btnRenewLock) {
        const bool fLocked = pwalletMain && pwalletMain->IsLocked();
        updateRenewButtonAppearance(fLocked);
    }

#ifdef ENABLE_WALLET
    if (!pwalletMain || nTipHeight < 0) return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain || !chainActive.Tip()) return;
    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet) return;

    auto updateLiveAdamSolLabel = [this](CBlockIndex* pTip, int tipHeight, size_t nElectedCount, int nMnCount) {
        if (!lblAdamSolutionsLive) return;
        size_t nSolCount = 0;
        if (pTip) {
            LOCK(cs_adam_solutions);
            auto itCache = mapAdamSolutionsCache.find(pTip->GetBlockHash());
            if (itCache != mapAdamSolutionsCache.end()) {
                nSolCount = itCache->second.size();
            }
        }
        const size_t nDisplaySeats = (nElectedCount > 0)
            ? nElectedCount
            : (size_t)Params().GetConsensus().nAdamMinersCount;
        const int64_t nTipAge = pTip ? std::max<int64_t>(0, GetTime() - pTip->GetBlockTime()) : 0;
        const int nEffThresh = Params().GetConsensus().GetAdamEffectiveThreshold(tipHeight + 1, nTipAge, nMnCount);
        const bool fReady = ((int)nSolCount >= nEffThresh) || (tipHeight + 1 <= Params().GetConsensus().nAdamQuorumEnforcementHeight);
        const bool fLight = isLightTheme();
        const QString solBg = fReady ? (fLight ? "#EEF3FB" : "#1C2942") : (fLight ? "#FEF2F2" : "#3B1D24");
        const QString solBorder = fReady ? (fLight ? "#C6D5EE" : "#4A6A9E") : (fLight ? "#FCA5A5" : "#B91C1C");
        const QString solFg = fReady ? (fLight ? "#3E5B94" : "#CBE0FF") : (fLight ? "#B91C1C" : "#FCA5A5");
        lblAdamSolutionsLive->setText(
            tr("ADAMSOL: %1 / %2 (Min %3)  •  %4s / %5s")
                .arg(nSolCount)
                .arg(nDisplaySeats)
                .arg(nEffThresh)
                .arg(nTipAge)
                .arg(Params().GetConsensus().nTargetSpacing));
        lblAdamSolutionsLive->setStyleSheet(QString(
            "QLabel#lblAdamSolutionsLive { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 3px 8px; font-size: 12px; font-weight: bold; }"
        ).arg(solBg, solFg, solBorder));
        lblAdamSolutionsLive->setToolTip(tr("<qt><b>Live Block #%1 ADAM Solutions:</b> %2 cached solutions out of %3 elected seats (quorum threshold: %4).<br/><b>Time since Block #%5:</b> %6s</qt>")
            .arg(tipHeight + 1).arg(nSolCount).arg(nDisplaySeats).arg(nEffThresh).arg(tipHeight).arg(nTipAge));
    };

    updateLiveAdamSolLabel(chainActive.Tip(), nTipHeight, (size_t)cachedElectedSeatsCount, cachedActiveMnCount);

    size_t nCurrentWalletSize = pwalletMain->mapWallet.size();
    const int64_t nNowSec = GetTime();
    const bool fExtAgeTick = fAnyExtActive && (nNowSec - cachedExtRefreshTime >= 6);
    if (!fForceMinerTableRefresh &&
        !fExtAgeTick &&
        nTipHeight == cachedMinerStatsHeight &&
        nCurrentWalletSize == cachedMinerStatsWalletSize &&
        fGenerating == cachedMinerGenerating &&
        nExtDigest == cachedExtMinerDigest) {
        return;
    }

    cachedExtRefreshTime = nNowSec;
    cachedMinerStatsHeight = nTipHeight;
    cachedMinerStatsWalletSize = nCurrentWalletSize;
    cachedMinerGenerating = fGenerating;
    cachedExtMinerDigest = nExtDigest;
    fForceMinerTableRefresh = false;

    enum MinerTrackType {
        TRACK_BOOTSTRAP = 0,
        TRACK_ZEROCOIN = 1,
        TRACK_POW_LOCK = 2,
        TRACK_COIN_LOCK = 3,
        TRACK_MASTERNODE = 4
    };

    struct MyMinerEntry {
        CKeyID keyId;
        CKeyID payoutKeyId;
        CPubKey pubKey;
        QString mnAlias;
        MinerTrackType track = TRACK_BOOTSTRAP;
        bool inActivePool = false;
        bool hasExplicitReg = false;
        bool fRegPending = false;
        int nRegHeight = -1;
        int nUnlockHeight = 0;
        bool fLockSpent = false;
        bool isUnregisteredExt = false;
        ExternalMinerKind extKind = ExternalMinerKind::UNKNOWN;
        int nPaidHits = 0;
        int nPuzzlesSolved = 0;
        int nCoordSigned = 0;
        CAmount nEarned = 0;
        int nLastHitHeight = -1;
        int nLastPuzzleHeight = -1;
        int nLastCoordHeight = -1;
    };

    std::map<CKeyID, MyMinerEntry> mapMyMiners;
    std::map<CKeyID, CKeyID> mapPayoutToMinerKey;

    // Map collateral outpoints from masternode.conf to their alias
    std::map<COutPoint, std::string> mapConfOutpointAlias;
    for (const auto& mnEntry : masternodeConfig.getEntries()) {
        mapConfOutpointAlias[mnEntry.getOutpoint()] = mnEntry.getAlias();
    }

    // 1. Inspect active network pool at current tip (including Masternodes owned by this wallet / masternode.conf / KTDIP3)
    std::set<CPubKey> setMNKeys, setCoinLockKeys;
    std::vector<CPubKey> vPool = GetAdamMinerPool(nTipHeight, nullptr, &setMNKeys, &setCoinLockKeys);
    const dip3::CDeterministicMNList dip3List = deterministicMNManager ? deterministicMNManager->GetListAtChainTip() : dip3::CDeterministicMNList();
    for (const CPubKey& pk : vPool) {
        if (!pk.IsValid()) continue;
        CKeyID kid = pk.GetID();
        bool isMyKey = pwalletMain->HaveKey(kid);
        CKey dummyMnPriv;
        if (!isMyKey && llmq::GetMasternodePrivKey(pk, dummyMnPriv)) {
            isMyKey = true;
        }

        CKeyID collateralKeyId;
        std::string mnAlias;
        COutPoint colOut;
        bool hasColOut = false;
        auto itCol = mapMasternodeCollateralOutpoints.find(pk);
        if (itCol != mapMasternodeCollateralOutpoints.end()) {
            colOut = itCol->second;
            hasColOut = true;
        }
        const dip3::CDeterministicMN* dmn = dip3List.GetMNByOwner(pk);
        if (dmn && !dmn->state.IsRevoked()) {
            if (!hasColOut && !dmn->collateralOutpoint.IsNull()) {
                colOut = dmn->collateralOutpoint;
                hasColOut = true;
            }
            if (!dmn->state.scriptPayout.empty()) {
                if (pwalletMain->IsMine(CTxOut(0, dmn->state.scriptPayout)) & ISMINE_ALL) {
                    isMyKey = true;
                }
                CTxDestination payoutDest;
                if (ExtractDestination(dmn->state.scriptPayout, payoutDest)) {
                    if (const CKeyID* pPayoutKey = boost::get<CKeyID>(&payoutDest)) {
                        collateralKeyId = *pPayoutKey;
                    }
                }
            }
        }
        if (hasColOut) {
            auto itAlias = mapConfOutpointAlias.find(colOut);
            if (itAlias != mapConfOutpointAlias.end()) {
                isMyKey = true;
                mnAlias = itAlias->second;
            }
            auto itWtx = pwalletMain->mapWallet.find(colOut.hash);
            if (itWtx != pwalletMain->mapWallet.end() && colOut.n < itWtx->second.vout.size()) {
                const CTxOut& colTxOut = itWtx->second.vout[colOut.n];
                if (pwalletMain->IsMine(colTxOut) & ISMINE_ALL) {
                    isMyKey = true;
                }
                if (collateralKeyId.IsNull()) {
                    CTxDestination colDest;
                    if (ExtractDestination(colTxOut.scriptPubKey, colDest)) {
                        if (const CKeyID* pColKey = boost::get<CKeyID>(&colDest)) {
                            collateralKeyId = *pColKey;
                        }
                    }
                }
            }
        }

        if (isMyKey) {
            MyMinerEntry& e = mapMyMiners[kid];
            e.keyId = kid;
            e.pubKey = pk;
            e.inActivePool = true;
            if (!collateralKeyId.IsNull()) {
                e.payoutKeyId = collateralKeyId;
                mapPayoutToMinerKey[collateralKeyId] = kid;
            }
            if (!mnAlias.empty()) {
                e.mnAlias = QString::fromStdString(mnAlias);
            }
            if (setMNKeys.count(pk)) {
                e.track = TRACK_MASTERNODE;
            } else if (setCoinLockKeys.count(pk)) {
                e.track = TRACK_COIN_LOCK;
            } else {
                // Tentatively Bootstrap fallback until upgraded by an explicit PoW-Lock / Zero-PoW tx in Pass 2a
                e.track = TRACK_BOOTSTRAP;
            }
        }
    }

    const int nAdamStartHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_ADAM].nActivationHeight;
    const int nModelDStartHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight;

    // 2a. First pass: discover all explicit Coin-Lock / PoW-Lock / Zero-Coin registrations in wallet
    for (const auto& pair : pwalletMain->mapWallet) {
        const uint256& txid = pair.first;
        const CWalletTx& wtx = pair.second;

        int nTxHeight = nTipHeight;
        if (!wtx.hashBlock.IsNull()) {
            auto itBlk = mapBlockIndex.find(wtx.hashBlock);
            if (itBlk != mapBlockIndex.end() && itBlk->second) {
                nTxHeight = itBlk->second->nHeight;
            }
        }

        bool fTxConflicted = false;
        int nTxDepth = wtx.GetDepthAndMempool(fTxConflicted);
        bool fTxValidForReg = !wtx.isAbandoned() && !fTxConflicted && (nTxDepth > 0 || (nTxDepth == 0 && wtx.InMempool()));
        if (!fTxValidForReg) continue;

        for (size_t i = 0; i < wtx.vout.size(); ++i) {
            const CTxOut& vout = wtx.vout[i];
            if (vout.scriptPubKey.size() < 60) continue;

            CPubKey regPubKey;
            int64_t lockTime = 0;
            CKeyID pubkeyHash;
            std::vector<unsigned char> nonce;
            uint256 challenge;

            bool isCoinLock = MatchCoinLockRegistration(vout.scriptPubKey, regPubKey, lockTime, pubkeyHash);
            bool isPoWLock = !isCoinLock && MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, regPubKey, lockTime, pubkeyHash);

            if ((isCoinLock || isPoWLock) && regPubKey.IsValid()) {
                CKeyID kid = regPubKey.GetID();
                if (pwalletMain->HaveKey(kid) || (pwalletMain->IsMine(vout) & ISMINE_ALL)) {
                    MyMinerEntry& e = mapMyMiners[kid];
                    e.keyId = kid;
                    e.pubKey = regPubKey;
                    e.hasExplicitReg = true;
                    if (nTxHeight >= e.nRegHeight) {
                        e.nRegHeight = nTxHeight;
                        const bool fZeroCoinReg = !isCoinLock && vout.nValue == 0;
                        const bool fHardeningV2Tip = Params().GetConsensus().NetworkUpgradeActive(std::max(0, nTipHeight), Consensus::UPGRADE_HARDENING_V2);
                        if (fZeroCoinReg && fHardeningV2Tip) {
                            const int nBaseH = (nTxDepth == 0) ? (nTipHeight + 1) : nTxHeight;
                            e.nUnlockHeight = (int)std::min<int64_t>(lockTime, (int64_t)nBaseH + GetZeroCoinRegPeriod(nBaseH));
                        } else {
                            e.nUnlockHeight = (int)lockTime;
                        }
                        e.fLockSpent = pwalletMain->IsSpent(txid, i);
                        e.fRegPending = (nTxDepth == 0);
                        if (e.track != TRACK_MASTERNODE) {
                            if (isCoinLock || vout.nValue == MINER_REGISTRATION_LOCK_AMOUNT) {
                                e.track = TRACK_COIN_LOCK;
                            } else if (vout.nValue >= POW_LOCK_REGISTRATION_AMOUNT) {
                                e.track = TRACK_POW_LOCK;
                            } else {
                                e.track = TRACK_ZEROCOIN;
                            }
                        }
                    }
                }
            }
        }
    }

    // 2b. Second pass: accumulate wallet-wide rewards (summary strip) and ADAM-era per-miner payouts (table rows)
    int nTotalWalletHits = 0;
    CAmount nTotalWalletEarned = 0;
    CAmount nImmatureBalance = pwalletMain->GetImmatureBalance();
    std::set<std::pair<int, CKeyID>> setCreditedMinerBlocks;
    std::map<int, CAmount> mapUnassignedPreModelDCoordPayout;

    auto isModelDSeatPayoutAmount = [](int h, CAmount val) -> bool {
        if (!IsAdamValidatorSplitActive(h) || val <= 0) return false;
        CAmount nBlockValActual = CMasternode::GetBlockValue(h);
        CAmount nTreasurySplit = (h > 1) ? (nBlockValActual * 7 / 100) : 0;
        CAmount nBlockVal = nBlockValActual - nTreasurySplit;
        CAmount nPartSplitTotal = nBlockVal * 40 / 100;
        CAmount nCoinLockPool = nBlockVal * 8 / 100;
        CAmount nOpenPoWPool = (nBlockVal * 20 / 100) - nCoinLockPool;
        CAmount nMNPartPool = nPartSplitTotal - (nCoinLockPool + nOpenPoWPool);

        CAmount nCoinLockBase = nCoinLockPool / Consensus::Params::ADAM_COIN_LOCK_TRACK_QUOTA;
        CAmount nCoinLockRem = nCoinLockPool % Consensus::Params::ADAM_COIN_LOCK_TRACK_QUOTA;
        CAmount nOpenPoWBase = nOpenPoWPool / Consensus::Params::ADAM_OPEN_POW_TRACK_QUOTA;
        CAmount nOpenPoWRem = nOpenPoWPool % Consensus::Params::ADAM_OPEN_POW_TRACK_QUOTA;
        CAmount nMNBase = nMNPartPool / Consensus::Params::ADAM_MASTERNODE_TRACK_QUOTA;
        CAmount nMNRem = nMNPartPool % Consensus::Params::ADAM_MASTERNODE_TRACK_QUOTA;

        return (val == nCoinLockBase || val == nCoinLockBase + nCoinLockRem ||
                val == nOpenPoWBase || val == nOpenPoWBase + nOpenPoWRem ||
                val == nMNBase || val == nMNBase + nMNRem);
    };

    for (const auto& pair : pwalletMain->mapWallet) {
        const CWalletTx& wtx = pair.second;
        if (!wtx.IsCoinBase() && !wtx.IsCoinStake()) continue;
        if (wtx.GetDepthInMainChain() <= 0) continue;

        int nTxHeight = nTipHeight;
        if (!wtx.hashBlock.IsNull()) {
            auto itBlk = mapBlockIndex.find(wtx.hashBlock);
            if (itBlk != mapBlockIndex.end() && itBlk->second) {
                nTxHeight = itBlk->second->nHeight;
            }
        }

        const bool fIsAdamBlock = (nTxHeight >= nAdamStartHeight);

        auto creditDirectPayout = [&](const CKeyID& destKeyId, CAmount amount, bool isPuzzleSeat) -> bool {
            CKeyID targetKid = destKeyId;
            auto itMap = mapPayoutToMinerKey.find(destKeyId);
            if (itMap != mapPayoutToMinerKey.end()) {
                targetKid = itMap->second;
            }
            auto itMiner = mapMyMiners.find(targetKid);
            if (itMiner != mapMyMiners.end()) {
                itMiner->second.nEarned += amount;
                itMiner->second.nPaidHits++;
                if (nTxHeight > itMiner->second.nLastHitHeight) {
                    itMiner->second.nLastHitHeight = nTxHeight;
                }
                if (fIsAdamBlock && isPuzzleSeat) {
                    itMiner->second.nPuzzlesSolved++;
                    if (nTxHeight > itMiner->second.nLastPuzzleHeight) {
                        itMiner->second.nLastPuzzleHeight = nTxHeight;
                    }
                }
                if (fIsAdamBlock) {
                    setCreditedMinerBlocks.insert(std::make_pair(nTxHeight, targetKid));
                }
                return true;
            }
            return false;
        };

        if (wtx.IsCoinBase()) {
            for (size_t vIdx = 0; vIdx < wtx.vout.size(); ++vIdx) {
                const CTxOut& vout = wtx.vout[vIdx];
                if (vout.nValue <= 0) continue;
                if (pwalletMain->IsMine(vout) & ISMINE_ALL) {
                    nTotalWalletHits++;
                    nTotalWalletEarned += vout.nValue;
                    bool creditedToMinerRow = false;
                    CTxDestination dest;
                    if (ExtractDestination(vout.scriptPubKey, dest)) {
                        if (const CKeyID* pKeyID = boost::get<CKeyID>(&dest)) {
                            const size_t minSeatOutIdx = IsModelDActive(nTxHeight) ? 2 : 1;
                            bool isSeat = (vIdx >= minSeatOutIdx) && isModelDSeatPayoutAmount(nTxHeight, vout.nValue);
                            creditedToMinerRow = creditDirectPayout(*pKeyID, vout.nValue, isSeat);
                        }
                    }
                    // If a pre-Model-D ADAM PoW Coordinator block paid vout[0] to an unassigned reservekey,
                    // remember it so Step 2c can attribute it to the on-chain Coordinator key from our wallet.
                    if (fIsAdamBlock && !IsModelDActive(nTxHeight) && vIdx == 0 && !creditedToMinerRow) {
                        mapUnassignedPreModelDCoordPayout[nTxHeight] = vout.nValue;
                    }
                }
            }
        } else if (wtx.IsCoinStake()) {
            std::set<CKeyID> setStakerInputKeys;
            for (const CTxIn& vin : wtx.vin) {
                auto itPrev = pwalletMain->mapWallet.find(vin.prevout.hash);
                if (itPrev != pwalletMain->mapWallet.end() && vin.prevout.n < itPrev->second.vout.size()) {
                    CTxDestination prevDest;
                    if (ExtractDestination(itPrev->second.vout[vin.prevout.n].scriptPubKey, prevDest)) {
                        if (const CKeyID* pPrevKey = boost::get<CKeyID>(&prevDest)) {
                            setStakerInputKeys.insert(*pPrevKey);
                        }
                    }
                }
            }

            CAmount nStakerOutTotal = 0;
            CKeyID primaryStakerKey;
            for (const CTxOut& vout : wtx.vout) {
                if (vout.nValue <= 0) continue;
                if (pwalletMain->IsMine(vout) & ISMINE_ALL) {
                    CTxDestination dest;
                    if (ExtractDestination(vout.scriptPubKey, dest)) {
                        if (const CKeyID* pKeyID = boost::get<CKeyID>(&dest)) {
                            if (setStakerInputKeys.count(*pKeyID)) {
                                nStakerOutTotal += vout.nValue;
                                if (primaryStakerKey.IsNull()) primaryStakerKey = *pKeyID;
                            } else {
                                nTotalWalletHits++;
                                nTotalWalletEarned += vout.nValue;
                                bool isSeat = isModelDSeatPayoutAmount(nTxHeight, vout.nValue);
                                creditDirectPayout(*pKeyID, vout.nValue, isSeat);
                            }
                        }
                    }
                }
            }
            CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
            if (nStakerOutTotal > nDebit && !primaryStakerKey.IsNull()) {
                CAmount nNetStakeReward = nStakerOutTotal - nDebit;
                nTotalWalletHits++;
                nTotalWalletEarned += nNetStakeReward;
                creditDirectPayout(primaryStakerKey, nNetStakeReward, false);
            }
        }
    }

    // 2c. Bounded & chunked pre-Model D ADAM block scan (!IsModelDActive(h), max 200 blocks per timer tick)
    const int nMaxPreModelDHeight = std::min(nTipHeight, nModelDStartHeight - 1);

    static int s_nLastScannedHeight = 199;
    static uint256 s_hashLastScannedBlock;
    static std::map<CKeyID, std::vector<int>> s_mapPreModelDSolvedHeights;
    static std::map<CKeyID, std::vector<int>> s_mapPreModelDCoordHeights;
    static std::map<CKeyID, std::vector<int>> s_mapPreModelDCoordSignedHeights;

    if (s_nLastScannedHeight >= nAdamStartHeight) {
        if (s_nLastScannedHeight > nTipHeight ||
            !chainActive[s_nLastScannedHeight] ||
            chainActive[s_nLastScannedHeight]->GetBlockHash() != s_hashLastScannedBlock) {
            s_nLastScannedHeight = nAdamStartHeight - 1;
            s_hashLastScannedBlock.SetNull();
            s_mapPreModelDSolvedHeights.clear();
            s_mapPreModelDCoordHeights.clear();
            s_mapPreModelDCoordSignedHeights.clear();
        }
    }

    if (nMaxPreModelDHeight >= nAdamStartHeight && s_nLastScannedHeight < nMaxPreModelDHeight) {
        const int MAX_SCAN_BLOCKS_PER_TICK = 200;
        const int nStartScan = std::max(nAdamStartHeight, s_nLastScannedHeight + 1);
        const int nEndScan = std::min(nMaxPreModelDHeight, nStartScan + MAX_SCAN_BLOCKS_PER_TICK - 1);

        for (int h = nStartScan; h <= nEndScan; ++h) {
            if (IsModelDActive(h)) break;
            CBlockIndex* pindex = chainActive[h];
            if (!pindex) continue;
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex)) continue;

            std::set<CKeyID> solvedSeatsInBlock;
            for (size_t i = 0; i < block.vAdamMiners.size(); ++i) {
                const CPubKey& pk = block.vAdamMiners[i];
                if (!pk.IsValid()) continue;
                bool isSolvedSeat = (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty());
                bool isV11CoordEntry = (block.nVersion == 11 && i + 1 == block.vAdamMiners.size());
                if (isSolvedSeat && !isV11CoordEntry) {
                    if (!IsAdamValidatorSplitActive(h)) {
                        s_mapPreModelDSolvedHeights[pk.GetID()].push_back(h);
                    }
                    solvedSeatsInBlock.insert(pk.GetID());
                }
                if (isV11CoordEntry) {
                    s_mapPreModelDCoordSignedHeights[pk.GetID()].push_back(h);
                    if (!block.IsProofOfStake() && !solvedSeatsInBlock.count(pk.GetID())) {
                        s_mapPreModelDCoordHeights[pk.GetID()].push_back(h);
                    }
                }
            }
            if (block.nVersion >= 12 && pindex->pprev) {
                const uint256 blockHash = pindex->GetBlockHash();
                const uint256 adamSeed = GetAdamSeed(pindex->pprev);
                std::vector<CPubKey> vDummyMiners;
                CPubKey v12Coord;
                bool fFoundV12Coord = false;
                if (SelectAdamNodes(adamSeed, Params().GetConsensus(), vDummyMiners, v12Coord, h) &&
                    v12Coord.IsValid() && VerifyAdamCoordinatorSig(block, v12Coord, &blockHash)) {
                    fFoundV12Coord = true;
                } else {
                    for (size_t i = 0; i < block.vAdamMiners.size(); ++i) {
                        if (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty() &&
                            block.vAdamMiners[i].IsValid() &&
                            VerifyAdamCoordinatorSig(block, block.vAdamMiners[i], &blockHash)) {
                            v12Coord = block.vAdamMiners[i];
                            fFoundV12Coord = true;
                            break;
                        }
                    }
                }
                if (fFoundV12Coord && v12Coord.IsValid()) {
                    s_mapPreModelDCoordSignedHeights[v12Coord.GetID()].push_back(h);
                    if (!block.IsProofOfStake() && !solvedSeatsInBlock.count(v12Coord.GetID())) {
                        s_mapPreModelDCoordHeights[v12Coord.GetID()].push_back(h);
                    }
                }
            }

            s_nLastScannedHeight = h;
            s_hashLastScannedBlock = pindex->GetBlockHash();
        }

        if (s_nLastScannedHeight < nMaxPreModelDHeight) {
            fForceMinerTableRefresh = true;
        }
    }

    for (auto& kv : mapMyMiners) {
        MyMinerEntry& e = kv.second;
        auto itSolved = s_mapPreModelDSolvedHeights.find(e.keyId);
        if (itSolved != s_mapPreModelDSolvedHeights.end()) {
            for (int h : itSolved->second) {
                e.nPuzzlesSolved++;
                if (h > e.nLastPuzzleHeight) e.nLastPuzzleHeight = h;
            }
        }
        auto itCoordSigned = s_mapPreModelDCoordSignedHeights.find(e.keyId);
        if (itCoordSigned != s_mapPreModelDCoordSignedHeights.end()) {
            for (int h : itCoordSigned->second) {
                e.nCoordSigned++;
                if (h > e.nLastCoordHeight) e.nLastCoordHeight = h;
            }
        }
        auto itCoord = s_mapPreModelDCoordHeights.find(e.keyId);
        if (itCoord != s_mapPreModelDCoordHeights.end()) {
            for (int h : itCoord->second) {
                if (!setCreditedMinerBlocks.count(std::make_pair(h, e.keyId))) {
                    auto itUnassigned = mapUnassignedPreModelDCoordPayout.find(h);
                    if (itUnassigned != mapUnassignedPreModelDCoordPayout.end()) {
                        e.nPaidHits++;
                        e.nEarned += itUnassigned->second;
                        if (h > e.nLastHitHeight) e.nLastHitHeight = h;
                        mapUnassignedPreModelDCoordPayout.erase(itUnassigned);
                    }
                }
            }
        }
    }

    // 3. Filter to miners that are either in the active pool OR explicitly registered via PoW-Lock / Coin-Lock / Zero-Coin
    std::vector<MyMinerEntry> vDisplayMiners;
    std::set<CKeyID> seenDisplayKeys;
    int nMyActivePoolCount = 0;
    for (const auto& kv : mapMyMiners) {
        const MyMinerEntry& e = kv.second;
        if (e.inActivePool || e.hasExplicitReg) {
            vDisplayMiners.push_back(e);
            if (!e.keyId.IsNull()) seenDisplayKeys.insert(e.keyId);
            if (!e.payoutKeyId.IsNull()) seenDisplayKeys.insert(e.payoutKeyId);
            if (e.inActivePool) nMyActivePoolCount++;
        }
    }

    // Also include active external RPC miners (cpuminer/ccminer) polling for addresses not yet registered in ADAM
    for (const ExternalAdamMinerInfo& extInfo : GetActiveExternalAdamMiners(65)) {
        if (seenDisplayKeys.count(extInfo.keyId)) continue;
        MyMinerEntry extEntry;
        extEntry.keyId = extInfo.keyId;
        extEntry.isUnregisteredExt = true;
        extEntry.extKind = extInfo.kind;
        for (const auto& pair : pwalletMain->mapWallet) {
            const CWalletTx& wtx = pair.second;
            if (!wtx.IsCoinBase() || wtx.GetDepthInMainChain() <= 0) continue;
            for (const CTxOut& vout : wtx.vout) {
                if (vout.nValue <= 0) continue;
                CTxDestination dest;
                if (ExtractDestination(vout.scriptPubKey, dest)) {
                    const CKeyID* kid = boost::get<CKeyID>(&dest);
                    if (kid && *kid == extInfo.keyId) {
                        extEntry.nPaidHits++;
                        extEntry.nEarned += vout.nValue;
                    }
                }
            }
        }
        vDisplayMiners.push_back(extEntry);
        seenDisplayKeys.insert(extInfo.keyId);
    }

    // Sort: Unregistered External RPC miners at top so active cpuminer/ccminer workers are immediately visible,
    // then Active in pool (MN > Coin-Lock > PoW-Lock > Zero-Coin > Bootstrap), then Earned > Puzzles > CoordSigned
    std::sort(vDisplayMiners.begin(), vDisplayMiners.end(), [](const MyMinerEntry& a, const MyMinerEntry& b) {
        if (a.isUnregisteredExt != b.isUnregisteredExt) return a.isUnregisteredExt > b.isUnregisteredExt;
        if (a.inActivePool != b.inActivePool) return a.inActivePool > b.inActivePool;
        if (a.track != b.track) return a.track > b.track;
        if (a.nEarned != b.nEarned) return a.nEarned > b.nEarned;
        if (a.nPuzzlesSolved != b.nPuzzlesSolved) return a.nPuzzlesSolved > b.nPuzzlesSolved;
        if (a.nCoordSigned != b.nCoordSigned) return a.nCoordSigned > b.nCoordSigned;
        return a.nRegHeight > b.nRegHeight;
    });

    // 4. Compute live committee election for next block (nTipHeight + 1)
    int nNextHeight = nTipHeight + 1;
    uint256 nextSeed = GetAdamSeed(chainActive.Tip());
    std::vector<CPubKey> vElectedMiners, vStandbyMiners;
    CPubKey coordinatorKey;
    SelectAdamNodes(nextSeed, Params().GetConsensus(), vElectedMiners, coordinatorKey, nNextHeight, nullptr, &vStandbyMiners);
    cachedElectedSeatsCount = (int)vElectedMiners.size();
    cachedActiveMnCount = (int)setMNKeys.size();
    updateLiveAdamSolLabel(chainActive.Tip(), nTipHeight, vElectedMiners.size(), (int)setMNKeys.size());

    const auto activeMns = amnodeman.GetActiveMasternodesCopy();
    auto isLocalHotMasternodeKey = [&activeMns, &dip3List](const CPubKey& pk) -> bool {
        if (!pk.IsValid() || activeMns.empty()) {
            return false;
        }
        const dip3::CDeterministicMN* dmn = dip3List.GetMNByOwner(pk);
        const bool hasValidDip3Bls = dmn && !dmn->state.IsRevoked() && dmn->state.blsPubKeyOperator.IsValid();
        for (const auto& activeMn : activeMns) {
            if (activeMn.pubKeyMasternode.IsValid() &&
                (activeMn.pubKeyMasternode == pk ||
                 CXOnlyPubKey(activeMn.pubKeyMasternode) == CXOnlyPubKey(pk))) {
                return true;
            }
            if (hasValidDip3Bls && activeMn.blsKeyMasternode.IsValid() &&
                CBLSPubKey(activeMn.blsKeyMasternode) == dmn->state.blsPubKeyOperator) {
                return true;
            }
        }
        return false;
    };

    int nElectedNowCount = 0;
    int nRemoteActiveCount = 0;
    int nLocalHashingCount = 0;
    int nLocalStoppedCount = 0;
    int nRegExtCount = 0;
    int nUnregExtCount = 0;
    for (const MyMinerEntry& e : vDisplayMiners) {
        if (e.isUnregisteredExt) {
            nUnregExtCount++;
            continue;
        }
        if (!e.pubKey.IsValid()) continue;
        const bool isRemoteMN = (e.track == TRACK_MASTERNODE && !isLocalHotMasternodeKey(e.pubKey));
        const bool isExtActive = !isRemoteMN && (
            IsExternalAdamMinerActive(e.keyId, 65) ||
            (!e.payoutKeyId.IsNull() && IsExternalAdamMinerActive(e.payoutKeyId, 65))
        );
        const bool isRowHashing = isRemoteMN || fGenerating || isExtActive;
        if (e.inActivePool) {
            if (isRemoteMN) {
                nRemoteActiveCount++;
            } else if (isRowHashing) {
                nLocalHashingCount++;
            } else {
                nLocalStoppedCount++;
            }
            if (isExtActive) {
                nRegExtCount++;
            }
        }
        if (e.inActivePool &&
            (e.pubKey == coordinatorKey || std::find(vElectedMiners.begin(), vElectedMiners.end(), e.pubKey) != vElectedMiners.end())) {
            nElectedNowCount++;
        }
    }

    // 5. Update Summary Strip
    QString seatsText;
    if (nLocalStoppedCount == 0) {
        seatsText = tr("%1 Active (%2 Elected)").arg(nMyActivePoolCount).arg(nElectedNowCount);
    } else if (nLocalHashingCount == 0 && nRemoteActiveCount > 0) {
        seatsText = tr("%1 VPS • %2 Stopped").arg(nRemoteActiveCount).arg(nLocalStoppedCount);
    } else if (nLocalHashingCount + nRemoteActiveCount > 0) {
        seatsText = tr("%1 Active • %2 Stopped").arg(nLocalHashingCount + nRemoteActiveCount).arg(nLocalStoppedCount);
    } else {
        seatsText = tr("0 Active (%1 Stopped)").arg(nLocalStoppedCount);
    }
    const int nTotalExtCount = nRegExtCount + nUnregExtCount;
    if (nTotalExtCount > 0) {
        seatsText += tr(" • %1 Ext RPC").arg(nTotalExtCount);
    }
    lblStatMyMinersVal->setText(seatsText);
    const QString seatsTip = tr("<qt><b>Registered Pool Seats:</b> %1<br/>"
                                "• Active Local Miners (CPU / External): %2<br/>"
                                "• Remote VPS Masternodes (24/7): %3<br/>"
                                "• Stopped / Idle Local Seats: %4<br/>"
                                "• External RPC Miners (cpuminer / ccminer): %5 (%6 Reg, %7 Unreg)<br/>"
                                "• Elected in Next Block (#%8): %9</qt>")
        .arg(nMyActivePoolCount)
        .arg(nLocalHashingCount)
        .arg(nRemoteActiveCount)
        .arg(nLocalStoppedCount)
        .arg(nTotalExtCount)
        .arg(nRegExtCount)
        .arg(nUnregExtCount)
        .arg(nNextHeight)
        .arg(nElectedNowCount);
    lblStatMyMinersVal->setToolTip(seatsTip);
    if (lblStatMyMinersTitle) lblStatMyMinersTitle->setToolTip(seatsTip);

    const QString hitsTip = tr("<qt>Total confirmed block, staking, and validator reward payouts received across all addresses in this wallet (including Blocks #1–#%1 bootstrap rewards).</qt>")
        .arg(nAdamStartHeight - 1);
    lblStatHitsVal->setText(tr("%1 hits").arg(nTotalWalletHits));
    lblStatHitsVal->setToolTip(hitsTip);
    if (lblStatHitsTitle) lblStatHitsTitle->setToolTip(hitsTip);

    const QString earnedTip = tr("<qt>Cumulative KRISTA earned from mining, staking, and validator rewards across the entire wallet.<br/>Individual rows below track ADAM-era (Block #%1+) rewards and solved puzzles.</qt>")
        .arg(nAdamStartHeight);
    lblStatEarnedVal->setText(QString("%1 KRISTA").arg(QString::number(nTotalWalletEarned / (double)COIN, 'f', 2)));
    lblStatEarnedVal->setToolTip(earnedTip);
    if (lblStatEarnedTitle) lblStatEarnedTitle->setToolTip(earnedTip);

    const QString immatureTip = tr("<qt>Newly minted mining, staking, Masternode, and LLMQ rewards awaiting 100-block coinbase maturity before becoming spendable.</qt>");
    lblStatImmatureVal->setText(QString("%1 KRISTA").arg(QString::number(nImmatureBalance / (double)COIN, 'f', 2)));
    lblStatImmatureVal->setToolTip(immatureTip);
    if (lblStatImmatureTitle) lblStatImmatureTitle->setToolTip(immatureTip);

    lblRosterSection->setText(tr("My Registered Miner Addresses & Live Block #%1 Committee Status:").arg(nNextHeight));

    // 5b. Check for expiring (funded: <= 240 blocks; Zero-Coin PoW: <= 24 blocks) or recently expired validator locks
    if (lockExpiryBanner && lblLockExpiryWarning && btnRenewLock) {
        const bool fShortTestPeriod = (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) &&
                                      IsModelDActive(nNextHeight);
        const int nFundedWarnThreshold = fShortTestPeriod ? 15 : 240;
        const int nZeroWarnThreshold = fShortTestPeriod ? 8 : 24;

        bool fAnyPendingReg = false;
        bool fPendingCoinLock = false;
        bool fPendingPoW = false;
        int nActiveCoinLockRem = 0;
        int nActiveFundedPoWRem = 0;
        int nActiveZeroPoWRem = 0;
        int nMinExpiringBlocks = INT_MAX;
        MinerTrackType expiringTrack = TRACK_POW_LOCK;
        bool fHasUnspentExpiredFundedLock = false;
        MinerTrackType expiredFundedTrack = TRACK_POW_LOCK;
        bool fHasRecentExpiredZeroSeat = false;

        for (const MyMinerEntry& e : vDisplayMiners) {
            if (e.fRegPending) {
                fAnyPendingReg = true;
                if (e.track == TRACK_COIN_LOCK) fPendingCoinLock = true;
                else if (e.track == TRACK_POW_LOCK || e.track == TRACK_ZEROCOIN) fPendingPoW = true;
            }
            if (e.track != TRACK_COIN_LOCK && e.track != TRACK_POW_LOCK && e.track != TRACK_ZEROCOIN) {
                continue;
            }
            const int nTrackWarnThreshold = (e.track == TRACK_ZEROCOIN) ? nZeroWarnThreshold : nFundedWarnThreshold;
            if (e.inActivePool && e.nUnlockHeight > nTipHeight) {
                const int nRem = e.nUnlockHeight - nTipHeight;
                if (e.track == TRACK_COIN_LOCK && nRem > nActiveCoinLockRem) nActiveCoinLockRem = nRem;
                else if (e.track == TRACK_POW_LOCK && nRem > nActiveFundedPoWRem) nActiveFundedPoWRem = nRem;
                else if (e.track == TRACK_ZEROCOIN && nRem > nActiveZeroPoWRem) nActiveZeroPoWRem = nRem;
                if (nRem <= nTrackWarnThreshold && nRem < nMinExpiringBlocks) {
                    nMinExpiringBlocks = nRem;
                    expiringTrack = e.track;
                }
            } else if (e.hasExplicitReg && e.nUnlockHeight > 0 && e.nUnlockHeight <= nTipHeight) {
                if (e.track == TRACK_ZEROCOIN) {
                    if ((nTipHeight - e.nUnlockHeight) <= 240) {
                        fHasRecentExpiredZeroSeat = true;
                    }
                } else if (!e.fLockSpent && (nTipHeight - e.nUnlockHeight) <= 2880) {
                    fHasUnspentExpiredFundedLock = true;
                    if (e.track == TRACK_COIN_LOCK) expiredFundedTrack = TRACK_COIN_LOCK;
                }
            }
        }

        const int nSpacingSec = std::max(1, (int)Params().GetConsensus().nTargetSpacing);
        CAmount nMaturePoWBal = 0;
        if (pwalletMain) {
            const bool fDualTrackNext = IsAdamDualTrackActive(nNextHeight);
            std::vector<COutput> vCoins;
            pwalletMain->AvailableCoins(&vCoins, nullptr, ALL_COINS, true);
            for (const COutput& out : vCoins) {
                if (out.fSpendable && (!fDualTrackNext || out.nDepth >= ADAM_POW_LOCK_MIN_INPUT_DEPTH)) {
                    nMaturePoWBal += out.Value();
                }
            }
        }

        const bool fLocked = pwalletMain && pwalletMain->IsLocked();
        const size_t nMaxBootstrapSeats = (size_t)(Params().GetConsensus().nAdamMinersCount + 2);
        const size_t nFundedPoolNow = GetFundedAdamMinerPoolCount(nTipHeight);
        const bool fOpenEntryActive = Params().GetConsensus().IsAdamOpenEntryActive(nTipHeight + 1);
        const bool fZeroBootstrapOpen = fOpenEntryActive || (nFundedPoolNow < nMaxBootstrapSeats);
        const CAmount availBal = pwalletMain->GetAvailableBalance();

        if (lblTrackCoinLockMeta) {
            if (nActiveCoinLockRem > 0) {
                double remHrs = (nActiveCoinLockRem * (double)nSpacingSec) / 3600.0;
                lblTrackCoinLockMeta->setText(tr("Active in wallet: %1 blocks (~%2h) remaining").arg(nActiveCoinLockRem).arg(QString::number(remHrs, 'f', 1)));
            } else if (nMaturePoWBal >= MINER_REGISTRATION_LOCK_AMOUNT + (COIN / 100)) {
                lblTrackCoinLockMeta->setText(tr("Ready: 250+ mature KRISTA available"));
            } else {
                lblTrackCoinLockMeta->setText(tr("Requires >= 250.01 mature KRISTA (100+ conf)"));
            }
        }

        if (lblTrackPoWLockMeta) {
            if (nActiveFundedPoWRem > 0) {
                double remHrs = (nActiveFundedPoWRem * (double)nSpacingSec) / 3600.0;
                lblTrackPoWLockMeta->setText(tr("Active in wallet: %1 blocks (~%2h) remaining").arg(nActiveFundedPoWRem).arg(QString::number(remHrs, 'f', 1)));
            } else if (nMaturePoWBal >= POW_LOCK_REGISTRATION_AMOUNT + (COIN / 100)) {
                lblTrackPoWLockMeta->setText(tr("Ready: 10+ mature KRISTA available"));
            } else {
                lblTrackPoWLockMeta->setText(tr("Requires >= 10.01 mature KRISTA (100+ conf)"));
            }
        }

        if (lblTrackZeroPoWMeta) {
            if (nActiveZeroPoWRem > 0) {
                lblTrackZeroPoWMeta->setText(tr("Active Zero-PoW lease: %1 blocks remaining").arg(nActiveZeroPoWRem));
            } else if (fOpenEntryActive) {
                lblTrackZeroPoWMeta->setText(tr("OPEN-ENTRY ACTIVE — 1 permanent 0-KRISTA seat (Funded Pool: %1)").arg(nFundedPoolNow));
            } else if (fZeroBootstrapOpen) {
                lblTrackZeroPoWMeta->setText(tr("OPEN — Funded Pool %1 / %2 (< %2)").arg(nFundedPoolNow).arg(nMaxBootstrapSeats));
            } else {
                lblTrackZeroPoWMeta->setText(tr("CLOSED — Funded Pool %1 / %2 (>= %2)").arg(nFundedPoolNow).arg(nMaxBootstrapSeats));
            }
        }

        if (btnRegCoinLock) {
            if (fPendingCoinLock) {
                btnRegCoinLock->setEnabled(false);
                btnRegCoinLock->setText(tr("Coin-Lock Pending..."));
            } else if (nActiveCoinLockRem > nFundedWarnThreshold) {
                btnRegCoinLock->setEnabled(false);
                btnRegCoinLock->setText(tr("Coin-Lock Active (%1b left)").arg(nActiveCoinLockRem));
            } else if (nActiveCoinLockRem > 0) {
                btnRegCoinLock->setEnabled(true);
                btnRegCoinLock->setText(tr("Renew Coin-Lock (250 KRISTA)"));
            } else {
                btnRegCoinLock->setEnabled(nMaturePoWBal >= MINER_REGISTRATION_LOCK_AMOUNT + (COIN / 100));
                btnRegCoinLock->setText(tr("Register Coin-Lock (250 KRISTA)"));
            }
        }

        if (btnRegPoW && !isSolvingPoWReg.load()) {
            const bool hasMature10 = (nMaturePoWBal >= POW_LOCK_REGISTRATION_AMOUNT + (COIN / 100));
            if (fPendingPoW) {
                btnRegPoW->setEnabled(false);
                btnRegPoW->setText(tr("PoW Seat Pending..."));
            } else if (nActiveFundedPoWRem > nFundedWarnThreshold) {
                btnRegPoW->setEnabled(false);
                btnRegPoW->setText(tr("PoW-Lock Active (%1b left)").arg(nActiveFundedPoWRem));
            } else if (nActiveZeroPoWRem > nZeroWarnThreshold) {
                btnRegPoW->setEnabled(hasMature10);
                btnRegPoW->setText(tr("Upgrade to PoW-Lock (10 KRISTA)"));
            } else if (nActiveFundedPoWRem > 0) {
                btnRegPoW->setEnabled(hasMature10);
                btnRegPoW->setText(tr("Renew PoW-Lock (10 KRISTA)"));
            } else {
                btnRegPoW->setEnabled(hasMature10);
                btnRegPoW->setText(tr("Register PoW-Lock (10 KRISTA)"));
            }
        }

        if (btnRegZeroPoW && !isSolvingPoWReg.load()) {
            const bool hasMature10 = (nMaturePoWBal >= POW_LOCK_REGISTRATION_AMOUNT + (COIN / 100));
            if (fPendingPoW) {
                btnRegZeroPoW->setEnabled(false);
                btnRegZeroPoW->setText(tr("PoW Seat Pending..."));
            } else if (nActiveFundedPoWRem > nFundedWarnThreshold) {
                btnRegZeroPoW->setEnabled(false);
                btnRegZeroPoW->setText(tr("Funded PoW-Lock Active"));
            } else if (nActiveZeroPoWRem > nZeroWarnThreshold) {
                btnRegZeroPoW->setEnabled(false);
                btnRegZeroPoW->setText(tr("Zero-PoW Active (%1b left)").arg(nActiveZeroPoWRem));
            } else if (!fZeroBootstrapOpen) {
                btnRegZeroPoW->setEnabled(false);
                btnRegZeroPoW->setText(tr("Bootstrap Closed (Pool >= %1)").arg(nMaxBootstrapSeats));
            } else if (hasMature10) {
                btnRegZeroPoW->setEnabled(false);
                btnRegZeroPoW->setText(tr("Use Funded PoW-Lock (10+ Ready)"));
            } else if (nActiveZeroPoWRem > 0) {
                btnRegZeroPoW->setEnabled(true);
                btnRegZeroPoW->setText(tr("Renew Zero-Coin PoW (0 KRISTA)"));
            } else {
                btnRegZeroPoW->setEnabled(true);
                btnRegZeroPoW->setText(tr("Register Zero-Coin PoW (0 KRISTA)"));
            }
        }

        if (!fAnyPendingReg && nMinExpiringBlocks < INT_MAX) {
            const int nMins = std::max(1, (nMinExpiringBlocks * nSpacingSec) / 60);
            QString trackName;
            if (expiringTrack == TRACK_COIN_LOCK) {
                trackName = tr("Coin-Lock (250 KRISTA)");
            } else if (expiringTrack == TRACK_POW_LOCK) {
                trackName = tr("PoW-Lock (10 KRISTA)");
            } else {
                trackName = tr("Zero-PoW Seat");
            }
            pendingRenewTrack = (expiringTrack == TRACK_COIN_LOCK) ? 2 : 1;
            if (lblLockExpiryIcon) {
                lblLockExpiryIcon->setPixmap(MakeMinerHubVectorPixmap(MinerHubGlyph::Clock, QColor("#C94A3E"), 14));
            }
            lblLockExpiryWarning->setText(tr("%1 expires in %2 blocks (~%3 min) — renew to keep your validator seat active.")
                                              .arg(trackName).arg(nMinExpiringBlocks).arg(nMins));
            updateRenewButtonAppearance(fLocked);
            lockExpiryBanner->setVisible(true);
        } else if (!fAnyPendingReg && nMyActivePoolCount == 0 && fHasUnspentExpiredFundedLock) {
            pendingRenewTrack = (expiredFundedTrack == TRACK_COIN_LOCK && availBal >= MINER_REGISTRATION_LOCK_AMOUNT + 1 * CENT) ? 2 : 1;
            if (lblLockExpiryIcon) {
                lblLockExpiryIcon->setPixmap(MakeMinerHubVectorPixmap(MinerHubGlyph::Unlock, QColor("#C94A3E"), 14));
            }
            lblLockExpiryWarning->setText(tr("Your validator lock has expired (coins unlocked in wallet) — renew to rejoin the ADAM committee."));
            updateRenewButtonAppearance(fLocked);
            lockExpiryBanner->setVisible(true);
        } else if (!fAnyPendingReg && nMyActivePoolCount == 0 && fHasRecentExpiredZeroSeat &&
                   (fZeroBootstrapOpen || availBal >= POW_LOCK_REGISTRATION_AMOUNT + 1 * CENT)) {
            pendingRenewTrack = 1;
            if (lblLockExpiryIcon) {
                lblLockExpiryIcon->setPixmap(MakeMinerHubVectorPixmap(MinerHubGlyph::Clock, QColor("#C94A3E"), 14));
            }
            lblLockExpiryWarning->setText(tr("Your Zero-PoW bootstrap seat lease has expired — renew to rejoin the ADAM committee."));
            updateRenewButtonAppearance(fLocked);
            lockExpiryBanner->setVisible(true);
        } else {
            lockExpiryBanner->setVisible(false);
        }
    }

    // Avoid tearing down tableMyMiners cell items while the user is actively inspecting a tooltip
    if (tableMyMiners && tableMyMiners->underMouse() && QToolTip::isVisible()) {
        fForceMinerTableRefresh = true;
        return;
    }

    // 6. Populate tableMyMiners
    const int savedScrollPos = tableMyMiners->verticalScrollBar() ? tableMyMiners->verticalScrollBar()->value() : 0;
    tableMyMiners->setSortingEnabled(false);
    tableMyMiners->setRowCount((int)vDisplayMiners.size());
    const bool light = isLightTheme();
    const QColor mutedRowColor = light ? QColor("#64748B") : QColor("#B0C0D6");
    const QString copyBtnCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; color: %5; border-color: %6; }"
    ).arg(light ? "#EEF3FA" : "#213458",
          light ? "#3E5B94" : "#F8FAFC",
          light ? "#B8CBE8" : "#4A6A9E",
          light ? "#5576B2" : "#5576B2",
          "#FFFFFF",
          light ? "#4A679E" : "#F5897C");

    for (size_t row = 0; row < vDisplayMiners.size(); ++row) {
        const MyMinerEntry& e = vDisplayMiners[row];
        const bool isRemoteMN = (e.track == TRACK_MASTERNODE && !isLocalHotMasternodeKey(e.pubKey));
        const bool isExtActive = !isRemoteMN && (
            IsExternalAdamMinerActive(e.keyId, 65) ||
            (!e.payoutKeyId.IsNull() && IsExternalAdamMinerActive(e.payoutKeyId, 65))
        );
        ExternalMinerKind activeExtKind = ExternalMinerKind::UNKNOWN;
        if (isExtActive) {
            activeExtKind = GetExternalAdamMinerKind(e.keyId, 65);
            if (activeExtKind == ExternalMinerKind::UNKNOWN && !e.payoutKeyId.IsNull()) {
                activeExtKind = GetExternalAdamMinerKind(e.payoutKeyId, 65);
            }
        }
        int64_t extAgeSecs = -1;
        if (isExtActive || e.isUnregisteredExt) {
            extAgeSecs = GetExternalAdamMinerAgeSecs(e.keyId, 65);
            if (extAgeSecs < 0 && !e.payoutKeyId.IsNull()) {
                extAgeSecs = GetExternalAdamMinerAgeSecs(e.payoutKeyId, 65);
            }
        }
        const QString extLabel = (activeExtKind == ExternalMinerKind::CPUMINER)
            ? tr("Ext (cpuminer)")
            : ((activeExtKind == ExternalMinerKind::CCMINER) ? tr("Ext (ccminer)") : tr("Ext"));
        const bool isRowHashing = isRemoteMN || fGenerating || isExtActive;
        const bool isLocalPaused = e.inActivePool && !isRowHashing;

        CKeyID displayKeyId = (!e.payoutKeyId.IsNull()) ? e.payoutKeyId : e.keyId;
        QString fullAddr = QString::fromStdString(EncodeDestination(CTxDestination(displayKeyId)));
        QString operatorAddr = QString::fromStdString(EncodeDestination(CTxDestination(e.keyId)));
        QString shortAddr = fullAddr.left(8) + "..." + fullAddr.right(6);
        QString addrLabel;
        if (pwalletMain) {
            LOCK(pwalletMain->cs_wallet);
            auto itLabel = pwalletMain->mapAddressBook.find(CTxDestination(displayKeyId));
            if (itLabel != pwalletMain->mapAddressBook.end() && !itLabel->second.name.empty()) {
                addrLabel = QString::fromStdString(itLabel->second.name);
            }
        }
        const QString displayAddr = addrLabel.isEmpty()
            ? shortAddr
            : QString("%1 • %2").arg(addrLabel.left(14), shortAddr);

        // Col 0: Shortened Miner Address (+ Label if set; full address in tooltip & Copy button)
        QTableWidgetItem* itemAddr = new MinerTableItem(displayAddr);
        if (!e.payoutKeyId.IsNull() && e.payoutKeyId != e.keyId) {
            itemAddr->setToolTip(tr("<qt><b>Label:</b> %1<br/><b>Collateral Payout Address:</b> %2<br/><b>ADAM Operator Key Address:</b> %3</qt>")
                .arg(addrLabel.isEmpty() ? tr("(none)") : addrLabel.toHtmlEscaped(), fullAddr, operatorAddr));
        } else {
            itemAddr->setToolTip(tr("<qt><b>Label:</b> %1<br/><b>Full Miner Address:</b> %2</qt>")
                .arg(addrLabel.isEmpty() ? tr("(none)") : addrLabel.toHtmlEscaped(), fullAddr));
        }
        if (isLocalPaused) {
            itemAddr->setForeground(QBrush(mutedRowColor));
        }
        tableMyMiners->setItem((int)row, 0, itemAddr);

        // Col 1: Track & Remaining Lock
        QString trackStr;
        MinerHubGlyph trackGlyph = MinerHubGlyph::ZeroSpark;
        QColor trackIconColor = light ? QColor("#475569") : QColor("#CBD5E1");
        const bool isExpiredRow = (e.hasExplicitReg && e.nUnlockHeight > 0 && e.nUnlockHeight <= nTipHeight) ||
                                  (!e.inActivePool && e.nUnlockHeight <= nTipHeight);
        if (e.isUnregisteredExt) {
            if (e.extKind == ExternalMinerKind::CPUMINER) {
                trackStr = tr("cpuminer (CPU)");
                trackIconColor = QColor(light ? "#5576B2" : "#93C5FD");
            } else if (e.extKind == ExternalMinerKind::CCMINER) {
                trackStr = tr("ccminer (GPU)");
                trackIconColor = QColor(light ? "#C94A3E" : "#FCA5A5");
            } else {
                trackStr = tr("External RPC");
                trackIconColor = QColor(light ? "#5576B2" : "#93C5FD");
            }
            trackGlyph = MinerHubGlyph::CpuBolt;
        } else if (e.track == TRACK_COIN_LOCK) {
            trackStr = tr("Lock 250");
            trackGlyph = isExpiredRow ? MinerHubGlyph::Unlock : MinerHubGlyph::LockClosed;
            trackIconColor = isExpiredRow ? mutedRowColor : QColor(light ? "#D96558" : "#FFA296");
        } else if (e.track == TRACK_POW_LOCK) {
            trackStr = tr("PoW 10");
            trackGlyph = MinerHubGlyph::CpuBolt;
            trackIconColor = isExpiredRow ? mutedRowColor : QColor(light ? "#5B7BB8" : "#93C5FD");
        } else if (e.track == TRACK_MASTERNODE) {
            trackStr = e.mnAlias.isEmpty() ? tr("MN 4200") : tr("%1 (4200)").arg(e.mnAlias);
            trackGlyph = MinerHubGlyph::ServerNode;
            trackIconColor = QColor(light ? "#5B7BB8" : "#93C5FD");
        } else if (e.track == TRACK_ZEROCOIN) {
            trackStr = tr("Zero-PoW");
            trackGlyph = MinerHubGlyph::ZeroSpark;
            trackIconColor = isExpiredRow ? mutedRowColor : QColor(light ? "#475569" : "#CBD5E1");
        } else {
            trackStr = tr("Bootstrap");
            trackGlyph = MinerHubGlyph::ZeroSpark;
            trackIconColor = isExpiredRow ? mutedRowColor : QColor(light ? "#475569" : "#CBD5E1");
        }

        const double blockSpacingSec = std::max(1.0, (double)Params().GetConsensus().nTargetSpacing);
        QString lockStatusStr;
        qlonglong trackSortKey = 0;
        const int nRemBlocks = (e.nUnlockHeight > nTipHeight) ? (e.nUnlockHeight - nTipHeight) : 0;
        if (e.isUnregisteredExt) {
            lockStatusStr = (extAgeSecs >= 0) ? tr(" • %1s ago").arg(extAgeSecs) : tr(" • Polling");
            trackSortKey = 1;
        } else if (e.fRegPending && !e.inActivePool) {
            double hours = (nRemBlocks * blockSpacingSec) / 3600.0;
            lockStatusStr = tr(" • %1b (~%2h) (Pending)").arg(nRemBlocks).arg(QString::number(hours, 'f', 1));
            trackSortKey = nRemBlocks;
        } else if (e.nUnlockHeight > nTipHeight) {
            double hours = (nRemBlocks * blockSpacingSec) / 3600.0;
            const int pct = std::min(100, std::max(0, (int)std::lround((nRemBlocks * 100.0) / 2880.0)));
            lockStatusStr = tr(" • %1b (%2%, ~%3h)").arg(nRemBlocks).arg(pct).arg(QString::number(hours, 'f', 1));
            if (!e.inActivePool) {
                lockStatusStr += tr(" (Standby)");
            }
            trackSortKey = nRemBlocks;
        } else if (e.hasExplicitReg && e.nUnlockHeight > 0) {
            lockStatusStr = e.fLockSpent ? tr(" • Spent (Expired)") : tr(" • Unlocked (Expired)");
            trackSortKey = -1;
        } else if (e.inActivePool) {
            lockStatusStr = isRemoteMN ? tr(" • VPS") : (isRowHashing ? tr(" • Active") : tr(" • Stopped"));
            trackSortKey = (e.track == TRACK_MASTERNODE) ? 999999 : 500000;
        } else {
            lockStatusStr = tr(" (Expired)");
            trackSortKey = -1;
        }

        QTableWidgetItem* itemTrack = new MinerTableItem(QIcon(MakeMinerHubVectorPixmap(trackGlyph, trackIconColor, 15)), trackStr + lockStatusStr);
        itemTrack->setData(Qt::UserRole + 10, trackSortKey);
        if (e.isUnregisteredExt) {
            itemTrack->setToolTip(tr("<qt><b>External RPC Miner (Unregistered):</b> Actively polling <code>getblocktemplate</code> / <code>getadammininginfo</code> for this address.<br/>Register this address as <b>Zero-PoW</b> or <b>PoW-Lock</b> to enter the ADAM committee.</qt>"));
        } else {
            itemTrack->setToolTip(tr("<qt><b>Registration Height:</b> %1<br/><b>Unlock Height:</b> %2<br/><b>In Active Network Roster:</b> %3<br/><b>Execution Mode:</b> %4</qt>")
                .arg(e.nRegHeight >= 0 ? QString("#%1").arg(e.nRegHeight) : tr("Blocks #1–#%1 Coinbase Fallback").arg(nAdamStartHeight - 1))
                .arg(e.nUnlockHeight > 0 ? QString("#%1").arg(e.nUnlockHeight)
                                         : tr("Auto-replaced when explicit pool &gt;= %1 or Block #%2")
                                               .arg(Params().GetConsensus().nAdamMinersCount + 2)
                                               .arg(Params().GetConsensus().nAdamBootstrapLimit))
                .arg(e.inActivePool ? tr("Yes") : tr("No"))
                .arg(isRemoteMN ? tr("Remote VPS Masternode (24/7)") : (isExtActive ? tr("External RPC Miner (%1)").arg(extLabel) : (fGenerating ? tr("Local 1-Click CPU Miner") : tr("Stopped (Idle)")))));
        }
        if (isLocalPaused) {
            itemTrack->setForeground(QBrush(mutedRowColor));
        }
        tableMyMiners->setItem((int)row, 1, itemTrack);

        // Col 2: Live Committee Role for Block #(nTipHeight + 1)
        QString roleStr = "—";
        QString roleTooltip;
        QColor roleColor = light ? QColor("#475569") : QColor("#CBD5E1");
        const QColor colCoord(light ? "#B45309" : "#FBBF24");
        const QColor colSeatLocal(light ? "#047857" : "#4ADE80");
        const QColor colSeatExt(light ? "#1D4ED8" : "#7DD3FC");
        const QColor colUnregCpu(light ? "#3E5B94" : "#93C5FD");
        const QColor colUnregGpu(light ? "#C94A3E" : "#FCA5A5");

        if (e.isUnregisteredExt) {
            const QString kindName = (e.extKind == ExternalMinerKind::CPUMINER)
                ? tr("cpuminer (CPU)")
                : ((e.extKind == ExternalMinerKind::CCMINER) ? tr("ccminer (GPU)") : tr("Ext"));
            const bool fPreAdam = (nNextHeight < nAdamStartHeight);
            roleStr = fPreAdam
                ? tr("● %1 • Unregistered (X11KVS)").arg(kindName)
                : tr("● %1 • Unregistered").arg(kindName);
            roleColor = (e.extKind == ExternalMinerKind::CCMINER) ? colUnregGpu : colUnregCpu;
            roleTooltip = tr("<qt><b>External RPC Miner Connected (%1):</b> Actively polling RPC for this address, not yet registered in an ADAM track.</qt>").arg(kindName);
        } else if (e.pubKey.IsValid() && e.inActivePool) {
            if (e.pubKey == coordinatorKey) {
                if (isRemoteMN) {
                    roleStr = tr("● VPS • ★ COORD (X11KVS)");
                    roleColor = colCoord;
                    roleTooltip = tr("<qt><b>Remote Masternode Coordinator:</b> Running 24/7 on its remote VPS server (independent of local wallet CPU mining).</qt>");
                } else if (isExtActive) {
                    roleStr = tr("● %1 • ★ COORD (X11KVS)").arg(extLabel);
                    roleColor = colCoord;
                    roleTooltip = tr("<qt><b>External Miner Active (%1):</b> Local wallet is coordinating Block #%2 while external RPC miner solves puzzles.</qt>").arg(extLabel).arg(nNextHeight);
                } else if (fGenerating) {
                    roleStr = tr("★ COORD (X11KVS)");
                    roleColor = colCoord;
                    roleTooltip = tr("<qt><b>Active Local Coordinator:</b> Assembling and signing Block #%1 via local CPU mining.</qt>").arg(nNextHeight);
                } else {
                    roleStr = tr("Stopped (COORD idle)");
                    roleColor = mutedRowColor;
                    roleTooltip = tr("<qt><b>Local Mining Stopped:</b> This seat is elected Coordinator for Block #%1, but local CPU &amp; external mining are currently stopped.</qt>").arg(nNextHeight);
                }
            } else {
                auto itEl = std::find(vElectedMiners.begin(), vElectedMiners.end(), e.pubKey);
                if (itEl != vElectedMiners.end()) {
                    int seatIdx = (int)std::distance(vElectedMiners.begin(), itEl) + 1;
                    QString algoText;
                    if (nNextHeight < nAdamStartHeight) {
                        algoText = QStringLiteral("X11KVS");
                    } else if (Params().GetConsensus().NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
                        int a1 = 0, a2 = 0, a3 = 0;
                        GetAdam3PermutationAlgos(chainActive.Tip()->GetBlockHash(), e.pubKey, a1, a2, a3);
                        algoText = QString("%1→%2→%3")
                            .arg(QString::fromStdString(GetAdamPuzzleAlgoName(a1)).left(5))
                            .arg(QString::fromStdString(GetAdamPuzzleAlgoName(a2)).left(5))
                            .arg(QString::fromStdString(GetAdamPuzzleAlgoName(a3)).left(5));
                    } else {
                        int algoIdx = GetAdamPuzzleAlgo(nextSeed, e.pubKey, true);
                        algoText = QString::fromStdString(GetAdamPuzzleAlgoName(algoIdx));
                    }
                    if (isRemoteMN) {
                        roleStr = tr("● VPS • Seat #%1 (%2)").arg(seatIdx).arg(algoText);
                        roleColor = colSeatLocal;
                        roleTooltip = tr("<qt><b>Remote Masternode Validator (Seat #%1):</b> Actively solving <code>%2</code> on its remote VPS server (24/7).</qt>")
                            .arg(seatIdx).arg(algoText);
                    } else if (isExtActive) {
                        roleStr = tr("● %1 • Seat #%2 (%3)").arg(extLabel).arg(seatIdx).arg(algoText);
                        roleColor = colSeatExt;
                        roleTooltip = tr("<qt><b>External RPC Miner Active (%1, Seat #%2):</b> Solving <code>%3</code> via external RPC miner.</qt>")
                            .arg(extLabel).arg(seatIdx).arg(algoText);
                    } else if (fGenerating) {
                        roleStr = tr("● Seat #%1 (%2)").arg(seatIdx).arg(algoText);
                        roleColor = colSeatLocal;
                        roleTooltip = tr("<qt><b>Active Local Validator (Seat #%1):</b> Solving <code>%2</code> with local CPU mining threads.</qt>")
                            .arg(seatIdx).arg(algoText);
                    } else {
                        roleStr = tr("Stopped (Seat #%1 • %2)").arg(seatIdx).arg(algoText);
                        roleColor = mutedRowColor;
                        roleTooltip = tr("<qt><b>Local Mining Stopped:</b> Elected for <b>Seat #%1</b> (<code>%2</code>), but no local CPU or external RPC miner is running.<br/>Click <b>Start 1-Click Mining</b> or launch <code>cpuminer</code> to solve puzzles and earn rewards.</qt>")
                            .arg(seatIdx).arg(algoText);
                    }
                } else {
                    auto itSt = std::find(vStandbyMiners.begin(), vStandbyMiners.end(), e.pubKey);
                    if (itSt != vStandbyMiners.end()) {
                        int standbyRank = (int)std::distance(vStandbyMiners.begin(), itSt) + 1;
                        if (isRemoteMN) {
                            roleStr = tr("● VPS • Standby #%1").arg(standbyRank);
                            roleTooltip = tr("<qt><b>Remote Masternode Standby (#%1):</b> Ready on remote VPS if a primary Masternode seat times out (&gt;=60s).</qt>").arg(standbyRank);
                        } else if (isExtActive) {
                            roleStr = tr("● %1 • Standby #%2").arg(extLabel).arg(standbyRank);
                            roleColor = colSeatExt;
                            roleTooltip = tr("<qt><b>External Miner Standby (%1, #%2):</b> Polling via external RPC miner; ready if a primary seat times out (&gt;=60s).</qt>").arg(extLabel).arg(standbyRank);
                        } else if (fGenerating) {
                            roleStr = tr("Standby #%1").arg(standbyRank);
                            roleTooltip = tr("<qt><b>Active Local Standby (#%1):</b> Ready to step in if a primary seat in your track times out (&gt;=60s).</qt>").arg(standbyRank);
                        } else {
                            roleStr = tr("Stopped (Standby #%1)").arg(standbyRank);
                            roleColor = mutedRowColor;
                            roleTooltip = tr("<qt><b>Local Mining Stopped:</b> Ranked Standby #%1 for Block #%2, currently idle because local mining is stopped.</qt>")
                                .arg(standbyRank).arg(nNextHeight);
                        }
                    }
                }
            }
        }
        if (extAgeSecs >= 0 && (isExtActive || e.isUnregisteredExt)) {
            roleStr += tr(" • %1s ago").arg(extAgeSecs);
        }
        QTableWidgetItem* itemRole = new MinerTableItem(roleStr);
        const qlonglong roleSort = roleStr.startsWith(tr("Stopped")) ? 1
            : (roleStr.contains(tr("COORD")) ? 4
            : (roleStr.contains(tr("Seat")) ? 3
            : (roleStr.contains(tr("Standby")) || roleStr.contains(tr("Unregistered")) ? 2 : 0)));
        itemRole->setData(Qt::UserRole + 10, roleSort);
        itemRole->setForeground(QBrush(roleColor));
        QFont roleFont = itemRole->font();
        roleFont.setWeight(QFont::DemiBold);
        itemRole->setFont(roleFont);
        if (!roleTooltip.isEmpty()) {
            itemRole->setToolTip(roleTooltip);
        }
        tableMyMiners->setItem((int)row, 2, itemRole);

        // Col 3: Hits & Earned (ADAM Paid Rewards + Solved Committee Puzzles + V11 Coordinator Co-Sigs)
        QString hitsStr;
        if (e.isUnregisteredExt) {
            hitsStr = (e.nPaidHits > 0)
                ? tr("%1 hits • %2 KRISTA (RPC only)")
                      .arg(e.nPaidHits)
                      .arg(QString::number(e.nEarned / (double)COIN, 'f', 2))
                : tr("Unregistered (RPC only)");
        } else {
            hitsStr = (e.nCoordSigned > 0)
                ? tr("%1 hits • %2 KRISTA (%3 puzzles, ★%4)")
                      .arg(e.nPaidHits)
                      .arg(QString::number(e.nEarned / (double)COIN, 'f', 2))
                      .arg(e.nPuzzlesSolved)
                      .arg(e.nCoordSigned)
                : tr("%1 hits • %2 KRISTA (%3 puzzles)")
                      .arg(e.nPaidHits)
                      .arg(QString::number(e.nEarned / (double)COIN, 'f', 2))
                      .arg(e.nPuzzlesSolved);
        }
        QTableWidgetItem* itemHits = new MinerTableItem(hitsStr);
        itemHits->setData(Qt::UserRole + 10, static_cast<qlonglong>(e.nEarned));
        QString hitsTooltip;
        if (e.isUnregisteredExt) {
            hitsTooltip = tr("<qt>Register this address as <b>Zero-PoW</b> or <b>PoW-Lock</b> to join the ADAM committee and earn puzzle rewards.</qt>");
        } else {
            hitsTooltip = tr("<qt><b>Paid Rewards Won (ADAM Era, Block #%1+):</b> %2<br/>"
                                 "<b>Total Earned by Miner:</b> %3 KRISTA<br/>"
                                 "<b>Committee Puzzles Solved:</b> %4<br/>"
                                 "<b>Coordinator Blocks Co-Signed (Pre-Model-D V11/V12):</b> %5<br/>"
                                 "<b>Last Paid:</b> %6 • <b>Last Puzzle:</b> %7 • <b>Last Coord:</b> %8")
                .arg(nAdamStartHeight)
                .arg(e.nPaidHits)
                .arg(QString::number(e.nEarned / (double)COIN, 'f', 8))
                .arg(e.nPuzzlesSolved)
                .arg(e.nCoordSigned)
                .arg(e.nLastHitHeight >= 0 ? QString("#%1").arg(e.nLastHitHeight) : tr("None yet"))
                .arg(e.nLastPuzzleHeight >= 0 ? QString("#%1").arg(e.nLastPuzzleHeight) : tr("None yet"))
                .arg(e.nLastCoordHeight >= 0 ? QString("#%1").arg(e.nLastCoordHeight) : tr("None yet"));
            if (!IsModelDActive(nTipHeight)) {
                if (IsAdamValidatorSplitActive(nTipHeight)) {
                    hitsTooltip += "<br/><br/>" + tr("<b>Note:</b> During Blocks #%1–#%2, 40% of the net block reward is paid directly to active committee validators "
                                                     "and 60% goes to the Block Producer (Coordinator / Staker). Full Model D (35% Passive MN + 10% LLMQ + 40% Validators + 15% Producer) activates at Block #%3.")
                        .arg(nAdamStartHeight)
                        .arg(nModelDStartHeight - 1)
                        .arg(nModelDStartHeight);
                } else {
                    hitsTooltip += "<br/><br/>" + tr("<b>Note:</b> Individual committee seat payouts (40% split) activate at Model D (Block #%1). "
                                                     "During Blocks #%2–#%3, 100% of the net block reward goes to the Block Producer (Coordinator / Staker).")
                        .arg(nModelDStartHeight)
                        .arg(nAdamStartHeight)
                        .arg(nModelDStartHeight - 1);
                }
            }
            hitsTooltip += "</qt>";
        }
        itemHits->setToolTip(hitsTooltip);
        if (isLocalPaused) {
            itemHits->setForeground(QBrush(mutedRowColor));
        }
        tableMyMiners->setItem((int)row, 3, itemHits);

        const bool isElected = roleStr.contains(tr("Seat")) || roleStr.contains(tr("COORD"));
        const int trackFilterCode = e.isUnregisteredExt
            ? 99
            : (e.track == TRACK_BOOTSTRAP ? static_cast<int>(TRACK_ZEROCOIN) : static_cast<int>(e.track));
        const QString searchBlob = QString("%1 %2 %3 %4 %5 %6")
            .arg(addrLabel, fullAddr, operatorAddr, e.mnAlias, trackStr, roleStr);
        itemAddr->setData(Qt::UserRole + 1, isExpiredRow);
        itemAddr->setData(Qt::UserRole + 2, isElected);
        itemAddr->setData(Qt::UserRole + 3, isExtActive || e.isUnregisteredExt);
        itemAddr->setData(Qt::UserRole + 4, isRemoteMN);
        itemAddr->setData(Qt::UserRole + 5, trackFilterCode);
        itemAddr->setData(Qt::UserRole + 6, searchBlob);

        // Col 4: 1-Click Copy Address & External Miner Command Buttons
        QWidget* actionWidget = new QWidget(tableMyMiners);
        actionWidget->setObjectName("minerActionCell");
        actionWidget->setStyleSheet("QWidget#minerActionCell { background-color: transparent; border: none; }");
        QHBoxLayout* actionLayout = new QHBoxLayout(actionWidget);
        actionLayout->setContentsMargins(2, 1, 6, 1);
        actionLayout->setSpacing(3);

        QPushButton* btnCopyAddr = new QPushButton("📋", actionWidget);
        btnCopyAddr->setFixedSize(26, 22);
        btnCopyAddr->setCursor(Qt::PointingHandCursor);
        btnCopyAddr->setFocusPolicy(Qt::NoFocus);
        btnCopyAddr->setStyleSheet(copyBtnCss);
        btnCopyAddr->setToolTip(tr("Copy miner address (%1) to clipboard").arg(fullAddr));
        connect(btnCopyAddr, &QPushButton::clicked, this, [this, fullAddr]() {
            QApplication::clipboard()->setText(fullAddr);
            inform(tr("Copied miner address: %1").arg(fullAddr));
        });

        QPushButton* btnCopyCmd = new QPushButton("💻", actionWidget);
        btnCopyCmd->setFixedSize(26, 22);
        btnCopyCmd->setCursor(Qt::PointingHandCursor);
        btnCopyCmd->setFocusPolicy(Qt::NoFocus);
        btnCopyCmd->setStyleSheet(copyBtnCss);
        btnCopyCmd->setToolTip(tr("Copy ready-to-run external cpuminer command for operator key %1").arg(operatorAddr));
        connect(btnCopyCmd, &QPushButton::clicked, this, [this, operatorAddr]() {
            int rpcPort = (int)GetArg("-rpcport", BaseParams().RPCPort());
            QString rpcUser = QString::fromStdString(GetArg("-rpcuser", "<RPC_USER>"));
            QString cmd = QString("./cpuminer -a adam -o http://127.0.0.1:%1 -u %2 -p <RPC_PASSWORD> --no-stratum --no-longpoll --coinbase-addr=%3 -t 1")
                .arg(rpcPort).arg(rpcUser, operatorAddr);
            QApplication::clipboard()->setText(cmd);
            inform(tr("Copied cpuminer command for %1 (replace <RPC_PASSWORD>)!").arg(operatorAddr.left(10) + "..."));
        });

        QPushButton* btnEditLabel = new QPushButton("✎", actionWidget);
        btnEditLabel->setFixedSize(26, 22);
        btnEditLabel->setCursor(Qt::PointingHandCursor);
        btnEditLabel->setFocusPolicy(Qt::NoFocus);
        btnEditLabel->setStyleSheet(copyBtnCss);
        btnEditLabel->setToolTip(tr("Set or edit a custom alias/label for %1").arg(fullAddr));
        connect(btnEditLabel, &QPushButton::clicked, this, [this, displayKeyId, fullAddr, addrLabel]() {
            bool ok = false;
            const QString newLabel = QInputDialog::getText(
                this,
                tr("Miner Label"),
                tr("Label for %1:").arg(fullAddr),
                QLineEdit::Normal,
                addrLabel,
                &ok);
            if (ok && pwalletMain) {
                LOCK(pwalletMain->cs_wallet);
                pwalletMain->SetAddressBook(CTxDestination(displayKeyId), newLabel.trimmed().toStdString(), "receive");
                QTimer::singleShot(0, this, [this]() { refreshMinerHubStatus(); });
            }
        });

        actionLayout->addWidget(btnCopyAddr);
        actionLayout->addWidget(btnCopyCmd);
        actionLayout->addWidget(btnEditLabel);
        const bool canInlineRenew = !e.isUnregisteredExt &&
            (e.track == TRACK_COIN_LOCK || e.track == TRACK_POW_LOCK || e.track == TRACK_ZEROCOIN || e.track == TRACK_BOOTSTRAP) &&
            (isExpiredRow || (e.nUnlockHeight > nTipHeight && (e.nUnlockHeight - nTipHeight) <= 240));
        if (canInlineRenew) {
            QPushButton* btnRenewRow = new QPushButton("↻", actionWidget);
            btnRenewRow->setFixedSize(26, 22);
            btnRenewRow->setCursor(Qt::PointingHandCursor);
            btnRenewRow->setFocusPolicy(Qt::NoFocus);
            btnRenewRow->setStyleSheet(copyBtnCss);
            btnRenewRow->setToolTip(tr("Renew lock for %1").arg(operatorAddr));
            const bool fCoinLock = (e.track == TRACK_COIN_LOCK);
            connect(btnRenewRow, &QPushButton::clicked, this, [this, operatorAddr, fCoinLock]() {
                QTimer::singleShot(0, this, [this, operatorAddr, fCoinLock]() {
                    if (fCoinLock) startCoinLockRegistration(operatorAddr);
                    else startPoWRegistration(operatorAddr);
                });
            });
            actionLayout->addWidget(btnRenewRow);
        }
        if (e.isUnregisteredExt) {
            QPushButton* btnRegRow = new QPushButton(tr("Reg"), actionWidget);
            btnRegRow->setFixedSize(38, 22);
            btnRegRow->setCursor(Qt::PointingHandCursor);
            btnRegRow->setFocusPolicy(Qt::NoFocus);
            btnRegRow->setToolTip(tr("Register %1 in the ADAM validator pool").arg(operatorAddr));
            btnRegRow->setStyleSheet(QString(
                "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 4px; padding: 2px 6px; font-size: 10px; font-weight: 600; }"
                "QPushButton:hover { background-color: %3; color: %4; border-color: %5; }"
            ).arg(light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
                  light ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"),
                  light ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860"),
                  light ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA"),
                  light ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0")));
            connect(btnRegRow, &QPushButton::clicked, this, [this, btnRegRow, operatorAddr, light]() {
                ScopedBoolFlag modalGuard(fModalOpen);
                QMenu menu(this);
                menu.setStyleSheet(QString(
                    "QMenu { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px; font-size: 12px; }"
                    "QMenu::item { padding: 6px 14px; border-radius: 4px; }"
                    "QMenu::item:selected { background-color: #5576B2; color: #FFFFFF; }"
                ).arg(light ? "#FFFFFF" : "#111A2E", light ? "#0F172A" : "#E2E8F0", light ? "#CBD5E1" : "#263859"));
                const QString powSuffix = (btnRegPoW && !btnRegPoW->isEnabled())
                    ? QString(" (%1)").arg(btnRegPoW->text())
                    : QString();
                const QString lockSuffix = (btnRegCoinLock && !btnRegCoinLock->isEnabled())
                    ? QString(" (%1)").arg(btnRegCoinLock->text())
                    : QString();
                QAction* actPow = menu.addAction(tr("Register PoW-Lock (10 KRISTA + PoW)%1").arg(powSuffix));
                QAction* actLock = menu.addAction(tr("Register Coin-Lock (250 KRISTA)%1").arg(lockSuffix));
                QAction* chosen = menu.exec(btnRegRow->mapToGlobal(QPoint(0, btnRegRow->height() + 2)));
                if (chosen == actPow) {
                    QTimer::singleShot(0, this, [this, operatorAddr]() {
                        startPoWRegistration(operatorAddr);
                    });
                } else if (chosen == actLock) {
                    QTimer::singleShot(0, this, [this, operatorAddr]() {
                        startCoinLockRegistration(operatorAddr);
                    });
                }
            });
            actionLayout->addWidget(btnRegRow);
        }
        tableMyMiners->setCellWidget((int)row, 4, actionWidget);
        tableMyMiners->setRowHeight((int)row, 32);
    }
    tableMyMiners->setSortingEnabled(true);
    applyRosterFilters();
    if (tableMyMiners->verticalScrollBar()) {
        tableMyMiners->verticalScrollBar()->setValue(savedScrollPos);
    }
#endif
}

void MinerHubWidget::applyRosterFilters()
{
    if (!tableMyMiners) return;
    const int selectedTrack = comboTrackFilter ? comboTrackFilter->currentData().toInt() : -1;
    const QString query = editMinerSearch ? editMinerSearch->text().trimmed() : QString();

    int visibleCount = 0;
    const int totalCount = tableMyMiners->rowCount();
    for (int r = 0; r < totalCount; ++r) {
        QTableWidgetItem* addrItem = tableMyMiners->item(r, 0);
        if (!addrItem) continue;

        const bool isExpired = addrItem->data(Qt::UserRole + 1).toBool();
        const bool isElected = addrItem->data(Qt::UserRole + 2).toBool();
        const bool isExt = addrItem->data(Qt::UserRole + 3).toBool();
        const bool isVps = addrItem->data(Qt::UserRole + 4).toBool();
        const int trackCode = addrItem->data(Qt::UserRole + 5).toInt();
        const QString searchBlob = addrItem->data(Qt::UserRole + 6).toString();

        bool hide = false;
        if (minerRosterFilter == 1 && isExpired) hide = true;
        else if (minerRosterFilter == 2 && !isElected) hide = true;
        else if (minerRosterFilter == 3 && !isExt) hide = true;
        else if (minerRosterFilter == 4 && !isVps) hide = true;

        if (!hide && selectedTrack >= 0 && trackCode != selectedTrack) {
            hide = true;
        }
        if (!hide && !query.isEmpty() && !searchBlob.contains(query, Qt::CaseInsensitive)) {
            hide = true;
        }
        tableMyMiners->setRowHidden(r, hide);
        if (!hide) ++visibleCount;
    }
    if (lblRosterCount) {
        lblRosterCount->setText(tr("%1 / %2").arg(visibleCount).arg(totalCount));
    }
}

void MinerHubWidget::onToggleMiningClicked()
{
#ifdef ENABLE_WALLET
    if (!walletModel) {
        inform(tr("Wallet is not available."));
        return;
    }
    bool fCurrentlyGenerating = fGenerateBitcoins.load();
    bool fNewState = !fCurrentlyGenerating;
    int nThreads = spinMinerThreads->value();

    if (fNewState && walletModel->getEncryptionStatus() == WalletModel::Locked) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            inform(tr("Unlock wallet to start cooperative mining."));
            return;
        }
    }

    walletModel->setCpuMiningEnabled(fNewState, nThreads);
    fForceMinerTableRefresh = true;
    refreshMinerHubStatus();

    if (fNewState) {
        inform(tr("Cooperative CPU mining started with %1 threads!").arg(nThreads));
    } else {
        inform(tr("Cooperative CPU mining stopped."));
    }
#endif
}

void MinerHubWidget::updateMinerStepperButtons()
{
    if (!spinMinerThreads) return;
    if (btnMinerThreadsDec) {
        btnMinerThreadsDec->setEnabled(spinMinerThreads->value() > spinMinerThreads->minimum());
    }
    if (btnMinerThreadsInc) {
        btnMinerThreadsInc->setEnabled(spinMinerThreads->value() < spinMinerThreads->maximum());
    }
}

void MinerHubWidget::onMinerThreadsChanged(int nThreads)
{
    updateMinerStepperButtons();
#ifdef ENABLE_WALLET
    if (walletModel && fGenerateBitcoins.load() && nThreads > 0) {
        walletModel->setCpuMiningEnabled(true, nThreads);
        fForceMinerTableRefresh = true;
        refreshMinerHubStatus();
    }
#endif
}

void MinerHubWidget::shutdownMinerWorkers()
{
    fShuttingDownMinerHub.store(true);
    if (minerStatusTimer) {
        minerStatusTimer->stop();
    }
    if (walletModel) {
        walletModel->cancelMinerPoWRegistration();
    }
    if (powRegWorkerThread.joinable()) {
        powRegWorkerThread.join();
    }
    powRegUnlockGuard.reset();
    isSolvingPoWReg.store(false);
}

void MinerHubWidget::onRegisterPoWClicked()
{
    startPoWRegistration(QString());
}

void MinerHubWidget::startPoWRegistration(const QString& targetAddress)
{
    if (!walletModel || fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    if (IsInitialBlockDownload()) {
        inform(tr("Please wait until blockchain synchronization completes before registering a validator seat."));
        return;
    }
    if (isSolvingPoWReg.load()) {
        inform(tr("Another PoW miner registration is already in progress; please wait."));
        return;
    }
    if (powRegWorkerThread.joinable()) {
        powRegWorkerThread.join();
    }

    // Pre-broadcast confirmation dialog so user sees exact refundable CLTV / Zero-PoW terms
    {
        ScopedBoolFlag modalGuard(fModalOpen);
        {
            QWidget* host = window ? static_cast<QWidget*>(window) : static_cast<QWidget*>(this);
            showHideOp(true);
            DefaultDialog* dialog = new DefaultDialog(host);
            QString targetLine = targetAddress.isEmpty()
                ? QString()
                : tr("<br/>• <b>Target Miner Address:</b> <code>%1</code>").arg(targetAddress);
            QString summary = tr("<b>Register Open PoW Validator Seat</b>%1<br/><br/>"
                                 "• <b>Block Share:</b> 4.00% of Net Reward Pool (3 Open PoW seats/block)<br/>"
                                 "• <b>Funded Mode (if 10+ mature KRISTA available):</b> Locks <b>10 KRISTA</b> inside your own wallet via refundable CLTV (~2,900 blocks / ~24h) + <b>2</b> Coordinator VRF tickets (priority seats over 0-KRISTA).<br/>"
                                 "• <b>Open-Entry Mode (if &lt; 10 mature KRISTA):</b> Registers with <b>0 KRISTA</b> collateral (240-block lease, 0 Coordinator VRF tickets, 1 permanent Open-Entry seat + fallback seats if Funded Pool &lt; 13).<br/>"
                                 "• <b>Network Fee:</b> Standard transaction fee (&lt; 0.01 KRISTA).<br/><br/>"
                                 "Solve registration puzzle and broadcast now?").arg(targetLine);
            dialog->setText(tr("Confirm Open PoW Registration"), summary, tr("REGISTER"), tr("CANCEL"));
            bool okConfirm = openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);
            dialog->deleteLater();
            if (!okConfirm) return;
        }

        auto unlockCtx = std::make_shared<WalletModel::UnlockContext>(walletModel->requestUnlock());
        if (!unlockCtx->isValid()) {
            inform(tr("Registration cancelled: wallet unlock is required."));
            return;
        }
        if (fShuttingDownMinerHub.load() || ShutdownRequested()) return;
        powRegUnlockGuard = unlockCtx;
    }

    walletModel->resetMinerPoWRegistrationCancel();
    isSolvingPoWReg.store(true);
    if (btnRegPoW) {
        btnRegPoW->setEnabled(false);
        btnRegPoW->setText(tr("Solving PoW..."));
    }
    if (btnRegZeroPoW) {
        btnRegZeroPoW->setEnabled(false);
        btnRegZeroPoW->setText(tr("Solving PoW..."));
    }

    powRegWorkerThread = std::thread([this, targetAddress]() {
        QString resultOrError;
        const bool ok = walletModel ? walletModel->registerMinerTrack(QStringLiteral("pow"), targetAddress, resultOrError) : false;
        QMetaObject::invokeMethod(this, "onPoWRegistrationFinished", Qt::QueuedConnection,
                                  Q_ARG(bool, ok), Q_ARG(QString, resultOrError));
    });
}

void MinerHubWidget::onPoWRegistrationFinished(bool success, const QString& resultOrError)
{
    if (fShuttingDownMinerHub.load() || ShutdownRequested() || !walletModel) {
        if (powRegWorkerThread.joinable()) {
            powRegWorkerThread.join();
        }
        powRegUnlockGuard.reset();
        isSolvingPoWReg.store(false);
        return;
    }

    if (powRegWorkerThread.joinable()) {
        powRegWorkerThread.join();
    }
    powRegUnlockGuard.reset();
    isSolvingPoWReg.store(false);

    if (btnRegPoW) {
        btnRegPoW->setEnabled(true);
        btnRegPoW->setText(tr("Register PoW-Lock (10 KRISTA)"));
    }
    if (btnRegZeroPoW) {
        btnRegZeroPoW->setEnabled(true);
        btnRegZeroPoW->setText(tr("Register Zero-Coin PoW (0 KRISTA)"));
    }

    if (success) {
        inform(tr("PoW Seat Registered! TXID: %1").arg(resultOrError.left(24) + "..."));
        fForceMinerTableRefresh = true;
        refreshMinerHubStatus();
    } else {
        refreshMinerHubStatus();
        inform(resultOrError);
        ScopedBoolFlag modalGuard(fModalOpen);
        QWidget* host = window ? static_cast<QWidget*>(window) : static_cast<QWidget*>(this);
        showHideOp(true);
        DefaultDialog* errDlg = new DefaultDialog(host);
        errDlg->setText(tr("Registration Not Possible"), resultOrError.toHtmlEscaped(), tr("OK"), QString());
        openDialogWithOpaqueBackgroundY(errDlg, window, 3, 5);
        errDlg->deleteLater();
    }
}

void MinerHubWidget::onRegisterCoinLockClicked()
{
    startCoinLockRegistration(QString());
}

void MinerHubWidget::startCoinLockRegistration(const QString& targetAddress)
{
    if (!walletModel || fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    if (IsInitialBlockDownload()) {
        inform(tr("Please wait until blockchain synchronization completes before registering a validator seat."));
        return;
    }

    ScopedBoolFlag modalGuard(fModalOpen);
    // Pre-broadcast confirmation dialog so user never locks 250 KRISTA accidentally
    {
        QWidget* host = window ? static_cast<QWidget*>(window) : static_cast<QWidget*>(this);
        showHideOp(true);
        DefaultDialog* dialog = new DefaultDialog(host);
        QString targetLine = targetAddress.isEmpty()
            ? QString()
            : tr("<br/>• <b>Target Miner Address:</b> <code>%1</code>").arg(targetAddress);
        QString summary = tr("<b>Register Dedicated Coin-Lock Seat (250 KRISTA)</b>%1<br/><br/>"
                             "• <b>Refundable Self-Lock:</b> <b>250 KRISTA</b> (requires a 100+ conf UTXO) stays 100% inside your own wallet under a BIP-65 CLTV time-lock for ~2,900 blocks (~24 hours).<br/>"
                             "• <b>Validator Share:</b> Competes for the dedicated <b>8.00%</b> Coin-Lock seat.<br/>"
                             "• <b>Coordinator Weight:</b> Grants <b>8</b> Coordinator VRF tickets.<br/>"
                             "• <b>Network Fee:</b> Standard transaction fee (&lt; 0.01 KRISTA).<br/><br/>"
                             "Lock 250 KRISTA in your wallet and register now?").arg(targetLine);
        dialog->setText(tr("Confirm 250 KRISTA Coin-Lock"), summary, tr("LOCK && REGISTER"), tr("CANCEL"));
        bool okConfirm = openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);
        dialog->deleteLater();
        if (!okConfirm) return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    modalGuard.release();
    if (!ctx.isValid()) {
        inform(tr("Registration cancelled: wallet unlock is required."));
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString resultOrError;
    const bool ok = walletModel->registerMinerTrack(QStringLiteral("lock"), targetAddress, resultOrError);
    QApplication::restoreOverrideCursor();
    if (ok) {
        inform(tr("250 KRISTA Coin-Lock Seat Registered! TXID: %1").arg(resultOrError.left(24) + "..."));
        fForceMinerTableRefresh = true;
        refreshMinerHubStatus();
    } else {
        inform(resultOrError);
        ScopedBoolFlag errModalGuard(fModalOpen);
        QWidget* host = window ? static_cast<QWidget*>(window) : static_cast<QWidget*>(this);
        showHideOp(true);
        DefaultDialog* errDlg = new DefaultDialog(host);
        errDlg->setText(tr("Registration Not Possible"), resultOrError.toHtmlEscaped(), tr("OK"), QString());
        openDialogWithOpaqueBackgroundY(errDlg, window, 3, 5);
        errDlg->deleteLater();
    }
}


void MinerHubWidget::onToggleMinerHubFullScreenClicked()
{
    setMinerHubFullScreenMode(!fMinerHubFullScreen);
}

void MinerHubWidget::setMinerHubFullScreenMode(bool enable)
{
    if (fMinerHubFullScreen == enable) return;
    fMinerHubFullScreen = enable;

    if (shortcutMinerFullScreenEsc) {
        shortcutMinerFullScreenEsc->setEnabled(enable);
    }
    if (window) {
        window->setMinerHubFullScreen(enable);
    }
    if (this->layout()) {
        this->layout()->setContentsMargins(enable ? 12 : 22, enable ? 12 : 16, enable ? 12 : 22, enable ? 12 : 18);
    }

    updateMinerHubTheme();
}

void MinerHubWidget::onRenewLockClicked()
{
    if (pendingRenewTrack == 2) {
        onRegisterCoinLockClicked();
    } else {
        onRegisterPoWClicked();
    }
}

