// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSABOUTWIDGET_H
#define SETTINGSABOUTWIDGET_H

#include "qt/kristatech/pwidget.h"

#include <QStringList>
#include <vector>

class QFrame;
class QLabel;
class QPushButton;
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
    QWidget* modernRoot = nullptr;
    QPushButton* copyVersionBtn = nullptr;
    QFrame* heroCard = nullptr;
    QLabel* logoLabel = nullptr;
    QLabel* taglineLabel = nullptr;
    std::vector<QLabel*> heroBadges;
    QFrame* licenseCard = nullptr;
    QFrame* licenseAlertBox = nullptr;
    QLabel* licenseSummaryLabel = nullptr;
    QLabel* creditsHeaderLabel = nullptr;
    std::vector<QLabel*> creditLabels;
    std::vector<QPushButton*> linkRowButtons;
    std::vector<QLabel*> linkTitleLabels;
    std::vector<QLabel*> linkDescLabels;
    std::vector<QLabel*> linkArrowLabels;

    QStringList parsedLicenseNotes;
    QStringList parsedCopyrights;
    QString rawAboutText;

    void applyThemeStyles(bool isLight);
};

#endif // SETTINGSABOUTWIDGET_H
