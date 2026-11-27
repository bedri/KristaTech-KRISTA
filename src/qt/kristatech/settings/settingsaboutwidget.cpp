// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsaboutwidget.h"

#include "chainparams.h"
#include "clientversion.h"
#include "guiutil.h"
#include "init.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "version.h"

#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRegExp>
#include <QUrl>
#include <QVBoxLayout>
#include <functional>

namespace {

QString formatNetworkDisplayName()
{
    const std::string net = Params().NetworkIDString();
    if (net == "main") return QStringLiteral("Mainnet");
    if (net == "test") return QStringLiteral("Testnet");
    if (net == "regtest") return QStringLiteral("Regtest");
    if (net.empty()) return QStringLiteral("Mainnet");
    QString out = QString::fromStdString(net);
    out[0] = out[0].toUpper();
    return out;
}

void addCompactRuntimeRow(QWidget* card,
                          QVBoxLayout* cardBody,
                          const QString& keyText,
                          const QString& valueText,
                          bool monoStyle,
                          bool addDividerBefore)
{
    if (!card || !cardBody) return;
    if (addDividerBefore) {
        cardBody->addWidget(createSettingsDivider(card));
    }

    QWidget* row = new QWidget(card);
    QHBoxLayout* h = new QHBoxLayout(row);
    h->setContentsMargins(0, addDividerBefore ? 9 : 2, 0, 9);
    h->setSpacing(12);

    QLabel* lblKey = new QLabel(keyText, row);
    lblKey->setProperty("settingsInfoKey", true);
    lblKey->setFixedWidth(124);
    h->addWidget(lblKey, 0, Qt::AlignTop);

    QLabel* lblVal = new QLabel(valueText, row);
    lblVal->setProperty(monoStyle ? "settingsInfoMono" : "settingsInfoVal", true);
    lblVal->setTextFormat(Qt::PlainText);
    lblVal->setWordWrap(true);
    lblVal->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    lblVal->setCursor(Qt::IBeamCursor);
    h->addWidget(lblVal, 1, Qt::AlignVCenter);

    cardBody->addWidget(row);
}

} // namespace

SettingsAboutWidget::SettingsAboutWidget(KRISTATECHGUI* _window, QWidget* parent) :
    PWidget(_window, parent)
{
    QVBoxLayout* hostLayout = new QVBoxLayout(this);
    hostLayout->setContentsMargins(0, 0, 0, 0);
    hostLayout->setSpacing(0);

    QVBoxLayout* scrollLayout = nullptr;
    QHBoxLayout* headerRight = nullptr;
    QVBoxLayout* rootLayout = nullptr;
    modernRoot = createSettingsPageShell(
        this,
        hostLayout,
        tr("About KristaTech"),
        tr("Software version, network details, helpful links, and open-source licenses."),
        scrollLayout,
        headerRight,
        rootLayout);

    copyVersionBtn = new QPushButton(tr("Copy Info"), modernRoot);
    copyVersionBtn->setObjectName(QStringLiteral("aboutCopyInfoBtn"));
    copyVersionBtn->setProperty("settingsSecondary", true);
    copyVersionBtn->setCursor(Qt::PointingHandCursor);
    copyVersionBtn->setFocusPolicy(Qt::TabFocus);
    copyVersionBtn->setFixedHeight(34);
    connect(copyVersionBtn, &QPushButton::clicked, this, &SettingsAboutWidget::onCopyVersionClicked);
    headerRight->addWidget(copyVersionBtn);

    const QString releaseVer = QString("v%1.%2.%3.%4")
        .arg(CLIENT_VERSION_MAJOR)
        .arg(CLIENT_VERSION_MINOR)
        .arg(CLIENT_VERSION_REVISION)
        .arg(CLIENT_VERSION_BUILD);
    const QString buildTag = QString::fromStdString(FormatFullVersion());
    const QString networkName = formatNetworkDisplayName();
    const int p2pPort = Params().GetDefaultPort();
    const QString qtVer = QString("Qt %1").arg(QString::fromLatin1(qVersion()));
    const QString buildDate = QString::fromStdString(CLIENT_DATE);

#if defined(__x86_64__) || defined(_M_X64)
    const QString archLabel = tr("%1-bit").arg(64);
#elif defined(__i386__) || defined(_M_IX86)
    const QString archLabel = tr("%1-bit").arg(32);
#else
    const QString archLabel = tr("Native");
#endif

    // 1. Hero identity card
    heroCard = new QFrame(modernRoot);
    heroCard->setObjectName(QStringLiteral("aboutHeroCard"));
    heroCard->setProperty("settingsCard", true);
    heroCard->setAttribute(Qt::WA_StyledBackground, true);

    QHBoxLayout* heroLay = new QHBoxLayout(heroCard);
    heroLay->setContentsMargins(20, 18, 20, 18);
    heroLay->setSpacing(16);

    QVBoxLayout* heroLeft = new QVBoxLayout();
    heroLeft->setContentsMargins(0, 0, 0, 0);
    heroLeft->setSpacing(6);

    logoLabel = new QLabel(heroCard);
    logoLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    heroLeft->addWidget(logoLabel, 0, Qt::AlignLeft);

    taglineLabel = new QLabel(
        tr("Fair, community-first hybrid Layer-1 digital currency wallet."),
        heroCard);
    taglineLabel->setWordWrap(true);
    heroLeft->addWidget(taglineLabel);

    QHBoxLayout* badgesLay = new QHBoxLayout();
    badgesLay->setContentsMargins(0, 0, 0, 0);
    badgesLay->setSpacing(8);

    const QStringList badgeTexts = {
        releaseVer,
        networkName,
        archLabel,
        tr("MIT Open Source")
    };
    for (int i = 0; i < badgeTexts.size(); ++i) {
        QLabel* badge = new QLabel(badgeTexts[i], heroCard);
        badge->setProperty("aboutBadgeVariant", i == 0 ? QStringLiteral("primary") : QStringLiteral("neutral"));
        badge->setAlignment(Qt::AlignCenter);
        badgesLay->addWidget(badge);
        heroBadges.push_back(badge);
    }

    heroLay->addLayout(heroLeft, 1);
    heroLay->addLayout(badgesLay, 0);
    heroLay->setAlignment(badgesLay, Qt::AlignVCenter | Qt::AlignRight);
    scrollLayout->addWidget(heroCard);

    // 2. Two-column balanced row: Software & Runtime | Quick Links & Resources
    QHBoxLayout* columnsRow = new QHBoxLayout();
    columnsRow->setContentsMargins(0, 0, 0, 0);
    columnsRow->setSpacing(14);

    // Left card: Software & Runtime
    QVBoxLayout* runtimeBody = nullptr;
    QFrame* runtimeCard = createSettingsCard(modernRoot, tr("Software & Runtime"), runtimeBody);
    runtimeCard->setObjectName(QStringLiteral("aboutRuntimeCard"));
    runtimeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Release Version"), releaseVer, false, false);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Build Tag"), buildTag, true, true);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Protocol Version"), QString::number(PROTOCOL_VERSION), true, true);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Network & Port"),
                         QString("%1  (TCP %2)").arg(networkName).arg(p2pPort), false, true);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Qt Framework"), qtVer, false, true);
    addCompactRuntimeRow(runtimeCard, runtimeBody, tr("Build Date"), buildDate, false, true);
    runtimeBody->addStretch(1);
    columnsRow->addWidget(runtimeCard, 1);

    // Right card: Quick Links & Resources
    QVBoxLayout* linksBody = nullptr;
    QFrame* linksCard = createSettingsCard(modernRoot, tr("Quick Links & Resources"), linksBody);
    linksCard->setObjectName(QStringLiteral("aboutLinksCard"));
    linksCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto addQuickLinkRow = [&](const QString& title,
                               const QString& desc,
                               const QString& arrowGlyph,
                               std::function<void()> onClick,
                               bool addDividerBefore) {
        if (addDividerBefore) {
            linksBody->addWidget(createSettingsDivider(linksCard));
        }

        QPushButton* rowBtn = new QPushButton(linksCard);
        rowBtn->setProperty("aboutLinkRow", true);
        rowBtn->setCursor(Qt::PointingHandCursor);
        rowBtn->setFocusPolicy(Qt::TabFocus);
        rowBtn->setMinimumHeight(54);
        rowBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QHBoxLayout* rowLay = new QHBoxLayout(rowBtn);
        rowLay->setContentsMargins(10, 8, 10, 8);
        rowLay->setSpacing(10);

        QVBoxLayout* textCol = new QVBoxLayout();
        textCol->setContentsMargins(0, 0, 0, 0);
        textCol->setSpacing(2);

        QLabel* lblTitle = new QLabel(title, rowBtn);
        lblTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
        QLabel* lblDesc = new QLabel(desc, rowBtn);
        lblDesc->setWordWrap(true);
        lblDesc->setAttribute(Qt::WA_TransparentForMouseEvents);
        textCol->addWidget(lblTitle);
        textCol->addWidget(lblDesc);

        QLabel* lblArrow = new QLabel(arrowGlyph, rowBtn);
        lblArrow->setAlignment(Qt::AlignCenter);
        lblArrow->setAttribute(Qt::WA_TransparentForMouseEvents);

        rowLay->addLayout(textCol, 1);
        rowLay->addWidget(lblArrow, 0, Qt::AlignVCenter);

        connect(rowBtn, &QPushButton::clicked, this, [onClick]() {
            if (onClick) onClick();
        });

        linksBody->addWidget(rowBtn);
        linkRowButtons.push_back(rowBtn);
        linkTitleLabels.push_back(lblTitle);
        linkDescLabels.push_back(lblDesc);
        linkArrowLabels.push_back(lblArrow);
    };

    const QString internalArrow = QString::fromUtf8("\xE2\x86\x92");
    const QString externalArrow = QString::fromUtf8("\xE2\x86\x97");

    addQuickLinkRow(
        tr("Help & FAQ"),
        tr("Plain-English guides for sending, backups, earning, and fixes."),
        internalArrow,
        [this]() {
            if (window) {
                window->openFAQ(0);
            }
        },
        false);
    addQuickLinkRow(
        tr("Node Information"),
        tr("Inspect live peers, block height, and wallet file location."),
        internalArrow,
        [this]() {
            if (window) {
                window->goToSettingsInfo();
            }
        },
        true);
    addQuickLinkRow(
        tr("Official Website"),
        tr("Guides, downloads, and project news (krista.kristalteknoloji.com)."),
        externalArrow,
        []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://krista.kristalteknoloji.com")));
        },
        true);
    addQuickLinkRow(
        tr("Block Explorer"),
        tr("Explore live blocks, transactions, and network statistics."),
        externalArrow,
        []() {
            QDesktopServices::openUrl(QUrl(QStringLiteral("https://explorer.kristalteknoloji.com")));
        },
        true);

    linksBody->addStretch(1);
    columnsRow->addWidget(linksCard, 1);
    scrollLayout->addLayout(columnsRow);

    // 3. Bottom card: Open-Source License & Credits
    QVBoxLayout* licBody = nullptr;
    licenseCard = createSettingsCard(modernRoot, tr("Open-Source License & Credits"), licBody);
    licenseCard->setObjectName(QStringLiteral("aboutLicenseCard"));

    const QString licenseInfo = QString::fromStdString(LicenseInfo());
    const QStringList rawParagraphs = licenseInfo.trimmed().split("\n\n", QString::SkipEmptyParts);
    QStringList copyrights;
    for (QString para : rawParagraphs) {
        para.replace(QRegExp("\\s*\\n\\s*"), " ");
        para = para.trimmed();
        if (para.isEmpty()) continue;
        if (para.startsWith(QStringLiteral("Copyright"), Qt::CaseInsensitive)) {
            copyrights.append(para);
        } else {
            parsedLicenseNotes.append(para);
        }
    }
    // Present newest lineage (KristaTech) first while preserving complete upstream attribution
    for (int i = copyrights.size() - 1; i >= 0; --i) {
        parsedCopyrights.append(copyrights[i]);
    }

    licenseAlertBox = new QFrame(licenseCard);
    licenseAlertBox->setProperty("settingsAlertBox", QStringLiteral("info"));
    licenseAlertBox->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* alertLay = new QVBoxLayout(licenseAlertBox);
    alertLay->setContentsMargins(14, 10, 14, 10);
    alertLay->setSpacing(4);

    licenseSummaryLabel = new QLabel(licenseAlertBox);
    licenseSummaryLabel->setWordWrap(true);
    licenseSummaryLabel->setTextFormat(Qt::RichText);
    licenseSummaryLabel->setOpenExternalLinks(true);
    licenseSummaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    alertLay->addWidget(licenseSummaryLabel);
    licBody->addWidget(licenseAlertBox);

    if (!parsedCopyrights.isEmpty()) {
        licBody->addSpacing(12);
        creditsHeaderLabel = new QLabel(tr("Contributors & Upstream Lineage"), licenseCard);
        creditsHeaderLabel->setProperty("settingsRowTitle", true);
        licBody->addWidget(creditsHeaderLabel);
        licBody->addSpacing(6);

        QWidget* creditsGrid = new QWidget(licenseCard);
        QVBoxLayout* creditsLay = new QVBoxLayout(creditsGrid);
        creditsLay->setContentsMargins(0, 0, 0, 6);
        creditsLay->setSpacing(4);
        for (const QString& copyLine : parsedCopyrights) {
            QLabel* lblCopy = new QLabel(copyLine, creditsGrid);
            lblCopy->setWordWrap(true);
            lblCopy->setTextFormat(Qt::PlainText);
            lblCopy->setTextInteractionFlags(Qt::TextSelectableByMouse);
            creditsLay->addWidget(lblCopy);
            creditLabels.push_back(lblCopy);
        }
        licBody->addWidget(creditsGrid);
    }

    scrollLayout->addWidget(licenseCard);
    scrollLayout->addStretch(1);

    rawAboutText = QString(
        "KristaTech %1 (%2, %3)\n"
        "Build: %4\n"
        "Protocol: %5  |  Port: %6  |  %7\n"
        "Build Date: %8\n\n%9"
    ).arg(releaseVer, networkName, archLabel, buildTag)
     .arg(PROTOCOL_VERSION)
     .arg(p2pPort)
     .arg(qtVer, buildDate, licenseInfo);

    applyThemeStyles(isLightTheme());
}

void SettingsAboutWidget::onCopyVersionClicked()
{
    GUIUtil::setClipboard(rawAboutText);
    inform(tr("Version & license info copied to clipboard"));
}

void SettingsAboutWidget::applyThemeStyles(bool isLight)
{
    // Theme-adapted wordmark (cached per theme)
    static QPixmap cachedLightLogo;
    static QPixmap cachedDarkLogo;
    QPixmap& cachedLogo = isLight ? cachedLightLogo : cachedDarkLogo;
    if (cachedLogo.isNull()) {
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
            cachedLogo = QPixmap::fromImage(aboutImg).scaledToHeight(42, Qt::SmoothTransformation);
        }
    }
    if (!cachedLogo.isNull() && logoLabel) {
        logoLabel->setPixmap(cachedLogo);
    }

    const QString titleCol    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString mutedCol    = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    const QString bodyCol     = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString linkCol     = isLight ? QStringLiteral("#2F4878") : QStringLiteral("#93C5FD");
    const QString primBg      = isLight ? QStringLiteral("#EEF3FB") : QStringLiteral("#1B2B4A");
    const QString primFg      = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE");
    const QString primBrd     = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#355184");
    const QString neutBg      = isLight ? QStringLiteral("#F1F5F9") : QStringLiteral("#1A2438");
    const QString neutFg      = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString neutBrd     = isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#2E3F5E");
    const QString rowHoverBg  = isLight ? QStringLiteral("#F4F7FC") : QStringLiteral("#1A2640");
    const QString arrowCol    = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#8FAEE6");

    const QString extraCss = QString(
        "QPushButton[aboutLinkRow=\"true\"] { background: transparent; border: none; border-radius: 7px; text-align: left; }"
        "QPushButton[aboutLinkRow=\"true\"]:hover { background-color: %1; }"
        "QPushButton[aboutLinkRow=\"true\"]:focus { border: 1.5px solid #7695CD; outline: none; }"
    ).arg(rowHoverBg);

    if (modernRoot) {
        modernRoot->setStyleSheet(buildSettingsPageStyleSheet(isLight) + extraCss);
    }

    if (taglineLabel) {
        taglineLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; background: transparent; }").arg(mutedCol));
    }

    for (QLabel* badge : heroBadges) {
        if (!badge) continue;
        const bool isPrimary = badge->property("aboutBadgeVariant").toString() == QStringLiteral("primary");
        const QString bg = isPrimary ? primBg : neutBg;
        const QString fg = isPrimary ? primFg : neutFg;
        const QString brd = isPrimary ? primBrd : neutBrd;
        badge->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 11px; padding: 4px 10px; font-size: 11.5px; font-weight: 600; }"
        ).arg(bg, fg, brd));
    }

    for (QLabel* lbl : linkTitleLabels) {
        if (lbl) lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; }").arg(titleCol));
    }
    for (QLabel* lbl : linkDescLabels) {
        if (lbl) lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; background: transparent; }").arg(mutedCol));
    }
    for (QLabel* lbl : linkArrowLabels) {
        if (lbl) lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 15px; font-weight: 700; background: transparent; }").arg(arrowCol));
    }
    for (QLabel* lbl : creditLabels) {
        if (lbl) lbl->setStyleSheet(QString("QLabel { color: %1; font-size: 12.5px; background: transparent; }").arg(bodyCol));
    }

    if (licenseSummaryLabel) {
        QStringList htmlParas;
        for (const QString& note : parsedLicenseNotes) {
            QString escaped = note.toHtmlEscaped();
            escaped.replace(
                QRegExp("&lt;([^&]+)&gt;"),
                QString("<a style=\"color:%1; font-weight:600; text-decoration:underline;\" href=\"\\1\">\\1</a>").arg(linkCol));
            htmlParas.append(QString("<p style=\"margin: 0 0 6px 0; color: %1; font-size: 12.5px; line-height: 1.45;\">%2</p>")
                                 .arg(bodyCol, escaped));
        }
        licenseSummaryLabel->setText(htmlParas.join(QString()));
    }
}

void SettingsAboutWidget::changeTheme(bool isLightTheme, QString& theme)
{
    PWidget::changeTheme(isLightTheme, theme);
    applyThemeStyles(isLightTheme);
}
