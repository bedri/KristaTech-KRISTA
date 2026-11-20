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
#include "masternode-sync.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "netbase.h"
#include "sync.h"
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
        row->updateView(address, label, status, wasCollateralAccepted, txId, isWalletOwned);
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
        tr("Manage 4,200 KRISTA collateralized KTDIP3 Masternodes (35% Passive Pool \xE2\x80\xA2 20% Validator Track \xE2\x80\xA2 10% LLMQ Quorum)."),
        mnPageRoot);
    lblPageSubtitle->setProperty("mnSubtitle", true);
    lblPageSubtitle->setWordWrap(true);
    titleCol->addWidget(lblPageTitle);
    titleCol->addWidget(lblPageSubtitle);

    QHBoxLayout* headerActions = new QHBoxLayout();
    headerActions->setContentsMargins(0, 0, 0, 0);
    headerActions->setSpacing(8);

    btnOpenActiveMNConf = new QPushButton(tr("activemasternode.conf (Hot)"), mnPageRoot);
    btnOpenActiveMNConf->setCursor(Qt::PointingHandCursor);
    btnOpenActiveMNConf->setFocusPolicy(Qt::NoFocus);
    btnOpenActiveMNConf->setFixedHeight(34);
    btnOpenActiveMNConf->setProperty("mnSecondary", true);
    btnOpenActiveMNConf->setToolTip(tr("Open or initialize activemasternode.conf for single-IP multi-Masternode operation"));
    connect(btnOpenActiveMNConf, &QPushButton::clicked, [this]() {
        if (!GUIUtil::openActiveMNConfigfile()) {
            inform(tr("Unable to open activemasternode.conf."));
        }
    });

    btnOpenControllerMNConf = new QPushButton(tr("masternode.conf (Controller)"), mnPageRoot);
    btnOpenControllerMNConf->setCursor(Qt::PointingHandCursor);
    btnOpenControllerMNConf->setFocusPolicy(Qt::NoFocus);
    btnOpenControllerMNConf->setFixedHeight(34);
    btnOpenControllerMNConf->setProperty("mnSecondary", true);
    btnOpenControllerMNConf->setToolTip(tr("Open cold controller masternode.conf file"));
    connect(btnOpenControllerMNConf, &QPushButton::clicked, [this]() {
        if (!GUIUtil::openMNConfigfile()) {
            inform(tr("masternode.conf does not exist yet — create a Masternode first or open the Data Directory."));
        }
    });

    btnRefreshMNs = new QPushButton(QString::fromUtf8("\xE2\x86\xBB ") + tr("Refresh"), mnPageRoot);
    btnRefreshMNs->setCursor(Qt::PointingHandCursor);
    btnRefreshMNs->setFocusPolicy(Qt::NoFocus);
    btnRefreshMNs->setFixedHeight(34);
    btnRefreshMNs->setProperty("mnSecondary", true);
    btnRefreshMNs->setToolTip(tr("Refresh Masternode list and on-chain status now"));
    connect(btnRefreshMNs, &QPushButton::clicked, [this]() {
        std::string strErr;
        masternodeConfig.clear();
        if (!masternodeConfig.read(strErr) && !strErr.empty()) {
            inform(QString::fromStdString(strErr));
        }
        if (mnModel) {
            mnModel->updateMNList();
            updateListState();
        }
    });

    headerActions->addWidget(btnOpenActiveMNConf);
    headerActions->addWidget(btnOpenControllerMNConf);
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
    btnScopeMine->setFocusPolicy(Qt::NoFocus);
    btnScopeMine->setFixedHeight(32);
    btnScopeMine->setProperty("mnScopePill", true);

    btnScopeAll = new QPushButton(tr("All Network"), toolbarWrap);
    btnScopeAll->setCheckable(true);
    btnScopeAll->setChecked(false);
    btnScopeAll->setCursor(Qt::PointingHandCursor);
    btnScopeAll->setFocusPolicy(Qt::NoFocus);
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

    ui->labelHeaderAlias->setMinimumWidth(110);
    ui->labelHeaderAlias->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderAddress->setMinimumWidth(155);
    ui->labelHeaderAddress->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderTxId->setMinimumWidth(175);
    ui->labelHeaderTxId->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->labelHeaderStatus->setFixedWidth(145);
    ui->labelHeaderStatus->setAlignment(Qt::AlignCenter);
    ui->labelHeaderActions->setFixedWidth(205);
    ui->labelHeaderActions->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    ui->labelHeaderAlias->setProperty("mnColHeader", true);
    ui->labelHeaderAddress->setProperty("mnColHeader", true);
    ui->labelHeaderTxId->setProperty("mnColHeader", true);
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

    /* Card Footer Action Bar: Rule 8 Reminder (left) + Primary/Secondary Actions (right) */
    QWidget* footerWrap = new QWidget(tableCard);
    footerWrap->setObjectName("mnFooterWrap");
    QHBoxLayout* footerLayout = new QHBoxLayout(footerWrap);
    footerLayout->setContentsMargins(18, 12, 18, 12);
    footerLayout->setSpacing(12);

    lblRule8Note = new QLabel(
        tr("Rule 8 (KTDIP3): Remote hot servers load '<alias> <privkey> <bls_privkey_hex>' from activemasternode.conf — never put masternodeprivkey in kristatech.conf."),
        footerWrap);
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
    ui->pushButtonStartMissing->setFocusPolicy(Qt::NoFocus);

    ui->pushButtonStartAll->setMinimumSize(0, 36);
    ui->pushButtonStartAll->setMaximumSize(16777215, 36);
    ui->pushButtonStartAll->setCursor(Qt::PointingHandCursor);
    ui->pushButtonStartAll->setFocusPolicy(Qt::NoFocus);

    ui->pushButtonSave->setMinimumSize(0, 36);
    ui->pushButtonSave->setMaximumSize(16777215, 36);
    ui->pushButtonSave->setCursor(Qt::PointingHandCursor);
    ui->pushButtonSave->setFocusPolicy(Qt::NoFocus);

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
            mnModel->updateMNList();
            updateListState();
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
    const bool hasAnySource = totalSourceRows > 0;
    const bool hasVisible = visibleRows > 0;

    ui->headerFrame->setVisible(hasAnySource);
    ui->listMn->setVisible(hasAnySource && hasVisible);
    if (lblFilteredEmpty) {
        lblFilteredEmpty->setVisible(hasAnySource && !hasVisible);
    }
    ui->emptyContainer->setVisible(!hasAnySource);
    ui->pushButtonStartAll->setVisible(hasAnySource);
    ui->pushButtonStartMissing->setVisible(hasAnySource);
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
        if (index.sibling(index.row(), MNModel::WAS_COLLATERAL_ACCEPTED).data(Qt::DisplayRole).toBool()) {
            // Start MN
            QString strAlias = this->index.data(Qt::DisplayRole).toString();
            if (ask(tr("Start Masternode"), tr("Are you sure you want to start masternode %1?\n").arg(strAlias))) {
                WalletModel::UnlockContext ctx(walletModel->requestUnlock());
                if (!ctx.isValid()) {
                    // Unlock wallet was cancelled
                    inform(tr("Cannot edit masternode, wallet locked"));
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
    strStatusHtml += "Alias: " + strAlias + " ";

    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias.toStdString()) {
            std::string strError;
            strStatusHtml += (!startMN(mne, strError)) ? ("failed to start.\nError: " + QString::fromStdString(strError)) : "successfully started.";
            break;
        }
    }
    // update UI and notify
    updateModelAndInform(strStatusHtml);
}

void MasterNodesWidget::updateModelAndInform(QString informText)
{
    mnModel->updateMNList();
    inform(informText);
}

bool MasterNodesWidget::startMN(CMasternodeConfig::CMasternodeEntry mne, std::string& strError)
{
    const bool fDip3Active = Params().GetConsensus().NetworkUpgradeActive(
        chainActive.Height() + 1, Consensus::UPGRADE_DIP3);
    if (fDip3Active && deterministicMNManager) {
        int nIndex = 0;
        if (!mne.castOutputIndex(nIndex) || nIndex < 0) {
            strError = "Invalid collateral output index.";
            return false;
        }
        const COutPoint colOut(uint256S(mne.getTxHash()), static_cast<uint32_t>(nIndex));
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
        if (!Lookup(mne.getIp().c_str(), addr, Params().GetDefaultPort(), false) || !addr.IsValid() || addr.GetPort() == 0) {
            strError = "Invalid Masternode IP:port in masternode.conf.";
            return false;
        }
        CKey mnKey = DecodeSecret(mne.getPrivKey());
        if (!mnKey.IsValid()) {
            strError = "Invalid masternodeprivkey in masternode.conf.";
            return false;
        }
        CBLSSecretKey opSk = DeriveBLSFromCKey(mnKey);
        if (!opSk.IsValid()) {
            strError = "Failed to derive BLS operator key.";
            return false;
        }
        CBLSPubKey opPk(opSk);
        std::string proTxHashOut;
        return RegisterDip3MasternodeFromWallet(colOut, addr, opPk, proTxHashOut, strError);
    }

    CMasternodeBroadcast mnb;
    if (!CMasternodeBroadcast::Create(mne.getIp(), mne.getPrivKey(), mne.getTxHash(), mne.getOutputIndex(), strError, mnb))
        return false;

    mnodeman.UpdateMasternodeList(mnb);
    mnb.Relay();
    return true;
}

void MasterNodesWidget::onStartAllClicked(int type)
{
    if (!Params().IsRegTestNet() && !checkMNsNetwork()) return;     // skip on RegNet: so we can test even if tier two not synced

    if (isLoading) {
        inform(tr("Background task is being executed, please wait"));
    } else {
        std::unique_ptr<WalletModel::UnlockContext> pctx = MakeUnique<WalletModel::UnlockContext>(walletModel->requestUnlock());
        if (!pctx->isValid()) {
            warn(tr("Start ALL masternodes failed"), tr("Wallet unlock cancelled"));
            return;
        }
        isLoading = true;
        if (!execute(type, std::move(pctx))) {
            isLoading = false;
            inform(tr("Cannot perform Masternodes start"));
        }
    }
}

bool MasterNodesWidget::startAll(QString& failText, bool onlyMissing)
{
    int amountOfMnFailed = 0;
    int amountOfMnStarted = 0;
    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        // Check for missing only
        QString mnAlias = QString::fromStdString(mne.getAlias());
        if (onlyMissing && !mnModel->isMNInactive(mnAlias)) {
            if (!mnModel->isMNActive(mnAlias))
                amountOfMnFailed++;
            continue;
        }

        if (!mnModel->isMNCollateralMature(mnAlias)) {
            amountOfMnFailed++;
            continue;
        }

        std::string strError;
        if (!startMN(mne, strError)) {
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
        QString inform = startAll(failText, isStartMissing) ? tr("All Masternodes started!") : failText;
        QMetaObject::invokeMethod(this, "updateModelAndInform", Qt::QueuedConnection,
                                  Q_ARG(QString, inform));
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
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        // Unlock wallet was cancelled
        inform(tr("Cannot show Masternode information, wallet locked"));
        return;
    }
    showHideOp(true);
    MnInfoDialog* dialog = new MnInfoDialog(window);
    QString label = index.data(Qt::DisplayRole).toString();
    QString address = index.sibling(index.row(), MNModel::ADDRESS).data(Qt::DisplayRole).toString();
    QString status = index.sibling(index.row(), MNModel::STATUS).data(Qt::DisplayRole).toString();
    QString txId = index.sibling(index.row(), MNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), MNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString pubKey = index.sibling(index.row(), MNModel::PUB_KEY).data(Qt::DisplayRole).toString();
    QString mnPrivKey = index.sibling(index.row(), MNModel::PRIV_KEY).data(Qt::DisplayRole).toString();
    dialog->setData(pubKey, label, address, txId, outIndex, status, mnPrivKey);
    dialog->adjustSize();
    showDialog(dialog, 3, 17);
    if (dialog->exportMN) {
        QString cleanAlias = label.simplified().replace(' ', '_');
        if (cleanAlias.isEmpty()) cleanAlias = "mn1";
        QString hostIp = address.contains(":") ? address.left(address.lastIndexOf(":")) : address;
        if (ask(tr("Export Remote Masternode (activemasternode.conf)"),
                tr("Copy this Masternode's operator entry for activemasternode.conf to your clipboard?\n\n"
                   "1. Paste the copied line (<alias> <privkey> [bls_privkey_hex]) into ~/.kristatech/activemasternode.conf on your remote server.\n"
                   "2. Ensure masternode=1 and externalip=%1 are set in ~/.kristatech/kristatech.conf "
                   "(do NOT add masternodeprivkey=... to kristatech.conf).\n"
                   "3. Restart the remote daemon, then register/update on-chain via KTDIP3 (protx register) or start from this controller wallet.").arg(hostIp))) {
            CKey mnKey = DecodeSecret(mnPrivKey.toStdString());
            CBLSSecretKey opSk = mnKey.IsValid() ? DeriveBLSFromCKey(mnKey) : CBLSSecretKey();
            QString exportedMN = opSk.IsValid()
                ? QString("%1 %2 %3\n").arg(cleanAlias, mnPrivKey, QString::fromStdString(HexStr(opSk.begin(), opSk.end())))
                : QString("%1 %2\n").arg(cleanAlias, mnPrivKey);
            GUIUtil::setClipboard(exportedMN);
            inform(tr("activemasternode.conf entry copied to clipboard."));
        }
    }

    dialog->deleteLater();
}

void MasterNodesWidget::onDeleteMNClicked()
{
    QString txId = index.sibling(index.row(), MNModel::COLLATERAL_ID).data(Qt::DisplayRole).toString();
    QString outIndex = index.sibling(index.row(), MNModel::COLLATERAL_OUT_INDEX).data(Qt::DisplayRole).toString();
    QString address = index.sibling(index.row(), MNModel::ADDRESS).data(Qt::DisplayRole).toString();
    QString qAliasString = index.data(Qt::DisplayRole).toString();
    std::string aliasToRemove = qAliasString.toStdString();

    const bool isDip3 = qAliasString.startsWith(QStringLiteral("dip3-"));
    const QString shortTx = txId.length() > 16 ? (txId.left(8) + "..." + txId.right(8)) : txId;
    const QString confirmMsg = isDip3
        ? tr("WARNING: You are about to remove a KTDIP3 Deterministic Masternode from this view:\n\n"
             "• Alias: %1\n"
             "• Service: %2\n"
             "• Collateral: %3:%4\n\n"
             "On-chain ProRegTx registrations cannot be deleted locally — to revoke on-chain, use 'protx revoke' in Debug Console. "
             "If you also unlock or spend the 4,200 KRISTA collateral UTXO, this Masternode will be permanently terminated.\n\n"
             "Do you want to continue?")
            .arg(qAliasString, address, shortTx, outIndex)
        : tr("WARNING: Deleting this Masternode cannot be undone!\n\n"
             "• Alias: %1\n"
             "• Service: %2\n"
             "• Collateral: %3:%4\n\n"
             "This will permanently remove the entry from masternode.conf, stop managing this node, and UNLOCK the 4,200 KRISTA collateral UTXO "
             "(spending it will deactivate the Masternode).\n\n"
             "Are you sure you want to delete this Masternode?")
            .arg(qAliasString, address, shortTx, outIndex);

    if (!showThemedConfirm(this, tr("Delete Masternode"), confirmMsg, tr("DELETE"), tr("CANCEL"), true))
        return;

    std::string strConfFile = "masternode.conf";
    std::string strDataDir = GetDataDir().string();
    fs::path conf_file_path(strConfFile);
    if (strConfFile != conf_file_path.filename().string()) {
        throw std::runtime_error(strprintf(_("masternode.conf %s resides outside data directory %s"), strConfFile, strDataDir));
    }

    fs::path pathBootstrap = GetDataDir() / strConfFile;
    if (fs::exists(pathBootstrap)) {
        fs::path pathMasternodeConfigFile = GetMasternodeConfigFile();
        fs::ifstream streamConfig(pathMasternodeConfigFile);

        if (!streamConfig.good()) {
            inform(tr("Invalid masternode.conf file"));
            return;
        }

        int lineNumToRemove = -1;
        int linenumber = 1;
        std::string lineCopy = "";
        for (std::string line; std::getline(streamConfig, line); linenumber++) {
            if (line.empty()) continue;

            std::istringstream iss(line);
            std::string comment, alias, ip, privKey, txHash, outputIndex;

            if (iss >> comment) {
                if (comment.at(0) == '#') continue;
                iss.str(line);
                iss.clear();
            }

            if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                iss.str(line);
                iss.clear();
                if (!(iss >> alias >> ip >> privKey >> txHash >> outputIndex)) {
                    streamConfig.close();
                    inform(tr("Error parsing masternode.conf file"));
                    return;
                }
            }

            if (aliasToRemove == alias) {
                lineNumToRemove = linenumber;
            } else
                lineCopy += line + "\n";

        }

        if (lineCopy.size() == 0) {
            const std::string defaultPortStr = std::to_string(Params().GetDefaultPort());
            lineCopy = "# Masternode config file\n"
                       "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
                       "# Example: mn1 127.0.0.2:" + defaultPortStr + " 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";
        }

        streamConfig.close();

        if (lineNumToRemove != -1) {
            fs::path pathConfigFile = AbsPathForConfigVal(fs::path("masternode_temp.conf"));
            FILE* configFile = fsbridge::fopen(pathConfigFile, "w");
            if (!configFile) {
                inform(tr("Unable to write temporary masternode_temp.conf file."));
                return;
            }
            fwrite(lineCopy.c_str(), lineCopy.size(), 1, configFile);
            fclose(configFile);

            fs::path pathOldConfFile = AbsPathForConfigVal(fs::path("old_masternode.conf"));
            if (fs::exists(pathOldConfFile)) {
                fs::remove(pathOldConfFile);
            }
            rename(pathMasternodeConfigFile, pathOldConfFile);

            fs::path pathNewConfFile = AbsPathForConfigVal(fs::path("masternode.conf"));
            rename(pathConfigFile, pathNewConfFile);

            // Unlock collateral
            bool convertOK = false;
            unsigned int indexOut = outIndex.toUInt(&convertOK);
            if (convertOK) {
                COutPoint collateralOut(uint256(txId.toStdString()), indexOut);
                walletModel->unlockCoin(collateralOut);
            }

            // Remove alias
            masternodeConfig.remove(aliasToRemove);
            // Update list
            QModelIndex sourceIdx = proxyModel ? proxyModel->mapToSource(index) : index;
            mnModel->removeMn(sourceIdx);
            updateListState();
        }
    } else {
        inform(tr("masternode.conf file doesn't exists"));
    }
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
            mnModel->addMn(dialog->mnEntry);
            updateListState();
            if (!dialog->createdActiveMnLine.isEmpty()) {
                DefaultDialog* copyDlg = new DefaultDialog(window);
                QString detailsText = dialog->createdMasternodeConfLine.isEmpty()
                    ? tr("%1\n\nactivemasternode.conf:\n%2")
                        .arg(dialog->returnStr, dialog->createdActiveMnLine)
                    : tr("%1\n\nNode config:\n%2\n\nDebug Console command:\n%3")
                        .arg(dialog->returnStr, dialog->createdActiveMnLine, dialog->createdMasternodeConfLine);
                copyDlg->setText(
                    tr("Masternode Created"),
                    detailsText,
                    tr("COPY LINE"),
                    tr("CLOSE")
                );
                if (openDialogWithOpaqueBackgroundY(copyDlg, window, 5, 7)) {
                    GUIUtil::setClipboard(dialog->createdActiveMnLine);
                    inform(tr("Copied activemasternode.conf line to clipboard."));
                } else {
                    inform(dialog->returnStr);
                }
                copyDlg->deleteLater();
            } else {
                inform(dialog->returnStr);
            }
        } else {
            warn(tr("Error creating masternode"), dialog->returnStr);
        }
    }
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

    css += QString(
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 15px; padding: 0 13px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:hover { border-color: #7695CD; background-color: %4; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:checked { background-color: %4; color: %5; border: 1px solid #7695CD; }"
        "QWidget#mnPageRoot QPushButton[mnScopePill=\"true\"]:checked:hover { background-color: %6; border-color: %5; }"
    ).arg(secBg, secFg, secBrd, pillActBg, pillActFg, pillActHov);

    css += QString(
        "QWidget#mnPageRoot QLineEdit[mnInput=\"true\"] { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 12px; }"
        "QWidget#mnPageRoot QLineEdit[mnInput=\"true\"]:focus { border-color: #7695CD; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"] { combobox-popup: 0; background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 4px 26px 4px 10px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]:hover { border-color: #7695CD; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 24px; border: none; background: transparent; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"]::down-arrow { image: url(\"%4\"); width: 12px; height: 12px; }"
        "QWidget#mnPageRoot QComboBox[mnCombo=\"true\"] QAbstractItemView { background-color: %5; color: %2; border: 1px solid %3; selection-background-color: #5576B2; selection-color: #FFFFFF; outline: none; }"
    ).arg(inputBg, titleCol, inputBrd, dropArrow, cardBg);

    css += QString(
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 13px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:hover { background-color: %4; border-color: #7695CD; }"
        "QWidget#mnPageRoot QPushButton[mnSecondary=\"true\"]:pressed { background-color: %5; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"] { background-color: %6; color: #FFFFFF; border: 1px solid %6; border-radius: 7px; padding: 0 16px; font-size: 12px; font-weight: 600; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:hover { background-color: %7; border-color: %7; }"
        "QWidget#mnPageRoot QPushButton[mnPrimary=\"true\"]:pressed { background-color: %8; border-color: %8; }"
    ).arg(secBg, secFg, secBrd, secHov, secPres, priBg, priHov, priPres);

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
