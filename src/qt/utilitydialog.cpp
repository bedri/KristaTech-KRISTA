// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilitydialog.h"

#include "ui_helpmessagedialog.h"

#include "clientmodel.h"
#include "guiconstants.h"
#include "intro.h"
#include "guiutil.h"

#include "qt/kristatech/qtutils.cpp"

#include "clientversion.h"
#include "init.h"
#include "util.h"

#include <stdio.h>

#include <QCloseEvent>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QRegExp>
#include <QTextTable>
#include <QTextCursor>
#include <QVBoxLayout>

/** "Help message" or "About" dialog box */
HelpMessageDialog::HelpMessageDialog(QWidget* parent, bool about) : QDialog(parent, Qt::WindowSystemMenuHint | Qt::WindowTitleHint | Qt::WindowCloseButtonHint),
                                                                    ui(new Ui::HelpMessageDialog)
{
    ui->setupUi(this);
    GUIUtil::restoreWindowGeometry("nHelpMessageDialogWindow", this->size(), this);

    const bool light = isLightTheme();

    // Prevent Qt 5.9.9 QPalette::Window (#FFFFFF / #EFEFEF) auto-fill inside nested containers
    this->setAttribute(Qt::WA_StyledBackground, true);
    ui->helpContainer->setAutoFillBackground(false);
    ui->helpContainer->setAttribute(Qt::WA_StyledBackground, true);
    ui->scrollArea->setAutoFillBackground(false);
    ui->scrollArea->setAttribute(Qt::WA_StyledBackground, true);
    if (ui->scrollArea->viewport()) {
        ui->scrollArea->viewport()->setAutoFillBackground(false);
        ui->scrollArea->viewport()->setAttribute(Qt::WA_StyledBackground, true);
    }
    ui->scrollAreaWidgetContents->setAutoFillBackground(false);
    ui->scrollAreaWidgetContents->setAttribute(Qt::WA_StyledBackground, true);
    ui->aboutMessage->setAutoFillBackground(false);
    ui->widget->setAutoFillBackground(false);
    ui->widget->setAttribute(Qt::WA_StyledBackground, true);

    // Render theme-adapted header wordmark (keep K icon colors intact; lighten dark-navy text in Dark theme)
    QImage aboutImg(":/images/about");
    if (!aboutImg.isNull()) {
        aboutImg = aboutImg.convertToFormat(QImage::Format_ARGB32);
        if (!light) {
            const int w = aboutImg.width();
            const int h = aboutImg.height();
            for (int y = 0; y < h; ++y) {
                QRgb* scan = reinterpret_cast<QRgb*>(aboutImg.scanLine(y));
                for (int x = 80; x < w; ++x) {
                    const int a = qAlpha(scan[x]);
                    if (a > 0) {
                        // "KristaTech" in crisp #F1F5F9, "(KRISTA)" in calm Slate-Blue #9BB4E2
                        scan[x] = (x < 358) ? qRgba(241, 245, 249, a) : qRgba(155, 180, 226, a);
                    }
                }
            }
        }
        ui->graphic->setPixmap(QPixmap::fromImage(aboutImg).scaledToHeight(64, Qt::SmoothTransformation));
    }

    const QString bodyText = light ? "#334155" : "#E2E8F0";
    const QString headText = light ? "#0F172A" : "#FFFFFF";
    const QString linkCol  = light ? "#3E5C94" : "#8FB0E8";

    const QString dialogSheet = light ? QStringLiteral(
        "QDialog#HelpMessageDialog, QWidget#helpContainer, QWidget#widget {"
        "  background-color: #F8FAFC; border: none;"
        "}"
        "QLabel#graphic { background-color: transparent; border: none; }"
        "QScrollArea#scrollArea, QTextEdit#helpMessage {"
        "  background-color: #FFFFFF; border: 1px solid #E2E8F0; border-radius: 8px; color: #334155; font-size: 13px;"
        "}"
        "QScrollArea#scrollArea > QWidget > QWidget, QWidget#scrollAreaWidgetContents {"
        "  background-color: transparent; border: none;"
        "}"
        "QLabel#aboutMessage {"
        "  background-color: transparent; border: none; color: #334155; font-size: 13px;"
        "}"
        "QScrollBar:vertical {"
        "  background: #F1F5F9; width: 8px; margin: 4px 2px 4px 0px; border: none; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #CBD5E1; min-height: 28px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #94A3B8; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QPushButton#pushButtonOk {"
        "  background-color: #5576B2; border: 1px solid #48669E; border-radius: 6px;"
        "  color: #FFFFFF; font-size: 13px; font-weight: 500; padding: 6px 18px;"
        "}"
        "QPushButton#pushButtonOk:hover { background-color: #48669E; color: #FFFFFF; }"
        "QPushButton#pushButtonOk:pressed { background-color: #3E598C; color: #FFFFFF; }"
    ) : QStringLiteral(
        "QDialog#HelpMessageDialog, QWidget#helpContainer, QWidget#widget {"
        "  background-color: #0D1321; border: none;"
        "}"
        "QLabel#graphic { background-color: transparent; border: none; }"
        "QScrollArea#scrollArea, QTextEdit#helpMessage {"
        "  background-color: #111A2E; border: 1px solid #1E2D4A; border-radius: 8px; color: #CBD5E1; font-size: 13px;"
        "}"
        "QScrollArea#scrollArea > QWidget > QWidget, QWidget#scrollAreaWidgetContents {"
        "  background-color: transparent; border: none;"
        "}"
        "QLabel#aboutMessage {"
        "  background-color: transparent; border: none; color: #CBD5E1; font-size: 13px;"
        "}"
        "QScrollBar:vertical {"
        "  background: #111A2E; width: 8px; margin: 4px 2px 4px 0px; border: none; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #2D4470; min-height: 28px; border-radius: 4px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: #5576B2; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QPushButton#pushButtonOk {"
        "  background-color: #5576B2; border: 1px solid #6C8BC4; border-radius: 6px;"
        "  color: #FFFFFF; font-size: 13px; font-weight: 500; padding: 6px 18px;"
        "}"
        "QPushButton#pushButtonOk:hover { background-color: #6384C0; color: #FFFFFF; }"
        "QPushButton#pushButtonOk:pressed { background-color: #466399; color: #FFFFFF; }"
    );

    this->setStyleSheet(dialogSheet);

    QPalette textPal = ui->aboutMessage->palette();
    textPal.setColor(QPalette::WindowText, QColor(bodyText));
    textPal.setColor(QPalette::Text, QColor(bodyText));
    ui->aboutMessage->setPalette(textPal);
    ui->aboutMessage->setStyleSheet(QString(
        "background-color: transparent; border: none; color: %1; font-size: 13px;"
    ).arg(bodyText));
    ui->helpMessage->setPalette(textPal);

    QString version = tr("KristaTech") + " " + tr("version") + " " + QString::fromStdString(FormatFullVersion());
/* On x86 add a bit specifier to the version so that users can distinguish between
     * 32 and 64 bit builds. On other architectures, 32/64 bit may be more ambigious.
     */
#if defined(__x86_64__)
    version += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__)
    version += " " + tr("(%1-bit)").arg(32);
#endif

    connect(ui->pushButtonOk, &QPushButton::clicked, this, &HelpMessageDialog::close);
    if (about) {
        setWindowTitle(tr("About KristaTech"));

        /// HTML-format the license message from the core
        QString licenseInfo = QString::fromStdString(LicenseInfo());
        QStringList rawParagraphs = licenseInfo.trimmed().split("\n\n", QString::SkipEmptyParts);
        QStringList htmlParagraphs;
        QRegExp uri("<(.*)>", Qt::CaseSensitive, QRegExp::RegExp2);
        uri.setMinimal(true); // use non-greedy matching

        for (QString para : rawParagraphs) {
            // Unwrap hard 79-column FormatParagraph breaks so QLabel word-wraps cleanly
            para.replace(QRegExp("\\s*\\n\\s*"), " ");
            para = para.trimmed().toHtmlEscaped();
            para.replace(QRegExp("&lt;([^&]+)&gt;"),
                         QString("<a style='color: %1; font-weight: 600; text-decoration: underline;' href=\"\\1\">\\1</a>").arg(linkCol));
            htmlParagraphs.append(QString("<p style='margin: 0 0 10px 0; color: %1; font-size: 13px;'>%2</p>").arg(bodyText, para));
        }

        const QString fullHTML = QString(
            "<div style='color: %1; font-size: 13px; line-height: 145%;'>"
            "<p style='margin: 0 0 12px 0; color: %2; font-size: 14px; font-weight: 600;'>%3</p>"
            "%4"
            "</div>"
        ).arg(bodyText, headText, version.toHtmlEscaped(), htmlParagraphs.join(""));

        ui->aboutMessage->setTextFormat(Qt::RichText);
        ui->scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        text = version + "\n" + licenseInfo;
        ui->aboutMessage->setText(fullHTML);
        ui->aboutMessage->setWordWrap(true);
        ui->helpMessage->setVisible(false);
    } else {
        setWindowTitle(tr("Command-line options"));
        QString header = tr("Usage:") + "\n" +
                         "  kristatech-qt [" + tr("command-line options") + "]                     " + "\n";
        QTextCursor cursor(ui->helpMessage->document());

        QTextCharFormat normalFmt;
        normalFmt.setForeground(QColor(bodyText));
        QTextCharFormat bold;
        bold.setFontWeight(QFont::DemiBold);
        bold.setForeground(QColor(headText));

        cursor.insertText(version, bold);
        cursor.insertBlock();
        cursor.insertText(header, normalFmt);
        cursor.insertBlock();

        std::string strUsage = HelpMessage(HMM_BITCOIN_QT);
        strUsage += HelpMessageGroup(tr("UI Options:").toStdString());
        strUsage += HelpMessageOpt("-choosedatadir", strprintf(tr("Choose data directory on startup (default: %u)").toStdString(), DEFAULT_CHOOSE_DATADIR));
        strUsage += HelpMessageOpt("-lang=<lang>", tr("Set language, for example \"de_DE\" (default: system locale)").toStdString());
        strUsage += HelpMessageOpt("-min", tr("Start minimized").toStdString());
        strUsage += HelpMessageOpt("-rootcertificates=<file>", tr("Set SSL root certificates for payment request (default: -system-)").toStdString());
        strUsage += HelpMessageOpt("-splash", strprintf(tr("Show splash screen on startup (default: %u)").toStdString(), DEFAULT_SPLASHSCREEN));
        strUsage += HelpMessageOpt("-hidecharts", strprintf(tr("Hide QT staking charts on startup (default: %u)").toStdString(), false));
        QString coreOptions = QString::fromStdString(strUsage);
        text = version + "\n" + header + "\n" + coreOptions;

        QTextTableFormat tf;
        tf.setBorderStyle(QTextFrameFormat::BorderStyle_None);
        tf.setCellPadding(3);
        QVector<QTextLength> widths;
        widths << QTextLength(QTextLength::PercentageLength, 35);
        widths << QTextLength(QTextLength::PercentageLength, 65);
        tf.setColumnWidthConstraints(widths);

        Q_FOREACH (const QString &line, coreOptions.split("\n")) {
            if (line.startsWith("  -"))
            {
                cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::PreviousCell);
                cursor.movePosition(QTextCursor::NextRow);
                cursor.insertText(line.trimmed(), bold);
                cursor.movePosition(QTextCursor::NextCell);
            } else if (line.startsWith("   ")) {
                cursor.insertText(line.trimmed() + ' ', normalFmt);
            } else if (line.size() > 0) {
                //Title of a group
                if (cursor.currentTable())
                    cursor.currentTable()->appendRows(1);
                cursor.movePosition(QTextCursor::Down);
                cursor.insertText(line.trimmed(), bold);
                cursor.insertTable(1, 2, tf);
            }
        }

        ui->helpMessage->moveCursor(QTextCursor::Start);
        ui->scrollArea->setVisible(false);
    }
}

HelpMessageDialog::~HelpMessageDialog()
{
    GUIUtil::saveWindowGeometry("nHelpMessageDialogWindow", this);
    delete ui;
}

void HelpMessageDialog::printToConsole()
{
    // On other operating systems, the expected action is to print the message to the console.
    fprintf(stdout, "%s\n", qPrintable(text));
}

void HelpMessageDialog::showOrPrint()
{
#if defined(WIN32)
    // On Windows, show a message box, as there is no stderr/stdout in windowed applications
    exec();
#else
    // On other operating systems, print help text to console
    printToConsole();
#endif
}


/** "Shutdown" window */
ShutdownWindow::ShutdownWindow(QWidget* parent, Qt::WindowFlags f) : QWidget(parent, f)
{
    const bool light = isLightTheme();
    this->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet(QString(
        "ShutdownWindow { background-color: %1; border: 1px solid %2; }"
        "QLabel { background-color: transparent; color: %3; font-size: 13px; padding: 14px 18px; }"
    ).arg(light ? "#F8FAFC" : "#0D1321",
          light ? "#CBD5E1" : "#1E2D4A",
          light ? "#1E293B" : "#E2E8F0"));

    QVBoxLayout* layout = new QVBoxLayout();
    layout->setContentsMargins(12, 10, 12, 10);
    const QString shutTextCol = light ? "#1E293B" : "#E2E8F0";
    QLabel* lblShut = new QLabel(QString(
        "<div style='color: %1; font-size: 13px;'>%2<br/><br/>%3</div>"
    ).arg(shutTextCol,
          tr("KristaTech is shutting down...").toHtmlEscaped(),
          tr("Do not shut down the computer until this window disappears.").toHtmlEscaped()));
    lblShut->setTextFormat(Qt::RichText);
    layout->addWidget(lblShut);
    setLayout(layout);
}

void ShutdownWindow::showShutdownWindow(QMainWindow* window)
{
    if (!window)
        return;

    // Show a simple window indicating shutdown status
    QWidget* shutdownWindow = new ShutdownWindow();
    // We don't hold a direct pointer to the shutdown window after creation, so use
    // Qt::WA_DeleteOnClose to make sure that the window will be deleted eventually.
    shutdownWindow->setAttribute(Qt::WA_DeleteOnClose);
    shutdownWindow->setWindowTitle(window->windowTitle());

    // Center shutdown window at where main window was
    const QPoint global = window->mapToGlobal(window->rect().center());
    shutdownWindow->move(global.x() - shutdownWindow->width() / 2, global.y() - shutdownWindow->height() / 2);
    shutdownWindow->show();
}

void ShutdownWindow::closeEvent(QCloseEvent* event)
{
    event->ignore();
}
