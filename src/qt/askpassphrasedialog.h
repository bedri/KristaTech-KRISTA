// Copyright (c) 2011-2013 The Bitcoin developers
// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_ASKPASSPHRASEDIALOG_H
#define BITCOIN_QT_ASKPASSPHRASEDIALOG_H

#include <QDialog>
#include "qt/kristatech/prunnable.h"
#include "allocators.h"
#include <QCheckBox>

class QAction;
class QCheckBox;
class QFrame;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class WalletModel;
class KRISTATECHGUI;

namespace Ui
{
class AskPassphraseDialog;
class QCheckBox;
}

/** Multifunctional dialog to ask for passphrases. Used for encryption, unlocking, and changing the passphrase.
 */
class AskPassphraseDialog : public QDialog, public Runnable
{
    Q_OBJECT

public:
    enum class Mode {
        Encrypt,         /**< Two-level passphrase creation, alerts, confirmation, and encrypt */
        UnlockAnonymize, /**< Ask passphrase and unlock only for anonymization */
        Unlock,          /**< Ask passphrase and unlock */
        ChangePass,      /**< Ask old passphrase + new passphrase twice */
        Decrypt          /**< Ask passphrase and decrypt wallet */
    };

    // Context from where / for what the passphrase dialog was called to set the status of the checkbox
    // Partly redundant to Mode above, but offers more flexibility for future enhancements
    enum class Context {
        Unlock_Menu,    /** Unlock wallet from menu     */
        Unlock_Full,    /** Wallet needs to be fully unlocked */
        Encrypt,        /** Encrypt unencrypted wallet */
        ToggleLock,     /** Toggle wallet lock state */
        ChangePass,     /** Change passphrase */
        Send_PIV,       /** Send KRISTA */
        BIP_38,         /** BIP38 menu */
        Multi_Sig,      /** Multi-Signature dialog */
        Sign_Message,   /** Sign/verify message dialog */
        UI_Vote,        /** Governance Tab UI Voting */
    };

    explicit AskPassphraseDialog(Mode mode, QWidget* parent, WalletModel* model, Context context);
    ~AskPassphraseDialog();

    void showEvent(QShowEvent *event) override;
    void accept() override;

private:
    Ui::AskPassphraseDialog* ui;
    Mode mode;
    WalletModel* model;
    Context context;
    bool fCapsLock;
    bool passVisible{false};
    SecureString newpassCache = "";

    QFrame* dialogCard{nullptr};
    QLabel* lblHeaderTitle{nullptr};
    QLabel* lblStepPill{nullptr};
    QStackedWidget* stepStack{nullptr};

    QFrame* alertTopBox{nullptr};
    QLabel* lblAlertTopTitle{nullptr};
    QLabel* lblAlertTopBody{nullptr};
    QLabel* lblSubtitle{nullptr};

    QWidget* strengthWidget{nullptr};
    QProgressBar* strengthBar{nullptr};
    QLabel* lblStrengthLevel{nullptr};
    QLabel* lblStrengthHint{nullptr};

    QFrame* inlineAlertBox{nullptr};
    QLabel* lblInlineAlert{nullptr};
    QCheckBox* chkAckRisk{nullptr};
    QPushButton* btnCancel{nullptr};

    // Step 2 (Level 2 Verification for Mode::Encrypt)
    QFrame* step2AlertBox{nullptr};
    QFrame* step2AuditBox{nullptr};
    QLabel* lblStep2Audit{nullptr};
    QLineEdit* confirmWordEdit{nullptr};
    QPushButton* btnBack{nullptr};
    QPushButton* btnCancelStep2{nullptr};
    QPushButton* btnConfirmEncrypt{nullptr};

    QAction* actWatch1{nullptr};
    QAction* actWatch2{nullptr};
    QAction* actWatch3{nullptr};

    void updateWarningsLabel();
    void updateStrengthMeter();
    void updateActionEnabledState();
    void recenterInParent();
    void showEncryptStep(int stepIndex);
    QString buildDialogStyleSheet(bool isLight) const;
    void run(int type) override;
    void onError(QString error, int type) override;

private Q_SLOTS:
    void togglePassVisibility();
    void textChanged();
    void onConfirmEncryptClicked();
    void warningMessage();
    void errorEncryptingWallet();
    bool openStandardDialog(QString title = "", QString body = "", QString okBtn = "OK", QString cancelBtn = "");

protected:
    bool event(QEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;
};

#endif // BITCOIN_QT_ASKPASSPHRASEDIALOG_H
