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
#include "fs.h"
#include "guiutil.h"
#include "init.h"
#include "masternode-sync.h"
#include "masternodeconfig.h"
#include "masternodeman.h"
#include "sync.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"
#include "util.h"
#include "qt/kristatech/optionbutton.h"
#include <QComboBox>
#include <QLineEdit>
#include <QSortFilterProxyModel>
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

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
    {
        QAbstractItemModel* src = sourceModel();
        if (!src) return true;

        const QString alias  = src->index(sourceRow, MNModel::ALIAS, sourceParent).data(Qt::DisplayRole).toString();
        const QString addr   = src->index(sourceRow, MNModel::ADDRESS, sourceParent).data(Qt::DisplayRole).toString();
        const QString txId   = src->index(sourceRow, MNModel::COLLATERAL_ID, sourceParent).data(Qt::DisplayRole).toString();
        const QString status = src->index(sourceRow, MNModel::STATUS, sourceParent).data(Qt::DisplayRole).toString();

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
        row->updateView(address, label, status, wasCollateralAccepted, txId);
    }

    QColor rectColor(bool isHovered, bool isSelected) override
    {
        return getRowColor(isLightTheme, isHovered, isSelected);
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

    this->setStyleSheet(parent->styleSheet());

    /* Containers */
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0,20,0,20);

    /* Light Font */
    QFont fontLight;
    fontLight.setWeight(QFont::Light);

    /* Title */
    setCssTitleScreen(ui->labelTitle);
    ui->labelTitle->setFont(fontLight);
    setCssSubtitleScreen(ui->labelSubtitle1);
    ui->labelSubtitle1->setText(tr("Manage 4,200 KRISTA collateralized Masternodes (35% Passive Pool + 20% Validator Track + 10% LLMQ Quorum).\n"
                                   "Rule 8 Single-IP Multi-MN (KTDIP3): Remote servers load '<alias> <privkey> <bls_privkey_hex>' from activemasternode.conf — never put masternodeprivkey in kristatech.conf."));

    // Quick-access chip buttons for activemasternode.conf & masternode.conf right in the header
    QWidget* confActionsBar = new QWidget(this);
    confActionsBar->setObjectName("confActionsBar");
    confActionsBar->setStyleSheet("QWidget#confActionsBar { background-color: transparent; border: none; }");
    QHBoxLayout* confActionsLayout = new QHBoxLayout(confActionsBar);
    confActionsLayout->setContentsMargins(0, 6, 20, 0);
    confActionsLayout->setSpacing(8);

    QPushButton* btnOpenActiveMNConf = new QPushButton(tr("Open activemasternode.conf (Hot)"), confActionsBar);
    btnOpenActiveMNConf->setCursor(Qt::PointingHandCursor);
    btnOpenActiveMNConf->setFocusPolicy(Qt::NoFocus);
    btnOpenActiveMNConf->setFixedHeight(32);
    btnOpenActiveMNConf->setToolTip(tr("Open or initialize activemasternode.conf for single-IP multi-Masternode operation"));
    setCssProperty(btnOpenActiveMNConf, "btn-chip");
    connect(btnOpenActiveMNConf, &QPushButton::clicked, [this]() {
        if (!GUIUtil::openActiveMNConfigfile()) {
            inform(tr("Unable to open activemasternode.conf."));
        }
    });

    QPushButton* btnOpenControllerMNConf = new QPushButton(tr("Open masternode.conf (Controller)"), confActionsBar);
    btnOpenControllerMNConf->setCursor(Qt::PointingHandCursor);
    btnOpenControllerMNConf->setFocusPolicy(Qt::NoFocus);
    btnOpenControllerMNConf->setFixedHeight(32);
    btnOpenControllerMNConf->setToolTip(tr("Open cold controller masternode.conf file"));
    setCssProperty(btnOpenControllerMNConf, "btn-chip");
    connect(btnOpenControllerMNConf, &QPushButton::clicked, [this]() {
        if (!GUIUtil::openMNConfigfile()) {
            inform(tr("masternode.conf does not exist yet — create a Masternode first or open the Data Directory."));
        }
    });

    confActionsLayout->addWidget(btnOpenActiveMNConf);
    confActionsLayout->addWidget(btnOpenControllerMNConf);

    QPushButton* btnRefreshMNs = new QPushButton(tr("Refresh"), confActionsBar);
    btnRefreshMNs->setCursor(Qt::PointingHandCursor);
    btnRefreshMNs->setFocusPolicy(Qt::NoFocus);
    btnRefreshMNs->setFixedHeight(32);
    btnRefreshMNs->setToolTip(tr("Refresh Masternode list and on-chain status now"));
    setCssProperty(btnRefreshMNs, "btn-chip");
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
    confActionsLayout->addWidget(btnRefreshMNs);
    confActionsLayout->addStretch();
    ui->verticalLayout_8->addWidget(confActionsBar);

    // Compact Live Summary Chips + Instant Search & Status Filter Bar
    QWidget* mnToolbar = new QWidget(this);
    mnToolbar->setObjectName("mnToolbar");
    mnToolbar->setStyleSheet("QWidget#mnToolbar { background-color: transparent; border: none; }");
    QHBoxLayout* mnToolbarLayout = new QHBoxLayout(mnToolbar);
    mnToolbarLayout->setContentsMargins(0, 8, 20, 4);
    mnToolbarLayout->setSpacing(8);

    lblSummaryTotal = new QLabel(tr("0 Total"), mnToolbar);
    lblSummaryEnabled = new QLabel(tr("0 Enabled"), mnToolbar);
    lblSummaryInactive = new QLabel(tr("0 Inactive"), mnToolbar);
    lblSummaryLocked = new QLabel(tr("Locked: 0 KRISTA"), mnToolbar);

    lineEditSearchMN = new QLineEdit(mnToolbar);
    lineEditSearchMN->setPlaceholderText(tr("Search alias, IP, or TxID..."));
    lineEditSearchMN->setClearButtonEnabled(true);
    lineEditSearchMN->setMinimumWidth(210);
    lineEditSearchMN->setMaximumWidth(280);
    lineEditSearchMN->setFixedHeight(32);
    setCssEditLine(lineEditSearchMN, true);
    styleLineEditClearButton(lineEditSearchMN);

    comboStatusFilter = new QComboBox(mnToolbar);
    comboStatusFilter->addItem(tr("All Status"));
    comboStatusFilter->addItem(tr("Enabled Only"));
    comboStatusFilter->addItem(tr("Inactive / Missing"));
    comboStatusFilter->setFixedHeight(32);
    comboStatusFilter->setMinimumWidth(145);
    initComboBox(comboStatusFilter);

    connect(lineEditSearchMN, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (proxyModel) proxyModel->setSearchText(text);
        updateListState();
    });
    connect(comboStatusFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int idx) {
        if (proxyModel) proxyModel->setStatusFilter(idx);
        updateListState();
    });

    mnToolbarLayout->addWidget(lblSummaryTotal);
    mnToolbarLayout->addWidget(lblSummaryEnabled);
    mnToolbarLayout->addWidget(lblSummaryInactive);
    mnToolbarLayout->addWidget(lblSummaryLocked);
    mnToolbarLayout->addStretch();
    mnToolbarLayout->addWidget(lineEditSearchMN);
    mnToolbarLayout->addWidget(comboStatusFilter);
    ui->verticalLayout_8->addWidget(mnToolbar);
    applySummaryChipStyles();

    /* Columns Headers */
    setCssProperty(ui->labelHeaderAlias, "text-title-topbar");
    setCssProperty(ui->labelHeaderAddress, "text-title-topbar");
    setCssProperty(ui->labelHeaderTxId, "text-title-topbar");
    setCssProperty(ui->labelHeaderStatus, "text-title-topbar");
    setCssProperty(ui->labelHeaderActions, "text-title-topbar");

    /* Buttons */
    setCssBtnPrimary(ui->pushButtonSave);
    setCssBtnPrimary(ui->pushButtonStartAll);
    setCssBtnPrimary(ui->pushButtonStartMissing);

    setCssProperty(ui->listMn, "container");
    ui->listMn->setItemDelegate(delegate);
    ui->listMn->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
    ui->listMn->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listMn->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->listMn->setSelectionBehavior(QAbstractItemView::SelectRows);

    ui->emptyContainer->setVisible(false);
    setCssProperty(ui->pushImgEmpty, "img-empty-master");
    setCssProperty(ui->labelEmpty, "text-empty");

    connect(ui->pushButtonSave, &QPushButton::clicked, this, &MasterNodesWidget::onCreateMNClicked);
    connect(ui->pushButtonStartAll, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_ALL);
    });
    connect(ui->pushButtonStartMissing, &QPushButton::clicked, [this]() {
        onStartAllClicked(REQUEST_START_MISSING);
    });
    connect(ui->listMn, &QListView::clicked, this, &MasterNodesWidget::onMNClicked);
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

void MasterNodesWidget::updateSummaryChips()
{
    if (!mnModel || !lblSummaryTotal || !lblSummaryEnabled || !lblSummaryInactive || !lblSummaryLocked)
        return;

    const int total = mnModel->rowCount();
    int enabled = 0;
    for (int r = 0; r < total; ++r) {
        const QString status = mnModel->index(r, MNModel::STATUS, QModelIndex()).data(Qt::DisplayRole).toString();
        if (status == "ENABLED" || status == "PRE_ENABLED") {
            enabled++;
        }
    }
    const int inactive = total - enabled;
    const int curHeight = []() {
        LOCK(cs_main);
        return chainActive.Height();
    }();
    const CAmount perMnCollateral = CMasternode::GetMasternodeNodeCollateral(curHeight);
    const CAmount totalLocked = static_cast<CAmount>(total) * perMnCollateral;

    lblSummaryTotal->setText(tr("%1 Total").arg(total));
    lblSummaryEnabled->setText(tr("%1 Enabled").arg(enabled));
    lblSummaryInactive->setText(tr("%1 Inactive").arg(inactive));
    lblSummaryLocked->setText(tr("Locked: %1 %2")
        .arg(QString::number(totalLocked / COIN))
        .arg(QString::fromStdString(CURRENCY_UNIT)));
    lblSummaryInactive->setVisible(inactive > 0);
}

void MasterNodesWidget::applySummaryChipStyles()
{
    const bool light = isLightTheme();
    const QString baseChip = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 10px; padding: 4px 10px; font-size: 12px; font-weight: 600; }"
    );
    if (lblSummaryTotal) {
        lblSummaryTotal->setStyleSheet(baseChip.arg(
            light ? "rgba(118, 149, 205, 0.14)" : "rgba(118, 149, 205, 0.22)",
            light ? "#3E5B94" : "#E2E8F0",
            "#7695CD"));
    }
    if (lblSummaryEnabled) {
        lblSummaryEnabled->setStyleSheet(baseChip.arg(
            "rgba(16, 185, 129, 0.14)",
            light ? "#047857" : "#34D399",
            "rgba(16, 185, 129, 0.45)"));
    }
    if (lblSummaryInactive) {
        lblSummaryInactive->setStyleSheet(baseChip.arg(
            "rgba(245, 158, 11, 0.14)",
            light ? "#B45309" : "#FBBF24",
            "rgba(245, 158, 11, 0.45)"));
    }
    if (lblSummaryLocked) {
        lblSummaryLocked->setStyleSheet(baseChip.arg(
            light ? "rgba(100, 116, 139, 0.10)" : "rgba(148, 163, 184, 0.16)",
            light ? "#334155" : "#CBD5E1",
            light ? "rgba(100, 116, 139, 0.30)" : "rgba(148, 163, 184, 0.35)"));
    }
}

void MasterNodesWidget::updateListState()
{
    updateSummaryChips();
    const int totalRows = mnModel ? mnModel->rowCount() : 0;
    const bool hasAny = totalRows > 0;
    ui->listMn->setVisible(hasAny);
    ui->headerFrame->setVisible(hasAny);
    ui->emptyContainer->setVisible(!hasAny);
    ui->pushButtonStartAll->setVisible(hasAny);
    ui->pushButtonStartMissing->setVisible(hasAny);
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

    QPushButton* btnInfo = rowWidget->findChild<QPushButton*>("pushButtonInfo");
    QPushButton* btnStart = rowWidget->findChild<QPushButton*>("pushButtonStart");
    QPushButton* btnDelete = rowWidget->findChild<QPushButton*>("pushButtonDelete");

    if (btnInfo && btnInfo->geometry().contains(localClickPos)) {
        onInfoMNClicked();
    } else if (btnStart && btnStart->geometry().contains(localClickPos)) {
        onEditMNClicked();
    } else if (btnDelete && btnDelete->geometry().contains(localClickPos)) {
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
            QString exportedMN = cleanAlias + " " + mnPrivKey + "\n";
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
                copyDlg->setText(
                    tr("Masternode Created"),
                    tr("%1\n\nactivemasternode.conf:\n%2")
                        .arg(dialog->returnStr)
                        .arg(dialog->createdActiveMnLine),
                    tr("COPY LINE"),
                    tr("CLOSE")
                );
                copyDlg->adjustSize();
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

void MasterNodesWidget::changeTheme(bool isLightTheme, QString& theme)
{
    static_cast<MNHolder*>(this->delegate->getRowFactory())->isLightTheme = isLightTheme;
    applySummaryChipStyles();
    if (lineEditSearchMN) {
        styleLineEditClearButton(lineEditSearchMN);
    }
}

MasterNodesWidget::~MasterNodesWidget()
{
    delete ui;
}
