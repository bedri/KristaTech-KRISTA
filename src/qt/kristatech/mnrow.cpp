// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/mnrow.h"
#include "qt/kristatech/forms/ui_mnrow.h"
#include "qt/kristatech/qtutils.h"

#include <QFont>

static QString deleteButtonStyleSheet(bool light)
{
    return QString(
        "QPushButton#pushButtonDelete { "
        "background-color: #C94A3E; color: #FFFFFF; border: 1px solid %1; "
        "border-radius: 6px; padding: 3px 8px; font-size: 12px; font-weight: 600; }"
        "QPushButton#pushButtonDelete:hover { "
        "background-color: #A6362B; color: #FFFFFF; border-color: %2; }"
        "QPushButton#pushButtonDelete:pressed { "
        "background-color: #882920; color: #FFFFFF; border-color: %3; }"
    ).arg(light ? QStringLiteral("#9E3329") : QStringLiteral("#F08074"),
          light ? QStringLiteral("#7A231B") : QStringLiteral("#F5897C"),
          light ? QStringLiteral("#631B14") : QStringLiteral("#C94A3E"));
}

static QString secondaryActionStyleSheet(bool light)
{
    return QString(
        "QPushButton { "
        "background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 6px; padding: 3px 8px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { "
        "background-color: #5576B2; color: #FFFFFF; border-color: #4A679E; }"
        "QPushButton:pressed { "
        "background-color: #3E5B94; color: #FFFFFF; border-color: #3E5B94; }"
    ).arg(light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
          light ? QStringLiteral("#3E5B94") : QStringLiteral("#DCE7F7"),
          light ? QStringLiteral("#7695CD") : QStringLiteral("#4A679E"));
}

MNRow::MNRow(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MNRow)
{
    ui->setupUi(this);

    QFont monoFont(QStringLiteral("Monospace"));
    monoFont.setStyleHint(QFont::TypeWriter);
    monoFont.setPointSize(9);
    ui->labelAddress->setFont(monoFont);
    ui->labelTxId->setFont(monoFont);

    ui->frame_2->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    ui->statusContainer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));
    ui->actionsContainer->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    ui->labelDate->setAlignment(Qt::AlignCenter);
    ui->labelDate->setMaximumHeight(24);
}

void MNRow::updateView(QString address, QString label, QString status, bool wasCollateralAccepted, QString txId, bool isWalletOwned, bool isInConf, bool isOnChainDip3)
{
    ui->labelName->setText(label);
    ui->labelAddress->setText(address);
    ui->pushButtonStart->setVisible(isWalletOwned && isInConf && !(isOnChainDip3 && status == "ENABLED"));
    ui->pushButtonDelete->setVisible(isWalletOwned && isInConf);
    if (!wasCollateralAccepted && status != "ENABLED" && status != "PRE_ENABLED") {
        status = tr("Collateral tx not found");
    }
    ui->labelDate->setText(status);

    const bool light = isLightTheme();
    ui->labelName->setStyleSheet(QString(
        "QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }"
    ).arg(light ? QStringLiteral("#0F172A") : QStringLiteral("#F1F5F9")));

    const QString metaStyle = QString(
        "QLabel { color: %1; font-size: 12px; background: transparent; border: none; }"
    ).arg(light ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1"));
    ui->labelAddress->setStyleSheet(metaStyle);
    ui->labelTxId->setStyleSheet(metaStyle);
    ui->lblDivisory->setStyleSheet(QString("background-color: %1; border: none;")
        .arg(light ? QStringLiteral("#E2E8F0") : QStringLiteral("#1E2D4A")));

    const QString secCss = secondaryActionStyleSheet(light);
    ui->pushButtonInfo->setStyleSheet(secCss);
    ui->pushButtonStart->setStyleSheet(secCss);
    ui->pushButtonDelete->setStyleSheet(deleteButtonStyleSheet(light));

    QString pillColor = light ? "#475569" : "#CBD5E1";
    QString pillBg = "rgba(100, 116, 139, 0.14)";
    QString pillBorder = "rgba(100, 116, 139, 0.35)";
    if (status == "ENABLED") {
        pillColor = light ? "#3E5B94" : "#A8C1EE";
        pillBg = light ? "#EEF3FA" : "#1B2B4A";
        pillBorder = "#7695CD";
    } else if (status == "PRE_ENABLED") {
        pillColor = light ? "#3E5B94" : "#93B4F0";
        pillBg = light ? "rgba(118, 149, 205, 0.16)" : "rgba(118, 149, 205, 0.22)";
        pillBorder = light ? "rgba(118, 149, 205, 0.55)" : "rgba(147, 180, 240, 0.50)";
    } else if (status == "EXPIRED" || status == "NEW_START_REQUIRED" || status == "WATCHDOG_EXPIRED") {
        pillColor = light ? "#B45309" : "#FBBF24";
        pillBg = light ? "rgba(245, 158, 11, 0.14)" : "rgba(245, 158, 11, 0.16)";
        pillBorder = "rgba(245, 158, 11, 0.45)";
    } else {
        pillColor = light ? "#C94A3E" : "#FCA5A5";
        pillBg = light ? "#FEF2F2" : "#2E1619";
        pillBorder = light ? "#F5B5AE" : "#7F2D28";
    }
    ui->labelDate->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; "
        "border-radius: 11px; padding: 2px 10px; font-size: 11px; font-weight: 600; }"
    ).arg(pillColor, pillBg, pillBorder));

    if (txId.length() > 16) {
        ui->labelTxId->setText(txId.left(8) + "..." + txId.right(8));
    } else {
        ui->labelTxId->setText(txId);
    }
}

MNRow::~MNRow()
{
    delete ui;
}
