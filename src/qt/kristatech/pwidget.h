// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PWIDGET_H
#define PWIDGET_H

#include <QObject>
#include <QWidget>
#include <QString>
#include "qt/kristatech/prunnable.h"
#include "walletmodel.h"

class KRISTATECHGUI;
class ClientModel;
class WalletModel;
class WorkerTask;

namespace Ui {
class PWidget;
}

class Translator
{
public:
    virtual QString translate(const char *msg) = 0;
};

class PWidget : public QWidget, public Runnable, public Translator
{
    Q_OBJECT
public:
    explicit PWidget(KRISTATECHGUI* _window = nullptr, QWidget *parent = nullptr);
    explicit PWidget(PWidget *parent = nullptr);

    void setClientModel(ClientModel* model);
    void setWalletModel(WalletModel* model);
    void clearModels();

    KRISTATECHGUI* getWindow() { return this->window; }

    void run(int type) override;
    void onError(QString error, int type) override;

    void inform(const QString& message);
    void emitMessage(const QString& title, const QString& message, unsigned int style, bool* ret = nullptr);

    QString translate(const char *msg) override { return tr(msg); }

Q_SIGNALS:
    void message(const QString& title, const QString& body, unsigned int style, bool* ret = nullptr);
    void showHide(bool show);
    bool execDialog(QDialog *dialog, int xDiv = 3, int yDiv = 5);

protected Q_SLOTS:
    virtual void changeTheme(bool isLightTheme, QString &theme);
    void onChangeTheme(bool isLightTheme, QString &theme);

protected:
    KRISTATECHGUI* window = nullptr;
    ClientModel* clientModel = nullptr;
    WalletModel* walletModel = nullptr;

    virtual void loadClientModel();
    virtual void loadWalletModel();
    bool event(QEvent* e) override;
    bool isFlushingOnShow() const { return m_flushingOnShow; }

    void showHideOp(bool show);
    bool execute(int type, std::unique_ptr<WalletModel::UnlockContext> pctx = nullptr);
    void warn(const QString& title, const QString& message);
    bool ask(const QString& title, const QString& message);
    void showDialog(QDialog *dialog, int xDiv = 3, int yDiv = 5);

private:
    QSharedPointer<WorkerTask> task;
    bool m_themeDirty = false;
    bool m_appliedLightTheme = false;
    bool m_pendingLightTheme = false;
    bool m_flushingOnShow = false;
    QString m_pendingThemeCss;

    void init();
    void applyThemeNow(bool isLightTheme, QString& theme);
    void flushPendingTheme();
private Q_SLOTS:
    void errorString(QString, int);

};

#endif // PWIDGET_H
