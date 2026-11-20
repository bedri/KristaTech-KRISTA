// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODESWIDGET_H
#define MASTERNODESWIDGET_H

#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/furabstractlistitemdelegate.h"
#include "qt/kristatech/mnmodel.h"
#include "qt/kristatech/tooltipmenu.h"
#include "walletmodel.h"

#include <atomic>
#include <vector>

#include <QTimer>
#include <QWidget>

class KRISTATECHGUI;
class MNFilterProxyModel;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Ui {
class MasterNodesWidget;
}

QT_BEGIN_NAMESPACE
class QModelIndex;
QT_END_NAMESPACE

class MasterNodesWidget : public PWidget
{
    Q_OBJECT

public:

    explicit MasterNodesWidget(KRISTATECHGUI *parent = nullptr);
    ~MasterNodesWidget();

    void loadWalletModel() override;

    void run(int type) override;
    void onError(QString error, int type) override;

    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private Q_SLOTS:
    void onCreateMNClicked();
    void onStartAllClicked(int type);
    void changeTheme(bool isLightTheme, QString &theme) override;
    void onMNClicked(const QModelIndex &index);
    void onEditMNClicked();
    void onDeleteMNClicked();
    void onInfoMNClicked();
    void updateListState();
    void updateModelAndInform(QString informText);

private:
    Ui::MasterNodesWidget *ui;
    FurAbstractListItemDelegate *delegate;
    MNModel *mnModel = nullptr;
    MNFilterProxyModel *proxyModel = nullptr;
    TooltipMenu* menu = nullptr;
    QModelIndex index;
    QTimer *timer = nullptr;

    QWidget* mnPageRoot = nullptr;
    QLabel* lblPageTitle = nullptr;
    QLabel* lblPageSubtitle = nullptr;
    QPushButton* btnOpenActiveMNConf = nullptr;
    QPushButton* btnOpenControllerMNConf = nullptr;
    QPushButton* btnRefreshMNs = nullptr;

    QLabel* lblKpiTotalVal = nullptr;
    QLabel* lblKpiTotalSub = nullptr;
    QLabel* lblKpiEnabledVal = nullptr;
    QLabel* lblKpiEnabledSub = nullptr;
    QLabel* lblKpiInactiveVal = nullptr;
    QLabel* lblKpiInactiveSub = nullptr;
    QLabel* lblKpiLockedVal = nullptr;
    QLabel* lblKpiLockedSub = nullptr;

    QLabel* lblListSectionTitle = nullptr;
    QLabel* lblShownCount = nullptr;
    QPushButton* btnScopeMine = nullptr;
    QPushButton* btnScopeAll = nullptr;
    int currentScopeMode = 0;

    QLineEdit* lineEditSearchMN = nullptr;
    QComboBox* comboStatusFilter = nullptr;
    QLabel* lblFilteredEmpty = nullptr;
    QLabel* lblRule8Note = nullptr;

    std::atomic<bool> isLoading;
    std::vector<CMasternodeConfig::CMasternodeEntry> m_pendingStartEntries;
    int m_pendingPreFailed{0};

    void applyModernTheme(bool isLight);
    void setScopeMode(int mode);
    void updateSummaryChips();
    bool checkMNsNetwork();
    void startAlias(QString strAlias);
    bool startAll(QString& failedMN, bool onlyMissing);
    bool startMN(CMasternodeConfig::CMasternodeEntry mne, std::string& strError);
};

#endif // MASTERNODESWIDGET_H
