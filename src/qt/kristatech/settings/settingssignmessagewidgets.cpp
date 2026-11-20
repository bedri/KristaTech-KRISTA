// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingssignmessagewidgets.h"
#include "qt/kristatech/settings/forms/ui_settingssignmessagewidgets.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "guiutil.h"
#include "walletmodel.h"

#include "base58.h"
#include "init.h"
#include "wallet/wallet.h"
#include "askpassphrasedialog.h"
#include "addressbookpage.h"

#include <algorithm>
#include <string>
#include <vector>

#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
QString SignMsgStatusCss(bool isSuccess, bool isLight)
{
    const QString col = isSuccess
        ? (isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0"))
        : (isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#F87171"));
    return QStringLiteral("QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }").arg(col);
}
} // namespace

SettingsSignMessageWidgets::SettingsSignMessageWidgets(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsSignMessageWidgets)
{
    ui->setupUi(this);
    ui->left->setVisible(false);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        ui->verticalLayout,
        tr("Sign / Verify Message"),
        tr("Prove address ownership by signing messages or verify signatures from other KRISTA addresses."),
        scrollLayout,
        headerRight,
        rootLayout);

    // Header mode toggle buttons
    for (QPushButton* modeBtn : {ui->pushSign, ui->pushVerify}) {
        clearLegacySettingsConstraints(modeBtn);
        modeBtn->setParent(modernRoot);
        modeBtn->setCheckable(true);
        modeBtn->setAutoExclusive(true);
        modeBtn->setFixedSize(92, 34);
        modeBtn->setCursor(Qt::PointingHandCursor);
        modeBtn->setFocusPolicy(Qt::NoFocus);
        modeBtn->setProperty("settingsSegBtn", true);
    }
    ui->pushSign->setText(tr("Sign"));
    ui->pushVerify->setText(tr("Verify"));
    ui->pushSign->setChecked(true);
    headerRight->addWidget(ui->pushSign);
    headerRight->addWidget(ui->pushVerify);

    // Card: Sign / Verify Form
    QVBoxLayout* formBody = nullptr;
    QFrame* cardForm = createSettingsCard(modernRoot, QString(), formBody);

    lblCardHeader = new QLabel(tr("Sign Message"), cardForm);
    lblCardHeader->setProperty("settingsCardTitle", true);
    formBody->addWidget(lblCardHeader);
    formBody->addSpacing(4);

    lblModeDesc = new QLabel(cardForm);
    lblModeDesc->setProperty("settingsRowDesc", true);
    lblModeDesc->setWordWrap(true);
    formBody->addWidget(lblModeDesc);
    formBody->addSpacing(12);
    formBody->addWidget(createSettingsDivider(cardForm));
    formBody->addSpacing(14);

    // Row 1: Address + Paste
    QLabel* lblAddrTitle = new QLabel(tr("KRISTA Address"), cardForm);
    lblAddrTitle->setProperty("settingsRowTitle", true);
    QLabel* lblAddrDesc = new QLabel(tr("Enter a P2PKH (K...) address or pick one from your receiving address book."), cardForm);
    lblAddrDesc->setProperty("settingsRowDesc", true);
    lblAddrDesc->setWordWrap(true);
    formBody->addWidget(lblAddrTitle);
    formBody->addSpacing(2);
    formBody->addWidget(lblAddrDesc);
    formBody->addSpacing(8);

    QHBoxLayout* addrRow = new QHBoxLayout();
    addrRow->setContentsMargins(0, 0, 0, 0);
    addrRow->setSpacing(8);

    clearLegacySettingsConstraints(ui->addressIn_SM);
    ui->addressIn_SM->setParent(cardForm);
    ui->addressIn_SM->setFixedHeight(38);
    ui->addressIn_SM->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->addressIn_SM->setProperty("settingsInput", true);
    btnContact = ui->addressIn_SM->addAction(QIcon("://ic-contact-arrow-down"), QLineEdit::TrailingPosition);

    QPushButton* btnPasteAddr = new QPushButton(tr("Paste"), cardForm);
    btnPasteAddr->setProperty("settingsSecondary", true);
    btnPasteAddr->setFixedSize(88, 38);
    btnPasteAddr->setCursor(Qt::PointingHandCursor);
    btnPasteAddr->setFocusPolicy(Qt::NoFocus);

    addrRow->addWidget(ui->addressIn_SM, 1);
    addrRow->addWidget(btnPasteAddr, 0);
    formBody->addLayout(addrRow);
    formBody->addSpacing(14);

    // Row 2: Message
    QLabel* lblMsgTitle = new QLabel(tr("Message"), cardForm);
    lblMsgTitle->setProperty("settingsRowTitle", true);
    formBody->addWidget(lblMsgTitle);
    formBody->addSpacing(6);

    clearLegacySettingsConstraints(ui->messageIn_SM);
    ui->messageIn_SM->setParent(cardForm);
    ui->messageIn_SM->setMinimumHeight(125);
    ui->messageIn_SM->setMaximumHeight(175);
    ui->messageIn_SM->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->messageIn_SM->setProperty("settingsInput", true);
    formBody->addWidget(ui->messageIn_SM);
    formBody->addSpacing(14);

    // Row 3: Signature + Copy
    QLabel* lblSigTitle = new QLabel(tr("Signature"), cardForm);
    lblSigTitle->setProperty("settingsRowTitle", true);
    formBody->addWidget(lblSigTitle);
    formBody->addSpacing(6);

    QHBoxLayout* sigRow = new QHBoxLayout();
    sigRow->setContentsMargins(0, 0, 0, 0);
    sigRow->setSpacing(8);

    clearLegacySettingsConstraints(ui->signatureOut_SM);
    ui->signatureOut_SM->setParent(cardForm);
    ui->signatureOut_SM->setFixedHeight(38);
    ui->signatureOut_SM->setAttribute(Qt::WA_MacShowFocusRect, false);

    btnCopySig = new QPushButton(tr("Copy"), cardForm);
    btnCopySig->setProperty("settingsSecondary", true);
    btnCopySig->setFixedSize(88, 38);
    btnCopySig->setCursor(Qt::PointingHandCursor);
    btnCopySig->setFocusPolicy(Qt::NoFocus);

    sigRow->addWidget(ui->signatureOut_SM, 1);
    sigRow->addWidget(btnCopySig, 0);
    formBody->addLayout(sigRow);

    clearLegacySettingsConstraints(ui->statusLabel_SM);
    ui->statusLabel_SM->setParent(cardForm);
    ui->statusLabel_SM->setWordWrap(true);
    ui->statusLabel_SM->setContentsMargins(0, 10, 0, 2);
    ui->statusLabel_SM->setVisible(false);
    formBody->addWidget(ui->statusLabel_SM);

    formBody->addSpacing(14);
    formBody->addWidget(createSettingsDivider(cardForm));

    QWidget* actionRow = new QWidget(cardForm);
    QHBoxLayout* actionLayout = new QHBoxLayout(actionRow);
    actionLayout->setContentsMargins(0, 12, 0, 10);
    actionLayout->setSpacing(10);

    clearLegacySettingsConstraints(ui->pushButtonClear);
    ui->pushButtonClear->setParent(actionRow);
    ui->pushButtonClear->setText(tr("Clear All"));
    ui->pushButtonClear->setProperty("settingsSecondary", true);
    ui->pushButtonClear->setFixedHeight(36);
    ui->pushButtonClear->setMinimumWidth(110);
    ui->pushButtonClear->setCursor(Qt::PointingHandCursor);
    ui->pushButtonClear->setFocusPolicy(Qt::NoFocus);

    clearLegacySettingsConstraints(ui->pushButtonSave);
    ui->pushButtonSave->setParent(actionRow);
    ui->pushButtonSave->setProperty("settingsPrimary", true);
    ui->pushButtonSave->setMinimumSize(165, 36);
    ui->pushButtonSave->setMaximumHeight(36);
    ui->pushButtonSave->setCursor(Qt::PointingHandCursor);
    ui->pushButtonSave->setFocusPolicy(Qt::NoFocus);

    actionLayout->addWidget(ui->pushButtonClear);
    actionLayout->addStretch(1);
    actionLayout->addWidget(ui->pushButtonSave);
    formBody->addWidget(actionRow);

    scrollLayout->addWidget(cardForm);
    scrollLayout->addStretch(1);

    updateMode();
    applyTheme(isLightTheme());

    connect(ui->pushButtonSave, &QPushButton::clicked, this, &SettingsSignMessageWidgets::onGoClicked);
    connect(btnContact, &QAction::triggered, this, &SettingsSignMessageWidgets::onAddressesClicked);
    connect(btnPasteAddr, &QPushButton::clicked, this, &SettingsSignMessageWidgets::onPasteButtonSMClicked);
    connect(btnCopySig, &QPushButton::clicked, this, [this]() {
        const QString sig = ui->signatureOut_SM->text().trimmed();
        if (sig.isEmpty()) {
            inform(tr("No signature to copy."));
            return;
        }
        if (QApplication::clipboard()) {
            QApplication::clipboard()->setText(sig);
            inform(tr("Signature copied to clipboard."));
        }
    });
    connect(ui->pushButtonClear, &QPushButton::clicked, this, &SettingsSignMessageWidgets::onClearAll);
    connect(ui->pushSign, &QPushButton::clicked, [this](){onModeSelected(true);});
    connect(ui->pushVerify, &QPushButton::clicked, [this](){onModeSelected(false);});
}

SettingsSignMessageWidgets::~SettingsSignMessageWidgets()
{
    delete ui;
}

void SettingsSignMessageWidgets::applyTheme(bool isLight)
{
    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight));
    }
    if (hasStatusMsg && ui->statusLabel_SM) {
        ui->statusLabel_SM->setStyleSheet(SignMsgStatusCss(lastStatusSuccess, isLight));
    }
}

void SettingsSignMessageWidgets::changeTheme(bool isLightTheme, QString& theme)
{
    Q_UNUSED(theme);
    applyTheme(isLightTheme);
}

void SettingsSignMessageWidgets::setStatusMessage(const QString& text, bool isSuccess)
{
    hasStatusMsg = true;
    lastStatusSuccess = isSuccess;
    ui->statusLabel_SM->setStyleSheet(SignMsgStatusCss(isSuccess, isLightTheme()));
    ui->statusLabel_SM->setText(text);
    ui->statusLabel_SM->setVisible(true);
}

void SettingsSignMessageWidgets::clearStatusMessage()
{
    hasStatusMsg = false;
    ui->statusLabel_SM->clear();
    ui->statusLabel_SM->setVisible(false);
}

void SettingsSignMessageWidgets::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (ui->addressIn_SM) ui->addressIn_SM->setFocus();
}

void SettingsSignMessageWidgets::onModeSelected(bool isSign)
{
    this->isSign = isSign;
    updateMode();
}

void SettingsSignMessageWidgets::onGoClicked()
{
    if (isSign) {
        onSignMessageButtonSMClicked();
    } else {
        onVerifyMessage();
    }
}

void SettingsSignMessageWidgets::updateMode()
{
    QString subtitle;
    QString go;
    if (isSign) {
        lblCardHeader->setText(tr("Sign Message"));
        subtitle = tr("You can sign messages with your addresses to prove you own them. Be careful not to sign anything vague, as phishing attacks may try to trick you into signing your identity over to them. Only sign fully-detailed statements you agree to.");
        go = tr("Sign Message");
        ui->signatureOut_SM->setReadOnly(true);
        ui->signatureOut_SM->clear();
        ui->signatureOut_SM->setPlaceholderText(tr("Generated Base64 signature will appear here"));
        ui->signatureOut_SM->setProperty("settingsInput", false);
        ui->signatureOut_SM->setProperty("settingsInputReadOnly", true);
    } else {
        lblCardHeader->setText(tr("Verify Signature"));
        subtitle = tr("Enter the signing address, message (ensure you copy line breaks, spaces, tabs, etc. exactly) and signature below to verify the message. Be careful not to read more into the signature than what is in the signed message itself, to avoid being tricked by a man-in-the-middle attack.");
        go = tr("Verify Signature");
        ui->signatureOut_SM->setReadOnly(false);
        ui->signatureOut_SM->setPlaceholderText(tr("Paste Base64 signature to verify"));
        ui->signatureOut_SM->setProperty("settingsInputReadOnly", false);
        ui->signatureOut_SM->setProperty("settingsInput", true);
    }
    lblModeDesc->setText(subtitle);
    ui->pushButtonSave->setText(go);
    updateStyle(ui->signatureOut_SM);
    clearStatusMessage();
}

void SettingsSignMessageWidgets::setAddress_SM(const QString& address)
{
    ui->addressIn_SM->setText(address);
    ui->messageIn_SM->setFocus();
}

void SettingsSignMessageWidgets::onAddressBookButtonSMClicked()
{
    if (walletModel && walletModel->getAddressTableModel()) {
        AddressBookPage dlg(AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(walletModel->getAddressTableModel());
        if (dlg.exec()) {
            setAddress_SM(dlg.getReturnValue());
        }
    }
}

void SettingsSignMessageWidgets::onPasteButtonSMClicked()
{
    if (QApplication::clipboard()) {
        setAddress_SM(QApplication::clipboard()->text().trimmed());
    }
}

void SettingsSignMessageWidgets::onClearAll()
{
    ui->addressIn_SM->clear();
    ui->signatureOut_SM->clear();
    ui->messageIn_SM->clear();
    clearStatusMessage();
}

void SettingsSignMessageWidgets::onSignMessageButtonSMClicked()
{
    if (!walletModel)
        return;

    /* Clear old signature to ensure users don't get confused on error with an old signature displayed */
    ui->signatureOut_SM->clear();

    CTxDestination addr = DecodeDestination(ui->addressIn_SM->text().trimmed().toStdString());
    if (!IsValidDestination(addr)) {
        setStatusMessage(tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."), false);
        return;
    }
    const CKeyID* keyID = boost::get<CKeyID>(&addr);
    if (!keyID) {
        setStatusMessage(tr("The entered address does not refer to a P2PKH key.") + QString(" ") + tr("Please check the address and try again."), false);
        return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        setStatusMessage(tr("Wallet unlock was cancelled."), false);
        return;
    }

    CKey key;
    {
        if (!pwalletMain) {
            setStatusMessage(tr("Wallet is not available."), false);
            return;
        }
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->GetKey(*keyID, key)) {
            setStatusMessage(tr("Private key for the entered address is not available."), false);
            return;
        }
    }

    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << ui->messageIn_SM->document()->toPlainText().toStdString();

    std::vector<unsigned char> vchSig;
    if (!key.SignCompact(Hash(ss.begin(), ss.end()), vchSig)) {
        setStatusMessage(tr("Message signing failed."), false);
        return;
    }

    setStatusMessage(tr("Message signed successfully."), true);
    ui->signatureOut_SM->setText(QString::fromStdString(EncodeBase64(&vchSig[0], vchSig.size())));
}

void SettingsSignMessageWidgets::onVerifyMessage()
{
    CTxDestination addr = DecodeDestination(ui->addressIn_SM->text().trimmed().toStdString());
    if (!IsValidDestination(addr)) {
        setStatusMessage(tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."), false);
        return;
    }
    const CKeyID* keyID = boost::get<CKeyID>(&addr);
    if (!keyID) {
        setStatusMessage(tr("The entered address does not refer to a P2PKH key.") + QString(" ") + tr("Please check the address and try again."), false);
        return;
    }

    bool fInvalid = false;
    std::vector<unsigned char> vchSig = DecodeBase64(ui->signatureOut_SM->text().trimmed().toStdString().c_str(), &fInvalid);

    if (fInvalid || ui->signatureOut_SM->text().trimmed().isEmpty()) {
        setStatusMessage(tr("The signature could not be decoded.") + QString(" ") + tr("Please check the signature and try again."), false);
        return;
    }

    CDataStream ss(SER_GETHASH, 0);
    ss << strMessageMagic;
    ss << ui->messageIn_SM->document()->toPlainText().toStdString();

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(Hash(ss.begin(), ss.end()), vchSig)) {
        setStatusMessage(tr("The signature did not match the message digest.") + QString(" ") + tr("Please check the signature and try again."), false);
        return;
    }

    if (!(pubkey.GetID() == *keyID)) {
        setStatusMessage(tr("Message verification failed."), false);
        return;
    }

    setStatusMessage(tr("Message verified successfully."), true);
}

void SettingsSignMessageWidgets::onAddressesClicked()
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
    const int width = std::max(280, ui->addressIn_SM->width());

    if (!menuContacts) {
        menuContacts = new ContactsDropdown(width, height, this);
        menuContacts->setWalletModel(walletModel, AddressTableModel::Receive);
        connect(menuContacts, &ContactsDropdown::contactSelected, [this](QString address, QString label){
            Q_UNUSED(label);
            setAddress_SM(address);
        });
    }

    if (menuContacts->isVisible()) {
        menuContacts->hide();
        return;
    }

    menuContacts->resizeList(width, height);
    menuContacts->setStyleSheet(window ? window->styleSheet() : GUIUtil::loadStyleSheet());
    menuContacts->adjustSize();

    const QPoint pos = ui->addressIn_SM->mapTo(this, QPoint(0, ui->addressIn_SM->height() + 4));
    menuContacts->move(pos);
    menuContacts->show();
    menuContacts->raise();
}

void SettingsSignMessageWidgets::resizeMenu()
{
    if (menuContacts && menuContacts->isVisible()) {
        const int width = std::max(280, ui->addressIn_SM->width());
        menuContacts->resizeList(width, menuContacts->height());
        menuContacts->resize(width, menuContacts->height());
        const QPoint pos = ui->addressIn_SM->mapTo(this, QPoint(0, ui->addressIn_SM->height() + 4));
        menuContacts->move(pos);
    }
}

void SettingsSignMessageWidgets::resizeEvent(QResizeEvent *event)
{
    resizeMenu();
    QWidget::resizeEvent(event);
}
