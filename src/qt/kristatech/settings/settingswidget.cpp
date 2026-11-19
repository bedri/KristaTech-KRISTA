// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingswidget.h"
#include "qt/kristatech/settings/settingsaboutwidget.h"
#include "qt/kristatech/settings/settingsfaqwidget.h"
#include "qt/kristatech/settings/forms/ui_settingswidget.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/defaultdialog.h"
#include "optionsmodel.h"
#include "clientmodel.h"
#include "utilitydialog.h"
#include "wallet/wallet.h"
#include <QScrollBar>
#include <QDataWidgetMapper>
#include <QDesktopServices>
#include <QUrl>

SettingsWidget::SettingsWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::SettingsWidget)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    /* Containers */
    ui->scrollArea->setFrameShape(QFrame::NoFrame);
    setCssProperty(ui->scrollArea, "container");
    if (ui->scrollArea->viewport()) {
        ui->scrollArea->viewport()->setAttribute(Qt::WA_StyledBackground, true);
        setCssProperty(ui->scrollArea->viewport(), "container");
    }
    ui->scrollAreaWidgetContents->setAttribute(Qt::WA_StyledBackground, true);
    setCssProperty(ui->scrollAreaWidgetContents, "container");
    ui->left->setAttribute(Qt::WA_StyledBackground, true);
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0, 16, 0, 16);
    ui->left->setMinimumWidth(248);
    ui->left->setMaximumWidth(264);
    if (ui->horizontalLayout) {
        ui->horizontalLayout->setStretch(0, 0);
        ui->horizontalLayout->setStretch(1, 1);
    }
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(24, 14, 24, 20);

    ui->verticalLayout->setAlignment(Qt::AlignTop);

    /* Title */
    setCssProperty(ui->labelTitle, "text-title-screen");

    // Lift legacy height caps so all flat grouped sections display without clipping
    ui->scrollAreaWidgetContents->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->groupBox->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->menuFileContainer->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->fileButtonsWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->menuToolsContainer->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->configurationButtonsWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->optionsButtonsWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->toolsButtonsWidget->setMaximumHeight(QWIDGETSIZE_MAX);
    ui->helpButtonsWidget->setMaximumHeight(QWIDGETSIZE_MAX);

    // Flat Section Headers (non-collapsing, scannable at a glance)
    QList<QPushButton*> sectionHeaders = {
        ui->pushButtonFile,
        ui->pushButtonConfiguration,
        ui->pushButtonOptions,
        ui->pushButtonTools,
        ui->pushButtonHelp
    };
    ui->pushButtonFile->setText(tr("WALLET && SECURITY"));
    ui->pushButtonConfiguration->setText(tr("CRYPTOGRAPHY TOOLS"));
    ui->pushButtonOptions->setText(tr("PREFERENCES"));
    ui->pushButtonTools->setText(tr("DIAGNOSTICS && REPAIR"));
    ui->pushButtonHelp->setText(tr("HELP && ECOSYSTEM"));

    for (QPushButton* hdr : sectionHeaders) {
        hdr->setCheckable(false);
        hdr->setEnabled(false);
        hdr->setMinimumHeight(30);
        hdr->setMaximumHeight(30);
        setCssProperty(hdr, "label-settings-group");
    }

    // Rename sub-items for crystal-clear UX
    ui->pushButtonFile2->setText(tr("Backup && Passphrase"));
    ui->pushButtonExportCsv->setText(tr("Export History (CSV)"));
    ui->pushButtonConfiguration4->setText(tr("Sign / Verify Message"));
    ui->pushButtonConfiguration3->setText(tr("BIP38 Key Tool"));
    ui->pushButtonOptions1->setText(tr("Main Options"));
    ui->pushButtonOptions2->setText(tr("Wallet && Network"));
    ui->pushButtonOptions5->setText(tr("Display && Units"));
    ui->pushButtonTools1->setText(tr("Node Information"));
    ui->pushButtonTools2->setText(tr("RPC Debug Console"));
    ui->pushButtonTools5->setText(tr("Wallet Repair"));
    ui->pushButtonHelp2->setText(tr("About KristaTech"));
    ui->pushButtonHelp3->setText(tr("Official Website"));
    ui->pushButtonHelp4->setText(tr("Block Explorer"));

    // Add FAQ / Protocol Guide button inside Help & Ecosystem
    btnFaqGuide = new QPushButton(tr("FAQ && Protocol Guide"), ui->helpButtonsWidget);
    btnFaqGuide->setMinimumHeight(38);
    btnFaqGuide->setMaximumHeight(38);
    btnFaqGuide->setFocusPolicy(Qt::NoFocus);
    btnFaqGuide->setCursor(Qt::PointingHandCursor);
    btnFaqGuide->setCheckable(true);
    setCssProperty(btnFaqGuide, "btn-settings-options");
    if (ui->verticalHelp) {
        ui->verticalHelp->insertWidget(0, btnFaqGuide);
    }
    connect(btnFaqGuide, &QPushButton::clicked, this, &SettingsWidget::onFaqClicked);

    QList<QPushButton*> allSubButtons = {
        ui->pushButtonFile2,
        ui->pushButtonExportCsv,
        ui->pushButtonConfiguration4,
        ui->pushButtonConfiguration3,
        ui->pushButtonOptions1,
        ui->pushButtonOptions2,
        ui->pushButtonOptions5,
        ui->pushButtonTools1,
        ui->pushButtonTools2,
        ui->pushButtonTools5,
        ui->pushButtonHelp2,
        ui->pushButtonHelp3,
        ui->pushButtonHelp4
    };
    for (QPushButton* subBtn : allSubButtons) {
        subBtn->setMinimumHeight(38);
        subBtn->setMaximumHeight(38);
        subBtn->setCursor(Qt::PointingHandCursor);
        setCssProperty(subBtn, "btn-settings-options");
    }

    options = {
        ui->pushButtonFile2,
        ui->pushButtonExportCsv,
        ui->pushButtonOptions1,
        ui->pushButtonOptions2,
        ui->pushButtonOptions5,
        ui->pushButtonConfiguration3,
        ui->pushButtonConfiguration4,
        ui->pushButtonTools1,
        ui->pushButtonTools2,
        ui->pushButtonTools5,
    };

    // Keep all flat section containers permanently visible
    ui->fileButtonsWidget->setVisible(true);
    ui->configurationButtonsWidget->setVisible(true);
    ui->optionsButtonsWidget->setVisible(true);
    ui->toolsButtonsWidget->setVisible(true);
    ui->helpButtonsWidget->setVisible(true);

    settingsBackupWallet = new SettingsBackupWallet(window, this);
    settingsExportCsvWidget = new SettingsExportCSV(window, this);
    settingsBitToolWidget = new SettingsBitToolWidget(window, this);
    settingsSingMessageWidgets = new SettingsSignMessageWidgets(window, this);
    settingsWalletRepairWidget = new SettingsWalletRepairWidget(window, this);
    settingsWalletOptionsWidget = new SettingsWalletOptionsWidget(window, this);
    settingsMainOptionsWidget = new SettingsMainOptionsWidget(window, this);
    settingsDisplayOptionsWidget = new SettingsDisplayOptionsWidget(window, this);
    settingsInformationWidget = new SettingsInformationWidget(window, this);
    settingsConsoleWidget = new SettingsConsoleWidget(window, this);
    settingsFaqWidget = new SettingsFaqWidget(window, this);
    settingsAboutWidget = new SettingsAboutWidget(window, this);

    ui->pushButtonHelp2->setCheckable(true);
    options.push_back(ui->pushButtonHelp2);
    if (btnFaqGuide) {
        options.push_back(btnFaqGuide);
    }

    ui->stackedWidgetContainer->addWidget(settingsBackupWallet);
    ui->stackedWidgetContainer->addWidget(settingsExportCsvWidget);
    ui->stackedWidgetContainer->addWidget(settingsBitToolWidget);
    ui->stackedWidgetContainer->addWidget(settingsSingMessageWidgets);
    ui->stackedWidgetContainer->addWidget(settingsWalletRepairWidget);
    ui->stackedWidgetContainer->addWidget(settingsWalletOptionsWidget);
    ui->stackedWidgetContainer->addWidget(settingsMainOptionsWidget);
    ui->stackedWidgetContainer->addWidget(settingsDisplayOptionsWidget);
    ui->stackedWidgetContainer->addWidget(settingsInformationWidget);
    ui->stackedWidgetContainer->addWidget(settingsConsoleWidget);
    ui->stackedWidgetContainer->addWidget(settingsFaqWidget);
    ui->stackedWidgetContainer->addWidget(settingsAboutWidget);
    ui->stackedWidgetContainer->setCurrentWidget(settingsBackupWallet);

    for (PWidget* subPage : {
             static_cast<PWidget*>(settingsBackupWallet),
             static_cast<PWidget*>(settingsExportCsvWidget),
             static_cast<PWidget*>(settingsBitToolWidget),
             static_cast<PWidget*>(settingsSingMessageWidgets),
             static_cast<PWidget*>(settingsWalletRepairWidget),
             static_cast<PWidget*>(settingsWalletOptionsWidget),
             static_cast<PWidget*>(settingsMainOptionsWidget),
             static_cast<PWidget*>(settingsDisplayOptionsWidget),
             static_cast<PWidget*>(settingsInformationWidget),
             static_cast<PWidget*>(settingsConsoleWidget),
             static_cast<PWidget*>(settingsFaqWidget),
             static_cast<PWidget*>(settingsAboutWidget)}) {
        if (subPage) {
            connect(subPage, &PWidget::message, this, &SettingsWidget::message);
            connect(subPage, &PWidget::showHide, this, &SettingsWidget::showHide);
            connect(subPage, &PWidget::execDialog, this, &SettingsWidget::execDialog);
        }
    }

    // File Section
    connect(ui->pushButtonFile, &QPushButton::clicked, this, &SettingsWidget::onFileClicked);
    connect(ui->pushButtonFile2, &QPushButton::clicked, this, &SettingsWidget::onBackupWalletClicked);
    connect(ui->pushButtonExportCsv, &QPushButton::clicked, this, &SettingsWidget::onExportCSVClicked);

    // Options
    connect(ui->pushButtonOptions, &QPushButton::clicked, this, &SettingsWidget::onOptionsClicked);
    connect(ui->pushButtonOptions1, &QPushButton::clicked, this, &SettingsWidget::onMainOptionsClicked);
    connect(ui->pushButtonOptions2, &QPushButton::clicked, this, &SettingsWidget::onWalletOptionsClicked);
    connect(ui->pushButtonOptions5, &QPushButton::clicked, this, &SettingsWidget::onDisplayOptionsClicked);

    // Configuration
    connect(ui->pushButtonConfiguration, &QPushButton::clicked, this, &SettingsWidget::onConfigurationClicked);
    connect(ui->pushButtonConfiguration3, &QPushButton::clicked, this, &SettingsWidget::onBipToolClicked);
    connect(ui->pushButtonConfiguration4, &QPushButton::clicked, this, &SettingsWidget::onSignMessageClicked);

    // Tools
    connect(ui->pushButtonTools, &QPushButton::clicked, this, &SettingsWidget::onToolsClicked);
    connect(ui->pushButtonTools1, &QPushButton::clicked, this, &SettingsWidget::onInformationClicked);
    connect(ui->pushButtonTools2, &QPushButton::clicked, this, &SettingsWidget::onDebugConsoleClicked);
    ui->pushButtonTools2->setShortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_C));
    connect(ui->pushButtonTools5, &QPushButton::clicked, this, &SettingsWidget::onWalletRepairClicked);

    // Help
    connect(ui->pushButtonHelp, &QPushButton::clicked, this, &SettingsWidget::onHelpClicked);
    connect(ui->pushButtonHelp2, &QPushButton::clicked, this, &SettingsWidget::onAboutClicked);
    connect(ui->pushButtonHelp3, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://kristalteknoloji.com"));
    });
    connect(ui->pushButtonHelp4, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://explorer.kristalteknoloji.com"));
    });

    // Get restart command-line parameters and handle restart
    connect(settingsWalletRepairWidget, &SettingsWalletRepairWidget::handleRestart, [this](QStringList arg){Q_EMIT handleRestart(arg);});

    connect(settingsDisplayOptionsWidget, &SettingsDisplayOptionsWidget::saveSettings, this, &SettingsWidget::onSaveOptionsClicked);
    connect(settingsDisplayOptionsWidget, &SettingsDisplayOptionsWidget::discardSettings, this, &SettingsWidget::onDiscardChanges);

    connect(settingsMainOptionsWidget, &SettingsMainOptionsWidget::saveSettings, this, &SettingsWidget::onSaveOptionsClicked);
    connect(settingsMainOptionsWidget, &SettingsMainOptionsWidget::discardSettings, this, &SettingsWidget::onDiscardChanges);

    connect(settingsWalletOptionsWidget, &SettingsWalletOptionsWidget::saveSettings, this, &SettingsWidget::onSaveOptionsClicked);
    connect(settingsWalletOptionsWidget, &SettingsWalletOptionsWidget::discardSettings, this, &SettingsWidget::onDiscardChanges);

    /* Widget-to-option mapper */
    mapper = new QDataWidgetMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    // scroll area
    ui->scrollArea->setWidgetResizable(true);
    ui->scrollAreaWidgetContents->setAutoFillBackground(false);
    if (ui->scrollArea->viewport()) {
        ui->scrollArea->viewport()->setAutoFillBackground(false);
    }
    QSizePolicy scrollAreaPolicy = ui->scrollArea->sizePolicy();
    scrollAreaPolicy.setVerticalStretch(1);
    ui->scrollArea->setSizePolicy(scrollAreaPolicy);
    QSizePolicy scrollVertPolicy = ui->scrollAreaWidgetContents->sizePolicy();
    scrollVertPolicy.setVerticalStretch(1);
    ui->scrollAreaWidgetContents->setSizePolicy(scrollVertPolicy);

    selectOption(ui->pushButtonFile2);
}

void SettingsWidget::loadClientModel()
{
    if (clientModel) {
        this->settingsInformationWidget->setClientModel(this->clientModel);
        this->settingsConsoleWidget->setClientModel(this->clientModel);

        OptionsModel *optionsModel = this->clientModel->getOptionsModel();
        if (optionsModel) {
            settingsDisplayOptionsWidget->setClientModel(clientModel);
            settingsMainOptionsWidget->setClientModel(clientModel);
            settingsWalletOptionsWidget->setClientModel(clientModel);

            mapper->setModel(optionsModel);
            setMapper();
            mapper->toFirst();
        }
    }
}

void SettingsWidget::loadWalletModel()
{
    this->settingsBackupWallet->setWalletModel(this->walletModel);
    this->settingsExportCsvWidget->setWalletModel(this->walletModel);
    this->settingsSingMessageWidgets->setWalletModel(this->walletModel);
    this->settingsBitToolWidget->setWalletModel(this->walletModel);
    this->settingsWalletRepairWidget->setWalletModel(this->walletModel);
    this->settingsDisplayOptionsWidget->setWalletModel(this->walletModel);
}

void SettingsWidget::onResetAction()
{
    if (walletModel) {
        // confirmation dialog
        if (!ask(tr("Confirm options reset"), tr("Client restart required to activate changes.") + "<br><br>" + tr("Client will be shutdown, do you want to proceed?")))
            return;

        /* reset all options and close GUI */
        this->clientModel->getOptionsModel()->Reset();
        QApplication::quit();
    }
}

void SettingsWidget::onSaveOptionsClicked()
{
    if (mapper->submit()) {
        OptionsModel* optionsModel = this->clientModel->getOptionsModel();
        if (optionsModel->isSSTChanged() && !optionsModel->isSSTValid()) {
            const double stakeSplitMinimum = optionsModel->getSSTMinimum();
            settingsWalletOptionsWidget->setSpinBoxStakeSplitThreshold(stakeSplitMinimum);
            inform(tr("Stake Split too low, it shall be either >= %1 or equal to 0 (to disable stake splitting)").arg(stakeSplitMinimum));
            return;
        }
        pwalletMain->MarkDirty();
        if (optionsModel->isRestartRequired()) {
            bool fAcceptRestart = openStandardDialog(tr("Restart required"), tr("Your wallet needs to be restarted to apply the changes\n"), tr("Restart Now"), tr("Restart Later"));

            if (fAcceptRestart) {
                // Get command-line arguments and remove the application name
                QStringList args = QApplication::arguments();
                args.removeFirst();

                // Remove existing repair-options
                args.removeAll(SALVAGEWALLET);
                args.removeAll(RESCAN);
                args.removeAll(ZAPTXES1);
                args.removeAll(ZAPTXES2);
                args.removeAll(UPGRADEWALLET);
                args.removeAll(REINDEX);
                args.removeAll(RESYNC);

                Q_EMIT handleRestart(args);
            } else {
                inform(tr("Options will be applied on next wallet restart"));
            }
        } else {
            inform(tr("Options stored"));
        }
    } else {
        inform(tr("Options store failed"));
    }
}

void SettingsWidget::selectMenu(QPushButton* /*btn*/)
{
    // All grouped settings sections remain flat & visible at a glance
}

void SettingsWidget::onFileClicked()
{
    selectMenu(ui->pushButtonFile);
}

void SettingsWidget::onBackupWalletClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsBackupWallet);
    selectOption(ui->pushButtonFile2);
}

void SettingsWidget::onSignMessageClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsSingMessageWidgets);
    selectOption(ui->pushButtonConfiguration4);
}

void SettingsWidget::onConfigurationClicked()
{
    selectMenu(ui->pushButtonConfiguration);
}

void SettingsWidget::onBipToolClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsBitToolWidget);
    selectOption(ui->pushButtonConfiguration3);
}

void SettingsWidget::onExportCSVClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsExportCsvWidget);
    selectOption(ui->pushButtonExportCsv);
}

void SettingsWidget::onOptionsClicked()
{
    selectMenu(ui->pushButtonOptions);
}

void SettingsWidget::onMainOptionsClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsMainOptionsWidget);
    selectOption(ui->pushButtonOptions1);
}

void SettingsWidget::onWalletOptionsClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsWalletOptionsWidget);
    selectOption(ui->pushButtonOptions2);
}

void SettingsWidget::onDisplayOptionsClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsDisplayOptionsWidget);
    selectOption(ui->pushButtonOptions5);
}


void SettingsWidget::onToolsClicked()
{
    selectMenu(ui->pushButtonTools);
}

void SettingsWidget::onInformationClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsInformationWidget);
    selectOption(ui->pushButtonTools1);
}

void SettingsWidget::showDebugConsole()
{
    ui->pushButtonTools->setChecked(true);
    onToolsClicked();
    ui->pushButtonTools2->setChecked(true);
    onDebugConsoleClicked();
}

void SettingsWidget::showFaq(int section)
{
    if (settingsFaqWidget) {
        settingsFaqWidget->setSection(section);
    }
    onFaqClicked();
}

void SettingsWidget::onFaqClicked()
{
    if (settingsFaqWidget) {
        ui->stackedWidgetContainer->setCurrentWidget(settingsFaqWidget);
    }
    if (btnFaqGuide) {
        selectOption(btnFaqGuide);
    }
}

void SettingsWidget::showInformation()
{
    ui->pushButtonTools->setChecked(true);
    onToolsClicked();
    ui->pushButtonTools1->setChecked(true);
    onInformationClicked();
}

void SettingsWidget::onDebugConsoleClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsConsoleWidget);
    selectOption(ui->pushButtonTools2);
}

void SettingsWidget::onWalletRepairClicked()
{
    ui->stackedWidgetContainer->setCurrentWidget(settingsWalletRepairWidget);
    selectOption(ui->pushButtonTools5);
}


void SettingsWidget::onHelpClicked()
{
    selectMenu(ui->pushButtonHelp);
}

void SettingsWidget::onAboutClicked()
{
    if (settingsAboutWidget) {
        ui->stackedWidgetContainer->setCurrentWidget(settingsAboutWidget);
    }
    selectOption(ui->pushButtonHelp2);
}

void SettingsWidget::openNetworkMonitor()
{
    settingsInformationWidget->openNetworkMonitor();
}

void SettingsWidget::showPeers()
{
    settingsInformationWidget->showPeers();
}

void SettingsWidget::selectOption(QPushButton* option)
{
    for (QPushButton* wid : options) {
        if (wid) wid->setChecked(wid == option);
    }
}

void SettingsWidget::onDiscardChanges()
{
    if (clientModel) {
        if (!ask(tr("Discard Unsaved Changes"), tr("You are just about to discard all of your unsaved options.\n\nAre you sure?\n")))
            return;
        clientModel->getOptionsModel()->refreshDataView();
    }
}

void SettingsWidget::setMapper()
{
    settingsMainOptionsWidget->setMapper(mapper);
    settingsWalletOptionsWidget->setMapper(mapper);
    settingsDisplayOptionsWidget->setMapper(mapper);
}

bool SettingsWidget::openStandardDialog(const QString& title, const QString& body, const QString& okBtn, const QString& cancelBtn)
{
    showHideOp(true);
    DefaultDialog *confirmDialog = new DefaultDialog(window);
    confirmDialog->setText(title, body, okBtn, cancelBtn);
    confirmDialog->adjustSize();
    openDialogWithOpaqueBackground(confirmDialog, window);
    confirmDialog->deleteLater();
    return confirmDialog->isOk;
}

SettingsWidget::~SettingsWidget()
{
    delete ui;
}
