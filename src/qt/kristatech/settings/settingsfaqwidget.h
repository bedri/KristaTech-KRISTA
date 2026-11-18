// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSFAQWIDGET_H
#define SETTINGSFAQWIDGET_H

#include "qt/kristatech/pwidget.h"

class KRISTATECHGUI;
class QLabel;
class QLineEdit;
class QPushButton;

namespace Ui {
class SettingsFaqWidget;
}

class SettingsFaqWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsFaqWidget(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsFaqWidget();

    void showEvent(QShowEvent *event) override;
    void changeTheme(bool isLightTheme, QString& theme) override;

public Q_SLOTS:
    void setSection(int num);

private Q_SLOTS:
    void onFaqClicked(const QWidget* const widget);
    void onSearchTextChanged(const QString& text);

private:
    Ui::SettingsFaqWidget *ui;
    QLineEdit* searchEdit = nullptr;
    QLabel* labelNoResults = nullptr;
    int pos = 0;

    std::vector<QPushButton*> getButtons();
    void applyThemeStyles(bool isLightTheme);
};

#endif // SETTINGSFAQWIDGET_H
