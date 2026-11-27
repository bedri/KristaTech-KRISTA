// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsmainoptionswidget.h"
#include "qt/kristatech/settings/forms/ui_settingsmainoptionswidget.h"
#include "QListView"

#if defined(HAVE_CONFIG_H)
#include "config/kristatech-config.h"
#endif

#include "bitcoinunits.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "clientmodel.h"
#include "qt/kristatech/qtutils.h"

#include "main.h" // for MAX_SCRIPTCHECK_THREADS
#include "netbase.h"
#include "txdb.h" // for -dbcache defaults

#ifdef ENABLE_WALLET
#include "wallet/wallet.h" // for CWallet::minTxFee
#endif

#include <boost/thread.hpp>

#include <QDataWidgetMapper>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLocale>
#include <QTimer>
#include <QVBoxLayout>


SettingsMainOptionsWidget::SettingsMainOptionsWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsMainOptionsWidget)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    /* Main elements init */
    ui->databaseCache->setMinimum(nMinDbCache);
    ui->databaseCache->setMaximum(nMaxDbCache);
    ui->threadsScriptVerif->setMinimum(-GetNumCores());
    ui->threadsScriptVerif->setMaximum(MAX_SCRIPTCHECK_THREADS);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Main Options"),
        tr("Configure startup behavior, database cache, script verification, and window controls."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Card 1: Application & Performance
    QVBoxLayout* appBody = nullptr;
    QFrame* cardApp = createSettingsCard(modernRoot, tr("Application & Performance"), appBody);

    clearLegacySettingsConstraints(ui->pushSwitchStart);
    ui->pushSwitchStart->setText(QString());
    ui->pushSwitchStart->setFixedSize(44, 28);
    ui->pushSwitchStart->setCursor(Qt::PointingHandCursor);
    ui->pushSwitchStart->setFocusPolicy(Qt::NoFocus);
    ui->pushSwitchStart->setProperty("settingsToggle", true);
    addSettingsControlRow(
        cardApp,
        appBody,
        tr("Start KRISTA on system login"),
        tr("Automatically launch KristaTech Wallet when you sign in to your desktop."),
        ui->pushSwitchStart,
        false);

    clearLegacySettingsConstraints(ui->databaseCache);
    ui->databaseCache->setFixedSize(160, 36);
    ui->databaseCache->setSuffix(QStringLiteral(" MB"));
    ui->databaseCache->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->databaseCache->setProperty("settingsInput", true);
    addSettingsControlRow(
        cardApp,
        appBody,
        tr("Database Cache Size"),
        tr("Memory allocated for UTXO and block index caching (restart required)."),
        ui->databaseCache,
        true);

    clearLegacySettingsConstraints(ui->threadsScriptVerif);
    ui->threadsScriptVerif->setFixedSize(160, 36);
    ui->threadsScriptVerif->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->threadsScriptVerif->setProperty("settingsInput", true);
    addSettingsControlRow(
        cardApp,
        appBody,
        tr("Script Verification Threads"),
        tr("0 = automatic; negative values leave CPU cores free for the operating system."),
        ui->threadsScriptVerif,
        true);

    scrollLayout->addWidget(cardApp);

#ifndef Q_OS_MAC
    // Card 2: Window Behavior
    QVBoxLayout* winBody = nullptr;
    QFrame* cardWin = createSettingsCard(modernRoot, tr("Window Behavior"), winBody);
    addSettingsCheckRow(
        cardWin,
        winBody,
        ui->checkBoxMinTaskbar,
        tr("Minimize to the tray instead of the taskbar"),
        tr("Keep the wallet icon in the system tray rather than the window taskbar."),
        false);
    addSettingsCheckRow(
        cardWin,
        winBody,
        ui->checkBoxMinClose,
        tr("Minimize on close"),
        tr("Closing the main window keeps the node running quietly in the background."),
        true);
    scrollLayout->addWidget(cardWin);
#endif

    scrollLayout->addStretch(1);

    buildSettingsOptionsFooter(modernRoot, rootLayout, ui->pushButtonReset, ui->pushButtonClean, ui->pushButtonSave);

    connect(ui->pushButtonSave, &QPushButton::clicked, [this] { Q_EMIT saveSettings(); });
    connect(ui->pushButtonReset, &QPushButton::clicked, this, &SettingsMainOptionsWidget::onResetClicked);
    connect(ui->pushButtonClean, &QPushButton::clicked, [this] { Q_EMIT discardSettings(); });

    applyTheme(isLightTheme());
}

void SettingsMainOptionsWidget::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsMainOptionsWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsMainOptionsWidget::onResetClicked()
{
    if (clientModel) {
        if (!ask(tr("Reset Options"), tr("You are just about to reset the app\'s options to the default values.\n\nAre you sure?\n")))
            return;
        OptionsModel *optionsModel = clientModel->getOptionsModel();
        QSettings settings;
        // default setting for OptionsModel::StartAtStartup - disabled
        if (GUIUtil::GetStartOnSystemStartup())
            GUIUtil::SetStartOnSystemStartup(false);
        optionsModel->setMainDefaultOptions(settings, true);
        optionsModel->setWindowDefaultOptions(settings, true);
        inform(tr("Options reset succeed"));
    }
}

void SettingsMainOptionsWidget::setMapper(QDataWidgetMapper *mapper)
{
    mapper->addMapping(ui->pushSwitchStart, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->threadsScriptVerif, OptionsModel::ThreadsScriptVerif);
    mapper->addMapping(ui->databaseCache, OptionsModel::DatabaseCache);
    /* Window */
#ifndef Q_OS_MAC
    mapper->addMapping(ui->checkBoxMinTaskbar, OptionsModel::MinimizeToTray);
    mapper->addMapping(ui->checkBoxMinClose, OptionsModel::MinimizeOnClose);
#endif
}

SettingsMainOptionsWidget::~SettingsMainOptionsWidget()
{
    delete ui;
}
