// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/topbar.h"
#include "askpassphrasedialog.h"
#include "qt/kristatech/forms/ui_topbar.h"
#include "qt/kristatech/loadingdialog.h"
#include "qt/kristatech/lockunlock.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/receivedialog.h"

#include "addresstablemodel.h"
#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiinterface.h"
#include "optionsmodel.h"
#include "qt/guiconstants.h"
#include "qt/guiutil.h"
#include "qt/platformstyle.h"
#include "walletmodel.h"

#include "evo/deterministicmns.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "wallet/wallet.h"

#include <set>

#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>
#include "qt/kristatech/kristatechgui.h"

#define REQUEST_UPGRADE_WALLET 1

TopBar::TopBar(KRISTATECHGUI* _mainWindow, QWidget* parent) : PWidget(_mainWindow, parent),
                                                        ui(new Ui::TopBar)
{
    ui->setupUi(this);
    if (_mainWindow) {
        this->setStyleSheet(_mainWindow->styleSheet());
    }

    // Create Logo button programmatically
    imgLogo = new QPushButton(this);
    imgLogo->setObjectName("btnTopLogo");
    imgLogo->setMinimumSize(QSize(68, 68));
    imgLogo->setMaximumSize(QSize(68, 68));
    imgLogo->setIcon(QIcon("://img-nav-logo"));
    imgLogo->setIconSize(QSize(62, 62));
    imgLogo->setCursor(Qt::PointingHandCursor);
    imgLogo->setFocusPolicy(Qt::NoFocus);
    imgLogo->setToolTip(tr("KristaTech Home"));
    imgLogo->setStyleSheet("QPushButton#btnTopLogo { background-color: transparent; border: none; margin-right: 8px; }");
    connect(imgLogo, &QPushButton::clicked, this, &TopBar::onDashboardClicked);
    ui->horizontalLayout_4->insertWidget(0, imgLogo, 0, Qt::AlignVCenter);

    // Create 8-tab single-line navigation layout & buttons
    QHBoxLayout* navLayout = new QHBoxLayout();
    navLayout->setSpacing(4);
    navLayout->setContentsMargins(0, 0, 8, 0);

    btnDashboard = new QToolButton(this);
    btnSend = new QToolButton(this);
    btnReceive = new QToolButton(this);
    btnMinerHub = new QToolButton(this);
    btnSmartContract = new QToolButton(this);
    btnMaster = new QToolButton(this);
    btnAddresses = new QToolButton(this);
    btnStats = new QToolButton(this);
    btnSettings = new QToolButton(this);

    navBtns = {
        btnDashboard,
        btnSend,
        btnReceive,
        btnMinerHub,
        btnSmartContract,
        btnMaster,
        btnAddresses,
        btnStats,
        btnSettings
    };

    btnDashboard->setText(tr("HOME"));
    btnSend->setText(tr("SEND"));
    btnReceive->setText(tr("RECEIVE"));
    btnMinerHub->setText(tr("MINER HUB"));
    btnMinerHub->setToolTip(tr("ADAM Dual-Track Cooperative Miner & Validator Hub"));
    btnSmartContract->setText(tr("CONTRACTS"));
    btnSmartContract->setToolTip(tr("MESCAL v2 Taproot Smart Contract Studio"));
    btnMaster->setText(tr("MASTERNODES"));
    btnAddresses->setText(tr("CONTACTS"));
    btnAddresses->setToolTip(tr("Saved Contacts & Address Book"));
    btnStats->setText(tr("STATS"));
    btnStats->setToolTip(tr("Wallet, Rewards, Consensus & Network Statistics"));
    btnSettings->setText(tr("SETTINGS"));

    for (QToolButton* btn : navBtns) {
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        btn->setFixedHeight(40);
        setCssProperty(btn, "btn-nav-top");
        navLayout->addWidget(btn, 0, Qt::AlignVCenter);
    }

    connect(btnDashboard, &QToolButton::clicked, this, &TopBar::onDashboardClicked);
    connect(btnSend, &QToolButton::clicked, this, &TopBar::onSendClicked);
    connect(btnReceive, &QToolButton::clicked, this, &TopBar::onReceiveClicked);
    connect(btnMinerHub, &QToolButton::clicked, this, &TopBar::onMinerHubClicked);
    connect(btnSmartContract, &QToolButton::clicked, this, &TopBar::onSmartContractClicked);
    connect(btnMaster, &QToolButton::clicked, this, &TopBar::onMasterClicked);
    connect(btnAddresses, &QToolButton::clicked, this, &TopBar::onAddressesClicked);
    connect(btnStats, &QToolButton::clicked, this, &TopBar::onStatsClicked);
    connect(btnSettings, &QToolButton::clicked, this, &TopBar::onSettingsClicked);

    ui->horizontalLayout_4->insertLayout(1, navLayout);
    onNavSelected(btnDashboard);

    /* Containers */
    ui->containerTop->setContentsMargins(10, 4, 10, 4);
    ui->containerTop->setProperty("cssClass", "container-top");

    setCssProperty({ui->labelTitle1, ui->labelTitle3, ui->labelTitle4, ui->labelTitle5,
                       ui->labelTitle6, ui->labelMasternodesTitle, ui->labelTitle8,
                       ui->labelNextMasternodesTitle, ui->labelTitle9},
        "text-title-topbar");

    // Persistent Dual-Balance Top Summary (Available + Locked/Pending badge across all non-Home tabs)
    ui->widgetTopAmount->setVisible(false);
    ui->widgetAmount->setVisible(true);
    ui->labelAmountTopPiv->setMinimumHeight(20);
    setCssProperty({ui->labelAmountTopPiv}, "amount-small-topbar");

    if (ui->horizontalLayout_5) {
        ui->horizontalLayout_5->removeWidget(ui->labelAmountTopPiv);
        QVBoxLayout* topBalStack = new QVBoxLayout();
        topBalStack->setSpacing(2);
        topBalStack->setContentsMargins(4, 2, 4, 2);
        ui->labelAmountTopPiv->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labelLockedTopPiv = new QLabel(tr("0.00 Locked"), ui->widgetTopAmount);
        labelLockedTopPiv->setObjectName("labelLockedTopPiv");
        labelLockedTopPiv->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labelLockedTopPiv->setMinimumHeight(18);
        labelLockedTopPiv->setCursor(Qt::PointingHandCursor);
        labelLockedTopPiv->installEventFilter(this);
        topBalStack->addWidget(ui->labelAmountTopPiv);
        topBalStack->addWidget(labelLockedTopPiv);
        ui->horizontalLayout_5->addLayout(topBalStack);
    }
    updateTopBalancePillStyle();

    setCssProperty({ui->labelAmountPiv}, "amount-topbar");
    setCssProperty({ui->labelPendingPiv, ui->labelImmaturePiv, ui->labelAvailablePiv,
                       ui->labelLockedPiv, ui->labelMasternodeCount, ui->labelCollateralPiv,
                       ui->labelNextCollateralBlocks, ui->labelNextCollateralValue},
        "amount-small-topbar");
    ui->labelLockedPiv->setCursor(Qt::PointingHandCursor);
    ui->labelLockedPiv->installEventFilter(this);

    // Next masternode collateral
    ui->widgetNextCollateral->setVisible(false);

    // Progress Sync
    progressBar = new QProgressBar(ui->layoutSync);
    progressBar->setRange(1, 10);
    progressBar->setValue(4);
    progressBar->setTextVisible(false);
    progressBar->setMaximumHeight(2);
    progressBar->setMaximumWidth(36);
    setCssProperty(progressBar, "progress-sync");
    progressBar->show();
    progressBar->raise();
    progressBar->move(0, 34);

    ui->pushButtonHDUpgrade->setButtonClassStyle("cssClass", "btn-check-hd-upgrade");
    ui->pushButtonHDUpgrade->setButtonText(tr("Upgrade to HD Wallet"));
    ui->pushButtonHDUpgrade->setNoIconText("HD");
    ui->pushButtonHDUpgrade->setCheckable(false);

    ui->pushButtonConnection->setButtonClassStyle("cssClass", "btn-check-connect-inactive");
    ui->pushButtonConnection->setButtonText(tr("No Connection"));

    ui->pushButtonTor->setButtonClassStyle("cssClass", "btn-check-tor-inactive");
    ui->pushButtonTor->setButtonText(tr("Tor Disabled"));
    ui->pushButtonTor->setChecked(false);
    ui->pushButtonTor->setVisible(false);

    ui->pushButtonStack->setButtonClassStyle("cssClass", "btn-check-stack-inactive");
    ui->pushButtonStack->setButtonText(tr("Staking Disabled"));

    ui->pushButtonConf->setButtonClassStyle("cssClass", "btn-check-conf");
    ui->pushButtonConf->setButtonText(tr("Config Files"));
    ui->pushButtonConf->setCheckable(false);
    ui->pushButtonConf->setChecked(false);

    ui->pushButtonMasternodes->setButtonClassStyle("cssClass", "btn-check-masternodes");
    ui->pushButtonMasternodes->setButtonText("masternode.conf");
    ui->pushButtonMasternodes->setCheckable(false);
    ui->pushButtonMasternodes->setChecked(false);
    ui->pushButtonMasternodes->setVisible(false); // Unified inside Config Files quick-menu

    ui->pushButtonConsole->setButtonClassStyle("cssClass", "btn-check-console");
    ui->pushButtonConsole->setButtonText(tr("Debug Console"));
    ui->pushButtonConsole->setCheckable(false);
    ui->pushButtonConsole->setChecked(false);

    ui->pushButtonSync->setButtonClassStyle("cssClass", "btn-check-sync");
    ui->pushButtonSync->setSuppressToolTip(true);
    ui->pushButtonSync->setButtonText(tr(" %54 Synchronizing.."));
    ui->pushButtonSync->setCheckable(false);

    ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-lock");
    ui->pushButtonLock->setCheckable(false);

    ui->pushButtonTheme->setCheckable(false);
    if (isLightTheme()) {
        ui->pushButtonTheme->setButtonClassStyle("cssClass", "btn-check-theme-light");
        ui->pushButtonTheme->setButtonText(tr("Light Theme"));
    } else {
        ui->pushButtonTheme->setButtonClassStyle("cssClass", "btn-check-theme-dark");
        ui->pushButtonTheme->setButtonText(tr("Dark Theme"));
    }

    // Replace standalone Sync icon button with the compact live Sync Status box in horizontalLayout_4
    ui->layoutSync->setVisible(false);
    if (ui->horizontalLayout_4) {
        ui->horizontalLayout_4->setSpacing(8);
        btnSyncStatusBox = new QPushButton(tr("Syncing.."), this);
        btnSyncStatusBox->setObjectName("btnSyncStatusBox");
        btnSyncStatusBox->setFlat(true);
        btnSyncStatusBox->setCursor(Qt::PointingHandCursor);
        btnSyncStatusBox->installEventFilter(this);
        connect(btnSyncStatusBox, &QPushButton::clicked, [this]() { window->goToSettingsInfo(); });
        const int syncIdx = ui->horizontalLayout_4->indexOf(ui->layoutSync);
        ui->horizontalLayout_4->insertWidget(syncIdx >= 0 ? syncIdx : 0, btnSyncStatusBox, 0, Qt::AlignVCenter);

        // Persistent, hover-driven Sync Info popup anchored below btnSyncStatusBox (closes on mouseout, never times out while hovered)
        syncInfoPopup = new QFrame(window);
        syncInfoPopup->setObjectName("syncInfoPopup");
        syncInfoPopup->setFrameShape(QFrame::StyledPanel);
        syncInfoPopup->installEventFilter(this);
        QHBoxLayout* popupLayout = new QHBoxLayout(syncInfoPopup);
        popupLayout->setContentsMargins(12, 8, 8, 8);
        popupLayout->setSpacing(10);

        lblSyncPopupText = new QLabel(tr("Synchronizing.."), syncInfoPopup);
        lblSyncPopupText->setObjectName("lblSyncPopupText");
        lblSyncPopupText->setCursor(Qt::PointingHandCursor);
        lblSyncPopupText->installEventFilter(this);
        popupLayout->addWidget(lblSyncPopupText);

        QPushButton* btnClosePopup = new QPushButton(QString::fromUtf8("×"), syncInfoPopup);
        btnClosePopup->setObjectName("btnCloseSyncPopup");
        btnClosePopup->setFlat(true);
        btnClosePopup->setFixedSize(20, 20);
        btnClosePopup->setCursor(Qt::PointingHandCursor);
        btnClosePopup->setToolTip(tr("Close"));
        connect(btnClosePopup, &QPushButton::clicked, syncInfoPopup, &QFrame::hide);
        popupLayout->addWidget(btnClosePopup, 0, Qt::AlignTop);
        syncInfoPopup->hide();

        rightToolsDivider = new QFrame(this);
        rightToolsDivider->setFixedSize(1, 24);
        ui->horizontalLayout_4->removeWidget(ui->pushButtonConf);
        ui->horizontalLayout_4->removeWidget(ui->pushButtonConsole);
        ui->horizontalLayout_4->removeWidget(ui->pushButtonTheme);
        ui->horizontalLayout_4->addWidget(rightToolsDivider, 0, Qt::AlignVCenter);
        ui->horizontalLayout_4->addWidget(ui->pushButtonConf, 0, Qt::AlignVCenter);
        ui->horizontalLayout_4->addWidget(ui->pushButtonConsole, 0, Qt::AlignVCenter);
        ui->horizontalLayout_4->addWidget(ui->pushButtonTheme, 0, Qt::AlignVCenter);
    }
    updateTopBalancePillStyle();

    setCssProperty(ui->qrContainer, "container-qr");
    setCssProperty(ui->pushButtonQR, "btn-qr");
    ui->pushButtonQR->setIcon(QIcon());
    ui->pushButtonQR->setText(tr("Receive"));
    ui->pushButtonQR->setMinimumSize(72, 22);
    ui->pushButtonQR->setMaximumSize(78, 22);
    ui->pushButtonQR->setCursor(Qt::PointingHandCursor);
    ui->pushButtonQR->setToolTip(tr("Open Receive QR & address dialog"));
    if (ui->btnQr) {
        ui->btnQr->setCursor(Qt::PointingHandCursor);
        ui->btnQr->setToolTip(tr("Click to open Receive QR & address dialog"));
        ui->btnQr->setStyleSheet("QPushButton#btnQr { background-color: transparent; border: 1px solid transparent; padding: 2px; border-radius: 6px; }"
                                 "QPushButton#btnQr:hover { background-color: transparent; border: 1px solid rgba(118, 149, 205, 0.45); }");
    }

    if (ui->widgetMasternodesSummary) {
        ui->widgetMasternodesSummary->setAttribute(Qt::WA_StyledBackground, true);
        ui->widgetMasternodesSummary->setAttribute(Qt::WA_Hover, true);
        ui->widgetMasternodesSummary->setCursor(Qt::PointingHandCursor);
        ui->widgetMasternodesSummary->setToolTip(tr("Click to open Masternodes"));
        ui->widgetMasternodesSummary->setStyleSheet(
            "QWidget#widgetMasternodesSummary { background-color: transparent; border: 1px solid transparent; border-radius: 8px; }"
            "QWidget#widgetMasternodesSummary:hover { background-color: transparent; border: 1px solid rgba(118, 149, 205, 0.38); }");
        ui->widgetMasternodesSummary->installEventFilter(this);
    }

    // QR image
    QPixmap pixmap("://img-qr-test");
    ui->btnQr->setIcon(
        QIcon(pixmap.scaled(
            70,
            70,
            Qt::KeepAspectRatio)));

    ui->pushButtonLock->setButtonText(tr("Wallet Locked "));
    ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-lock");


    connect(ui->pushButtonQR, &QPushButton::clicked, this, &TopBar::onBtnReceiveClicked);
    connect(ui->btnQr, &QPushButton::clicked, this, &TopBar::onBtnReceiveClicked);
    connect(ui->pushButtonLock, &ExpandableButton::Mouse_Pressed, this, &TopBar::onBtnLockClicked);
    connect(ui->pushButtonTheme, &ExpandableButton::Mouse_Pressed, this, &TopBar::onThemeClicked);
    connect(ui->pushButtonConf, &ExpandableButton::Mouse_Pressed, this, &TopBar::onBtnConfClicked);
    connect(ui->pushButtonMasternodes, &ExpandableButton::Mouse_Pressed, this, &TopBar::onBtnMasternodesClicked);
    connect(ui->pushButtonSync, &ExpandableButton::Mouse_HoverLeave, this, &TopBar::refreshProgressBarSize);
    connect(ui->pushButtonSync, &ExpandableButton::Mouse_Hover, this, &TopBar::refreshProgressBarSize);
    connect(ui->pushButtonSync, &ExpandableButton::Mouse_Pressed, [this]() { window->goToSettingsInfo(); });
    connect(ui->pushButtonConsole, &ExpandableButton::Mouse_Pressed, [this]() { window->goToDebugConsole(); });
    connect(ui->pushButtonConnection, &ExpandableButton::Mouse_Pressed, [this]() { window->showPeers(); });

    refreshStatus();
}

void TopBar::onThemeClicked()
{
    // Store theme
    bool lightTheme = !isLightTheme();

    setTheme(lightTheme);

    if (lightTheme) {
        ui->pushButtonTheme->setButtonClassStyle("cssClass", "btn-check-theme-light", true);
        ui->pushButtonTheme->setButtonText(tr("Light Theme"));
    } else {
        ui->pushButtonTheme->setButtonClassStyle("cssClass", "btn-check-theme-dark", true);
        ui->pushButtonTheme->setButtonText(tr("Dark Theme"));
    }
    updateStyle(ui->pushButtonTheme);
    updateTopBalancePillStyle();

    Q_EMIT themeChanged(lightTheme);
}

void TopBar::updateTopBalancePillStyle()
{
    const bool light = isLightTheme();
    if (labelLockedTopPiv) {
        const QString pillBg = light ? "rgba(118, 149, 205, 0.14)" : "rgba(118, 149, 205, 0.20)";
        const QString pillBorder = "#7695CD";
        const QString pillText = light ? "#3E5B94" : "#F1F5F9";
        labelLockedTopPiv->setStyleSheet(QString(
            "QLabel#labelLockedTopPiv { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 9px; padding: 1px 7px; font-size: 11px; font-weight: bold; }"
        ).arg(pillBg, pillText, pillBorder));
    }
    if (btnSyncStatusBox) {
        const QString boxBg = light ? "#F4F7FC" : "rgba(118, 149, 205, 0.16)";
        const QString boxText = light ? "#2B4268" : "#E2E8F0";
        const QString boxBorder = light ? "#94B0DC" : "rgba(118, 149, 205, 0.55)";
        const QString boxHoverBg = light ? "#E6EEF9" : "rgba(118, 149, 205, 0.28)";
        btnSyncStatusBox->setStyleSheet(QString(
            "QPushButton#btnSyncStatusBox { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 6px; padding: 2px 9px; min-height: 18px; font-size: 11.5px; font-weight: 600; }"
            "QPushButton#btnSyncStatusBox:hover { background-color: %4; }"
        ).arg(boxBg, boxText, boxBorder, boxHoverBg));
    }
    if (syncInfoPopup) {
        const QString popBg = light ? "#FFFFFF" : "#1A2640";
        const QString popText = light ? "#1E293B" : "#F1F5F9";
        const QString popBorder = light ? "#5576B2" : "#7695CD";
        syncInfoPopup->setStyleSheet(QString(
            "QFrame#syncInfoPopup { background-color: %1; border: 1.5px solid %3; border-radius: 8px; }"
            "QLabel#lblSyncPopupText { color: %2; font-size: 13px; background: transparent; border: none; }"
            "QPushButton#btnCloseSyncPopup { color: %2; background: transparent; border: none; font-size: 14px; font-weight: bold; border-radius: 4px; }"
            "QPushButton#btnCloseSyncPopup:hover { background-color: rgba(118, 149, 205, 0.22); }"
        ).arg(popBg, popText, popBorder));
    }
    if (rightToolsDivider) {
        rightToolsDivider->setStyleSheet(QString("background-color: %1; border: none;")
            .arg(light ? "#CBD5E1" : "#4E6178"));
    }
}

void TopBar::onBtnLockClicked()
{
    if (walletModel) {
        if (walletModel->getEncryptionStatus() == WalletModel::Unencrypted) {
            encryptWallet();
        } else {
            if (!lockUnlockWidget) {
                lockUnlockWidget = new LockUnlock(window);
                lockUnlockWidget->setStyleSheet("margin:0px; padding:0px;");
                connect(lockUnlockWidget, &LockUnlock::Mouse_Leave, this, &TopBar::lockDropdownMouseLeave);
                connect(ui->pushButtonLock, &ExpandableButton::Mouse_HoverLeave, [this]() {
                    QMetaObject::invokeMethod(this, "lockDropdownMouseLeave", Qt::QueuedConnection);
                });
                connect(lockUnlockWidget, &LockUnlock::lockClicked, this, &TopBar::lockDropdownClicked);
            }

            lockUnlockWidget->updateStatus(walletModel->getEncryptionStatus());
            if (ui->pushButtonLock->width() <= 40) {
                ui->pushButtonLock->setExpanded();
            }
            // Keep it open
            ui->pushButtonLock->setKeepExpanded(true);
            QMetaObject::invokeMethod(this, "openLockUnlock", Qt::QueuedConnection);
        }
    }
}

void TopBar::openLockUnlock()
{
    lockUnlockWidget->setFixedWidth(ui->pushButtonLock->width());
    lockUnlockWidget->adjustSize();

    lockUnlockWidget->move(
        ui->pushButtonLock->pos().rx() + window->getNavWidth() + 10,
        ui->pushButtonLock->y() + 36);

    lockUnlockWidget->raise();
    lockUnlockWidget->activateWindow();
    lockUnlockWidget->show();
}

void TopBar::openPassPhraseDialog(AskPassphraseDialog::Mode mode, AskPassphraseDialog::Context ctx)
{
    if (!walletModel)
        return;

    showHideOp(true);
    AskPassphraseDialog* dlg = new AskPassphraseDialog(mode, window, walletModel, ctx);
    dlg->adjustSize();
    openDialogWithOpaqueBackgroundY(dlg, window);

    refreshStatus();
    dlg->deleteLater();
}

void TopBar::encryptWallet()
{
    return openPassPhraseDialog(AskPassphraseDialog::Mode::Encrypt, AskPassphraseDialog::Context::Encrypt);
}

void TopBar::unlockWallet()
{
    if (!walletModel)
        return;
    // Unlock wallet when requested by wallet model (if unlocked or unlocked for staking only)
    if (walletModel->isWalletLocked(false))
        return openPassPhraseDialog(AskPassphraseDialog::Mode::Unlock, AskPassphraseDialog::Context::Unlock_Full);
}

static bool isExecuting = false;

void TopBar::lockDropdownClicked(const StateClicked& state)
{
    lockUnlockWidget->close();
    if (walletModel && !isExecuting) {
        isExecuting = true;

        switch (lockUnlockWidget->lock) {
        case 0: {
            if (walletModel->getEncryptionStatus() == WalletModel::Locked)
                break;
            walletModel->setWalletLocked(true);
            ui->pushButtonLock->setButtonText(tr("Wallet Locked"));
            ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-lock", true);
            // Directly update the staking status icon when the wallet is manually locked here
            // so the feedback is instant (no need to wait for the polling timeout)
            setStakingStatusActive(false);
            break;
        }
        case 1: {
            if (walletModel->getEncryptionStatus() == WalletModel::Unlocked)
                break;
            showHideOp(true);
            AskPassphraseDialog* dlg = new AskPassphraseDialog(AskPassphraseDialog::Mode::Unlock, window, walletModel,
                AskPassphraseDialog::Context::ToggleLock);
            dlg->adjustSize();
            openDialogWithOpaqueBackgroundY(dlg, window);
            if (walletModel->getEncryptionStatus() == WalletModel::Unlocked) {
                ui->pushButtonLock->setButtonText(tr("Wallet Unlocked"));
                ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-unlock", true);
            }
            dlg->deleteLater();
            break;
        }
        case 2: {
            WalletModel::EncryptionStatus status = walletModel->getEncryptionStatus();
            if (status == WalletModel::UnlockedForStaking)
                break;

            if (status == WalletModel::Unlocked) {
                walletModel->lockForStakingOnly();
            } else {
                showHideOp(true);
                AskPassphraseDialog* dlg = new AskPassphraseDialog(AskPassphraseDialog::Mode::UnlockAnonymize,
                    window, walletModel,
                    AskPassphraseDialog::Context::ToggleLock);
                dlg->adjustSize();
                openDialogWithOpaqueBackgroundY(dlg, window);
                dlg->deleteLater();
            }
            if (walletModel->getEncryptionStatus() == WalletModel::UnlockedForStaking) {
                ui->pushButtonLock->setButtonText(tr("Wallet Unlocked for staking"));
                ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-staking", true);
            }
            break;
        }
        }

        lastEncryptionStatus = static_cast<int>(walletModel->getEncryptionStatus());
        ui->pushButtonLock->setKeepExpanded(false);
        ui->pushButtonLock->setSmall();
        ui->pushButtonLock->update();

        isExecuting = false;
    }
}

void TopBar::lockDropdownMouseLeave()
{
    if (lockUnlockWidget->isVisible() && !lockUnlockWidget->isHovered()) {
        lockUnlockWidget->hide();
        lastEncryptionStatus = static_cast<int>(walletModel->getEncryptionStatus());
        ui->pushButtonLock->setKeepExpanded(false);
        ui->pushButtonLock->setSmall();
        ui->pushButtonLock->update();
    }
}

void TopBar::onBtnReceiveClicked()
{
    if (walletModel) {
        QString addressStr = walletModel->getAddressTableModel()->getAddressToShow();
        if (addressStr.isNull()) {
            inform(tr("Error generating address"));
            return;
        }
        showHideOp(true);
        ReceiveDialog* receiveDialog = new ReceiveDialog(window);
        receiveDialog->updateQr(addressStr);
        if (openDialogWithOpaqueBackground(receiveDialog, window)) {
            inform(tr("Address Copied"));
        }
        receiveDialog->deleteLater();
    }
}

void TopBar::showTop()
{
    if (ui->bottom_container->isVisible()) {
        ui->bottom_container->setVisible(false);
        ui->widgetTopAmount->setVisible(true);
        ui->widgetAmount->setVisible(false);
        this->setFixedHeight(90);
    }
}

void TopBar::showBottom()
{
    ui->widgetTopAmount->setVisible(false);
    ui->widgetAmount->setVisible(true);
    ui->bottom_container->setVisible(true);
    this->setFixedHeight(220);
    this->adjustSize();
}

void TopBar::onBtnConfClicked()
{
    ui->pushButtonConf->setChecked(false);

    const bool light = isLightTheme();
    QMenu menu(this);
    menu.setStyleSheet(QString(
        "QMenu { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 4px; }"
        "QMenu::item { padding: 7px 20px 7px 14px; border-radius: 4px; font-size: 13px; }"
        "QMenu::item:selected { background-color: %3; color: %4; font-weight: bold; }"
        "QMenu::separator { height: 1px; background-color: %5; margin: 4px 8px; }"
    ).arg(light ? "#FFFFFF" : "#131B2E",
          light ? "#1C2B45" : "#E2E8F0",
          light ? "rgba(245, 137, 124, 0.18)" : "rgba(245, 137, 124, 0.22)",
          light ? "#1C2B45" : "#FFFFFF",
          light ? "#E2E8F0" : "#4E6178"));

    QAction* actNodeConf = menu.addAction(tr("Open kristatech.conf (Node Config)"));
    QAction* actActiveMnConf = menu.addAction(tr("Open activemasternode.conf (Hot Multi-MN)"));
    QAction* actColdMnConf = menu.addAction(tr("Open masternode.conf (Cold Controller)"));
    menu.addSeparator();
    QAction* actBackups = menu.addAction(tr("Open Wallet Backups Folder"));

    QFont measureFont = menu.font();
    measureFont.setPixelSize(13);
    measureFont.setBold(true);
    QFontMetrics fm(measureFont);
    int maxTextWidth = 0;
    for (QAction* act : menu.actions()) {
        if (act && !act->isSeparator()) {
            maxTextWidth = std::max(maxTextWidth, fm.boundingRect(act->text()).width());
        }
    }
    menu.setMinimumWidth(maxTextWidth + 52);

    QAction* chosen = menu.exec(ui->pushButtonConf->mapToGlobal(QPoint(0, ui->pushButtonConf->height() + 4)));
    if (chosen == actNodeConf) {
        if (!GUIUtil::openConfigfile())
            inform(tr("Unable to open kristatech.conf with default application"));
    } else if (chosen == actActiveMnConf) {
        if (!GUIUtil::openActiveMNConfigfile())
            inform(tr("Unable to open activemasternode.conf with default application"));
    } else if (chosen == actColdMnConf) {
        if (!GUIUtil::openMNConfigfile())
            inform(tr("Unable to open masternode.conf with default application"));
    } else if (chosen == actBackups) {
        if (!GUIUtil::showBackups())
            inform(tr("Unable to open wallet backups folder"));
    }
}

void TopBar::onBtnMasternodesClicked()
{
    ui->pushButtonMasternodes->setChecked(false);

    if (!GUIUtil::openMNConfigfile())
        inform(tr("Unable to open masternode.conf with default application"));
}

TopBar::~TopBar()
{
    if (timerStakingIcon) {
        timerStakingIcon->stop();
    }
    delete ui;
}

void TopBar::loadClientModel()
{
    if (clientModel) {
        // Keep up to date with client
        setNumConnections(clientModel->getNumConnections());
        connect(clientModel, &ClientModel::numConnectionsChanged, this, &TopBar::setNumConnections);

        setNumBlocks(clientModel->getNumBlocks());
        connect(clientModel, &ClientModel::numBlocksChanged, this, &TopBar::setNumBlocks);

        timerStakingIcon = new QTimer(ui->pushButtonStack);
        connect(timerStakingIcon, &QTimer::timeout, this, &TopBar::updateStakingStatus);
        timerStakingIcon->start(50000);
        updateStakingStatus();
    }
}

void TopBar::setStakingStatusActive(bool fActive)
{
    if (ui->pushButtonStack->isChecked() != fActive) {
        ui->pushButtonStack->setButtonText(fActive ? tr("Staking active") : tr("Staking not active"));
        ui->pushButtonStack->setChecked(fActive);
        ui->pushButtonStack->setButtonClassStyle("cssClass", (fActive ? "btn-check-stack" : "btn-check-stack-inactive"), true);
    }
}
void TopBar::updateStakingStatus()
{
    setStakingStatusActive(walletModel &&
                           !walletModel->isWalletLocked() &&
                           walletModel->isStakingStatusActive());

    // Taking advantage of this timer to update Tor status if needed.
    updateTorIcon();
}

void TopBar::setNumConnections(int count)
{
    if (count > 0) {
        if (!ui->pushButtonConnection->isChecked()) {
            ui->pushButtonConnection->setChecked(true);
            ui->pushButtonConnection->setButtonClassStyle("cssClass", "btn-check-connect", true);
        }
    } else {
        if (ui->pushButtonConnection->isChecked()) {
            ui->pushButtonConnection->setChecked(false);
            ui->pushButtonConnection->setButtonClassStyle("cssClass", "btn-check-connect-inactive", true);
        }
    }

    ui->pushButtonConnection->setButtonText(tr("%n active connection(s)", "", count));
}

void TopBar::setNumBlocks(int count)
{
    if (!clientModel)
        return;

    // Acquire current block source
    enum BlockSource blockSource = clientModel->getBlockSource();
    std::string text = "";
    switch (blockSource) {
    case BLOCK_SOURCE_NETWORK:
        text = "Synchronizing..";
        break;
    case BLOCK_SOURCE_DISK:
        text = "Importing blocks from disk..";
        break;
    case BLOCK_SOURCE_REINDEX:
        text = "Reindexing blocks on disk..";
        break;
    case BLOCK_SOURCE_NONE:
        // Case: not Importing, not Reindexing and no network connection
        text = "No block source available..";
        ui->pushButtonSync->setChecked(false);
        break;
    }

    bool needState = true;
    if (masternodeSync.IsBlockchainSynced() && clientModel && !clientModel->inInitialBlockDownload()) {
        // chain synced
        Q_EMIT walletSynced(true);
        if (masternodeSync.IsSynced()) {
            // Node synced
            const QString syncedText = tr("Synchronized - Block: %1").arg(QString::number(count));
            ui->pushButtonSync->setButtonText(syncedText);
            if (btnSyncStatusBox) {
                btnSyncStatusBox->setText(tr("✓ Synced · #%1").arg(QString::number(count)));
            }
            if (lblSyncPopupText) {
                lblSyncPopupText->setText(QString("<b>%1</b><br/>%2")
                    .arg(syncedText.toHtmlEscaped(), tr("Click to open Network & Sync Information").toHtmlEscaped()));
                if (syncInfoPopup && syncInfoPopup->isVisible()) {
                    positionSyncInfoPopup();
                }
            }
            progressBar->setRange(0, 100);
            progressBar->setValue(100);
            refreshStatus();
            return;
        } else {
            // TODO: Show out of sync warning
            int nAttempt = masternodeSync.RequestedMasternodeAttempt < MASTERNODE_SYNC_THRESHOLD ?
                               masternodeSync.RequestedMasternodeAttempt + 1 :
                               MASTERNODE_SYNC_THRESHOLD;
            int progress = nAttempt + (masternodeSync.RequestedMasternodeAssets - 1) * MASTERNODE_SYNC_THRESHOLD;
            if (progress >= 0) {
                // todo: MN progress..
                text = strprintf("%s - Block: %d", masternodeSync.GetSyncStatus(), count);
                //progressBar->setMaximum(4 * MASTERNODE_SYNC_THRESHOLD);
                //progressBar->setValue(progress);
                needState = false;
            }
        }
    } else {
        Q_EMIT walletSynced(false);
    }

    if (needState) {
        // Represent time from last generated block in human readable text
        QDateTime lastBlockDate = clientModel->getLastBlockDate();
        QDateTime currentDate = QDateTime::currentDateTime();
        int secs = lastBlockDate.secsTo(currentDate);

        QString timeBehindText;
        const int HOUR_IN_SECONDS = 60 * 60;
        const int DAY_IN_SECONDS = 24 * 60 * 60;
        const int WEEK_IN_SECONDS = 7 * 24 * 60 * 60;
        const int YEAR_IN_SECONDS = 31556952; // Average length of year in Gregorian calendar
        if (secs < 2 * DAY_IN_SECONDS) {
            timeBehindText = tr("%n hour(s)", "", secs / HOUR_IN_SECONDS);
        } else if (secs < 2 * WEEK_IN_SECONDS) {
            timeBehindText = tr("%n day(s)", "", secs / DAY_IN_SECONDS);
        } else if (secs < YEAR_IN_SECONDS) {
            timeBehindText = tr("%n week(s)", "", secs / WEEK_IN_SECONDS);
        } else {
            int years = secs / YEAR_IN_SECONDS;
            int remainder = secs % YEAR_IN_SECONDS;
            timeBehindText = tr("%1 and %2").arg(tr("%n year(s)", "", years)).arg(tr("%n week(s)", "", remainder / WEEK_IN_SECONDS));
        }
        QString timeBehind(" behind. Scanning block ");
        QString str = timeBehindText + timeBehind + QString::number(count);
        text = str.toStdString();

        progressBar->setMaximum(1000000000);
        progressBar->setValue(clientModel->getVerificationProgress() * 1000000000.0 + 0.5);
    }

    if (text.empty()) {
        text = "No block source available..";
    }

    const QString statusText = QString::fromStdString(text);
    ui->pushButtonSync->setButtonText(statusText);
    if (btnSyncStatusBox) {
        btnSyncStatusBox->setText(tr("↻ Syncing · #%1").arg(QString::number(count)));
    }
    if (lblSyncPopupText) {
        lblSyncPopupText->setText(QString("<b>%1</b><br/>%2")
            .arg(statusText.toHtmlEscaped(), tr("Click to open Network & Sync Information").toHtmlEscaped()));
        if (syncInfoPopup && syncInfoPopup->isVisible()) {
            positionSyncInfoPopup();
        }
    }
}

void TopBar::showUpgradeDialog()
{
    QString title = tr("Wallet Upgrade");
    if (ask(title,
            tr("Upgrading to HD wallet will improve\nthe wallet's reliability and security.\n\n\n"
               "NOTE: after the upgrade, a new\nbackup will be created.\n"))) {
        std::unique_ptr<WalletModel::UnlockContext> pctx = MakeUnique<WalletModel::UnlockContext>(walletModel->requestUnlock());
        if (!pctx->isValid()) {
            warn(tr("Upgrade Wallet"), tr("Wallet unlock cancelled"));
            return;
        }
        // Action performed on a separate thread, it's locking cs_main and cs_wallet.
        LoadingDialog* dialog = new LoadingDialog(window);
        dialog->execute(this, REQUEST_UPGRADE_WALLET, std::move(pctx));
        openDialogWithOpaqueBackgroundFullScreen(dialog, window);
    }
}

void TopBar::loadWalletModel()
{
    // Upgrade wallet.
    if (walletModel->isHDEnabled()) {
        ui->pushButtonHDUpgrade->setVisible(false);
    } else {
        connect(ui->pushButtonHDUpgrade, &ExpandableButton::Mouse_Pressed, this, &TopBar::showUpgradeDialog);

        // Upgrade wallet timer, only once. launched 4 seconds after the wallet started.
        QTimer::singleShot(4000, [this]() {
            showUpgradeDialog();
        });
    }

    connect(walletModel, &WalletModel::balanceChanged, this, &TopBar::updateBalances);
    connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &TopBar::updateDisplayUnit);
    connect(walletModel, &WalletModel::encryptionStatusChanged, this, &TopBar::refreshStatus);
    // Ask for passphrase if needed
    connect(walletModel, &WalletModel::requireUnlock, this, &TopBar::unlockWallet);
    // update the display unit, to not use the default ("KRISTA")
    updateDisplayUnit();

    refreshStatus();

    isInitializing = false;
}

void TopBar::updateTorIcon()
{
    std::string ip_port;
    bool torEnabled = clientModel->getTorInfo(ip_port);

    if (torEnabled) {
        if (!ui->pushButtonTor->isChecked()) {
            ui->pushButtonTor->setChecked(true);
            ui->pushButtonTor->setButtonClassStyle("cssClass", "btn-check-tor", true);
        }
        QString ip_port_q = QString::fromStdString(ip_port);
        ui->pushButtonTor->setButtonText(tr("Tor Active: %1").arg(ip_port_q));
    } else {
        if (ui->pushButtonTor->isChecked()) {
            ui->pushButtonTor->setChecked(false);
            ui->pushButtonTor->setButtonClassStyle("cssClass", "btn-check-tor-inactive", true);
            ui->pushButtonTor->setButtonText(tr("Tor Disabled"));
        }
    }
}

void TopBar::refreshMasternodeStatus()
{
    bool isSynced = masternodeSync.IsSynced();
    const int64_t nowMillis = GetTimeMillis();
    if (nLastMnStatusRefreshMillis == 0 || isSynced != fLastMnSynced || (nowMillis - nLastMnStatusRefreshMillis) >= 5000) {
        const dip3::CDeterministicMNList dip3List = deterministicMNManager
            ? deterministicMNManager->GetListAtChainTip()
            : dip3::CDeterministicMNList();
        std::set<COutPoint> seenDip3Collaterals;

        int nMNCount = 0;
        int nMNActive = 0;
        bool hasDip3Entries = false;

        for (auto mne : masternodeConfig.getEntries()) {
            nMNCount++;
            int nIndex;
            if (!mne.castOutputIndex(nIndex))
                continue;

            uint256 txHash(mne.getTxHash());
            CTxIn txIn(txHash, uint32_t(nIndex));
            const dip3::CDeterministicMN* dip3Mn = dip3List.GetMNByCollateral(txIn.prevout);
            if (dip3Mn) {
                seenDip3Collaterals.insert(txIn.prevout);
                hasDip3Entries = true;
                if (dip3Mn->state.IsValid()) {
                    nMNActive++;
                }
                continue;
            }
            if (!isSynced) continue;

            auto pmn = mnodeman.Find(txIn);
            if (!pmn) continue;

            int activeState = pmn->activeState;
            if (activeState == CMasternode::MASTERNODE_PRE_ENABLED || activeState == CMasternode::MASTERNODE_ENABLED) {
                nMNActive++;
            }
        }

        // Also include wallet-owned KTDIP3 deterministic masternodes registered via ProRegTx
        for (const auto& kv : dip3List.mapMNs) {
            const dip3::CDeterministicMN& dmn = kv.second;
            if (seenDip3Collaterals.count(dmn.collateralOutpoint)) {
                continue;
            }
            bool isMine = false;
            if (pwalletMain) {
                LOCK(pwalletMain->cs_wallet);
                isMine = pwalletMain->IsLockedCoin(dmn.collateralOutpoint.hash, dmn.collateralOutpoint.n)
                      || pwalletMain->HaveKey(dmn.state.pubKeyOwner.GetID())
                      || pwalletMain->HaveKey(dmn.state.keyIDVoting)
                      || (pwalletMain->GetWalletTx(dmn.collateralOutpoint.hash) != nullptr);
            }
            if (!isMine) continue;
            hasDip3Entries = true;
            nMNCount++;
            if (dmn.state.IsValid()) {
                nMNActive++;
            }
        }

        if ((isSynced || hasDip3Entries) && nMNCount > 0) {
            nLastMnStatusRefreshMillis = nowMillis;
            fLastMnSynced = isSynced;
            ui->labelMasternodeCount->setText(tr("%1/%2").arg(std::to_string(nMNActive).c_str()).arg(nMNCount));
            ui->labelMasternodesTitle->setText(tr("Masternodes"));
            if (ui->widgetMasternodesSummary) {
                ui->widgetMasternodesSummary->setToolTip(tr("Click to open Masternodes (%1/%2)").arg(nMNActive).arg(nMNCount));
            }
        } else {
            nLastMnStatusRefreshMillis = nowMillis;
            fLastMnSynced = isSynced;
            ui->labelMasternodeCount->setText(tr("%1/%2").arg(isSynced ? "0" : "--").arg(nMNCount));
            ui->labelMasternodesTitle->setText(tr("Masternodes%1").arg(isSynced ? "" : " (Syncing)"));
            if (ui->widgetMasternodesSummary) {
                ui->widgetMasternodesSummary->setToolTip(tr("Click to open Masternodes"));
            }
        }
    }

    const CBlockIndex* tip = chainActive.Tip();
    if (tip) {
        auto p = CMasternode::GetNextMasternodeCollateral(tip->nHeight);

        ui->widgetNextCollateral->setVisible(p.first > 0);
        if (p.first > 0) {
            ui->labelNextCollateralValue->setText(GUIUtil::formatBalance(p.second, nDisplayUnit));
            ui->labelNextCollateralBlocks->setText(tr("%1 Blocks").arg(p.first));
        }
    }
}

void TopBar::refreshStatus()
{
    refreshMasternodeStatus();

    // Check lock status
    if (!this->walletModel)
        return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (static_cast<int>(encStatus) != lastEncryptionStatus) {
        lastEncryptionStatus = static_cast<int>(encStatus);
        switch (encStatus) {
        case WalletModel::EncryptionStatus::Unencrypted:
            ui->pushButtonLock->setButtonText(tr("Wallet Unencrypted"));
            ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-unlock", true);
            break;
        case WalletModel::EncryptionStatus::Locked:
            ui->pushButtonLock->setButtonText(tr("Wallet Locked"));
            ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-lock", true);
            break;
        case WalletModel::EncryptionStatus::UnlockedForStaking:
            ui->pushButtonLock->setButtonText(tr("Wallet Unlocked for staking"));
            ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-staking", true);
            break;
        case WalletModel::EncryptionStatus::Unlocked:
            ui->pushButtonLock->setButtonText(tr("Wallet Unlocked"));
            ui->pushButtonLock->setButtonClassStyle("cssClass", "btn-check-status-unlock", true);
            break;
        }
    }

    // Collateral
    const CBlockIndex* tip = chainActive.Tip();
    if (tip) {
        ui->labelCollateralPiv->setText(GUIUtil::formatBalance(CMasternode::GetMasternodeNodeCollateral(tip->nHeight), nDisplayUnit));
    }
}

void TopBar::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        int displayUnitPrev = nDisplayUnit;
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (displayUnitPrev != nDisplayUnit)
            updateBalances(walletModel->GetWalletBalances());
    }
}

void TopBar::updateBalances(const interfaces::WalletBalances& newBalance)
{
    const CAmount nLockedBalance = newBalance.locked_balance;
    const CAmount nAvailableBalance = newBalance.balance - nLockedBalance;
    const CAmount nTotalBalance = newBalance.balance + newBalance.immature_balance;

    // KRISTA
    // Top persistent dual-balance summary (always visible across non-Home tabs)
    const QString strAvail = GUIUtil::formatBalance(nAvailableBalance, nDisplayUnit);
    const QString strLocked = GUIUtil::formatBalance(nLockedBalance, nDisplayUnit);
    const QString strImmature = GUIUtil::formatBalance(newBalance.immature_balance, nDisplayUnit);
    const QString strPending = GUIUtil::formatBalance(newBalance.unconfirmed_balance, nDisplayUnit);
    const QString strTotal = GUIUtil::formatBalance(nTotalBalance, nDisplayUnit);

    ui->labelAmountTopPiv->setText(strAvail);
    if (labelLockedTopPiv) {
        if (nLockedBalance > 0) {
            labelLockedTopPiv->setText(tr("🔒 %1 Locked").arg(strLocked));
        } else if (newBalance.immature_balance + newBalance.unconfirmed_balance > 0) {
            labelLockedTopPiv->setText(tr("⏳ %1 Pending").arg(
                GUIUtil::formatBalance(newBalance.immature_balance + newBalance.unconfirmed_balance, nDisplayUnit)));
        } else {
            labelLockedTopPiv->setText(tr("Total: %1").arg(strTotal));
        }
    }

    const QString balTooltip = tr(
        "<qt><b>Wallet Balance Breakdown</b><br/>"
        "• <b>Total:</b> %1<br/>"
        "• <b>Available (Spendable):</b> %2<br/>"
        "• <b>Locked Collateral (MN / Coin-Lock / PoW-Lock):</b> %3<br/>"
        "• <b>Immature (100-block maturity):</b> %4<br/>"
        "• <b>Pending (Unconfirmed):</b> %5</qt>")
        .arg(strTotal, strAvail, strLocked, strImmature, strPending);
    ui->labelAmountTopPiv->setToolTip(balTooltip);
    if (labelLockedTopPiv) {
        labelLockedTopPiv->setToolTip(balTooltip);
    }

    // Expanded (Home header)
    ui->labelAmountPiv->setText(strTotal);
    ui->labelAmountPiv->setToolTip(balTooltip);
    ui->labelAvailablePiv->setText(strAvail);
    ui->labelAvailablePiv->setToolTip(tr("Spendable balance ready for transactions and staking"));
    ui->labelPendingPiv->setText(strPending);
    ui->labelPendingPiv->setToolTip(tr("Unconfirmed incoming balance awaiting block confirmation"));
    ui->labelImmaturePiv->setText(strImmature);
    ui->labelImmaturePiv->setToolTip(tr("Newly mined/staked rewards awaiting 100-block maturity"));
    ui->labelLockedPiv->setText(strLocked);
    ui->labelLockedPiv->setToolTip(tr("Locked collateral (Masternodes, Coin-Lock & PoW-Lock) — click to open Miner Hub"));

    refreshMasternodeStatus();

    // Collateral
    const CBlockIndex* tip = chainActive.Tip();
    if (tip) {
        ui->labelCollateralPiv->setText(GUIUtil::formatBalance(CMasternode::GetMasternodeNodeCollateral(tip->nHeight), nDisplayUnit));
    }
}

void TopBar::resizeEvent(QResizeEvent* event)
{
    if (lockUnlockWidget && lockUnlockWidget->isVisible()) lockDropdownMouseLeave();
    QWidget::resizeEvent(event);
}

void TopBar::refreshProgressBarSize()
{
    QMetaObject::invokeMethod(this, "expandSync", Qt::QueuedConnection);
}

void TopBar::expandSync()
{
    if (progressBar) {
        progressBar->setMaximumWidth(ui->pushButtonSync->maximumWidth());
        progressBar->setFixedWidth(ui->pushButtonSync->width());
        progressBar->setMinimumWidth(ui->pushButtonSync->width() - 2);
    }
}

void TopBar::updateHDState(const bool& upgraded, const QString& upgradeError)
{
    if (upgraded) {
        ui->pushButtonHDUpgrade->setVisible(false);
        if (ask("HD Upgrade Complete", tr("The wallet has been successfully upgraded to HD.") + "\n" +
                                           tr("It is advised to make a backup.") + "\n\n" + tr("Do you wish to backup now?") + "\n\n")) {
            // backup wallet
            QString filename = GUIUtil::getSaveFileName(this,
                tr("Backup Wallet"), QString(),
                tr("Wallet Data (*.dat)"), NULL);
            if (!filename.isEmpty()) {
                inform(walletModel->backupWallet(filename) ? tr("Backup created") : tr("Backup creation failed"));
            } else {
                warn(tr("Backup creation failed"), tr("no file selected"));
            }
        } else {
            inform(tr("Wallet upgraded successfully, but no backup created.") + "\n" +
                   tr("WARNING: remember to make a copy of your wallet file!"));
        }
    } else {
        warn(tr("Upgrade Wallet Error"), upgradeError);
    }
}

void TopBar::run(int type)
{
    if (type == REQUEST_UPGRADE_WALLET) {
        std::string upgradeError;
        bool ret = this->walletModel->upgradeWallet(upgradeError);
        QMetaObject::invokeMethod(this,
            "updateHDState",
            Qt::QueuedConnection,
            Q_ARG(bool, ret),
            Q_ARG(QString, QString::fromStdString(upgradeError)));
    }
}

void TopBar::onError(QString error, int type)
{
    if (type == REQUEST_UPGRADE_WALLET) {
        warn(tr("Upgrade Wallet Error"), error);
    }
}

void TopBar::selectTab(QString name)
{
    QToolButton* target = nullptr;
    if (name == "dash") target = btnDashboard;
    else if (name == "send") target = btnSend;
    else if (name == "receive") target = btnReceive;
    else if (name == "minerhub") target = btnMinerHub;
    else if (name == "smartcontract") target = btnSmartContract;
    else if (name == "master") target = btnMaster;
    else if (name == "address") target = btnAddresses;
    else if (name == "stats") target = btnStats;
    else if (name == "settings") target = btnSettings;

    if (target) {
        onNavSelected(target);
    } else {
        onNavSelected(nullptr);
    }
}

void TopBar::onDashboardClicked() {
    window->goToDashboard();
    onNavSelected(btnDashboard);
}

void TopBar::onSendClicked() {
    window->goToSend();
    onNavSelected(btnSend);
}

void TopBar::onReceiveClicked() {
    window->goToReceive();
    onNavSelected(btnReceive);
}

void TopBar::onMinerHubClicked() {
    window->goToMinerHub();
    onNavSelected(btnMinerHub);
}

void TopBar::onSmartContractClicked() {
    window->goToSmartContract();
    onNavSelected(btnSmartContract);
}

void TopBar::onMasterClicked() {
    window->goToMasterNodes();
    onNavSelected(btnMaster);
}

void TopBar::onAddressesClicked() {
    window->goToAddresses();
    onNavSelected(btnAddresses);
}

void TopBar::onStatsClicked() {
    window->goToStats();
    onNavSelected(btnStats);
}

void TopBar::onSettingsClicked() {
    window->goToSettings();
    onNavSelected(btnSettings);
}

void TopBar::onNavSelected(QToolButton* active)
{
    for (QToolButton* btn : navBtns) {
        if (btn) {
            btn->setChecked(btn == active);
        }
    }
    forceUpdateStyle({
        btnDashboard,
        btnSend,
        btnReceive,
        btnMinerHub,
        btnSmartContract,
        btnMaster,
        btnAddresses,
        btnStats,
        btnSettings
    });
}

void TopBar::positionSyncInfoPopup()
{
    if (!syncInfoPopup || !btnSyncStatusBox || !window) {
        return;
    }
    syncInfoPopup->adjustSize();
    QPoint belowBtn = btnSyncStatusBox->mapTo(window, QPoint(btnSyncStatusBox->width() - syncInfoPopup->width(), btnSyncStatusBox->height() + 6));
    const int minX = 8;
    const int maxX = std::max(minX, window->width() - syncInfoPopup->width() - 8);
    belowBtn.setX(std::max(minX, std::min(maxX, belowBtn.x())));
    syncInfoPopup->move(belowBtn);
}

bool TopBar::eventFilter(QObject* obj, QEvent* event)
{
    if (event && event->type() == QEvent::Enter && (obj == btnSyncStatusBox || obj == syncInfoPopup) && syncInfoPopup) {
        positionSyncInfoPopup();
        syncInfoPopup->show();
        syncInfoPopup->raise();
        return false;
    }
    if (event && event->type() == QEvent::Leave && (obj == btnSyncStatusBox || obj == syncInfoPopup) && syncInfoPopup) {
        QTimer::singleShot(120, this, [this]() {
            if (syncInfoPopup && btnSyncStatusBox &&
                !btnSyncStatusBox->underMouse() && !syncInfoPopup->underMouse()) {
                syncInfoPopup->hide();
            }
        });
        return false;
    }
    if (event && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            if (obj == lblSyncPopupText) {
                window->goToSettingsInfo();
                return true;
            }
            if (obj == ui->widgetMasternodesSummary) {
                onMasterClicked();
                return true;
            }
            if (obj == ui->labelLockedPiv || obj == labelLockedTopPiv) {
                onMinerHubClicked();
                return true;
            }
        }
    }
    return PWidget::eventFilter(obj, event);
}
