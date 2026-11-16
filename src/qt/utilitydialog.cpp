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

#include "qt/kristatech/qtutils.h"

#include "clientversion.h"
#include "init.h"
#include "util.h"

#include <stdio.h>

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegExp>
#include <QShortcut>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextTable>
#include <QTextCursor>
#include <QTimer>
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
        if (width() < 780 || height() < 580) {
            resize(qMax(width(), 800), qMax(height(), 600));
        }

        QString header = tr("Usage:") + "\n" +
                         "  kristatech-qt [" + tr("command-line options") + "]                     " + "\n";

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

        struct CliOptionEntry {
            QString group;
            QString flag;
            QString description;
        };
        QList<CliOptionEntry> entries;
        QStringList categories;
        QString currentGroup = tr("General");

        const QStringList lines = coreOptions.split("\n");
        for (const QString& rawLine : lines) {
            if (rawLine.startsWith("  -")) {
                QString trimmed = rawLine.trimmed();
                // Handle single-line "  -flag   description" if any, otherwise description follows on indented lines
                int splitIdx = trimmed.indexOf("  ");
                QString flagPart = (splitIdx > 0) ? trimmed.left(splitIdx).trimmed() : trimmed;
                QString descPart = (splitIdx > 0) ? trimmed.mid(splitIdx).trimmed() : QString();
                entries.append({currentGroup, flagPart, descPart});
            } else if (rawLine.startsWith("   ")) {
                if (!entries.isEmpty()) {
                    QString cont = rawLine.trimmed();
                    if (!cont.isEmpty()) {
                        if (!entries.last().description.isEmpty())
                            entries.last().description += " ";
                        entries.last().description += cont;
                    }
                }
            } else if (!rawLine.trimmed().isEmpty()) {
                currentGroup = rawLine.trimmed();
                if (currentGroup.endsWith(':'))
                    currentGroup.chop(1);
                if (!categories.contains(currentGroup))
                    categories.append(currentGroup);
            }
        }

        ui->helpMessage->setVisible(false);
        ui->scrollArea->setVisible(false);

        const QString surfaceBg   = light ? "#FFFFFF" : "#111A2E";
        const QString subtleBg    = light ? "#F1F5F9" : "#16223B";
        const QString borderCol   = light ? "#CBD5E1" : "#263859";
        const QString mutedText   = light ? "#64748B" : "#94A3B8";
        const QString accentCol   = light ? "#5576B2" : "#7695CD";
        const QString accentHover = light ? "#48669E" : "#89A6DC";
        const QString rowAltBg    = light ? "#F8FAFC" : "#142038";
        const QString selBg       = light ? "#E6EEF9" : "#1F3358";

        // 1. Header meta row (version + live count badge)
        QWidget* metaBar = new QWidget(ui->helpContainer);
        QHBoxLayout* metaLayout = new QHBoxLayout(metaBar);
        metaLayout->setContentsMargins(0, 0, 0, 0);
        metaLayout->setSpacing(8);

        QLabel* lblVersion = new QLabel(QString("%1  •  kristatech-qt [options]").arg(version), metaBar);
        lblVersion->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: 600; background: transparent; border: none;").arg(headText));
        QLabel* lblCount = new QLabel(metaBar);
        lblCount->setStyleSheet(QString(
            "color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; "
            "padding: 2px 10px; font-size: 11px; font-weight: 600;"
        ).arg(accentCol, subtleBg, borderCol));

        metaLayout->addWidget(lblVersion);
        metaLayout->addStretch();
        metaLayout->addWidget(lblCount);

        // 2. Interactive Search & Category Filter Bar
        QWidget* filterBar = new QWidget(ui->helpContainer);
        QHBoxLayout* filterLayout = new QHBoxLayout(filterBar);
        filterLayout->setContentsMargins(0, 0, 0, 0);
        filterLayout->setSpacing(8);

        QLineEdit* searchEdit = new QLineEdit(filterBar);
        searchEdit->setPlaceholderText(tr("Search flags or descriptions (e.g. -rpcauth, -blockmaxsize, taproot)...  [Ctrl+F]"));
        searchEdit->setClearButtonEnabled(true);
        searchEdit->setFixedHeight(34);
        searchEdit->setStyleSheet(QString(
            "QLineEdit {"
            "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
            "  padding: 0 10px; font-size: 13px;"
            "}"
            "QLineEdit:focus { border: 1px solid %4; }"
        ).arg(surfaceBg, headText, borderCol, accentCol));

        QComboBox* categoryCombo = new QComboBox(filterBar);
        categoryCombo->setFixedHeight(34);
        categoryCombo->setMinimumWidth(190);
        initComboBox(categoryCombo);
        categoryCombo->addItem(tr("All Categories"), QString());
        for (const QString& cat : categories) {
            categoryCombo->addItem(cat, cat);
        }
        categoryCombo->setStyleSheet(QString(
            "QComboBox {"
            "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
            "  padding: 0 10px; font-size: 12px; font-weight: 500;"
            "}"
            "QComboBox:hover, QComboBox:focus { border: 1px solid %4; }"
            "QComboBox::drop-down { border: none; width: 22px; }"
            "QComboBox QAbstractItemView {"
            "  background-color: %1; color: %2; border: 1px solid %3; selection-background-color: %5;"
            "}"
        ).arg(surfaceBg, headText, borderCol, accentCol, selBg));

        filterLayout->addWidget(searchEdit, 1);
        filterLayout->addWidget(categoryCombo, 0);

        // 3. Interactive Options Table
        QTableWidget* table = new QTableWidget(ui->helpContainer);
        table->setColumnCount(3);
        table->setHorizontalHeaderLabels({tr("Option / Flag"), tr("Category"), tr("Description")});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->setWordWrap(true);
        table->setShowGrid(false);
        table->verticalHeader()->setVisible(false);
        table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setHighlightSections(false);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
        table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table->setColumnWidth(0, 225);
        table->setColumnWidth(1, 145);
        table->setFocusPolicy(Qt::StrongFocus);
        table->setStyleSheet(QString(
            "QTableWidget {"
            "  background-color: %1; alternate-background-color: %2; color: %3;"
            "  border: 1px solid %4; border-radius: 8px; font-size: 12px; outline: none;"
            "}"
            "QTableWidget::item {"
            "  padding: 7px 10px; border-bottom: 1px solid %4;"
            "}"
            "QTableWidget::item:selected {"
            "  background-color: %5; color: %6;"
            "}"
            "QHeaderView::section {"
            "  background-color: %7; color: %3; border: none; border-bottom: 1px solid %4;"
            "  padding: 7px 10px; font-size: 12px; font-weight: 600; text-align: left;"
            "}"
        ).arg(surfaceBg, rowAltBg, bodyText, borderCol, selBg, headText, subtleBg));

        // 4. Quick Preview & Copy Bar
        QFrame* previewBar = new QFrame(ui->helpContainer);
        previewBar->setStyleSheet(QString(
            "QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; }"
        ).arg(subtleBg, borderCol));
        QHBoxLayout* previewLayout = new QHBoxLayout(previewBar);
        previewLayout->setContentsMargins(10, 8, 10, 8);
        previewLayout->setSpacing(8);

        QLabel* lblPreview = new QLabel(tr("Select an option (or double-click a row) to copy CLI flag or kristatech.conf entry."), previewBar);
        lblPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);
        lblPreview->setStyleSheet(QString(
            "color: %1; font-family: monospace; font-size: 12px; background: transparent; border: none;"
        ).arg(headText));

        auto makeActionBtn = [&](const QString& title) -> QPushButton* {
            QPushButton* b = new QPushButton(title, previewBar);
            b->setCursor(Qt::PointingHandCursor);
            b->setFocusPolicy(Qt::NoFocus);
            b->setFixedHeight(28);
            b->setStyleSheet(QString(
                "QPushButton {"
                "  background-color: %1; color: %2; border: 1px solid %3; border-radius: 5px;"
                "  padding: 0 10px; font-size: 11px; font-weight: 600;"
                "}"
                "QPushButton:hover { border-color: %4; color: %4; }"
                "QPushButton:disabled { color: %5; border-color: %3; }"
            ).arg(surfaceBg, headText, borderCol, accentCol, mutedText));
            return b;
        };

        QPushButton* btnCopyFlag = makeActionBtn(tr("Copy CLI Flag"));
        QPushButton* btnCopyConf = makeActionBtn(tr("Copy .conf"));
        QPushButton* btnCopyAll  = makeActionBtn(tr("Copy Visible"));
        btnCopyFlag->setEnabled(false);
        btnCopyConf->setEnabled(false);

        previewLayout->addWidget(lblPreview, 1);
        previewLayout->addWidget(btnCopyFlag);
        previewLayout->addWidget(btnCopyConf);
        previewLayout->addWidget(btnCopyAll);

        ui->verticalLayout->addWidget(metaBar);
        ui->verticalLayout->addWidget(filterBar);
        ui->verticalLayout->addWidget(table, 1);
        ui->verticalLayout->addWidget(previewBar);

        auto flashCopied = [accentCol, accentHover](QPushButton* btn, const QString& origText) {
            btn->setText(tr("Copied!"));
            QTimer::singleShot(1200, btn, [btn, origText]() {
                if (btn) btn->setText(origText);
            });
            Q_UNUSED(accentCol);
            Q_UNUSED(accentHover);
        };

        auto toConfLine = [](const QString& flag) -> QString {
            QString f = flag.startsWith('-') ? flag.mid(1) : flag;
            if (!f.contains('=')) {
                return f + "=1";
            }
            return f;
        };

        auto updatePreview = [=]() {
            int row = table->currentRow();
            if (row < 0 || !table->item(row, 0)) {
                btnCopyFlag->setEnabled(false);
                btnCopyConf->setEnabled(false);
                lblPreview->setText(tr("Select an option (or double-click a row) to copy CLI flag or kristatech.conf entry."));
                return;
            }
            QString flag = table->item(row, 0)->text();
            QString confLine = toConfLine(flag);
            lblPreview->setText(QString("CLI: %1   |   kristatech.conf: %2").arg(flag, confLine));
            btnCopyFlag->setEnabled(true);
            btnCopyConf->setEnabled(true);
        };

        auto populateTable = [=]() {
            const QString query = searchEdit->text().trimmed();
            const QString catFilter = categoryCombo->currentData().toString();

            table->setRowCount(0);
            int shown = 0;
            for (const CliOptionEntry& e : entries) {
                if (!catFilter.isEmpty() && e.group != catFilter)
                    continue;
                if (!query.isEmpty() &&
                    !e.flag.contains(query, Qt::CaseInsensitive) &&
                    !e.description.contains(query, Qt::CaseInsensitive) &&
                    !e.group.contains(query, Qt::CaseInsensitive)) {
                    continue;
                }
                int r = table->rowCount();
                table->insertRow(r);

                QTableWidgetItem* flagItem = new QTableWidgetItem(e.flag);
                QFont monoFont = flagItem->font();
                monoFont.setFamily("monospace");
                monoFont.setBold(true);
                flagItem->setFont(monoFont);
                flagItem->setForeground(QColor(headText));

                QTableWidgetItem* catItem = new QTableWidgetItem(e.group);
                catItem->setForeground(QColor(accentCol));

                QTableWidgetItem* descItem = new QTableWidgetItem(e.description);
                descItem->setForeground(QColor(bodyText));

                table->setItem(r, 0, flagItem);
                table->setItem(r, 1, catItem);
                table->setItem(r, 2, descItem);
                ++shown;
            }
            table->resizeRowsToContents();
            lblCount->setText(tr("%1 / %2 options").arg(shown).arg(entries.size()));
            if (shown > 0) {
                table->selectRow(0);
            } else {
                updatePreview();
            }
        };

        connect(searchEdit, &QLineEdit::textChanged, this, [=](const QString&) { populateTable(); });
        connect(categoryCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [=](int) { populateTable(); });
        connect(table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [=](const QItemSelection&, const QItemSelection&) { updatePreview(); });
        connect(table, &QTableWidget::cellDoubleClicked, this, [=](int row, int) {
            if (row >= 0 && table->item(row, 0)) {
                QApplication::clipboard()->setText(table->item(row, 0)->text());
                flashCopied(btnCopyFlag, tr("Copy CLI Flag"));
            }
        });
        connect(btnCopyFlag, &QPushButton::clicked, this, [=]() {
            int row = table->currentRow();
            if (row >= 0 && table->item(row, 0)) {
                QApplication::clipboard()->setText(table->item(row, 0)->text());
                flashCopied(btnCopyFlag, tr("Copy CLI Flag"));
            }
        });
        connect(btnCopyConf, &QPushButton::clicked, this, [=]() {
            int row = table->currentRow();
            if (row >= 0 && table->item(row, 0)) {
                QApplication::clipboard()->setText(toConfLine(table->item(row, 0)->text()));
                flashCopied(btnCopyConf, tr("Copy .conf"));
            }
        });
        connect(btnCopyAll, &QPushButton::clicked, this, [=]() {
            QStringList out;
            for (int r = 0; r < table->rowCount(); ++r) {
                out << QString("%1\t[%2]\t%3").arg(
                    table->item(r, 0)->text(),
                    table->item(r, 1)->text(),
                    table->item(r, 2)->text());
            }
            QApplication::clipboard()->setText(out.join("\n"));
            flashCopied(btnCopyAll, tr("Copy Visible"));
        });

        QShortcut* findShortcut = new QShortcut(QKeySequence::Find, this);
        connect(findShortcut, &QShortcut::activated, searchEdit, [searchEdit]() {
            searchEdit->setFocus();
            searchEdit->selectAll();
        });

        populateTable();
        searchEdit->setFocus();
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
