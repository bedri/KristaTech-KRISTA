// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsinformationwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsinformationwidget.h"

#include "clientmodel.h"
#include "chainparams.h"
#include "db.h"
#include "util.h"
#include "guiutil.h"
#include "qt/kristatech/qtutils.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QHBoxLayout>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

SettingsInformationWidget::SettingsInformationWidget(KRISTATECHGUI* _window,QWidget *parent) :
    PWidget(_window,parent),
    ui(new Ui::SettingsInformationWidget)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    // TODO: Mempool section is not currently implemented and instead, hidden for now
    ui->labelTitleMemory->setVisible(false);
    ui->labelTitleNumberTransactions->setVisible(false);
    ui->labelInfoNumberTransactions->setText("0");
    ui->labelInfoNumberTransactions->setVisible(false);

    // Initial Network & Blockchain values
    ui->labelInfoName->setText(tr("Main"));
    ui->labelInfoConnections->setText("0 (In: 0 / Out: 0)");
    ui->labelInfoMasternodes->setText("Total: 0 (IPv4: 0 / IPv6: 0 / Tor: 0 / Unknown: 0)");
    ui->labelInfoBlockNumber->setText("0");
    ui->labelInfoBlockTime->setText(tr("Syncing..."));

#ifdef ENABLE_WALLET
    ui->labelInfoBerkeley->setText(DbEnv::version(0, 0, 0));
    ui->labelInfoDataDir->setText(QString::fromStdString(GetDataDir().string() + QDir::separator().toLatin1() + GetArg("-wallet", DEFAULT_WALLET_DAT)));
#else
    ui->labelInfoBerkeley->setText(tr("No information"));
#endif

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Node Information"),
        tr("Live client build details, peer connectivity, and active blockchain tip."),
        scrollLayout,
        headerRight,
        rootLayout);

    for (QPushButton* hdrBtn : {ui->pushButtonNetworkMonitor, ui->pushButtonBackups, ui->pushButtonFile}) {
        clearLegacySettingsConstraints(hdrBtn);
        hdrBtn->setParent(modernRoot);
        hdrBtn->setFixedHeight(34);
        hdrBtn->setCursor(Qt::PointingHandCursor);
        hdrBtn->setFocusPolicy(Qt::NoFocus);
        hdrBtn->setProperty("settingsSecondary", true);
        headerRight->addWidget(hdrBtn);
    }

    // Card 1: General
    QVBoxLayout* genBody = nullptr;
    QFrame* cardGen = createSettingsCard(modernRoot, tr("General"), genBody);
    addSettingsInfoRow(cardGen, genBody, tr("Client Version"), ui->labelInfoClient, false, false);
    addSettingsInfoRow(cardGen, genBody, tr("User Agent"), ui->labelInfoAgent, false, true);
    addSettingsInfoRow(cardGen, genBody, tr("BerkeleyDB Version"), ui->labelInfoBerkeley, false, true);
    ui->labelInfoDataDir->setWordWrap(true);
    addSettingsInfoRow(cardGen, genBody, tr("Wallet File"), ui->labelInfoDataDir, false, true);
    addSettingsInfoRow(cardGen, genBody, tr("Startup Time"), ui->labelInfoTime, false, true);
    scrollLayout->addWidget(cardGen);

    // Card 2: Network
    QVBoxLayout* netBody = nullptr;
    QFrame* cardNet = createSettingsCard(modernRoot, tr("Network"), netBody);
    addSettingsInfoRow(cardNet, netBody, tr("Network Name"), ui->labelInfoName, false, false);
    addSettingsInfoRow(cardNet, netBody, tr("Connections"), ui->labelInfoConnections, false, true);
    addSettingsInfoRow(cardNet, netBody, tr("Active Masternodes"), ui->labelInfoMasternodes, false, true);
    scrollLayout->addWidget(cardNet);

    // Card 3: Blockchain
    QVBoxLayout* chainBody = nullptr;
    QFrame* cardChain = createSettingsCard(modernRoot, tr("Blockchain"), chainBody);
    addSettingsInfoRow(cardChain, chainBody, tr("Block Height"), ui->labelInfoBlockNumber, false, false);
    addSettingsInfoRow(cardChain, chainBody, tr("Last Block Time"), ui->labelInfoBlockTime, false, true);

    ui->labelInfoBlockHash->setWordWrap(true);
    QPushButton* btnCopyHash = new QPushButton(tr("Copy"), cardChain);
    btnCopyHash->setProperty("settingsMiniCopy", true);
    btnCopyHash->setFixedHeight(26);
    btnCopyHash->setCursor(Qt::PointingHandCursor);
    btnCopyHash->setFocusPolicy(Qt::NoFocus);
    btnCopyHash->setToolTip(tr("Copy last block hash to clipboard"));
    connect(btnCopyHash, &QPushButton::clicked, this, [this]() {
        if (QApplication::clipboard() && ui->labelInfoBlockHash) {
            QApplication::clipboard()->setText(ui->labelInfoBlockHash->text());
            inform(tr("Block hash copied to clipboard"));
        }
    });
    addSettingsInfoRow(cardChain, chainBody, tr("Last Block Hash"), ui->labelInfoBlockHash, true, true, btnCopyHash);
    scrollLayout->addWidget(cardChain);

    scrollLayout->addStretch(1);

    connect(ui->pushButtonBackups, &QPushButton::clicked, [this](){
        if (!GUIUtil::showBackups())
            inform(tr("Unable to open backups folder"));
    });
    connect(ui->pushButtonFile, &QPushButton::clicked, [this](){
        if (!GUIUtil::openConfigfile())
            inform(tr("Unable to open kristatech.conf with default application"));
    });
    connect(ui->pushButtonNetworkMonitor, &QPushButton::clicked, this, &SettingsInformationWidget::openNetworkMonitor);

    applyTheme(isLightTheme());
}


void SettingsInformationWidget::loadClientModel()
{
    if (clientModel && clientModel->getPeerTableModel() && clientModel->getBanTableModel()) {
        // Provide initial values
        ui->labelInfoClient->setText(clientModel->formatFullVersion());
        ui->labelInfoAgent->setText(clientModel->clientName());
        ui->labelInfoTime->setText(clientModel->formatClientStartupTime());
        ui->labelInfoName->setText(QString::fromStdString(Params().NetworkIDString()));

        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, &ClientModel::numConnectionsChanged, this, &SettingsInformationWidget::setNumConnections);

        setNumBlocks(clientModel->getNumBlocks());
        connect(clientModel, &ClientModel::numBlocksChanged, this, &SettingsInformationWidget::setNumBlocks);

        connect(clientModel, &ClientModel::strMasternodesChanged, this, &SettingsInformationWidget::setMasternodeCount);
        setMasternodeCount(clientModel->getMasternodeCountString());
    }
}

void SettingsInformationWidget::setNumConnections(int count)
{
    if (!clientModel)
        return;

    QString connections = QString::number(count) + " (";
    connections += tr("In:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_IN)) + " / ";
    connections += tr("Out:") + " " + QString::number(clientModel->getNumConnections(CONNECTIONS_OUT)) + ")";

    ui->labelInfoConnections->setText(connections);
}

void SettingsInformationWidget::setNumBlocks(int count)
{
    ui->labelInfoBlockNumber->setText(QLocale().toString(count));
    if (clientModel) {
        ui->labelInfoBlockTime->setText(clientModel->getLastBlockDate().toString());
        ui->labelInfoBlockHash->setText(clientModel->getLastBlockHash());
    }
}

void SettingsInformationWidget::setMasternodeCount(const QString& strMasternodes)
{
    ui->labelInfoMasternodes->setText(strMasternodes);
}

void SettingsInformationWidget::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsInformationWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsInformationWidget::openNetworkMonitor()
{
    if (!rpcConsole) {
        rpcConsole = new RPCConsole(0);
        rpcConsole->setClientModel(clientModel);
    }
    rpcConsole->showNetwork();
}

void SettingsInformationWidget::showPeers()
{
    if (!rpcConsole) {
        rpcConsole = new RPCConsole(0);
        rpcConsole->setClientModel(clientModel);
    }
    rpcConsole->showPeers();
}

void SettingsInformationWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (clientModel) {
        setMasternodeCount(clientModel->getMasternodeCountString());
        clientModel->startMasternodesTimer();
    }
}

void SettingsInformationWidget::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    if (clientModel) {
        clientModel->stopMasternodesTimer();
    }
}

SettingsInformationWidget::~SettingsInformationWidget()
{
    delete ui;
}
