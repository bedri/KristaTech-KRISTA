// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/kristatech-config.h"
#endif

#include "qt/kristatech/settings/settingswalletoptionswidget.h"
#include "qt/kristatech/settings/forms/ui_settingswalletoptionswidget.h"
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QListView>
#include <QShowEvent>
#include <QVBoxLayout>
#include "optionsmodel.h"
#include "clientmodel.h"
#include "wallet/wallet.h"
#include "qt/kristatech/qtutils.h"

SettingsWalletOptionsWidget::SettingsWalletOptionsWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsWalletOptionsWidget)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Wallet & Network"),
        tr("Manage staking split thresholds, change spending, and P2P network / proxy settings."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Card 1: Wallet & Staking
    QVBoxLayout* walletBody = nullptr;
    QFrame* cardWallet = createSettingsCard(modernRoot, tr("Wallet & Staking"), walletBody);

    clearLegacySettingsConstraints(ui->spinBoxStakeSplitThreshold);
    ui->spinBoxStakeSplitThreshold->setFixedSize(235, 36);
    ui->spinBoxStakeSplitThreshold->setSuffix(QStringLiteral(" KRISTA"));
    ui->spinBoxStakeSplitThreshold->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->spinBoxStakeSplitThreshold->setProperty("settingsInput", true);
    addSettingsControlRow(
        cardWallet,
        walletBody,
        tr("Stake Split Threshold"),
        tr("Minimum UTXO size in KRISTA before coinstake splits outputs (set to 0 to disable splitting)."),
        ui->spinBoxStakeSplitThreshold,
        false);

    addSettingsCheckRow(
        cardWallet,
        walletBody,
        ui->radioButtonSpend,
        tr("Spend unconfirmed change"),
        tr("Allow spending unconfirmed change outputs created by your own recent transactions."),
        true);

    scrollLayout->addWidget(cardWallet);

    // Card 2: Network & Proxy
    QVBoxLayout* netBody = nullptr;
    QFrame* cardNet = createSettingsCard(modernRoot, tr("Network & Proxy"), netBody);

    addSettingsCheckRow(
        cardNet,
        netBody,
        ui->checkBoxMap,
        tr("Map port using UPnP"),
#ifdef USE_UPNP
        tr("Automatically open the P2P port on your router using UPnP."),
#else
        tr("UPnP port mapping is disabled in this build."),
#endif
        false);
#ifndef USE_UPNP
    ui->checkBoxMap->setEnabled(false);
#endif

    addSettingsCheckRow(
        cardNet,
        netBody,
        ui->checkBoxAllow,
        tr("Allow incoming connections"),
        tr("Accept inbound peer connections to help strengthen the KristaTech network."),
        true);

    addSettingsCheckRow(
        cardNet,
        netBody,
        ui->checkBoxConnect,
        tr("Connect through SOCKS5 proxy (default proxy)"),
        tr("Route outbound node traffic through a local or remote SOCKS5 proxy (e.g., Tor)."),
        true);

    QWidget* proxyFieldsRow = new QWidget(cardNet);
    QHBoxLayout* proxyLayout = new QHBoxLayout(proxyFieldsRow);
    proxyLayout->setContentsMargins(28, 0, 0, 12);
    proxyLayout->setSpacing(14);

    QVBoxLayout* ipCol = new QVBoxLayout();
    ipCol->setContentsMargins(0, 0, 0, 0);
    ipCol->setSpacing(4);
    QLabel* lblProxyIp = new QLabel(tr("Proxy IP"), proxyFieldsRow);
    lblProxyIp->setProperty("settingsRowDesc", true);
    clearLegacySettingsConstraints(ui->lineEditProxy);
    ui->lineEditProxy->setParent(proxyFieldsRow);
    ui->lineEditProxy->setFixedHeight(36);
    ui->lineEditProxy->setPlaceholderText(QStringLiteral("127.0.0.1"));
    ui->lineEditProxy->setProperty("settingsInput", true);
    ipCol->addWidget(lblProxyIp);
    ipCol->addWidget(ui->lineEditProxy);

    QVBoxLayout* portCol = new QVBoxLayout();
    portCol->setContentsMargins(0, 0, 0, 0);
    portCol->setSpacing(4);
    QLabel* lblProxyPort = new QLabel(tr("Port"), proxyFieldsRow);
    lblProxyPort->setProperty("settingsRowDesc", true);
    clearLegacySettingsConstraints(ui->lineEditPort);
    ui->lineEditPort->setParent(proxyFieldsRow);
    ui->lineEditPort->setFixedSize(140, 36);
    ui->lineEditPort->setPlaceholderText(QStringLiteral("9050"));
    ui->lineEditPort->setValidator(new QIntValidator(1, 65535, this));
    ui->lineEditPort->setProperty("settingsInput", true);
    portCol->addWidget(lblProxyPort);
    portCol->addWidget(ui->lineEditPort);

    proxyLayout->addLayout(ipCol, 1);
    proxyLayout->addLayout(portCol, 0);
    netBody->addWidget(proxyFieldsRow);

    scrollLayout->addWidget(cardNet);
    scrollLayout->addStretch(1);

    buildSettingsOptionsFooter(modernRoot, rootLayout, ui->pushButtonReset, ui->pushButtonClean, ui->pushButtonSave);

    connect(ui->checkBoxConnect, &QCheckBox::toggled, this, &SettingsWalletOptionsWidget::updateProxyFieldsEnabled);
    updateProxyFieldsEnabled(ui->checkBoxConnect->isChecked());

    connect(ui->pushButtonSave, &QPushButton::clicked, [this] { Q_EMIT saveSettings(); });
    connect(ui->pushButtonReset, &QPushButton::clicked, this, &SettingsWalletOptionsWidget::onResetClicked);
    connect(ui->pushButtonClean, &QPushButton::clicked, [this] { Q_EMIT discardSettings(); });

    applyTheme(isLightTheme());
}

void SettingsWalletOptionsWidget::updateProxyFieldsEnabled(bool enabled)
{
    if (ui->lineEditProxy) ui->lineEditProxy->setEnabled(enabled);
    if (ui->lineEditPort) ui->lineEditPort->setEnabled(enabled);
}

void SettingsWalletOptionsWidget::showEvent(QShowEvent* event)
{
    PWidget::showEvent(event);
    if (ui->checkBoxConnect) {
        updateProxyFieldsEnabled(ui->checkBoxConnect->isChecked());
    }
}

void SettingsWalletOptionsWidget::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsWalletOptionsWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsWalletOptionsWidget::onResetClicked(){
    if (clientModel && clientModel->getOptionsModel()) {
        if (!ask(tr("Reset Wallet & Network Options"), tr("Reset all Wallet & Network preferences back to default values?"))) {
            return;
        }
        OptionsModel *optionsModel = clientModel->getOptionsModel();
        QSettings settings;
        optionsModel->setWalletDefaultOptions(settings, true);
        optionsModel->setNetworkDefaultOptions(settings, true);
        if (pwalletMain) {
            pwalletMain->MarkDirty();
        }
        updateProxyFieldsEnabled(ui->checkBoxConnect && ui->checkBoxConnect->isChecked());
        inform(tr("Options reset succeed"));
    }
}

void SettingsWalletOptionsWidget::setMapper(QDataWidgetMapper *mapper){
    mapper->addMapping(ui->radioButtonSpend, OptionsModel::SpendZeroConfChange);
    mapper->addMapping(ui->spinBoxStakeSplitThreshold, OptionsModel::StakeSplitThreshold);

    // Network
    mapper->addMapping(ui->checkBoxMap, OptionsModel::MapPortUPnP);
    mapper->addMapping(ui->checkBoxAllow, OptionsModel::Listen);
    mapper->addMapping(ui->checkBoxConnect, OptionsModel::ProxyUse);
    mapper->addMapping(ui->lineEditProxy, OptionsModel::ProxyIP);
    mapper->addMapping(ui->lineEditPort, OptionsModel::ProxyPort);
}

void SettingsWalletOptionsWidget::setSpinBoxStakeSplitThreshold(double val)
{
    ui->spinBoxStakeSplitThreshold->setValue(val);
}

SettingsWalletOptionsWidget::~SettingsWalletOptionsWidget(){
    delete ui;
}
