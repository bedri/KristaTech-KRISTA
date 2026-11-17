// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MINERHUBWIDGET_H
#define MINERHUBWIDGET_H

#include "qt/kristatech/pwidget.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>

class KRISTATECHGUI;
class WalletModel;

/**
 * Dedicated top-level Miner & Validator Hub page extracted from DashboardWidget
 * to keep Home UI and mining/registration/RPC logic cleanly separated.
 */
class MinerHubWidget : public PWidget
{
    Q_OBJECT

public:
    explicit MinerHubWidget(KRISTATECHGUI* _window, QWidget* parent = nullptr);
    ~MinerHubWidget();

    void loadWalletModel() override;
    void shutdownMinerWorkers();
    void setMinerHubFullScreenMode(bool enable);
    bool isMinerHubFullScreenMode() const { return fMinerHubFullScreen; }

public Q_SLOTS:
    void changeTheme(bool isLightTheme, QString& theme) override;
    void refreshMinerHubStatus();

Q_SIGNALS:
    void homeStatusPillUpdated(const QString& text, const QString& styleSheet);

private Q_SLOTS:
    void onToggleMiningClicked();
    void onToggleMinerHubFullScreenClicked();
    void onRenewLockClicked();
    void onMinerThreadsChanged(int nThreads);
    void onRegisterPoWClicked();
    void onPoWRegistrationFinished(bool success, const QString& resultOrError);
    void onRegisterCoinLockClicked();
    void startPoWRegistration(const QString& targetAddress = QString());
    void startCoinLockRegistration(const QString& targetAddress = QString());

private:
    void initMinerHub();
    void updateMinerHubTheme();
    void updateMinerStepperButtons();

    QFrame* minerHubCard = nullptr;
    QLabel* lblMinerHubTitle = nullptr;
    QLabel* lblMinerHubSubtitle = nullptr;
    QLabel* lblMinerStatusBadge = nullptr;
    QPushButton* btnFullScreenMinerHub = nullptr;
    QPushButton* btnToggleTrackReg = nullptr;
    QPushButton* btnFilterAll = nullptr;
    QPushButton* btnFilterActive = nullptr;
    QPushButton* btnFilterElected = nullptr;
    int minerRosterFilter = 0; // 0 = All, 1 = Active, 2 = Elected
    bool fTrackRegVisible = true;
    QShortcut* shortcutMinerFullScreenEsc = nullptr;
    QShortcut* shortcutMinerFullScreenF11 = nullptr;
    QFrame* coordVrfStripFrame = nullptr;
    QLabel* lblCoordVrfSummary = nullptr;
    QLabel* lblMatureUtxoSummary = nullptr;
    QFrame* lockExpiryBanner = nullptr;
    QLabel* lblLockExpiryIcon = nullptr;
    QLabel* lblLockExpiryWarning = nullptr;
    QPushButton* btnRenewLock = nullptr;
    QWidget* minerRegRowWidget = nullptr;
    QFrame* cardTrackCoinLock = nullptr;
    QFrame* cardTrackPoWLock = nullptr;
    QFrame* cardTrackZeroPoW = nullptr;
    QLabel* lblTrackCoinLockMeta = nullptr;
    QLabel* lblTrackPoWLockMeta = nullptr;
    QLabel* lblTrackZeroPoWMeta = nullptr;
    QLabel* lblMinerThreads = nullptr;
    QFrame* minerThreadsStepperFrame = nullptr;
    QPushButton* btnMinerThreadsDec = nullptr;
    QSpinBox* spinMinerThreads = nullptr;
    QPushButton* btnMinerThreadsInc = nullptr;
    QPushButton* btnToggleMining = nullptr;
    QLabel* lblTrackSection = nullptr;
    QPushButton* btnRegPoW = nullptr;
    QPushButton* btnRegZeroPoW = nullptr;
    QPushButton* btnRegCoinLock = nullptr;
    QFrame* statsStripFrame = nullptr;
    QLabel* lblStatMyMinersTitle = nullptr;
    QLabel* lblStatMyMinersVal = nullptr;
    QLabel* lblStatHitsTitle = nullptr;
    QLabel* lblStatHitsVal = nullptr;
    QLabel* lblStatEarnedTitle = nullptr;
    QLabel* lblStatEarnedVal = nullptr;
    QLabel* lblStatImmatureTitle = nullptr;
    QLabel* lblStatImmatureVal = nullptr;
    QLabel* lblRosterSection = nullptr;
    QTableWidget* tableMyMiners = nullptr;
    QTimer* minerStatusTimer = nullptr;
    bool fMinerHubFullScreen = false;
    int pendingRenewTrack = 1; // 1 = PoW-Lock / Zero-Coin, 2 = Coin-Lock (250 KRISTA)
    int cachedMinerStatsHeight = -1;
    size_t cachedMinerStatsWalletSize = 0;
    bool cachedMinerGenerating = false;
    uint64_t cachedExtMinerDigest = 0;
    bool fForceMinerTableRefresh = true;
    std::atomic<bool> fShuttingDownMinerHub{false};
    std::atomic<bool> isSolvingPoWReg{false};
    std::thread powRegWorkerThread;
    std::shared_ptr<void> powRegUnlockGuard;
};

#endif // MINERHUBWIDGET_H
