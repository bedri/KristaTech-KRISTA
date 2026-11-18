// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsaboutwidget.h"

#include "clientversion.h"
#include "init.h"
#include "qt/kristatech/qtutils.h"
#include "version.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRegExp>
#include <QScrollArea>
#include <QVBoxLayout>

SettingsAboutWidget::SettingsAboutWidget(KRISTATECHGUI* _window, QWidget* parent) :
    PWidget(_window, parent)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(14);

    // Header row: Logo wordmark + version badge + Copy Version button
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(4, 0, 4, 0);
    headerLayout->setSpacing(12);

    logoLabel = new QLabel(this);
    logoLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);

    titleLabel = new QLabel(tr("About KristaTech"), this);
    setCssProperty(titleLabel, "text-title-faq");

    QString versionStr = tr("KristaTech version %1").arg(QString::fromStdString(FormatFullVersion()));
#if defined(__x86_64__)
    versionStr += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__)
    versionStr += " " + tr("(%1-bit)").arg(32);
#endif
    versionStr += QString("  |  Protocol %1").arg(PROTOCOL_VERSION);

    versionBadgeLabel = new QLabel(versionStr, this);
    titleCol->addWidget(titleLabel);
    titleCol->addWidget(versionBadgeLabel);

    copyVersionBtn = new QPushButton(tr("Copy Info"), this);
    copyVersionBtn->setCursor(Qt::PointingHandCursor);
    copyVersionBtn->setFocusPolicy(Qt::NoFocus);
    copyVersionBtn->setMinimumHeight(34);
    copyVersionBtn->setMaximumHeight(34);
    setCssProperty(copyVersionBtn, "btn-secundary-small");
    connect(copyVersionBtn, &QPushButton::clicked, this, &SettingsAboutWidget::onCopyVersionClicked);

    headerLayout->addWidget(logoLabel);
    headerLayout->addLayout(titleCol, 1);
    headerLayout->addWidget(copyVersionBtn, 0, Qt::AlignVCenter);
    mainLayout->addLayout(headerLayout);

    // Scrollable License & Build Card
    scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    QWidget* scrollContents = new QWidget(scrollArea);
    scrollContents->setStyleSheet("background: transparent;");
    QVBoxLayout* scrollLayout = new QVBoxLayout(scrollContents);
    scrollLayout->setContentsMargins(4, 2, 8, 12);
    scrollLayout->setSpacing(12);

    aboutCard = new QWidget(scrollContents);
    aboutCard->setObjectName("aboutLicenseCard");
    QVBoxLayout* cardLayout = new QVBoxLayout(aboutCard);
    cardLayout->setContentsMargins(20, 18, 20, 18);
    cardLayout->setSpacing(10);

    aboutMessageLabel = new QLabel(aboutCard);
    aboutMessageLabel->setWordWrap(true);
    aboutMessageLabel->setTextFormat(Qt::RichText);
    aboutMessageLabel->setOpenExternalLinks(true);
    aboutMessageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    cardLayout->addWidget(aboutMessageLabel);

    scrollLayout->addWidget(aboutCard);
    scrollLayout->addStretch(1);
    scrollArea->setWidget(scrollContents);
    mainLayout->addWidget(scrollArea, 1);

    const QString licenseInfo = QString::fromStdString(LicenseInfo());
    rawAboutText = versionStr + "\n\n" + licenseInfo;

    applyThemeStyles(isLightTheme());
}

void SettingsAboutWidget::onCopyVersionClicked()
{
    if (QApplication::clipboard()) {
        QApplication::clipboard()->setText(rawAboutText);
        inform(tr("Version & license info copied to clipboard"));
    }
}

void SettingsAboutWidget::applyThemeStyles(bool isLight)
{
    // Theme-adapted wordmark
    QImage aboutImg(":/images/about");
    if (!aboutImg.isNull()) {
        aboutImg = aboutImg.convertToFormat(QImage::Format_ARGB32);
        if (!isLight) {
            const int w = aboutImg.width();
            const int h = aboutImg.height();
            for (int y = 0; y < h; ++y) {
                QRgb* scan = reinterpret_cast<QRgb*>(aboutImg.scanLine(y));
                for (int x = 80; x < w; ++x) {
                    const int a = qAlpha(scan[x]);
                    if (a > 0) {
                        scan[x] = (x < 358) ? qRgba(241, 245, 249, a) : qRgba(155, 180, 226, a);
                    }
                }
            }
        }
        logoLabel->setPixmap(QPixmap::fromImage(aboutImg).scaledToHeight(48, Qt::SmoothTransformation));
    }

    const QString cardBg = isLight ? "#FFFFFF" : "#182132";
    const QString cardBorder = isLight ? "#DDE5F2" : "#28354D";
    const QString bodyText = isLight ? "#334155" : "#CBD5E1";
    const QString headText = isLight ? "#0F172A" : "#F8FAFC";
    const QString badgeText = isLight ? "#4A679E" : "#8FAEE6";
    const QString linkCol = isLight ? "#3E5C94" : "#8FB0E8";

    if (versionBadgeLabel) {
        versionBadgeLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: 500; }").arg(badgeText));
    }
    if (aboutCard) {
        aboutCard->setStyleSheet(QString(
            "QWidget#aboutLicenseCard { background-color: %1; border: 1px solid %2; border-radius: 10px; }"
        ).arg(cardBg, cardBorder));
    }

    QString versionLine = tr("KristaTech version %1").arg(QString::fromStdString(FormatFullVersion()));
#if defined(__x86_64__)
    versionLine += " " + tr("(%1-bit)").arg(64);
#elif defined(__i386__)
    versionLine += " " + tr("(%1-bit)").arg(32);
#endif

    const QString licenseInfo = QString::fromStdString(LicenseInfo());
    const QStringList rawParagraphs = licenseInfo.trimmed().split("\n\n", QString::SkipEmptyParts);
    QStringList htmlParagraphs;
    for (QString para : rawParagraphs) {
        para.replace(QRegExp("\\s*\\n\\s*"), " ");
        para = para.trimmed().toHtmlEscaped();
        para.replace(QRegExp("&lt;([^&]+)&gt;"),
                     QString("<a style='color: %1; font-weight: 600; text-decoration: underline;' href=\"\\1\">\\1</a>").arg(linkCol));
        htmlParagraphs.append(QString("<p style='margin: 0 0 10px 0; color: %1; font-size: 13px;'>%2</p>").arg(bodyText, para));
    }

    const QString fullHTML = QString(
        "<div style='color: %1; font-size: 13px; line-height: 150%;'>"
        "<p style='margin: 0 0 12px 0; color: %2; font-size: 14px; font-weight: 600;'>%3</p>"
        "%4"
        "</div>"
    ).arg(bodyText, headText, versionLine.toHtmlEscaped(), htmlParagraphs.join(""));

    if (aboutMessageLabel) {
        aboutMessageLabel->setText(fullHTML);
        aboutMessageLabel->setStyleSheet(QString("QLabel { background: transparent; border: none; color: %1; }").arg(bodyText));
    }
}

void SettingsAboutWidget::changeTheme(bool isLightTheme, QString& theme)
{
    PWidget::changeTheme(isLightTheme, theme);
    applyThemeStyles(isLightTheme);
}
