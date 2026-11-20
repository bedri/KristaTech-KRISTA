// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsdisplayoptionswidget.h"
#include "qt/kristatech/settings/forms/ui_settingsdisplayoptionswidget.h"
#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QListView>
#include <QSettings>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include "guiutil.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "bitcoinunits.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"

SettingsDisplayOptionsWidget::SettingsDisplayOptionsWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window,parent),
    ui(new Ui::SettingsDisplayOptionsWidget)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    // TODO: Reconnect these options to actions. Keep hidden for now while preserving mapper binding.
    ui->labelTitleUrl->hide();
    ui->lineEditUrl->hide();
    ui->pushButtonSwitchBalance->setVisible(false);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Display & Units"),
        tr("Customize interface theme, language, KRISTA display units, and balance decimal precision."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Card: Interface & Formatting
    QVBoxLayout* fmtBody = nullptr;
    QFrame* cardFmt = createSettingsCard(modernRoot, tr("Interface & Formatting"), fmtBody);

    auto configureCombo = [](QComboBox* cb) {
        clearLegacySettingsConstraints(cb);
        cb->setView(new QListView());
        cb->setMaxVisibleItems(10);
        cb->setCursor(Qt::PointingHandCursor);
        cb->setFixedSize(280, 36);
        cb->setProperty("settingsInput", true);
    };

    comboBoxTheme = new QComboBox(cardFmt);
    configureCombo(comboBoxTheme);
    comboBoxTheme->addItem(tr("Light Theme"), true);
    comboBoxTheme->addItem(tr("Dark Theme"), false);
    comboBoxTheme->setCurrentIndex(isLightTheme() ? 0 : 1);
    addSettingsControlRow(
        cardFmt,
        fmtBody,
        tr("Theme"),
        tr("Switch between crisp daylight readability and low-glare dark mode (applies immediately)."),
        comboBoxTheme,
        false);
    connect(comboBoxTheme, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        const bool light = (idx == 0);
        if (light != isLightTheme()) {
            setTheme(light);
            if (window) window->changeTheme(light);
        }
    });

    configureCombo(ui->comboBoxLanguage);
    addSettingsControlRow(
        cardFmt,
        fmtBody,
        tr("Language"),
        tr("Interface translation language (wallet restart required to apply)."),
        ui->comboBoxLanguage,
        true);

    configureCombo(ui->comboBoxUnit);
    ui->comboBoxUnit->setModel(new BitcoinUnits(this));
    ui->comboBoxUnit->setModelColumn(Qt::DisplayRole);
    addSettingsControlRow(
        cardFmt,
        fmtBody,
        tr("Unit to Show Amount"),
        tr("Default denomination used across balances and transaction rows."),
        ui->comboBoxUnit,
        true);

    configureCombo(ui->comboBoxDigits);
    QString digits;
    for (int index = 2; index <= 8; index++) {
        digits.setNum(index);
        ui->comboBoxDigits->addItem(digits, digits);
    }
    addSettingsControlRow(
        cardFmt,
        fmtBody,
        tr("Decimal Digits"),
        tr("Number of fractional decimal places displayed for KRISTA amounts (wallet restart required to apply)."),
        ui->comboBoxDigits,
        true);

    scrollLayout->addWidget(cardFmt);
    scrollLayout->addStretch(1);

    buildSettingsOptionsFooter(modernRoot, rootLayout, ui->pushButtonReset, ui->pushButtonClean, ui->pushButtonSave);

    connect(ui->pushButtonSave, &QPushButton::clicked, [this] { Q_EMIT saveSettings(); });
    connect(ui->pushButtonReset, &QPushButton::clicked, this, &SettingsDisplayOptionsWidget::onResetClicked);
    connect(ui->pushButtonClean, &QPushButton::clicked, [this] { Q_EMIT discardSettings(); });

    applyTheme(isLightTheme());
}

void SettingsDisplayOptionsWidget::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsDisplayOptionsWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    if (comboBoxTheme) {
        QSignalBlocker blockTheme(comboBoxTheme);
        comboBoxTheme->setCurrentIndex(isLightTheme ? 0 : 1);
    }
    applyTheme(isLightTheme);
}

void SettingsDisplayOptionsWidget::initLanguages()
{
    if (!clientModel || !clientModel->getOptionsModel() || !ui->comboBoxLanguage) return;
    ui->comboBoxLanguage->clear();
    const QString selectedLang = clientModel->getOptionsModel()->getLang();
    /* Language selector */
    QDir translations(":translations");
    QString defaultStr = QString("(") + tr("default") + QString(")");
    ui->comboBoxLanguage->addItem(defaultStr, QVariant(""));
    QStringList list = translations.entryList();
    int selectedIndex = 0;
    for (int i = 0; i < list.size(); ++i) {
        const QString& langStr = list[i];
        ui->comboBoxLanguage->addItem(formatLocaleDisplayName(langStr), QVariant(langStr));
        // Save selected index
        if (langStr == selectedLang) {
            selectedIndex = i + 1;
        }
    }
    ui->comboBoxLanguage->setCurrentIndex(selectedIndex);
    syncLanguageFromModel();
}

void SettingsDisplayOptionsWidget::syncLanguageFromModel()
{
    if (!clientModel || !clientModel->getOptionsModel() || !ui->comboBoxLanguage) return;
    OptionsModel* optionsModel = clientModel->getOptionsModel();
    const QString targetLang = optionsModel->data(optionsModel->index(OptionsModel::Language, 0), Qt::EditRole).toString();
    const int idx = ui->comboBoxLanguage->findData(QVariant(targetLang));
    ui->comboBoxLanguage->setCurrentIndex(idx >= 0 ? idx : 0);
}

void SettingsDisplayOptionsWidget::onResetClicked()
{
    if (clientModel && clientModel->getOptionsModel()) {
        if (!ask(tr("Reset Display Options"), tr("Reset all Display & Units preferences back to default values?"))) {
            return;
        }
        OptionsModel *optionsModel = clientModel->getOptionsModel();
        QSettings settings;
        optionsModel->setDisplayDefaultOptions(settings, true);
        if (!isLightTheme()) {
            setTheme(true);
            if (window) window->changeTheme(true);
        } else if (comboBoxTheme) {
            QSignalBlocker blockTheme(comboBoxTheme);
            comboBoxTheme->setCurrentIndex(0);
        }
        ui->comboBoxUnit->setCurrentIndex(optionsModel->getDisplayUnit());
        syncLanguageFromModel();
        inform(tr("Options reset succeed"));
    }
}

void SettingsDisplayOptionsWidget::setMapper(QDataWidgetMapper *mapper)
{
    mapper->addMapping(ui->comboBoxDigits, OptionsModel::Digits);
    mapper->addMapping(ui->comboBoxLanguage, OptionsModel::Language, "currentData");
    mapper->addMapping(ui->comboBoxUnit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->pushButtonSwitchBalance, OptionsModel::HideZeroBalances);
}

void SettingsDisplayOptionsWidget::loadClientModel()
{
    if (clientModel) {
        ui->comboBoxUnit->setCurrentIndex(this->clientModel->getOptionsModel()->getDisplayUnit());
        initLanguages();
    }
}

SettingsDisplayOptionsWidget::~SettingsDisplayOptionsWidget()
{
    delete ui;
}
