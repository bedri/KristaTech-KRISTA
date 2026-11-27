// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSBACKUPWALLET_H
#define SETTINGSBACKUPWALLET_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"

class QFrame;
class QLabel;

namespace Ui {
class SettingsBackupWallet;
}

class SettingsBackupWallet : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsBackupWallet(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsBackupWallet();

    void loadWalletModel() override;
    void changeTheme(bool isLightTheme, QString& theme) override;

private Q_SLOTS:
    void selectFileOutput();
    void changePassphrase();
    void updateEncryptionUI();

private:
    void applyTheme(bool isLight);
    void updateBackupStatusLabel();

    Ui::SettingsBackupWallet *ui;
    QWidget* modernRoot{nullptr};
    QLabel* lblBackupPath{nullptr};
    QLabel* lblEncTitle{nullptr};
    QLabel* lblEncBadge{nullptr};
    QFrame* encAlertBox{nullptr};
    QLabel* lblEncAlert{nullptr};
};

#endif // SETTINGSBACKUPWALLET_H
