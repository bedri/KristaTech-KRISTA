// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "askpassphrasedialog.h"
#include "ui_askpassphrasedialog.h"

#include "guiconstants.h"
#include "guiutil.h"
#include "support/cleanse.h"
#include "walletmodel.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/loadingdialog.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/kristatechgui.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace {
constexpr int MIN_NEW_PASSPHRASE_LEN = 8;

struct StrengthResult {
    int score;
    QString label;
    QString tone; // "none", "weak", "fair", "strong"
};

StrengthResult evaluatePassphraseStrength(const QString& pass)
{
    if (pass.isEmpty()) {
        return {0, QObject::tr("Not set"), QStringLiteral("none")};
    }

    const int len = pass.size();
    bool hasLower = false;
    bool hasUpper = false;
    bool hasDigit = false;
    bool hasSymbol = false;
    for (const QChar& c : pass) {
        if (c.isLower()) hasLower = true;
        else if (c.isUpper()) hasUpper = true;
        else if (c.isDigit()) hasDigit = true;
        else if (!c.isSpace()) hasSymbol = true;
    }

    const int classCount = (hasLower ? 1 : 0) + (hasUpper ? 1 : 0) + (hasDigit ? 1 : 0) + (hasSymbol ? 1 : 0);
    int wordCount = 0;
    bool inWord = false;
    for (const QChar& c : pass) {
        if (c.isSpace()) {
            inWord = false;
        } else if (!inWord) {
            inWord = true;
            ++wordCount;
        }
    }
    const bool isMultiWord = wordCount >= 4 && len >= 16;

    if (len < MIN_NEW_PASSPHRASE_LEN) {
        return {std::max(10, len * 3), QObject::tr("Too short (min %1 chars)").arg(MIN_NEW_PASSPHRASE_LEN), QStringLiteral("weak")};
    }

    int score = 25;
    if (len >= 10) score += 15;
    if (len >= 14) score += 15;
    if (len >= 18) score += 10;
    score += classCount * 10;
    if (isMultiWord) score += 20;
    score = std::min(100, score);

    if (score < 50) {
        return {score, QObject::tr("Weak"), QStringLiteral("weak")};
    }
    if (score < 78) {
        return {score, QObject::tr("Fair"), QStringLiteral("fair")};
    }
    return {score, QObject::tr("Strong"), QStringLiteral("strong")};
}

void cleanseQString(QString& s)
{
    if (!s.isEmpty()) {
        memory_cleanse(s.data(), static_cast<size_t>(s.size()) * sizeof(QChar));
        s.clear();
    }
}

void copyLineEditToSecureString(QLineEdit* edit, SecureString& out)
{
    if (!edit) {
        out.clear();
        return;
    }
    QString text = edit->text();
    QByteArray utf8 = text.toUtf8();
    if (!utf8.isEmpty()) {
        out.assign(utf8.constData(), static_cast<size_t>(utf8.size()));
        memory_cleanse(utf8.data(), static_cast<size_t>(utf8.size()));
        utf8.clear();
    } else {
        out.clear();
    }
    cleanseQString(text);
}
} // namespace

AskPassphraseDialog::AskPassphraseDialog(Mode mode, QWidget* parent, WalletModel* model, Context context) :
    QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
    ui(new Ui::AskPassphraseDialog),
    mode(mode),
    model(model),
    context(context),
    fCapsLock(false)
{
    ui->setupUi(this);
    setObjectName(QStringLiteral("AskPassphraseDialog"));
    setMinimumSize(560, 0);
    setMaximumSize(580, QWIDGETSIZE_MAX);

    ui->left->setVisible(false);
    if (ui->verticalLayout) {
        ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
        ui->verticalLayout->setSpacing(0);
    }

    const bool isEncrypt = (mode == Mode::Encrypt);
    const bool isChangePass = (mode == Mode::ChangePass);
    const bool isNewPassMode = (isEncrypt || isChangePass);
    const bool isOldPassMode = !isEncrypt;

    QString headerTitle;
    QString subtitleText;
    QString primaryBtnText;
    switch (mode) {
    case Mode::Encrypt:
        headerTitle = tr("Encrypt Wallet");
        primaryBtnText = tr("Continue to Security Check →");
        break;
    case Mode::UnlockAnonymize:
        headerTitle = tr("Unlock Wallet for Staking");
        subtitleText = tr("Enter your passphrase to unlock this wallet for PoS staking only. Sending and transfers remain locked.");
        primaryBtnText = tr("Unlock for Staking");
        break;
    case Mode::Unlock:
        headerTitle = tr("Unlock Wallet");
        subtitleText = tr("Enter your wallet passphrase to unlock spending and administrative operations.");
        primaryBtnText = tr("Unlock Wallet");
        break;
    case Mode::Decrypt:
        headerTitle = tr("Decrypt Wallet");
        subtitleText = tr("Enter your current passphrase to permanently remove disk encryption from wallet.dat.");
        primaryBtnText = tr("Decrypt Wallet");
        break;
    case Mode::ChangePass:
        headerTitle = tr("Change Passphrase");
        primaryBtnText = tr("Change Passphrase");
        break;
    }

    dialogCard = new QFrame(this);
    dialogCard->setObjectName(QStringLiteral("passDialogCard"));
    dialogCard->setAttribute(Qt::WA_StyledBackground, true);
    ui->verticalLayout->addWidget(dialogCard);

    QVBoxLayout* cardRoot = new QVBoxLayout(dialogCard);
    cardRoot->setContentsMargins(0, 0, 0, 0);
    cardRoot->setSpacing(0);

    // Header Row
    QWidget* headerBar = new QWidget(dialogCard);
    QHBoxLayout* headerLayout = new QHBoxLayout(headerBar);
    headerLayout->setContentsMargins(22, 16, 18, 14);
    headerLayout->setSpacing(10);

    lblHeaderTitle = new QLabel(headerTitle, headerBar);
    lblHeaderTitle->setObjectName(QStringLiteral("passHeaderTitle"));

    lblStepPill = new QLabel(tr("Step 1 of 2"), headerBar);
    lblStepPill->setObjectName(QStringLiteral("passStepPill"));
    lblStepPill->setProperty("stepTone", QStringLiteral("step1"));
    lblStepPill->setVisible(isEncrypt);

    clearLegacySettingsConstraints(ui->btnEsc);
    ui->btnEsc->setParent(headerBar);
    ui->btnEsc->setObjectName(QStringLiteral("passCloseBtn"));
    ui->btnEsc->setText(QString::fromUtf8("×"));
    ui->btnEsc->setFixedSize(30, 30);
    ui->btnEsc->setCursor(Qt::PointingHandCursor);
    ui->btnEsc->setFocusPolicy(Qt::NoFocus);

    headerLayout->addWidget(lblHeaderTitle);
    headerLayout->addStretch(1);
    headerLayout->addWidget(lblStepPill, 0, Qt::AlignVCenter);
    headerLayout->addWidget(ui->btnEsc, 0, Qt::AlignVCenter);
    cardRoot->addWidget(headerBar);

    QFrame* headerDiv = new QFrame(dialogCard);
    headerDiv->setObjectName(QStringLiteral("passDivider"));
    headerDiv->setFixedHeight(1);
    cardRoot->addWidget(headerDiv);

    stepStack = new QStackedWidget(dialogCard);
    cardRoot->addWidget(stepStack, 1);

    // =========================================================================
    // Page 0: Passphrase Entry & Level 1 Security Validation
    // =========================================================================
    QWidget* page1 = new QWidget(stepStack);
    QVBoxLayout* p1Layout = new QVBoxLayout(page1);
    p1Layout->setContentsMargins(22, 16, 22, 20);
    p1Layout->setSpacing(12);

    // Top Alert / Subtitle
    alertTopBox = new QFrame(page1);
    alertTopBox->setObjectName(QStringLiteral("passAlertBox"));
    alertTopBox->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* alertTopLayout = new QVBoxLayout(alertTopBox);
    alertTopLayout->setContentsMargins(14, 11, 14, 11);
    alertTopLayout->setSpacing(4);
    lblAlertTopTitle = new QLabel(alertTopBox);
    lblAlertTopTitle->setObjectName(QStringLiteral("passAlertTitle"));
    lblAlertTopBody = new QLabel(alertTopBox);
    lblAlertTopBody->setObjectName(QStringLiteral("passAlertBody"));
    lblAlertTopBody->setWordWrap(true);
    alertTopLayout->addWidget(lblAlertTopTitle);
    alertTopLayout->addWidget(lblAlertTopBody);

    lblSubtitle = new QLabel(subtitleText, page1);
    lblSubtitle->setObjectName(QStringLiteral("passSubtitle"));
    lblSubtitle->setWordWrap(true);

    if (isEncrypt) {
        alertTopBox->setProperty("alertTone", QStringLiteral("warn"));
        lblAlertTopTitle->setProperty("alertTone", QStringLiteral("warn"));
        lblAlertTopBody->setProperty("alertTone", QStringLiteral("warn"));
        lblAlertTopTitle->setText(tr("Security Alert: No Passphrase Recovery"));
        lblAlertTopBody->setText(tr("Encrypting wallet.dat locks all private keys behind this passphrase. There is no reset or recovery option — losing your passphrase permanently locks all KRISTA in this wallet."));
        p1Layout->addWidget(alertTopBox);
        lblSubtitle->setVisible(false);
    } else if (isChangePass) {
        alertTopBox->setProperty("alertTone", QStringLiteral("info"));
        lblAlertTopTitle->setProperty("alertTone", QStringLiteral("info"));
        lblAlertTopBody->setProperty("alertTone", QStringLiteral("info"));
        lblAlertTopTitle->setText(tr("Passphrase Rotation Notice"));
        lblAlertTopBody->setText(tr("Changing your passphrase re-encrypts wallet.dat with new key material. Export a fresh wallet backup immediately afterwards."));
        p1Layout->addWidget(alertTopBox);
        lblSubtitle->setVisible(false);
    } else {
        alertTopBox->setVisible(false);
        p1Layout->addWidget(lblSubtitle);
    }

    const QIcon watchIcon(isLightTheme() ? QStringLiteral("://ic-watch-password") : QStringLiteral("://ic-watch-password-white"));

    auto configurePassField = [&](QLabel* lbl, const QString& labelText, QLineEdit* edit, const QString& placeholder, QAction*& watchAct, bool visible) {
        clearLegacySettingsConstraints(lbl);
        lbl->setParent(page1);
        lbl->setObjectName(QStringLiteral("passFieldLabel"));
        lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        lbl->setText(labelText);
        lbl->setVisible(visible);

        clearLegacySettingsConstraints(edit);
        edit->setParent(page1);
        edit->setObjectName(QStringLiteral("passFieldInput"));
        edit->setFixedHeight(38);
        edit->setMaxLength(MAX_PASSPHRASE_SIZE);
        edit->setEchoMode(QLineEdit::Password);
        edit->setPlaceholderText(placeholder);
        edit->setAttribute(Qt::WA_MacShowFocusRect, false);
        edit->setVisible(visible);
        edit->installEventFilter(this);

        if (visible) {
            watchAct = edit->addAction(watchIcon, QLineEdit::TrailingPosition);
            watchAct->setToolTip(tr("Show / hide passphrase"));
            connect(watchAct, &QAction::triggered, this, &AskPassphraseDialog::togglePassVisibility);

            QVBoxLayout* grp = new QVBoxLayout();
            grp->setContentsMargins(0, 0, 0, 0);
            grp->setSpacing(5);
            grp->addWidget(lbl);
            grp->addWidget(edit);
            p1Layout->addLayout(grp);
        }
    };

    configurePassField(ui->passLabel1,
                       isOldPassMode ? (isChangePass ? tr("Current passphrase") : tr("Wallet passphrase")) : QString(),
                       ui->passEdit1,
                       tr("Enter wallet passphrase"),
                       actWatch1,
                       isOldPassMode);

    configurePassField(ui->passLabel2,
                       tr("New passphrase"),
                       ui->passEdit2,
                       tr("Minimum 8 characters (10+ recommended)"),
                       actWatch2,
                       isNewPassMode);

    // Live Strength Meter (Encrypt & ChangePass)
    strengthWidget = new QWidget(page1);
    QVBoxLayout* strengthLayout = new QVBoxLayout(strengthWidget);
    strengthLayout->setContentsMargins(0, 0, 0, 2);
    strengthLayout->setSpacing(5);

    QHBoxLayout* strengthTop = new QHBoxLayout();
    strengthTop->setContentsMargins(0, 0, 0, 0);
    QLabel* lblStrengthCaption = new QLabel(tr("Passphrase strength:"), strengthWidget);
    lblStrengthCaption->setObjectName(QStringLiteral("passHintText"));
    lblStrengthLevel = new QLabel(tr("Not set"), strengthWidget);
    lblStrengthLevel->setObjectName(QStringLiteral("passStrengthBadge"));
    lblStrengthLevel->setProperty("strengthTone", QStringLiteral("none"));
    strengthTop->addWidget(lblStrengthCaption);
    strengthTop->addStretch(1);
    strengthTop->addWidget(lblStrengthLevel);

    strengthBar = new QProgressBar(strengthWidget);
    strengthBar->setObjectName(QStringLiteral("passStrengthBar"));
    strengthBar->setRange(0, 100);
    strengthBar->setValue(0);
    strengthBar->setTextVisible(false);
    strengthBar->setFixedHeight(6);
    strengthBar->setProperty("strengthTone", QStringLiteral("none"));

    lblStrengthHint = new QLabel(tr("Recommended: 10+ random characters with mixed types, or 4+ random words."), strengthWidget);
    lblStrengthHint->setObjectName(QStringLiteral("passHintText"));
    lblStrengthHint->setWordWrap(true);

    strengthLayout->addLayout(strengthTop);
    strengthLayout->addWidget(strengthBar);
    strengthLayout->addWidget(lblStrengthHint);
    strengthWidget->setVisible(isNewPassMode);
    if (isNewPassMode) {
        p1Layout->addWidget(strengthWidget);
    }

    configurePassField(ui->passLabel3,
                       tr("Repeat passphrase"),
                       ui->passEdit3,
                       tr("Re-enter passphrase to verify"),
                       actWatch3,
                       isNewPassMode);

    // Live Inline Alert Box (CapsLock / Short passphrase / Mismatch)
    inlineAlertBox = new QFrame(page1);
    inlineAlertBox->setObjectName(QStringLiteral("passInlineAlertBox"));
    inlineAlertBox->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* inlineAlertLayout = new QVBoxLayout(inlineAlertBox);
    inlineAlertLayout->setContentsMargins(12, 9, 12, 9);
    lblInlineAlert = new QLabel(inlineAlertBox);
    lblInlineAlert->setObjectName(QStringLiteral("passInlineAlertText"));
    lblInlineAlert->setWordWrap(true);
    inlineAlertLayout->addWidget(lblInlineAlert);
    inlineAlertBox->setVisible(false);
    p1Layout->addWidget(inlineAlertBox);

    // Level 1 Risk Acknowledgment Checkbox (Encrypt only)
    chkAckRisk = new QCheckBox(
        tr("I have written this passphrase down securely offline and understand\n"
           "that losing it permanently locks all funds in this wallet."),
        page1);
    chkAckRisk->setObjectName(QStringLiteral("passAckCheck"));
    chkAckRisk->setCursor(Qt::PointingHandCursor);
    chkAckRisk->setFocusPolicy(Qt::NoFocus);
    chkAckRisk->setVisible(isEncrypt);
    if (isEncrypt) {
        p1Layout->addWidget(chkAckRisk);
    }

    // Page 1 Footer
    QFrame* p1Div = new QFrame(page1);
    p1Div->setObjectName(QStringLiteral("passDivider"));
    p1Div->setFixedHeight(1);
    p1Layout->addWidget(p1Div);

    QHBoxLayout* p1Footer = new QHBoxLayout();
    p1Footer->setContentsMargins(0, 4, 0, 0);
    p1Footer->setSpacing(10);

    btnCancel = new QPushButton(tr("Cancel"), page1);
    btnCancel->setObjectName(QStringLiteral("passSecondaryBtn"));
    btnCancel->setFixedHeight(38);
    btnCancel->setMinimumWidth(100);
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFocusPolicy(Qt::NoFocus);
    btnCancel->setAutoDefault(false);

    clearLegacySettingsConstraints(ui->pushButtonOk);
    ui->pushButtonOk->setParent(page1);
    ui->pushButtonOk->setObjectName(QStringLiteral("passPrimaryBtn"));
    ui->pushButtonOk->setText(primaryBtnText);
    ui->pushButtonOk->setFixedHeight(38);
    ui->pushButtonOk->setMinimumWidth(isEncrypt ? 220 : 165);
    ui->pushButtonOk->setCursor(Qt::PointingHandCursor);
    ui->pushButtonOk->setAutoDefault(false);

    p1Footer->addWidget(btnCancel);
    p1Footer->addStretch(1);
    p1Footer->addWidget(ui->pushButtonOk);
    p1Layout->addLayout(p1Footer);

    stepStack->addWidget(page1);

    // =========================================================================
    // Page 1: Level 2 Critical Confirmation (Mode::Encrypt only)
    // =========================================================================
    if (isEncrypt) {
        QWidget* page2 = new QWidget(stepStack);
        QVBoxLayout* p2Layout = new QVBoxLayout(page2);
        p2Layout->setContentsMargins(22, 16, 22, 20);
        p2Layout->setSpacing(12);

        step2AlertBox = new QFrame(page2);
        step2AlertBox->setObjectName(QStringLiteral("passAlertBox"));
        step2AlertBox->setAttribute(Qt::WA_StyledBackground, true);
        step2AlertBox->setProperty("alertTone", QStringLiteral("warn"));
        QVBoxLayout* s2AlertLayout = new QVBoxLayout(step2AlertBox);
        s2AlertLayout->setContentsMargins(14, 12, 14, 12);
        s2AlertLayout->setSpacing(6);

        QLabel* s2Title = new QLabel(tr("Level 2 Security Verification — Read Carefully"), step2AlertBox);
        s2Title->setObjectName(QStringLiteral("passAlertTitle"));
        s2Title->setProperty("alertTone", QStringLiteral("warn"));

        QLabel* s2Body = new QLabel(
            QStringLiteral("• <b>") + tr("Permanent Lockout:") + QStringLiteral("</b> ") +
            tr("If you lose or forget this passphrase, nobody can recover or unlock your KRISTA.") +
            QStringLiteral("<br><br>• <b>") + tr("Replace Prior Backups:") + QStringLiteral("</b> ") +
            tr("Previous unencrypted wallet.dat backups will become obsolete once new keys are generated. Create a fresh encrypted backup immediately after restarting.") +
            QStringLiteral("<br><br>• <b>") + tr("Automatic Wallet Closure:") + QStringLiteral("</b> ") +
            tr("KristaTech Wallet will close cleanly right after encryption finishes so unencrypted key memory is wiped."),
            step2AlertBox);
        s2Body->setObjectName(QStringLiteral("passAlertBody"));
        s2Body->setProperty("alertTone", QStringLiteral("warn"));
        s2Body->setWordWrap(true);
        s2Body->setTextFormat(Qt::RichText);

        s2AlertLayout->addWidget(s2Title);
        s2AlertLayout->addWidget(s2Body);
        p2Layout->addWidget(step2AlertBox);

        step2AuditBox = new QFrame(page2);
        step2AuditBox->setObjectName(QStringLiteral("passAlertBox"));
        step2AuditBox->setAttribute(Qt::WA_StyledBackground, true);
        step2AuditBox->setProperty("alertTone", QStringLiteral("info"));
        QVBoxLayout* auditLayout = new QVBoxLayout(step2AuditBox);
        auditLayout->setContentsMargins(14, 10, 14, 10);
        lblStep2Audit = new QLabel(step2AuditBox);
        lblStep2Audit->setObjectName(QStringLiteral("passAlertBody"));
        lblStep2Audit->setProperty("alertTone", QStringLiteral("info"));
        lblStep2Audit->setWordWrap(true);
        lblStep2Audit->setTextFormat(Qt::RichText);
        auditLayout->addWidget(lblStep2Audit);
        p2Layout->addWidget(step2AuditBox);

        QVBoxLayout* confirmGroup = new QVBoxLayout();
        confirmGroup->setContentsMargins(0, 0, 0, 0);
        confirmGroup->setSpacing(6);
        QLabel* lblConfirmPrompt = new QLabel(tr("Type ENCRYPT (all caps) to unlock final wallet encryption:"), page2);
        lblConfirmPrompt->setObjectName(QStringLiteral("passFieldLabel"));
        lblConfirmPrompt->setWordWrap(true);

        confirmWordEdit = new QLineEdit(page2);
        confirmWordEdit->setObjectName(QStringLiteral("passFieldInput"));
        confirmWordEdit->setFixedHeight(38);
        confirmWordEdit->setMaxLength(16);
        confirmWordEdit->setPlaceholderText(QStringLiteral("ENCRYPT"));
        confirmWordEdit->setAttribute(Qt::WA_MacShowFocusRect, false);

        confirmGroup->addWidget(lblConfirmPrompt);
        confirmGroup->addWidget(confirmWordEdit);
        p2Layout->addLayout(confirmGroup);

        QFrame* p2Div = new QFrame(page2);
        p2Div->setObjectName(QStringLiteral("passDivider"));
        p2Div->setFixedHeight(1);
        p2Layout->addWidget(p2Div);

        QHBoxLayout* p2Footer = new QHBoxLayout();
        p2Footer->setContentsMargins(0, 4, 0, 0);
        p2Footer->setSpacing(10);

        btnBack = new QPushButton(tr("← Back"), page2);
        btnBack->setObjectName(QStringLiteral("passSecondaryBtn"));
        btnBack->setFixedHeight(38);
        btnBack->setMinimumWidth(96);
        btnBack->setCursor(Qt::PointingHandCursor);
        btnBack->setFocusPolicy(Qt::NoFocus);
        btnBack->setAutoDefault(false);

        btnCancelStep2 = new QPushButton(tr("Cancel"), page2);
        btnCancelStep2->setObjectName(QStringLiteral("passSecondaryBtn"));
        btnCancelStep2->setFixedHeight(38);
        btnCancelStep2->setMinimumWidth(96);
        btnCancelStep2->setCursor(Qt::PointingHandCursor);
        btnCancelStep2->setFocusPolicy(Qt::NoFocus);
        btnCancelStep2->setAutoDefault(false);

        btnConfirmEncrypt = new QPushButton(tr("Encrypt Wallet Now"), page2);
        btnConfirmEncrypt->setObjectName(QStringLiteral("passDangerBtn"));
        btnConfirmEncrypt->setFixedHeight(38);
        btnConfirmEncrypt->setMinimumWidth(185);
        btnConfirmEncrypt->setCursor(Qt::PointingHandCursor);
        btnConfirmEncrypt->setAutoDefault(false);
        btnConfirmEncrypt->setEnabled(false);

        p2Footer->addWidget(btnBack);
        p2Footer->addWidget(btnCancelStep2);
        p2Footer->addStretch(1);
        p2Footer->addWidget(btnConfirmEncrypt);
        p2Layout->addLayout(p2Footer);

        stepStack->addWidget(page2);

        connect(chkAckRisk, &QCheckBox::toggled, this, &AskPassphraseDialog::textChanged);
        connect(confirmWordEdit, &QLineEdit::textChanged, this, [this]() {
            if (btnConfirmEncrypt && confirmWordEdit) {
                btnConfirmEncrypt->setEnabled(confirmWordEdit->text().trimmed() == QLatin1String("ENCRYPT"));
            }
        });
        connect(btnBack, &QPushButton::clicked, this, [this]() {
            showEncryptStep(0);
        });
        connect(btnCancelStep2, &QPushButton::clicked, this, &QDialog::reject);
        connect(btnConfirmEncrypt, &QPushButton::clicked, this, &AskPassphraseDialog::onConfirmEncryptClicked);
    }

    stepStack->setCurrentIndex(0);
    setStyleSheet(buildDialogStyleSheet(isLightTheme()));

    textChanged();
    connect(ui->passEdit1, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);
    connect(ui->passEdit2, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);
    connect(ui->passEdit3, &QLineEdit::textChanged, this, &AskPassphraseDialog::textChanged);
    connect(ui->pushButtonOk, &QPushButton::clicked, this, &AskPassphraseDialog::accept);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->btnEsc, &QPushButton::clicked, this, &QDialog::reject);
}

QString AskPassphraseDialog::buildDialogStyleSheet(bool isLight) const
{
    const QString cardBg     = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#131B2E");
    const QString cardBrd    = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2A3D61");
    const QString divider    = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2C46");
    const QString titleCol   = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString subCol     = isLight ? QStringLiteral("#475569") : QStringLiteral("#CBD5E1");
    const QString hintCol    = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString inputBg    = isLight ? QStringLiteral("#F8FAFC") : QStringLiteral("#0D1424");
    const QString inputBrd   = isLight ? QStringLiteral("#B6C5DC") : QStringLiteral("#33486E");
    const QString inputFocus = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#0A101E");
    const QString disBg      = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#111A2E");
    const QString disFg      = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#64748B");

    const QString primBg     = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString primFg     = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString primHov    = isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860");
    const QString secBg      = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1C2942");
    const QString secFg      = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString secBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#33486E");
    const QString secHov     = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#26385A");

    const QString warnBg     = isLight ? QStringLiteral("#FEF2F2") : QStringLiteral("#2A1519");
    const QString warnBrd    = isLight ? QStringLiteral("#F5B5AE") : QStringLiteral("#6E2B2A");
    const QString warnAccent = isLight ? QStringLiteral("#C94A3E") : QStringLiteral("#F5897C");
    const QString warnTitle  = isLight ? QStringLiteral("#991B1B") : QStringLiteral("#FCA5A5");
    const QString warnBody   = isLight ? QStringLiteral("#7F1D1D") : QStringLiteral("#FECACA");

    const QString infoBg     = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#182642");
    const QString infoBrd    = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#2E4775");
    const QString infoAccent = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#7695CD");
    const QString infoTitle  = isLight ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA");
    const QString infoBody   = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#CBD5E1");

    const QString fairCol    = isLight ? QStringLiteral("#6356B5") : QStringLiteral("#8B7FD6");
    const QString strongCol  = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#7695CD");
    const QString dangerBtn  = QStringLiteral("#C94A3E");
    const QString dangerHov  = QStringLiteral("#B53B30");
    const QString dangerDis  = isLight ? QStringLiteral("#E2E8F0") : QStringLiteral("#2A1A1D");
    const QString dangerDisF = isLight ? QStringLiteral("#94A3B8") : QStringLiteral("#7F5A5B");

    QString css = QString(
        "QDialog#AskPassphraseDialog { background-color: transparent; border: none; }"
        "QFrame#passDialogCard { background-color: %1; border: 1.5px solid %2; border-radius: 12px; }"
        "QFrame#passDivider { background-color: %3; border: none; }"
        "QLabel#passHeaderTitle { color: %4; font-size: 19px; font-weight: 700; background: transparent; border: none; }"
        "QLabel#passSubtitle { color: %5; font-size: 13px; font-weight: 400; background: transparent; border: none; }"
        "QLabel#passFieldLabel { color: %4; font-size: 13px; font-weight: 600; background: transparent; border: none; qproperty-alignment: 'AlignLeft | AlignVCenter'; }"
        "QLabel#passHintText { color: %6; font-size: 11px; font-weight: 500; background: transparent; border: none; }"
        "QPushButton#passCloseBtn { background-color: transparent; color: %6; border: none; border-radius: 6px; font-size: 20px; font-weight: 600; }"
        "QPushButton#passCloseBtn:hover { background-color: %7; color: %4; }"
    ).arg(cardBg, cardBrd, divider, titleCol, subCol, hintCol, secHov);

    css += QString(
        "QLabel#passStepPill[stepTone=\"step1\"] { background-color: %1; color: %2; border: 1px solid %3; border-radius: 11px; padding: 3px 10px; font-size: 11px; font-weight: 700; }"
        "QLabel#passStepPill[stepTone=\"step2\"] { background-color: %4; color: %5; border: 1px solid %6; border-radius: 11px; padding: 3px 10px; font-size: 11px; font-weight: 700; }"
        "QFrame#passAlertBox[alertTone=\"warn\"] { background-color: %4; border: 1px solid %6; border-left: 3.5px solid %7; border-radius: 8px; }"
        "QFrame#passAlertBox[alertTone=\"info\"] { background-color: %1; border: 1px solid %3; border-left: 3.5px solid %8; border-radius: 8px; }"
        "QLabel#passAlertTitle[alertTone=\"warn\"] { color: %5; font-size: 13px; font-weight: 700; background: transparent; border: none; }"
        "QLabel#passAlertTitle[alertTone=\"info\"] { color: %9; font-size: 13px; font-weight: 700; background: transparent; border: none; }"
    ).arg(infoBg, primFg, infoBrd, warnBg, warnTitle, warnBrd, warnAccent, infoAccent, infoTitle);

    css += QString(
        "QLabel#passAlertBody[alertTone=\"warn\"] { color: %1; font-size: 12px; font-weight: 500; background: transparent; border: none; }"
        "QLabel#passAlertBody[alertTone=\"info\"] { color: %2; font-size: 12px; font-weight: 500; background: transparent; border: none; }"
        "QFrame#passInlineAlertBox { background-color: %3; border: 1px solid %4; border-left: 3px solid %5; border-radius: 7px; }"
        "QLabel#passInlineAlertText { color: %6; font-size: 12px; font-weight: 600; background: transparent; border: none; }"
        "QLineEdit#passFieldInput { background-color: %7; color: %8; border: 1.5px solid %9; border-radius: 7px; padding: 0px 10px; font-size: 13px; font-weight: 500; selection-background-color: #5576B2; selection-color: #FFFFFF; }"
    ).arg(warnBody, infoBody, warnBg, warnBrd, warnAccent, warnTitle, inputBg, titleCol, inputBrd);

    css += QString(
        "QLineEdit#passFieldInput:hover { border-color: #7695CD; }"
        "QLineEdit#passFieldInput:focus { border-color: #5576B2; background-color: %1; }"
        "QProgressBar#passStrengthBar { background-color: %2; border: none; border-radius: 3px; }"
        "QProgressBar#passStrengthBar[strengthTone=\"none\"]::chunk { background-color: %2; border-radius: 3px; }"
        "QProgressBar#passStrengthBar[strengthTone=\"weak\"]::chunk { background-color: %3; border-radius: 3px; }"
        "QProgressBar#passStrengthBar[strengthTone=\"fair\"]::chunk { background-color: %4; border-radius: 3px; }"
        "QProgressBar#passStrengthBar[strengthTone=\"strong\"]::chunk { background-color: %5; border-radius: 3px; }"
        "QLabel#passStrengthBadge { font-size: 12px; font-weight: 700; background: transparent; border: none; }"
        "QLabel#passStrengthBadge[strengthTone=\"none\"] { color: %6; }"
        "QLabel#passStrengthBadge[strengthTone=\"weak\"] { color: %3; }"
        "QLabel#passStrengthBadge[strengthTone=\"fair\"] { color: %4; }"
        "QLabel#passStrengthBadge[strengthTone=\"strong\"] { color: %5; }"
    ).arg(inputFocus, divider, warnAccent, fairCol, strongCol, hintCol);

    css += QString(
        "QCheckBox#passAckCheck { color: %1; font-size: 12px; font-weight: 600; spacing: 10px; background: transparent; }"
        "QCheckBox#passAckCheck::indicator { width: 18px; height: 18px; }"
        "QCheckBox#passAckCheck::indicator:unchecked { image: url(\"://ic-check-box\"); }"
        "QCheckBox#passAckCheck::indicator:checked { image: url(\"://ic-check-active\"); }"
        "QPushButton#passPrimaryBtn { background-color: %2; color: %3; border: 1px solid #7695CD; border-radius: 7px; padding: 0px 16px; font-size: 13px; font-weight: 600; }"
        "QPushButton#passPrimaryBtn:hover { background-color: %4; border-color: #5576B2; }"
        "QPushButton#passPrimaryBtn:disabled { background-color: %5; color: %6; border-color: %7; }"
        "QPushButton#passSecondaryBtn { background-color: %8; color: %9; border: 1px solid %7; border-radius: 7px; padding: 0px 14px; font-size: 13px; font-weight: 600; }"
    ).arg(titleCol, primBg, primFg, primHov, disBg, disFg, secBrd, secBg, secFg);

    css += QString(
        "QPushButton#passSecondaryBtn:hover { background-color: %1; border-color: #7695CD; color: %2; }"
        "QPushButton#passDangerBtn { background-color: %3; color: #FFFFFF; border: 1px solid %4; border-radius: 7px; padding: 0px 18px; font-size: 13px; font-weight: 700; }"
        "QPushButton#passDangerBtn:hover { background-color: %4; }"
        "QPushButton#passDangerBtn:disabled { background-color: %5; color: %6; border-color: %7; }"
    ).arg(secHov, titleCol, dangerBtn, dangerHov, dangerDis, dangerDisF, divider);

    return css;
}

void AskPassphraseDialog::togglePassVisibility()
{
    passVisible = !passVisible;
    const QLineEdit::EchoMode modeEcho = passVisible ? QLineEdit::Normal : QLineEdit::Password;
    if (ui->passEdit1) ui->passEdit1->setEchoMode(modeEcho);
    if (ui->passEdit2) ui->passEdit2->setEchoMode(modeEcho);
    if (ui->passEdit3) ui->passEdit3->setEchoMode(modeEcho);
}

AskPassphraseDialog::~AskPassphraseDialog()
{
    // Overwrite passphrase inputs so they do not linger around in memory
    if (ui->passEdit1) ui->passEdit1->setText(QString(" ").repeated(ui->passEdit1->text().size()));
    if (ui->passEdit2) ui->passEdit2->setText(QString(" ").repeated(ui->passEdit2->text().size()));
    if (ui->passEdit3) ui->passEdit3->setText(QString(" ").repeated(ui->passEdit3->text().size()));
    if (confirmWordEdit) confirmWordEdit->setText(QString(" ").repeated(confirmWordEdit->text().size()));
    newpassCache.clear();
    delete ui;
}

void AskPassphraseDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (mode == Mode::Encrypt && ui->passEdit2) ui->passEdit2->setFocus();
    else if (ui->passEdit1) ui->passEdit1->setFocus();
}

void AskPassphraseDialog::recenterInParent()
{
    if (!layout()) return;
    if (stepStack && stepStack->currentWidget()) {
        stepStack->currentWidget()->updateGeometry();
    }
    stepStack->updateGeometry();
    layout()->activate();
    adjustSize();
    if (QWidget* p = parentWidget()) {
        const int xPos = std::max(16, (p->width() - width()) / 2);
        const int yPos = std::max(16, (p->height() - height()) / 2);
        move(xPos, yPos);
    }
}

void AskPassphraseDialog::showEncryptStep(int stepIndex)
{
    if (!stepStack || mode != Mode::Encrypt) return;
    stepStack->setCurrentIndex(stepIndex);

    if (stepIndex == 1) {
        if (lblStepPill) {
            lblStepPill->setText(tr("Step 2 of 2"));
            lblStepPill->setProperty("stepTone", QStringLiteral("step2"));
            updateStyle(lblStepPill);
        }
        QString pass = ui->passEdit2 ? ui->passEdit2->text() : QString();
        const int passLen = pass.size();
        const StrengthResult res = evaluatePassphraseStrength(pass);
        cleanseQString(pass);
        const bool isWeak = (res.tone == QLatin1String("weak"));
        if (step2AuditBox && lblStep2Audit) {
            const QString tone = isWeak ? QStringLiteral("warn") : QStringLiteral("info");
            step2AuditBox->setProperty("alertTone", tone);
            lblStep2Audit->setProperty("alertTone", tone);
            QString summary = tr("Selected passphrase strength: <b>%1</b> (%2 characters)")
                .arg(res.label.toHtmlEscaped())
                .arg(passLen);
            if (isWeak) {
                summary += QStringLiteral("<br>") + tr("Caution: Your passphrase satisfies the minimum length but is rated weak. Consider pressing Back to choose a stronger passphrase.");
            }
            lblStep2Audit->setText(summary);
            updateStyle(step2AuditBox);
            updateStyle(lblStep2Audit);
        }
        if (confirmWordEdit) {
            confirmWordEdit->clear();
            confirmWordEdit->setFocus();
        }
        if (btnConfirmEncrypt) {
            btnConfirmEncrypt->setEnabled(false);
        }
    } else {
        if (lblStepPill) {
            lblStepPill->setText(tr("Step 1 of 2"));
            lblStepPill->setProperty("stepTone", QStringLiteral("step1"));
            updateStyle(lblStepPill);
        }
        if (confirmWordEdit) confirmWordEdit->clear();
        if (ui->passEdit2) ui->passEdit2->setFocus();
    }

    recenterInParent();
}

void AskPassphraseDialog::accept()
{
    if (!model)
        return;

    if (mode == Mode::Encrypt) {
        QString p2 = ui->passEdit2 ? ui->passEdit2->text() : QString();
        QString p3 = ui->passEdit3 ? ui->passEdit3->text() : QString();
        const bool okLen = p2.size() >= MIN_NEW_PASSPHRASE_LEN && p2 == p3;
        cleanseQString(p2);
        cleanseQString(p3);
        if (!okLen || !chkAckRisk || !chkAckRisk->isChecked()) {
            return;
        }
        if (stepStack && stepStack->currentIndex() == 0) {
            showEncryptStep(1);
            return;
        }
        onConfirmEncryptClicked();
        return;
    }

    SecureString oldpass, newpass1, newpass2;
    oldpass.reserve(MAX_PASSPHRASE_SIZE);
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    newpass2.reserve(MAX_PASSPHRASE_SIZE);
    copyLineEditToSecureString(ui->passEdit1, oldpass);
    copyLineEditToSecureString(ui->passEdit2, newpass1);
    copyLineEditToSecureString(ui->passEdit3, newpass2);

    switch (mode) {
    case Mode::Encrypt:
        break;
    case Mode::UnlockAnonymize:
        if (!model->setWalletLocked(false, oldpass, true)) {
            showThemedNotice(this, tr("Wallet unlock failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."), true);
        } else {
            QDialog::accept(); // Success
        }
        break;
    case Mode::Unlock:
        if (!model->setWalletLocked(false, oldpass, false)) {
            showThemedNotice(this, tr("Wallet unlock failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."), true);
        } else {
            QDialog::accept(); // Success
        }
        break;
    case Mode::Decrypt:
        if (!model->setWalletEncrypted(false, oldpass)) {
            showThemedNotice(this, tr("Wallet decryption failed"),
                tr("The passphrase entered for the wallet decryption was incorrect."), true);
        } else {
            QDialog::accept(); // Success
        }
        break;
    case Mode::ChangePass:
        if (newpass1.size() < MIN_NEW_PASSPHRASE_LEN) {
            showThemedNotice(this, tr("Wallet encryption failed"),
                tr("The new passphrase must be at least %1 characters long.").arg(MIN_NEW_PASSPHRASE_LEN), true);
            break;
        }
        if (newpass1 == newpass2) {
            if (model->changePassphrase(oldpass, newpass1)) {
                hide();
                openStandardDialog(tr("Passphrase Updated"),
                    tr("Your wallet passphrase was successfully changed. Remember to export a fresh backup of wallet.dat."));
                QDialog::accept(); // Success
            } else {
                showThemedNotice(this, tr("Wallet encryption failed"),
                    tr("The current passphrase entered for the wallet decryption was incorrect."), true);
            }
        } else {
            showThemedNotice(this, tr("Wallet encryption failed"),
                tr("The supplied passphrases do not match."), true);
        }
        break;
    }
}

void AskPassphraseDialog::onConfirmEncryptClicked()
{
    if (!model || mode != Mode::Encrypt) return;
    if (!confirmWordEdit || confirmWordEdit->text().trimmed() != QLatin1String("ENCRYPT")) {
        return;
    }

    QString p2 = ui->passEdit2 ? ui->passEdit2->text() : QString();
    QString p3 = ui->passEdit3 ? ui->passEdit3->text() : QString();
    const bool matchOk = (p2.size() >= MIN_NEW_PASSPHRASE_LEN && p2 == p3);
    cleanseQString(p2);
    cleanseQString(p3);
    if (!matchOk) {
        return;
    }

    SecureString newpass1;
    newpass1.reserve(MAX_PASSPHRASE_SIZE);
    copyLineEditToSecureString(ui->passEdit2, newpass1);

    hide();
    newpassCache = newpass1;
    KRISTATECHGUI* window = qobject_cast<KRISTATECHGUI*>(parentWidget());
    LoadingDialog* dialog = new LoadingDialog(window ? static_cast<QWidget*>(window) : this);
    dialog->execute(this, 1);
    if (window) {
        openDialogWithOpaqueBackgroundFullScreen(dialog, window);
    } else {
        dialog->exec();
    }
}

void AskPassphraseDialog::updateStrengthMeter()
{
    if (mode != Mode::Encrypt && mode != Mode::ChangePass) return;
    if (!strengthBar || !lblStrengthLevel || !ui->passEdit2) return;

    const StrengthResult res = evaluatePassphraseStrength(ui->passEdit2->text());
    strengthBar->setValue(res.score);
    strengthBar->setProperty("strengthTone", res.tone);
    lblStrengthLevel->setText(res.label);
    lblStrengthLevel->setProperty("strengthTone", res.tone);
    updateStyle(strengthBar);
    updateStyle(lblStrengthLevel);
}

void AskPassphraseDialog::updateActionEnabledState()
{
    bool acceptable = false;
    switch (mode) {
    case Mode::Encrypt:
        acceptable = ui->passEdit2 && ui->passEdit3 &&
                     ui->passEdit2->text().size() >= MIN_NEW_PASSPHRASE_LEN &&
                     ui->passEdit2->text() == ui->passEdit3->text() &&
                     chkAckRisk && chkAckRisk->isChecked();
        break;
    case Mode::UnlockAnonymize:
    case Mode::Unlock:
    case Mode::Decrypt:
        acceptable = ui->passEdit1 && !ui->passEdit1->text().isEmpty();
        break;
    case Mode::ChangePass:
        acceptable = ui->passEdit1 && ui->passEdit2 && ui->passEdit3 &&
                     !ui->passEdit1->text().isEmpty() &&
                     ui->passEdit2->text().size() >= MIN_NEW_PASSPHRASE_LEN &&
                     ui->passEdit2->text() == ui->passEdit3->text();
        break;
    }
    if (ui->pushButtonOk) {
        ui->pushButtonOk->setEnabled(acceptable);
    }
}

void AskPassphraseDialog::textChanged()
{
    updateStrengthMeter();
    updateWarningsLabel();
    updateActionEnabledState();
}

bool AskPassphraseDialog::event(QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_CapsLock) {
            fCapsLock = !fCapsLock;
            updateWarningsLabel();
        }

        if (ke->key() == Qt::Key_Enter || ke->key() == Qt::Key_Return) {
            if (mode == Mode::Encrypt && stepStack && stepStack->currentIndex() == 1) {
                if (btnConfirmEncrypt && btnConfirmEncrypt->isEnabled()) {
                    onConfirmEncryptClicked();
                }
            } else if (ui->pushButtonOk && ui->pushButtonOk->isEnabled()) {
                accept();
            }
            return true;
        }
    }
    return QDialog::event(event);
}

bool AskPassphraseDialog::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        QString str = ke->text();
        if (str.length() != 0) {
            const QChar* psz = str.unicode();
            bool fShift = (ke->modifiers() & Qt::ShiftModifier) != 0;
            if ((fShift && *psz >= 'a' && *psz <= 'z') || (!fShift && *psz >= 'A' && *psz <= 'Z')) {
                fCapsLock = true;
            } else if (psz->isLetter()) {
                fCapsLock = false;
            }
        }
        updateWarningsLabel();
    }

    return QDialog::eventFilter(object, event);
}

bool AskPassphraseDialog::openStandardDialog(QString title, QString body, QString okBtn, QString cancelBtn)
{
    KRISTATECHGUI* gui = qobject_cast<KRISTATECHGUI*>(parentWidget());
    return showThemedConfirm(gui ? static_cast<QWidget*>(gui) : this, title, body, okBtn, cancelBtn, false);
}

void AskPassphraseDialog::updateWarningsLabel()
{
    QStringList alerts;
    if (fCapsLock) {
        alerts << QStringLiteral("• ") + tr("Caps Lock is ON — verify letter casing carefully.");
    }

    if (mode == Mode::Encrypt || mode == Mode::ChangePass) {
        QString p2 = ui->passEdit2 ? ui->passEdit2->text() : QString();
        QString p3 = ui->passEdit3 ? ui->passEdit3->text() : QString();
        if (!p2.isEmpty() && p2.size() < MIN_NEW_PASSPHRASE_LEN) {
            alerts << QStringLiteral("• ") + tr("Passphrase must be at least %1 characters long.").arg(MIN_NEW_PASSPHRASE_LEN);
        }
        if (!p3.isEmpty() && p2 != p3) {
            alerts << QStringLiteral("• ") + tr("Passphrases do not match.");
        }
        cleanseQString(p2);
        cleanseQString(p3);
    }

    const bool hadAlert = inlineAlertBox && inlineAlertBox->isVisible();
    const bool hasAlert = !alerts.isEmpty();

    if (inlineAlertBox && lblInlineAlert) {
        if (hasAlert) {
            lblInlineAlert->setText(alerts.join(QStringLiteral("<br>")));
            inlineAlertBox->setVisible(true);
        } else {
            lblInlineAlert->clear();
            inlineAlertBox->setVisible(false);
        }
        if (hadAlert != hasAlert && isVisible()) {
            adjustSize();
        }
    }
}

void AskPassphraseDialog::warningMessage()
{
    hide();
    KRISTATECHGUI* gui = qobject_cast<KRISTATECHGUI*>(parentWidget());
    const QString title = tr("Wallet Encrypted — Action Required");
    const QString body =
        tr("Your wallet has been encrypted with AES-256.") + QStringLiteral("\n\n") +
        tr("KristaTech will close now to finish the encryption process cleanly and clear unencrypted keys from memory.") +
        QStringLiteral("\n\n") +
        tr("CRITICAL BACKUP NOTICE: Previous unencrypted backups of wallet.dat are now obsolete and will not recover newly generated keys. Open Settings → Backup & Passphrase after restarting to export a fresh encrypted backup.");

    showThemedNotice(gui ? static_cast<QWidget*>(gui) : this, title, body, true, tr("Close Wallet Now"));
    QDialog::accept();
    QApplication::quit();
}

void AskPassphraseDialog::errorEncryptingWallet()
{
    showThemedNotice(this, tr("Wallet encryption failed"),
        tr("Wallet encryption failed due to an internal error. Your wallet was not encrypted."), true);
    QDialog::reject();
}

void AskPassphraseDialog::run(int type)
{
    if (type == 1) {
        if (!newpassCache.empty()) {
            const bool ok = model->setWalletEncrypted(true, newpassCache);
            newpassCache.clear();
            if (ok) {
                QMetaObject::invokeMethod(this, "warningMessage", Qt::QueuedConnection);
            } else {
                QMetaObject::invokeMethod(this, "errorEncryptingWallet", Qt::QueuedConnection);
            }
        }
    }
}

void AskPassphraseDialog::onError(QString error, int type)
{
    Q_UNUSED(type);
    newpassCache.clear();
    LogPrintf("Error encrypting wallet, %s\n", error.toStdString());
    QMetaObject::invokeMethod(this, "errorEncryptingWallet", Qt::QueuedConnection);
}
