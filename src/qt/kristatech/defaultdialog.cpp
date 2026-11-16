// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/forms/ui_defaultdialog.h"
#include "qt/kristatech/qtutils.h"
#include "guiutil.h"

#include <QScrollBar>
#include <QTextDocument>
#include <algorithm>
#include <cmath>

DefaultDialog::DefaultDialog(QWidget *parent) :
    FocusedDialog(parent),
    ui(new Ui::DefaultDialog)
{
    ui->setupUi(this);

    // Stylesheet
    this->setStyleSheet(parent && !parent->styleSheet().isEmpty() ? parent->styleSheet() : GUIUtil::loadStyleSheet());

    // Container
    ui->frame->setProperty("cssClass", "container-dialog");

    // Header divider & scroll container styling
    const bool light = isLightTheme();
    ui->frameHeaderDivider->setStyleSheet(light
        ? "background-color: #DDE7F5; border: none;"
        : "background-color: #243556; border: none;");
    ui->scrollAreaMessage->setStyleSheet(
        "QScrollArea { background-color: transparent; border: none; }"
        "QWidget#scrollAreaMessageContents { background-color: transparent; border: none; }");
    if (ui->scrollAreaMessage->viewport()) {
        ui->scrollAreaMessage->viewport()->setStyleSheet("background-color: transparent; border: none;");
    }

    // Text
    ui->labelTitle->setProperty("cssClass", "text-title-dialog");
    ui->labelMessage->setProperty("cssClass", "text-main-grey");
    ui->labelMessage->setOpenExternalLinks(true);

    // Buttons
    ui->btnEsc->setText("");
    ui->btnEsc->setProperty("cssClass", "ic-close");

    ui->btnCancel->setProperty("cssClass", "btn-dialog-cancel");
    ui->btnSave->setProperty("cssClass", "btn-primary");

    connect(ui->btnEsc, &QPushButton::clicked, this, &DefaultDialog::close);
    connect(ui->btnCancel, &QPushButton::clicked, this, &DefaultDialog::close);
    connect(ui->btnSave, &QPushButton::clicked, this, &DefaultDialog::accept);
}

void DefaultDialog::setText(const QString& title, const QString& message, const QString& okBtnText, const QString& cancelBtnText)
{
    if (!okBtnText.trimmed().isEmpty()) {
        ui->btnSave->setText(okBtnText);
    }
    if (!cancelBtnText.trimmed().isEmpty()) {
        ui->btnCancel->setVisible(true);
        ui->btnCancel->setText(cancelBtnText);
    } else {
        ui->btnCancel->setVisible(false);
    }
    if (!title.isNull()) ui->labelTitle->setText(title);
    if (!message.isNull()) ui->labelMessage->setText(message);

    updateStyle(ui->btnCancel);
    updateStyle(ui->btnSave);
    updateDialogMetrics();
}

void DefaultDialog::updateDialogMetrics()
{
    const QString rawMsg = ui->labelMessage->text();
    const bool isRich = Qt::mightBeRichText(rawMsg);

    QTextDocument probeDoc;
    if (isRich) {
        probeDoc.setHtml(rawMsg);
    } else {
        probeDoc.setPlainText(rawMsg);
    }
    const QString plainText = probeDoc.toPlainText();

    const bool isStructured = plainText.length() > 130 ||
                              rawMsg.contains(QString::fromUtf8("•")) ||
                              rawMsg.contains("<li", Qt::CaseInsensitive) ||
                              rawMsg.contains("<br", Qt::CaseInsensitive) ||
                              plainText.count('\n') >= 3;

    const bool light = isLightTheme();
    const int msgFontPx = isStructured ? 14 : 15;
    ui->labelMessage->setAlignment(isStructured ? (Qt::AlignLeft | Qt::AlignVCenter) : Qt::AlignCenter);
    ui->labelMessage->setStyleSheet(QString(
        "color: %1; font-size: %2px; background-color: transparent; border: none;"
    ).arg(light ? "#334155" : "#E2E8F0").arg(msgFontPx));

    int targetW = isStructured ? 620 : 500;
    if (parentWidget() && parentWidget()->width() > 360) {
        targetW = std::min(targetW, std::max(380, parentWidget()->width() - 48));
    }

    // Inner message width: account for frame margins (28*2), scroll area margins, and optional scrollbar slack
    const int contentW = std::max(260, targetW - 96);

    this->ensurePolished();
    ui->labelTitle->ensurePolished();
    ui->labelMessage->ensurePolished();

    // Measure title height accurately
    QTextDocument titleDoc;
    QFont titleFont = ui->labelTitle->font();
    titleFont.setFamily("Nunito");
    titleFont.setPixelSize(20);
    titleFont.setBold(true);
    titleDoc.setDefaultFont(titleFont);
    titleDoc.setPlainText(ui->labelTitle->text());
    titleDoc.setTextWidth(std::max(200, targetW - 120));
    const int rawTitleH = static_cast<int>(std::ceil(titleDoc.size().height())) + 10;
    const int titleH = std::max(34, std::min(72, rawTitleH));
    ui->labelTitle->setMinimumHeight(titleH);

    // Measure message height using both QTextDocument and actual QLabel::heightForWidth
    // so bold HTML spans never trigger an unnecessary vertical scrollbar.
    QFont msgFont = ui->labelMessage->font();
    msgFont.setFamily("Nunito");
    msgFont.setPixelSize(msgFontPx);
    probeDoc.setDefaultFont(msgFont);
    probeDoc.setDocumentMargin(4);
    probeDoc.setTextWidth(contentW);
    const int docH = static_cast<int>(std::ceil(probeDoc.size().height()));
    const int labelH = ui->labelMessage->hasHeightForWidth()
        ? ui->labelMessage->heightForWidth(contentW)
        : ui->labelMessage->sizeHint().height();
    const int msgH = std::max(docH, labelH) + 38;

    // Total dialog height: top(20) + titleH + gap(10) + divider(1) + gap(10) + msgH + gap(16) + buttons(46) + bottom(22) + border(12)
    const int chromeH = 20 + titleH + 10 + 1 + 10 + 16 + 46 + 22 + 12;
    int maxH = 680;
    if (parentWidget() && parentWidget()->height() > 320) {
        maxH = std::max(320, parentWidget()->height() - 40);
    }

    const int finalH = std::max(240, std::min(maxH, chromeH + msgH));
    m_preferredSize = QSize(targetW, finalH);
    setMinimumSize(std::min(targetW, 420), std::min(finalH, 240));
    resize(m_preferredSize);
}

QSize DefaultDialog::sizeHint() const
{
    return m_preferredSize;
}

void DefaultDialog::setDanger(bool danger)
{
    setCssProperty(ui->btnSave, danger ? "btn-danger" : "btn-primary", true);
    setCssProperty(ui->frame, danger ? "container-dialog-danger" : "container-dialog", true);
}

void DefaultDialog::accept()
{
    this->isOk = true;
    QDialog::accept();
}

DefaultDialog::~DefaultDialog()
{
    delete ui;
}
