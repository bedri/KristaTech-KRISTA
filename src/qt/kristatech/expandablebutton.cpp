// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/expandablebutton.h"
#include "qt/kristatech/forms/ui_expandablebutton.h"
#include "qt/kristatech/qtutils.h"
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QStyle>
#include <QToolTip>
#include <iostream>

ExpandableButton::ExpandableButton(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ExpandableButton),
    isAnimating(false)
{
    ui->setupUi(this);

    ui->pushButton->setCheckable(true);
    ui->pushButton->setCursor(Qt::PointingHandCursor);
    setCursor(Qt::PointingHandCursor);
    this->layout()->setSizeConstraint(QLayout::SetFixedSize);

    connect(ui->pushButton, &QPushButton::clicked, this, &ExpandableButton::innerMousePressEvent);
}

void ExpandableButton::setButtonClassStyle(const char *name, const QVariant &value, bool forceUpdate)
{
    if (ui->pushButton->property(name) == value) {
        return;
    }
    ui->pushButton->setProperty(name, value);
    if (forceUpdate) {
        updateStyle(ui->pushButton);
    }
}

void ExpandableButton::setIcon(QString path)
{
    ui->pushButton->setIcon(QIcon(path));
}

QString ExpandableButton::effectiveToolTip() const
{
    if (suppressToolTip) return QString();
    if (!customToolTip.isEmpty()) return customToolTip;
    if (!toolTip().isEmpty()) return toolTip();
    return this->text;
}

void ExpandableButton::setButtonToolTip(const QString& _tip)
{
    customToolTip = _tip;
    QWidget::setToolTip(QString());
    ui->pushButton->setToolTip(QString());
    if (!suppressToolTip && (underMouse() || ui->pushButton->underMouse())) {
        const QString tip = effectiveToolTip();
        if (!tip.isEmpty()) {
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
        } else {
            QToolTip::hideText();
        }
    }
}

void ExpandableButton::setButtonText(const QString& _text)
{
    this->text = _text;
    if (this->isExpanded) {
        ui->pushButton->setText(_text);
    }
    if (!suppressToolTip && (underMouse() || ui->pushButton->underMouse())) {
        const QString tip = effectiveToolTip();
        if (!tip.isEmpty()) {
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
        }
    }
}

void ExpandableButton::setNoIconText(const QString& _text)
{
    notExpandedText = _text;
    if (!this->isExpanded)
        ui->pushButton->setText(_text);
}

void ExpandableButton::setText2(QString text2)
{
    this->text = text2;
    if (this->isExpanded) {
        ui->pushButton->setText(text2);
    }
    if (!suppressToolTip && (underMouse() || ui->pushButton->underMouse())) {
        const QString tip = effectiveToolTip();
        if (!tip.isEmpty()) {
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
        }
    }
}

ExpandableButton::~ExpandableButton()
{
    delete ui;
}

bool ExpandableButton::isChecked()
{
    return ui->pushButton->isChecked();
}

void ExpandableButton::setChecked(bool check)
{
    ui->pushButton->setChecked(check);
}

void ExpandableButton::setCheckable(bool checkable)
{
    ui->pushButton->setCheckable(checkable);
}

void ExpandableButton::setSmall()
{
    ui->pushButton->setText(notExpandedText);
    this->setMaximumWidth(36);
    this->isExpanded = false;
    update();
}

void ExpandableButton::setExpanded()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    const int textPx = ui->pushButton->fontMetrics().horizontalAdvance(text);
#else
    const int textPx = ui->pushButton->fontMetrics().width(text);
#endif
    const int targetWidth = std::max(100, std::min(240, textPx + 48));
    this->setMaximumWidth(targetWidth);
    ui->pushButton->setText(text);
    this->isExpanded = true;
}

void ExpandableButton::enterEvent(QEvent *)
{
    const QString tip = effectiveToolTip();
    if (!tip.isEmpty()) {
        QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
    }
    Q_EMIT Mouse_Hover();
    update();
}

void ExpandableButton::leaveEvent(QEvent *)
{
    QToolTip::hideText();
    Q_EMIT Mouse_HoverLeave();
    update();
}

bool ExpandableButton::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        const QString tip = effectiveToolTip();
        if (!tip.isEmpty()) {
            if (!QToolTip::isVisible() || QToolTip::text() != tip) {
                QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
            }
        }
        e->accept();
        return true;
    }
    return QWidget::event(e);
}

void ExpandableButton::innerMousePressEvent()
{
    Q_EMIT Mouse_Pressed();
}

void ExpandableButton::on_pushButton_clicked(bool checked)
{
    // TODO: Add callback event
}
