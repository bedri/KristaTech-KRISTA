// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsbittoolwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsbittoolwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"

#include "guiutil.h"
#include "walletmodel.h"

#include "base58.h"
#include "bip38.h"
#include "chainparams.h"
#include "init.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"

#include <algorithm>
#include <string>
#include <vector>

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
enum BitToolTone { ToneError = 0, ToneSuccess = 1, ToneInfo = 2 };

QString BitToolStatusCss(int tone, bool light)
{
    QString col;
    if (tone == ToneError) {
        col = light ? QStringLiteral("#C94A3E") : QStringLiteral("#F87171");
    } else if (tone == ToneSuccess) {
        col = light ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    } else {
        col = light ? QStringLiteral("#5576B2") : QStringLiteral("#7695CD");
    }
    return QStringLiteral("QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }").arg(col);
}
} // namespace

SettingsBitToolWidget::SettingsBitToolWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsBitToolWidget)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("BIP38 Key Tool"),
        tr("Encrypt legacy P2PKH private keys with BIP38 passphrases or decrypt and import BIP38 keys."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Segmented mode toggle buttons in page header
    for (QPushButton* modeBtn : {ui->pushLeft, ui->pushRight}) {
        clearLegacySettingsConstraints(modeBtn);
        modeBtn->setParent(modernRoot);
        modeBtn->setCheckable(true);
        modeBtn->setAutoExclusive(true);
        modeBtn->setFixedSize(92, 34);
        modeBtn->setCursor(Qt::PointingHandCursor);
        modeBtn->setFocusPolicy(Qt::NoFocus);
        modeBtn->setProperty("settingsSegBtn", true);
    }
    ui->pushLeft->setText(tr("Encrypt"));
    ui->pushRight->setText(tr("Decrypt"));
    ui->pushLeft->setChecked(true);
    headerRight->addWidget(ui->pushLeft);
    headerRight->addWidget(ui->pushRight);

    modeStack = new QStackedWidget(modernRoot);

    // Index 0: Decrypt BIP38 Key Card
    QWidget* decryptPage = new QWidget(modeStack);
    QVBoxLayout* decPageLayout = new QVBoxLayout(decryptPage);
    decPageLayout->setContentsMargins(0, 0, 0, 0);
    decPageLayout->setSpacing(0);

    QVBoxLayout* decBody = nullptr;
    QFrame* cardDec = createSettingsCard(decryptPage, tr("Decrypt BIP38 Key"), decBody);

    QFrame* decInfoBox = new QFrame(cardDec);
    decInfoBox->setAttribute(Qt::WA_StyledBackground, true);
    decInfoBox->setProperty("settingsAlertBox", QStringLiteral("info"));
    QVBoxLayout* decInfoLayout = new QVBoxLayout(decInfoBox);
    decInfoLayout->setContentsMargins(12, 10, 12, 10);
    QLabel* lblDecInfo = new QLabel(tr("Decrypt a BIP38 password-protected key and optionally import its private key directly into your wallet (triggers a chain rescan)."), decInfoBox);
    lblDecInfo->setProperty("settingsAlertText", QStringLiteral("info"));
    lblDecInfo->setWordWrap(true);
    decInfoLayout->addWidget(lblDecInfo);
    decBody->addWidget(decInfoBox);
    decBody->addSpacing(14);

    QLabel* lblEncKeyTitle = new QLabel(tr("Encrypted Key (BIP38)"), cardDec);
    lblEncKeyTitle->setProperty("settingsRowTitle", true);
    decBody->addWidget(lblEncKeyTitle);
    decBody->addSpacing(6);

    clearLegacySettingsConstraints(ui->lineEditKey);
    ui->lineEditKey->setParent(cardDec);
    ui->lineEditKey->setFixedHeight(38);
    ui->lineEditKey->setPlaceholderText(tr("Enter a BIP38 encrypted key (6P...)"));
    ui->lineEditKey->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->lineEditKey->setProperty("settingsInput", true);
    decBody->addWidget(ui->lineEditKey);
    decBody->addSpacing(14);

    QLabel* lblDecPassTitle = new QLabel(tr("Passphrase"), cardDec);
    lblDecPassTitle->setProperty("settingsRowTitle", true);
    decBody->addWidget(lblDecPassTitle);
    decBody->addSpacing(6);

    clearLegacySettingsConstraints(ui->lineEditPassphrase);
    ui->lineEditPassphrase->setParent(cardDec);
    ui->lineEditPassphrase->setFixedHeight(38);
    ui->lineEditPassphrase->setEchoMode(QLineEdit::Password);
    ui->lineEditPassphrase->setPlaceholderText(tr("Enter passphrase"));
    ui->lineEditPassphrase->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->lineEditPassphrase->setProperty("settingsInput", true);
    actToggleDecPass = ui->lineEditPassphrase->addAction(QIcon(isLightTheme() ? QStringLiteral("://ic-watch-password") : QStringLiteral("://ic-watch-password-white")), QLineEdit::TrailingPosition);
    decBody->addWidget(ui->lineEditPassphrase);
    decBody->addSpacing(14);

    QLabel* lblDecResTitle = new QLabel(tr("Decrypted Address Result"), cardDec);
    lblDecResTitle->setProperty("settingsRowTitle", true);
    decBody->addWidget(lblDecResTitle);
    decBody->addSpacing(6);

    QHBoxLayout* decResRow = new QHBoxLayout();
    decResRow->setContentsMargins(0, 0, 0, 0);
    decResRow->setSpacing(8);

    clearLegacySettingsConstraints(ui->lineEditDecryptResult);
    ui->lineEditDecryptResult->setParent(cardDec);
    ui->lineEditDecryptResult->setFixedHeight(38);
    ui->lineEditDecryptResult->setReadOnly(true);
    ui->lineEditDecryptResult->setPlaceholderText(tr("Decrypted address will appear here"));
    ui->lineEditDecryptResult->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->lineEditDecryptResult->setProperty("settingsInputReadOnly", true);

    QPushButton* btnCopyDec = new QPushButton(tr("Copy"), cardDec);
    btnCopyDec->setProperty("settingsSecondary", true);
    btnCopyDec->setFixedSize(82, 38);
    btnCopyDec->setCursor(Qt::PointingHandCursor);
    btnCopyDec->setFocusPolicy(Qt::NoFocus);

    clearLegacySettingsConstraints(ui->pushButtonImport);
    ui->pushButtonImport->setParent(cardDec);
    ui->pushButtonImport->setText(tr("Import Address"));
    ui->pushButtonImport->setProperty("settingsPrimary", true);
    ui->pushButtonImport->setFixedHeight(38);
    ui->pushButtonImport->setMinimumWidth(135);
    ui->pushButtonImport->setCursor(Qt::PointingHandCursor);
    ui->pushButtonImport->setFocusPolicy(Qt::NoFocus);
    ui->pushButtonImport->setVisible(false);

    decResRow->addWidget(ui->lineEditDecryptResult, 1);
    decResRow->addWidget(btnCopyDec, 0);
    decResRow->addWidget(ui->pushButtonImport, 0);
    decBody->addLayout(decResRow);

    clearLegacySettingsConstraints(ui->statusLabel_DEC);
    ui->statusLabel_DEC->setParent(cardDec);
    ui->statusLabel_DEC->setWordWrap(true);
    ui->statusLabel_DEC->setContentsMargins(0, 10, 0, 2);
    ui->statusLabel_DEC->setVisible(false);
    decBody->addWidget(ui->statusLabel_DEC);

    decBody->addSpacing(14);
    decBody->addWidget(createSettingsDivider(cardDec));

    QWidget* decActionRow = new QWidget(cardDec);
    QHBoxLayout* decActionLayout = new QHBoxLayout(decActionRow);
    decActionLayout->setContentsMargins(0, 12, 0, 10);
    decActionLayout->setSpacing(10);

    clearLegacySettingsConstraints(ui->pushButtonDecryptClear);
    ui->pushButtonDecryptClear->setParent(decActionRow);
    ui->pushButtonDecryptClear->setText(tr("Clear All"));
    ui->pushButtonDecryptClear->setProperty("settingsSecondary", true);
    ui->pushButtonDecryptClear->setFixedHeight(36);
    ui->pushButtonDecryptClear->setMinimumWidth(110);
    ui->pushButtonDecryptClear->setCursor(Qt::PointingHandCursor);
    ui->pushButtonDecryptClear->setFocusPolicy(Qt::NoFocus);

    clearLegacySettingsConstraints(ui->pushButtonDecrypt);
    ui->pushButtonDecrypt->setParent(decActionRow);
    ui->pushButtonDecrypt->setText(tr("Decrypt Key"));
    ui->pushButtonDecrypt->setProperty("settingsPrimary", true);
    ui->pushButtonDecrypt->setMinimumSize(160, 36);
    ui->pushButtonDecrypt->setMaximumHeight(36);
    ui->pushButtonDecrypt->setCursor(Qt::PointingHandCursor);
    ui->pushButtonDecrypt->setFocusPolicy(Qt::NoFocus);

    decActionLayout->addWidget(ui->pushButtonDecryptClear);
    decActionLayout->addStretch(1);
    decActionLayout->addWidget(ui->pushButtonDecrypt);
    decBody->addWidget(decActionRow);

    decPageLayout->addWidget(cardDec);
    decPageLayout->addStretch(1);

    // Index 1: Encrypt Private Key (BIP38) Card
    QWidget* encryptPage = new QWidget(modeStack);
    QVBoxLayout* encPageLayout = new QVBoxLayout(encryptPage);
    encPageLayout->setContentsMargins(0, 0, 0, 0);
    encPageLayout->setSpacing(0);

    QVBoxLayout* encBody = nullptr;
    QFrame* cardEnc = createSettingsCard(encryptPage, tr("Encrypt Private Key (BIP38)"), encBody);

    QFrame* encInfoBox = new QFrame(cardEnc);
    encInfoBox->setAttribute(Qt::WA_StyledBackground, true);
    encInfoBox->setProperty("settingsAlertBox", QStringLiteral("info"));
    QVBoxLayout* encInfoLayout = new QVBoxLayout(encInfoBox);
    encInfoLayout->setContentsMargins(12, 10, 12, 10);
    const QString p2pkhPrefix = (Params().NetworkID() == CBaseChainParams::MAIN) ? QStringLiteral("KT...") : QStringLiteral("kt...");
    const QString taprootPrefix = QString::fromStdString(Params().Bech32HRP()) + QStringLiteral("1p...");
    QLabel* lblEncInfo = new QLabel(tr("BIP38 encryption supports legacy P2PKH (%1) single-key addresses only. Taproot (%2) and script addresses are not supported.").arg(p2pkhPrefix, taprootPrefix), encInfoBox);
    lblEncInfo->setProperty("settingsAlertText", QStringLiteral("info"));
    lblEncInfo->setWordWrap(true);
    encInfoLayout->addWidget(lblEncInfo);
    encBody->addWidget(encInfoBox);
    encBody->addSpacing(14);

    QLabel* lblAddrTitle = new QLabel(tr("KRISTA Address"), cardEnc);
    lblAddrTitle->setProperty("settingsRowTitle", true);
    encBody->addWidget(lblAddrTitle);
    encBody->addSpacing(6);

    clearLegacySettingsConstraints(ui->addressIn_ENC);
    ui->addressIn_ENC->setParent(cardEnc);
    ui->addressIn_ENC->setFixedHeight(38);
    ui->addressIn_ENC->setPlaceholderText(tr("Enter a P2PKH address (%1)").arg(p2pkhPrefix));
    ui->addressIn_ENC->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->addressIn_ENC->setProperty("settingsInput", true);
    btnContact = ui->addressIn_ENC->addAction(QIcon("://ic-contact-arrow-down"), QLineEdit::TrailingPosition);
    encBody->addWidget(ui->addressIn_ENC);
    encBody->addSpacing(14);

    QLabel* lblEncPassTitle = new QLabel(tr("Passphrase"), cardEnc);
    lblEncPassTitle->setProperty("settingsRowTitle", true);
    encBody->addWidget(lblEncPassTitle);
    encBody->addSpacing(6);

    clearLegacySettingsConstraints(ui->passphraseIn_ENC);
    ui->passphraseIn_ENC->setParent(cardEnc);
    ui->passphraseIn_ENC->setFixedHeight(38);
    ui->passphraseIn_ENC->setEchoMode(QLineEdit::Password);
    ui->passphraseIn_ENC->setPlaceholderText(tr("Enter passphrase"));
    ui->passphraseIn_ENC->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->passphraseIn_ENC->setProperty("settingsInput", true);
    actToggleEncPass = ui->passphraseIn_ENC->addAction(QIcon(isLightTheme() ? QStringLiteral("://ic-watch-password") : QStringLiteral("://ic-watch-password-white")), QLineEdit::TrailingPosition);
    encBody->addWidget(ui->passphraseIn_ENC);
    encBody->addSpacing(14);

    QLabel* lblEncOutTitle = new QLabel(tr("Encrypted Key"), cardEnc);
    lblEncOutTitle->setProperty("settingsRowTitle", true);
    encBody->addWidget(lblEncOutTitle);
    encBody->addSpacing(6);

    QHBoxLayout* encOutRow = new QHBoxLayout();
    encOutRow->setContentsMargins(0, 0, 0, 0);
    encOutRow->setSpacing(8);

    clearLegacySettingsConstraints(ui->encryptedKeyOut_ENC);
    ui->encryptedKeyOut_ENC->setParent(cardEnc);
    ui->encryptedKeyOut_ENC->setFixedHeight(38);
    ui->encryptedKeyOut_ENC->setReadOnly(true);
    ui->encryptedKeyOut_ENC->setPlaceholderText(tr("BIP38 encrypted key will appear here"));
    ui->encryptedKeyOut_ENC->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->encryptedKeyOut_ENC->setProperty("settingsInputReadOnly", true);

    QPushButton* btnCopyEnc = new QPushButton(tr("Copy"), cardEnc);
    btnCopyEnc->setProperty("settingsSecondary", true);
    btnCopyEnc->setFixedSize(88, 38);
    btnCopyEnc->setCursor(Qt::PointingHandCursor);
    btnCopyEnc->setFocusPolicy(Qt::NoFocus);

    encOutRow->addWidget(ui->encryptedKeyOut_ENC, 1);
    encOutRow->addWidget(btnCopyEnc, 0);
    encBody->addLayout(encOutRow);

    clearLegacySettingsConstraints(ui->statusLabel_ENC);
    ui->statusLabel_ENC->setParent(cardEnc);
    ui->statusLabel_ENC->setWordWrap(true);
    ui->statusLabel_ENC->setContentsMargins(0, 10, 0, 2);
    ui->statusLabel_ENC->setVisible(false);
    encBody->addWidget(ui->statusLabel_ENC);

    encBody->addSpacing(14);
    encBody->addWidget(createSettingsDivider(cardEnc));

    QWidget* encActionRow = new QWidget(cardEnc);
    QHBoxLayout* encActionLayout = new QHBoxLayout(encActionRow);
    encActionLayout->setContentsMargins(0, 12, 0, 10);
    encActionLayout->setSpacing(10);

    clearLegacySettingsConstraints(ui->pushButtonClear);
    ui->pushButtonClear->setParent(encActionRow);
    ui->pushButtonClear->setText(tr("Clear All"));
    ui->pushButtonClear->setProperty("settingsSecondary", true);
    ui->pushButtonClear->setFixedHeight(36);
    ui->pushButtonClear->setMinimumWidth(110);
    ui->pushButtonClear->setCursor(Qt::PointingHandCursor);
    ui->pushButtonClear->setFocusPolicy(Qt::NoFocus);

    clearLegacySettingsConstraints(ui->pushButtonEncrypt);
    ui->pushButtonEncrypt->setParent(encActionRow);
    ui->pushButtonEncrypt->setText(tr("Encrypt Key"));
    ui->pushButtonEncrypt->setProperty("settingsPrimary", true);
    ui->pushButtonEncrypt->setMinimumSize(160, 36);
    ui->pushButtonEncrypt->setMaximumHeight(36);
    ui->pushButtonEncrypt->setCursor(Qt::PointingHandCursor);
    ui->pushButtonEncrypt->setFocusPolicy(Qt::NoFocus);

    encActionLayout->addWidget(ui->pushButtonClear);
    encActionLayout->addStretch(1);
    encActionLayout->addWidget(ui->pushButtonEncrypt);
    encBody->addWidget(encActionRow);

    encPageLayout->addWidget(cardEnc);
    encPageLayout->addStretch(1);

    modeStack->addWidget(decryptPage); // index 0
    modeStack->addWidget(encryptPage); // index 1
    modeStack->setCurrentIndex(1);
    scrollLayout->addWidget(modeStack);

    applyTheme(isLightTheme());

    connect(ui->pushLeft, &QPushButton::clicked, [this](){onEncryptSelected(true);});
    connect(ui->pushRight, &QPushButton::clicked, [this](){onEncryptSelected(false);});
    connect(ui->pushButtonEncrypt, &QPushButton::clicked, this, &SettingsBitToolWidget::onEncryptKeyButtonENCClicked);
    connect(ui->pushButtonDecrypt, &QPushButton::clicked, this, &SettingsBitToolWidget::onDecryptClicked);
    connect(ui->pushButtonImport, &QPushButton::clicked, this, &SettingsBitToolWidget::importAddressFromDecKey);
    connect(btnContact, &QAction::triggered, this, &SettingsBitToolWidget::onAddressesClicked);
    connect(ui->pushButtonClear, &QPushButton::clicked, this, &SettingsBitToolWidget::onClearAll);
    connect(ui->pushButtonDecryptClear, &QPushButton::clicked, this, &SettingsBitToolWidget::onClearDecrypt);
    connect(actToggleEncPass, &QAction::triggered, this, [this]() {
        encPassVisible = !encPassVisible;
        ui->passphraseIn_ENC->setEchoMode(encPassVisible ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(actToggleDecPass, &QAction::triggered, this, [this]() {
        decPassVisible = !decPassVisible;
        ui->lineEditPassphrase->setEchoMode(decPassVisible ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(btnCopyEnc, &QPushButton::clicked, this, [this]() {
        const QString val = ui->encryptedKeyOut_ENC->text().trimmed();
        if (val.isEmpty()) {
            inform(tr("No encrypted key to copy."));
            return;
        }
        if (QApplication::clipboard()) {
            QApplication::clipboard()->setText(val);
            inform(tr("Encrypted key copied to clipboard."));
        }
    });
    connect(btnCopyDec, &QPushButton::clicked, this, [this]() {
        const QString val = ui->lineEditDecryptResult->text().trimmed();
        if (val.isEmpty()) {
            inform(tr("No decrypted address to copy."));
            return;
        }
        if (QApplication::clipboard()) {
            QApplication::clipboard()->setText(val);
            inform(tr("Decrypted address copied to clipboard."));
        }
    });
}

void SettingsBitToolWidget::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
    const QIcon eyeIcon(isLight ? QStringLiteral("://ic-watch-password") : QStringLiteral("://ic-watch-password-white"));
    if (actToggleEncPass) actToggleEncPass->setIcon(eyeIcon);
    if (actToggleDecPass) actToggleDecPass->setIcon(eyeIcon);
    if (hasEncStatus && ui->statusLabel_ENC) {
        ui->statusLabel_ENC->setStyleSheet(BitToolStatusCss(encTone, isLight));
    }
    if (hasDecStatus && ui->statusLabel_DEC) {
        ui->statusLabel_DEC->setStyleSheet(BitToolStatusCss(decTone, isLight));
    }
}

void SettingsBitToolWidget::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsBitToolWidget::setEncStatus(const QString& text, int tone)
{
    hasEncStatus = true;
    encTone = tone;
    ui->statusLabel_ENC->setStyleSheet(BitToolStatusCss(tone, isLightTheme()));
    ui->statusLabel_ENC->setText(text);
    ui->statusLabel_ENC->setVisible(true);
}

void SettingsBitToolWidget::clearEncStatus()
{
    hasEncStatus = false;
    ui->statusLabel_ENC->clear();
    ui->statusLabel_ENC->setVisible(false);
}

void SettingsBitToolWidget::setDecStatus(const QString& text, int tone)
{
    hasDecStatus = true;
    decTone = tone;
    ui->statusLabel_DEC->setStyleSheet(BitToolStatusCss(tone, isLightTheme()));
    ui->statusLabel_DEC->setText(text);
    ui->statusLabel_DEC->setVisible(true);
}

void SettingsBitToolWidget::clearDecStatus()
{
    hasDecStatus = false;
    ui->statusLabel_DEC->clear();
    ui->statusLabel_DEC->setVisible(false);
}

void SettingsBitToolWidget::setAddress_ENC(const QString& address)
{
    ui->addressIn_ENC->setText(address);
    ui->passphraseIn_ENC->setFocus();
}

void SettingsBitToolWidget::onEncryptSelected(bool isEncr)
{
    if (menuContacts && menuContacts->isVisible()) {
        menuContacts->hide();
    }
    if (modeStack) {
        modeStack->setCurrentIndex(isEncr ? 1 : 0);
    }
}

QString specialChar = "\"@!#$%&'()*+,-./:;<=>?`{|}~^_[]\\";
QString validChar = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz" + specialChar;

bool isValidPassphrase(QString strPassphrase, QString& strInvalid)
{
    for (int i = 0; i < strPassphrase.size(); i++) {
        if (!validChar.contains(strPassphrase[i], Qt::CaseSensitive)) {
            if (QString("\"'").contains(strPassphrase[i]))
                continue;

            strInvalid = strPassphrase[i];
            return false;
        }
    }

    return true;
}

void SettingsBitToolWidget::onEncryptKeyButtonENCClicked()
{
    if (!walletModel)
        return;

    QString qstrPassphrase = ui->passphraseIn_ENC->text();
    QString strInvalid;
    if (!isValidPassphrase(qstrPassphrase, strInvalid)) {
        setEncStatus(tr("The entered passphrase is invalid. ") + strInvalid + QString(" is not valid") + QString(" ") + tr("Allowed: 0-9,a-z,A-Z,") + specialChar, ToneError);
        return;
    }

    CTxDestination dest = DecodeDestination(ui->addressIn_ENC->text().trimmed().toStdString());
    if (!IsValidDestination(dest)) {
        setEncStatus(tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."), ToneError);
        return;
    }

    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        const QString p2pkhPrefix = (Params().NetworkID() == CBaseChainParams::MAIN) ? QStringLiteral("KT...") : QStringLiteral("kt...");
        const QString taprootPrefix = QString::fromStdString(Params().Bech32HRP()) + QStringLiteral("1p...");
        setEncStatus(tr("BIP38 encryption supports legacy P2PKH (%1) single-key addresses only. Taproot (%2) and script addresses are not supported.").arg(p2pkhPrefix, taprootPrefix), ToneError);
        return;
    }

    if (!pwalletMain) {
        setEncStatus(tr("Wallet is not available."), ToneError);
        return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        setEncStatus(tr("Wallet unlock was cancelled."), ToneError);
        return;
    }

    CKey encKey;
    {
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(*keyID, encKey)) {
            setEncStatus(tr("Private key for the entered address is not available."), ToneError);
            return;
        }
    }

    std::string encryptedKey = BIP38_Encrypt(EncodeDestination(dest), qstrPassphrase.toStdString(), encKey.GetPrivKey_256(), encKey.IsCompressed());
    ui->encryptedKeyOut_ENC->setText(QString::fromStdString(encryptedKey));
    setEncStatus(tr("Address encrypted successfully."), ToneSuccess);
}

void SettingsBitToolWidget::onClearAll()
{
    ui->addressIn_ENC->clear();
    ui->passphraseIn_ENC->clear();
    ui->encryptedKeyOut_ENC->clear();
    clearEncStatus();
    ui->addressIn_ENC->setFocus();
}

void SettingsBitToolWidget::onAddressesClicked()
{
    if (!walletModel || !walletModel->getAddressTableModel()) {
        return;
    }
    int addressSize = walletModel->getAddressTableModel()->sizeRecv();
    if (addressSize == 0) {
        inform(tr("No addresses available, you can go to the receive screen and add some there!"));
        return;
    }

    const int height = std::min(240, std::max(150, addressSize * 74));
    const int width = std::max(280, ui->addressIn_ENC->width());

    if (!menuContacts) {
        menuContacts = new ContactsDropdown(width, height, this);
        menuContacts->setWalletModel(walletModel, AddressTableModel::Receive);
        connect(menuContacts, &ContactsDropdown::contactSelected, [this](QString address, QString label){
            Q_UNUSED(label);
            setAddress_ENC(address);
        });
    }

    if (menuContacts->isVisible()) {
        menuContacts->hide();
        return;
    }

    menuContacts->resizeList(width, height);
    menuContacts->setStyleSheet(window ? window->styleSheet() : GUIUtil::loadStyleSheet());
    menuContacts->adjustSize();

    const QPoint pos = ui->addressIn_ENC->mapTo(this, QPoint(0, ui->addressIn_ENC->height() + 4));
    menuContacts->move(pos);
    menuContacts->show();
    menuContacts->raise();
}

void SettingsBitToolWidget::resizeMenu()
{
    if (menuContacts && menuContacts->isVisible()) {
        const int width = std::max(280, ui->addressIn_ENC->width());
        menuContacts->resizeList(width, menuContacts->height());
        menuContacts->resize(width, menuContacts->height());
        const QPoint pos = ui->addressIn_ENC->mapTo(this, QPoint(0, ui->addressIn_ENC->height() + 4));
        menuContacts->move(pos);
    }
}

void SettingsBitToolWidget::onClearDecrypt()
{
    ui->lineEditKey->clear();
    ui->lineEditDecryptResult->clear();
    ui->lineEditPassphrase->clear();
    ui->pushButtonImport->setVisible(false);
    clearDecStatus();
    key = CKey();
}

void SettingsBitToolWidget::onDecryptClicked()
{
    std::string strPassphrase = ui->lineEditPassphrase->text().toStdString();
    std::string strKey = ui->lineEditKey->text().trimmed().toStdString();

    uint256 privKey;
    bool fCompressed;
    if (!BIP38_Decrypt(strPassphrase, strKey, privKey, fCompressed)) {
        setDecStatus(tr("Failed to decrypt.") + QString(" ") + tr("Please check the key and passphrase and try again."), ToneError);
        return;
    }

    key.Set(privKey.begin(), privKey.end(), fCompressed);
    CPubKey pubKey = key.GetPubKey();
    ui->lineEditDecryptResult->setText(QString::fromStdString(EncodeDestination(pubKey.GetID())));
    ui->pushButtonImport->setVisible(true);
    setDecStatus(tr("Key decrypted successfully. You can copy the address or import it into your wallet."), ToneSuccess);
}

void SettingsBitToolWidget::importAddressFromDecKey()
{
    if (!walletModel || !pwalletMain) {
        return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        setDecStatus(tr("Wallet unlock was cancelled."), ToneError);
        return;
    }

    CTxDestination dest = DecodeDestination(ui->lineEditDecryptResult->text().toStdString());
    CPubKey pubkey = key.GetPubKey();

    if (!IsValidDestination(dest) || !key.IsValid() || EncodeDestination(pubkey.GetID()) != EncodeDestination(dest)) {
        setDecStatus(tr("Data Not Valid.") + QString(" ") + tr("Please try again."), ToneError);
        return;
    }

    CKeyID vchAddress = pubkey.GetID();
    setDecStatus(tr("Please wait while key is imported..."), ToneInfo);
    QCoreApplication::processEvents();

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, "", AddressBook::AddressBookPurpose::RECEIVE);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress)) {
            setDecStatus(tr("Cannot import address, key already held by the wallet"), ToneError);
            return;
        }

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
            setDecStatus(tr("Error adding key to the wallet"), ToneError);
            return;
        }

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
    }

    setDecStatus(tr("Successfully added private key to the wallet"), ToneSuccess);
}

void SettingsBitToolWidget::resizeEvent(QResizeEvent *event)
{
    resizeMenu();
    QWidget::resizeEvent(event);
}

SettingsBitToolWidget::~SettingsBitToolWidget()
{
    delete ui;
}
