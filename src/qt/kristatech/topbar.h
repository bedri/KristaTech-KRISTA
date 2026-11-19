// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOPBAR_H
#define TOPBAR_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/lockunlock.h"
#include "amount.h"
#include <QTimer>
#include <QLabel>
#include <QFrame>
#include <QProgressBar>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QToolButton>

class KRISTATECHGUI;
class WalletModel;
class ClientModel;

namespace Ui {
class TopBar;
}

class TopBar : public PWidget
{
    Q_OBJECT

public:
    explicit TopBar(KRISTATECHGUI* _mainWindow, QWidget *parent = nullptr);
    ~TopBar();

    void showTop();
    void showBottom();

    void loadWalletModel() override;
    void loadClientModel() override;

    void openPassPhraseDialog(AskPassphraseDialog::Mode mode, AskPassphraseDialog::Context ctx);
    void encryptWallet();
    void showUpgradeDialog();

    void run(int type) override;
    void onError(QString error, int type) override;
    void unlockWallet();
    void selectTab(QString name);

public Q_SLOTS:
    void updateBalances(const interfaces::WalletBalances& newBalance);
    void updateDisplayUnit();

    void setNumConnections(int count);
    void setNumBlocks(int count);
    void setStakingStatusActive(bool fActive);
    void updateStakingStatus();
    void updateHDState(const bool& upgraded, const QString& upgradeError);

Q_SIGNALS:
    void themeChanged(bool isLight);
    void walletSynced(bool isSync);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
protected Q_SLOTS:
    void changeTheme(bool isLightTheme, QString& theme) override;
private Q_SLOTS:
    void onBtnReceiveClicked();
    void onThemeClicked();
    void onBtnLockClicked();
    void lockDropdownMouseLeave();
    void lockDropdownClicked(const StateClicked&);
    void refreshStatus();
    void refreshMasternodeStatus(bool force = false);
    void openLockUnlock();
    void onBtnConfClicked();
    void onBtnMasternodesClicked();
    void refreshProgressBarSize();
    void expandSync();

private Q_SLOTS:
    void onDashboardClicked();
    void onSendClicked();
    void onReceiveClicked();
    void onMinerHubClicked();
    void onSmartContractClicked();
    void onMasterClicked();
    void onAddressesClicked();
    void onStatsClicked();
    void onSettingsClicked();

private:
    Ui::TopBar *ui;
    LockUnlock *lockUnlockWidget = nullptr;
    QProgressBar* progressBar = nullptr;
    QLabel* labelLockedTopPiv = nullptr;
    QPushButton* btnSyncStatusBox = nullptr;
    QFrame* syncInfoPopup = nullptr;
    QLabel* lblSyncPopupText = nullptr;
    QFrame* rightToolsDivider = nullptr;
    void positionSyncInfoPopup();

    QPushButton* imgLogo = nullptr;
    QToolButton* btnDashboard = nullptr;
    QToolButton* btnSend = nullptr;
    QToolButton* btnReceive = nullptr;
    QToolButton* btnMinerHub = nullptr;
    QToolButton* btnSmartContract = nullptr;
    QToolButton* btnMaster = nullptr;
    QToolButton* btnAddresses = nullptr;
    QToolButton* btnStats = nullptr;
    QToolButton* btnSettings = nullptr;
    QList<QToolButton*> navBtns;
    void onNavSelected(QToolButton* active);
    void updateNavButtonWidths();
    void updateTopBalancePillStyle();

    int nDisplayUnit = -1;
    QTimer* timerStakingIcon = nullptr;
    bool isInitializing = true;
    int lastEncryptionStatus = -1;
    int64_t nLastMnStatusRefreshMillis = 0;
    bool fLastMnSynced = false;

    void updateTorIcon();
};

#endif // TOPBAR_H
