// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSFAQWIDGET_H
#define SETTINGSFAQWIDGET_H

#include "qt/kristatech/pwidget.h"
#include <functional>
#include <utility>
#include <vector>

class KRISTATECHGUI;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QWidget;

class SettingsWidget;

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
    void onToggleAllClicked();
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
        QPushButton* headerBtn{nullptr};
        QLabel* titleLabel{nullptr};
        QLabel* catTagLabel{nullptr};
        QLabel* chevronLabel{nullptr};
        QFrame* tldrBox{nullptr};
        QLabel* tldrLabel{nullptr};
        QWidget* detailsContainer{nullptr};
        QLabel* bodyLabel{nullptr};
        QPushButton* actionBtn{nullptr};
        QPushButton* techToggleBtn{nullptr};
        QFrame* techContainer{nullptr};
        QLabel* techLabel{nullptr};
        QPushButton* tocButton{nullptr};
        std::function<QString(bool)> bodyBuilder;
        std::function<QString(bool)> techBuilder;
        bool userExpanded{false};
        bool userTechExpanded{false};
        bool currentLight{false};
    };

    struct TocGroupHeader {
        QString categoryKey;
        QLabel* label{nullptr};
    };

    Ui::SettingsFaqWidget *ui;
    QLineEdit* searchEdit = nullptr;
    QLabel* labelSubtitle = nullptr;
    QLabel* labelResultCount = nullptr;
    QPushButton* btnToggleAll = nullptr;
    QLabel* labelNoResults = nullptr;
    QWidget* filterBarWidget = nullptr;
    QFrame* popularBarWidget = nullptr;
    QLabel* popularTitleLabel = nullptr;
    std::vector<QPushButton*> popularButtons;
    QScrollArea* tocScrollArea = nullptr;
    QWidget* tocContentWidget = nullptr;
    std::vector<std::pair<QString, QPushButton*>> categoryFilterButtons;
    std::vector<TocGroupHeader> tocHeaders;
    std::vector<FaqItemWidgets> faqItems;
    QString activeCategory = QStringLiteral("all");
    bool isProgrammaticScroll = false;
    int pos = 0;

    std::vector<QPushButton*> getButtons();
    SettingsWidget* findSettingsWidget() const;
    void buildFaqContent();
    void setCardExpanded(FaqItemWidgets& item, bool expanded, bool updateUserState);
    void setTechExpanded(FaqItemWidgets& item, bool expanded, bool updateUserState);
    void jumpToGuide(int num);
    void updateToggleAllLabel();
    void applyFilters();
    void applyThemeStyles(bool isLightTheme);
};

#endif // SETTINGSFAQWIDGET_H
