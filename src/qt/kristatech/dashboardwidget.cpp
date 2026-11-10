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
#include "rpc/server.h"
#include <vector>
#include <thread>
#include <QApplication>
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
    ui->right->setVisible(true);

    minerHubCard = new QFrame(ui->right);
    minerHubCard->setObjectName("minerHubCard");
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

    ui->verticalLayout_2->insertWidget(0, minerHubCard);
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

    minerHubCard->setStyleSheet(QString(
        "QFrame#minerHubCard { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
    ).arg(cardBg, cardBorder));

    lblMinerHubTitle->setStyleSheet(QString("color: %1; background: transparent; border: none;").arg(textPrimary));
    lblMinerThreads->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 12px;").arg(textSecondary));
    lblTrackSection->setStyleSheet(QString("color: %1; background: transparent; border: none; font-size: 11px;").arg(textSecondary));

    spinMinerThreads->setStyleSheet(QString(
        "QSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 5px; padding: 3px 8px; font-weight: bold; }"
    ).arg(spinBg, textPrimary, cardBorder));

    QString regBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 8px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background-color: %4; }"
    ).arg(trackBtnBg, textPrimary, cardBorder, trackBtnHover);

    btnRegPoW->setStyleSheet(regBtnStyle);
    btnRegCoinLock->setStyleSheet(regBtnStyle);
    refreshMinerHubStatus();
}

void DashboardWidget::refreshMinerHubStatus()
{
    if (!minerHubCard) return;
    bool fGenerating = GetBoolArg("-gen", false);
    int nThreads = (int)GetArg("-genproclimit", spinMinerThreads->value());
    if (nThreads <= 0) nThreads = std::max(1, (int)std::thread::hardware_concurrency());

    size_t nPoolSize = 0;
    {
        TRY_LOCK(cs_main, lockMain);
        if (lockMain && chainActive.Tip()) {
            nPoolSize = GetAdamMinerPool(chainActive.Height()).size();
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

void DashboardWidget::onRegisterPoWClicked()
{
    if (!walletModel) return;
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        JSONRPCRequest req;
        req.strMethod = "registerminer";
        req.params = UniValue(UniValue::VARR);
        req.params.push_back("pow");
        UniValue res = tableRPC.execute(req);
        QApplication::restoreOverrideCursor();
        inform(tr("PoW Seat Registered! TXID: %1").arg(QString::fromStdString(res.get_str()).left(24) + "..."));
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

void DashboardWidget::onRegisterCoinLockClicked()
{
    if (!walletModel) return;
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
#ifdef USE_QTCHARTS
    delete chart;
#endif
    delete ui;
}
