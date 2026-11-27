// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsbackupwallet.h"
#include "qt/kristatech/settings/forms/ui_settingsbackupwallet.h"
#include "chainparams.h"
#include "guiinterface.h"
#include "guiutil.h"
#include "qt/kristatech/qtutils.h"

#include <QDateTime>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

SettingsBackupWallet::SettingsBackupWallet(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsBackupWallet)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Backup & Passphrase"),
        tr("Back up your wallet.dat file externally and manage AES-256 disk encryption for your private keys."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Card 1: Wallet Backup
    QVBoxLayout* backupBody = nullptr;
    QFrame* cardBackup = createSettingsCard(modernRoot, tr("Wallet Backup"), backupBody);

    clearLegacySettingsConstraints(ui->pushButtonDocuments);
    ui->pushButtonDocuments->setProperty("settingsPrimary", true);
    ui->pushButtonDocuments->setText(tr("Backup Wallet (.dat)..."));
    ui->pushButtonDocuments->setMinimumSize(195, 36);
    ui->pushButtonDocuments->setMaximumHeight(36);
    ui->pushButtonDocuments->setCursor(Qt::PointingHandCursor);
    ui->pushButtonDocuments->setFocusPolicy(Qt::TabFocus);

    addSettingsControlRow(
        cardBackup,
        backupBody,
        tr("Export Wallet Backup (.dat)"),
        tr("Save a copy of wallet.dat to an external drive or offline storage to recover your balance, addresses, and keys on another device."),
        ui->pushButtonDocuments,
        false);

    backupBody->addWidget(createSettingsDivider(cardBackup));
    lblBackupPath = new QLabel(cardBackup);
    lblBackupPath->setProperty("settingsRowDesc", true);
    lblBackupPath->setWordWrap(true);
    lblBackupPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    lblBackupPath->setContentsMargins(0, 10, 0, 10);
    updateBackupStatusLabel();
    backupBody->addWidget(lblBackupPath);

    scrollLayout->addWidget(cardBackup);

    // Card 2: Wallet Encryption & Passphrase
    QVBoxLayout* encBody = nullptr;
    QFrame* cardEnc = createSettingsCard(modernRoot, tr("Wallet Encryption & Passphrase"), encBody);

    QWidget* encHeaderRow = new QWidget(cardEnc);
    QHBoxLayout* encHeaderLayout = new QHBoxLayout(encHeaderRow);
    encHeaderLayout->setContentsMargins(0, 2, 0, 4);
    encHeaderLayout->setSpacing(10);

    lblEncTitle = new QLabel(tr("Wallet Encryption"), encHeaderRow);
    lblEncTitle->setProperty("settingsRowTitle", true);

    lblEncBadge = new QLabel(tr("CHECKING..."), encHeaderRow);
    lblEncBadge->setProperty("settingsBadge", QStringLiteral("warn"));

    encHeaderLayout->addWidget(lblEncTitle);
    encHeaderLayout->addStretch(1);
    encHeaderLayout->addWidget(lblEncBadge, 0, Qt::AlignVCenter);
    encBody->addWidget(encHeaderRow);

    clearLegacySettingsConstraints(ui->labelSubtitle_2);
    ui->labelSubtitle_2->setParent(cardEnc);
    ui->labelSubtitle_2->setProperty("settingsRowDesc", true);
    ui->labelSubtitle_2->setWordWrap(true);
    ui->labelSubtitle_2->setContentsMargins(0, 0, 0, 10);
    encBody->addWidget(ui->labelSubtitle_2);

    encAlertBox = new QFrame(cardEnc);
    encAlertBox->setAttribute(Qt::WA_StyledBackground, true);
    encAlertBox->setProperty("settingsAlertBox", QStringLiteral("warn"));
    QVBoxLayout* alertLayout = new QVBoxLayout(encAlertBox);
    alertLayout->setContentsMargins(12, 10, 12, 10);
    alertLayout->setSpacing(0);
    lblEncAlert = new QLabel(encAlertBox);
    lblEncAlert->setProperty("settingsAlertText", QStringLiteral("warn"));
    lblEncAlert->setWordWrap(true);
    alertLayout->addWidget(lblEncAlert);
    encBody->addWidget(encAlertBox);
    encBody->addSpacing(12);

    encBody->addWidget(createSettingsDivider(cardEnc));

    QWidget* encActionRow = new QWidget(cardEnc);
    QHBoxLayout* encActionLayout = new QHBoxLayout(encActionRow);
    encActionLayout->setContentsMargins(0, 12, 0, 10);
    encActionLayout->setSpacing(12);

    clearLegacySettingsConstraints(ui->pushButtonSave_2);
    ui->pushButtonSave_2->setParent(encActionRow);
    ui->pushButtonSave_2->setProperty("settingsPrimary", true);
    ui->pushButtonSave_2->setMinimumSize(195, 36);
    ui->pushButtonSave_2->setMaximumHeight(36);
    ui->pushButtonSave_2->setCursor(Qt::PointingHandCursor);
    ui->pushButtonSave_2->setFocusPolicy(Qt::TabFocus);

    encActionLayout->addStretch(1);
    encActionLayout->addWidget(ui->pushButtonSave_2);
    encBody->addWidget(encActionRow);

    scrollLayout->addWidget(cardEnc);
    scrollLayout->addStretch(1);

    connect(ui->pushButtonDocuments, &QPushButton::clicked, this, &SettingsBackupWallet::selectFileOutput);
    connect(ui->pushButtonSave_2, &QPushButton::clicked, this, &SettingsBackupWallet::changePassphrase);

    updateEncryptionUI();
    applyTheme(isLightTheme());
}

void SettingsBackupWallet::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
}

void SettingsBackupWallet::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsBackupWallet::loadWalletModel()
{
    if (walletModel) {
        connect(walletModel, &WalletModel::encryptionStatusChanged, this, &SettingsBackupWallet::updateEncryptionUI);
        updateEncryptionUI();
    }
}

void SettingsBackupWallet::updateEncryptionUI()
{
    const bool isUnencrypted = !walletModel || (walletModel->getEncryptionStatus() == WalletModel::Unencrypted);
    if (isUnencrypted) {
        lblEncTitle->setText(tr("Encrypt Wallet (Recommended)"));
        lblEncBadge->setText(tr("UNENCRYPTED"));
        lblEncBadge->setProperty("settingsBadge", QStringLiteral("warn"));
        ui->labelSubtitle_2->setText(tr("Private keys inside wallet.dat are currently stored unencrypted on disk."));
        encAlertBox->setProperty("settingsAlertBox", QStringLiteral("warn"));
        lblEncAlert->setProperty("settingsAlertText", QStringLiteral("warn"));
        lblEncAlert->setText(tr("Security alert: Anyone with access to your wallet.dat file can spend your funds. Set a strong offline passphrase — lost passphrases cannot be recovered."));
        ui->pushButtonSave_2->setText(tr("Encrypt Wallet..."));
    } else {
        lblEncTitle->setText(tr("Change Wallet Passphrase"));
        lblEncBadge->setText(tr("ENCRYPTED (AES-256)"));
        lblEncBadge->setProperty("settingsBadge", QStringLiteral("active"));
        ui->labelSubtitle_2->setText(tr("Your wallet keys are protected with passphrase encryption on disk."));
        encAlertBox->setProperty("settingsAlertBox", QStringLiteral("info"));
        lblEncAlert->setProperty("settingsAlertText", QStringLiteral("info"));
        lblEncAlert->setText(tr("Updating your passphrase decrypts and re-encrypts wallet.dat with the new passphrase. Create a fresh wallet backup immediately after updating your passphrase."));
        ui->pushButtonSave_2->setText(tr("Change Passphrase..."));
    }
    updateStyle(lblEncBadge);
    updateStyle(encAlertBox);
    updateStyle(lblEncAlert);
}

void SettingsBackupWallet::updateBackupStatusLabel()
{
    if (!lblBackupPath) return;
    QSettings settings;
    const QString net = QString::fromStdString(Params().NetworkIDString());
    const QString path = settings.value(QStringLiteral("walletBackupLastPath_%1").arg(net)).toString().trimmed();
    const QString ts = settings.value(QStringLiteral("walletBackupLastTime_%1").arg(net)).toString().trimmed();
    if (!path.isEmpty()) {
        lblBackupPath->setText(tr("Last backup saved (%1): %2")
            .arg(ts.isEmpty() ? tr("previous session") : ts, path));
    } else {
        lblBackupPath->setText(tr("No wallet backup recorded yet."));
    }
}

void SettingsBackupWallet::selectFileOutput()
{
    QString filename = GUIUtil::getSaveFileName(this,
                                        tr("Backup Wallet"), QString(),
                                        tr("Wallet Data (*.dat)"), nullptr);

    if (filename.isEmpty()) {
        return;
    }
    if (walletModel) {
        if (walletModel->backupWallet(filename)) {
            QSettings settings;
            const QString net = QString::fromStdString(Params().NetworkIDString());
            settings.setValue(QStringLiteral("walletBackupLastPath_%1").arg(net), filename);
            settings.setValue(QStringLiteral("walletBackupLastTime_%1").arg(net),
                              QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm")));
            updateBackupStatusLabel();
            inform(tr("Backup created"));
        } else {
            inform(tr("Backup creation failed"));
        }
    }
}

void SettingsBackupWallet::changePassphrase()
{
    if (!walletModel) return;
    showHideOp(true);
    AskPassphraseDialog *dlg = nullptr;
    if (walletModel->getEncryptionStatus() == WalletModel::Unencrypted) {
        dlg = new AskPassphraseDialog(AskPassphraseDialog::Mode::Encrypt, window,
                walletModel, AskPassphraseDialog::Context::Encrypt);
    } else {
        dlg = new AskPassphraseDialog(AskPassphraseDialog::Mode::ChangePass, window,
                walletModel, AskPassphraseDialog::Context::ChangePass);
    }
    dlg->adjustSize();
    Q_EMIT execDialog(dlg);
    dlg->deleteLater();
}

SettingsBackupWallet::~SettingsBackupWallet()
{
    delete ui;
}
