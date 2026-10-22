// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/furabstractlistitemdelegate.h"

FurAbstractListItemDelegate::FurAbstractListItemDelegate(int _rowHeight, FurListRow<> *_row, QObject *parent) :
    QAbstractItemDelegate(parent), rowHeight(_rowHeight), row(_row){}

void FurAbstractListItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                  const QModelIndex &index ) const
{
    painter->save();

    // Status
    bool isStateSelected = option.state & QStyle::State_Selected;
    bool isStateHovered = option.state & QStyle::State_MouseOver;

    QRect selectedRect = option.rect;
    selectedRect.setLeft(0);
    painter->fillRect(selectedRect, this->row->rectColor(isStateHovered, isStateSelected));

    painter->translate(option.rect.topLeft());
    QWidget *row = this->row->createHolder(index.row());
    QWidget* parentWidget = qobject_cast<QWidget*>(parent());
    if (parentWidget) {
        const QString& parentStyle = parentWidget->styleSheet();
        if (row->styleSheet() != parentStyle) {
            row->setStyleSheet(parentStyle);
        }
    }
    this->row->init(row, index, isStateHovered, isStateSelected);
    if (!row->testAttribute(Qt::WA_DontShowOnScreen)) {
        row->setAttribute(Qt::WA_DontShowOnScreen, true);
    }
    if (row->size() != option.rect.size()) {
        row->resize(option.rect.width(), option.rect.height());
    }
    row->render(painter, QPoint(), QRegion(), QWidget::DrawChildren );

    painter->restore();
}

FurListRow<>* FurAbstractListItemDelegate::getRowFactory(){
    return this->row;
}

QSize FurAbstractListItemDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const
{
    return QSize(rowHeight, rowHeight);
}
