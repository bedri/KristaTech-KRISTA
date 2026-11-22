// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MNINFODIALOG_H
#define MNINFODIALOG_H

#include "qt/kristatech/focuseddialog.h"
#include "qt/kristatech/mnmodel.h"
#include "qt/kristatech/snackbar.h"

class QLabel;
class QPushButton;
class WalletModel;

namespace Ui {
class MnInfoDialog;
}

class MnInfoDialog : public FocusedDialog
{
    Q_OBJECT

public:
    explicit MnInfoDialog(QWidget *parent = nullptr);
    ~MnInfoDialog();

    bool exportMN = false;

    void setData(QString pubKey, QString name, QString address, QString txId, QString outputIndex, QString status, bool hasLocalOperatorKey = false);
    void setDip3Meta(const MNModel::Dip3Meta& meta);

public Q_SLOTS:
    void reject() override;

private:
    Ui::MnInfoDialog *ui;
    SnackBar *snackBar = nullptr;
    int nDisplayUnit = 0;
    WalletModel *model = nullptr;
    QString txId;
    QString pubKey;
    QString collateralOutpointStr;
    MNModel::Dip3Meta dip3Meta;

    QWidget* rowOperatorBls = nullptr;
    QLabel* valOperatorBls = nullptr;
    QLabel* divOperatorBls = nullptr;
    QWidget* rowPayout = nullptr;
    QLabel* valPayout = nullptr;
    QLabel* divPayout = nullptr;
    QWidget* rowPose = nullptr;
    QLabel* valPose = nullptr;
    QLabel* divPose = nullptr;
    QWidget* rowHeights = nullptr;
    QLabel* valHeights = nullptr;
    QLabel* divHeights = nullptr;
    QWidget* rowCtrl = nullptr;

    void updateDividers();
    void refreshScrollSize();
    void copyInform(const QString& copyStr, const QString& message);
};

#endif // MNINFODIALOG_H
