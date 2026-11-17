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

void ExpandableButton::setButtonText(const QString& _text)
{
    this->text = _text;
    const QString tipText = suppressToolTip ? QString() : _text;
    setToolTip(tipText);
    ui->pushButton->setToolTip(tipText);
    if (this->isExpanded) {
        ui->pushButton->setText(_text);
    }
    if (!suppressToolTip && (underMouse() || ui->pushButton->underMouse())) {
        QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), _text, this, rect(), 600000);
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
    const QString tipText = suppressToolTip ? QString() : text2;
    setToolTip(tipText);
    ui->pushButton->setToolTip(tipText);
    if (this->isExpanded) {
        ui->pushButton->setText(text2);
    }
    if (!suppressToolTip && (underMouse() || ui->pushButton->underMouse())) {
        QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), text2, this, rect(), 600000);
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
    if (!suppressToolTip) {
        const QString tip = !toolTip().isEmpty()
            ? toolTip()
            : (!ui->pushButton->toolTip().isEmpty() ? ui->pushButton->toolTip() : this->text);
        if (!tip.isEmpty()) {
            QToolTip::showText(mapToGlobal(QPoint(width() / 2, height())), tip, this, rect(), 600000);
        }
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

void ExpandableButton::innerMousePressEvent()
{
    Q_EMIT Mouse_Pressed();
}

void ExpandableButton::on_pushButton_clicked(bool checked)
{
    // TODO: Add callback event
}
