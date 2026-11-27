// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/snackbar.h"
#include "qt/kristatech/forms/ui_snackbar.h"
#include "qt/kristatech/qtutils.h"
#include <QPropertyAnimation>
#include <QTimer>


SnackBar::SnackBar(KRISTATECHGUI* _window, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SnackBar),
    window(_window),
    timeout(MIN_TIMEOUT)
{
    ui->setupUi(this);

    setWindowFlags(Qt::CustomizeWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setModal(false);

    ui->snackContainer->setProperty("cssClass", "container-snackbar");
    ui->label->setProperty("cssClass", "text-snackbar");
    ui->pushButton->setProperty("cssClass", "ic-close");
    ui->pushButton->setFocusPolicy(Qt::NoFocus);

    hideAnimTimer = new QTimer(this);
    hideAnimTimer->setSingleShot(true);
    connect(hideAnimTimer, &QTimer::timeout, this, &SnackBar::hideAnim);

    hideTimer = new QTimer(this);
    hideTimer->setSingleShot(true);
    hideTimer->setInterval(310);
    connect(hideTimer, &QTimer::timeout, this, &QDialog::hide);

    connect(ui->pushButton, &QPushButton::clicked, this, &SnackBar::dismissNow);
    if (window)
        connect(window, &KRISTATECHGUI::windowResizeEvent, this, &SnackBar::windowResizeEvent);
    else {
        ui->horizontalLayout->setContentsMargins(0,0,0,0);
        ui->label->setStyleSheet("font-size: 15px; color:white;");
    }
}

void SnackBar::windowResizeEvent(QResizeEvent* event) {
    Q_UNUSED(event);
    QWidget* host = window ? static_cast<QWidget*>(window) : qobject_cast<QWidget*>(parent());
    if (!host) return;
    this->resize(host->width(), this->height());
    this->move(QPoint(0, host->height() - this->height()));
}

void SnackBar::restartTimers()
{
    if (hideTimer) hideTimer->stop();
    if (hideAnimTimer) {
        hideAnimTimer->stop();
        hideAnimTimer->start(timeout);
    }
}

void SnackBar::dismissNow()
{
    if (hideAnimTimer) hideAnimTimer->stop();
    if (hideTimer) hideTimer->stop();
    for (QPropertyAnimation* anim : findChildren<QPropertyAnimation*>()) {
        anim->stop();
        anim->deleteLater();
    }
    hide();
}

void SnackBar::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    restartTimers();
}

void SnackBar::hideAnim()
{
    if (hideAnimTimer) hideAnimTimer->stop();
    if (window) {
        closeDialog(this, window);
        if (hideTimer) hideTimer->start(310);
    } else {
        hide();
    }
}

void SnackBar::setText(const QString& text)
{
    ui->label->setText(text);
    setTimeoutForText(text);
    if (isVisible()) {
        restartTimers();
    }
}

void SnackBar::setTimeoutForText(const QString& text)
{
    timeout = std::max(MIN_TIMEOUT, std::min(MAX_TIMEOUT, GetTimeout(text)));
}

int SnackBar::GetTimeout(const QString& message)
{
    // 50 milliseconds per char
    return (50 * message.length());
}

SnackBar::~SnackBar()
{
    delete ui;
}

const int SnackBar::MIN_TIMEOUT;
const int SnackBar::MAX_TIMEOUT;
