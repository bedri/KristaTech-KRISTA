// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/pwidget.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/loadingdialog.h"
#include "clientmodel.h"
#include "guiutil.h"
#include <QEvent>
#include <QRunnable>
#include <QThreadPool>

PWidget::PWidget(KRISTATECHGUI* _window, QWidget *parent) : QWidget((parent) ? parent : _window), window(_window) { init(); }
PWidget::PWidget(PWidget* parent) : QWidget(parent), window(parent->getWindow()) { init(); }

void PWidget::init()
{
    m_appliedLightTheme = isLightTheme();
    if (window)
        connect(window, &KRISTATECHGUI::themeChanged, this, &PWidget::onChangeTheme);
}

void PWidget::setClientModel(ClientModel* model)
{
    if (this->clientModel && this->clientModel != model) {
        disconnect(this->clientModel, nullptr, this, nullptr);
    }
    this->clientModel = model;
    if (model) {
        loadClientModel();
    }
}

void PWidget::setWalletModel(WalletModel* model)
{
    if (this->walletModel && this->walletModel != model) {
        disconnect(this->walletModel, nullptr, this, nullptr);
    }
    this->walletModel = model;
    if (model) {
        loadWalletModel();
    }
}

void PWidget::clearModels()
{
    if (this->clientModel) {
        disconnect(this->clientModel, nullptr, this, nullptr);
        this->clientModel = nullptr;
    }
    if (this->walletModel) {
        disconnect(this->walletModel, nullptr, this, nullptr);
        this->walletModel = nullptr;
    }
}

void PWidget::onChangeTheme(bool isLightTheme, QString& theme)
{
    if (!isVisible()) {
        if (isLightTheme == m_appliedLightTheme) {
            m_themeDirty = false;
            m_pendingThemeCss.clear();
            return;
        }
        m_themeDirty = true;
        m_pendingLightTheme = isLightTheme;
        m_pendingThemeCss = (isWindow() || !parentWidget()) ? theme : QString();
        return;
    }
    applyThemeNow(isLightTheme, theme);
}

void PWidget::applyThemeNow(bool isLightTheme, QString& theme)
{
    m_themeDirty = false;
    m_appliedLightTheme = isLightTheme;
    if (isWindow() || !parentWidget()) {
        this->setStyleSheet(theme);
    }
    changeTheme(isLightTheme, theme);
}

void PWidget::flushPendingTheme()
{
    if (!m_themeDirty) return;
    m_themeDirty = false;
    QString css = m_pendingThemeCss;
    m_pendingThemeCss.clear();
    if (css.isEmpty() && (isWindow() || !parentWidget())) {
        css = GUIUtil::loadStyleSheet();
    }
    m_flushingOnShow = true;
    applyThemeNow(m_pendingLightTheme, css);
    m_flushingOnShow = false;
}

bool PWidget::event(QEvent* e)
{
    if (e && e->type() == QEvent::Show && m_themeDirty) {
        flushPendingTheme();
    }
    return QWidget::event(e);
}

void PWidget::showHideOp(bool show)
{
    Q_EMIT showHide(show);
}

void PWidget::inform(const QString& message)
{
    emitMessage("", message, CClientUIInterface::MSG_INFORMATION_SNACK);
}

void PWidget::warn(const QString& title, const QString& message)
{
    emitMessage(title, message, CClientUIInterface::MSG_ERROR);
}

bool PWidget::ask(const QString& title, const QString& message)
{
    bool ret = false;
    emitMessage(title, message, CClientUIInterface::MSG_INFORMATION | CClientUIInterface::BTN_MASK | CClientUIInterface::MODAL, &ret);
    return ret;
}

void PWidget::showDialog(QDialog *dlg, int xDiv, int yDiv)
{
    Q_EMIT execDialog(dlg, xDiv, yDiv);
}

void PWidget::emitMessage(const QString& title, const QString& body, unsigned int style, bool* ret)
{
    Q_EMIT message(title, body, style, ret);
}

class WorkerTask : public QRunnable
{
public:
    WorkerTask(QPointer<Worker> worker) {
        this->worker = worker;
    }

    ~WorkerTask() {
        if (!worker.isNull()) worker.clear();
    }

    void run() override {
        if (!worker.isNull()) {
            Worker* _worker = worker.data();
            _worker->process();
            _worker->clean();
        }
    }

    QPointer<Worker> worker;
};

bool PWidget::execute(int type, std::unique_ptr<WalletModel::UnlockContext> pctx)
{
    if (task.isNull()) {
        Worker* worker = (!pctx) ? new Worker(this, type) : new WalletWorker(this, type, std::move(pctx));
        connect(worker, &Worker::error, this, &PWidget::errorString);

        WorkerTask* workerTask = new WorkerTask(QPointer<Worker>(worker));
        workerTask->setAutoDelete(false);
        task = QSharedPointer<WorkerTask>(workerTask);
    } else if (pctx){
        if (task->worker.isNull() || !task->worker.data()) // Must never happen
            throw std::runtime_error("Worker task null");

        // Update context
        if (dynamic_cast<WalletWorker*>(task->worker.data()) != nullptr) {
            WalletWorker* _worker = static_cast<WalletWorker*>(task->worker.data());
            _worker->setContext(std::move(pctx));
        }
    }
    QThreadPool::globalInstance()->start(task.data());
    return true;
}

void PWidget::errorString(QString error, int type)
{
    onError(error, type);
}


////////////////////////////////////////////////////////////////
//////////////////Override methods//////////////////////////////
////////////////////////////////////////////////////////////////


void PWidget::loadClientModel() { /* override*/ }
void PWidget::loadWalletModel() { /* override*/ }
void PWidget::changeTheme(bool isLightTheme, QString& theme) { /* override*/ }
void PWidget::run(int type) { /* override*/ }
void PWidget::onError(QString error, int type) { /* override*/ }
