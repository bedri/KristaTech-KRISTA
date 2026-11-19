// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/dashboardwidget.h"
#include "qt/kristatech/forms/ui_dashboardwidget.h"
#include "qt/kristatech/minerhubwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/defaultdialog.h"
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
#include "masternodeman.h"
#include "llmq.h"
#include "wallet/wallet.h"
#include "rpc/server.h"
#include <vector>
#include <thread>
#include <set>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QClipboard>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QModelIndex>
#include <QList>
#include <QMenu>
#include <QGraphicsLayout>
#include <QScrollBar>
#include <QSettings>
#include <QToolTip>

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
    ui->labelMessage->setText(tr("Amount of KRISTA earned via Staking, Mining & Masternodes"));
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
    initComboBox(ui->comboBoxMonths, "btn-combo-chart-selected");
    initComboBox(ui->comboBoxYears, "btn-combo-chart-selected");
    ui->pushButtonYear->setChecked(true);

    ui->pushButtonAll->setCursor(Qt::PointingHandCursor);
    ui->pushButtonMonth->setCursor(Qt::PointingHandCursor);
    ui->pushButtonYear->setCursor(Qt::PointingHandCursor);
    ui->comboBoxMonths->setCursor(Qt::PointingHandCursor);
    ui->comboBoxYears->setCursor(Qt::PointingHandCursor);
    ui->pushButtonChartArrow->setCursor(Qt::PointingHandCursor);
    ui->pushButtonChartRight->setCursor(Qt::PointingHandCursor);

    ui->pushButtonYear->setToolTip(tr("Show monthly reward totals for the selected year"));
    ui->pushButtonMonth->setToolTip(tr("Show daily reward totals for the selected month"));
    ui->pushButtonAll->setToolTip(tr("Show yearly reward totals across all history"));
    ui->comboBoxYears->setToolTip(tr("Select year"));
    ui->comboBoxMonths->setToolTip(tr("Select month"));
    ui->pushButtonChartArrow->setToolTip(tr("Previous days"));
    ui->pushButtonChartRight->setToolTip(tr("Next days"));

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

    // Instant Transaction Search Bar (Address, Label, or TxID)
    lineEditSearchTx = new QLineEdit(ui->left);
    lineEditSearchTx->setClearButtonEnabled(true);
    styleLineEditClearButton(lineEditSearchTx);
    lineEditSearchTx->setMinimumHeight(38);
    lineEditSearchTx->setPlaceholderText(tr("Search transactions by address, label, or TxID..."));
    initCssEditLine(lineEditSearchTx, true);
    ui->verticalLayout22->addSpacing(10);
    ui->verticalLayout22->addWidget(lineEditSearchTx);
    connect(lineEditSearchTx, &QLineEdit::textChanged, this, &DashboardWidget::onSearchTxChanged);

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
    initRewardChartUx();
#ifdef USE_QTCHARTS
    fShowCharts = getSettings()->value("dashboardChartVisible", true).toBool();
    setChartVisible(fShowCharts, false);
#else
    setChartVisible(false, false);
#endif
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
            if (lineEditSearchTx) lineEditSearchTx->setVisible(false);
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
                updateMinerHubLayoutState();
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

        connect(stakesFilter, &QAbstractItemModel::rowsInserted, this, [this]() {
            if (!stakesFilter) return;
            const bool nowHasStakes = stakesFilter->rowCount() > 0;
            if (nowHasStakes && (!hasStakes || !chart || fChartEmptyDesired)) {
                hasStakes = true;
                tryChartRefresh();
                updateMinerHubLayoutState();
            }
        });

        const bool savedVisible = getSettings()->value("dashboardChartVisible",
            !walletModel->getOptionsModel()->isHideCharts()).toBool();
        setChartVisible(savedVisible, false);
        connect(walletModel->getOptionsModel(), &OptionsModel::hideChartsChanged, this, &DashboardWidget::onHideChartsChanged);

        QTimer::singleShot(200, this, [this]() {
            if (stakesFilter && fShowCharts && (!chart || fChartEmptyDesired)) {
                hasStakes = stakesFilter->rowCount() > 0;
                if (hasStakes) {
                    tryChartRefresh();
                    updateMinerHubLayoutState();
                }
            }
        });
#endif
    }
    // update the display unit, to not use the default ("KRISTA")
    updateDisplayUnit();
    if (minerHubWidget) {
        minerHubWidget->setWalletModel(walletModel);
    }
}

void DashboardWidget::onTxArrived(const QString& hash, const bool& isCoinStake)
{
    showList();
    if (minerHubWidget) {
        minerHubWidget->refreshMinerHubStatus();
    }
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
        updateMinerHubLayoutState();
    }
#endif
}

void DashboardWidget::showList()
{
    const bool hasAnyWalletTx = (txModel && txModel->size() > 0);
    ui->comboBoxSortType->setVisible(hasAnyWalletTx);
    ui->comboBoxSort->setVisible(hasAnyWalletTx);
    if (lineEditSearchTx) lineEditSearchTx->setVisible(hasAnyWalletTx);

    if (!filter || filter->rowCount() == 0) {
        ui->emptyContainer->setVisible(true);
        ui->listTransactions->setVisible(false);
        ui->labelEmpty->setText(hasAnyWalletTx ? tr("No matching transactions found") : tr("No transactions yet"));
    } else {
        ui->emptyContainer->setVisible(false);
        ui->listTransactions->setVisible(true);
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
    showList();
}

void DashboardWidget::onSearchTxChanged(const QString& query)
{
    if (!filter) return;
    filter->setAddressPrefix(query.trimmed());
    ui->listTransactions->update();
    showList();
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
    styleLineEditClearButton(lineEditSearchTx);
    updateRewardChartTheme();
#ifdef USE_QTCHARTS
    if (chart) this->changeChartColors();
#endif
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

void DashboardWidget::initMinerHub()
{
    fMinerHubHidden = false;
    fMinerHubCollapsed = false;

    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20, 20, 20, 20);
#ifdef USE_QTCHARTS
    ui->right->setVisible(true);
#else
    ui->right->setVisible(false);
    ui->labelTitle2->setVisible(false);
    ui->labelMessage->setVisible(false);
    ui->labelAmountPiv->setVisible(false);
    ui->labelAmountMNRewards->setVisible(false);
    ui->layoutChart->setVisible(false);
    ui->emptyContainerChart->setVisible(false);
    ui->layoutShadow->setVisible(false);
#endif

    // Quick-jump Miner Hub status pill on the Home Transactions header
    btnToggleMinerHubVisibility = new QPushButton(tr("○ Open Miner Hub"), ui->left_top_container);
    btnToggleMinerHubVisibility->setObjectName("btnToggleMinerHubVisibility");
    btnToggleMinerHubVisibility->setMinimumHeight(34);
    btnToggleMinerHubVisibility->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    btnToggleMinerHubVisibility->setCursor(Qt::PointingHandCursor);
    btnToggleMinerHubVisibility->setFocusPolicy(Qt::NoFocus);
    btnToggleMinerHubVisibility->setToolTip(tr("Open the dedicated Miner & Validator Hub page"));
    ui->horizontalLayout->addSpacing(8);
    ui->horizontalLayout->addWidget(btnToggleMinerHubVisibility);

    minerHubWidget = new MinerHubWidget(window, window);
    connect(minerHubWidget, &MinerHubWidget::homeStatusPillUpdated, this, [this](const QString& text, const QString& style) {
        if (btnToggleMinerHubVisibility) {
            btnToggleMinerHubVisibility->setText(text);
            btnToggleMinerHubVisibility->setStyleSheet(style);
            btnToggleMinerHubVisibility->adjustSize();
        }
    });
    connect(btnToggleMinerHubVisibility, &QPushButton::clicked, this, &DashboardWidget::onToggleMinerHubVisibilityClicked);
    minerHubWidget->refreshMinerHubStatus();
}

void DashboardWidget::updateMinerHubLayoutState()
{
    if (!ui) return;
    ui->left->setVisible(true);

    bool showChartSection = false;
#ifdef USE_QTCHARTS
    showChartSection = fShowCharts;
#endif

    ui->right->setVisible(showChartSection);
    if (showChartSection) {
        ui->right->setContentsMargins(20, 20, 20, 20);
    }
    ui->labelTitle2->setVisible(showChartSection);
    ui->labelMessage->setVisible(showChartSection);
    ui->labelAmountPiv->setVisible(showChartSection);
    ui->labelAmountMNRewards->setVisible(showChartSection);
    ui->layoutChart->setVisible(showChartSection && !fChartEmptyDesired);
    ui->emptyContainerChart->setVisible(showChartSection && fChartEmptyDesired);
    ui->layoutShadow->setVisible(showChartSection);
    if (ui->verticalLayout_2) ui->verticalLayout_2->invalidate();
    if (ui->verticalLayout_31) ui->verticalLayout_31->invalidate();
}

void DashboardWidget::onToggleMinerHubCollapseClicked()
{
}

void DashboardWidget::onToggleMinerHubVisibilityClicked()
{
    if (window) window->goToMinerHub();
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
        fChartEmptyDesired = showEmpty;
        updateMinerHubLayoutState();
    }
    // Enable/Disable sort buttons
    bool invLoading = !loading;
    ui->comboBoxMonths->setEnabled(invLoading);
    ui->comboBoxYears->setEnabled(invLoading);
    ui->pushButtonMonth->setEnabled(invLoading);
    ui->pushButtonAll->setEnabled(invLoading);
    ui->pushButtonYear->setEnabled(invLoading);
    ui->labelEmptyChart->setText(loading ? tr("Loading chart..") : tr("No rewards recorded in this period"));
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
    chart->setAnimationOptions(QChart::NoAnimation);

    chartView = new QChartView(chart);
    chartView->setRenderHint(QPainter::Antialiasing);
    chartView->setRubberBand(QChartView::NoRubberBand);
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
        labelsColor = QColor("#475569");
        backgroundColor = QColor(255,255,255);
    } else {
        gridLineColor = QColor("#40ffffff");
        labelsColor = QColor("#CBD5E1");
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
            piv = (pair["piv"] != 0) ? ((qreal)pair["piv"] / (qreal)COIN) : 0;
            mnrewards = (pair["mn"] != 0) ? ((qreal)pair["mn"] / (qreal)COIN) : 0;
            chartData->totalPiv += pair["piv"];
            chartData->totalMNRewards += pair["mn"];
        }

        chartData->xLabels << ((withMonthNames) ? monthsNames[num - 1] : QString::number(num));

        chartData->valuesPiv.append(piv);    
        chartData->valuesMNRewards.append(mnrewards);    

        qreal max = std::max(piv, mnrewards);
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
        }
    }
}

bool DashboardWidget::refreshChart()
{
    if (isLoading) return false;
    isLoading = true;
    isChartMin = width() < 1300;
    isChartInitialized = false;
    if (!chart || fChartEmptyDesired) {
        showHideEmptyChart(true, true);
    }
    updateStakeFilter();
    return execute(REQUEST_LOAD_TASK);
}

void DashboardWidget::onChartRefreshed()
{
    if (!chart || !chartData) return;

    chart->removeAllSeries();
    series = nullptr;
    stackedSeries = nullptr;
    set0 = nullptr;
    set1 = nullptr;
    axisX->clear();

    // Update totals & percentage shares in the 3 summary cards
    nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
    if (chartData->totalPiv > 0 || chartData->totalMNRewards > 0) {
        setCssProperty(ui->labelAmountPiv, "text-stake-piv");
        setCssProperty(ui->labelAmountMNRewards, "text-stake-mnrewards");
    } else {
        setCssProperty(ui->labelAmountPiv, "text-stake-piv-disable");
        setCssProperty(ui->labelAmountMNRewards, "text-stake-mnrewards-disable");
    }
    forceUpdateStyle({ui->labelAmountPiv, ui->labelAmountMNRewards});

    const CAmount totalCombined = chartData->totalPiv + chartData->totalMNRewards;
    ui->labelAmountPiv->setText(formatCompactKrista(chartData->totalPiv, nDisplayUnit));
    ui->labelAmountMNRewards->setText(formatCompactKrista(chartData->totalMNRewards, nDisplayUnit));
    if (lblChartTotalAmount) {
        lblChartTotalAmount->setText(formatCompactKrista(totalCombined, nDisplayUnit));
    }

    const double posPct = (totalCombined > 0) ? (100.0 * static_cast<double>(chartData->totalPiv) / static_cast<double>(totalCombined)) : 0.0;
    const double mnPct = (totalCombined > 0) ? (100.0 * static_cast<double>(chartData->totalMNRewards) / static_cast<double>(totalCombined)) : 0.0;
    if (lblChartCardStakingShare) {
        lblChartCardStakingShare->setText(QString("%1% of total").arg(posPct, 0, 'f', 1));
    }
    if (lblChartCardMnShare) {
        lblChartCardMnShare->setText(QString("%1% of total").arg(mnPct, 0, 'f', 1));
    }
    if (lblChartCardTotalMeta) {
        lblChartCardTotalMeta->setText(tr("All tracks (100%)"));
    }

    if (frameChartCardStaking) {
        frameChartCardStaking->setToolTip(tr("Proof-of-Stake (StakeMint) rewards\nExact: %1 (%2%)\nClick to filter Staking (PoS)")
            .arg(BitcoinUnits::formatWithUnit(nDisplayUnit, chartData->totalPiv, false, BitcoinUnits::separatorStandard))
            .arg(posPct, 0, 'f', 1));
    }
    if (frameChartCardMn) {
        frameChartCardMn->setToolTip(tr("Masternode, LLMQ & PoW Mining rewards\nExact: %1 (%2%)\nClick to filter MN & Mining")
            .arg(BitcoinUnits::formatWithUnit(nDisplayUnit, chartData->totalMNRewards, false, BitcoinUnits::separatorStandard))
            .arg(mnPct, 0, 'f', 1));
    }
    if (frameChartCardTotal) {
        frameChartCardTotal->setToolTip(tr("Combined rewards for the selected period\nExact: %1\nClick to show all reward tracks")
            .arg(BitcoinUnits::formatWithUnit(nDisplayUnit, totalCombined, false, BitcoinUnits::separatorStandard)));
    }

    const QStringList xLabelsCopy = chartData->xLabels;
    const int unitCopy = nDisplayUnit;

    if (chartVisualMode == 2) {
        // Donut Split view
        axisX->setVisible(false);
        axisY->setVisible(false);
        chart->setMargins({14, 18, 14, 14});

        QPieSeries* pie = new QPieSeries(chart);
        pie->setHoleSize(0.54);
        pie->setPieSize(0.62);

        const double posCoins = static_cast<double>(chartData->totalPiv) / static_cast<double>(COIN);
        const double mnCoins = static_cast<double>(chartData->totalMNRewards) / static_cast<double>(COIN);
        const QColor borderCol = isLightTheme() ? QColor("#FFFFFF") : QColor("#0D1321");
        const QColor labelCol = isLightTheme() ? QColor("#334155") : QColor("#E2E8F0");

        if (posCoins > 0.0 && chartSeriesFilter != 2) {
            QPieSlice* sPos = pie->append(QString("PoS (%1%)").arg(posPct, 0, 'f', 1), posCoins);
            sPos->setColor(QColor(245, 137, 124));
            sPos->setBorderColor(borderCol);
            sPos->setBorderWidth(2);
            sPos->setLabelColor(labelCol);
            sPos->setLabelArmLengthFactor(0.10);
            sPos->setLabelVisible(true);
            sPos->setExploded(chartSeriesFilter == 1);
            sPos->setProperty("exactAmount", static_cast<qint64>(chartData->totalPiv));
            sPos->setProperty("trackTitle", tr("Staking (PoS)"));
        }
        if (mnCoins > 0.0 && chartSeriesFilter != 1) {
            QPieSlice* sMn = pie->append(QString("MN & Mining (%1%)").arg(mnPct, 0, 'f', 1), mnCoins);
            sMn->setColor(QColor(118, 149, 205));
            sMn->setBorderColor(borderCol);
            sMn->setBorderWidth(2);
            sMn->setLabelColor(labelCol);
            sMn->setLabelArmLengthFactor(0.10);
            sMn->setLabelVisible(true);
            sMn->setExploded(chartSeriesFilter == 2);
            sMn->setProperty("exactAmount", static_cast<qint64>(chartData->totalMNRewards));
            sMn->setProperty("trackTitle", tr("MN & Mining"));
        }

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
    } else if (chartVisualMode == 1) {
        // Cumulative Trend view (QAreaSeries + QLineSeries)
        axisX->setVisible(true);
        axisY->setVisible(true);
        chart->setMargins({0, 0, 0, 0});
        axisX->append(chartData->xLabels);

        QLineSeries* upper = new QLineSeries();
        const int count = std::min(chartData->valuesPiv.size(), chartData->valuesMNRewards.size());
        double running = 0.0;
        QVector<double> periodDeltas;
        periodDeltas.reserve(count);

        for (int i = 0; i < count; ++i) {
            double delta = 0.0;
            if (chartSeriesFilter == 1) delta = chartData->valuesPiv.at(i);
            else if (chartSeriesFilter == 2) delta = chartData->valuesMNRewards.at(i);
            else delta = chartData->valuesPiv.at(i) + chartData->valuesMNRewards.at(i);
            running += delta;
            periodDeltas.append(delta);
            upper->append(i, running);
        }

        const QColor lineColor = (chartSeriesFilter == 1) ? QColor(245, 137, 124) : QColor(118, 149, 205);
        QColor fillColor = lineColor;
        fillColor.setAlpha(60);

        QAreaSeries* area = new QAreaSeries(upper, nullptr);
        upper->setParent(area);
        area->setPen(QPen(lineColor, 2.5));
        area->setBrush(QBrush(fillColor));
        area->setPointsVisible(true);

        connect(area, &QAreaSeries::hovered, area, [xLabelsCopy, periodDeltas, unitCopy](const QPointF& pt, bool state) {
            if (!state) {
                QToolTip::hideText();
                return;
            }
            const int idx = std::max(0, std::min(static_cast<int>(std::llround(pt.x())), xLabelsCopy.size() - 1));
            const QString period = (idx >= 0 && idx < xLabelsCopy.size()) ? xLabelsCopy.at(idx) : QString::number(idx + 1);
            const CAmount cumSat = static_cast<CAmount>(std::llround(pt.y() * COIN));
            const CAmount perSat = (idx >= 0 && idx < periodDeltas.size())
                ? static_cast<CAmount>(std::llround(periodDeltas.at(idx) * COIN)) : 0;
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
        // Stacked Bars view (default)
        axisX->setVisible(true);
        axisY->setVisible(true);
        chart->setMargins({0, 0, 0, 0});
        axisX->append(chartData->xLabels);

        set0 = new QBarSet(tr("Staking (PoS)"));
        set1 = new QBarSet(tr("MN & Mining"));
        set0->setColor(QColor(245, 137, 124));
        set1->setColor(QColor(118, 149, 205));
        const QColor barBorder = isLightTheme() ? QColor("#FFFFFF") : QColor("#0D1321");
        set0->setBorderColor(barBorder);
        set1->setBorderColor(barBorder);

        auto bindBarHover = [xLabelsCopy, unitCopy](QBarSet* barSet, const QString& trackName) {
            connect(barSet, &QBarSet::hovered, barSet, [barSet, xLabelsCopy, unitCopy, trackName](bool status, int index) {
                if (!status || index < 0 || index >= barSet->count()) {
                    QToolTip::hideText();
                    return;
                }
                const QString period = (index < xLabelsCopy.size()) ? xLabelsCopy.at(index) : QString::number(index + 1);
                const CAmount satoshis = static_cast<CAmount>(std::llround(barSet->at(index) * COIN));
                QToolTip::showText(QCursor::pos(),
                    QString("%1 — %2\n%3").arg(period, trackName,
                        BitcoinUnits::formatWithUnit(unitCopy, satoshis, false, BitcoinUnits::separatorStandard)));
            });
        };
        bindBarHover(set0, tr("Staking (PoS)"));
        bindBarHover(set1, tr("MN & Mining"));

        set0->append(chartData->valuesPiv);
        set1->append(chartData->valuesMNRewards);

        stackedSeries = new QStackedBarSeries(chart);
        if (chartSeriesFilter == 1) {
            stackedSeries->append(set0);
        } else if (chartSeriesFilter == 2 && hasMNRewards) {
            stackedSeries->append(set1);
        } else {
            stackedSeries->append(set0);
            if (hasMNRewards) {
                stackedSeries->append(set1);
            }
        }
        stackedSeries->setBarWidth(chartShow == YEAR ? 0.62 : 0.44);

        chart->addSeries(stackedSeries);
        stackedSeries->attachAxis(axisX);
        stackedSeries->attachAxis(axisY);

        double stackedMax = 0.0;
        const int count = std::min(chartData->valuesPiv.size(), chartData->valuesMNRewards.size());
        for (int i = 0; i < count; ++i) {
            double v = 0.0;
            if (chartSeriesFilter == 1) v = chartData->valuesPiv.at(i);
            else if (chartSeriesFilter == 2 && hasMNRewards) v = chartData->valuesMNRewards.at(i);
            else v = chartData->valuesPiv.at(i) + (hasMNRewards ? chartData->valuesMNRewards.at(i) : 0.0);
            if (v > stackedMax) stackedMax = v;
        }
        const double yMax = (stackedMax > 0.0) ? (stackedMax * 1.1) : 1.0;
        axisY->setLabelFormat(yMax >= 20.0 ? "%.0f" : "%.2f");
        axisY->setRange(0.0, yMax);
    }

    // Sync top toolbar period buttons & date selectors
    if (btnPeriodMonthly) btnPeriodMonthly->setChecked(chartShow == YEAR);
    if (btnPeriodDaily) btnPeriodDaily->setChecked(chartShow == MONTH);
    if (btnPeriodAll) btnPeriodAll->setChecked(chartShow == ALL);

    ui->comboBoxYears->setVisible(chartShow != ALL);
    ui->comboBoxMonths->setVisible(chartShow == MONTH);

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
    if (fShowCharts) {
        tryChartRefresh();
    }
    updateMinerHubLayoutState();
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

void DashboardWidget::initRewardChartUx()
{
    if (!ui) return;

    // Close / Hide button in Rewards Chart header (top-right)
    if (!btnCloseChart && ui->horizontalLayout_3) {
        btnCloseChart = new QPushButton(QString::fromUtf8("\xC3\x97"), ui->right);
        btnCloseChart->setObjectName("btnCloseChart");
        btnCloseChart->setFixedSize(28, 28);
        btnCloseChart->setCursor(Qt::PointingHandCursor);
        btnCloseChart->setFocusPolicy(Qt::NoFocus);
        btnCloseChart->setToolTip(tr("Hide Rewards Chart panel"));
        ui->horizontalLayout_3->addWidget(btnCloseChart, 0, Qt::AlignTop | Qt::AlignRight);
        connect(btnCloseChart, &QPushButton::clicked, this, [this]() {
            setChartVisible(false, true);
        });
    }

    // Re-open button on Transactions header (shown only while Rewards Chart is hidden)
    if (!btnShowChart && ui->horizontalLayout) {
        btnShowChart = new QPushButton(tr("Show Chart"), ui->left);
        btnShowChart->setObjectName("btnShowChart");
        btnShowChart->setFixedHeight(28);
        btnShowChart->setCursor(Qt::PointingHandCursor);
        btnShowChart->setFocusPolicy(Qt::NoFocus);
        btnShowChart->setToolTip(tr("Show Rewards Chart panel"));
        btnShowChart->setVisible(false);
        ui->horizontalLayout->addSpacing(6);
        ui->horizontalLayout->addWidget(btnShowChart);
        connect(btnShowChart, &QPushButton::clicked, this, [this]() {
            setChartVisible(true, true);
        });
    }

    // Hide legacy DSW bottom sort/filter box and redundant square legend row
    if (ui->containerSort) ui->containerSort->setVisible(false);
    if (ui->labelChart) ui->labelChart->setVisible(false);
    if (ui->labelSquarePiv) ui->labelSquarePiv->setVisible(false);
    if (ui->labelPiv) ui->labelPiv->setVisible(false);
    if (ui->labelSquareMNRewards) ui->labelSquareMNRewards->setVisible(false);
    if (ui->labelMNRewards) ui->labelMNRewards->setVisible(false);

    // Modern top toolbar above chart (Period pills + Year/Month + Chart Mode pills)
    if (!chartTopToolbarFrame && ui->verticalLayout_4) {
        chartTopToolbarFrame = new QFrame(ui->verticalWidgetChart);
        chartTopToolbarFrame->setObjectName("chartTopToolbarFrame");
        QHBoxLayout* tbLayout = new QHBoxLayout(chartTopToolbarFrame);
        tbLayout->setContentsMargins(0, 0, 0, 8);
        tbLayout->setSpacing(6);

        auto makePillBtn = [this](const QString& text, const QString& tip) -> QPushButton* {
            QPushButton* b = new QPushButton(text, chartTopToolbarFrame);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setFixedHeight(26);
            b->setToolTip(tip);
            return b;
        };

        btnPeriodDaily = makePillBtn(tr("Daily"), tr("Show daily rewards for selected month"));
        btnPeriodMonthly = makePillBtn(tr("Monthly"), tr("Show monthly rewards for selected year"));
        btnPeriodAll = makePillBtn(tr("All"), tr("Show yearly totals across all history"));
        btnPeriodMonthly->setChecked(true);

        tbLayout->addWidget(btnPeriodDaily);
        tbLayout->addWidget(btnPeriodMonthly);
        tbLayout->addWidget(btnPeriodAll);

        // Re-parent compact Year & Month selectors into the top toolbar
        ui->comboBoxYears->setParent(chartTopToolbarFrame);
        ui->comboBoxYears->setFixedHeight(26);
        ui->comboBoxYears->setMinimumWidth(72);
        ui->comboBoxMonths->setParent(chartTopToolbarFrame);
        ui->comboBoxMonths->setFixedHeight(26);
        ui->comboBoxMonths->setMinimumWidth(86);
        ui->comboBoxMonths->setVisible(false);
        tbLayout->addWidget(ui->comboBoxYears);
        tbLayout->addWidget(ui->comboBoxMonths);

        tbLayout->addStretch(1);

        btnModeBars = makePillBtn(tr("Bars"), tr("Stacked bar breakdown by period"));
        btnModeTrend = makePillBtn(tr("Trend"), tr("Cumulative earnings curve"));
        btnModeSplit = makePillBtn(tr("Split"), tr("Proportional donut breakdown"));
        btnModeBars->setChecked(true);

        tbLayout->addWidget(btnModeBars);
        tbLayout->addWidget(btnModeTrend);
        tbLayout->addWidget(btnModeSplit);

        ui->verticalLayout_4->insertWidget(0, chartTopToolbarFrame);
        ui->verticalLayout_4->setStretch(0, 0);
        ui->verticalLayout_4->setStretch(1, 0);
        ui->verticalLayout_4->setStretch(2, 1);

        connect(btnPeriodDaily, &QPushButton::clicked, this, [this]() {
            if (ui->pushButtonMonth) ui->pushButtonMonth->click();
        });
        connect(btnPeriodMonthly, &QPushButton::clicked, this, [this]() {
            if (ui->pushButtonYear) ui->pushButtonYear->click();
        });
        connect(btnPeriodAll, &QPushButton::clicked, this, [this]() {
            if (ui->pushButtonAll) ui->pushButtonAll->click();
        });

        auto setVisualMode = [this](int mode) {
            chartVisualMode = mode;
            if (btnModeBars) btnModeBars->setChecked(mode == 0);
            if (btnModeTrend) btnModeTrend->setChecked(mode == 1);
            if (btnModeSplit) btnModeSplit->setChecked(mode == 2);
            updateRewardChartTheme();
#ifdef USE_QTCHARTS
            if (chartData && isChartInitialized) {
                onChartRefreshed();
            }
#endif
        };
        connect(btnModeBars, &QPushButton::clicked, this, [setVisualMode]() { setVisualMode(0); });
        connect(btnModeTrend, &QPushButton::clicked, this, [setVisualMode]() { setVisualMode(1); });
        connect(btnModeSplit, &QPushButton::clicked, this, [setVisualMode]() { setVisualMode(2); });
    }

    // Upgrade unlabeled reward amounts into 3 clearly labeled summary cards with % share
    if (!frameChartCardStaking && ui->horizontalLayout_5) {
        ui->horizontalLayout_5->removeWidget(ui->labelAmountPiv);
        ui->horizontalLayout_5->removeWidget(ui->labelAmountMNRewards);
        ui->horizontalLayout_5->setSpacing(8);

        lblChartTotalAmount = new QLabel(QString("0 %1").arg(CURRENCY_UNIT.c_str()), ui->right);
        lblChartTotalAmount->setObjectName("lblChartTotalAmount");
        lblChartTotalAmount->setAlignment(Qt::AlignCenter);

        auto buildSummaryCard = [this](QFrame*& card, QLabel*& titleLbl, QLabel*& subLbl,
                                       const QString& objName, const QString& titleText,
                                       const QString& subText, const QString& tooltipText, QLabel* valueLbl) {
            card = new QFrame(ui->right);
            card->setObjectName(objName);
            card->setToolTip(tooltipText);
            card->setCursor(Qt::PointingHandCursor);
            card->installEventFilter(this);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

            QVBoxLayout* col = new QVBoxLayout(card);
            col->setContentsMargins(6, 7, 6, 7);
            col->setSpacing(2);

            titleLbl = new QLabel(titleText, card);
            titleLbl->setAlignment(Qt::AlignCenter);
            col->addWidget(titleLbl);

            valueLbl->setParent(card);
            valueLbl->setAlignment(Qt::AlignCenter);
            col->addWidget(valueLbl);

            subLbl = new QLabel(subText, card);
            subLbl->setAlignment(Qt::AlignCenter);
            col->addWidget(subLbl);

            ui->horizontalLayout_5->addWidget(card, 1);
        };

        buildSummaryCard(frameChartCardStaking, lblChartCardStakingTitle, lblChartCardStakingShare,
                         "frameChartCardStaking",
                         QString("<span style='color:#F5897C;'>&#9679;</span> %1").arg(tr("Staking (PoS)")),
                         QStringLiteral("0.0% of total"),
                         tr("Rewards earned from Proof-of-Stake (StakeMint)"),
                         ui->labelAmountPiv);
        buildSummaryCard(frameChartCardMn, lblChartCardMnTitle, lblChartCardMnShare,
                         "frameChartCardMn",
                         QString("<span style='color:#7695CD;'>&#9679;</span> %1").arg(tr("MN & Mining")),
                         QStringLiteral("0.0% of total"),
                         tr("Rewards earned from Masternodes, LLMQ, and PoW Mining"),
                         ui->labelAmountMNRewards);
        buildSummaryCard(frameChartCardTotal, lblChartCardTotalTitle, lblChartCardTotalMeta,
                         "frameChartCardTotal", tr("Total Earned"), tr("All tracks (100%)"),
                         tr("Combined Staking, Masternode, and Mining rewards for the selected period"),
                         lblChartTotalAmount);
    }

    updateRewardChartTheme();
}

void DashboardWidget::updateRewardChartTheme()
{
    const bool light = isLightTheme();
    const QString cardBg = light ? QStringLiteral("#F8FAFC") : QStringLiteral("#182235");
    const QString cardBorder = light ? QStringLiteral("#E2E8F0") : QStringLiteral("#2A3B58");
    const QString titleColor = light ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString subColor = light ? QStringLiteral("#475569") : QStringLiteral("#B8C7DC");
    const QString totalColor = light ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");

    const QString chipBg = light ? QStringLiteral("#EDF2FA") : QStringLiteral("#1E2C44");
    const QString chipText = light ? QStringLiteral("#3B5998") : QStringLiteral("#A9C3F0");
    const QString chipHoverBorder = light ? QStringLiteral("#4A679E") : QStringLiteral("#F5897C");

    if (btnCloseChart) {
        btnCloseChart->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; font-size: 16px; font-weight: bold; padding: 0px; }"
            "QPushButton:hover { background-color: #E57A6D; color: #FFFFFF; border-color: #C96054; }"
            "QPushButton:pressed { background-color: #C96054; color: #FFFFFF; }"
        ).arg(cardBg, titleColor, cardBorder));
    }

    if (btnShowChart) {
        btnShowChart->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1.5px solid #7695CD; border-radius: 6px; padding: 3px 10px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover { background-color: #5576B2; color: #FFFFFF; border-color: %3; }"
            "QPushButton:pressed { background-color: #466399; color: #FFFFFF; }"
        ).arg(chipBg, chipText, chipHoverBorder));
    }

    const QString pillCss = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 0px 9px; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { border-color: #7695CD; color: %4; }"
        "QPushButton:checked { background-color: #7695CD; color: #FFFFFF; border-color: #5B7BB8; }"
    ).arg(cardBg, titleColor, cardBorder, totalColor);
    for (QPushButton* pill : {btnPeriodDaily, btnPeriodMonthly, btnPeriodAll, btnModeBars, btnModeTrend, btnModeSplit}) {
        if (pill) pill->setStyleSheet(pillCss);
    }

    auto styleCard = [&](QFrame* card, QLabel* titleLbl, QLabel* subLbl, const QString& objName,
                         const QString& accentColor, const QString& selectedBg, bool isSelected) {
        const QString bg = isSelected ? selectedBg : cardBg;
        const QString border = isSelected ? accentColor : cardBorder;
        const QString borderW = isSelected ? QStringLiteral("1.5") : QStringLiteral("1");
        if (card) {
            card->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: %3px solid %4; border-radius: 8px; }"
                "QFrame#%1:hover { border-color: %5; }"
            ).arg(objName, bg, borderW, border, accentColor));
        }
        if (titleLbl) {
            titleLbl->setStyleSheet(QString(
                "QLabel { color: %1; background: transparent; border: none; font-size: 11px; font-weight: bold; }"
            ).arg(titleColor));
        }
        if (subLbl) {
            subLbl->setStyleSheet(QString(
                "QLabel { color: %1; background: transparent; border: none; font-size: 10px; }"
            ).arg(subColor));
        }
    };

    const QString posSelBg = light ? QStringLiteral("#FFF5F4") : QStringLiteral("#261D24");
    const QString mnSelBg = light ? QStringLiteral("#F0F5FF") : QStringLiteral("#1B2840");
    styleCard(frameChartCardStaking, lblChartCardStakingTitle, lblChartCardStakingShare, "frameChartCardStaking",
              "#F5897C", posSelBg, chartSeriesFilter == 1);
    styleCard(frameChartCardMn, lblChartCardMnTitle, lblChartCardMnShare, "frameChartCardMn",
              "#7695CD", mnSelBg, chartSeriesFilter == 2);
    styleCard(frameChartCardTotal, lblChartCardTotalTitle, lblChartCardTotalMeta, "frameChartCardTotal",
              "#7695CD", cardBg, false);

    ui->labelAmountPiv->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; }");
    ui->labelAmountMNRewards->setStyleSheet("QLabel { font-size: 13px; font-weight: bold; }");
    if (lblChartTotalAmount) {
        lblChartTotalAmount->setStyleSheet(QString(
            "QLabel { color: %1; background: transparent; border: none; font-size: 13px; font-weight: bold; }"
        ).arg(totalColor));
    }
}

QString DashboardWidget::formatCompactKrista(CAmount amount, int unit) const
{
    const QString unitName = BitcoinUnits::name(unit);
    if (amount == 0) {
        return QString("0.00 %1").arg(unitName);
    }
    double coins = static_cast<double>(amount) / static_cast<double>(COIN);
    if (unit == BitcoinUnits::mPIV) coins *= 1000.0;
    else if (unit == BitcoinUnits::uPIV) coins *= 1000000.0;

    if (std::abs(coins) < 0.01) {
        return QString("%1 %2").arg(QLocale().toString(coins, 'f', 4), unitName);
    }
    return QString("%1 %2").arg(QLocale().toString(coins, 'f', 2), unitName);
}

bool DashboardWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonRelease) {
        int nextFilter = chartSeriesFilter;
        bool matched = false;
        if (obj == frameChartCardStaking) {
            nextFilter = (chartSeriesFilter == 1) ? 0 : 1;
            matched = true;
        } else if (obj == frameChartCardMn) {
            nextFilter = (chartSeriesFilter == 2) ? 0 : 2;
            matched = true;
        } else if (obj == frameChartCardTotal) {
            nextFilter = 0;
            matched = true;
        }
        if (matched) {
            chartSeriesFilter = nextFilter;
            updateRewardChartTheme();
#ifdef USE_QTCHARTS
            if (chartData && isChartInitialized) {
                onChartRefreshed();
            }
#endif
            return true;
        }
    }
    return PWidget::eventFilter(obj, event);
}

void DashboardWidget::setChartVisible(bool visible, bool persist)
{
#ifdef USE_QTCHARTS
    fShowCharts = visible;
#else
    fShowCharts = false;
#endif
    if (persist) {
        getSettings()->setValue("dashboardChartVisible", fShowCharts);
    }
#ifdef USE_QTCHARTS
    if (btnShowChart) {
        btnShowChart->setVisible(!fShowCharts);
    }
    if (fShowCharts && stakesFilter) {
        hasStakes = stakesFilter->rowCount() > 0;
        if (hasStakes && (!chart || fChartEmptyDesired)) {
            tryChartRefresh();
        }
    }
#endif
    updateMinerHubLayoutState();
}

DashboardWidget::~DashboardWidget()
{
    shutdownMinerWorkers();
#ifdef USE_QTCHARTS
    delete chart;
#endif
    delete ui;
}
