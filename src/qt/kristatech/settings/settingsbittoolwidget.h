// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSBITTOOLWIDGET_H
#define SETTINGSBITTOOLWIDGET_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/contactsdropdown.h"
#include "key.h"

class QAction;
class QStackedWidget;

namespace Ui {
class SettingsBitToolWidget;
}

class SettingsBitToolWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsBitToolWidget(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsBitToolWidget();
    void changeTheme(bool isLightTheme, QString& theme) override;
protected:
    void resizeEvent(QResizeEvent *event) override;
public Q_SLOTS:
    void onEncryptSelected(bool isEncr);
    void setAddress_ENC(const QString& address);
    void onEncryptKeyButtonENCClicked();
    void onClearAll();
    void onClearDecrypt();
    void onAddressesClicked();
    void onDecryptClicked();
    void importAddressFromDecKey();

private:
    void applyTheme(bool isLight);
    void setEncStatus(const QString& text, int tone);
    void clearEncStatus();
    void setDecStatus(const QString& text, int tone);
    void clearDecStatus();
    void resizeMenu();

    Ui::SettingsBitToolWidget *ui;
    QAction *btnContact{nullptr};
    QAction *actToggleEncPass{nullptr};
    QAction *actToggleDecPass{nullptr};
    ContactsDropdown *menuContacts = nullptr;
    QWidget* modernRoot{nullptr};
    QStackedWidget* modeStack{nullptr};
    bool encPassVisible{false};
    bool decPassVisible{false};
    bool hasEncStatus{false};
    int encTone{0};
    bool hasDecStatus{false};
    int decTone{0};

    // Cached key
    CKey key;
};

#endif // SETTINGSBITTOOLWIDGET_H
