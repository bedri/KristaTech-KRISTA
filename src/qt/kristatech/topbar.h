// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef TOPBAR_H
#define TOPBAR_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/lockunlock.h"
#include "amount.h"
#include <QTimer>
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
private Q_SLOTS:
    void onBtnReceiveClicked();
    void onThemeClicked();
    void onBtnLockClicked();
    void lockDropdownMouseLeave();
    void lockDropdownClicked(const StateClicked&);
    void refreshStatus();
    void refreshMasternodeStatus();
    void openLockUnlock();
    void onBtnConfClicked();
    void onBtnMasternodesClicked();
    void refreshProgressBarSize();
    void expandSync();

private Q_SLOTS:
    void onDashboardClicked();
    void onSendClicked();
    void onReceiveClicked();
    void onSmartContractClicked();
    void onMasterClicked();
    void onSettingsClicked();

private:
    Ui::TopBar *ui;
    LockUnlock *lockUnlockWidget = nullptr;
    QProgressBar* progressBar = nullptr;

    QPushButton* imgLogo = nullptr;
    QToolButton* btnDashboard = nullptr;
    QToolButton* btnSend = nullptr;
    QToolButton* btnReceive = nullptr;
    QToolButton* btnSmartContract = nullptr;
    QToolButton* btnMaster = nullptr;
    QToolButton* btnSettings = nullptr;
    QList<QToolButton*> navBtns;
    void onNavSelected(QToolButton* active);

    int nDisplayUnit = -1;
    QTimer* timerStakingIcon = nullptr;
    bool isInitializing = true;

    void updateTorIcon();
};

#endif // TOPBAR_H
