// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/receivedialog.h"
#include "qt/kristatech/forms/ui_receivedialog.h"
#include "qt/kristatech/qtutils.h"
#include "walletmodel.h"
#include <QFile>

ReceiveDialog::ReceiveDialog(QWidget *parent) :
    FocusedDialog(parent),
    ui(new Ui::ReceiveDialog)
{
    ui->setupUi(this);

    // Stylesheet
    this->setStyleSheet(parent->styleSheet());

    ui->frameContainer->setProperty("cssClass", "container-dialog");
    ui->frameContainer->setContentsMargins(10,10,10,10);

    // Title
    ui->labelTitle->setProperty("cssClass", "text-title-dialog");

    // Address
    ui->labelAddress->setProperty("cssClass", "label-address-box");

    // QR image
    ui->labelQrImg->setAlignment(Qt::AlignCenter);
    ui->labelQrImg->setStyleSheet("QLabel { background-color: #FFFFFF; border: 1.5px solid #7695CD; border-radius: 10px; padding: 6px; }");
    QPixmap pixmap(":/res/img/img-qr-test-big.png");
    ui->labelQrImg->setPixmap(pixmap.scaled(
                                  ui->labelQrImg->width(),
                                  ui->labelQrImg->height(),
                                  Qt::KeepAspectRatio)
                              );

    // Buttons
    ui->btnEsc->setText("");
    ui->btnEsc->setProperty("cssClass", "ic-close");
    ui->btnCancel->setProperty("cssClass", "btn-dialog-cancel");
    ui->btnSave->setProperty("cssClass", "btn-primary");
    ui->btnCancel->setVisible(false);

    connect(ui->btnEsc, &QPushButton::clicked, this, &ReceiveDialog::close);
    connect(ui->btnSave, &QPushButton::clicked, this, &ReceiveDialog::onCopy);
}

void ReceiveDialog::updateQr(QString address)
{
    if (!info) info = new SendCoinsRecipient();
    info->address = address;
    QString uri = GUIUtil::formatBitcoinURI(*info);
    ui->labelQrImg->setText("");
    ui->labelAddress->setText(address);
    QString error;
    QPixmap pixmap = encodeToQr(uri, error, QColor("#1C2B45"));
    if (!pixmap.isNull()) {
        qrImage = pixmap;
        int qrSize = qMin(ui->labelQrImg->width(), ui->labelQrImg->height()) - 14;
        if (qrSize < 120) qrSize = 180;
        ui->labelQrImg->setPixmap(qrImage.scaled(qrSize, qrSize, Qt::KeepAspectRatio, Qt::FastTransformation));
    } else {
        ui->labelQrImg->setText(!error.isEmpty() ? error : tr("Error encoding address"));
    }
}

void ReceiveDialog::onCopy()
{
    if (info) {
        GUIUtil::setClipboard(info->address);
    }
    accept();
}

ReceiveDialog::~ReceiveDialog()
{
    delete info;
    delete ui;
}
