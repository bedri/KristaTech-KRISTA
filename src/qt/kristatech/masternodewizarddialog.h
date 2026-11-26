// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEWIZARDDIALOG_H
#define MASTERNODEWIZARDDIALOG_H

#include "walletmodel.h"
#include "qt/kristatech/focuseddialog.h"
#include "qt/kristatech/snackbar.h"
#include "qt/kristatech/pwidget.h"
#include "wallet/walletdb.h"

class QLabel;
class QLineEdit;
class WalletModel;

namespace Ui {
class MasterNodeWizardDialog;
class QPushButton;
}

class MasterNodeWizardDialog : public FocusedDialog, public PWidget::Translator
{
    Q_OBJECT

public:
    explicit MasterNodeWizardDialog(WalletModel *walletMode, QWidget *parent = nullptr);
    ~MasterNodeWizardDialog();
    void showEvent(QShowEvent *event) override;
    QString translate(const char *msg) override { return tr(msg); }

    QString returnStr = "";
    QString createdActiveMnLine = "";
    QString createdProTxHash = "";
    bool isOk = false;
    bool consolidationPending = false;
    bool hasRecord = false;
    CMasternodeWalletRecord mnRecord;

private Q_SLOTS:
    void accept() override;
    void onBackClicked();
    void updateNameValidation();
    void updateAddressValidation();
private:
    Ui::MasterNodeWizardDialog *ui;
    QPushButton* icConfirm1;
    QPushButton* icConfirm3;
    QPushButton* icConfirm4;
    QLabel* labelNameStatus = nullptr;
    QLabel* labelAddressStatus = nullptr;
    QLineEdit* lineEditPrivKey = nullptr;
    SnackBar *snackBar = nullptr;
    int pos = 0;

    WalletModel *walletModel = nullptr;
    bool createMN();
    void inform(QString text);
    void initBtn(std::initializer_list<QPushButton*> args);
    void updateStepButtons();
};

#endif // MASTERNODEWIZARDDIALOG_H
