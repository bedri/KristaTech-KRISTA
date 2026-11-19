// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSWALLETOPTIONSWIDGET_H
#define SETTINGSWALLETOPTIONSWIDGET_H

#include <QWidget>
#include <QDataWidgetMapper>
#include "qt/kristatech/pwidget.h"
namespace Ui {
class SettingsWalletOptionsWidget;
}

class SettingsWalletOptionsWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsWalletOptionsWidget(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsWalletOptionsWidget();

    void setMapper(QDataWidgetMapper *mapper);
    void setSpinBoxStakeSplitThreshold(double val);

Q_SIGNALS:
    void saveSettings();
    void discardSettings();

public Q_SLOTS:
    void onResetClicked();

protected:
    void showEvent(QShowEvent* event) override;

private Q_SLOTS:
    void changeTheme(bool isLightTheme, QString& theme) override;

private:
    Ui::SettingsWalletOptionsWidget *ui;
    QWidget* modernRoot = nullptr;
    void applyTheme(bool isLight);
    void updateProxyFieldsEnabled(bool enabled);
};

#endif // SETTINGSWALLETOPTIONSWIDGET_H
