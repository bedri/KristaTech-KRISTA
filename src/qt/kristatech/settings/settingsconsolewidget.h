// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SETTINGSCONSOLEWIDGET_H
#define SETTINGSCONSOLEWIDGET_H

#include <QWidget>
#include "qt/kristatech/pwidget.h"
#include "guiutil.h"
#include "net.h"
#include <QCompleter>
#include <QVector>

class ClientModel;
class QLabel;
class QLineEdit;
class QPushButton;
class RPCTimerInterface;

namespace Ui {
class SettingsConsoleWidget;
}

QT_BEGIN_NAMESPACE
class QMenu;
class QItemSelection;
class QThread;
QT_END_NAMESPACE

class SettingsConsoleWidget : public PWidget
{
    Q_OBJECT

public:
    explicit SettingsConsoleWidget(KRISTATECHGUI* _window, QWidget *parent = nullptr);
    ~SettingsConsoleWidget();

    void loadClientModel() override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void shutdownExecutor();

    enum MessageClass {
        MC_ERROR,
        MC_DEBUG,
        CMD_REQUEST,
        CMD_REPLY,
        CMD_ERROR
    };

public Q_SLOTS:
    void clear(bool clearHistory = true);
    void response(int category, const QString &message);
    void messageInternal(int category, const QString &message, bool html = false);
    /** Go forward or back in history */
    void browseHistory(int offset);
    /** Scroll console view to end */
    void scrollToEnd();
    void onCommandsClicked();
    void onBtnCommandsMenuClicked();
    void copyLastOutput();
    void findInOutput();
    void runQuickCommand(const QString& cmd);
    void applyCommandTemplate(const QString& tmpl);
    void updateLiveStatusBadge();

protected:
    virtual bool eventFilter(QObject* obj, QEvent* event) override;

protected Q_SLOTS:
    void changeTheme(bool isLightTheme, QString &theme) override;

Q_SIGNALS:
    // For RPC command executor
    void stopExecutor();
    void cmdCommandRequest(const QString& command);

private:
    struct ConsoleLogEntry {
        QString timeString;
        int category;
        QString message;
        bool html;
    };

    Ui::SettingsConsoleWidget *ui;

    QStringList history;
    int historyPtr{0};
    RPCTimerInterface *rpcTimerInterface{nullptr};
    QCompleter *autoCompleter{nullptr};
    QThread *executorThread{nullptr};

    QLabel *labelLiveStatus{nullptr};
    QLineEdit *lineEditFind{nullptr};
    QPushButton *btnFindNext{nullptr};
    QPushButton *btnRunCmd{nullptr};
    QPushButton *btnCopyLast{nullptr};
    QString lastReplyText;
    QVector<ConsoleLogEntry> consoleEntries;
    bool isCommandRunning{false};
    bool findMissHighlight{false};
    bool lastCommandSensitive{false};
    bool sessionHadSensitiveOutput{false};

    void startExecutor();
    void setCommandRunningState(bool running);
    void applyConsoleTheme(bool isLight);
    void appendConsoleEntry(const ConsoleLogEntry& entry, bool isLight);
    void rebuildConsoleForTheme(bool isLight);

private Q_SLOTS:
    void on_lineEdit_returnPressed();
};

#endif // SETTINGSCONSOLEWIDGET_H
