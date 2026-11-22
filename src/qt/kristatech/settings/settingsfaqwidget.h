// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSFAQWIDGET_H
#define SETTINGSFAQWIDGET_H

#include "qt/kristatech/pwidget.h"
#include <functional>
#include <vector>
#include <utility>

class KRISTATECHGUI;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;

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
    void onCategoryFilterClicked(const QString& categoryKey);
    void onFaqScrolled(int value);

private:
    struct FaqItemWidgets {
        int number{0};
        QString categoryKey;
        QString categoryLabel;
        QString tocTitle;
        QString title;
        QString tldr;
        QString keywords;
        QWidget* card{nullptr};
        QLabel* badge{nullptr};
        QLabel* titleLabel{nullptr};
        QLabel* catTagLabel{nullptr};
        QFrame* tldrBox{nullptr};
        QLabel* tldrLabel{nullptr};
        QLabel* bodyLabel{nullptr};
        QPushButton* tocButton{nullptr};
        std::function<QString(bool)> bodyBuilder;
    };

    struct TocGroupHeader {
        QString categoryKey;
        QLabel* label{nullptr};
    };

    Ui::SettingsFaqWidget *ui;
    QLineEdit* searchEdit = nullptr;
    QLabel* labelSubtitle = nullptr;
    QLabel* labelResultCount = nullptr;
    QLabel* labelNoResults = nullptr;
    QWidget* filterBarWidget = nullptr;
    QScrollArea* tocScrollArea = nullptr;
    QWidget* tocContentWidget = nullptr;
    std::vector<std::pair<QString, QPushButton*>> categoryFilterButtons;
    std::vector<TocGroupHeader> tocHeaders;
    std::vector<FaqItemWidgets> faqItems;
    QString activeCategory = QStringLiteral("all");
    bool isProgrammaticScroll = false;
    int pos = 0;

    std::vector<QPushButton*> getButtons();
    void buildFaqContent();
    void applyFilters();
    void applyThemeStyles(bool isLightTheme);
};

#endif // SETTINGSFAQWIDGET_H
