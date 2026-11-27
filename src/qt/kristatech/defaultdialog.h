// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef DEFAULTDIALOG_H
#define DEFAULTDIALOG_H

#include "qt/kristatech/focuseddialog.h"
#include <QSize>
#include <QString>

class QTextEdit;

namespace Ui {
class DefaultDialog;
}

class DefaultDialog : public FocusedDialog
{
    Q_OBJECT

public:
    explicit DefaultDialog(QWidget *parent = nullptr);
    ~DefaultDialog();

    void setText(const QString& title = "", const QString& message = "", const QString& okBtnText = "", const QString& cancelBtnText = "", bool allowRichText = false);
    void setDanger(bool danger = true);
    QSize sizeHint() const override;

    bool isOk = false;

public Q_SLOTS:
    void accept() override;

private:
    void updateDialogMetrics();

    Ui::DefaultDialog *ui;
    QTextEdit* msgBrowser{nullptr};
    QString m_rawMessage;
    bool m_allowRichText{false};
    QSize m_preferredSize{520, 300};
};

#endif // DEFAULTDIALOG_H
