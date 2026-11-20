// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSDISPLAYOPTIONSWIDGET_H
#define SETTINGSDISPLAYOPTIONSWIDGET_H

#include <QWidget>
#include <QDataWidgetMapper>
#include "qt/kristatech/pwidget.h"

class QComboBox;

namespace Ui {
class SettingsDisplayOptionsWidget;
}

class SettingsDisplayOptionsWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsDisplayOptionsWidget(KRISTATECHGUI* _window = nullptr, QWidget *parent = nullptr);
    ~SettingsDisplayOptionsWidget();

    void setMapper(QDataWidgetMapper *mapper);
    void initLanguages();
    void syncLanguageFromModel();
    void loadClientModel() override;

public Q_SLOTS:
    void onResetClicked();

Q_SIGNALS:
    void saveSettings();
    void discardSettings();

private Q_SLOTS:
    void changeTheme(bool isLightTheme, QString& theme) override;

private:
    Ui::SettingsDisplayOptionsWidget *ui;
    QWidget* modernRoot = nullptr;
    QComboBox* comboBoxTheme = nullptr;
    void applyTheme(bool isLight);
};

#endif // SETTINGSDISPLAYOPTIONSWIDGET_H
