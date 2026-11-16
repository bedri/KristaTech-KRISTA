// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSWALLETREPAIRWIDGET_H
#define SETTINGSWALLETREPAIRWIDGET_H

#include <QWidget>
#include <QFrame>
#include <QLabel>
#include <QPushButton>
#include "qt/kristatech/pwidget.h"

namespace Ui {
class SettingsWalletRepairWidget;
}

class SettingsWalletRepairWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsWalletRepairWidget(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsWalletRepairWidget();

    void loadWalletModel() override;

    /** Build parameter list for restart */
    void buildParameterlist(QString arg);

Q_SIGNALS:
    /** Get restart command-line parameters and handle restart */
    void handleRestart(QStringList args);

public Q_SLOTS:
    void backupWalletNow();
    void updateSafetyBanner();
    void walletSalvage();
    void walletRescan();
    void walletZaptxes1();
    void walletZaptxes2();
    void walletUpgrade();
    void walletReindex();
    void walletResync();

protected Q_SLOTS:
    void changeTheme(bool isLightTheme, QString &theme) override;

private:
    Ui::SettingsWalletRepairWidget *ui;
    QFrame* cardSafety = nullptr;
    QFrame* cardTier1 = nullptr;
    QFrame* cardTier2 = nullptr;
    QFrame* cardTier3 = nullptr;
    QLabel* labelEncryptionBadge = nullptr;
    QLabel* labelDataDirValue = nullptr;
    QPushButton* btnQuickBackup = nullptr;
    QWidget* upgradeWalletRow = nullptr;

    bool confirmRepairAction(const QString& title, const QString& htmlBody, const QString& okText, bool isDanger = false);
    void applyTierCardTheme(bool isLight);
};

#endif // SETTINGSWALLETREPAIRWIDGET_H
