// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsfaqwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsfaqwidget.h"
#include "qt/kristatech/settings/settingswidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "amount.h"
#include "chainparams.h"
#include "clientversion.h"
#include "version.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QEventLoop>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

struct FaqHtmlTheme {
    QString bodyText;
    QString mutedText;
    QString codeBg;
    QString codeText;
    QString tableBorder;
    QString tableHdrBg;
    QString tableHdrText;
    QString tableCellBg;
    QString tableAltBg;
    QString accentBlue;
    QString accentCoral;
};

FaqHtmlTheme makeHtmlTheme(bool isLight)
{
    FaqHtmlTheme t;
    t.bodyText     = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    t.mutedText    = isLight ? QStringLiteral("#64748B") : QStringLiteral("#94A3B8");
    t.codeBg       = isLight ? QStringLiteral("#EEF3FB") : QStringLiteral("#0F172A");
    t.codeText     = isLight ? QStringLiteral("#2F4878") : QStringLiteral("#93C5FD");
    t.tableBorder  = isLight ? QStringLiteral("#D5DFEE") : QStringLiteral("#2B3B58");
    t.tableHdrBg   = isLight ? QStringLiteral("#EBF1FA") : QStringLiteral("#202D47");
    t.tableHdrText = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#E2ECFA");
    t.tableCellBg  = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#151E30");
    t.tableAltBg   = isLight ? QStringLiteral("#F8FAFD") : QStringLiteral("#192439");
    t.accentBlue   = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#9BB4E2");
    t.accentCoral  = isLight ? QStringLiteral("#C85A4D") : QStringLiteral("#F5897C");
    return t;
}

QString wrapFaqHtml(const FaqHtmlTheme& t, const QString& bodyInner)
{
    return QString(
        "<html><head><style>"
        "body { color: %1; font-size: 13.5px; line-height: 1.55; }"
        "p { margin-top: 3px; margin-bottom: 9px; }"
        "ul, ol { margin-top: 4px; margin-bottom: 9px; margin-left: 18px; }"
        "li { margin-bottom: 5px; }"
        "code { background-color: %2; color: %3; font-family: monospace; font-size: 12.5px; }"
        "b { color: %4; }"
        "</style></head><body>%5</body></html>"
    ).arg(t.bodyText, t.codeBg, t.codeText, t.tableHdrText, bodyInner);
}

QString buildStyledTable(const FaqHtmlTheme& t,
                         const QStringList& headers,
                         const std::vector<QStringList>& rows)
{
    QString html = QString(
        "<table width=\"100%\" cellspacing=\"1\" cellpadding=\"6\" "
        "style=\"background-color:%1; margin-top:6px; margin-bottom:10px;\">"
        "<tr>"
    ).arg(t.tableBorder);
    for (const QString& h : headers) {
        html += QString(
            "<th align=\"left\" style=\"background-color:%1; color:%2; font-weight:600;\">%3</th>"
        ).arg(t.tableHdrBg, t.tableHdrText, h);
    }
    html += QStringLiteral("</tr>");
    for (size_t r = 0; r < rows.size(); ++r) {
        const QString bg = (r % 2 == 0) ? t.tableCellBg : t.tableAltBg;
        html += QStringLiteral("<tr>");
        for (const QString& cell : rows[r]) {
            html += QString(
                "<td valign=\"top\" style=\"background-color:%1; color:%2;\">%3</td>"
            ).arg(bg, t.bodyText, cell);
        }
        html += QStringLiteral("</tr>");
    }
    html += QStringLiteral("</table>");
    return html;
}

} // namespace

SettingsFaqWidget::SettingsFaqWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsFaqWidget)
{
    ui->setupUi(this);

    ui->verticalLayoutcont->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayoutcont->setSpacing(10);
    ui->container->setStyleSheet("#container { background: transparent; border: none; }");
    setCssProperty(ui->labelTitle, "text-title-faq");
    setCssProperty(ui->labelWebLink, "text-faq-weblink");
    setCssProperty(ui->pushButtonExit, "btn-faq-exit");
    setCssProperty(ui->pushButtonWebLink, "btn-faq-web");
    setCssProperty(ui->containerButtons, "container-faq-buttons");
    ui->pushButtonExit->setVisible(false);

    // Hide legacy 6-item .ui widgets while keeping layout container slots intact
    if (ui->groupBox) ui->groupBox->setVisible(false);
    for (QWidget* legacyCard : {
             ui->widget_Intro,
             ui->widget_UnspendablePIV,
             ui->widget_Stake,
             ui->widget_Support,
             ui->widget_Masternode,
             ui->widget_MNController}) {
        if (legacyCard) legacyCard->setVisible(false);
    }

    // Balance left TOC sidebar vs wider readable guide area on the right
    if (ui->left) {
        ui->left->setMinimumWidth(275);
        ui->left->setMaximumWidth(325);
    }
    if (ui->horizontalLayout_6) {
        ui->horizontalLayout_6->setStretch(0, 2);
        ui->horizontalLayout_6->setStretch(2, 5);
    }
    if (ui->containerButtons) {
        ui->containerButtons->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    }
    if (ui->verticalSpacer) {
        ui->verticalSpacer->changeSize(0, 8, QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    if (ui->verticalLayout_2) {
        ui->verticalLayout_2->setStretch(0, 1);
    }

    ui->labelTitle->setText(tr("Help & Frequently Asked Questions"));
    ui->labelWebLink->setText(tr("More:"));
    if (ui->horizontalSpacer_4) {
        ui->horizontalSpacer_4->changeSize(4, 20, QSizePolicy::Fixed, QSizePolicy::Minimum);
    }
    ui->pushButtonWebLink->setText(QStringLiteral("krista.kristalteknoloji.com"));

    // Instant Search bar in header
    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName(QStringLiteral("faqSearchEdit"));
    searchEdit->setPlaceholderText(tr("Search help (e.g. send, backup, locked, earn)..."));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(260);
    searchEdit->setMaximumWidth(390);
    searchEdit->setFixedHeight(36);
    ui->horizontalLayout_3->addWidget(searchEdit);
    connect(searchEdit, &QLineEdit::textChanged, this, &SettingsFaqWidget::onSearchTextChanged);

    // Subtitle row directly below header
    labelSubtitle = new QLabel(
        tr("Simple, everyday answers to help you use your wallet safely — with optional technical details whenever you want to dig deeper."),
        ui->container);
    labelSubtitle->setWordWrap(true);
    labelSubtitle->setContentsMargins(10, 0, 10, 0);
    ui->verticalLayoutcont->insertWidget(1, labelSubtitle);

    // Category filter bar directly above 2-column content area
    filterBarWidget = new QWidget(ui->container);
    filterBarWidget->setObjectName(QStringLiteral("faqFilterBar"));
    QHBoxLayout* filterLay = new QHBoxLayout(filterBarWidget);
    filterLay->setContentsMargins(10, 2, 10, 4);
    filterLay->setSpacing(6);

    QButtonGroup* filterBtnGroup = new QButtonGroup(this);
    filterBtnGroup->setExclusive(true);

    const std::vector<std::pair<QString, QString>> filters = {
        {QStringLiteral("all"),      tr("All (14)")},
        {QStringLiteral("basics"),   tr("Basics")},
        {QStringLiteral("safety"),   tr("Safety && Backup")},
        {QStringLiteral("earning"),  tr("Earning KRISTA")},
        {QStringLiteral("advanced"), tr("Advanced")}
    };
    for (size_t i = 0; i < filters.size(); ++i) {
        const QString& key = filters[i].first;
        const QString& label = filters[i].second;
        QPushButton* pill = new QPushButton(label, filterBarWidget);
        pill->setObjectName(QString("faqFilter_%1").arg(key));
        pill->setCheckable(true);
        pill->setChecked(i == 0);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setFocusPolicy(Qt::NoFocus);
        pill->setFixedHeight(30);
        filterBtnGroup->addButton(pill);
        filterLay->addWidget(pill);
        categoryFilterButtons.emplace_back(key, pill);
        connect(pill, &QPushButton::clicked, this, [this, key]() {
            onCategoryFilterClicked(key);
        });
    }
    filterLay->addStretch(1);

    labelResultCount = new QLabel(filterBarWidget);
    labelResultCount->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    filterLay->addWidget(labelResultCount);

    btnToggleAll = new QPushButton(tr("Expand All"), filterBarWidget);
    btnToggleAll->setObjectName(QStringLiteral("faqToggleAllBtn"));
    btnToggleAll->setCursor(Qt::PointingHandCursor);
    btnToggleAll->setFocusPolicy(Qt::TabFocus);
    btnToggleAll->setFixedHeight(30);
    filterLay->addWidget(btnToggleAll);
    connect(btnToggleAll, &QPushButton::clicked, this, &SettingsFaqWidget::onToggleAllClicked);

    ui->verticalLayoutcont->insertWidget(2, filterBarWidget);

    // Scrollable categorized TOC inside left containerButtons card
    tocScrollArea = new QScrollArea(ui->containerButtons);
    tocScrollArea->setObjectName(QStringLiteral("faqTocScroll"));
    tocScrollArea->setFrameShape(QFrame::NoFrame);
    tocScrollArea->setWidgetResizable(true);
    tocScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tocScrollArea->viewport()->setAutoFillBackground(false);
    tocScrollArea->setStyleSheet("QScrollArea#faqTocScroll { background: transparent; border: none; }");

    tocContentWidget = new QWidget(tocScrollArea);
    tocContentWidget->setObjectName(QStringLiteral("faqTocContent"));
    tocContentWidget->setStyleSheet("#faqTocContent { background: transparent; }");
    QVBoxLayout* tocLay = new QVBoxLayout(tocContentWidget);
    tocLay->setContentsMargins(2, 2, 2, 6);
    tocLay->setSpacing(3);
    tocScrollArea->setWidget(tocContentWidget);
    if (ui->verticalLayout) {
        ui->verticalLayout->addWidget(tocScrollArea);
    }

    // Card area spacing & Popular Questions bar
    ui->verticalLayout_7->setSpacing(12);
    ui->verticalLayout_7->setContentsMargins(4, 2, 10, 16);

    popularBarWidget = new QFrame(ui->scrollAreaWidgetContents);
    popularBarWidget->setObjectName(QStringLiteral("faqPopularBar"));
    QVBoxLayout* popOuterLay = new QVBoxLayout(popularBarWidget);
    popOuterLay->setContentsMargins(14, 12, 14, 12);
    popOuterLay->setSpacing(8);

    popularTitleLabel = new QLabel(tr("Popular questions:"), popularBarWidget);
    popOuterLay->addWidget(popularTitleLabel);

    QGridLayout* popGrid = new QGridLayout();
    popGrid->setContentsMargins(0, 0, 0, 0);
    popGrid->setHorizontalSpacing(8);
    popGrid->setVerticalSpacing(8);

    const std::vector<std::pair<QString, int>> popularShortcuts = {
        {tr("Why is my balance pending or locked?"), 3},
        {tr("How do I back up my wallet?"), 4},
        {tr("Can I earn with 0 KRISTA?"), 6},
        {tr("My wallet looks stuck"), 5}
    };
    for (size_t i = 0; i < popularShortcuts.size(); ++i) {
        const QString& label = popularShortcuts[i].first;
        const int targetNum = popularShortcuts[i].second;
        QPushButton* popBtn = new QPushButton(label, popularBarWidget);
        popBtn->setCursor(Qt::PointingHandCursor);
        popBtn->setFocusPolicy(Qt::TabFocus);
        popBtn->setMinimumHeight(32);
        popBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        popGrid->addWidget(popBtn, static_cast<int>(i / 2), static_cast<int>(i % 2));
        popularButtons.push_back(popBtn);
        connect(popBtn, &QPushButton::clicked, this, [this, targetNum]() {
            jumpToGuide(targetNum);
        });
    }
    popOuterLay->addLayout(popGrid);
    ui->verticalLayout_7->addWidget(popularBarWidget);

    buildFaqContent();

    labelNoResults = new QLabel(
        tr("No matching FAQ guides found. Try another keyword or switch category filter to All (14)."),
        ui->scrollAreaWidgetContents);
    labelNoResults->setWordWrap(true);
    labelNoResults->setAlignment(Qt::AlignCenter);
    labelNoResults->setVisible(false);
    ui->verticalLayout_7->addWidget(labelNoResults);
    ui->verticalLayout_7->addStretch(1);

    if (ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
        connect(ui->scrollAreaFaq->verticalScrollBar(), &QScrollBar::valueChanged,
                this, &SettingsFaqWidget::onFaqScrolled);
    }

    connect(ui->pushButtonWebLink, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://krista.kristalteknoloji.com")));
    });

    applyThemeStyles(isLightTheme());
    applyFilters();
    updateToggleAllLabel();
}

SettingsWidget* SettingsFaqWidget::findSettingsWidget() const
{
    for (QWidget* p = parentWidget(); p; p = p->parentWidget()) {
        if (auto* sw = qobject_cast<SettingsWidget*>(p)) {
            return sw;
        }
    }
    return nullptr;
}

void SettingsFaqWidget::buildFaqContent()
{
    const Consensus::Params& cp = Params().GetConsensus();
    const QLocale loc;

    const QString verStr = QString("%1.%2.%3.%4")
        .arg(CLIENT_VERSION_MAJOR).arg(CLIENT_VERSION_MINOR)
        .arg(CLIENT_VERSION_REVISION).arg(CLIENT_VERSION_BUILD);
    const QString netName = QString::fromStdString(Params().NetworkIDString());
    const int p2pPort = Params().GetDefaultPort();
    const QString hrp = QString::fromStdString(Params().Bech32HRP());
    const qint64 maxSupply = cp.nMaxMoneyOut / COIN;
    const qint64 targetSec = cp.nTargetSpacing;
    const int coinbaseMat = cp.nCoinbaseMaturity;
    const int coinbaseMins = static_cast<int>((coinbaseMat * targetSec) / 60);
    const int stakeConfs = cp.nStakeMinDepthV2;
    const int stakeAgeMins = cp.nStakeMinAge / 60;
    const int modelDHeight = cp.vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight;
    const int openEntryHeight = cp.nAdamOpenEntryHeight;
    const int burnDecay = cp.nBurnDecayBlocks;
    const int mnUptimeLimit = cp.nMasternodeUptimeLimit;
    const QString devAddr = QString::fromStdString(Params().DeveloperFundAddress());

    struct GroupSpec {
        QString key;
        QString header;
    };
    const std::vector<GroupSpec> groups = {
        {QStringLiteral("basics"),   tr("BASICS")},
        {QStringLiteral("safety"),   tr("SAFETY & BACKUP")},
        {QStringLiteral("earning"),  tr("EARNING KRISTA")},
        {QStringLiteral("advanced"), tr("ADVANCED & TOOLS")}
    };

    struct GuideSeed {
        int num;
        QString catKey;
        QString catLabel;
        QString tocTitle;
        QString title;
        QString tldr;
        QString keywords;
        std::function<QString(bool)> bodyBuilder;
        std::function<QString(bool)> techBuilder;
        QString actionLabel;
        std::function<void()> onAction;
    };

    std::vector<GuideSeed> seeds;
    seeds.reserve(14);

    // 1 - Basics: What is KRISTA?
    seeds.push_back({
        1, QStringLiteral("basics"), tr("Basics"),
        tr("What is KRISTA?"),
        tr("What is KRISTA and how does it work?"),
        tr("KRISTA is a fast, everyday digital currency where blocks confirm in about 30 seconds and anyone — even starting with 0 coins — can participate using a regular computer."),
        QStringLiteral("intro hybrid adam mpa pos llmq mescal taproot bip324 supply network port overview version"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">Most blockchains require expensive industrial hardware or large upfront holdings before you can take part. <b>KRISTA</b> is designed to stay approachable and fair:</p>"
                "<ul>"
                "<li><b>Fast Confirmations:</b> Transfers typically appear and confirm within roughly <b>%1 seconds</b> with tiny fees (a tiny fraction of 1 KRISTA).</li>"
                "<li><b>Open to Everyone:</b> You can start mining right inside this wallet with <b>0 KRISTA</b>, earn passively by holding coins (Staking), or run a 24/7 Masternode.</li>"
                "<li><b>Strong Security &amp; Built-in Vaults:</b> Network traffic between wallets is encrypted automatically, and built-in smart contracts let you create time-locked savings or shared multi-person vaults without third-party apps.</li>"
                "</ul>"
            ).arg(targetSec);
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString specsTable = buildStyledTable(t,
                {tr("Protocol / Network Property"), tr("Value (%1)").arg(netName)},
                {
                    {tr("Client &amp; Protocol"), QString("<code>v%1</code> &bull; Protocol <code>%2</code>").arg(verStr).arg(PROTOCOL_VERSION)},
                    {tr("Consensus Stack"), tr("MPA / ADAM cooperative CPU/GPU puzzles + Proof of Stake (PoS) + BLS Masternode LLMQ Finality + MESCAL v2 Taproot")},
                    {tr("Block Target Time"), tr("<b>%1 seconds</b> (15-second TimeProtocolV2 slots)").arg(targetSec)},
                    {tr("Maximum Total Supply"), tr("<b>%1 KRISTA</b> hard cap").arg(loc.toString(maxSupply))},
                    {tr("Initial Block Subsidy"), tr("<b>17 KRISTA</b> (reduces smoothly by <b>2.15%</b> every 259,200 blocks / ~90 days)")},
                    {tr("P2P Transport &amp; Port"), QString("<code>BIP324</code> v2 encrypted transport enabled by default &bull; TCP <code>%1</code>").arg(p2pPort)}
                });
            return wrapFaqHtml(t, specsTable);
        },
        QString(), nullptr
    });

    // 2 - Basics: Sending & Receiving
    seeds.push_back({
        2, QStringLiteral("basics"), tr("Basics"),
        tr("Sending & Receiving"),
        tr("How do I send and receive KRISTA?"),
        tr("Use standard addresses starting with KT... for everyday transfers, exchanges, mining, and staking. Transfers confirm in about 30 seconds with minimal fees."),
        QStringLiteral("address receive send fee p2pkh p2sh bech32 taproot kt kt1p address book"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p><b>To receive KRISTA:</b></p>"
                "<ol>"
                "<li>Open the <b>RECEIVE</b> tab at the top and click <b>Generate / Copy Address</b>.</li>"
                "<li>Double-check that the copied address matches the screen before sending it to someone or pasting it on an exchange.</li>"
                "</ol>"
                "<p><b>To send KRISTA safely:</b></p>"
                "<ol>"
                "<li>Open <b>SEND</b>, paste the recipient's <code>KT...</code> address (or pick a saved contact from <b>ADDRESS BOOK</b> to prevent typos).</li>"
                "<li>Enter the amount, review the confirmation popup, and submit — transfers usually settle in about <b>%1 seconds</b>.</li>"
                "</ol>"
            ).arg(targetSec);
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tbl = buildStyledTable(t,
                {tr("Address Prefix"), tr("Script Type"), tr("Intended Use")},
                {
                    {QStringLiteral("<code>KT...</code> / <code>k...</code>"), tr("Standard P2PKH"),
                     tr("Everyday transfers, exchanges, PoS staking, Miner Hub seats, and Masternode collateral/payouts.")},
                    {QStringLiteral("<code>7...</code> / <code>S...</code>"), tr("Script Hash P2SH"),
                     tr("Legacy multi-signature and script-wrapped outputs.")},
                    {QString("<code>%1" "1p...</code>").arg(hrp), tr("MESCAL v2 Taproot (Bech32m)"),
                     tr("Compact on-chain Time-Locks, M-of-N Multi-Sig Vaults, Hash-Lock swaps, and programmable rules.")}
                });
            return wrapFaqHtml(t, tbl);
        },
        tr("Open Receive Tab \xE2\x86\x92"),
        [this]() { if (window) window->goToReceive(); }
    });

    // 3 - Basics: Locked & Pending Balances
    seeds.push_back({
        3, QStringLiteral("basics"), tr("Basics"),
        tr("Locked & Pending Coins"),
        tr("Why are some of my coins Locked, Immature, or Pending?"),
        tr("Your coins never leave your wallet when locked or pending — they are simply waiting for block confirmations or a lock timer you chose, and unlock automatically."),
        QStringLiteral("locked pending unspendable immature 100 confirmations 4200 250 10 cltv collateral topbar timer"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString intro = tr(
                "<p align=\"justify\">If your top bar shows <b>Pending</b> or <b>Locked</b>, nothing is lost and nothing can leave your wallet without your confirmation. Here is what each label means:</p>"
            );
            const QString tbl = buildStyledTable(t,
                {tr("Wallet Status"), tr("Typical Wait Time"), tr("What Happens")},
                {
                    {tr("<b>Pending Transfer</b>"), tr("<b>~%1 seconds</b> (1 block)").arg(targetSec),
                     tr("An incoming transfer is waiting for the next block and becomes spendable right after.")},
                    {tr("<b>Immature Reward</b>"), tr("<b>~%1 minutes</b> (%2 blocks)").arg(coinbaseMins).arg(coinbaseMat),
                     tr("Freshly mined or staked rewards have a built-in cooling period before they can be spent or locked.")},
                    {tr("<b>Miner Seat Lock</b> (10 or 250 KRISTA)"), tr("<b>~24 hours</b>"),
                     tr("Coins locked for a Miner Hub seat remain inside your wallet and unlock automatically when the 24-hour timer ends.")},
                    {tr("<b>Masternode Collateral</b> (4,200 KRISTA)"), tr("While Masternode is running"),
                     tr("Safely held in your wallet and protected from accidental spending; unlock anytime if you retire the Masternode.")}
                });
            return wrapFaqHtml(t, intro + tbl);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>Protocol Mechanics:</b></p>"
                "<ul>"
                "<li><b>Hover Breakdown:</b> Hover your mouse over the TopBar <b>Locked / Pending</b> badge to see exact amounts per category.</li>"
                "<li><b>On-Chain <code>CLTV</code>:</b> Coin-Lock (<code>250 KRISTA</code>) and PoW-Lock (<code>10 KRISTA</code>) seats stay locked for <code>2,880 blocks</code> (+ a 20-block wallet auto-renewal buffer) via <code>OP_CHECKLOCKTIMEVERIFY</code> to keys you control.</li>"
                "<li><b>Proof of Lock (PoL):</b> Staking UTXOs with CLTV locks preserve both principal and lock script upon winning stakes until the chosen unlock block/date passes.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, tech);
        },
        QString(), nullptr
    });

    // 4 - Safety & Backup: Backup & Protect
    seeds.push_back({
        4, QStringLiteral("safety"), tr("Safety & Backup"),
        tr("Backups & Security"),
        tr("How do I back up and protect my KRISTA wallet?"),
        tr("Set a strong wallet passphrase first, then save copies of your wallet.dat backup on two separate USB drives or offline locations you control."),
        QStringLiteral("backup passphrase encrypt wallet.dat bip38 sign verify message csv history security private key usb restore"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p><b>4 simple habits that keep your funds safe:</b></p>"
                "<ol>"
                "<li><b>Add a Passphrase:</b> Open <b>Settings &rarr; Backup &amp; Passphrase</b> and encrypt your wallet with a strong password you will never forget (there is no &ldquo;forgot password&rdquo; reset).</li>"
                "<li><b>Save Two Offline Backups:</b> Click <b>Backup Wallet</b> and store your <code>wallet.dat</code> backup on two separate USB drives in safe places. Always take a fresh backup after creating new addresses or Masternodes.</li>"
                "<li><b>Keep Keys Strictly Private:</b> Nobody from support will ever ask for your <code>wallet.dat</code>, private keys, or passphrase.</li>"
                "<li><b>Daily Staking Protection:</b> Use the TopBar lock icon &rarr; <b>Unlock for Staking</b> so your wallet can earn rewards while still requiring your full passphrase before sending coins.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>Additional Security Tools in Settings:</b></p>"
                "<ul>"
                "<li><b>BIP38 Cold Paper Keys:</b> Use <b>Settings &rarr; BIP38 Key Tool</b> to password-encrypt individual private keys for paper cold storage.</li>"
                "<li><b>Sign / Verify Message:</b> Prove ownership of a <code>KT...</code> address off-chain without revealing keys.</li>"
                "<li><b>Export History (CSV):</b> Export clean accounting logs via <b>Settings &rarr; Export History</b>.</li>"
                "<li><b>Encrypted P2P Transport:</b> Peer-to-peer wallet connections use <code>BIP324</code> encrypted v2 transport by default.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, tech);
        },
        tr("Open Backup & Passphrase \xE2\x86\x92"),
        [this]() {
            if (auto* sw = findSettingsWidget()) sw->showBackupWallet();
        }
    });

    // 5 - Safety & Backup: Troubleshooting & Repair
    seeds.push_back({
        5, QStringLiteral("safety"), tr("Safety & Backup"),
        tr("Quick Fixes & Wallet Repair"),
        tr("Wallet stuck, missing a transfer, or showing the wrong balance — how do I fix it?"),
        tr("Check your computer's date & time, make sure the top bar says Synced, back up wallet.dat first, and use the 1-click tools in Settings → Wallet Repair."),
        QStringLiteral("troubleshoot fix stuck sync missing balance rescan recover salvage rebuild index clock port wallet repair peers"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString note = tr(
                "<p align=\"justify\"><b>Before running any repair:</b> make sure you have a recent backup of <code>wallet.dat</code>. Most sync or display glitches take under a minute to fix:</p>"
            );
            const QString tbl = buildStyledTable(t,
                {tr("What You See"), tr("Easy Fix")},
                {
                    {tr("<b>Wallet Not Syncing / Few Connections</b>"),
                     tr("Check that your computer clock &amp; timezone are set to automatic internet time (NTP), then wait 1–2 minutes for peers to connect.")},
                    {tr("<b>Sent or Received Coins Missing</b>"),
                     tr("Open <b>Settings &rarr; Wallet Repair</b> and click <b>Rescan blockchain files</b> (if an unconfirmed send looks stuck, try <b>Recover transactions 1</b>).")},
                    {tr("<b>Wallet Error After Power Outage</b>"),
                     tr("In <b>Settings &rarr; Wallet Repair</b>, click <b>Rebuild index</b> or <b>Salvage wallet</b> to cleanly repair local files.")},
                    {tr("<b>Mining or Staking Not Starting</b>"),
                     tr("Wait until the TopBar shows <code>✓ Synced</code>, click the TopBar lock icon to unlock your wallet, and confirm your coins have enough block confirmations.")}
                });
            return wrapFaqHtml(t, note + tbl);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>Diagnostic Thresholds &amp; Internals:</b></p>"
                "<ul>"
                "<li><b>Clock Tolerance:</b> Consensus uses <code>TimeProtocolV2</code> 15-second slots — large local clock drift causes peers to reject connections/blocks.</li>"
                "<li><b>Network Port:</b> Ensure outbound (and optional inbound) TCP port <code>%1</code> is reachable; check live peers under <b>Settings &rarr; Node Information</b>.</li>"
                "<li><b>Maturity Requirements:</b> Miner Hub funded locks need <b>%2+ confirmations</b>; PoS staking requires <b>%3+ confirmations</b> and <b>%4 min</b> coin age.</li>"
                "<li><b>CLI Equivalents:</b> <code>-rescan</code> scans blocks for missing wallet txos; <code>-reindex</code> rebuilds <code>blocks/</code>, <code>chainstate/</code>, and <code>evodb/</code>.</li>"
                "</ul>"
            ).arg(p2pPort).arg(coinbaseMat).arg(loc.toString(stakeConfs)).arg(stakeAgeMins);
            return wrapFaqHtml(t, tech);
        },
        tr("Open Wallet Repair \xE2\x86\x92"),
        [this]() {
            if (auto* sw = findSettingsWidget()) sw->showWalletRepair();
        }
    });

    // 6 - Earning KRISTA: Miner Hub Guide
    seeds.push_back({
        6, QStringLiteral("earning"), tr("Earning KRISTA"),
        tr("Earn in Miner Hub (From 0)"),
        tr("How can I earn KRISTA? (Even starting with 0 coins)"),
        tr("Open the MINER HUB tab, choose the Zero-Coin tier (0 KRISTA needed) or a locked tier, register once, and click Start 1-Click Mining."),
        QStringLiteral("miner hub zerocoin 0 krista pow-lock coin-lock open entry 250 10 track committee quota register earn mining"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString ways = tr(
                "<p><b>Three ways to earn KRISTA inside this wallet:</b></p>"
                "<ul>"
                "<li><b>Miner Hub (0, 10, or 250 KRISTA):</b> Earn on your laptop or desktop CPU with one click — no special hardware needed.</li>"
                "<li><b>Holding / Staking (Any Balance):</b> Keep mature coins unlocked for staking and earn automatically when your wallet mints a block.</li>"
                "<li><b>Masternode (4,200 KRISTA):</b> Lock collateral in your wallet and connect a small 24/7 server for full validator + quorum rewards.</li>"
                "</ul>"
                "<p><b>4 steps to start mining in Miner Hub right now:</b></p>"
                "<ol>"
                "<li>Wait until TopBar says <code>✓ Synced</code> and unlock your wallet if encrypted.</li>"
                "<li>Open <b>MINER HUB &rarr; Register Tracks</b> and pick <b>Zero-Coin (0 KRISTA)</b> (or 10 / 250 KRISTA once coins mature).</li>"
                "<li>Wait 1 block (~%1 seconds) for your wallet to show up in your active list.</li>"
                "<li>Click <b>Start 1-Click Mining</b> and leave the wallet open while participating.</li>"
                "</ol>"
            ).arg(targetSec);
            return wrapFaqHtml(t, ways);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tbl = buildStyledTable(t,
                {tr("Validator Track"), tr("Lock Required"), tr("Model D Committee Quota"), tr("Coordinator VRF Weight")},
                {
                    {tr("<b>Zero-Coin Open-Entry</b>"), QStringLiteral("<b>0 KRISTA</b><br/><small>15-bit HashX11KVS</small>"),
                     tr("<b>2 seats / block</b> (1 Pre-Model D; active from block %1; max 240 bootstrap slots)").arg(loc.toString(openEntryHeight)),
                     QStringLiteral("0")},
                    {tr("<b>Funded PoW-Lock</b>"), QStringLiteral("<b>10 KRISTA</b> (24h CLTV)<br/><small>+ HashX11KVS PoW</small>"),
                     tr("<b>3 seats / block</b> (up to 5 Open PoW incl. fallback; equal 4.00%/seat)"),
                     QStringLiteral("10")},
                    {tr("<b>Coin-Lock Validator</b>"), QStringLiteral("<b>250 KRISTA</b> (24h CLTV)"),
                     tr("<b>2 dedicated seats / block</b> (passive CLTV lock + puzzle; equal 4.00%/seat)"),
                     QStringLiteral("250")},
                    {tr("<b>KTDIP3 Masternode</b>"), QStringLiteral("<b>4,200 KRISTA</b> Collateral"),
                     tr("<b>8 seats / block</b> (+ 10% LLMQ + 5% passive MN queue)"),
                     QStringLiteral("4,200 (55% MN Floor)")}
                });
            const QString note = tr(
                "<p align=\"justify\"><small>Funded lock inputs require &ge; %1 confirmations. Each wallet holds at most 1 Coin-Lock and 1 PoW-Lock seat; funded locks renew cleanly inside the final 240 blocks (15 Testnet post-Model D), and Zero-Coin renews in the final 24 blocks (8 Testnet post-Model D).</small></p>"
            ).arg(coinbaseMat);
            return wrapFaqHtml(t, tbl + note);
        },
        tr("Open Miner Hub \xE2\x86\x92"),
        [this]() { if (window) window->goToMinerHub(); }
    });

    // 7 - Earning KRISTA: Staking
    seeds.push_back({
        7, QStringLiteral("earning"), tr("Earning KRISTA"),
        tr("Holding & Staking Rewards"),
        tr("How do Staking rewards work when I hold KRISTA?"),
        tr("Simply keep mature coins in an open, synced wallet unlocked for staking — your wallet stakes quietly in the background and earns block rewards plus transaction fees."),
        QStringLiteral("pos staking pol pom pob multiplier weight 600 confirmations unlock for staking proof of lock masternode burn hold earn"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\"><b>Proof of Stake (PoS)</b> lets coin holders earn rewards without heavy CPU usage — just keep the wallet open and connected:</p>"
                "<ol>"
                "<li>Hold spendable KRISTA in your wallet until the coins reach maturity (<b>%1 confirmations</b> on %2).</li>"
                "<li>If your wallet is encrypted, click the <b>Lock icon</b> in the TopBar and choose <b>Unlock for Staking</b> — your funds stay protected against spending.</li>"
                "<li>Check the TopBar status icon: once your coins mature and the chain is synced, staking activates automatically.</li>"
                "</ol>"
                "<p align=\"justify\">Optional time-locks (Proof of Lock), active Masternodes, and community Proof of Burn can further increase your staking odds.</p>"
            ).arg(loc.toString(stakeConfs), netName);
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString reqs = tr(
                "<p><b>Model D Staking Payout &amp; Multipliers:</b> Requires &ge; <b>%1 confirmations</b> and <b>%2 minutes</b> coin age. Winning stakes co-sign with the active Coordinator and earn <b>22% Staker Share + 100% of block transaction fees</b> under Model D (20% Pre-Model D).</p>"
            ).arg(loc.toString(stakeConfs)).arg(stakeAgeMins);
            const QString tbl = buildStyledTable(t,
                {tr("Multiplier"), tr("Max Boost"), tr("How It Works")},
                {
                    {tr("<b>PoL</b> (Proof of Lock)"), QStringLiteral("<b>Up to 3.0x</b>"),
                     tr("Time-lock staking UTXOs via CLTV to boost kernel weight. Winning stakes automatically preserve and renew both principal and CLTV script until lock expiry.")},
                    {tr("<b>PoM</b> (Proof of Masternode)"), QStringLiteral("<b>Up to 1.25x</b>"),
                     tr("Active KTDIP3 Masternode owners receive a smooth uptime loyalty bonus (<code>&alpha; = 0.25</code> over <code>%1</code> blocks).").arg(loc.toString(mnUptimeLimit))},
                    {tr("<b>PoB</b> (Proof of Burn)"), QStringLiteral("<b>Up to 6.0x</b>"),
                     tr("Voluntary burns sent to the burn address grant staking kernel weight that decays linearly over <code>%1</code> blocks.").arg(loc.toString(burnDecay))}
                });
            return wrapFaqHtml(t, reqs + tbl);
        },
        QString(), nullptr
    });

    // 8 - Earning KRISTA: Block Rewards
    seeds.push_back({
        8, QStringLiteral("earning"), tr("Earning KRISTA"),
        tr("How Block Rewards Are Shared"),
        tr("How are block rewards created and shared?"),
        tr("New blocks start at 17 KRISTA (slowly tapering over time). 7% funds ecosystem development, and the other 93% is shared fairly among miners, stakers, and masternodes."),
        QStringLiteral("reward emission subsidy model d 17 krista 7% 93% 5% 60% 25% 22% 10% burn dev treasury pos fee shared"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">Every ~%1 seconds, newly minted KRISTA rewards are divided automatically by the network so no single group takes everything:</p>"
                "<ul>"
                "<li><b>7% Ecosystem Development:</b> Supports ongoing core development and infrastructure.</li>"
                "<li><b>93% Community Consensus Rewards:</b> Split between active Miner Hub / Masternode puzzle solvers (<b>60%</b> equal per seat), the block Producer / Staker (<b>25%</b>), Masternode finality signers (<b>10%</b>), and passive Masternodes (<b>5%</b>).</li>"
                "<li><b>Unclaimed Slots Never Inflate Rewards:</b> If a seat goes unfilled, half is permanently burned out of supply and half goes to Ecosystem Development.</li>"
                "</ul>"
            ).arg(targetSec);
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString top = tr(
                "<p align=\"justify\">Period 0 (first 259,200 blocks, ~90 days) mints <b>17.00 KRISTA/block</b>: <b>7.0%</b> (<code>1.19 KRISTA</code>) goes to Dev Treasury (<code>%1</code>) and <b>93.0%</b> (<code>15.81 KRISTA</code>) forms the Net Consensus Pool under <b>Model D</b> (active from block <b>%2</b>; Pre-Model D splits <code>75%/11</code> per validator + <code>25%</code> Producer):</p>"
            ).arg(devAddr, loc.toString(modelDHeight));
            const QString tbl = buildStyledTable(t,
                {tr("Pool Share"), tr("% of Net Pool"), tr("Base KRISTA (Period 0)"), tr("Who Receives It")},
                {
                    {tr("<b>Passive Masternodes</b>"), QStringLiteral("<b>5%</b>"), QStringLiteral("0.7905 KRISTA"),
                     tr("Deterministic queue across active KTDIP3 Masternodes.")},
                    {tr("<b>Active MPA Validators</b> (15 seats)"), QStringLiteral("<b>60%</b> (4.00% / seat)"), QStringLiteral("0.6324 / seat"),
                     tr("Equal pay per solved seat: 8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Zero-Coin (9.4860 total).")},
                    {tr("<b>Producer / Staker</b>"), QStringLiteral("<b>25%</b>"), QStringLiteral("3.9525 KRISTA"),
                     tr("MPA Block Coordinator (PoW), or <b>22% Staker (3.4782) + 3% Coord (0.4743) + 100% tx fees</b> on PoS.")},
                    {tr("<b>BLS LLMQ Finality Quorum</b>"), QStringLiteral("<b>10%</b>"), QStringLiteral("1.5810 KRISTA"),
                     tr("Signer-only split (<code>10% / N</code>) paid to previous-block LLMQ signers (1-block lag).")}
                });
            return wrapFaqHtml(t, top + tbl);
        },
        QString(), nullptr
    });

    // 9 - Advanced: What is a Masternode?
    seeds.push_back({
        9, QStringLiteral("advanced"), tr("Advanced"),
        tr("What is a Masternode?"),
        tr("What is a Masternode and why run one?"),
        tr("A Masternode locks 4,200 KRISTA safely in your desktop wallet while an online server helps finalize blocks quickly and earns rewards across 4 streams."),
        QStringLiteral("masternode ktdip3 protx llmq quorum pose operator bls 4200 finality passive active cold"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">Think of a <b>Masternode</b> as a full-time reliability server backed by a refundable <b>4,200 KRISTA</b> deposit:</p>"
                "<ul>"
                "<li><b>Coins Stay in Your Desktop Wallet:</b> Your VPS server never holds spending access to your 4,200 KRISTA — you can unlock and spend them anytime you retire the node.</li>"
                "<li><b>Prevents Rollbacks:</b> Masternode groups sign each block together so confirmed transfers lock in permanently.</li>"
                "<li><b>Four Income Streams:</b> Active Masternodes earn from the passive rotation queue, 8 of 15 puzzle validator slots, finality quorum signing bonuses, and stronger Coordinator/Staking odds.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>KTDIP3 &amp; LLMQ Architecture:</b></p>"
                "<ul>"
                "<li><b>Deterministic On-Chain Registration:</b> <code>ProRegTx</code> metadata is stored in <code>evodb</code> with strict Hot/Cold BLS key separation (<code>blsPubKeyOperator</code> on VPS vs owner/payout keys in Cold Wallet).</li>"
                "<li><b>Dynamic BFT Quorums:</b> Quorums scale from <b>3-of-5</b> up to <b>8-of-15</b> signers with 1-block lag (10% signer-only LLMQ payout and <code>-5 / +5</code> deterministic PoSe health scoring).</li>"
                "<li><b>VRF Weight:</b> Holds <code>4,200</code> Coordinator VRF weight (55% active MN floor under Model D) plus PoM staking bonus (up to 1.25x).</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, tech);
        },
        tr("Open Masternodes \xE2\x86\x92"),
        [this]() { if (window) window->goToMasterNodes(); }
    });

    // 10 - Advanced: Cold Controller & VPS Setup
    seeds.push_back({
        10, QStringLiteral("advanced"), tr("Advanced"),
        tr("Masternode VPS Setup"),
        tr("How do I set up a Masternode on a server (VPS)?"),
        tr("First register your 4,200 KRISTA collateral in the MASTERNODES tab, then add your operator line to activemasternode.conf on your VPS server."),
        QStringLiteral("activemasternode.conf multi mn controller proregtx proupservtx vps hot cold bls_generate port 27999 setup"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p><b>Step 1 — In this Desktop Wallet (Cold Controller):</b></p>"
                "<ol>"
                "<li>Send or receive exactly <b>4,200 KRISTA</b> in a single payment and wait for <b>15 confirmations</b>.</li>"
                "<li>Open <b>MASTERNODES &rarr; + Create Masternode Controller</b>, enter your server's IP address and port (<code>%1</code>), and submit the setup wizard.</li>"
                "</ol>"
                "<p><b>Step 2 — On your 24/7 Server (Hot Node):</b></p>"
                "<ol>"
                "<li>Paste the generated operator key line into the server's <code>activemasternode.conf</code> file.</li>"
                "<li>Start the server daemon and watch live health/payout status back in your <b>MASTERNODES</b> tab.</li>"
                "</ol>"
            ).arg(p2pPort);
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>Single-IP Multi-MN Technical Rules:</b></p>"
                "<ol>"
                "<li>In <code>activemasternode.conf</code> (1-click accessible via TopBar <b>Config Files</b>), list one line per node:<br/>"
                "<code>mn1 &lt;mn_privkey&gt; &lt;bls_privkey_hex&gt;</code></li>"
                "<li>In <code>kristatech.conf</code> on the VPS, set <code>masternode=1</code> and <code>txindex=1</code>. <b>Never</b> put <code>masternodeprivkey=</code> inside <code>kristatech.conf</code>.</li>"
                "<li>If a node gets PoSe-banned after server downtime, revive it cleanly in 1 click using <code>ProUpServTx</code> in the <b>MASTERNODES</b> tab.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, tech);
        },
        tr("Open Masternodes \xE2\x86\x92"),
        [this]() { if (window) window->goToMasterNodes(); }
    });

    // 11 - Advanced: Smart Contracts
    seeds.push_back({
        11, QStringLiteral("advanced"), tr("Advanced"),
        tr("Smart Contracts (Vaults)"),
        tr("What are Smart Contracts (Time-Lock vaults & Multi-Sig)?"),
        tr("Create self-custodial savings lockboxes (locked until a date/block) or 2-of-3 shared vaults right inside the SMART CONTRACTS tab — without trusting third parties."),
        QStringLiteral("mescal smart contracts taproot kt1p multisig time-lock hash-lock htlc yaml vault escrow"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">You don't need programming knowledge to use built-in templates under <b>SMART CONTRACTS</b>:</p>"
                "<ul>"
                "<li><b>Time-Lock Vault:</b> Lock savings until a chosen future date or block height so they cannot be spent early.</li>"
                "<li><b>Shared M-of-N Multi-Sig Vault:</b> Require multiple approvals (for example, 2 of 3 trusted devices/partners) before funds move.</li>"
                "<li><b>Conditional Hash-Lock Swaps:</b> Safely release funds only when a secret phrase is revealed.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>MESCAL v2 Taproot Internals:</b></p>"
                "<ul>"
                "<li>Contracts compile deterministically to compact Bech32m <b>Taproot</b> scripts (<code>%1" "1p...</code>) with predictable byte-sized fees and no Turing-complete reentrancy surface.</li>"
                "<li>Advanced users can switch to the YAML Composer inside <b>SMART CONTRACTS</b>, preview byte size &amp; fee estimates in <b>Contract Summary</b>, and save/share definitions with <b>Export Spec &#x25BE;</b>.</li>"
                "</ul>"
            ).arg(hrp);
            return wrapFaqHtml(t, tech);
        },
        tr("Open Smart Contracts \xE2\x86\x92"),
        [this]() { if (window) window->goToSmartContract(); }
    });

    // 12 - Advanced: Fairness & Anti-Sybil
    seeds.push_back({
        12, QStringLiteral("advanced"), tr("Advanced"),
        tr("Fairness & Anti-Spam"),
        tr("Why can't someone cheat rewards by opening 100 wallets?"),
        tr("Separate track quotas, confirmation delays, and 1-slot-per-wallet rules prevent splitting coins or spamming free wallets from stealing rewards."),
        QStringLiteral("sybil fairness attack equal pay anti-sybil quota 8 2 3 2 7 1 2 1 withholding burn vrf 4200 250 10"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">KRISTA uses built-in fairness guardrails so splitting one large balance into 50 wallets or launching 100 empty wallets won't let anyone take over block rewards:</p>"
                "<ul>"
                "<li><b>Dedicated Reward Buckets:</b> Free 0-KRISTA miners, 10/250 KRISTA validators, and Masternodes each compete only within their own fixed number of seats per block.</li>"
                "<li><b>Anti-Recycling Cooldown:</b> Newly moved coins must wait for confirmations before registering a funded seat, stopping fast same-block coin hopping.</li>"
                "<li><b>No Withholding Advantage:</b> If someone refuses to turn in their assigned work on time, honest users still finalize the block and the skipped share is burned rather than handed to attackers.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>Five Consensus Defense Layers:</b></p>"
                "<ul>"
                "<li><b>Strict Track Isolation (8 / 2 / 3 / 2 Model D; 7 / 1 / 2 / 1 Pre-Model D):</b> Zero-Coin PoW competes strictly for its two Open-Entry seats.</li>"
                "<li><b>Maturity &amp; Burst Caps:</b> Funded lock inputs require <b>%1+ confirmations</b>, 1 active seat per track per wallet, and max 1 Coin-Lock + 1 Funded PoW-Lock + 1 Zero-Coin registration per block (<code>0x01:0</code> sentinel).</li>"
                "<li><b>Proportional Coordinator VRF (4,200 : 250 : 10, 55% Model D / 75% Pre-Model D MN Floor):</b> Coordinator election scales linearly with locked capital; Zero-Coin holds <code>0</code> Coordinator weight.</li>"
                "<li><b>BFT LLMQ Finality + 50/50 Withholding Burn:</b> Finality signatures require active Masternodes, and unsolved validator shares split <b>50% Burn / 50% Dev Treasury</b>.</li>"
                "</ul>"
            ).arg(coinbaseMat);
            return wrapFaqHtml(t, tech);
        },
        QString(), nullptr
    });

    // 13 - Advanced: External CPU/GPU Miners
    seeds.push_back({
        13, QStringLiteral("advanced"), tr("Advanced"),
        tr("External CPU / GPU Miners"),
        tr("How do I connect external mining software (cpuminer / ccminer)?"),
        tr("For advanced multi-rig setups, enable local RPC in kristatech.conf, keep your registered wallet synced & unlocked, and point cpuminer-opt or ccminer at your local RPC port."),
        QStringLiteral("external miner cpuminer ccminer gpu cpu getblocktemplate submitblock ext miner rpcuser rpcpassword"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">For most users, <b>Start 1-Click Mining</b> in the Miner Hub is all you need. If you run multiple rigs or prefer external CPU/GPU miners like <code>cpuminer-opt</code> or <code>ccminer</code>, your node acts as their local coordinator:</p>"
                "<ol>"
                "<li>Add local RPC credentials in <code>kristatech.conf</code> and restart the wallet.</li>"
                "<li>Register a Miner Hub seat (or Masternode) and keep the wallet synced &amp; unlocked.</li>"
                "<li>Point your miner software at your local RPC address using your registered miner address — <b>MINER HUB</b> will display the <b>&bull; EXT MINER</b> badge automatically.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, body);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tech = tr(
                "<p><b>RPC Setup &amp; Protocol Flow:</b></p>"
                "<ol>"
                "<li>Open TopBar <b>Config Files &rarr; kristatech.conf</b> and configure:<br/>"
                "<code>server=1</code> &bull; <code>rpcuser=youruser</code> &bull; <code>rpcpassword=strong_pass</code> &bull; <code>rpcallowip=127.0.0.1</code></li>"
                "<li>External miners use <code>getblocktemplate</code> and <code>submitblock</code> with <code>--coinbase-addr</code> / <code>mineraddress</code> matching your registered validator address.</li>"
                "<li>Keep the wallet <b>unlocked</b> so the node can BLS-sign external <code>puzzleheader</code> solutions before broadcasting <code>ADAMSOL</code> across the P2P network.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, tech);
        },
        QString(), nullptr
    });

    // 14 - Advanced: Explorer, Official Links & Diagnostics
    seeds.push_back({
        14, QStringLiteral("advanced"), tr("Advanced"),
        tr("Explorer & Debug Console"),
        tr("Official Website, Block Explorer & Diagnostic Tools"),
        tr("Verify blocks/transactions online at explorer.kristalteknoloji.com or inspect real-time network status locally in Settings → RPC Debug Console."),
        QStringLiteral("explorer website link support docs rpc console getinfo getmininginfo getadamminers protx listmasternodes getactivequorum"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString links = tr(
                "<p>"
                "<b>Official Website &amp; Documentation:</b> <a style=\"color:%1; font-weight:600;\" href=\"https://krista.kristalteknoloji.com\">https://krista.kristalteknoloji.com</a><br/>"
                "<b>Live Block &amp; Consensus Explorer:</b> <a style=\"color:%2; font-weight:600;\" href=\"https://explorer.kristalteknoloji.com\">https://explorer.kristalteknoloji.com</a>"
                "</p>"
                "<p align=\"justify\">Need deeper diagnostics? Open <b>Settings &rarr; RPC Debug Console</b> (1-click quick chips are built right in) to check sync height, peers, mining status, or active Masternodes.</p>"
            ).arg(t.accentBlue, t.accentCoral);
            return wrapFaqHtml(t, links);
        },
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tbl = buildStyledTable(t,
                {tr("RPC Command"), tr("What It Inspects")},
                {
                    {QStringLiteral("<code>getinfo</code>"), tr("Block height, connections, supply, wallet balance, and sync health.")},
                    {QStringLiteral("<code>getmininginfo</code> / <code>getstakingstatus</code>"), tr("Active difficulty, ADAM solver status, and PoS staking readiness.")},
                    {QStringLiteral("<code>getadamminers</code>"), tr("Live validator pool across Masternode, Coin-Lock, PoW-Lock, and Zero-Coin seats.")},
                    {QStringLiteral("<code>protx list valid</code> / <code>listmasternodes</code>"), tr("Registered KTDIP3 Masternodes, PoSe penalty scores, and payout queue.")},
                    {QStringLiteral("<code>getactivequorum</code>"), tr("Current rotating BLS LLMQ quorum members and signature threshold.")}
                });
            return wrapFaqHtml(t, tbl);
        },
        tr("Open RPC Console \xE2\x86\x92"),
        [this]() { if (window) window->goToDebugConsole(); }
    });

    QVBoxLayout* tocLay = qobject_cast<QVBoxLayout*>(tocContentWidget->layout());
    QButtonGroup* tocBtnGroup = new QButtonGroup(this);
    tocBtnGroup->setExclusive(true);

    QString lastGroup;
    for (const auto& seed : seeds) {
        if (seed.catKey != lastGroup) {
            lastGroup = seed.catKey;
            QString hdrText = seed.catLabel.toUpper();
            for (const auto& g : groups) {
                if (g.key == seed.catKey) {
                    hdrText = g.header;
                    break;
                }
            }
            QLabel* grpLbl = new QLabel(hdrText, tocContentWidget);
            grpLbl->setContentsMargins(10, tocHeaders.empty() ? 4 : 10, 8, 2);
            tocLay->addWidget(grpLbl);
            TocGroupHeader hdr;
            hdr.categoryKey = seed.catKey;
            hdr.label = grpLbl;
            tocHeaders.push_back(hdr);
        }

        // TOC button
        QString tocBtnText = seed.tocTitle;
        tocBtnText.replace(QStringLiteral("&"), QStringLiteral("&&"));
        QPushButton* tocBtn = new QPushButton(tocBtnText, tocContentWidget);
        tocBtn->setObjectName(QString("faqTocBtn_%1").arg(seed.num));
        tocBtn->setCheckable(true);
        tocBtn->setChecked(seed.num == 1);
        tocBtn->setCursor(Qt::PointingHandCursor);
        tocBtn->setFocusPolicy(Qt::NoFocus);
        tocBtn->setMinimumHeight(36);
        tocBtn->setMaximumHeight(40);
        setCssProperty(tocBtn, "btn-faq-options");
        tocBtnGroup->addButton(tocBtn);
        tocLay->addWidget(tocBtn);

        // Card container
        QWidget* card = new QWidget(ui->scrollAreaWidgetContents);
        card->setObjectName(QString("faqCard_%1").arg(seed.num));
        QVBoxLayout* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(16, 14, 16, 14);
        cardLay->setSpacing(10);

        const bool defaultExpanded = (seed.num == 1);

        // Accordion clickable header row: Title + Category Tag + Chevron
        QPushButton* headerBtn = new QPushButton(card);
        headerBtn->setObjectName(QString("faqHeaderBtn_%1").arg(seed.num));
        headerBtn->setCursor(Qt::PointingHandCursor);
        headerBtn->setFocusPolicy(Qt::TabFocus);
        headerBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        QHBoxLayout* hdrLay = new QHBoxLayout(headerBtn);
        hdrLay->setContentsMargins(2, 2, 2, 2);
        hdrLay->setSpacing(10);

        QLabel* titleLbl = new QLabel(seed.title, headerBtn);
        titleLbl->setWordWrap(true);
        titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel* catTag = new QLabel(seed.catLabel, headerBtn);
        catTag->setAlignment(Qt::AlignCenter);
        catTag->setMinimumHeight(24);
        catTag->setMaximumHeight(24);
        catTag->setAttribute(Qt::WA_TransparentForMouseEvents);

        QLabel* chevronLbl = new QLabel(
            defaultExpanded ? QString::fromUtf8("\xE2\x96\xBE") : QString::fromUtf8("\xE2\x96\xB8"),
            headerBtn);
        chevronLbl->setAlignment(Qt::AlignCenter);
        chevronLbl->setFixedWidth(20);
        chevronLbl->setAttribute(Qt::WA_TransparentForMouseEvents);

        hdrLay->addWidget(titleLbl, 1, Qt::AlignVCenter);
        hdrLay->addWidget(catTag, 0, Qt::AlignVCenter);
        hdrLay->addWidget(chevronLbl, 0, Qt::AlignVCenter);
        cardLay->addWidget(headerBtn);

        // Always-visible Quick Answer strip
        QFrame* tldrBox = new QFrame(card);
        tldrBox->setObjectName(QString("faqTldr_%1").arg(seed.num));
        QVBoxLayout* tldrLay = new QVBoxLayout(tldrBox);
        tldrLay->setContentsMargins(12, 8, 12, 8);
        QLabel* tldrLbl = new QLabel(
            QString("<b>%1</b> %2").arg(tr("Quick Answer:"), seed.tldr), tldrBox);
        tldrLbl->setWordWrap(true);
        tldrLbl->setTextFormat(Qt::RichText);
        tldrLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        tldrLay->addWidget(tldrLbl);
        cardLay->addWidget(tldrBox);

        // Collapsible details container (friendly body + optional action button + optional technical drawer)
        QWidget* detailsCont = new QWidget(card);
        detailsCont->setObjectName(QString("faqDetails_%1").arg(seed.num));
        QVBoxLayout* detailsLay = new QVBoxLayout(detailsCont);
        detailsLay->setContentsMargins(0, 2, 0, 0);
        detailsLay->setSpacing(8);

        QLabel* bodyLbl = new QLabel(detailsCont);
        bodyLbl->setWordWrap(true);
        bodyLbl->setTextFormat(Qt::RichText);
        bodyLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bodyLbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        bodyLbl->setOpenExternalLinks(true);
        detailsLay->addWidget(bodyLbl);

        QPushButton* actionBtn = nullptr;
        QPushButton* techToggleBtn = nullptr;
        QFrame* techContainer = nullptr;
        QLabel* techLbl = nullptr;

        if (!seed.actionLabel.isEmpty() || seed.techBuilder) {
            QHBoxLayout* btnRow = new QHBoxLayout();
            btnRow->setContentsMargins(0, 2, 0, 0);
            btnRow->setSpacing(10);

            if (!seed.actionLabel.isEmpty()) {
                actionBtn = new QPushButton(seed.actionLabel, detailsCont);
                actionBtn->setObjectName(QString("faqActionBtn_%1").arg(seed.num));
                actionBtn->setCursor(Qt::PointingHandCursor);
                actionBtn->setFocusPolicy(Qt::TabFocus);
                actionBtn->setFixedHeight(32);
                btnRow->addWidget(actionBtn);
                auto cb = seed.onAction;
                connect(actionBtn, &QPushButton::clicked, this, [cb]() {
                    if (cb) cb();
                });
            }

            if (seed.techBuilder) {
                techToggleBtn = new QPushButton(
                    tr("Show technical details \xE2\x96\xBE"), detailsCont);
                techToggleBtn->setObjectName(QString("faqTechBtn_%1").arg(seed.num));
                techToggleBtn->setCheckable(true);
                techToggleBtn->setChecked(false);
                techToggleBtn->setCursor(Qt::PointingHandCursor);
                techToggleBtn->setFocusPolicy(Qt::TabFocus);
                techToggleBtn->setFixedHeight(32);
                btnRow->addWidget(techToggleBtn);
            }

            btnRow->addStretch(1);
            detailsLay->addLayout(btnRow);
        }

        if (seed.techBuilder) {
            techContainer = new QFrame(detailsCont);
            techContainer->setObjectName(QString("faqTechContainer_%1").arg(seed.num));
            QVBoxLayout* techLay = new QVBoxLayout(techContainer);
            techLay->setContentsMargins(12, 10, 12, 10);
            techLay->setSpacing(6);

            techLbl = new QLabel(techContainer);
            techLbl->setWordWrap(true);
            techLbl->setTextFormat(Qt::RichText);
            techLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            techLbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
            techLbl->setOpenExternalLinks(true);
            techLay->addWidget(techLbl);

            techContainer->setVisible(false);
            detailsLay->addWidget(techContainer);
        }

        detailsCont->setVisible(defaultExpanded);
        cardLay->addWidget(detailsCont);

        ui->verticalLayout_7->addWidget(card);

        FaqItemWidgets item;
        item.number = seed.num;
        item.categoryKey = seed.catKey;
        item.categoryLabel = seed.catLabel;
        item.tocTitle = seed.tocTitle;
        item.title = seed.title;
        item.tldr = seed.tldr;
        item.keywords = seed.keywords;
        item.card = card;
        item.headerBtn = headerBtn;
        item.titleLabel = titleLbl;
        item.catTagLabel = catTag;
        item.chevronLabel = chevronLbl;
        item.tldrBox = tldrBox;
        item.tldrLabel = tldrLbl;
        item.detailsContainer = detailsCont;
        item.bodyLabel = bodyLbl;
        item.actionBtn = actionBtn;
        item.techToggleBtn = techToggleBtn;
        item.techContainer = techContainer;
        item.techLabel = techLbl;
        item.tocButton = tocBtn;
        item.bodyBuilder = seed.bodyBuilder;
        item.techBuilder = seed.techBuilder;
        item.userExpanded = defaultExpanded;
        item.userTechExpanded = false;
        faqItems.push_back(item);

        const size_t idx = faqItems.size() - 1;
        connect(headerBtn, &QPushButton::clicked, this, [this, idx]() {
            if (idx < faqItems.size()) {
                FaqItemWidgets& it = faqItems[idx];
                const bool currentlyOpen = it.detailsContainer && !it.detailsContainer->isHidden();
                setCardExpanded(it, !currentlyOpen, true);
                if (!currentlyOpen && it.tocButton) {
                    it.tocButton->setChecked(true);
                }
                updateToggleAllLabel();
            }
        });
        if (techToggleBtn) {
            connect(techToggleBtn, &QPushButton::toggled, this, [this, idx](bool checked) {
                if (idx < faqItems.size()) {
                    setTechExpanded(faqItems[idx], checked, true);
                }
            });
        }
        connect(tocBtn, &QPushButton::clicked, this, [this, idx]() {
            if (idx < faqItems.size()) {
                FaqItemWidgets& it = faqItems[idx];
                setCardExpanded(it, true, true);
                updateToggleAllLabel();
                onFaqClicked(it.card);
            }
        });
    }

    tocLay->addStretch(1);
}

void SettingsFaqWidget::setCardExpanded(FaqItemWidgets& item, bool expanded, bool updateUserState)
{
    if (updateUserState) {
        item.userExpanded = expanded;
    }
    if (item.detailsContainer) {
        item.detailsContainer->setVisible(expanded);
    }
    if (item.chevronLabel) {
        item.chevronLabel->setText(expanded
            ? QString::fromUtf8("\xE2\x96\xBE")
            : QString::fromUtf8("\xE2\x96\xB8"));
    }
}

void SettingsFaqWidget::setTechExpanded(FaqItemWidgets& item, bool expanded, bool updateUserState)
{
    if (updateUserState) {
        item.userTechExpanded = expanded;
    }
    if (item.techContainer) {
        item.techContainer->setVisible(expanded);
    }
    if (item.techToggleBtn) {
        item.techToggleBtn->blockSignals(true);
        item.techToggleBtn->setChecked(expanded);
        item.techToggleBtn->setText(expanded
            ? tr("Hide technical details \xE2\x96\xB4")
            : tr("Show technical details \xE2\x96\xBE"));
        item.techToggleBtn->blockSignals(false);
    }
}

void SettingsFaqWidget::jumpToGuide(int num)
{
    setSection(num);
}

void SettingsFaqWidget::updateToggleAllLabel()
{
    if (!btnToggleAll) return;
    int visibleCount = 0;
    int expandedCount = 0;
    for (const auto& item : faqItems) {
        if (item.card && !item.card->isHidden()) {
            ++visibleCount;
            if (item.detailsContainer && !item.detailsContainer->isHidden()) {
                ++expandedCount;
            }
        }
    }
    const bool allExpanded = (visibleCount > 0 && expandedCount == visibleCount);
    btnToggleAll->setText(allExpanded ? tr("Collapse All") : tr("Expand All"));
    btnToggleAll->setEnabled(visibleCount > 0);
}

void SettingsFaqWidget::onToggleAllClicked()
{
    int visibleCount = 0;
    int expandedCount = 0;
    for (const auto& item : faqItems) {
        if (item.card && !item.card->isHidden()) {
            ++visibleCount;
            if (item.detailsContainer && !item.detailsContainer->isHidden()) {
                ++expandedCount;
            }
        }
    }
    const bool expand = !(visibleCount > 0 && expandedCount == visibleCount);
    for (auto& item : faqItems) {
        if (item.card && !item.card->isHidden()) {
            setCardExpanded(item, expand, true);
        }
    }
    updateToggleAllLabel();
}

void SettingsFaqWidget::showEvent(QShowEvent *event)
{
    PWidget::showEvent(event);
    if (pos >= 1 && pos <= static_cast<int>(faqItems.size())) {
        QPushButton* btn = faqItems[pos - 1].tocButton;
        if (btn) {
            QMetaObject::invokeMethod(btn, "setChecked", Qt::QueuedConnection, Q_ARG(bool, true));
            QMetaObject::invokeMethod(btn, "clicked", Qt::QueuedConnection);
        }
    }
}

void SettingsFaqWidget::setSection(int num)
{
    if (num < 1 || num > static_cast<int>(faqItems.size())) {
        pos = 0;
        if (searchEdit && !searchEdit->text().isEmpty()) {
            searchEdit->clear();
        }
        onCategoryFilterClicked(QStringLiteral("all"));
        if (!faqItems.empty() && faqItems[0].tocButton) {
            faqItems[0].tocButton->setChecked(true);
        }
        if (ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
            ui->scrollAreaFaq->verticalScrollBar()->setValue(0);
        }
        return;
    }

    pos = num;
    if (searchEdit && !searchEdit->text().isEmpty()) {
        searchEdit->clear();
    }
    if (activeCategory != QStringLiteral("all")) {
        onCategoryFilterClicked(QStringLiteral("all"));
    }
    FaqItemWidgets& item = faqItems[pos - 1];
    setCardExpanded(item, true, true);
    updateToggleAllLabel();
    if (item.tocButton) {
        item.tocButton->setChecked(true);
    }
    onFaqClicked(item.card);
}

void SettingsFaqWidget::onFaqClicked(const QWidget* const widget)
{
    if (widget && ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
        if (ui->scrollAreaWidgetContents && ui->scrollAreaWidgetContents->layout()) {
            ui->scrollAreaWidgetContents->layout()->activate();
        }
        isProgrammaticScroll = true;
        ui->scrollAreaFaq->verticalScrollBar()->setValue(widget->y());
        isProgrammaticScroll = false;

        // Re-align after queued geometry updates settle if an accordion just opened above
        const QWidget* targetPtr = widget;
        QTimer::singleShot(0, this, [this, targetPtr]() {
            if (targetPtr && ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
                isProgrammaticScroll = true;
                ui->scrollAreaFaq->verticalScrollBar()->setValue(targetPtr->y());
                isProgrammaticScroll = false;
            }
        });
    }
}

void SettingsFaqWidget::onFaqScrolled(int value)
{
    if (isProgrammaticScroll || faqItems.empty()) {
        return;
    }
    const int anchorY = value + 36;
    const FaqItemWidgets* activeItem = nullptr;
    for (const auto& item : faqItems) {
        if (!item.card || item.card->isHidden()) {
            continue;
        }
        if (!activeItem) {
            activeItem = &item;
        }
        if (item.card->y() <= anchorY) {
            activeItem = &item;
        } else {
            break;
        }
    }
    if (activeItem && activeItem->tocButton) {
        activeItem->tocButton->setChecked(true);
        if (tocScrollArea) {
            tocScrollArea->ensureWidgetVisible(activeItem->tocButton, 0, 20);
        }
    }
}

void SettingsFaqWidget::onSearchTextChanged(const QString& text)
{
    Q_UNUSED(text);
    applyFilters();
}

void SettingsFaqWidget::onCategoryFilterClicked(const QString& categoryKey)
{
    activeCategory = categoryKey;
    for (const auto& pair : categoryFilterButtons) {
        if (pair.second) {
            pair.second->setChecked(pair.first == categoryKey);
        }
    }
    applyFilters();
    if (ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
        isProgrammaticScroll = true;
        ui->scrollAreaFaq->verticalScrollBar()->setValue(0);
        isProgrammaticScroll = false;
    }
}

void SettingsFaqWidget::applyFilters()
{
    const QString query = searchEdit ? searchEdit->text().trimmed() : QString();
    int visibleCount = 0;
    std::vector<QString> visibleCats;
    QPushButton* firstVisibleToc = nullptr;

    if (popularBarWidget) {
        popularBarWidget->setVisible(activeCategory == QStringLiteral("all") && query.isEmpty());
    }

    for (auto& item : faqItems) {
        const bool catMatches = (activeCategory == QStringLiteral("all")) ||
                                (item.categoryKey == activeCategory);
        bool searchMatches = query.isEmpty();
        bool matchedFriendly = false;
        bool matchedTech = false;

        if (catMatches && !query.isEmpty()) {
            const QString plainBody = item.bodyLabel
                ? QTextDocumentFragment::fromHtml(item.bodyLabel->text()).toPlainText()
                : QString();
            const QString plainTech = item.techLabel
                ? QTextDocumentFragment::fromHtml(item.techLabel->text()).toPlainText()
                : QString();

            matchedFriendly = item.title.contains(query, Qt::CaseInsensitive) ||
                              item.tldr.contains(query, Qt::CaseInsensitive) ||
                              item.categoryLabel.contains(query, Qt::CaseInsensitive) ||
                              item.tocTitle.contains(query, Qt::CaseInsensitive) ||
                              plainBody.contains(query, Qt::CaseInsensitive);
            const bool matchedKeyword = item.keywords.contains(query, Qt::CaseInsensitive);
            matchedTech = !plainTech.isEmpty() && plainTech.contains(query, Qt::CaseInsensitive);
            searchMatches = matchedFriendly || matchedTech || matchedKeyword;
        }

        const bool show = catMatches && searchMatches;
        if (item.card) item.card->setVisible(show);
        if (item.tocButton) item.tocButton->setVisible(show);

        if (show) {
            ++visibleCount;
            visibleCats.push_back(item.categoryKey);
            if (!firstVisibleToc) {
                firstVisibleToc = item.tocButton;
            }
            if (!query.isEmpty()) {
                setCardExpanded(item, true, false);
                if (item.techLabel) {
                    setTechExpanded(item, matchedTech && !matchedFriendly ? true : item.userTechExpanded, false);
                }
            } else {
                setCardExpanded(item, item.userExpanded, false);
                if (item.techLabel) {
                    setTechExpanded(item, item.userTechExpanded, false);
                }
            }
        }
    }

    for (const auto& hdr : tocHeaders) {
        if (!hdr.label) continue;
        bool hasVisible = false;
        for (const QString& c : visibleCats) {
            if (c == hdr.categoryKey) {
                hasVisible = true;
                break;
            }
        }
        hdr.label->setVisible(hasVisible);
    }

    if (firstVisibleToc) {
        bool currentStillVisible = false;
        for (const auto& item : faqItems) {
            if (item.tocButton && !item.tocButton->isHidden() && item.tocButton->isChecked()) {
                currentStillVisible = true;
                break;
            }
        }
        if (!currentStillVisible) {
            firstVisibleToc->setChecked(true);
        }
    }

    if (labelResultCount) {
        labelResultCount->setText(tr("Showing %1 of %2 guides").arg(visibleCount).arg(faqItems.size()));
    }
    if (labelNoResults) {
        labelNoResults->setVisible(visibleCount == 0);
    }
    updateToggleAllLabel();
}

void SettingsFaqWidget::applyThemeStyles(bool isLight)
{
    const QString cardBg        = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#182132");
    const QString cardBorder    = isLight ? QStringLiteral("#DDE5F2") : QStringLiteral("#28354D");
    const QString hdrHoverBg    = isLight ? QStringLiteral("#F4F7FC") : QStringLiteral("#1E2940");
    const QString titleColor    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#E2ECFA");
    const QString mutedText     = isLight ? QStringLiteral("#475569") : QStringLiteral("#94A3B8");
    const QString groupColor    = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#7D98CC");
    const QString tagBg         = isLight ? QStringLiteral("#F1F5FB") : QStringLiteral("#1E2A40");
    const QString tagText       = isLight ? QStringLiteral("#4A679E") : QStringLiteral("#A5BEEA");
    const QString tagBorder     = isLight ? QStringLiteral("#D6E1F2") : QStringLiteral("#314262");
    const QString tldrBg        = isLight ? QStringLiteral("#F4F7FC") : QStringLiteral("#121A2A");
    const QString tldrAccent    = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#F5897C");
    const QString tldrText      = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#E2E8F0");
    const QString techBg        = isLight ? QStringLiteral("#F8FAFD") : QStringLiteral("#101828");
    const QString techBorder    = isLight ? QStringLiteral("#D5DFEE") : QStringLiteral("#273552");
    const QString searchBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#141C2B");
    const QString searchBorder  = isLight ? QStringLiteral("#CBD6E6") : QStringLiteral("#2E3D59");
    const QString searchText    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString pillBg        = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#192338");
    const QString pillBorder    = isLight ? QStringLiteral("#C9D6EB") : QStringLiteral("#2E3F5E");
    const QString pillText      = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString pillActiveBg  = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#5576B2");
    const QString pillHoverBg   = isLight ? QStringLiteral("#DCE6F7") : QStringLiteral("#23324E");
    const QString actBg         = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A");
    const QString actFg         = isLight ? QStringLiteral("#2F4878") : QStringLiteral("#9BB4E2");
    const QString actBrd        = isLight ? QStringLiteral("#B8CBE8") : QStringLiteral("#355184");

    if (labelSubtitle) {
        labelSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; }").arg(mutedText));
    }
    if (labelResultCount) {
        labelResultCount->setStyleSheet(QString("QLabel { color: %1; font-size: 12.5px; font-weight: 500; }").arg(mutedText));
    }
    if (labelNoResults) {
        labelNoResults->setStyleSheet(QString("QLabel { color: %1; padding: 28px; font-size: 14px; }").arg(mutedText));
    }
    if (searchEdit) {
        searchEdit->setStyleSheet(QString(
            "QLineEdit#faqSearchEdit { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 8px; padding: 0 12px; font-size: 13px; }"
            "QLineEdit#faqSearchEdit:focus { border-color: #7695CD; }"
        ).arg(searchBg, searchText, searchBorder));
    }

    const QString pillStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 15px; padding: 0 13px; font-size: 12.5px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; }"
        "QPushButton:checked { background-color: %5; color: #FFFFFF; border-color: %5; }"
    ).arg(pillBg, pillText, pillBorder, pillHoverBg, pillActiveBg);
    for (const auto& pair : categoryFilterButtons) {
        if (pair.second) {
            pair.second->setStyleSheet(pillStyle);
        }
    }

    const QString secBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 0 12px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
        "QPushButton:checked { background-color: %4; color: %5; border-color: #7695CD; }"
        "QPushButton:disabled { opacity: 0.5; }"
    ).arg(pillBg, pillText, pillBorder, pillHoverBg, titleColor);
    if (btnToggleAll) {
        btnToggleAll->setStyleSheet(secBtnStyle);
    }

    if (popularBarWidget) {
        popularBarWidget->setStyleSheet(QString(
            "QFrame#faqPopularBar { background-color: %1; border: 1px solid %2; border-radius: 10px; }"
        ).arg(cardBg, cardBorder));
    }
    if (popularTitleLabel) {
        popularTitleLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12.5px; font-weight: 700; }").arg(titleColor));
    }
    const QString popBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 8px; padding: 4px 12px; font-size: 12.5px; font-weight: 500; text-align: left; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; color: %5; }"
    ).arg(tldrBg, pillText, cardBorder, pillHoverBg, titleColor);
    for (QPushButton* popBtn : popularButtons) {
        if (popBtn) popBtn->setStyleSheet(popBtnStyle);
    }

    const QString tocHdrStyle = QString(
        "QLabel { color: %1; font-size: 11px; font-weight: 700; letter-spacing: 0.6px; }"
    ).arg(groupColor);
    for (const auto& hdr : tocHeaders) {
        if (hdr.label) {
            hdr.label->setStyleSheet(tocHdrStyle);
        }
    }

    const QString titleStyle = QString(
        "QLabel { color: %1; font-size: 16px; font-weight: 700; background: transparent; }"
    ).arg(titleColor);
    const QString tagStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 12px; padding: 0 10px; font-size: 11px; font-weight: 600; }"
    ).arg(tagBg, tagText, tagBorder);
    const QString chevronStyle = QString(
        "QLabel { color: %1; font-size: 14px; font-weight: 700; background: transparent; }"
    ).arg(mutedText);
    const QString tldrTextStyle = QString(
        "QLabel { color: %1; font-size: 13px; }"
    ).arg(tldrText);
    const QString actionBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 7px; padding: 0 14px; font-size: 12.5px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
    ).arg(actBg, actFg, actBrd, pillHoverBg);

    for (auto& item : faqItems) {
        item.currentLight = isLight;
        if (item.card) {
            item.card->setStyleSheet(QString(
                "QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
            ).arg(item.card->objectName(), cardBg, cardBorder));
        }
        if (item.headerBtn) {
            item.headerBtn->setStyleSheet(QString(
                "QPushButton#%1 { background: transparent; border: none; border-radius: 7px; padding: 4px 6px; text-align: left; }"
                "QPushButton#%1:hover { background-color: %2; }"
            ).arg(item.headerBtn->objectName(), hdrHoverBg));
        }
        if (item.titleLabel)   item.titleLabel->setStyleSheet(titleStyle);
        if (item.catTagLabel)  item.catTagLabel->setStyleSheet(tagStyle);
        if (item.chevronLabel) item.chevronLabel->setStyleSheet(chevronStyle);
        if (item.tldrBox) {
            item.tldrBox->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: 1px solid %3; border-left: 3px solid %4; border-radius: 6px; }"
            ).arg(item.tldrBox->objectName(), tldrBg, cardBorder, tldrAccent));
        }
        if (item.tldrLabel)   item.tldrLabel->setStyleSheet(tldrTextStyle);
        if (item.bodyLabel && item.bodyBuilder) {
            item.bodyLabel->setText(item.bodyBuilder(isLight));
        }
        if (item.actionBtn)     item.actionBtn->setStyleSheet(actionBtnStyle);
        if (item.techToggleBtn) item.techToggleBtn->setStyleSheet(secBtnStyle);
        if (item.techContainer) {
            item.techContainer->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 8px; }"
            ).arg(item.techContainer->objectName(), techBg, techBorder));
        }
        if (item.techLabel && item.techBuilder) {
            item.techLabel->setText(item.techBuilder(isLight));
        }
    }
}

void SettingsFaqWidget::changeTheme(bool isLightTheme, QString& theme)
{
    PWidget::changeTheme(isLightTheme, theme);
    applyThemeStyles(isLightTheme);
    for (QWidget* w : {static_cast<QWidget*>(ui->labelTitle),
                       static_cast<QWidget*>(ui->containerButtons),
                       static_cast<QWidget*>(ui->labelWebLink),
                       static_cast<QWidget*>(ui->pushButtonWebLink)}) {
        if (w) updateStyle(w);
    }
    for (const auto& item : faqItems) {
        if (item.tocButton) updateStyle(item.tocButton);
    }
}

std::vector<QPushButton*> SettingsFaqWidget::getButtons()
{
    std::vector<QPushButton*> btns;
    btns.reserve(faqItems.size());
    for (const auto& item : faqItems) {
        if (item.tocButton) {
            btns.push_back(item.tocButton);
        }
    }
    return btns;
}

SettingsFaqWidget::~SettingsFaqWidget()
{
    delete ui;
}
