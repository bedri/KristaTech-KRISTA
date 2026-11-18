// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSABOUTWIDGET_H
#define SETTINGSABOUTWIDGET_H

#include "qt/kristatech/pwidget.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QWidget;

class SettingsAboutWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsAboutWidget(KRISTATECHGUI* _window, QWidget* parent = nullptr);
    ~SettingsAboutWidget() override = default;

    void changeTheme(bool isLightTheme, QString& theme) override;

private Q_SLOTS:
    void onCopyVersionClicked();

private:
    QLabel* logoLabel = nullptr;
    QLabel* titleLabel = nullptr;
    QLabel* versionBadgeLabel = nullptr;
    QPushButton* copyVersionBtn = nullptr;
    QScrollArea* scrollArea = nullptr;
    QWidget* aboutCard = nullptr;
    QLabel* aboutMessageLabel = nullptr;
    QString rawAboutText;

    void applyThemeStyles(bool isLight);
};

#endif // SETTINGSABOUTWIDGET_H
