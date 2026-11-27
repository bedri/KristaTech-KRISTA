// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/masternodeswidget.h"
#include "qt/kristatech/forms/ui_masternodeswidget.h"

#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/mnrow.h"
#include "qt/kristatech/mninfodialog.h"
#include "qt/kristatech/masternodewizarddialog.h"
#include "qt/kristatech/defaultdialog.h"

#include "activemasternode.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "evo/deterministicmns.h"
#include "fs.h"
#include "guiutil.h"
#include "init.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "netbase.h"
#include "sync.h"
#include "txmempool.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"
#include "util.h"
#include "qt/kristatech/optionbutton.h"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QListView>
#include <QLocale>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QVBoxLayout>
#include <iostream>
#include <fstream>

#define DECORATION_SIZE 52
#define NUM_ITEMS 3
#define REQUEST_START_ALL 1
#define REQUEST_START_MISSING 2

class MNFilterProxyModel : public QSortFilterProxyModel
{
public:
    explicit MNFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setSearchText(const QString& text)
    {
        searchText = text.trimmed();
        invalidateFilter();
    }

    void setStatusFilter(int mode)
    {
        statusMode = mode; // 0 = All, 1 = Enabled, 2 = Inactive / Missing
        invalidateFilter();
    }

    void setScopeFilter(int mode)
    {
        scopeMode = mode; // 0 = My Wallet, 1 = All Network
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        QAbstractItemModel* src = sourceModel();
        if (!src) return true;

        const QString alias  = src->index(sourceRow, MNModel::ALIAS, sourceParent).data(Qt::DisplayRole).toString();
        const QString addr   = src->index(sourceRow, MNModel::ADDRESS, sourceParent).data(Qt::DisplayRole).toString();
        const QString txId   = src->index(sourceRow, MNModel::COLLATERAL_ID, sourceParent).data(Qt::DisplayRole).toString();
        const QString status = src->index(sourceRow, MNModel::STATUS, sourceParent).data(Qt::DisplayRole).toString();

        if (scopeMode == 0) {
            MNModel* mnSrc = qobject_cast<MNModel*>(src);
            if (mnSrc && !mnSrc->isMNWalletOwned(alias)) {
                return false;
            }
        }

        if (statusMode == 1 && status != "ENABLED" && status != "PRE_ENABLED") {
            return false;
        }
        if (statusMode == 2 && (status == "ENABLED" || status == "PRE_ENABLED")) {
            return false;
        }

        if (!searchText.isEmpty()) {
            const bool match = alias.contains(searchText, Qt::CaseInsensitive) ||
                               addr.contains(searchText, Qt::CaseInsensitive) ||
                               txId.contains(searchText, Qt::CaseInsensitive) ||
                               status.contains(searchText, Qt::CaseInsensitive);
            if (!match) return false;
        }
        return true;
    }

private:
    QString searchText;
    int statusMode{0};
    int scopeMode{0};
};

class MNHolder : public FurListRow<QWidget*>
{
public:
    MNHolder();

    explicit MNHolder(bool _isLightTheme) : FurListRow(), isLightTheme(_isLightTheme) {}

    MNRow* createHolder(int pos) override
    {
        if (!cachedRow) cachedRow = new MNRow();
        return cachedRow;
    }

    void init(QWidget* holder,const QModelIndex &index, bool isHovered, bool isSelected) const override
    {
        MNRow* row = static_cast<MNRow*>(holder);
        QString label = index.data(Qt::DisplayRole).toString();
        QString address = index.sibling(index.row(), MNModel::ADDRESS).data(Qt::DisplayRole).toString();
        QString status = index.sibling(index.row(), MNModel::STATUS).data(Qt::DisplayRole).toString();
        bool wasCollateralAccepted = index.sibling(index.row(), MNModel::WAS_COLLATERAL_ACCEPTED).data(Qt::DisplayRole).toBool();
        QString txId = index.sibling(index.row(), MNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
        bool isWalletOwned = index.data(Qt::UserRole + 1).toBool();
        bool isOnChainDip3 = index.data(Qt::UserRole + 2).toBool();
        bool isInConf = index.data(Qt::UserRole + 3).toBool();
        int posePenalty = index.data(Qt::UserRole + 5).toInt();
        int lastPaidHeight = index.data(Qt::UserRole + 6).toInt();
        int poseBanHeight = index.data(Qt::UserRole + 10).toInt();
        int collateralConfs = index.data(Qt::UserRole + 13).toInt();
        int requiredConfs = index.data(Qt::UserRole + 14).toInt();
        row->updateView(address, label, status, wasCollateralAccepted, txId, isWalletOwned, isInConf, isOnChainDip3, lastPaidHeight, posePenalty, poseBanHeight, collateralConfs, requiredConfs);
    }

    QColor rectColor(bool isHovered, bool isSelected) override
    {
        if (isLightTheme) {
            if (isSelected) return QColor("#EEF3FA");
            if (isHovered) return QColor("#F8FAFC");
            return QColor("#FFFFFF");
        }
        if (isSelected) return QColor("#1E2E4D");
        if (isHovered) return QColor("#1A2540");
        return QColor("#131B2E");
    }

    ~MNHolder() override{}

    bool isLightTheme;
    MNRow* cachedRow = nullptr;
};

MasterNodesWidget::MasterNodesWidget(KRISTATECHGUI *parent) :
    PWidget(parent),
    ui(new Ui::MasterNodesWidget),
    isLoading(false)
{
    ui->setupUi(this);

    delegate = new FurAbstractListItemDelegate(
            DECORATION_SIZE,
            new MNHolder(isLightTheme()),
            this
    );
    mnModel = new MNModel(this);
    proxyModel = new MNFilterProxyModel(this);
    proxyModel->setSourceModel(mnModel);
    connect(mnModel, &QAbstractItemModel::modelReset, this, &MasterNodesWidget::updateListState);

    /* Hide legacy container whose right-hand horizontalSpacer squeezed header/filter widgets left */
    ui->left->setVisible(false);

    /* Modern full-width KristaTech canvas root */
    mnPageRoot = new QWidget(this);
    mnPageRoot->setObjectName("mnPageRoot");
    mnPageRoot->setAttribute(Qt::WA_StyledBackground, true);
    ui->horizontalLayout_2->addWidget(mnPageRoot);

    QVBoxLayout* pageLayout = new QVBoxLayout(mnPageRoot);
    pageLayout->setContentsMargins(24, 16, 24, 18);
    pageLayout->setSpacing(14);

    /* 1. Page Header Row: Title/Subtitle (left) + Quick Conf/Refresh Actions (right) */
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(12);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(3);

    lblPageTitle = new QLabel(tr("Masternodes"), mnPageRoot);
    lblPageTitle->setProperty("mnTitle", true);
    lblPageSubtitle = new QLabel(
        tr("Run 4,200 KRISTA Masternodes to secure the network and earn automatic block rewards."),
        mnPageRoot);
    lblPageSubtitle->setToolTip(tr("<qt><b>Deterministic Masternodes (KTDIP3 v2):</b> 4,200 KRISTA collateral stays in your wallet.<br/><b>Model D Rewards:</b> 5% Passive Pool • Up to 8/15 Validator Seats (4.00% each per solved puzzle) • 10% Signer-Only LLMQ Quorum.</qt>"));
    lblPageSubtitle->setProperty("mnSubtitle", true);
    lblPageSubtitle->setWordWrap(true);
    titleCol->addWidget(lblPageTitle);
    titleCol->addWidget(lblPageSubtitle);

    QHBoxLayout* headerActions = new QHBoxLayout();
    headerActions->setContentsMargins(0, 0, 0, 0);
    headerActions->setSpacing(8);

    btnOpenActiveMNConf = new QPushButton(tr("activemasternode.conf (Hot)"), mnPageRoot);
    btnOpenActiveMNConf->setCursor(Qt::PointingHandCursor);
    btnOpenActiveMNConf->setFocusPolicy(Qt::TabFocus);
    btnOpenActiveMNConf->setFixedHeight(34);
    btnOpenActiveMNConf->setProperty("mnSecondary", true);
    btnOpenActiveMNConf->setToolTip(tr("Open or initialize activemasternode.conf for single-IP multi-Masternode operation"));
    connect(btnOpenActiveMNConf, &QPushButton::clicked, [this]() {
        if (!GUIUtil::openActiveMNConfigfile()) {
            inform(tr("Unable to open activemasternode.conf."));
        }
    });

    btnRefreshMNs = new QPushButton(QString::fromUtf8("\xE2\x86\xBB ") + tr("Refresh"), mnPageRoot);
    btnRefreshMNs->setCursor(Qt::PointingHandCursor);
    btnRefreshMNs->setFocusPolicy(Qt::TabFocus);
    btnRefreshMNs->setFixedHeight(34);
    btnRefreshMNs->setProperty("mnSecondary", true);
    btnRefreshMNs->setToolTip(tr("Refresh Masternode list and on-chain status now"));
    connect(btnRefreshMNs, &QPushButton::clicked, [this]() {
        if (mnModel) {
            mnModel->updateMNList();
            updateListState();
        }
    });

    headerActions->addWidget(btnOpenActiveMNConf);
    headerActions->addWidget(btnRefreshMNs);

    headerRow->addLayout(titleCol, 1);
    headerRow->addLayout(headerActions);
    pageLayout->addLayout(headerRow);

    /* 2. Full-Width 4-Card KPI Summary Strip */
    QHBoxLayout* kpiRow = new QHBoxLayout();
    kpiRow->setContentsMargins(0, 0, 0, 0);
    kpiRow->setSpacing(12);

    auto makeKpiCard = [this](const QString& title, QLabel*& valOut, QLabel*& subOut) -> QFrame* {
        QFrame* card = new QFrame(mnPageRoot);
        card->setProperty("mnKpiCard", true);
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setMinimumWidth(0);

        QVBoxLayout* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(16, 12, 16, 12);
        cardLayout->setSpacing(3);

        QLabel* lblKey = new QLabel(title, card);
        lblKey->setProperty("mnKpiKey", true);
        lblKey->setMinimumWidth(0);

        valOut = new QLabel(QStringLiteral("0"), card);
        valOut->setProperty("mnKpiVal", true);
        valOut->setMinimumWidth(0);

        subOut = new QLabel(QStringLiteral("--"), card);
        subOut->setProperty("mnKpiSub", true);
        subOut->setMinimumWidth(0);

        cardLayout->addWidget(lblKey);
        cardLayout->addWidget(valOut);
        cardLayout->addWidget(subOut);
        return card;
    };

    QFrame* kpiTotal = makeKpiCard(tr("Total Masternodes"), lblKpiTotalVal, lblKpiTotalSub);
    QFrame* kpiEnabled = makeKpiCard(tr("Active / Enabled"), lblKpiEnabledVal, lblKpiEnabledSub);
    lblKpiEnabledVal->setProperty("mnKpiAccent", "blue");
    QFrame* kpiInactive = makeKpiCard(tr("Inactive / Attention"), lblKpiInactiveVal, lblKpiInactiveSub);
    lblKpiInactiveVal->setProperty("mnKpiAlert", "false");
    QFrame* kpiLocked = makeKpiCard(tr("Locked Collateral"), lblKpiLockedVal, lblKpiLockedSub);

    kpiRow->addWidget(kpiTotal, 1);
    kpiRow->addWidget(kpiEnabled, 1);
    kpiRow->addWidget(kpiInactive, 1);
    kpiRow->addWidget(kpiLocked, 1);
    pageLayout->addLayout(kpiRow);

    /* 3. Main Rounded Table Card */
    QFrame* tableCard = new QFrame(mnPageRoot);
    tableCard->setObjectName("mnTableCard");
    tableCard->setProperty("mnCard", true);
    tableCard->setAttribute(Qt::WA_StyledBackground, true);

    QVBoxLayout* cardLayout = new QVBoxLayout(tableCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(0);

    /* Card Top Toolbar: Title + Count Pill + Scope Switcher (left) | Search + Status Filter (right) */
    QWidget* toolbarWrap = new QWidget(tableCard);
    toolbarWrap->setObjectName("mnToolbarWrap");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbarWrap);
    toolbarLayout->setContentsMargins(18, 13, 18, 13);
    toolbarLayout->setSpacing(10);

    lblListSectionTitle = new QLabel(tr("Masternode List"), toolbarWrap);
    lblListSectionTitle->setProperty("mnSectionTitle", true);

    lblShownCount = new QLabel(tr("0 shown"), toolbarWrap);
    lblShownCount->setProperty("mnCountPill", true);

    btnScopeMine = new QPushButton(tr("My Masternodes"), toolbarWrap);
    btnScopeMine->setCheckable(true);
    btnScopeMine->setChecked(true);
    btnScopeMine->setCursor(Qt::PointingHandCursor);
    btnScopeMine->setFocusPolicy(Qt::TabFocus);
    btnScopeMine->setFixedHeight(32);
    btnScopeMine->setProperty("mnScopePill", true);

    btnScopeAll = new QPushButton(tr("All Network"), toolbarWrap);
    btnScopeAll->setCheckable(true);
    btnScopeAll->setChecked(false);
    btnScopeAll->setCursor(Qt::PointingHandCursor);
    btnScopeAll->setFocusPolicy(Qt::TabFocus);
    btnScopeAll->setFixedHeight(32);
    btnScopeAll->setProperty("mnScopePill", true);

    lineEditSearchMN = new QLineEdit(toolbarWrap);
    lineEditSearchMN->setPlaceholderText(tr("Search alias, IP, or TxID..."));
    lineEditSearchMN->setClearButtonEnabled(true);
    lineEditSearchMN->setMinimumWidth(220);
    lineEditSearchMN->setMaximumWidth(320);
    lineEditSearchMN->setFixedHeight(34);
    lineEditSearchMN->setProperty("mnInput", true);
    styleLineEditClearButton(lineEditSearchMN);

    comboStatusFilter = new QComboBox(toolbarWrap);
    comboStatusFilter->addItem(tr("All Status"));
    comboStatusFilter->addItem(tr("Enabled Only"));
    comboStatusFilter->addItem(tr("Inactive / Missing"));
    comboStatusFilter->setView(new QListView(comboStatusFilter));
    comboStatusFilter->setCursor(Qt::PointingHandCursor);
    comboStatusFilter->setFixedHeight(34);
    comboStatusFilter->setMinimumWidth(160);
    comboStatusFilter->setProperty("mnCombo", true);

    connect(btnScopeMine, &QPushButton::clicked, this, [this]() { setScopeMode(0); });
    connect(btnScopeAll, &QPushButton::clicked, this, [this]() { setScopeMode(1); });
    connect(lineEditSearchMN, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (proxyModel) proxyModel->setSearchText(text);
        updateListState();
    });
    connect(comboStatusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (proxyModel) proxyModel->setStatusFilter(idx);
        updateListState();
    });

    toolbarLayout->addWidget(lblListSectionTitle);
    toolbarLayout->addWidget(lblShownCount);
    toolbarLayout->addSpacing(4);
    toolbarLayout->addWidget(btnScopeMine);
    toolbarLayout->addWidget(btnScopeAll);
    toolbarLayout->addStretch(1);
    toolbarLayout->addWidget(lineEditSearchMN);
    toolbarLayout->addWidget(comboStatusFilter);
    cardLayout->addWidget(toolbarWrap);

    auto makeDivider = [tableCard]() -> QFrame* {
        QFrame* div = new QFrame(tableCard);
        div->setProperty("mnDivider", true);
        div->setFrameShape(QFrame::NoFrame);
        div->setFixedHeight(1);
        return div;
    };

    cardLayout->addWidget(makeDivider());

    /* Reparent and align Table Header Frame with MNRow columns */
    ui->headerFrame->setParent(tableCard);
    ui->headerFrame->setStyleSheet(QString());
    ui->headerFrame->setFixedHeight(36);
    ui->headerLayout->setContentsMargins(18, 0, 18, 0);
    ui->headerLayout->setSpacing(12);
    ui->headerLayout->setStretch(0, 2);
    ui->headerLayout->setStretch(1, 2);
    ui->headerLayout->setStretch(2, 2);
    ui->headerLayout->setStretch(3, 0);
    ui->headerLayout->setStretch(4, 0);
    ui->headerLayout->setStretch(5, 0);

    ui->labelHeaderAlias->setMinimumWidth(105);
    ui->labelHeaderAlias->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderAddress->setMinimumWidth(145);
    ui->labelHeaderAddress->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderTxId->setMinimumWidth(155);
    ui->labelHeaderTxId->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderPaidPose->setFixedWidth(148);
    ui->labelHeaderPaidPose->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderStatus->setFixedWidth(182);
    ui->labelHeaderStatus->setAlignment(Qt::AlignCenter);
    ui->labelHeaderActions->setFixedWidth(205);
    ui->labelHeaderActions->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ui->labelHeaderAlias->setProperty("mnColHeader", true);
    ui->labelHeaderAddress->setProperty("mnColHeader", true);
    ui->labelHeaderTxId->setProperty("mnColHeader", true);
    ui->labelHeaderPaidPose->setProperty("mnColHeader", true);
    ui->labelHeaderStatus->setProperty("mnColHeader", true);
    ui->labelHeaderActions->setProperty("mnColHeader", true);
    cardLayout->addWidget(ui->headerFrame);

    /* Reparent Masternode List View + Filtered Empty + No-Data Empty State */
    ui->listMn->setParent(tableCard);
    ui->listMn->setProperty("cssClass", QVariant());
    if (ui->listMn->viewport()) {
        ui->listMn->viewport()->setObjectName("mnListViewport");
    }
    ui->listMn->setFrameShape(QFrame::NoFrame);
    ui->listMn->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->listMn->setItemDelegate(delegate);
    ui->listMn->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listMn->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listMn->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listMn->setSelectionBehavior(QAbstractItemView::SelectRows);
    cardLayout->addWidget(ui->listMn, 1);

    lblFilteredEmpty = new QLabel(tr("No masternodes match the current filter."), tableCard);
    lblFilteredEmpty->setAlignment(Qt::AlignCenter);
    lblFilteredEmpty->setProperty("mnEmptyHint", true);
    lblFilteredEmpty->setVisible(false);
    cardLayout->addWidget(lblFilteredEmpty, 1);

    ui->emptyContainer->setParent(tableCard);
    ui->emptyContainer->setVisible(false);
    setCssProperty(ui->pushImgEmpty, "img-empty-master");
    setCssProperty(ui->labelEmpty, "text-empty");
    cardLayout->addWidget(ui->emptyContainer, 1);

    cardLayout->addWidget(makeDivider());

    /* Card Footer Action Bar: KTDIP3 Hot Node Note (left) + Primary/Secondary Actions (right) */
    QWidget* footerWrap = new QWidget(tableCard);
    footerWrap->setObjectName("mnFooterWrap");
    QHBoxLayout* footerLayout = new QHBoxLayout(footerWrap);
    footerLayout->setContentsMargins(18, 12, 18, 12);
    footerLayout->setSpacing(12);

    lblRule8Note = new QLabel(
        tr("Remote Server Tip: Multiple Masternodes can share one VPS IP via activemasternode.conf — your 4,200 KRISTA collateral stays safely in this wallet."),
        footerWrap);
    lblRule8Note->setToolTip(tr("Hot Node format in activemasternode.conf: '<alias> <privkey> <bls_privkey_hex>' (never put masternodeprivkey= in kristatech.conf)."));
    lblRule8Note->setProperty("mnRuleNote", true);
    lblRule8Note->setWordWrap(true);

    ui->pushButtonStartMissing->setParent(footerWrap);
    ui->pushButtonStartAll->setParent(footerWrap);
    ui->pushButtonSave->setParent(footerWrap);

    ui->pushButtonStartMissing->setProperty("cssClass", QVariant());
    ui->pushButtonStartAll->setProperty("cssClass", QVariant());
    ui->pushButtonSave->setProperty("cssClass", QVariant());

    ui->pushButtonStartMissing->setText(tr("Start Inactive"));
    ui->pushButtonStartAll->setText(tr("Start All"));
    ui->pushButtonSave->setText(QStringLiteral("+ ") + tr("Create Masternode Controller"));

    ui->pushButtonStartMissing->setProperty("mnSecondary", true);
    ui->pushButtonStartAll->setProperty("mnSecondary", true);
    ui->pushButtonSave->setProperty("mnPrimary", true);

    ui->pushButtonStartMissing->setMinimumSize(0, 36);
    ui->pushButtonStartMissing->setMaximumSize(16777215, 36);
    ui->pushButtonStartMissing->setCursor(Qt::PointingHandCursor);
    ui->pushButtonStartMissing->setFocusPolicy(Qt::TabFocus);

    ui->pushButtonStartAll->setMinimumSize(0, 36);
    ui->pushButtonStartAll->setMaximumSize(16777215, 36);
    ui->pushButtonStartAll->setCursor(Qt::PointingHandCursor);
    ui->pushButtonStartAll->setFocusPolicy(Qt::TabFocus);

    ui->pushButtonSave->setMinimumSize(0, 36);
    ui->pushButtonSave->setMaximumSize(16777215, 36);
    ui->pushButtonSave->setCursor(Qt::PointingHandCursor);
    ui->pushButtonSave->setFocusPolicy(Qt::TabFocus);

    updateStyle(ui->pushButtonStartMissing);
    updateStyle(ui->pushButtonStartAll);
    updateStyle(ui->pushButtonSave);

    footerLayout->addWidget(lblRule8Note, 1);
    footerLayout->addWidget(ui->pushButtonStartMissing);
    footerLayout->addWidget(ui->pushButtonStartAll);
    footerLayout->addWidget(ui->pushButtonSave);
    cardLayout->addWidget(footerWrap);

    pageLayout->addWidget(tableCard, 1);

    connect(ui->pushButtonSave, &QPushButton::clicked, this, &MasterNodesWidget::onCreateMNClicked);
    connect(ui->pushButtonStartAll, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_ALL);
    });
    connect(ui->pushButtonStartMissing, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_MISSING);
    });
    connect(ui->listMn, &QListView::clicked, this, &MasterNodesWidget::onMNClicked);

    applyModernTheme(isLightTheme());
}

void MasterNodesWidget::showEvent(QShowEvent *event)
{
    if (mnModel) {
        mnModel->updateMNList();
        updateListState();
    }
    if (!timer) {
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, [this]() {
            if (!isLoading.load() && mnModel) {
                mnModel->updateMNList();
                updateListState();
            }
        });
    }
    timer->start(30000);
}

void MasterNodesWidget::hideEvent(QHideEvent *event)
{
    if (timer) timer->stop();
}

void MasterNodesWidget::loadWalletModel()
{
    if (walletModel) {
        if (mnModel) mnModel->updateMNList();
        ui->listMn->setModel(proxyModel ? static_cast<QAbstractItemModel*>(proxyModel) : static_cast<QAbstractItemModel*>(mnModel));
        ui->listMn->setModelColumn(AddressTableModel::Label);
        updateListState();
    }
}

void MasterNodesWidget::setScopeMode(int mode)
{
    currentScopeMode = (mode == 1) ? 1 : 0;
    if (btnScopeMine) btnScopeMine->setChecked(currentScopeMode == 0);
    if (btnScopeAll) btnScopeAll->setChecked(currentScopeMode == 1);
    if (proxyModel) proxyModel->setScopeFilter(currentScopeMode);
    updateListState();
}

void MasterNodesWidget::updateSummaryChips()
{
    if (!mnModel || !lblKpiTotalVal || !lblKpiEnabledVal || !lblKpiInactiveVal || !lblKpiLockedVal)
        return;

    const bool onlyMine = (currentScopeMode == 0);
    int mineTotal = 0;
    int netTotal = mnModel->rowCount();
    int scopeTotal = 0;
    int scopeEnabled = 0;

    for (int r = 0; r < netTotal; ++r) {
        const QString alias = mnModel->index(r, MNModel::ALIAS, QModelIndex()).data(Qt::DisplayRole).toString();
        const bool isMine = mnModel->isMNWalletOwned(alias);
        if (isMine) mineTotal++;
        if (onlyMine && !isMine) continue;

        scopeTotal++;
        const QString status = mnModel->index(r, MNModel::STATUS, QModelIndex()).data(Qt::DisplayRole).toString();
        if (status == "ENABLED" || status == "PRE_ENABLED") {
            scopeEnabled++;
        }
    }

    const int scopeInactive = scopeTotal - scopeEnabled;
    const int visibleRows = proxyModel ? proxyModel->rowCount() : scopeTotal;

    const int curHeight = []() {
        LOCK(cs_main);
        return chainActive.Height();
    }();
    const CAmount perMnCollateral = CMasternode::GetMasternodeNodeCollateral(curHeight);
    const CAmount totalLocked = static_cast<CAmount>(scopeTotal) * perMnCollateral;
    const QLocale loc;

    if (btnScopeMine) btnScopeMine->setText(tr("My Masternodes (%1)").arg(mineTotal));
    if (btnScopeAll) btnScopeAll->setText(tr("All Network (%1)").arg(netTotal));
    if (lblShownCount) lblShownCount->setText(tr("%1 shown").arg(visibleRows));

    lblKpiTotalVal->setText(loc.toString(scopeTotal));
    if (lblKpiTotalSub) {
        lblKpiTotalSub->setText(onlyMine
            ? tr("All network: %1").arg(loc.toString(netTotal))
            : tr("Wallet owned: %1").arg(loc.toString(mineTotal)));
    }

    lblKpiEnabledVal->setText(loc.toString(scopeEnabled));
    if (lblKpiEnabledSub) {
        lblKpiEnabledSub->setText(tr("ENABLED / PRE_ENABLED"));
    }

    lblKpiInactiveVal->setText(loc.toString(scopeInactive));
    const QString alertVal = (scopeInactive > 0) ? QStringLiteral("true") : QStringLiteral("false");
    if (lblKpiInactiveVal->property("mnKpiAlert").toString() != alertVal) {
        lblKpiInactiveVal->setProperty("mnKpiAlert", alertVal);
        updateStyle(lblKpiInactiveVal);
    }
    if (lblKpiInactiveSub) {
        lblKpiInactiveSub->setText(scopeInactive > 0
            ? tr("Needs attention / start")
            : tr("All nodes healthy"));
    }

    lblKpiLockedVal->setText(QString("%1 %2")
        .arg(loc.toString(static_cast<qlonglong>(totalLocked / COIN)))
        .arg(QString::fromStdString(CURRENCY_UNIT)));
    if (lblKpiLockedSub) {
        lblKpiLockedSub->setText(tr("%1 %2 / node")
            .arg(loc.toString(static_cast<qlonglong>(perMnCollateral / COIN)))
            .arg(QString::fromStdString(CURRENCY_UNIT)));
    }
}

void MasterNodesWidget::updateListState()
{
    updateSummaryChips();
    const int totalSourceRows = mnModel ? mnModel->rowCount() : 0;
    const int visibleRows = proxyModel ? proxyModel->rowCount() : totalSourceRows;
    const bool onlyMine = (currentScopeMode == 0);
    int mineTotal = 0;
    if (mnModel) {
        for (int r = 0; r < totalSourceRows; ++r) {
            const QString alias = mnModel->index(r, MNModel::ALIAS, QModelIndex()).data(Qt::DisplayRole).toString();
            if (mnModel->isMNWalletOwned(alias)) {
                mineTotal++;
            }
        }
    }
    const int scopeTotalRows = onlyMine ? mineTotal : totalSourceRows;
    const bool hasScopeRows = scopeTotalRows > 0;
    const bool hasVisible = visibleRows > 0;

    ui->headerFrame->setVisible(hasScopeRows);
    ui->listMn->setVisible(hasScopeRows && hasVisible);
    if (lblFilteredEmpty) {
        lblFilteredEmpty->setVisible(hasScopeRows && !hasVisible);
    }
    ui->emptyContainer->setVisible(!hasScopeRows);
    ui->pushButtonStartAll->setVisible(mineTotal > 0);
    ui->pushButtonStartMissing->setVisible(mineTotal > 0);

    int metaCount = 0;
    int unregisteredMetaCount = 0;
    int inactiveLocalCount = 0;
    if (mnModel && walletModel) {
        for (const CMasternodeWalletRecord& rec : walletModel->getAllMasternodeMeta()) {
            const QString alias = QString::fromStdString(rec.strAlias);
            metaCount++;
            if (!mnModel->isMNOnChainDip3(alias)) {
                unregisteredMetaCount++;
            }
            try {
                if (mnModel->isMNInactive(alias)) {
                    inactiveLocalCount++;
                }
            } catch (...) {
            }
        }
    }
    if (ui->pushButtonStartMissing) {
        const bool canStartInactive = (inactiveLocalCount > 0);
        ui->pushButtonStartMissing->setEnabled(canStartInactive);
        ui->pushButtonStartMissing->setToolTip(canStartInactive
            ? tr("Register/start %1 inactive wallet-managed Masternode(s)").arg(inactiveLocalCount)
            : (metaCount == 0
                ? tr("No wallet-managed Masternodes to start")
                : tr("All wallet-managed Masternodes are already active")));
    }
    if (ui->pushButtonStartAll) {
        const bool canStartAll = (metaCount > 0) && (unregisteredMetaCount > 0 || inactiveLocalCount > 0);
        ui->pushButtonStartAll->setEnabled(canStartAll);
        ui->pushButtonStartAll->setToolTip(canStartAll
            ? tr("Register/start all wallet-managed Masternodes stored in wallet.dat")
            : (metaCount == 0
                ? tr("No wallet-managed Masternodes to start")
                : tr("All wallet-managed KTDIP3 Masternodes are already active on-chain")));
    }
}

void MasterNodesWidget::onMNClicked(const QModelIndex &index)
{
    ui->listMn->setCurrentIndex(index);
    QRect rect = ui->listMn->visualRect(index);
    this->index = index;

    QPoint localClickPos = ui->listMn->mapFromGlobal(QCursor::pos()) - rect.topLeft();

    // Calculate geometries by updating a mock row widget layout
    QWidget* rowWidget = delegate->getRowFactory()->createHolder(index.row());
    delegate->getRowFactory()->init(rowWidget, index, false, false);
    rowWidget->setGeometry(rect);
    rowWidget->resize(rect.width(), rect.height());
    if (rowWidget->layout()) {
        rowWidget->layout()->activate();
    }
    if (QWidget* frame2 = rowWidget->findChild<QWidget*>("frame_2")) {
        if (frame2->layout()) frame2->layout()->activate();
    }
    if (QWidget* actionsWrap = rowWidget->findChild<QWidget*>("actionsContainer")) {
        if (actionsWrap->layout()) actionsWrap->layout()->activate();
    }

    auto isBtnHit = [rowWidget, localClickPos](QPushButton* btn) {
        if (!btn || btn->isHidden()) return false;
        return QRect(btn->mapTo(rowWidget, QPoint(0, 0)), btn->size()).contains(localClickPos);
    };

    QPushButton* btnInfo = rowWidget->findChild<QPushButton*>("pushButtonInfo");
    QPushButton* btnStart = rowWidget->findChild<QPushButton*>("pushButtonStart");
    QPushButton* btnDelete = rowWidget->findChild<QPushButton*>("pushButtonDelete");

    if (isBtnHit(btnInfo)) {
        onInfoMNClicked();
    } else if (isBtnHit(btnStart)) {
        onEditMNClicked();
    } else if (isBtnHit(btnDelete)) {
        onDeleteMNClicked();
    }

    // Back to regular status
    ui->listMn->scrollTo(index);
    ui->listMn->clearSelection();
    ui->listMn->setFocus();
}

bool MasterNodesWidget::checkMNsNetwork()
{
    bool isTierTwoSync = mnModel->isMNsNetworkSynced();
    if (!isTierTwoSync) inform(tr("Please wait until the node is fully synced"));
    return isTierTwoSync;
}

void MasterNodesWidget::onEditMNClicked()
{
    if (walletModel) {
        if (!walletModel->isRegTestNetwork() && !checkMNsNetwork()) return;
        const QString strAlias = this->index.data(Qt::DisplayRole).toString();
        const QString status = this->index.sibling(this->index.row(), MNModel::STATUS).data(Qt::DisplayRole).toString();
        const bool isOnChainDip3 = this->index.data(Qt::UserRole + 2).toBool();
        const bool isInWalletMeta = this->index.data(Qt::UserRole + 3).toBool();

        if (!isInWalletMeta) {
            inform(tr("On-chain KTDIP3 Masternodes without local wallet.dat metadata cannot be started here. Use 'protx update_service' in Debug Console if PoSe-banned."));
            return;
        }
        if (isOnChainDip3 && status == QStringLiteral("ENABLED")) {
            inform(tr("Masternode '%1' is already active (ENABLED) on-chain.").arg(strAlias));
            return;
        }

        if (index.sibling(index.row(), MNModel::WAS_COLLATERAL_ACCEPTED).data(Qt::DisplayRole).toBool()) {
            // Start MN
            if (ask(tr("Start Masternode"), tr("Are you sure you want to register/start masternode %1?\n").arg(strAlias))) {
                WalletModel::UnlockContext ctx(walletModel->requestUnlock());
                if (!ctx.isValid()) {
                    // Unlock wallet was cancelled
                    inform(tr("Cannot register masternode, wallet locked"));
                    return;
                }
                startAlias(strAlias);
            }
        } else {
            inform(tr("Cannot start masternode, the collateral transaction has not been confirmed by the network yet.\n"
                    "Please wait few more minutes (masternode collaterals require %1 confirmations).").arg(MASTERNODE_MIN_CONFIRMATIONS));
        }
    }
}

void MasterNodesWidget::startAlias(QString strAlias)
{
    QString strStatusHtml;
    CMasternodeWalletRecord rec;
    if (walletModel && walletModel->getMasternodeMetaByAlias(strAlias, rec)) {
        std::string strError;
        strStatusHtml = (!startMN(rec, strError))
            ? tr("Alias %1 failed to register/start.\nError: %2").arg(strAlias, QString::fromStdString(strError))
            : tr("Alias %1 successfully registered on-chain (ProRegTx).").arg(strAlias);
    } else {
        strStatusHtml = tr("Alias '%1' was not found in wallet.dat metadata.").arg(strAlias);
    }
    // update UI and notify
    updateModelAndInform(strStatusHtml);
}

void MasterNodesWidget::updateModelAndInform(QString informText)
{
    mnModel->updateMNList();
    inform(informText);
}

bool MasterNodesWidget::startMN(const CMasternodeWalletRecord& rec, std::string& strError)
{
    if (!deterministicMNManager) {
        strError = "Deterministic MN manager is not initialized.";
        return false;
    }
    const COutPoint& colOut = rec.collateralOutpoint;
    if (colOut.IsNull()) {
        strError = "Invalid collateral outpoint in wallet.dat metadata.";
        return false;
    }

    const dip3::CDeterministicMNList dip3List = deterministicMNManager->GetListAtChainTip();
    const dip3::CDeterministicMN* existing = dip3List.GetMNByCollateral(colOut);
    if (existing) {
        if (existing->state.IsValid()) {
            return true;
        }
        strError = existing->state.IsBanned()
            ? "Masternode is PoSe-banned on-chain. Use 'protx update_service' to revive."
            : "Masternode is already registered on-chain but expired/revoked.";
        return false;
    }

    CService addr;
    if (!Lookup(rec.strService.c_str(), addr, Params().GetDefaultPort(), false) || !addr.IsValid() || addr.GetPort() == 0) {
        strError = "Invalid Masternode IP:port in wallet.dat metadata.";
        return false;
    }

    CBLSPubKey opPk;
    bool fGotOpPk = false;
    if (!rec.strOperatorBlsPubKey.empty()) {
        std::vector<unsigned char> vchOpPub = ParseHex(rec.strOperatorBlsPubKey);
        if (vchOpPub.size() == 48 && opPk.SetBuf(vchOpPub.data(), vchOpPub.size()) && opPk.IsValid()) {
            fGotOpPk = true;
        }
    }
    CBLSSignature opPoP;
    if (!rec.strOperatorBlsPoP.empty()) {
        std::vector<unsigned char> vchPoP = ParseHex(rec.strOperatorBlsPoP);
        if (vchPoP.size() == 96) {
            opPoP.SetBuf(vchPoP.data(), vchPoP.size());
        }
    }
    if ((!fGotOpPk || !opPoP.IsValid()) && walletModel) {
        CKey mnKey;
        CBLSSecretKey opSk;
        if (walletModel->getMasternodeOperatorKeys(colOut, mnKey, opSk)) {
            if (!fGotOpPk) {
                opPk = CBLSPubKey(opSk);
                fGotOpPk = opPk.IsValid();
            }
            if (!opPoP.IsValid()) {
                opPoP.SignPoP(opSk);
            }
            opSk.Reset();
            mnKey = CKey();
        }
    }
    if (!fGotOpPk) {
        strError = "No valid BLS operator public key available in wallet.dat metadata.";
        return false;
    }

    std::string proTxHashOut;
    return RegisterDip3MasternodeFromWallet(
        colOut, addr, opPk, proTxHashOut, strError, rec.nOperatorRewardBps, rec.strPayoutAddress,
        opPoP.IsValid() ? &opPoP : nullptr);
}

void MasterNodesWidget::onStartAllClicked(int type)
{
    if (!Params().IsRegTestNet() && !checkMNsNetwork()) return;     // skip on RegNet: so we can test even if tier two not synced
    if (!mnModel || !walletModel) return;

    if (isLoading.load()) {
        inform(tr("Background task is being executed, please wait"));
        return;
    }

    // Snapshot candidate records safely on the GUI thread so the background worker never touches mnModel
    const bool onlyMissing = (type == REQUEST_START_MISSING);
    std::vector<CMasternodeWalletRecord> candidateRecords;
    int preFailedCount = 0;

    for (const CMasternodeWalletRecord& rec : walletModel->getAllMasternodeMeta()) {
        const QString mnAlias = QString::fromStdString(rec.strAlias);
        try {
            if (onlyMissing && !mnModel->isMNInactive(mnAlias)) {
                if (!mnModel->isMNActive(mnAlias)) {
                    preFailedCount++;
                }
                continue;
            }
            if (!mnModel->isMNCollateralMature(mnAlias)) {
                preFailedCount++;
                continue;
            }
            candidateRecords.push_back(rec);
        } catch (const std::exception&) {
            preFailedCount++;
        }
    }

    if (candidateRecords.empty()) {
        if (preFailedCount == 0) {
            inform(tr("All wallet-managed Masternodes are already active."));
        } else {
            inform(tr("%1 Masternode(s) cannot be started yet (collateral immature or not ready).").arg(preFailedCount));
        }
        return;
    }

    std::unique_ptr<WalletModel::UnlockContext> pctx = MakeUnique<WalletModel::UnlockContext>(walletModel->requestUnlock());
    if (!pctx->isValid()) {
        warn(tr("Start ALL masternodes failed"), tr("Wallet unlock cancelled"));
        return;
    }

    m_pendingStartRecords = std::move(candidateRecords);
    m_pendingPreFailed = preFailedCount;
    isLoading = true;
    if (!execute(type, std::move(pctx))) {
        isLoading = false;
        m_pendingStartRecords.clear();
        m_pendingPreFailed = 0;
        inform(tr("Cannot perform Masternodes start"));
    }
}

bool MasterNodesWidget::startAll(QString& failText, bool /*onlyMissing*/)
{
    int amountOfMnFailed = m_pendingPreFailed;
    int amountOfMnStarted = 0;
    for (const CMasternodeWalletRecord& rec : m_pendingStartRecords) {
        std::string strError;
        if (!startMN(rec, strError)) {
            amountOfMnFailed++;
        } else {
            amountOfMnStarted++;
        }
    }
    if (amountOfMnFailed > 0) {
        failText = tr("%1 Masternodes failed to start, %2 started").arg(amountOfMnFailed).arg(amountOfMnStarted);
        return false;
    }
    return true;
}

void MasterNodesWidget::run(int type)
{
    bool isStartMissing = type == REQUEST_START_MISSING;
    if (type == REQUEST_START_ALL || isStartMissing) {
        QString failText;
        const bool ok = startAll(failText, isStartMissing);
        const int startedCount = static_cast<int>(m_pendingStartRecords.size());
        m_pendingStartRecords.clear();
        m_pendingPreFailed = 0;
        QString informMsg = ok ? tr("%1 Masternode(s) started!").arg(startedCount) : failText;
        QMetaObject::invokeMethod(this, "updateModelAndInform", Qt::QueuedConnection,
                                  Q_ARG(QString, informMsg));
    } else {
        m_pendingStartRecords.clear();
        m_pendingPreFailed = 0;
    }

    isLoading = false;
}

void MasterNodesWidget::onError(QString error, int type)
{
    if (type == REQUEST_START_ALL) {
        QMetaObject::invokeMethod(this, "inform", Qt::QueuedConnection,
                                  Q_ARG(QString, "Error starting all Masternodes"));
    }
}

void MasterNodesWidget::onInfoMNClicked()
{
    if (!walletModel || !mnModel) return;
    showHideOp(true);
    MnInfoDialog* dialog = new MnInfoDialog(window);
    QString label = index.data(Qt::DisplayRole).toString();
    QString address = index.sibling(index.row(), MNModel::ADDRESS).data(Qt::DisplayRole).toString();
    QString status = index.sibling(index.row(), MNModel::STATUS).data(Qt::DisplayRole).toString();
    QString txId = index.sibling(index.row(), MNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), MNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString pubKey = index.sibling(index.row(), MNModel::PUB_KEY).data(Qt::DisplayRole).toString();
    const bool hasLocalOpKey = index.data(Qt::UserRole + 12).toBool();
    dialog->setData(pubKey, label, address, txId, outIndex, status, hasLocalOpKey);
    MNModel::Dip3Meta dip3Meta;
    if (mnModel->getDip3Meta(label, dip3Meta)) {
        dialog->setDip3Meta(dip3Meta);
    }
    dialog->adjustSize();
    showDialog(dialog, 3, 17);
    if (dialog->exportMN) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) {
            inform(tr("Cannot export Masternode operator key, wallet locked"));
            dialog->deleteLater();
            return;
        }
        bool outOk = false;
        const unsigned int nOutIdx = outIndex.toUInt(&outOk);
        CKey mnKey;
        CBLSSecretKey opSk;
        if (!outOk || !walletModel->getMasternodeOperatorKeys(COutPoint(uint256S(txId.toStdString()), nOutIdx), mnKey, opSk)) {
            inform(tr("Operator private key is not stored in this wallet (external operator key)."));
            dialog->deleteLater();
            return;
        }
        QString cleanAlias = label.simplified().replace(' ', '_');
        if (cleanAlias.isEmpty()) cleanAlias = "mn1";
        QString hostIp = address.contains(":") ? address.left(address.lastIndexOf(":")) : address;
        if (ask(tr("Export Remote Masternode (activemasternode.conf)"),
                tr("Copy this Masternode's operator entry for activemasternode.conf to your clipboard?\n\n"
                   "1. Paste the copied line (<alias> <privkey> [bls_privkey_hex]) into ~/.kristatech/activemasternode.conf on your remote server.\n"
                   "2. Ensure masternode=1 and externalip=%1 are set in ~/.kristatech/kristatech.conf "
                   "(do NOT add masternodeprivkey=... to kristatech.conf).\n"
                   "3. Restart the remote daemon, then register/update on-chain via KTDIP3 (protx register) or start from this controller wallet.").arg(hostIp))) {
            std::string wifStr = EncodeSecret(mnKey);
            std::string blsStr = opSk.IsValid() ? HexStr(opSk.begin(), opSk.end()) : std::string();
            QString exportedMN = blsStr.empty()
                ? QString("%1 %2\n").arg(cleanAlias, QString::fromStdString(wifStr))
                : QString("%1 %2 %3\n").arg(cleanAlias, QString::fromStdString(wifStr), QString::fromStdString(blsStr));
            GUIUtil::setSensitiveClipboard(exportedMN);
            if (!wifStr.empty()) memory_cleanse(&wifStr[0], wifStr.size());
            if (!blsStr.empty()) memory_cleanse(&blsStr[0], blsStr.size());
            exportedMN.fill(QChar('0'));
            inform(tr("activemasternode.conf entry copied to clipboard (auto-clears in 60s)."));
        }
        opSk.Reset();
        mnKey = CKey();
    }

    dialog->deleteLater();
}

void MasterNodesWidget::onDeleteMNClicked()
{
    QString txId = index.sibling(index.row(), MNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), MNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString address = index.sibling(index.row(), MNModel::ADDRESS).data(Qt::DisplayRole).toString();
    QString qAliasString = index.data(Qt::DisplayRole).toString();

    const bool isInWalletMeta = index.data(Qt::UserRole + 3).toBool();
    bool isDip3OnChain = index.data(Qt::UserRole + 2).toBool();

    bool convertOutOk = false;
    const unsigned int parsedOutIdx = outIndex.toUInt(&convertOutOk);
    if (!convertOutOk) {
        inform(tr("Invalid collateral output index."));
        return;
    }
    const COutPoint colOut(uint256S(txId.toStdString()), parsedOutIdx);
    if (!isDip3OnChain && deterministicMNManager) {
        if (deterministicMNManager->GetListAtChainTip().GetMNByCollateral(colOut) != nullptr) {
            isDip3OnChain = true;
        }
    }
    bool isPendingProReg = false;
    {
        LOCK(mempool.cs);
        for (const auto& entry : mempool.mapTx) {
            std::vector<unsigned char> vchPayload;
            bool fMalformed = false;
            if (dip3::ExtractProTxPayload(entry.GetTx(), vchPayload, fMalformed) == dip3::PROTX_REG && !fMalformed) {
                try {
                    CDataStream ds(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
                    dip3::CProRegTx pendingPtx;
                    ds >> pendingPtx;
                    if (pendingPtx.collateralOutpoint == colOut) {
                        isPendingProReg = true;
                        break;
                    }
                } catch (...) {
                }
            }
        }
    }
    const bool isDip3Protected = isDip3OnChain || isPendingProReg;

    if (!isInWalletMeta) {
        showThemedNotice(
            this,
            tr("On-Chain KTDIP3 Masternode"),
            tr("This KTDIP3 Masternode is registered on-chain without local wallet.dat metadata and cannot be deleted locally.\n\n"
               "To revoke it on-chain, run 'protx revoke' in the RPC Debug Console."),
            false);
        return;
    }

    const QString shortTx = txId.length() > 16 ? (txId.left(8) + "..." + txId.right(8)) : txId;
    const QString confirmMsg = isDip3Protected
        ? tr("WARNING: You are about to remove a KTDIP3 Deterministic Masternode metadata entry from wallet.dat:\n\n"
             "• Alias: %1\n"
             "• Service: %2\n"
             "• Collateral: %3:%4\n\n"
             "This Masternode is currently ACTIVE OR PENDING ON-CHAIN. Removing it here only deletes the local controller alias; "
             "the 4,200 KRISTA collateral UTXO will REMAIN LOCKED to prevent accidental spend, and the node will still appear on-chain as 'dip3-<protx>' until revoked via 'protx revoke'.\n\n"
             "Do you want to continue?")
            .arg(qAliasString, address, shortTx, outIndex)
        : tr("WARNING: Deleting this Masternode draft cannot be undone!\n\n"
             "• Alias: %1\n"
             "• Service: %2\n"
             "• Collateral: %3:%4\n\n"
             "This will permanently remove the local metadata entry from wallet.dat and UNLOCK the 4,200 KRISTA collateral UTXO "
             "(spending it would prevent starting this Masternode).\n\n"
             "Are you sure you want to delete this Masternode?")
            .arg(qAliasString, address, shortTx, outIndex);

    if (!showThemedConfirm(this, tr("Delete Masternode"), confirmMsg, tr("DELETE"), tr("CANCEL"), true))
        return;

    if (!walletModel || !walletModel->removeMasternodeMeta(colOut)) {
        inform(tr("Failed to remove Masternode metadata from wallet.dat."));
        return;
    }

    // Unlock collateral ONLY if neither active on-chain nor pending PROTX_REG in mempool
    if (!isDip3Protected) {
        COutPoint colOutUnlock(colOut);
        walletModel->unlockCoin(colOutUnlock);
    }

    // Refresh list state
    if (isDip3Protected) {
        mnModel->updateMNList();
    } else {
        QModelIndex sourceIdx = proxyModel ? proxyModel->mapToSource(index) : index;
        mnModel->removeMn(sourceIdx);
    }
    updateListState();
}

void MasterNodesWidget::onCreateMNClicked()
{
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        // Unlock wallet was cancelled
        inform(tr("Cannot create Masternode controller, wallet locked"));
        return;
    }

    int nTipHeight = 0;
    {
        LOCK(cs_main);
        nTipHeight = chainActive.Height();
    }
    const CAmount requiredCollateral = CMasternode::GetMasternodeNodeCollateral(nTipHeight);
    COutPoint existingCandidate;
    if (!walletModel->getMNCollateralCandidate(existingCandidate) &&
        walletModel->getUnlockedBalance() <= requiredCollateral) {
        inform(tr("Not enough unlocked %1 balance to fund a %2 Masternode collateral (+ network fee).")
                   .arg(CURRENCY_UNIT.c_str())
                   .arg(BitcoinUnits::formatWithUnit(BitcoinUnits::PIV, requiredCollateral)));
        return;
    }
    showHideOp(true);
    MasterNodeWizardDialog *dialog = new MasterNodeWizardDialog(walletModel, window);
    if (openDialogWithOpaqueBackgroundY(dialog, window, 5, 7)) {
        if (dialog->isOk) {
            // Update list
            if (dialog->hasRecord) {
                mnModel->addMn(dialog->mnRecord);
            } else {
                mnModel->updateMNList();
            }
            updateListState();
            if (!dialog->createdActiveMnLine.isEmpty()) {
                DefaultDialog* copyDlg = new DefaultDialog(window);
                QString detailsText = dialog->createdProTxHash.isEmpty()
                    ? tr("%1\n\nactivemasternode.conf:\n%2")
                        .arg(dialog->returnStr, dialog->createdActiveMnLine)
                    : tr("%1\n\nNode config:\n%2\n\nRegistration reference:\n%3")
                        .arg(dialog->returnStr, dialog->createdActiveMnLine, dialog->createdProTxHash);
                copyDlg->setText(
                    tr("Masternode Created"),
                    detailsText,
                    tr("COPY LINE"),
                    tr("CLOSE")
                );
                if (openDialogWithOpaqueBackgroundY(copyDlg, window, 5, 7)) {
                    GUIUtil::setSensitiveClipboard(dialog->createdActiveMnLine);
                    inform(tr("Copied activemasternode.conf line to clipboard (auto-clears in 60s)."));
                } else {
                    inform(dialog->returnStr);
                }
                detailsText.fill(QChar('0'));
                copyDlg->deleteLater();
            } else {
                inform(dialog->returnStr);
            }
        } else if (dialog->consolidationPending) {
            DefaultDialog* infoDlg = new DefaultDialog(window);
            infoDlg->setText(
                tr("UTXO Consolidation Broadcast"),
                dialog->returnStr,
                tr("OK"),
                QString()
            );
            openDialogWithOpaqueBackgroundY(infoDlg, window, 5, 7);
            infoDlg->deleteLater();
        } else {
            warn(tr("Error creating masternode"), dialog->returnStr);
        }
    }
    dialog->createdActiveMnLine.fill(QChar('0'));
    dialog->deleteLater();
}

void MasterNodesWidget::applyModernTheme(bool isLight)
{
    if (!mnPageRoot) return;

    const QString canvasBg    = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0B1120");
    const QString cardBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#131B2E");
    const QString cardBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#253554");
    const QString subtleBg    = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0F172A");
    const QString divider     = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2D4A");
    const QString titleCol    = isLight ? QStringLiteral("#0F172A") : QStringLiteral("#F1F5F9");
    const QString subCol      = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString kpiBlue     = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString kpiAlert    = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#FCA5A5");

    const QString inputBg     = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#0F172A");
    const QString inputBrd    = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2D3F63");
    const QString pillBg      = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1E293B");
    const QString pillFg      = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString pillBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#334155");
    const QString pillActBg   = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString pillActFg   = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString pillActHov  = isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860");
    const QString dropArrow   = isLight ? QStringLiteral("://ic-arrow-drop-down") : QStringLiteral("://ic-arrow-drop-down-white");

    const QString priBg       = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#4A6AB0");
    const QString priHov      = isLight ? QStringLiteral("#2F4A7D") : QStringLiteral("#5576B2");
    const QString priPres     = isLight ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94");
    const QString secBg       = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#1A2540");
    const QString secFg       = isLight ? QStringLiteral("#334155") : QStringLiteral("#E2E8F0");
    const QString secBrd      = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2D3F63");
    const QString secHov      = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#223254");
    const QString secPres     = isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#162038");

    QString css = QString(
        "QWidget#mnPageRoot { background-color: %1; border: none; }"
        "QWidget#mnPageRoot QWidget#mnToolbarWrap, "
        "QWidget#mnPageRoot QWidget#mnFooterWrap, "
        "QWidget#mnPageRoot QWidget#emptyContainer, "
        "QWidget#mnPageRoot QWidget#emptyScreen { background-color: transparent; border: none; }"
        "QWidget#mnPageRoot QFrame[mnKpiCard=\"true\"], "
        "QWidget#mnPageRoot QFrame[mnCard=\"true\"] { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
        "QWidget#mnPageRoot QFrame[mnDivider=\"true\"] { background-color: %4; border: none; }"
        "QWidget#mnPageRoot QFrame#headerFrame { background-color: %5; border: none; border-bottom: 1px solid %4; }"
    ).arg(canvasBg, cardBg, cardBrd, divider, subtleBg);

    css += QString(
        "QWidget#mnPageRoot QLabel[mnTitle=\"true\"] { color: %1; font-size: 22px; font-weight: 500; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnSubtitle=\"true\"] { color: %2; font-size: 13px; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnKpiKey=\"true\"] { color: %2; font-size: 11px; font-weight: 600; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnKpiVal=\"true\"] { color: %1; font-size: 20px; font-weight: 600; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnKpiAccent=\"blue\"] { color: %3; }"
        "QWidget#mnPageRoot QLabel[mnKpiAlert=\"true\"] { color: %4; }"
        "QWidget#mnPageRoot QLabel[mnKpiSub=\"true\"] { color: %2; font-size: 11px; background: transparent; }"
    ).arg(titleCol, subCol, kpiBlue, kpiAlert);

    css += QString(
        "QWidget#mnPageRoot QLabel[mnSectionTitle=\"true\"] { color: %1; font-size: 15px; font-weight: 600; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnColHeader=\"true\"] { color: %2; font-size: 11px; font-weight: 600; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnRuleNote=\"true\"] { color: %2; font-size: 11px; background: transparent; }"
        "QWidget#mnPageRoot QLabel[mnEmptyHint=\"true\"] { color: %2; font-size: 13px; background: transparent; padding: 36px 16px; }"
        "QWidget#mnPageRoot QLabel[mnCountPill=\"true\"] { background-color: %3; color: %4; border: 1px solid %5; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
    ).arg(titleCol, subCol, pillBg, pillFg, pillBrd);

    const QString focCol  = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString priFoc  = isLight ? QStringLiteral("#1E3A8A") : QStringLiteral("#93B4F0");
    const QString disBg   = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E293B");
    const QString disFg   = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#475569");
    const QString disBrd  = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#263449");

    css += QString(
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 15px; padding: 0 13px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:hover { border-color: #7695CD; background-color: %4; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:checked { background-color: %4; color: %5; border: 1px solid #7695CD; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:checked:hover { background-color: %6; border-color: %5; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:focus { outline: none; border: 1.5px solid %7; }"
    ).arg(secBg, secFg, secBrd, pillActBg, pillActFg, pillActHov, focCol);

    css += QString(
        "QWidget#mnPageRoot QLineEdit[mnInput=\"true\"] { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 12px; }"
        "QWidget#mnPageRoot QLineEdit[mnInput=\"true\"]:focus { border-color: #7695CD; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"] { combobox-popup: 0; background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 4px 26px 4px 10px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]:hover { border-color: #7695CD; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]:focus { outline: none; border: 1.5px solid %6; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 24px; border: none; background: transparent; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]::down-arrow { image: url(\"%4\"); width: 12px; height: 12px; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"] QAbstractItemView { background-color: %5; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; outline: none; }"
    ).arg(inputBg, titleCol, inputBrd, dropArrow, cardBg, focCol);

    css += QString(
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 13px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:hover:enabled { background-color: %4; border-color: #7695CD; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:pressed:enabled { background-color: %5; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:disabled { background-color: %9; color: %10; border-color: %11; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:focus { outline: none; border: 1.5px solid %12; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"] { background-color: %6; color: #FFFFFF; border: 1px solid %6; border-radius: 7px; padding: 0 16px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:hover:enabled { background-color: %7; border-color: %7; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:pressed:enabled { background-color: %8; border-color: %8; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:disabled { background-color: %9; color: %10; border-color: %11; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:focus { outline: none; border: 2px solid %13; }"
    ).arg(secBg, secFg, secBrd, secHov, secPres, priBg, priHov, priPres, disBg, disFg, disBrd, focCol, priFoc);

    css += QString(
        "QWidget#mnPageRoot QListView#listMn { background-color: %1; border: none; outline: none; }"
        "QWidget#mnPageRoot QWidget#mnListViewport { background-color: %1; border: none; }"
    ).arg(cardBg);

    mnPageRoot->setStyleSheet(css);
}

void MasterNodesWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<MNHolder*>(this->delegate->getRowFactory())->isLightTheme = isLightTheme;
    applyModernTheme(isLightTheme);
    if (lineEditSearchMN) {
        styleLineEditClearButton(lineEditSearchMN);
    }
    if (ui->listMn && ui->listMn->viewport()) {
        ui->listMn->viewport()->update();
    }
}

MasterNodesWidget::~MasterNodesWidget()
{
    delete ui;
}
