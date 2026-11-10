// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/dashboardwidget.h"
#include "qt/kristatech/forms/ui_dashboardwidget.h"
#include "qt/kristatech/sendconfirmdialog.h"
#include "qt/kristatech/txrow.h"
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
#include "llmq.h"
#include "wallet/wallet.h"
#include "rpc/server.h"
#include <vector>
#include <thread>
#include <set>
#include <algorithm>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>	
#include <QPainter>
#include <QModelIndex>
#include <QList>
#include <QGraphicsLayout>
#include <QScrollBar>

#define DECORATION_SIZE 65
#define NUM_ITEMS 3
#define SHOW_EMPTY_CHART_VIEW_THRESHOLD 4000
#define REQUEST_LOAD_TASK 1
#define CHART_LOAD_MIN_TIME_INTERVAL 15

DashboardWidget::DashboardWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::DashboardWidget)
{
    ui->setupUi(this);

    txHolder = new TxViewHolder(isLightTheme());
    txViewDelegate = new FurAbstractListItemDelegate(
        DECORATION_SIZE,
        txHolder,
        this
    );

    this->setStyleSheet(parent->styleSheet());
    this->setContentsMargins(0,0,0,0);

    // Containers
    setCssProperty({this, ui->left}, "container");
    ui->left->setContentsMargins(0,0,0,0);

    // Title
    setCssTitleScreen(ui->labelTitle);
    setCssTitleScreen(ui->labelTitle2);

    /* Subtitle */
    setCssSubtitleScreen(ui->labelSubtitle);

    // Staking Information
    ui->labelMessage->setText(tr("Amount of KRISTA earned via Staking & Masternodes"));
    setCssSubtitleScreen(ui->labelMessage);
    setCssProperty(ui->labelSquarePiv, "square-chart-piv");
    setCssProperty(ui->labelPiv, "text-chart-piv");
	setCssProperty(ui->labelSquareMNRewards, "square-chart-mnrewards");
	setCssProperty(ui->labelMNRewards, "text-chart-mnrewards");



    // Staking Amount
    QFont fontBold;
    fontBold.setWeight(QFont::Bold);

    setCssProperty(ui->labelChart, "legend-chart");
    setCssProperty(ui->labelAmountPiv, "text-stake-piv-disable");
	ui->labelAmountMNRewards->setText("0 KRISTA");
	setCssProperty(ui->labelAmountMNRewards, "text-stake-mnrewards-disable");



    setCssProperty({ui->pushButtonAll,  ui->pushButtonMonth, ui->pushButtonYear}, "btn-check-time");
    setCssProperty({ui->comboBoxMonths,  ui->comboBoxYears}, "btn-combo-chart-selected");

    ui->comboBoxMonths->setView(new QListView());
    ui->comboBoxMonths->setStyleSheet("selection-background-color:transparent; selection-color:transparent;");
    ui->comboBoxYears->setView(new QListView());
    ui->comboBoxYears->setStyleSheet("selection-background-color:transparent; selection-color:transparent;");
    ui->pushButtonYear->setChecked(true);

    setCssProperty(ui->pushButtonChartArrow, "btn-chart-arrow");
    setCssProperty(ui->pushButtonChartRight, "btn-chart-arrow-right");

#ifdef USE_QTCHARTS
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20,20,20,0);
    connect(ui->comboBoxYears, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged),
        this, &DashboardWidget::onChartYearChanged);
#else
    // hide charts container if not USE_QTCHARTS
    ui->right->setVisible(false);
#endif // USE_QTCHARTS

    // Sort Transactions
    setSortTx(ui->comboBoxSort);
    connect(ui->comboBoxSort, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged), this, &DashboardWidget::onSortChanged);

    // Sort type
    setSortTxTypeFilter(ui->comboBoxSortType);
    ui->comboBoxSortType->setCurrentIndex(0);
    connect(ui->comboBoxSortType, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged),
        this, &DashboardWidget::onSortTypeChanged);

    // Transactions
    setCssProperty(ui->listTransactions, "container");
    ui->listTransactions->setItemDelegate(txViewDelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listTransactions->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->listTransactions->setUniformItemSizes(true);

    // Sync Warning
    ui->layoutWarning->setVisible(true);
    ui->lblWarning->setText(tr("Please wait until the wallet is fully synced to see your correct balance"));
    setCssProperty(ui->lblWarning, "text-warning");
    setCssProperty(ui->imgWarning, "ic-warning");

    //Empty List
    ui->emptyContainer->setVisible(false);
    setCssProperty(ui->pushImgEmpty, "img-empty-transactions");
    setCssProperty(ui->labelEmpty, "text-empty");
    setCssProperty(ui->chartContainer, "container-chart");
    setCssProperty(ui->pushImgEmptyChart, "img-empty-staking-on");

    setCssBtnSecondary(ui->btnHowTo);

    setCssProperty(ui->labelEmptyChart, "text-empty");
    setCssSubtitleScreen(ui->labelMessageEmpty);

    // Chart State
    ui->layoutChart->setVisible(false);
    ui->emptyContainerChart->setVisible(true);
    setShadow(ui->layoutShadow);

    connect(ui->listTransactions, &QListView::clicked, this, &DashboardWidget::handleTransactionClicked);

bool hasCharts = false;
#ifdef USE_QTCHARTS
    hasCharts = true;
    isLoading = false;
    setChartShow(YEAR);
    connect(ui->pushButtonYear, &QPushButton::clicked, [this](){setChartShow(YEAR);});
    connect(ui->pushButtonMonth, &QPushButton::clicked, [this](){setChartShow(MONTH);});
    connect(ui->pushButtonAll, &QPushButton::clicked, [this](){setChartShow(ALL);});
    if (window)
        connect(window, &KRISTATECHGUI::windowResizeEvent, this, &DashboardWidget::windowResizeEvent);
#endif

    if (hasCharts) {
        ui->labelEmptyChart->setText(tr("You have no staking rewards"));
    } else {
        ui->labelEmptyChart->setText(tr("No charts library"));
    }

    initMinerHub();
}

void DashboardWidget::handleTransactionClicked(const QModelIndex &index)
{
    ui->listTransactions->setCurrentIndex(index);
    QModelIndex rIndex = filter->mapToSource(index);

    window->showHide(true);
    TxDetailDialog *dialog = new TxDetailDialog(window, false);
    dialog->setData(walletModel, rIndex);
    openDialogWithOpaqueBackgroundY(dialog, window, 3, 17);

    // Back to regular status
    ui->listTransactions->scrollTo(index);
    ui->listTransactions->clearSelection();
    ui->listTransactions->setFocus();
    dialog->deleteLater();
}

void DashboardWidget::loadWalletModel()
{
    if (walletModel && walletModel->getOptionsModel()) {
        txModel = walletModel->getTransactionTableModel();
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setDynamicSortFilter(true);
        filter->setSortCaseSensitivity(Qt::CaseInsensitive);
        filter->setFilterCaseSensitivity(Qt::CaseInsensitive);
        filter->setSortRole(Qt::EditRole);
        filter->setSourceModel(txModel);

        // Read filter settings
        QSettings settings;
        int filterByType = TransactionFilterProxy::ALL_TYPES;

        filter->setTypeFilter(filterByType); // Set filter
        int filterIndex = ui->comboBoxSortType->findData(filterByType); // Find index
        ui->comboBoxSortType->setCurrentIndex(filterIndex); // Set item in ComboBox

        // Read sort settings
        changeSort(SortTx::DATE_DESC);

        txHolder->setFilter(filter);
        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);

        if (txModel->size() == 0) {
            ui->emptyContainer->setVisible(true);
            ui->listTransactions->setVisible(false);
            ui->comboBoxSortType->setVisible(false);
            ui->comboBoxSort->setVisible(false);
        }

        connect(ui->pushImgEmpty, &QPushButton::clicked, window, &KRISTATECHGUI::openFAQ);
        connect(ui->btnHowTo, &QPushButton::clicked, window, &KRISTATECHGUI::openFAQ);
        connect(txModel, &TransactionTableModel::txArrived, this, &DashboardWidget::onTxArrived);
        connect(txModel, &QAbstractItemModel::modelReset, this, [this]() {
            showList();
            if (filter && filter->rowCount() > 0) {
                ui->listTransactions->update();
            }
#ifdef USE_QTCHARTS
            if (stakesFilter) {
                hasStakes = stakesFilter->rowCount() > 0;
                tryChartRefresh();
            }
#endif
        });
        connect(filter, &QAbstractItemModel::rowsInserted, this, [this]() {
            if (!ui->listTransactions->isVisible()) {
                showList();
            }
        });

        // Notification pop-up for new transaction
        connect(txModel, &TransactionTableModel::rowsInserted, this, &DashboardWidget::processNewTransaction);
#ifdef USE_QTCHARTS
        // chart filter
        stakesFilter = new TransactionFilterProxy();
        stakesFilter->setDynamicSortFilter(true);
        stakesFilter->setOnlyStakesandMN(true);
        stakesFilter->setSourceModel(txModel);
        hasStakes = stakesFilter->rowCount() > 0;

        onHideChartsChanged(walletModel->getOptionsModel()->isHideCharts());
        connect(walletModel->getOptionsModel(), &OptionsModel::hideChartsChanged, this, &DashboardWidget::onHideChartsChanged);
#endif
    }
    // update the display unit, to not use the default ("KRISTA")
    updateDisplayUnit();
}

void DashboardWidget::onTxArrived(const QString& hash, const bool& isCoinStake)
{
    showList();
    if (filter && filter->rowCount() > 0) {
        if (ui->listTransactions->verticalScrollBar()->value() == 0) {
            ui->listTransactions->scrollTo(filter->index(0, 0));
        }
        ui->listTransactions->update();
    }
#ifdef USE_QTCHARTS
    if (isCoinStake) {
        // Update value if this is our first stake
        if (!hasStakes)
            hasStakes = stakesFilter->rowCount() > 0;
        tryChartRefresh();
    }
#endif
}

void DashboardWidget::showList()
{
    if (filter->rowCount() == 0) {
        ui->emptyContainer->setVisible(true);
        ui->listTransactions->setVisible(false);
        ui->comboBoxSortType->setVisible(false);
        ui->comboBoxSort->setVisible(false);
    } else {
        ui->emptyContainer->setVisible(false);
        ui->listTransactions->setVisible(true);
        ui->comboBoxSortType->setVisible(true);
        ui->comboBoxSort->setVisible(true);
    }
}

void DashboardWidget::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        txHolder->setDisplayUnit(nDisplayUnit);
        ui->listTransactions->update();
    }
}

void DashboardWidget::onSortChanged(const QString& value)
{
    if (!filter) return;

    if (!value.isNull()) {
        changeSort(ui->comboBoxSort->currentIndex());
    } else {
        changeSort(SortTx::DATE_DESC);
    }
}

void DashboardWidget::changeSort(int nSortIndex)
{
    int nColumnIndex = TransactionTableModel::Date;
    Qt::SortOrder order = Qt::DescendingOrder;

    switch (nSortIndex) {
        case SortTx::DATE_DESC:
        {
            nColumnIndex = TransactionTableModel::Date;
            break;
        }
        case SortTx::DATE_ASC:
        {
            nColumnIndex = TransactionTableModel::Date;
            order = Qt::AscendingOrder;
            break;
        }
        case SortTx::AMOUNT_DESC:
        {
            nColumnIndex = TransactionTableModel::Amount;
            break;
        }
        case SortTx::AMOUNT_ASC:
        {
            nColumnIndex = TransactionTableModel::Amount;
            order = Qt::AscendingOrder;
            break;
        }
    }

    ui->comboBoxSort->setCurrentIndex(nSortIndex);
    filter->sort(nColumnIndex, order);
    ui->listTransactions->update();
}

void DashboardWidget::onSortTypeChanged(const QString& value)
{
    if (!filter) return;
    int filterIndex = ui->comboBoxSortType->currentIndex();
    int filterByType = ui->comboBoxSortType->itemData(filterIndex).toInt();

    filter->setTypeFilter(filterByType);
    ui->listTransactions->update();

    if (filter->rowCount() == 0) {
        ui->emptyContainer->setVisible(true);
        ui->listTransactions->setVisible(false);
    } else {
        showList();
    }
}

void DashboardWidget::walletSynced(bool sync)
{
    if (this->isSync != sync) {
        this->isSync = sync;
        ui->layoutWarning->setVisible(!this->isSync);
#ifdef USE_QTCHARTS
        tryChartRefresh();
#endif
    }
}

void DashboardWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<TxViewHolder*>(this->txViewDelegate->getRowFactory())->isLightTheme = isLightTheme;
    updateMinerHubTheme();
#ifdef USE_QTCHARTS
    if (chart) this->changeChartColors();
#endif
}

void DashboardWidget::initMinerHub()
{
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20, 20, 20, 20);
    ui->right->setVisible(true);
#ifndef USE_QTCHARTS
    ui->labelTitle2->setVisible(false);
    ui->labelMessage->setVisible(false);
    ui->labelAmountPiv->setVisible(false);
    ui->labelAmountMNRewards->setVisible(false);
    ui->layoutChart->setVisible(false);
    ui->emptyContainerChart->setVisible(false);
    ui->layoutShadow->setVisible(false);
#endif

    minerHubCard = new QFrame(ui->right);
    minerHubCard->setObjectName("minerHubCard");
    minerHubCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* cardLayout = new QVBoxLayout(minerHubCard);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    // Header Row: Title + Live Status Badge
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setSpacing(8);
    lblMinerHubTitle = new QLabel(tr("⛏️ 1-Click Cooperative Miner (ADAM)"), minerHubCard);
    QFont titleFont = lblMinerHubTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(10);
    lblMinerHubTitle->setFont(titleFont);

    lblMinerStatusBadge = new QLabel(minerHubCard);
    lblMinerStatusBadge->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headerRow->addWidget(lblMinerHubTitle);
    headerRow->addStretch();
    headerRow->addWidget(lblMinerStatusBadge);
    cardLayout->addLayout(headerRow);

    // Row 1: Thread count spinner + 1-Click Start/Stop button
    QHBoxLayout* minerCtrlRow = new QHBoxLayout();
    minerCtrlRow->setSpacing(8);

    lblMinerThreads = new QLabel(tr("CPU Cores:"), minerHubCard);
    spinMinerThreads = new QSpinBox(minerHubCard);
    int maxCores = std::max(1, (int)std::thread::hardware_concurrency());
    int defaultCores = (int)GetArg("-genproclimit", std::max(1, maxCores / 2));
    if (defaultCores <= 0) defaultCores = maxCores;
    spinMinerThreads->setRange(1, maxCores);
    spinMinerThreads->setValue(std::min(defaultCores, maxCores));
    spinMinerThreads->setSuffix(tr(" threads"));
    spinMinerThreads->setMinimumHeight(32);
    spinMinerThreads->setMinimumWidth(105);
    spinMinerThreads->setToolTip(tr("Number of CPU threads to use for cooperative ADAM puzzle solving and block coordination."));

    btnToggleMining = new QPushButton(minerHubCard);
    btnToggleMining->setMinimumHeight(34);
    btnToggleMining->setCursor(Qt::PointingHandCursor);
    btnToggleMining->setFocusPolicy(Qt::NoFocus);

    minerCtrlRow->addWidget(lblMinerThreads);
    minerCtrlRow->addWidget(spinMinerThreads);
    minerCtrlRow->addWidget(btnToggleMining, 1);
    cardLayout->addLayout(minerCtrlRow);

    // Row 2: 1-Click Validator Track Registration Wizard
    lblTrackSection = new QLabel(tr("Validator Seat Registration (~24h Refundable CLTV Time-Lock):"), minerHubCard);
    QFont subFont = lblTrackSection->font();
    subFont.setPointSize(9);
    lblTrackSection->setFont(subFont);
    cardLayout->addWidget(lblTrackSection);

    QHBoxLayout* regRow = new QHBoxLayout();
    regRow->setSpacing(8);

    btnRegPoW = new QPushButton(tr("⚡ Register PoW Seat (0 / 10 KRISTA • 4%)"), minerHubCard);
    btnRegPoW->setMinimumHeight(32);
    btnRegPoW->setCursor(Qt::PointingHandCursor);
    btnRegPoW->setFocusPolicy(Qt::NoFocus);
    btnRegPoW->setToolTip(tr("1-Click registration for the Open PoW Validator track (3 seats, 4.00% block share each).\n"
                             "Requires 0 KRISTA during bootstrap when network miners < 13, or a refundable 10 KRISTA (~24h) time-lock."));

    btnRegCoinLock = new QPushButton(tr("🔒 Register Coin-Lock (250 KRISTA • 8%)"), minerHubCard);
    btnRegCoinLock->setMinimumHeight(32);
    btnRegCoinLock->setCursor(Qt::PointingHandCursor);
    btnRegCoinLock->setFocusPolicy(Qt::NoFocus);
    btnRegCoinLock->setToolTip(tr("1-Click registration for the dedicated Coin-Lock Validator seat (8.00% block share).\n"
                                  "Locks 250 KRISTA in your own wallet for ~24 hours (2,900 blocks) via CLTV; coins return to spendable balance automatically."));

    regRow->addWidget(btnRegPoW, 1);
    regRow->addWidget(btnRegCoinLock, 1);
    cardLayout->addLayout(regRow);

    // Row 3: 4-Metric Summary Stats Strip
    statsStripFrame = new QFrame(minerHubCard);
    statsStripFrame->setObjectName("statsStripFrame");
    QHBoxLayout* statsLayout = new QHBoxLayout(statsStripFrame);
    statsLayout->setContentsMargins(10, 6, 10, 6);
    statsLayout->setSpacing(10);

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

    createStatBox(lblStatMyMinersTitle, lblStatMyMinersVal, tr("My Miners"));
    createStatBox(lblStatHitsTitle, lblStatHitsVal, tr("Rewards Won"));
    createStatBox(lblStatEarnedTitle, lblStatEarnedVal, tr("Total Earned"));
    createStatBox(lblStatImmatureTitle, lblStatImmatureVal, tr("Immature (100b)"));
    cardLayout->addWidget(statsStripFrame);

    // Row 4: Registered Miners Roster Header + Live Committee Table
    lblRosterSection = new QLabel(tr("My Registered Miner Addresses & Live Block Committee Status:"), minerHubCard);
    lblRosterSection->setFont(subFont);
    cardLayout->addWidget(lblRosterSection);

    tableMyMiners = new QTableWidget(0, 5, minerHubCard);
    tableMyMiners->setObjectName("tableMyMiners");
    tableMyMiners->setHorizontalHeaderLabels({
        tr("Miner Address"),
        tr("Track & Lock"),
        tr("Live Committee Role"),
        tr("Hits & Earned"),
        tr("Copy")
    });
    tableMyMiners->verticalHeader()->setVisible(false);
    tableMyMiners->setSelectionMode(QAbstractItemView::NoSelection);
    tableMyMiners->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableMyMiners->setFocusPolicy(Qt::NoFocus);
    tableMyMiners->setShowGrid(false);
    tableMyMiners->setAlternatingRowColors(true);
    tableMyMiners->horizontalHeader()->setStretchLastSection(false);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    tableMyMiners->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    tableMyMiners->setColumnWidth(4, 66);
    tableMyMiners->setMinimumHeight(130);
    tableMyMiners->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    cardLayout->addWidget(tableMyMiners, 1);

    ui->verticalLayout_2->insertWidget(0, minerHubCard, 1);
    ui->verticalLayout_2->insertSpacing(1, 12);

    connect(btnToggleMining, &QPushButton::clicked, this, &DashboardWidget::onToggleMiningClicked);
    connect(spinMinerThreads, static_cast<void (QSpinBox::*)(int)>(&QSpinBox::valueChanged), this, &DashboardWidget::onMinerThreadsChanged);
    connect(btnRegPoW, &QPushButton::clicked, this, &DashboardWidget::onRegisterPoWClicked);
    connect(btnRegCoinLock, &QPushButton::clicked, this, &DashboardWidget::onRegisterCoinLockClicked);

    updateMinerHubTheme();
    refreshMinerHubStatus();

    minerStatusTimer = new QTimer(this);
    connect(minerStatusTimer, &QTimer::timeout, this, &DashboardWidget::refreshMinerHubStatus);
    minerStatusTimer->start(3000);
}

void DashboardWidget::updateMinerHubTheme()
{
    if (!minerHubCard) return;
    const bool light = isLightTheme();
    const QString cardBg = light ? "#F4F6FA" : "#1B222C";
    const QString cardBorder = light ? "#D0D7E2" : "#2E3A4B";
    const QString textPrimary = light ? "#172B4D" : "#E6EDF3";
    const QString textSecondary = light ? "#4A5568" : "#9BA7B4";
    const QString spinBg = light ? "#FFFFFF" : "#12171E";
    const QString trackBtnBg = light ? "#E9EEF6" : "#253041";
    const QString trackBtnHover = light ? "#DCE4F2" : "#303E54";
    const QString tableAltBg = light ? "#F8FAFC" : "#161C24";
    const QString headerBg = light ? "#E9EEF6" : "#202937";

    minerHubCard->setStyleSheet(QString(
        "QFrame#minerHubCard { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
    ).arg(cardBg, cardBorder));

    lblMinerHubTitle->setStyleSheet(QString("color: %1; background: transparent; border: none;").arg(textPrimary));
    lblMinerThreads->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 12px;").arg(textSecondary));
    lblTrackSection->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 11px;").arg(textSecondary));
    lblRosterSection->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 11px; font-weight: bold;").arg(textSecondary));

    spinMinerThreads->setStyleSheet(QString(
        "QSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 3px 8px; font-weight: bold; }"
    ).arg(spinBg, textPrimary, cardBorder));

    QString regBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; }"
    ).arg(trackBtnBg, textPrimary, cardBorder, trackBtnHover);

    btnRegPoW->setStyleSheet(regBtnStyle);
    btnRegCoinLock->setStyleSheet(regBtnStyle);

    statsStripFrame->setStyleSheet(QString(
        "QFrame#statsStripFrame { background-color: %1; border: 1px solid %2; border-radius: 6px; }"
    ).arg(spinBg, cardBorder));

    QString statTitleCss = QString("color: %1; font-size: 10px; background: transparent; border: none;").arg(textSecondary);
    QString statValCss = QString("color: %1; font-size: 11px; font-weight: bold; background: transparent; border: none;").arg(textPrimary);
    for (QLabel* lbl : {lblStatMyMinersTitle, lblStatHitsTitle, lblStatEarnedTitle, lblStatImmatureTitle}) {
        if (lbl) lbl->setStyleSheet(statTitleCss);
    }
    for (QLabel* lbl : {lblStatMyMinersVal, lblStatHitsVal, lblStatEarnedVal, lblStatImmatureVal}) {
        if (lbl) lbl->setStyleSheet(statValCss);
    }

    tableMyMiners->setStyleSheet(QString(
        "QTableWidget#tableMyMiners { background-color: %1; alternate-background-color: %2; color: %3; border: 1px solid %4; border-radius: 6px; font-size: 11px; }"
        "QTableWidget#tableMyMiners::item { padding: 3px 5px; border: none; }"
        "QHeaderView::section { background-color: %5; color: %3; font-size: 10px; font-weight: bold; padding: 4px 5px; border: none; border-bottom: 1px solid %4; }"
    ).arg(spinBg, tableAltBg, textPrimary, cardBorder, headerBg));

    fForceMinerTableRefresh = true;
    refreshMinerHubStatus();
}

void DashboardWidget::refreshMinerHubStatus()
{
    if (!minerHubCard || fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    bool fGenerating = GetBoolArg("-gen", false);
    int nThreads = (int)GetArg("-genproclimit", spinMinerThreads->value());
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
        lblMinerStatusBadge->setText(tr("● ACTIVE (%1T) • Network Miners: %2").arg(nThreads).arg(nPoolSize));
        lblMinerStatusBadge->setStyleSheet("color: #00875A; font-weight: bold; font-size: 11px; background: transparent; border: none;");
        btnToggleMining->setText(tr("⏹️ Stop CPU Mining"));
        btnToggleMining->setStyleSheet(
            "QPushButton { background-color: #DE350B; color: #FFFFFF; border: none; border-radius: 6px; padding: 6px 12px; font-weight: bold; font-size: 12px; }"
            "QPushButton:hover { background-color: #BF2600; }"
        );
    } else {
        lblMinerStatusBadge->setText(tr("○ STOPPED • Network Miners: %1").arg(nPoolSize));
        lblMinerStatusBadge->setStyleSheet("color: #6B778C; font-weight: bold; font-size: 11px; background: transparent; border: none;");
        btnToggleMining->setText(tr("⛏️ Start 1-Click Mining"));
        btnToggleMining->setStyleSheet(
            "QPushButton { background-color: #00875A; color: #FFFFFF; border: none; border-radius: 6px; padding: 6px 12px; font-weight: bold; font-size: 12px; }"
            "QPushButton:hover { background-color: #006644; }"
        );
    }

#ifdef ENABLE_WALLET
    if (!pwalletMain || nTipHeight < 0) return;

    TRY_LOCK(cs_main, lockMain);
    if (!lockMain || !chainActive.Tip()) return;
    TRY_LOCK(pwalletMain->cs_wallet, lockWallet);
    if (!lockWallet) return;

    size_t nCurrentWalletSize = pwalletMain->mapWallet.size();
    if (!fForceMinerTableRefresh && nTipHeight == cachedMinerStatsHeight && nCurrentWalletSize == cachedMinerStatsWalletSize) {
        return;
    }

    cachedMinerStatsHeight = nTipHeight;
    cachedMinerStatsWalletSize = nCurrentWalletSize;
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

    // 1. Inspect active network pool at current tip (including Masternodes owned by this wallet / masternode.conf)
    std::set<CPubKey> setMNKeys, setCoinLockKeys;
    std::vector<CPubKey> vPool = GetAdamMinerPool(nTipHeight, nullptr, &setMNKeys, &setCoinLockKeys);
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
        auto itCol = mapMasternodeCollateralOutpoints.find(pk);
        if (itCol != mapMasternodeCollateralOutpoints.end()) {
            const COutPoint& colOut = itCol->second;
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
                CTxDestination colDest;
                if (ExtractDestination(colTxOut.scriptPubKey, colDest)) {
                    if (const CKeyID* pColKey = boost::get<CKeyID>(&colDest)) {
                        collateralKeyId = *pColKey;
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
                        e.nUnlockHeight = (int)lockTime;
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
        if (!IsModelDActive(h) || val <= 0) return false;
        CAmount nBlockValActual = CMasternode::GetBlockValue(h);
        CAmount nTreasurySplit = (h > 1) ? (nBlockValActual * 7 / 100) : 0;
        CAmount nFaucetSplit = (h > 1 && h <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
        CAmount nBlockVal = nBlockValActual - nTreasurySplit - nFaucetSplit;
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
            if (!fIsAdamBlock) return false;
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
                if (isPuzzleSeat) {
                    itMiner->second.nPuzzlesSolved++;
                    if (nTxHeight > itMiner->second.nLastPuzzleHeight) {
                        itMiner->second.nLastPuzzleHeight = nTxHeight;
                    }
                }
                setCreditedMinerBlocks.insert(std::make_pair(nTxHeight, targetKid));
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
                            bool isSeat = (vIdx >= 2) && isModelDSeatPayoutAmount(nTxHeight, vout.nValue);
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

            for (size_t i = 0; i < block.vAdamMiners.size(); ++i) {
                const CPubKey& pk = block.vAdamMiners[i];
                if (!pk.IsValid()) continue;
                bool isSolvedSeat = (i < block.vAdamSolutions.size() && !block.vAdamSolutions[i].empty());
                bool isV11CoordEntry = (block.nVersion == 11 && i + 1 == block.vAdamMiners.size());
                if (isSolvedSeat) {
                    s_mapPreModelDSolvedHeights[pk.GetID()].push_back(h);
                }
                if (isV11CoordEntry) {
                    s_mapPreModelDCoordSignedHeights[pk.GetID()].push_back(h);
                    if (!block.IsProofOfStake() && !isSolvedSeat) {
                        s_mapPreModelDCoordHeights[pk.GetID()].push_back(h);
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
    int nMyActivePoolCount = 0;
    for (const auto& kv : mapMyMiners) {
        const MyMinerEntry& e = kv.second;
        if (e.inActivePool || e.hasExplicitReg) {
            vDisplayMiners.push_back(e);
            if (e.inActivePool) nMyActivePoolCount++;
        }
    }

    // Sort: Active in pool first, then MN > Coin-Lock > PoW-Lock > Zero-Coin > Bootstrap, then Earned > Puzzles > CoordSigned
    std::sort(vDisplayMiners.begin(), vDisplayMiners.end(), [](const MyMinerEntry& a, const MyMinerEntry& b) {
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

    int nElectedNowCount = 0;
    for (const MyMinerEntry& e : vDisplayMiners) {
        if (!e.pubKey.IsValid()) continue;
        if (e.pubKey == coordinatorKey || std::find(vElectedMiners.begin(), vElectedMiners.end(), e.pubKey) != vElectedMiners.end()) {
            nElectedNowCount++;
        }
    }

    // 5. Update Summary Strip
    lblStatMyMinersVal->setText(tr("%1 Active (%2 Elected)").arg(nMyActivePoolCount).arg(nElectedNowCount));
    lblStatHitsVal->setText(tr("%1 hits").arg(nTotalWalletHits));
    lblStatHitsVal->setToolTip(tr("Total confirmed block, staking, and validator reward payouts received across all addresses in this wallet (including Blocks #1–#%1 bootstrap rewards).")
        .arg(nAdamStartHeight - 1));
    lblStatEarnedVal->setText(QString("%1 KRISTA").arg(QString::number(nTotalWalletEarned / (double)COIN, 'f', 2)));
    lblStatEarnedVal->setToolTip(tr("Cumulative KRISTA earned from mining, staking, and validator rewards across the entire wallet. Individual rows below track ADAM-era (Block #%1+) rewards and solved puzzles.")
        .arg(nAdamStartHeight));
    lblStatImmatureVal->setText(QString("%1 KRISTA").arg(QString::number(nImmatureBalance / (double)COIN, 'f', 2)));

    lblRosterSection->setText(tr("My Registered Miner Addresses & Live Block #%1 Committee Status:").arg(nNextHeight));

    // 6. Populate tableMyMiners
    tableMyMiners->setRowCount((int)vDisplayMiners.size());
    const bool light = isLightTheme();
    const QString copyBtnCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: 2px 4px; font-size: 10px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; }"
    ).arg(light ? "#E9EEF6" : "#253041",
          light ? "#172B4D" : "#E6EDF3",
          light ? "#D0D7E2" : "#2E3A4B",
          light ? "#DCE4F2" : "#303E54");

    for (size_t row = 0; row < vDisplayMiners.size(); ++row) {
        const MyMinerEntry& e = vDisplayMiners[row];
        CKeyID displayKeyId = (!e.payoutKeyId.IsNull()) ? e.payoutKeyId : e.keyId;
        QString fullAddr = QString::fromStdString(EncodeDestination(CTxDestination(displayKeyId)));
        QString operatorAddr = QString::fromStdString(EncodeDestination(CTxDestination(e.keyId)));
        QString shortAddr = fullAddr.left(7) + "..." + fullAddr.right(6);

        // Col 0: Address
        QTableWidgetItem* itemAddr = new QTableWidgetItem(shortAddr);
        if (!e.payoutKeyId.IsNull() && e.payoutKeyId != e.keyId) {
            itemAddr->setToolTip(tr("Collateral Payout Address: %1\nADAM Operator Key Address: %2").arg(fullAddr, operatorAddr));
        } else {
            itemAddr->setToolTip(tr("Full Miner Address: %1").arg(fullAddr));
        }
        tableMyMiners->setItem((int)row, 0, itemAddr);

        // Col 1: Track & Remaining Lock
        QString trackStr;
        if (e.track == TRACK_COIN_LOCK) {
            trackStr = tr("🔒 Lock 250");
        } else if (e.track == TRACK_POW_LOCK) {
            trackStr = tr("⚡ PoW 10");
        } else if (e.track == TRACK_MASTERNODE) {
            trackStr = e.mnAlias.isEmpty() ? tr("🛡️ MN 4200") : tr("🛡️ %1 (4200)").arg(e.mnAlias);
        } else if (e.track == TRACK_ZEROCOIN) {
            trackStr = tr("🌱 Zero-PoW");
        } else {
            trackStr = tr("🏁 Bootstrap");
        }

        QString lockStatusStr;
        if (e.fRegPending && !e.inActivePool) {
            int nRemBlocks = std::max(0, e.nUnlockHeight - nTipHeight);
            double hours = (nRemBlocks * 30.0) / 3600.0;
            lockStatusStr = tr(" • ⏳ %1b (~%2h) (Pending)").arg(nRemBlocks).arg(QString::number(hours, 'f', 1));
        } else if (e.nUnlockHeight > nTipHeight) {
            int nRemBlocks = e.nUnlockHeight - nTipHeight;
            double hours = (nRemBlocks * 30.0) / 3600.0;
            lockStatusStr = tr(" • ⏳ %1b (~%2h)").arg(nRemBlocks).arg(QString::number(hours, 'f', 1));
            if (!e.inActivePool) {
                lockStatusStr += tr(" (Standby)");
            }
        } else if (e.hasExplicitReg && e.nUnlockHeight > 0) {
            lockStatusStr = e.fLockSpent ? tr(" • Spent (Expired)") : tr(" • 🔓 Unlocked (Expired)");
        } else if (e.inActivePool) {
            lockStatusStr = tr(" • Active");
        } else {
            lockStatusStr = tr(" (Expired)");
        }

        QTableWidgetItem* itemTrack = new QTableWidgetItem(trackStr + lockStatusStr);
        itemTrack->setToolTip(tr("Registration Height: %1 • Unlock Height: %2 • In Active Network Roster: %3")
            .arg(e.nRegHeight >= 0 ? QString("#%1").arg(e.nRegHeight) : tr("Blocks #1–#%1 Coinbase Fallback").arg(nAdamStartHeight - 1))
            .arg(e.nUnlockHeight > 0 ? QString("#%1").arg(e.nUnlockHeight)
                                     : tr("Auto-replaced when explicit pool >= %1 or Block #%2")
                                           .arg(Params().GetConsensus().nAdamMinersCount + 2)
                                           .arg(Params().GetConsensus().nAdamBootstrapLimit))
            .arg(e.inActivePool ? tr("Yes") : tr("No")));
        tableMyMiners->setItem((int)row, 1, itemTrack);

        // Col 2: Live Committee Role for Block #(nTipHeight + 1)
        QString roleStr = "—";
        QColor roleColor = light ? QColor("#6B778C") : QColor("#9BA7B4");
        if (e.pubKey.IsValid() && e.inActivePool) {
            if (e.pubKey == coordinatorKey) {
                roleStr = tr("👑 COORD (X11KVS)");
                roleColor = QColor("#D97706");
            } else {
                auto itEl = std::find(vElectedMiners.begin(), vElectedMiners.end(), e.pubKey);
                if (itEl != vElectedMiners.end()) {
                    int seatIdx = (int)std::distance(vElectedMiners.begin(), itEl) + 1;
                    QString algoText;
                    if (Params().GetConsensus().NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
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
                    roleStr = tr("🎯 Seat #%1 (%2)").arg(seatIdx).arg(algoText);
                    roleColor = QColor("#00875A");
                } else {
                    auto itSt = std::find(vStandbyMiners.begin(), vStandbyMiners.end(), e.pubKey);
                    if (itSt != vStandbyMiners.end()) {
                        int standbyRank = (int)std::distance(vStandbyMiners.begin(), itSt) + 1;
                        roleStr = tr("⏳ Standby #%1").arg(standbyRank);
                    }
                }
            }
        }
        QTableWidgetItem* itemRole = new QTableWidgetItem(roleStr);
        itemRole->setForeground(QBrush(roleColor));
        tableMyMiners->setItem((int)row, 2, itemRole);

        // Col 3: Hits & Earned (ADAM Paid Rewards + Solved Committee Puzzles + V11 Coordinator Co-Sigs)
        QString hitsStr = (e.nCoordSigned > 0)
            ? tr("🏆 %1 • %2 KRISTA (🧩%3 👑%4)")
                  .arg(e.nPaidHits)
                  .arg(QString::number(e.nEarned / (double)COIN, 'f', 2))
                  .arg(e.nPuzzlesSolved)
                  .arg(e.nCoordSigned)
            : tr("🏆 %1 • %2 KRISTA (🧩 %3)")
                  .arg(e.nPaidHits)
                  .arg(QString::number(e.nEarned / (double)COIN, 'f', 2))
                  .arg(e.nPuzzlesSolved);
        QTableWidgetItem* itemHits = new QTableWidgetItem(hitsStr);
        QString hitsTooltip = tr("Paid Rewards Won (ADAM Era, Block #%1+): %2\n"
                                 "Total Earned by Miner: %3 KRISTA\n"
                                 "Committee Puzzles Solved: %4\n"
                                 "Coordinator Blocks Co-Signed (V11 PoW+PoS): %5\n"
                                 "Last Paid Block: %6 • Last Puzzle Block: %7 • Last Coord Block: %8")
            .arg(nAdamStartHeight)
            .arg(e.nPaidHits)
            .arg(QString::number(e.nEarned / (double)COIN, 'f', 8))
            .arg(e.nPuzzlesSolved)
            .arg(e.nCoordSigned)
            .arg(e.nLastHitHeight >= 0 ? QString("#%1").arg(e.nLastHitHeight) : tr("None yet"))
            .arg(e.nLastPuzzleHeight >= 0 ? QString("#%1").arg(e.nLastPuzzleHeight) : tr("None yet"))
            .arg(e.nLastCoordHeight >= 0 ? QString("#%1").arg(e.nLastCoordHeight) : tr("None yet"));
        if (!IsModelDActive(nTipHeight)) {
            hitsTooltip += "\n\n" + tr("Note: Individual committee seat payouts (40% split) activate at Model D (Block #%1). "
                                       "During Blocks #%2–#%3, 100% of the net block reward goes to the Block Producer (Coordinator / Staker).")
                .arg(nModelDStartHeight)
                .arg(nAdamStartHeight)
                .arg(nModelDStartHeight - 1);
        }
        itemHits->setToolTip(hitsTooltip);
        tableMyMiners->setItem((int)row, 3, itemHits);

        // Col 4: 1-Click Copy Address & External Miner Command Buttons
        QWidget* actionWidget = new QWidget(tableMyMiners);
        actionWidget->setStyleSheet("background: transparent; border: none;");
        QHBoxLayout* actionLayout = new QHBoxLayout(actionWidget);
        actionLayout->setContentsMargins(2, 1, 2, 1);
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

        actionLayout->addWidget(btnCopyAddr);
        actionLayout->addWidget(btnCopyCmd);
        tableMyMiners->setCellWidget((int)row, 4, actionWidget);
        tableMyMiners->setRowHeight((int)row, 28);
    }
#endif
}

void DashboardWidget::onToggleMiningClicked()
{
#ifdef ENABLE_WALLET
    if (!pwalletMain) {
        inform(tr("Wallet is not available."));
        return;
    }
    bool fCurrentlyGenerating = GetBoolArg("-gen", false);
    bool fNewState = !fCurrentlyGenerating;
    int nThreads = spinMinerThreads->value();

    if (fNewState && walletModel && walletModel->getEncryptionStatus() == WalletModel::Locked) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            inform(tr("Unlock wallet to start cooperative mining."));
            return;
        }
    }

    mapArgs["-gen"] = fNewState ? "1" : "0";
    mapArgs["-genproclimit"] = itostr(fNewState ? nThreads : 0);
    GenerateBitcoins(fNewState, pwalletMain, fNewState ? nThreads : 0);
    refreshMinerHubStatus();

    if (fNewState) {
        inform(tr("Cooperative CPU mining started with %1 threads!").arg(nThreads));
    } else {
        inform(tr("Cooperative CPU mining stopped."));
    }
#endif
}

void DashboardWidget::onMinerThreadsChanged(int nThreads)
{
#ifdef ENABLE_WALLET
    if (pwalletMain && GetBoolArg("-gen", false) && nThreads > 0) {
        mapArgs["-genproclimit"] = itostr(nThreads);
        GenerateBitcoins(true, pwalletMain, nThreads);
        refreshMinerHubStatus();
    }
#endif
}

void DashboardWidget::shutdownMinerWorkers()
{
    fShuttingDownMinerHub.store(true);
    if (minerStatusTimer) {
        minerStatusTimer->stop();
    }
    CancelMinerPoWRegistration();
    if (powRegWorkerThread.joinable()) {
        powRegWorkerThread.join();
    }
    powRegUnlockGuard.reset();
    isSolvingPoWReg.store(false);
}

void DashboardWidget::onRegisterPoWClicked()
{
    if (!walletModel || fShuttingDownMinerHub.load() || ShutdownRequested() || isSolvingPoWReg.load()) return;
    if (powRegWorkerThread.joinable()) {
        powRegWorkerThread.join();
    }

    auto unlockCtx = std::make_shared<WalletModel::UnlockContext>(walletModel->requestUnlock());
    if (!unlockCtx->isValid()) return;
    if (fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    powRegUnlockGuard = unlockCtx;

    ResetMinerPoWRegistrationCancel();
    isSolvingPoWReg.store(true);
    if (btnRegPoW) {
        btnRegPoW->setEnabled(false);
        btnRegPoW->setText(tr("⏳ Solving PoW..."));
    }

    powRegWorkerThread = std::thread([this]() {
        bool ok = false;
        QString resultOrError;
        try {
            JSONRPCRequest req;
            req.strMethod = "registerminer";
            req.params = UniValue(UniValue::VARR);
            req.params.push_back("pow");
            UniValue res = tableRPC.execute(req);
            ok = true;
            resultOrError = QString::fromStdString(res.get_str());
        } catch (const UniValue& objError) {
            resultOrError = QString::fromStdString(find_value(objError, "message").get_str());
        } catch (const std::exception& e) {
            resultOrError = QString::fromStdString(e.what());
        }
        QMetaObject::invokeMethod(this, "onPoWRegistrationFinished", Qt::QueuedConnection,
                                  Q_ARG(bool, ok), Q_ARG(QString, resultOrError));
    });
}

void DashboardWidget::onPoWRegistrationFinished(bool success, const QString& resultOrError)
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
        btnRegPoW->setText(tr("⚡ Register PoW Seat (0 / 10 KRISTA • 4%)"));
    }

    if (success) {
        inform(tr("PoW Seat Registered! TXID: %1").arg(resultOrError.left(24) + "..."));
        fForceMinerTableRefresh = true;
        refreshMinerHubStatus();
    } else {
        inform(resultOrError);
    }
}

void DashboardWidget::onRegisterCoinLockClicked()
{
    if (!walletModel || fShuttingDownMinerHub.load() || ShutdownRequested()) return;
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        JSONRPCRequest req;
        req.strMethod = "registerminer";
        req.params = UniValue(UniValue::VARR);
        req.params.push_back("lock");
        UniValue res = tableRPC.execute(req);
        QApplication::restoreOverrideCursor();
        inform(tr("250 KRISTA Coin-Lock Seat Registered! TXID: %1").arg(QString::fromStdString(res.get_str()).left(24) + "..."));
        fForceMinerTableRefresh = true;
        refreshMinerHubStatus();
    } catch (const UniValue& objError) {
        QApplication::restoreOverrideCursor();
        std::string message = find_value(objError, "message").get_str();
        inform(QString::fromStdString(message));
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        inform(QString::fromStdString(e.what()));
    }
}

#ifdef USE_QTCHARTS

void DashboardWidget::tryChartRefresh()
{
    if (!fShowCharts)
        return;
    if (hasStakes) {
        // First check that everything was loaded properly.
        if (!chart) {
            loadChart();
        } else {
            // Check for min update time to not reload the UI so often if the node is syncing.
            int64_t now = GetTime();
            if (lastRefreshTime + CHART_LOAD_MIN_TIME_INTERVAL < now) {
                lastRefreshTime = now;
                refreshChart();
            }
        }
    }
}

void DashboardWidget::setChartShow(ChartShowType type)
{
    this->chartShow = type;
    if (chartShow == MONTH) {
        ui->containerChartArrow->setVisible(true);
    } else {
        ui->containerChartArrow->setVisible(false);
    }
    if (isChartInitialized) refreshChart();
}

const QStringList monthsNames = {QObject::tr("Jan"), QObject::tr("Feb"), QObject::tr("Mar"), QObject::tr("Apr"),
                                 QObject::tr("May"), QObject::tr("Jun"), QObject::tr("Jul"), QObject::tr("Aug"),
                                 QObject::tr("Sep"), QObject::tr("Oct"), QObject::tr("Nov"), QObject::tr("Dec")};

void DashboardWidget::loadChart()
{
    if (hasStakes) {
        if (!chart) {
            showHideEmptyChart(false, false);
            initChart();
            QDate currentDate = QDate::currentDate();
            monthFilter = currentDate.month();
            yearFilter = currentDate.year();
            for (int i = 1; i < 13; ++i) ui->comboBoxMonths->addItem(QString(monthsNames[i-1]), QVariant(i));
            ui->comboBoxMonths->setCurrentIndex(monthFilter - 1);
            connect(ui->comboBoxMonths, static_cast<void (QComboBox::*)(const QString&)>(&QComboBox::currentIndexChanged),
                this, &DashboardWidget::onChartMonthChanged);
            connect(ui->pushButtonChartArrow, &QPushButton::clicked, [this](){ onChartArrowClicked(true); });
            connect(ui->pushButtonChartRight, &QPushButton::clicked, [this](){ onChartArrowClicked(false); });
        }
        refreshChart();
        changeChartColors();
    } else {
        showHideEmptyChart(true, false);
    }
}

void DashboardWidget::showHideEmptyChart(bool showEmpty, bool loading, bool forceView)
{
    if (stakesFilter->rowCount() > SHOW_EMPTY_CHART_VIEW_THRESHOLD || forceView) {
        ui->layoutChart->setVisible(!showEmpty);
        ui->emptyContainerChart->setVisible(showEmpty);
    }
    // Enable/Disable sort buttons
    bool invLoading = !loading;
    ui->comboBoxMonths->setEnabled(invLoading);
    ui->comboBoxYears->setEnabled(invLoading);
    ui->pushButtonMonth->setEnabled(invLoading);
    ui->pushButtonAll->setEnabled(invLoading);
    ui->pushButtonYear->setEnabled(invLoading);
    ui->labelEmptyChart->setText(loading ? tr("Loading chart..") : tr("You have no staking rewards"));
}

void DashboardWidget::initChart()
{
    chart = new QChart();
    axisX = new QBarCategoryAxis();
    axisY = new QValueAxis();

    // Chart style
    chart->legend()->setVisible(false);
    chart->legend()->setAlignment(Qt::AlignTop);
    chart->layout()->setContentsMargins(0, 0, 0, 0);
    chart->setMargins({0, 0, 0, 0});
    chart->setBackgroundRoundness(0);
    // Axis
    chart->addAxis(axisX, Qt::AlignBottom);
    chart->addAxis(axisY, Qt::AlignRight);
    chart->setAnimationOptions(QChart::SeriesAnimations);

    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setRubberBand( QChartView::HorizontalRubberBand );
    chartView->setContentsMargins(0,0,0,0);

    QHBoxLayout *baseScreensContainer = new QHBoxLayout(this);
    baseScreensContainer->setMargin(0);
    baseScreensContainer->addWidget(chartView);
    ui->chartContainer->setLayout(baseScreensContainer);
    ui->chartContainer->setContentsMargins(0,0,0,0);
    setCssProperty(ui->chartContainer, "container-chart");
}

void DashboardWidget::changeChartColors()
{
    QColor gridLineColor;
    QColor labelsColor;
    QColor backgroundColor;
    
    if (isLightTheme()) {
        gridLineColor = QColor("#1a000000");
        labelsColor = QColor("#77000000");
        backgroundColor = QColor(255,255,255);
    } else {
        gridLineColor = QColor("#40ffffff");
        labelsColor = QColor("#a0ffffff");
        backgroundColor = QColor(13, 19, 33);
    }

    axisX->setGridLineColor(backgroundColor);
    axisY->setGridLineColor(gridLineColor);
    axisX->setLabelsColor(labelsColor);
    axisY->setLabelsColor(labelsColor);
    axisX->setLinePenColor(backgroundColor);
    axisY->setLinePenColor(backgroundColor);
    chart->setBackgroundBrush(QBrush(backgroundColor));
    if (set0) set0->setBorderColor(backgroundColor);
    if (set1) set1->setBorderColor(backgroundColor);
}

void DashboardWidget::updateStakeFilter()
{
    if (chartShow != ALL) {
        bool filterByMonth = false;
        if (monthFilter != 0 && chartShow == MONTH) {
            filterByMonth = true;
        }
        if (yearFilter != 0) {
            if (filterByMonth) {
                QDate monthFirst = QDate(yearFilter, monthFilter, 1);
                stakesFilter->setDateRange(
                        QDateTime(monthFirst),
                        QDateTime(QDate(yearFilter, monthFilter, monthFirst.daysInMonth())).addDays(1).addMSecs(-1)
                );
            } else {
                stakesFilter->setDateRange(
                        QDateTime(QDate(yearFilter, 1, 1)),
                        QDateTime(QDate(yearFilter, 12, 31)).addDays(1).addMSecs(-1)
                );
            }
        } else if (filterByMonth) {
            QDate currentDate = QDate::currentDate();
            QDate monthFirst = QDate(currentDate.year(), monthFilter, 1);
            stakesFilter->setDateRange(
                    QDateTime(monthFirst),
                    QDateTime(QDate(currentDate.year(), monthFilter, monthFirst.daysInMonth())).addDays(1).addMSecs(-1)
            );
            ui->comboBoxYears->setCurrentText(QString::number(currentDate.year()));
        } else {
            stakesFilter->clearDateRange();
        }
    } else {
        stakesFilter->clearDateRange();
    }
}

// pair KRISTA
const QMap<int, QMap<QString, qint64>> DashboardWidget::getAmountBy()
{
    const int size = stakesFilter->rowCount();
    QMap<int, QMap<QString, qint64>> amountBy;
    // Get all of the stakes
    for (int i = 0; i < size; ++i) {
        QModelIndex modelIndex = stakesFilter->index(i, TransactionTableModel::ToAddress);
        qint64 amount = llabs(modelIndex.data(TransactionTableModel::AmountRole).toLongLong());
        QDate date = modelIndex.data(TransactionTableModel::DateRole).toDateTime().date();
        bool isPiv = modelIndex.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::StakeMint;
		bool isMN = modelIndex.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::MNReward ||
		            modelIndex.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::LlmqReward ||
		            modelIndex.data(TransactionTableModel::TypeRole).toInt() == TransactionRecord::AdamReward;

        int time = 0;
        switch (chartShow) {
            case YEAR: {
                time = date.month();
                break;
            }
            case ALL: {
                time = date.year();
                break;
            }
            case MONTH: {
                time = date.day();
                break;
            }
            default:
                inform(tr("Error loading chart, invalid show option"));
                return amountBy;
        }
        if (amountBy.contains(time)) {
            if (isPiv) {
                amountBy[time]["piv"] += amount;
            } else if (isMN) {
                amountBy[time]["mn"] += amount;
                hasMNRewards = true;
            }
        } else {
            if (isPiv) {
                amountBy[time]["piv"] = amount;
                amountBy[time]["mn"] = 0;
            } else if (isMN) {
                amountBy[time]["piv"] = 0;
                amountBy[time]["mn"] = amount;
                hasMNRewards = true;
            }
        }
    }
    return amountBy;
}

bool DashboardWidget::loadChartData(bool withMonthNames)
{
    if (chartData) {
        delete chartData;
        chartData = nullptr;
    }

    chartData = new ChartData();
    chartData->amountsByCache = getAmountBy(); // pair KRISTA

    std::pair<int,int> range = getChartRange(chartData->amountsByCache);
    if (range.first == 0 && range.second == 0) {
        // Problem loading the chart.
        return false;
    }

    bool isOrderedByMonth = chartShow == MONTH;
    int daysInMonth = QDate(yearFilter, monthFilter, 1).daysInMonth();

    for (int j = range.first; j < range.second; j++) {
        int num = (isOrderedByMonth && j > daysInMonth) ? (j % daysInMonth) : j;
        qreal piv = 0;
		qreal mnrewards = 0;
        if (chartData->amountsByCache.contains(num)) {
            QMap<QString, qint64> pair = chartData->amountsByCache[num];
            piv = (pair["piv"] != 0) ? pair["piv"] / 100000000 : 0;
            mnrewards = (pair["mn"] != 0) ? pair["mn"] / 100000000 : 0;
            chartData->totalPiv += pair["piv"];
            chartData->totalMNRewards += pair["mn"];
        }

        chartData->xLabels << ((withMonthNames) ? monthsNames[num - 1] : QString::number(num));

        chartData->valuesPiv.append(piv);    
        chartData->valuesMNRewards.append(mnrewards);    

        int max = std::max(piv, mnrewards);
        if (max > chartData->maxValue) {
            chartData->maxValue = max;
        }
    }

    return true;
}

void DashboardWidget::onChartYearChanged(const QString& yearStr)
{
    if (isChartInitialized) {
        int newYear = yearStr.toInt();
        if (newYear != yearFilter) {
            yearFilter = newYear;
            refreshChart();
        }
    }
}

void DashboardWidget::onChartMonthChanged(const QString& monthStr)
{
    if (isChartInitialized) {
        int newMonth = ui->comboBoxMonths->currentData().toInt();
        if (newMonth != monthFilter) {
            monthFilter = newMonth;
            refreshChart();
#ifndef Q_OS_MAC
        // quick hack to re paint the chart view.
        chart->removeSeries(series);
        chart->addSeries(series);
#endif
        }
    }
}

bool DashboardWidget::refreshChart()
{
    if (isLoading) return false;
    isLoading = true;
    isChartMin = width() < 1300;
    isChartInitialized = false;
    showHideEmptyChart(true, true);
    updateStakeFilter();
    return execute(REQUEST_LOAD_TASK);
}

void DashboardWidget::onChartRefreshed()
{
    if (chart) {
        if (series) {
            series->clear();
            series->detachAxis(axisX);
            series->detachAxis(axisY);
        }
        axisX->clear();
    }
    // init sets
    set0 = new QBarSet(CURRENCY_UNIT.c_str());
	set1 = new QBarSet("MN_" + QString(CURRENCY_UNIT.c_str()));
    set0->setColor(QColor(245,137,124));
	set1->setColor(QColor(118,149,205));

    if (!series) {
        series = new QBarSeries();
        chart->addSeries(series);
    }
    series->attachAxis(axisX);
    series->attachAxis(axisY);

    set0->append(chartData->valuesPiv);
    set1->append(chartData->valuesMNRewards);

    // Total
    nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
    if (chartData->totalPiv > 0 || chartData->totalMNRewards > 0) {
        setCssProperty(ui->labelAmountPiv, "text-stake-piv");
		setCssProperty(ui->labelAmountMNRewards, "text-stake-mnrewards");
    } else {
        setCssProperty(ui->labelAmountPiv, "text-stake-piv-disable");
		setCssProperty(ui->labelAmountMNRewards, "text-stake-mnrewards-disable");
    }

	forceUpdateStyle({ui->labelAmountPiv, ui->labelAmountMNRewards});
    ui->labelAmountPiv->setText(GUIUtil::formatBalance(chartData->totalPiv, nDisplayUnit));
	ui->labelAmountMNRewards->setText(GUIUtil::formatBalance(chartData->totalMNRewards, nDisplayUnit));

    series->append(set0);

	if(hasMNRewards)
		series->append(set1);

    // bar width
    if (chartShow == YEAR)
        series->setBarWidth(0.8);
    else {
        series->setBarWidth(0.3);
    }
    axisX->append(chartData->xLabels);
    axisY->setRange(0, chartData->maxValue);

    // Controllers
    switch (chartShow) {
        case ALL: {
            ui->container_chart_dropboxes->setVisible(false);
            break;
        }
        case YEAR: {
            ui->container_chart_dropboxes->setVisible(true);
            ui->containerBoxMonths->setVisible(false);
            break;
        }
        case MONTH: {
            ui->container_chart_dropboxes->setVisible(true);
            ui->containerBoxMonths->setVisible(true);
            break;
        }
        default: break;
    }

    // Refresh years filter, first address created is the start
    int yearStart = QDateTime::fromTime_t(static_cast<uint>(walletModel->getCreationTime())).date().year();
    int currentYear = QDateTime::currentDateTime().date().year();

    QString selection;
    if (ui->comboBoxYears->count() > 0) {
        selection = ui->comboBoxYears->currentText();
        isChartInitialized = false;
    }
    ui->comboBoxYears->clear();
    if (yearStart == currentYear) {
        ui->comboBoxYears->addItem(QString::number(currentYear));
    } else {
        for (int i = yearStart; i < (currentYear + 1); ++i)ui->comboBoxYears->addItem(QString::number(i));
    }

    if (!selection.isEmpty()) {
        ui->comboBoxYears->setCurrentText(selection);
        isChartInitialized = true;
    } else {
        ui->comboBoxYears->setCurrentText(QString::number(currentYear));
    }

    // back to normal
    isChartInitialized = true;
    showHideEmptyChart(false, false, true);
    isLoading = false;
}

std::pair<int, int> DashboardWidget::getChartRange(QMap<int, QMap<QString, qint64>> amountsBy)
{
    switch (chartShow) {
        case YEAR:
            return std::make_pair(1, 13);
        case ALL: {
            QList<int> keys = amountsBy.uniqueKeys();
            if (keys.isEmpty()) {
                // This should never happen, ALL means from the beginning of time and if this is called then it must have at least one stake..
                inform(tr("Error loading chart, invalid data"));
                return std::make_pair(0, 0);
            }
            std::sort(keys.begin(), keys.end());
            return std::make_pair(keys.first(), keys.last() + 1);
        }
        case MONTH:
            return std::make_pair(dayStart, dayStart + 9);
        default:
            inform(tr("Error loading chart, invalid show option"));
            return std::make_pair(0, 0);
    }
}

void DashboardWidget::updateAxisX(const QStringList* args)
{
    axisX->clear();
    QStringList months;
    std::pair<int,int> range = getChartRange(chartData->amountsByCache);
    if (args) {
        months = *args;
    } else {
        for (int i = range.first; i < range.second; i++) months << QString::number(i);
    }
    axisX->append(months);
}

void DashboardWidget::onChartArrowClicked(bool goLeft)
{
    bool updateMonth = false;
    bool updateYear = false;
    int dataenddate = getChartRange(chartData->amountsByCache).second;
    QDate currentDate = QDate::currentDate();
    if (goLeft) {
        dayStart--;
        if (dayStart == 0) {
            updateMonth = true;
            if (monthFilter == 1) {
                // Prev year
                monthFilter = 12;
                yearFilter--;
                updateYear = true;
            } else {
                monthFilter--; // Prev month
            }
            dayStart = QDate(yearFilter, monthFilter, 1).daysInMonth();
        }
    } else {
        int dayInMonth = QDate(yearFilter, monthFilter, dayStart).daysInMonth();
        dayStart++;
        if (dayStart > dayInMonth) {
            dayStart = 1;
            updateMonth = true;
            if (monthFilter == 12) {
                // Next year
                monthFilter = 1;
                yearFilter++;
                updateYear = true;
            } else {
                monthFilter++; // Next month
            }
        }
    }

    refreshChart();
    //Check if data end day is current date and monthfilter is current month
    bool fEndDayisCurrent = dataenddate  == currentDate.day() && monthFilter == currentDate.month();

    if (updateMonth)
        ui->comboBoxMonths->setCurrentIndex(monthFilter - 1);

    if (updateYear)
        ui->comboBoxYears->setCurrentText(QString::number(yearFilter));

    // enable/disable the pushButtonChartRight.
    ui->pushButtonChartRight->setEnabled(!fEndDayisCurrent);


}

void DashboardWidget::windowResizeEvent(QResizeEvent* event)
{
    if (hasStakes && axisX) {
        if (width() > 1300) {
            if (isChartMin) {
                isChartMin = false;
                switch (chartShow) {
                    case YEAR: {
                        updateAxisX(&monthsNames);
                        break;
                    }
                    case ALL: break;
                    case MONTH: {
                        updateAxisX();
                        break;
                    }
                    default:
                        inform(tr("Error loading chart, invalid show option"));
                        return;
                }
                chartView->repaint();
            }
        } else {
            if (!isChartMin) {
                updateAxisX();
                isChartMin = true;
            }
        }
    }
}

void DashboardWidget::onHideChartsChanged(bool fHide)
{
    fShowCharts = !fHide;
    ui->right->setVisible(true);
    ui->labelTitle2->setVisible(fShowCharts);
    ui->labelMessage->setVisible(fShowCharts);
    ui->labelAmountPiv->setVisible(fShowCharts);
    ui->labelAmountMNRewards->setVisible(fShowCharts);
    if (!fShowCharts) {
        ui->layoutChart->setVisible(false);
        ui->emptyContainerChart->setVisible(false);
    } else {
        tryChartRefresh();
    }
}

#endif

void DashboardWidget::run(int type)
{
#ifdef USE_QTCHARTS
    if (type == REQUEST_LOAD_TASK) {
        bool withMonthNames = !isChartMin && (chartShow == YEAR);
        if (loadChartData(withMonthNames))
            QMetaObject::invokeMethod(this, "onChartRefreshed", Qt::QueuedConnection);
    }
#endif
}
void DashboardWidget::onError(QString error, int type)
{
    inform(tr("Error loading chart: %1").arg(error));
}

void DashboardWidget::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent notifications-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    if (!txModel || txModel->processingQueuedTransactions())
        return;

    QString date = txModel->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = txModel->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = txModel->index(start, TransactionTableModel::Type, parent).data().toString();
    QString address = txModel->index(start, TransactionTableModel::ToAddress, parent).data().toString();

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address);
}

DashboardWidget::~DashboardWidget()
{
    shutdownMinerWorkers();
#ifdef USE_QTCHARTS
    delete chart;
#endif
    delete ui;
}
