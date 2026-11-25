// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsfaqwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsfaqwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "amount.h"
#include "chainparams.h"
#include "clientversion.h"
#include "version.h"

#include <QButtonGroup>
#include <QDesktopServices>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextDocumentFragment>
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
        "body { color: %1; font-size: 13.5px; line-height: 1.5; }"
        "p { margin-top: 4px; margin-bottom: 9px; }"
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
    this->setStyleSheet(_window ? _window->styleSheet() : QString());

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
        ui->left->setMinimumWidth(290);
        ui->left->setMaximumWidth(350);
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

    ui->labelTitle->setText(tr("KristaTech FAQ & Protocol Guide"));
    ui->pushButtonWebLink->setText(QStringLiteral("krista.kristalteknoloji.com"));

    // Instant Search bar in header
    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName("faqSearchEdit");
    searchEdit->setPlaceholderText(tr("Search FAQ (e.g. locked, staking, 4200, taproot, backup)..."));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(260);
    searchEdit->setMaximumWidth(370);
    searchEdit->setMinimumHeight(36);
    searchEdit->setMaximumHeight(36);
    ui->horizontalLayout_3->addWidget(searchEdit);
    connect(searchEdit, &QLineEdit::textChanged, this, &SettingsFaqWidget::onSearchTextChanged);

    // Subtitle row directly below header
    labelSubtitle = new QLabel(
        tr("Plain-English answers covering your wallet balance, mining & staking rewards, masternodes, security, and diagnostics."),
        ui->container);
    labelSubtitle->setWordWrap(true);
    labelSubtitle->setContentsMargins(10, 0, 10, 0);
    ui->verticalLayoutcont->insertWidget(1, labelSubtitle);

    // Category filter bar directly above 2-column content area
    filterBarWidget = new QWidget(ui->container);
    filterBarWidget->setObjectName("faqFilterBar");
    QHBoxLayout* filterLay = new QHBoxLayout(filterBarWidget);
    filterLay->setContentsMargins(10, 2, 10, 4);
    filterLay->setSpacing(6);

    QButtonGroup* filterBtnGroup = new QButtonGroup(this);
    filterBtnGroup->setExclusive(true);

    const std::vector<std::pair<QString, QString>> filters = {
        {QStringLiteral("all"),         tr("All (14)")},
        {QStringLiteral("started"),     tr("Getting Started")},
        {QStringLiteral("earning"),     tr("Mining && Staking")},
        {QStringLiteral("masternodes"), tr("Masternodes")},
        {QStringLiteral("security"),    tr("Contracts && Security")},
        {QStringLiteral("help"),        tr("Help && Tools")}
    };
    for (size_t i = 0; i < filters.size(); ++i) {
        const QString& key = filters[i].first;
        const QString& label = filters[i].second;
        QPushButton* pill = new QPushButton(label, filterBarWidget);
        pill->setCheckable(true);
        pill->setChecked(i == 0);
        pill->setCursor(Qt::PointingHandCursor);
        pill->setFocusPolicy(Qt::NoFocus);
        pill->setMinimumHeight(30);
        pill->setMaximumHeight(30);
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
    ui->verticalLayoutcont->insertWidget(2, filterBarWidget);

    // Scrollable categorized TOC inside left containerButtons card
    tocScrollArea = new QScrollArea(ui->containerButtons);
    tocScrollArea->setObjectName("faqTocScroll");
    tocScrollArea->setFrameShape(QFrame::NoFrame);
    tocScrollArea->setWidgetResizable(true);
    tocScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tocScrollArea->viewport()->setAutoFillBackground(false);
    tocScrollArea->setStyleSheet("QScrollArea#faqTocScroll { background: transparent; border: none; }");

    tocContentWidget = new QWidget(tocScrollArea);
    tocContentWidget->setObjectName("faqTocContent");
    tocContentWidget->setStyleSheet("#faqTocContent { background: transparent; }");
    QVBoxLayout* tocLay = new QVBoxLayout(tocContentWidget);
    tocLay->setContentsMargins(2, 2, 2, 6);
    tocLay->setSpacing(3);
    tocScrollArea->setWidget(tocContentWidget);
    if (ui->verticalLayout) {
        ui->verticalLayout->addWidget(tocScrollArea);
    }

    // Card area spacing & empty state
    ui->verticalLayout_7->setSpacing(14);
    ui->verticalLayout_7->setContentsMargins(4, 2, 10, 16);

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
        {QStringLiteral("started"),     tr("GETTING STARTED")},
        {QStringLiteral("earning"),     tr("MINING, STAKING & REWARDS")},
        {QStringLiteral("masternodes"), tr("MASTERNODES & FAIRNESS")},
        {QStringLiteral("security"),    tr("CONTRACTS & SECURITY")},
        {QStringLiteral("help"),        tr("HELP & DIAGNOSTICS")}
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
    };

    std::vector<GuideSeed> seeds;
    seeds.reserve(14);

    // 1
    seeds.push_back({
        1, QStringLiteral("started"), tr("Getting Started"),
        tr("01  What is KRISTA?"),
        tr("What is KRISTA (v%1) and how does it work?").arg(verStr),
        tr("KRISTA is a Layer-1 hybrid blockchain combining fair multi-algorithm CPU/GPU mining (ADAM / MPA), Proof of Stake, Masternode finality (LLMQ), and Taproot smart contracts with 30-second blocks."),
        QStringLiteral("intro hybrid adam mpa pos llmq mescal taproot bip324 supply network port overview"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString intro = tr(
                "<p align=\"justify\"><b>KRISTA</b> is built so both newcomers with zero balance and long-term holders can participate without expensive ASIC farms:</p>"
                "<ul>"
                "<li><b>MPA / ADAM Cooperative Consensus:</b> Every %1 seconds, 11 randomly elected validators solve lightweight single-pass puzzles using a randomly assigned algorithm per slot so consumer CPUs and GPUs compete fairly.</li>"
                "<li><b>Proof of Stake (PoS):</b> Mature wallet balances can stake quietly in the background to mint blocks and earn transaction fees + staking multipliers.</li>"
                "<li><b>BLS Masternode Finality (LLMQ):</b> 24/7 Masternodes form rotating BLS quorums (3-of-5 up to 8-of-15) that lock block finality and prevent chain reorganizations.</li>"
                "<li><b>MESCAL v2 Taproot Smart Contracts:</b> Built-in native smart contracts on <code>%2" "1p...</code> addresses with zero gas auction wars.</li>"
                "</ul>"
            ).arg(targetSec).arg(hrp);
            const QString specsTable = buildStyledTable(t,
                {tr("Network Property"), tr("Value (%1)").arg(netName)},
                {
                    {tr("Client & Protocol Version"), QString("<code>v%1</code> &bull; Protocol <code>%2</code>").arg(verStr).arg(PROTOCOL_VERSION)},
                    {tr("Block Target Time"), tr("<b>%1 seconds</b> (15-second TimeProtocolV2 slots)").arg(targetSec)},
                    {tr("Maximum Total Supply"), tr("<b>%1 KRISTA</b> hard cap").arg(loc.toString(maxSupply))},
                    {tr("Initial Block Subsidy"), tr("<b>17 KRISTA</b> (reduces smoothly by <b>2.15%</b> every 259,200 blocks / ~90 days)")},
                    {tr("P2P Transport & Port"), tr("<code>BIP324</code> encrypted v2 transport on by default &bull; TCP <code>%1</code>").arg(p2pPort)}
                });
            return wrapFaqHtml(t, intro + specsTable);
        }
    });

    // 2
    seeds.push_back({
        2, QStringLiteral("started"), tr("Getting Started"),
        tr("02  Addresses, Sending & Receiving"),
        tr("Which address format should I use (KT..., 7..., kt1p...)?"),
        tr("Use regular KT... addresses for everyday transfers, Masternodes, and Miner Hub seats; kt1p... is used for MESCAL Taproot smart contracts."),
        QStringLiteral("address receive send fee p2pkh p2sh bech32 taproot kt kt1p address book"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tbl = buildStyledTable(t,
                {tr("Prefix"), tr("Type"), tr("Best Used For")},
                {
                    {QStringLiteral("<code>KT...</code> / <code>k...</code>"), tr("Standard P2PKH"),
                     tr("Everyday payments, exchanges, PoS staking, Miner Hub seats, and Masternode collateral/payouts.")},
                    {QStringLiteral("<code>7...</code> / <code>S...</code>"), tr("Script Hash P2SH"),
                     tr("Legacy multi-signature scripts and classic script-wrapped outputs.")},
                    {QString("<code>%1" "1p...</code>").arg(hrp), tr("MESCAL v2 Taproot (Bech32m)"),
                     tr("Private & compact Taproot Time-Locks, M-of-N Vaults, Hash-Lock swaps, and programmable rules.")}
                });
            const QString steps = tr(
                "<p><b>How to Send &amp; Receive safely:</b></p>"
                "<ol>"
                "<li>Open <b>RECEIVE</b>, click generate/copy, and double-check the copied address before sharing.</li>"
                "<li>Save frequent recipients under <b>ADDRESS BOOK</b> for 1-click sending without typos.</li>"
                "<li>Standard transfers confirm in <b>1 block (~%1 seconds)</b>; transaction fees stay tiny (fractions of a KRISTA).</li>"
                "</ol>"
            ).arg(targetSec);
            return wrapFaqHtml(t, tbl + steps);
        }
    });

    // 3
    seeds.push_back({
        3, QStringLiteral("started"), tr("Getting Started"),
        tr("03  Locked & Pending Balances"),
        tr("Why are my coins Locked, Immature, or Pending?"),
        tr("Coins locked in Masternode collateral, Coin-Lock, PoW-Lock, or PoS Time-Lock remain 100% under your own private keys and unlock automatically when their timer ends."),
        QStringLiteral("locked pending unspendable immature 100 confirmations 4200 250 10 cltv collateral topbar"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString intro = tr(
                "<p align=\"justify\">Hover over the <b>Locked / Pending</b> badge in the TopBar at any time to see the exact breakdown. Nothing in KRISTA leaves your wallet unless you explicitly send it:</p>"
            );
            const QString tbl = buildStyledTable(t,
                {tr("Status Type"), tr("Duration / Requirement"), tr("What Happens Next")},
                {
                    {tr("<b>Incoming Transfer</b> (Pending)"), tr("<b>1 confirmation</b> (~%1s)").arg(targetSec),
                     tr("Becomes Available automatically as soon as the next block confirms it.")},
                    {tr("<b>Mined / Staked / Validator Reward</b> (Immature)"), tr("<b>%1 confirmations</b> (~%2 min)").arg(coinbaseMat).arg((coinbaseMat * targetSec) / 60),
                     tr("Newly minted rewards and Miner Hub lock inputs require %1 confirmations before spending or locking.").arg(coinbaseMat)},
                    {tr("<b>Miner Hub Coin-Lock</b> (250 KRISTA) &amp; <b>PoW-Lock</b> (10 KRISTA)"), tr("<b>2,880 blocks</b> (~24 hours)<br/><i>+20-block wallet renewal buffer</i>"),
                     tr("Protected by on-chain <code>CLTV</code> inside your own wallet; automatically becomes spendable when the lock block expires.")},
                    {tr("<b>Masternode Collateral</b> (4,200 KRISTA)"), tr("While Masternode is active"),
                     tr("Held inside your Cold Wallet and locked against accidental spending; unlock or spend anytime if you shut down the Masternode.")},
                    {tr("<b>PoS Proof of Lock (PoL)</b>"), tr("User-chosen block/date horizon"),
                     tr("Earns up to <b>3.0x</b> stake weight and auto-renews principal + script on winning stakes until expiry.")}
                });
            return wrapFaqHtml(t, intro + tbl);
        }
    });

    // 4
    seeds.push_back({
        4, QStringLiteral("earning"), tr("Mining & Staking"),
        tr("04  Block Rewards & Model D"),
        tr("How are block rewards distributed? (Model D Economics)"),
        tr("7% goes to Ecosystem Development; the remaining 93% Net Consensus Pool splits 5% Passive Masternodes, 60% Equal Active Validators (15 seats × 4%), 25% Producer (22% Staker + 3% Coord in PoS), and 10% Signer-Only LLMQ."),
        QStringLiteral("reward emission subsidy model d 17 krista 7% 93% 5% 60% 25% 22% 10% burn dev treasury pos fee"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString top = tr(
                "<p align=\"justify\">During Period 0 (first 259,200 blocks, ~90 days), each block creates <b>17.00 KRISTA</b>: <b>7.0%</b> (<code>1.19 KRISTA</code>) supports ecosystem development (<code>%1</code>), and <b>93.0%</b> (<code>15.81 KRISTA</code>) goes to consensus participants under <b>Model D</b> (active from block <b>%2</b>; pre-Model D blocks split <code>75% / 11</code> per validator seat + <code>25%</code> Producer/Coordinator):</p>"
            ).arg(devAddr, loc.toString(modelDHeight));
            const QString tbl = buildStyledTable(t,
                {tr("Pool Share"), tr("% of Net Pool"), tr("Base KRISTA (Period 0)"), tr("Who Receives It")},
                {
                    {tr("<b>Passive Masternodes</b>"), QStringLiteral("<b>5%</b>"), QStringLiteral("0.7905 KRISTA"),
                     tr("Deterministic queue across active KTDIP3 Masternodes.")},
                    {tr("<b>Active MPA Validators</b> (15 seats)"), QStringLiteral("<b>60%</b> (4.00% / seat)"), QStringLiteral("0.6324 / seat"),
                     tr("Equal pay per solved committee seat across 8 MN, 2 Coin-Lock, 3 PoW-Lock, and 2 Zero-Coin slots (9.4860 KRISTA total).")},
                    {tr("<b>Producer / Staker</b>"), QStringLiteral("<b>25%</b>"), QStringLiteral("3.9525 KRISTA"),
                     tr("MPA Block Coordinator (PoW), or split <b>22% Staker (3.4782) + 3% Signing Coordinator (0.4743) + 100% tx fees</b> on PoS blocks.")},
                    {tr("<b>BLS LLMQ Finality Quorum</b>"), QStringLiteral("<b>10%</b>"), QStringLiteral("1.5810 KRISTA"),
                     tr("Signer-only split (<code>10% / N</code>) paid exclusively to previous-block LLMQ signers (1-block lag).")}
                });
            const QString bottom = tr(
                "<p align=\"justify\"><b>What happens to unsolved or offline seats?</b> Unclaimed validator or non-signing LLMQ shares are never redistributed to attackers: <b>50% is permanently burned</b> (deducted from total supply with zero PoB weight) and <b>50% goes to the Developer Treasury</b>.</p>"
            );
            return wrapFaqHtml(t, top + tbl + bottom);
        }
    });

    // 5
    seeds.push_back({
        5, QStringLiteral("earning"), tr("Mining & Staking"),
        tr("05  Miner Hub Guide (0 to 4,200)"),
        tr("Miner Hub: How can I mine or validate (even with 0 KRISTA)?"),
        tr("Open the MINER HUB tab, choose a tier (0 KRISTA Zero-Coin PoW, 10 KRISTA PoW-Lock, or 250 KRISTA Coin-Lock), register your seat, and click Start 1-Click Mining."),
        QStringLiteral("miner hub zerocoin 0 krista pow-lock coin-lock open entry 250 10 track committee quota register"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString steps = tr(
                "<p><b>4-Step Quick Start in the MINER HUB tab:</b></p>"
                "<ol>"
                "<li>Wait until TopBar shows <code>✓ Synced</code> and your wallet is unlocked.</li>"
                "<li>Click <b>MINER HUB → Register Tracks</b> to pick the tier that matches your wallet balance (inputs require &ge; %1 confirmations for 10 or 250 KRISTA tiers).</li>"
                "<li>Wait 1 block (~%2s) for your registration transaction to appear in your live validator roster.</li>"
                "<li>Click <b>Start 1-Click Mining</b> — your wallet automatically solves puzzles whenever one of your addresses is elected to the 15-seat Model D (11-seat Pre-Model D) committee.</li>"
                "</ol>"
            ).arg(coinbaseMat).arg(targetSec);
            const QString tbl = buildStyledTable(t,
                {tr("Validator Track"), tr("Lock Required"), tr("Block Committee Quota (Model D)"), tr("Coordinator VRF Weight")},
                {
                    {tr("<b>Zero-Coin Open-Entry</b>"), QStringLiteral("<b>0 KRISTA</b><br/><small>15-bit HashX11KVS</small>"),
                     tr("<b>2 permanent seats / block</b> (Model D; 1 Pre-Model D; active from block %1; max 240 bootstrap slots)").arg(loc.toString(openEntryHeight)),
                     QStringLiteral("0")},
                    {tr("<b>Funded PoW-Lock</b>"), QStringLiteral("<b>10 KRISTA</b> (24h CLTV)<br/><small>+ HashX11KVS PoW</small>"),
                     tr("<b>3 seats / block</b> (up to 5 Open PoW incl. fallback; equal 4.00%/seat)"),
                     QStringLiteral("10")},
                    {tr("<b>Coin-Lock Validator</b>"), QStringLiteral("<b>250 KRISTA</b> (24h CLTV)"),
                     tr("<b>2 dedicated seats / block</b> (passive CLTV lock + puzzle solution; equal 4.00%/seat)"),
                     QStringLiteral("250")},
                    {tr("<b>KTDIP3 Masternode</b>"), QStringLiteral("<b>4,200 KRISTA</b> Collateral"),
                     tr("<b>8 seats / block</b> (+ LLMQ 10% + 5% passive MN queue)"),
                     QStringLiteral("4,200 (55% MN Floor)")}
                });
            const QString note = tr(
                "<p align=\"justify\"><small><b>Tip:</b> Each wallet holds at most 1 active Coin-Lock seat and 1 active PoW-Lock seat. Funded locks renew cleanly within the final <b>240 blocks</b> on Mainnet (<b>15 blocks</b> on Testnet when Model D is active) with zero gap; Zero-Coin seats renew in the final <b>24 blocks</b> (<b>8 blocks</b> post-Model D Testnet).</small></p>"
            );
            return wrapFaqHtml(t, steps + tbl + note);
        }
    });

    // 6
    seeds.push_back({
        6, QStringLiteral("earning"), tr("Mining & Staking"),
        tr("06  PoS Staking & PoL / PoM / PoB"),
        tr("How do PoS Staking and Weight Multipliers (PoL, PoM, PoB) work?"),
        tr("Keep mature coins (%1+ confirmations on %2) in an unlocked wallet; staking happens automatically, and you can boost stake weight up to 3x (PoL), 1.25x (PoM), or 6x (PoB).").arg(stakeConfs).arg(netName),
        QStringLiteral("pos staking pol pom pob multiplier weight 600 confirmations unlock for staking proof of lock masternode burn"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString reqs = tr(
                "<p><b>Staking Requirements:</b> Hold spendable coins with at least <b>%1 confirmations</b> and <b>%2 minutes</b> coin age on %3, sync the chain, and click the TopBar lock icon &rarr; <b>Unlock for Staking</b>. When a stake wins, your wallet cooperates with the active Coordinator and earns <b>22% Staker Share + 100% of block transaction fees</b> under Model D (20% Pre-Model D).</p>"
            ).arg(loc.toString(stakeConfs)).arg(stakeAgeMins).arg(netName);
            const QString tbl = buildStyledTable(t,
                {tr("Multiplier"), tr("Max Boost"), tr("How It Works")},
                {
                    {tr("<b>PoL</b> (Proof of Lock)"), QStringLiteral("<b>Up to 3.0x</b>"),
                     tr("Time-lock staking UTXOs via CLTV to boost kernel weight. On winning stakes, the protocol <b>automatically preserves and renews</b> both principal and CLTV script until lock expiry.")},
                    {tr("<b>PoM</b> (Proof of Masternode)"), QStringLiteral("<b>Up to 1.25x</b>"),
                     tr("Active KTDIP3 Masternode owners gain a modest uptime loyalty boost (<code>&alpha; = 0.25</code> scaling smoothly over <code>%1</code> blocks) without outweighing dedicated stakers or burners.").arg(loc.toString(mnUptimeLimit))},
                    {tr("<b>PoB</b> (Proof of Burn)"), QStringLiteral("<b>Up to 6.0x</b>"),
                     tr("Voluntary burns sent to the community burn address receive the highest staking weight multiplier, decaying linearly over <code>%1</code> blocks.").arg(loc.toString(burnDecay))}
                });
            return wrapFaqHtml(t, reqs + tbl);
        }
    });

    // 7
    seeds.push_back({
        7, QStringLiteral("earning"), tr("Mining & Staking"),
        tr("07  External CPU / GPU Miners"),
        tr("How do I connect external miners (cpuminer-opt / ccminer)?"),
        tr("Enable RPC in kristatech.conf, unlock your synced wallet with registered validator keys, and point cpuminer-opt or ccminer at your node's RPC port."),
        QStringLiteral("external miner cpuminer ccminer gpu cpu getblocktemplate submitblock ext miner rpcuser rpcpassword"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">For higher puzzle throughput or multi-rig setups, external miners communicate with your node via <code>getblocktemplate</code> and <code>submitblock</code>. Once connected, <b>MINER HUB</b> automatically shows the <b>&bull; EXT MINER</b> status badge.</p>"
                "<p><b>Required steps:</b></p>"
                "<ol>"
                "<li>Open TopBar <b>Config Files &rarr; kristatech.conf</b> and add RPC credentials:<br/>"
                "<code>server=1</code> &bull; <code>rpcuser=youruser</code> &bull; <code>rpcpassword=strong_pass</code> &bull; <code>rpcallowip=127.0.0.1</code></li>"
                "<li>Restart the wallet/daemon so RPC listens locally.</li>"
                "<li>Register a Miner Hub seat (or Masternode) and keep the wallet <b>synced and unlocked</b> so the daemon can BLS-sign external <code>puzzleheader</code> solutions before broadcasting <code>ADAMSOL</code> to peers.</li>"
                "<li>Point <code>cpuminer-opt</code> (CPU) or <code>ccminer</code> (GPU) to your local RPC endpoint with <code>--coinbase-addr</code> or <code>mineraddress</code> set to your registered miner address.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, body);
        }
    });

    // 8
    seeds.push_back({
        8, QStringLiteral("masternodes"), tr("Masternodes"),
        tr("08  Masternodes & BLS Finality"),
        tr("What is a KristaTech Masternode (KTDIP3) & BLS LLMQ Quorum?"),
        tr("A Masternode locks 4,200 KRISTA safely in your Cold Wallet to earn from 4 reward streams while rotating BLS quorums finalize blocks."),
        QStringLiteral("masternode ktdip3 protx llmq quorum pose operator bls 4200 finality passive active"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString intro = tr(
                "<p align=\"justify\">KristaTech uses <b>KTDIP3</b> deterministic Masternodes (<code>ProRegTx</code> stored on-chain in <code>evodb</code>) with strict <b>Hot/Cold key separation</b>: your <code>4,200 KRISTA</code> collateral never leaves your offline/desktop wallet, while your VPS only holds an operator BLS secret key (<code>blsPubKeyOperator</code>).</p>"
                "<p><b>A healthy online Masternode earns across four streams simultaneously:</b></p>"
                "<ul>"
                "<li><b>5% Passive Masternode Queue:</b> Deterministic payouts rotated in order across active Masternodes.</li>"
                "<li><b>8 of 15 Active Validator Seats (4.00% each / 32%):</b> Elected Masternodes solve MPA puzzles alongside Peer Tracks.</li>"
                "<li><b>10% Signer-Only LLMQ Finality:</b> Dynamic quorums scaling from <b>3-of-5</b> up to <b>8-of-15</b> sign blocks with 1-block lag; non-signers receive zero LLMQ payout and accrue <code>-5 / +5</code> deterministic PoSe health scoring.</li>"
                "<li><b>Coordinator VRF (4,200 weight, 55% MN floor) + PoM (up to 1.25x):</b> Strongest odds to seal MPA blocks (+25% PoW / +3% PoS Coordinator reward) plus a reduced PoM staking bonus.</li>"
                "</ul>"
            );
            return wrapFaqHtml(t, intro);
        }
    });

    // 9
    seeds.push_back({
        9, QStringLiteral("masternodes"), tr("Masternodes"),
        tr("09  Cold Controller & Multi-MN Setup"),
        tr("How do I set up a Cold Controller and Single-IP Multi-Masternode?"),
        tr("Register collateral cleanly in the MASTERNODES tab, then list one alias/key/BLS line per node in activemasternode.conf on your VPS (never put masternodeprivkey inside kristatech.conf)."),
        QStringLiteral("activemasternode.conf multi mn controller proregtx proupservtx vps hot cold bls_generate port 27999"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p><b>Part 1 — Cold Controller (this Desktop Wallet):</b></p>"
                "<ol>"
                "<li>Receive exactly <b>4,200 KRISTA</b> into an address and wait for <b>15 confirmations</b>.</li>"
                "<li>Open <b>MASTERNODES &rarr; + Create Masternode Controller</b>, enter your VPS <code>IP:%1</code>, and submit the on-chain <code>ProRegTx</code>.</li>"
                "</ol>"
                "<p><b>Part 2 — Hot VPS Hosting (1 or Many MNs on One IP:%1):</b></p>"
                "<ol>"
                "<li>Create/open <code>activemasternode.conf</code> (1-click accessible via TopBar <b>Config Files</b> menu) and add one line per Masternode:<br/>"
                "<code>mn1 &lt;mn_privkey&gt; &lt;bls_privkey_hex&gt;</code></li>"
                "<li>In <code>kristatech.conf</code> on the VPS, enable <code>masternode=1</code> and <code>txindex=1</code>. <b>Never</b> place <code>masternodeprivkey=</code> inside <code>kristatech.conf</code>.</li>"
                "<li>Restart the VPS daemon and monitor live PoSe health &amp; BLS quorum status in <b>MASTERNODES</b> (if PoSe-banned after downtime, revive in 1 click via <code>ProUpServTx</code>).</li>"
                "</ol>"
            ).arg(p2pPort);
            return wrapFaqHtml(t, body);
        }
    });

    // 10
    seeds.push_back({
        10, QStringLiteral("masternodes"), tr("Masternodes"),
        tr("10  Fairness & Anti-Sybil Protection"),
        tr("How does ADAM guarantee fairness and stop Sybil attacks?"),
        tr("Rigid per-track quotas (8 / 2 / 3 / 2 under Model D; 7 / 1 / 2 / 1 Pre-Model D), 100-confirmation lock inputs, 1-registration-per-block limits, Proportional Capital Coordinator VRF, and 50/50 burn on skipped seats stop wallet-splitting attacks."),
        QStringLiteral("sybil fairness attack equal pay anti-sybil quota 8 2 3 2 7 1 2 1 withholding burn vrf 4200 250 10"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\">Opening 100 empty wallets or splitting funds cannot take over the KristaTech network because five independent protocol walls work together:</p>"
                "<ul>"
                "<li><b>Strict Track Isolation (8 / 2 / 3 / 2 Model D; 7 / 1 / 2 / 1 Pre-Model D):</b> Flooding Zero-Coin PoW can never steal seats reserved for Masternodes (8), Coin-Lock (2), or Funded PoW-Lock (3). Even with 20+ empty identities, Zero-Coin competes strictly for its two permanent Open-Entry seats.</li>"
                "<li><b>Immature Coin &amp; Burst Defense:</b> Lock inputs must have <b>%1+ confirmations</b> (blocking single-block coin recycling), each wallet may hold only <b>1 active seat per track</b>, and each block accepts at most <b>1 Coin-Lock + 1 Funded PoW-Lock + 1 Zero-Coin</b> registration (<code>0x01:0</code> sentinel).</li>"
                "<li><b>Proportional Coordinator VRF (4,200 : 250 : 10, 55% Model D / 75% Pre-Model D MN Floor):</b> Coordinator election scales strictly with locked capital and preserves at least a 55% active MN security floor under Model D (75% Pre-Model D); Zero-Coin holds <code>0</code> Coordinator weight.</li>"
                "<li><b>BFT LLMQ Protection:</b> Finality signatures come 100% from 24/7 Masternodes — uncollateralized Sybils cannot forge or stall quorum signatures.</li>"
                "<li><b>Withholding Immunity:</b> If attackers refuse to submit their puzzle solutions, honest nodes still clear consensus threshold and unsolved seats go <b>50% Burn / 50% Dev Treasury</b>.</li>"
                "</ul>"
            ).arg(coinbaseMat);
            return wrapFaqHtml(t, body);
        }
    });

    // 11
    seeds.push_back({
        11, QStringLiteral("security"), tr("Contracts & Security"),
        tr("11  MESCAL v2 Smart Contracts"),
        tr("What can I build with MESCAL v2 Taproot Smart Contracts?"),
        tr("Create non-custodial Time-Lock savings vaults, M-of-N Multi-Sig treasuries, and Hash-Lock atomic swaps directly inside the SMART CONTRACTS tab."),
        QStringLiteral("mescal smart contracts taproot kt1p multisig time-lock hash-lock htlc yaml vault escrow"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p align=\"justify\"><b>MESCAL v2</b> brings Bitcoin-grade deterministic Taproot smart contracts (<code>%1" "1p...</code>) right into the wallet UI with predictable byte-sized fees and no Turing-complete reentrancy bugs:</p>"
                "<ul>"
                "<li><b>Time-Lock Vaults:</b> Lock funds until an exact block height or calendar date before anyone can spend them.</li>"
                "<li><b>M-of-N Multi-Sig Vaults:</b> Require <code>M</code> out of <code>N</code> trusted public keys (e.g., 2-of-3 team or cold backup treasury).</li>"
                "<li><b>Hash-Lock (HTLC):</b> Trustless secret-preimage swaps and conditional payments.</li>"
                "<li><b>Custom YAML Composer:</b> Open the <b>SMART CONTRACTS</b> tab to choose a template, inspect the live <b>Contract Summary</b> (byte size &amp; fee estimate), fund the contract, and save specifications via <b>Export Spec &#x25BE;</b>.</li>"
                "</ul>"
            ).arg(hrp);
            return wrapFaqHtml(t, body);
        }
    });

    // 12
    seeds.push_back({
        12, QStringLiteral("security"), tr("Contracts & Security"),
        tr("12  Backups, Encryption & BIP38"),
        tr("How do I back up and protect my KRISTA wallet properly?"),
        tr("Encrypt your wallet with a strong passphrase, save offline copies of wallet.dat after generating new addresses or Masternode keys, and store a paper/encrypted BIP38 backup."),
        QStringLiteral("backup passphrase encrypt wallet.dat bip38 sign verify message csv history security private key"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString body = tr(
                "<p><b>Essential 4-Step Security Checklist (all built into Settings):</b></p>"
                "<ol>"
                "<li><b>Encrypt First:</b> Open <b>Settings &rarr; Backup &amp; Passphrase</b> to encrypt your wallet. Unlock using <i>For Staking Only</i> mode during daily operation so transfers remain passphrase-protected.</li>"
                "<li><b>Back Up <code>wallet.dat</code> Regularly:</b> Click <b>Backup Wallet</b> in <b>Backup &amp; Passphrase</b> and store encrypted copies on two separate offline drives after major key creation.</li>"
                "<li><b>Cold Paper / BIP38 Backups:</b> Use <b>Settings &rarr; BIP38 Key Tool</b> to password-encrypt individual private keys for cold vault storage.</li>"
                "<li><b>Verify Ownership &amp; Records:</b> Use <b>Sign / Verify Message</b> to prove address ownership without exposing keys, and <b>Export History (CSV)</b> for tax/audit logs. Peer traffic is automatically encrypted with <code>BIP324</code>.</li>"
                "</ol>"
            );
            return wrapFaqHtml(t, body);
        }
    });

    // 13
    seeds.push_back({
        13, QStringLiteral("help"), tr("Help & Tools"),
        tr("13  Troubleshooting & Wallet Repair"),
        tr("Stuck sync, missing transactions, or balance mismatch — how do I fix it?"),
        tr("Verify your system clock is synced via NTP, ensure your wallet is unlocked when mining/staking, and use the 1-click tools under Settings → Wallet Repair."),
        QStringLiteral("troubleshoot fix stuck sync missing balance rescan recover salvage rebuild index clock port wallet repair"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString tbl = buildStyledTable(t,
                {tr("Symptom"), tr("Recommended Action")},
                {
                    {tr("<b>Wallet Stuck / Few Peers</b>"),
                     tr("Check your PC clock accuracy (TimeProtocolV2 uses strict 15-second slots), open outbound/inbound TCP <code>%1</code>, and inspect peer list in <b>Settings &rarr; Node Information</b>.").arg(p2pPort)},
                    {tr("<b>Transactions Missing or Wrong Balance</b>"),
                     tr("Back up <code>wallet.dat</code> first, then open <b>Settings &rarr; Wallet Repair</b> and run <b>Rescan blockchain files</b> (or <b>Recover transactions 1</b> to drop unconfirmed stuck mempool entries).")},
                    {tr("<b>Database Corruption After Power Loss</b>"),
                     tr("In <b>Settings &rarr; Wallet Repair</b>, click <b>Rebuild index</b> (<code>-reindex</code>) or <b>Salvage wallet</b>.")},
                    {tr("<b>Miner Hub / Staking Not Producing</b>"),
                     tr("Make sure TopBar shows <code>✓ Synced</code>, click the TopBar lock icon to unlock your wallet, and verify inputs have <b>%1+ confs</b> (Miner Hub) or <b>%2+ confs</b> (PoS).").arg(coinbaseMat).arg(loc.toString(stakeConfs))}
                });
            return wrapFaqHtml(t, tbl);
        }
    });

    // 14
    seeds.push_back({
        14, QStringLiteral("help"), tr("Help & Tools"),
        tr("14  Explorer, Docs & RPC Diagnostics"),
        tr("Official Website, Live Block Explorer & RPC Console Cheat Sheet"),
        tr("Check live blocks on explorer.kristalteknoloji.com or inspect real-time consensus state locally via Settings → RPC Debug Console."),
        QStringLiteral("explorer website link support docs rpc console getinfo getmininginfo getadamminers protx listmasternodes getactivequorum"),
        [=](bool isLight) -> QString {
            const FaqHtmlTheme t = makeHtmlTheme(isLight);
            const QString links = tr(
                "<p>"
                "<b>Official Website &amp; Documentation:</b> <a style=\"color:%1; font-weight:600;\" href=\"https://krista.kristalteknoloji.com\">https://krista.kristalteknoloji.com</a><br/>"
                "<b>Live Block &amp; Consensus Explorer:</b> <a style=\"color:%2; font-weight:600;\" href=\"https://explorer.kristalteknoloji.com\">https://explorer.kristalteknoloji.com</a>"
                "</p>"
                "<p><b>Helpful Commands for Settings &rarr; RPC Debug Console</b> (also available as 1-click quick chips):</p>"
            ).arg(t.accentBlue, t.accentCoral);
            const QString tbl = buildStyledTable(t,
                {tr("RPC Command"), tr("What It Inspects")},
                {
                    {QStringLiteral("<code>getinfo</code>"), tr("Block height, connections, supply, wallet balance, and sync health.")},
                    {QStringLiteral("<code>getmininginfo</code> / <code>getstakingstatus</code>"), tr("Active mining difficulty, ADAM solver status, and PoS staking readiness.")},
                    {QStringLiteral("<code>getadamminers</code>"), tr("Live ADAM validator pool showing Masternode, Coin-Lock, PoW-Lock, and Zero-Coin seats.")},
                    {QStringLiteral("<code>protx list valid</code> / <code>listmasternodes</code>"), tr("All valid KTDIP3 Masternodes, PoSe penalty scores, and queue order.")},
                    {QStringLiteral("<code>getactivequorum</code>"), tr("Current rotating BLS LLMQ quorum members and signature threshold.")}
                });
            return wrapFaqHtml(t, links + tbl);
        }
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
        QPushButton* tocBtn = new QPushButton(seed.tocTitle, tocContentWidget);
        tocBtn->setCheckable(true);
        tocBtn->setChecked(seed.num == 1);
        tocBtn->setCursor(Qt::PointingHandCursor);
        tocBtn->setFocusPolicy(Qt::NoFocus);
        tocBtn->setMinimumHeight(38);
        tocBtn->setMaximumHeight(42);
        setCssProperty(tocBtn, "btn-faq-options");
        tocBtnGroup->addButton(tocBtn);
        tocLay->addWidget(tocBtn);

        // Card container
        QWidget* card = new QWidget(ui->scrollAreaWidgetContents);
        card->setObjectName(QString("faqCard_%1").arg(seed.num));
        QVBoxLayout* cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(18, 16, 18, 16);
        cardLay->setSpacing(10);

        // Header row: Badge + Title + Category Tag
        QHBoxLayout* hdrLay = new QHBoxLayout();
        hdrLay->setContentsMargins(0, 0, 0, 0);
        hdrLay->setSpacing(12);

        QLabel* badge = new QLabel(QString("%1").arg(seed.num, 2, 10, QLatin1Char('0')), card);
        badge->setFixedSize(30, 30);
        badge->setAlignment(Qt::AlignCenter);

        QLabel* titleLbl = new QLabel(seed.title, card);
        titleLbl->setWordWrap(true);
        titleLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        titleLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

        QLabel* catTag = new QLabel(seed.catLabel, card);
        catTag->setAlignment(Qt::AlignCenter);
        catTag->setMinimumHeight(24);
        catTag->setMaximumHeight(24);

        hdrLay->addWidget(badge, 0, Qt::AlignTop);
        hdrLay->addWidget(titleLbl, 1, Qt::AlignVCenter);
        hdrLay->addWidget(catTag, 0, Qt::AlignTop);
        cardLay->addLayout(hdrLay);

        // Quick Answer callout strip
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

        // Rich body label
        QLabel* bodyLbl = new QLabel(card);
        bodyLbl->setWordWrap(true);
        bodyLbl->setTextFormat(Qt::RichText);
        bodyLbl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        bodyLbl->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
        bodyLbl->setOpenExternalLinks(true);
        cardLay->addWidget(bodyLbl);

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
        item.badge = badge;
        item.titleLabel = titleLbl;
        item.catTagLabel = catTag;
        item.tldrBox = tldrBox;
        item.tldrLabel = tldrLbl;
        item.bodyLabel = bodyLbl;
        item.tocButton = tocBtn;
        item.bodyBuilder = seed.bodyBuilder;
        faqItems.push_back(item);

        connect(tocBtn, &QPushButton::clicked, this, [this, card]() {
            onFaqClicked(card);
        });
    }

    tocLay->addStretch(1);
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
    QPushButton* btn = faqItems[pos - 1].tocButton;
    if (btn) {
        btn->setChecked(true);
        Q_EMIT btn->clicked();
    }
}

void SettingsFaqWidget::onFaqClicked(const QWidget* const widget)
{
    if (widget && ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
        isProgrammaticScroll = true;
        ui->scrollAreaFaq->verticalScrollBar()->setValue(widget->y());
        isProgrammaticScroll = false;
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
        if (!item.card || !item.card->isVisible()) {
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

    for (const auto& item : faqItems) {
        const bool catMatches = (activeCategory == QStringLiteral("all")) ||
                                (item.categoryKey == activeCategory);
        bool searchMatches = query.isEmpty();
        if (catMatches && !searchMatches) {
            const QString plainBody = item.bodyLabel
                ? QTextDocumentFragment::fromHtml(item.bodyLabel->text()).toPlainText()
                : QString();
            searchMatches = item.title.contains(query, Qt::CaseInsensitive) ||
                            item.tldr.contains(query, Qt::CaseInsensitive) ||
                            item.categoryLabel.contains(query, Qt::CaseInsensitive) ||
                            item.tocTitle.contains(query, Qt::CaseInsensitive) ||
                            item.keywords.contains(query, Qt::CaseInsensitive) ||
                            plainBody.contains(query, Qt::CaseInsensitive);
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
            if (item.tocButton && item.tocButton->isVisible() && item.tocButton->isChecked()) {
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
}

void SettingsFaqWidget::applyThemeStyles(bool isLight)
{
    const QString cardBg        = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#182132");
    const QString cardBorder    = isLight ? QStringLiteral("#DDE5F2") : QStringLiteral("#28354D");
    const QString titleColor    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#E2ECFA");
    const QString mutedText     = isLight ? QStringLiteral("#475569") : QStringLiteral("#94A3B8");
    const QString groupColor    = isLight ? QStringLiteral("#5576B2") : QStringLiteral("#7D98CC");
    const QString badgeBg       = isLight ? QStringLiteral("#EEF3FB") : QStringLiteral("#22304A");
    const QString badgeText     = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#8FAEE6");
    const QString badgeBorder   = isLight ? QStringLiteral("#D0DCF0") : QStringLiteral("#34496D");
    const QString tagBg         = isLight ? QStringLiteral("#F1F5FB") : QStringLiteral("#1E2A40");
    const QString tagText       = isLight ? QStringLiteral("#4A679E") : QStringLiteral("#A5BEEA");
    const QString tagBorder     = isLight ? QStringLiteral("#D6E1F2") : QStringLiteral("#314262");
    const QString tldrBg        = isLight ? QStringLiteral("#F4F7FC") : QStringLiteral("#121A2A");
    const QString tldrAccent    = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#F5897C");
    const QString tldrText      = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#E2E8F0");
    const QString searchBg      = isLight ? QStringLiteral("#FFFFFF") : QStringLiteral("#141C2B");
    const QString searchBorder  = isLight ? QStringLiteral("#CBD6E6") : QStringLiteral("#2E3D59");
    const QString searchText    = isLight ? QStringLiteral("#1E293B") : QStringLiteral("#F1F5F9");
    const QString pillBg        = isLight ? QStringLiteral("#EEF3FA") : QStringLiteral("#192338");
    const QString pillBorder    = isLight ? QStringLiteral("#C9D6EB") : QStringLiteral("#2E3F5E");
    const QString pillText      = isLight ? QStringLiteral("#334155") : QStringLiteral("#CBD5E1");
    const QString pillActiveBg  = isLight ? QStringLiteral("#3E5C94") : QStringLiteral("#5576B2");
    const QString pillHoverBg   = isLight ? QStringLiteral("#DCE6F7") : QStringLiteral("#23324E");

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

    const QString tocHdrStyle = QString(
        "QLabel { color: %1; font-size: 11px; font-weight: 700; letter-spacing: 0.6px; }"
    ).arg(groupColor);
    for (const auto& hdr : tocHeaders) {
        if (hdr.label) {
            hdr.label->setStyleSheet(tocHdrStyle);
        }
    }

    const QString badgeStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 15px; font-size: 12.5px; font-weight: 700; }"
    ).arg(badgeBg, badgeText, badgeBorder);
    const QString titleStyle = QString(
        "QLabel { color: %1; font-size: 17px; font-weight: 700; }"
    ).arg(titleColor);
    const QString tagStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 12px; padding: 0 10px; font-size: 11.5px; font-weight: 600; }"
    ).arg(tagBg, tagText, tagBorder);
    const QString tldrTextStyle = QString(
        "QLabel { color: %1; font-size: 13px; }"
    ).arg(tldrText);

    for (const auto& item : faqItems) {
        if (item.card) {
            item.card->setStyleSheet(QString(
                "QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
            ).arg(item.card->objectName(), cardBg, cardBorder));
        }
        if (item.badge)       item.badge->setStyleSheet(badgeStyle);
        if (item.titleLabel)  item.titleLabel->setStyleSheet(titleStyle);
        if (item.catTagLabel) item.catTagLabel->setStyleSheet(tagStyle);
        if (item.tldrBox) {
            item.tldrBox->setStyleSheet(QString(
                "QFrame#%1 { background-color: %2; border: 1px solid %3; border-left: 3px solid %4; border-radius: 6px; }"
            ).arg(item.tldrBox->objectName(), tldrBg, cardBorder, tldrAccent));
        }
        if (item.tldrLabel)   item.tldrLabel->setStyleSheet(tldrTextStyle);
        if (item.bodyLabel && item.bodyBuilder) {
            item.bodyLabel->setText(item.bodyBuilder(isLight));
        }
    }
}

void SettingsFaqWidget::changeTheme(bool isLightTheme, QString& theme)
{
    PWidget::changeTheme(isLightTheme, theme);
    applyThemeStyles(isLightTheme);
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
