// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsfaqwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsfaqwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "chainparams.h"
#include "clientversion.h"
#include "version.h"
#include <QDesktopServices>
#include <QLineEdit>
#include <QLocale>
#include <QMetaObject>
#include <QScrollBar>
#include <QTextDocumentFragment>
#include <QUrl>
#include "qt/kristatech/qtutils.h"

SettingsFaqWidget::SettingsFaqWidget(KRISTATECHGUI* _window, QWidget *parent) :
    PWidget(_window, parent),
    ui(new Ui::SettingsFaqWidget)
{
    ui->setupUi(this);
    this->setStyleSheet(_window ? _window->styleSheet() : QString());

    ui->verticalLayoutcont->setContentsMargins(0, 0, 0, 0);
    ui->container->setStyleSheet("#container { background: transparent; border: none; }");
    setCssProperty(ui->labelTitle, "text-title-faq");
    setCssProperty(ui->labelWebLink, "text-faq-weblink");
    ui->pushButtonExit->setVisible(false);

    searchEdit = new QLineEdit(this);
    searchEdit->setObjectName("faqSearchEdit");
    searchEdit->setPlaceholderText(tr("Search FAQ..."));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(220);
    searchEdit->setMaximumWidth(320);
    searchEdit->setMinimumHeight(34);
    searchEdit->setMaximumHeight(34);
    ui->horizontalLayout_3->addWidget(searchEdit);
    connect(searchEdit, &QLineEdit::textChanged, this, &SettingsFaqWidget::onSearchTextChanged);

    // Content
    setCssProperty({
           ui->labelNumber_Intro,
           ui->labelNumber_UnspendablePIV,
           ui->labelNumber_Stake,
           ui->labelNumber_Support,
           ui->labelNumber_Masternode,
           ui->labelNumber_MNController
        }, "container-number-faq");

    setCssProperty({
              ui->labelSubtitle_Intro,
              ui->labelSubtitle_UnspendablePIV,
              ui->labelSubtitle_Stake,
              ui->labelSubtitle_Support,
              ui->labelSubtitle_Masternode,
              ui->labelSubtitle_MNController
            }, "text-subtitle-faq");


    setCssProperty({
              ui->labelContent_Intro,
              ui->labelContent_UnspendablePIV,
              ui->labelContent_Stake,
              ui->labelContent_Support,
              ui->labelContent_Masternode,
              ui->labelContent_MNController
            }, "text-content-faq");


    setCssProperty({
              ui->pushButton_Intro,
              ui->pushButton_UnspendablePIV,
              ui->pushButton_Stake,
              ui->pushButton_Support,
              ui->pushButton_Masternode,
              ui->pushButton_MNController
            }, "btn-faq-options");

    ui->labelContent_Support->setOpenExternalLinks(true);
    ui->labelContent_Intro->setOpenExternalLinks(true);

    // Accurate KristaTech FAQ & Protocol Guide
    const QString verStr = QString("%1.%2.%3.%4")
        .arg(CLIENT_VERSION_MAJOR).arg(CLIENT_VERSION_MINOR).arg(CLIENT_VERSION_REVISION).arg(CLIENT_VERSION_BUILD);
    const QString taprootHrp = QString::fromStdString(Params().Bech32HRP()) + QStringLiteral("1p...");
    const int modelDHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight;
    ui->labelTitle->setText(tr("KristaTech FAQ & Protocol Guide"));
    ui->pushButtonWebLink->setText("krista.kristalteknoloji.com");

    ui->pushButton_Intro->setText(tr("1) What is KRISTA?"));
    ui->labelSubtitle_Intro->setText(tr("What is KRISTA (v%1)?").arg(verStr));
    ui->labelContent_Intro->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "<b>KRISTA</b> (Protocol <code>%1</code>) is a hybrid Layer-1 blockchain combining Multi-Puzzle Aggregation (<b>MPA / ADAM</b>), "
        "Proof of Stake (<b>PoS</b>), rotating BLS Long-Living Masternode Quorums (<b>LLMQ</b> dynamic 3-of-5 up to 8-of-15 BFT finality), and "
        "<b>MESCAL v2 Taproot</b> (<code>%2</code>) smart contracts with a <b>30-second</b> block target."
        "</p><p align=\"justify\">"
        "From Block 2 onward, <b>7.0%</b> of each block subsidy (<b>17 KRISTA</b> base, <b>-2.15%</b> every 259,200 blocks / 90 days) funds ecosystem development and <b>93.0%</b> enters the Net Consensus Reward Pool — "
        "distributed under Model D (Height &ge; %3, with <code>UPGRADE_FAIRNESS</code> from Block 1) across Passive Masternodes (<b>35%</b>), Equal Dual-Track MPA Validators (<b>40% / 11</b> equal per solved seat), "
        "the Block Producer / Coordinator (<b>15%</b>; PoS splits <b>12%</b> Staker + <b>3%</b> Coordinator + symmetric fees), and the active BLS LLMQ Quorum (<b>10%</b> signer-only, 1-block lag)."
        "</p></body></html>")
        .arg(PROTOCOL_VERSION)
        .arg(taprootHrp)
        .arg(QLocale().toString(modelDHeight)));

    ui->pushButton_UnspendablePIV->setText(tr("2) Why are coins Locked / Pending?"));
    ui->labelSubtitle_UnspendablePIV->setText(tr("Why do balances appear under Locked / Pending?"));
    ui->labelContent_UnspendablePIV->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "Hover over the <b>Locked / Pending</b> badge in the TopBar anytime for an exact breakdown:"
        "</p><ul>"
        "<li><b>Standard Transfers (1 Confirmation):</b> Incoming transfers become spendable after 1 block (~30 seconds).</li>"
        "<li><b>Coinbase / Stake / Validator Maturity (100 Confirmations):</b> Newly mined, staked, or validator-earned rewards mature after 100 blocks (~50 minutes). Miner Hub lock inputs also require &ge; 100 confirmations.</li>"
        "<li><b>Refundable Collateral &amp; CLTV Time-Locks:</b> Masternode collateral (<b>4,200 KRISTA</b>) stays 100% inside your own wallet while active until unlocked or spent, whereas Miner Hub Coin-Lock (<b>250 KRISTA</b>) and Funded PoW-Lock (<b>10 KRISTA</b>) stay 100% inside your own wallet and unlock automatically after the 2,880-block (~24h, 2,900-block wallet buffer) CLTV window expires.</li>"
        "</ul></body></html>"));

    ui->pushButton_Stake->setText(tr("3) Staking && Miner Hub Guide"));
    ui->labelSubtitle_Stake->setText(tr("How do PoS Staking and the Miner Hub work?"));
    ui->labelContent_Stake->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "<b>PoS Staking:</b> Synchronize your wallet, hold UTXOs with at least <b>600 confirmations</b> and <b>1 hour</b> minimum stake age on Mainnet (<b>30 confirmations</b> on Testnet), and keep the wallet online and unlocked for staking (CLTV <b>PoL up to 3x</b> UTXOs self-renew principal &amp; script automatically; PoS blocks share <b>12%</b> Staker + <b>3%</b> Signing Coordinator + symmetric tx fees)."
        "</p><p align=\"justify\">"
        "<b>Miner Hub (Dual-Track 7 / 1 / 3 &amp; Proportional Capital Coordinator VRF 4,200 : 250 : 10, 75% MN Floor):</b> Open the <b>MINER HUB</b> tab in the TopBar to participate in MPA puzzle validation (17 uniform single-pass algos, 33B compressed keys):"
        "</p><ul>"
        "<li><b>Coin-Lock (250 KRISTA):</b> Dedicated 1-seat slot earning equal validator share (<code>40% / 11</code> Model D, <code>75% / 11</code> Pre-Model D) + <b>250</b> Coordinator VRF weight.</li>"
        "<li><b>Funded PoW-Lock (10 KRISTA + PoW):</b> Competes for 2 Funded PoW seats earning equal validator share (<code>40% / 11</code> Model D, <code>75% / 11</code> Pre-Model D) + <b>10</b> Coordinator VRF weight.</li>"
        "<li><b>Zero-Coin PoW (0 KRISTA + 15-bit HashX11KVS PoW):</b> Up to <b>240</b> bootstrap slots with permanent <b>1-seat Open-Entry</b> admission (equal validator share, 0 Coordinator weight; active at block <code>2,500</code> on Mainnet).</li>"
        "</ul></body></html>"));

    ui->pushButton_Support->setText(tr("4) Official Explorer && Links"));
    ui->labelSubtitle_Support->setText(tr("Official Website, Live Block Explorer & Diagnostics"));

    ui->pushButton_Masternode->setText(tr("5) Masternodes && LLMQ Finality"));
    ui->labelSubtitle_Masternode->setText(tr("What is a KristaTech Masternode & BLS LLMQ Quorum?"));
    ui->labelContent_Masternode->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "A Masternode locks <b>4,200 KRISTA</b> in a single UTXO (never leaving your Cold Wallet) to provide 24/7 consensus and finality:"
        "</p><ul>"
        "<li><b>35% Passive Masternode Queue</b> deterministic block payouts.</li>"
        "<li><b>40% / 11 Equal MPA Validator Seats</b> (up to 7 MN seats per block, equal ~3.636% of net pool per solved seat).</li>"
        "<li><b>10% Signer-Only BLS LLMQ Finality</b> (rotating quorum scaling dynamically from 3-of-5 up to 8-of-15 BFT majority by active MN count; <code>10% / N</code> paid strictly to previous-block signers with <code>-5/+5</code> deterministic PoSe scoring).</li>"
        "<li><b>4,200 Coordinator VRF Weight</b> (proportional locked-capital tickets in the <code>4,200 : 250 : 10</code> Coordinator lottery with a <code>75%</code> MN floor).</li>"
        "</ul><p align=\"justify\">"
        "<b>KTDIP3</b> Deterministic Masternodes (<code>ProRegTx</code>) are tracked deterministically in <code>evodb</code> with PoSe health scoring &amp; <code>ProUpServTx</code> revival (with pure Hot/Cold <code>blsPubKeyOperator</code> separation active from block <code>1</code> on Mainnet &amp; Testnet)."
        "</p></body></html>"));

    ui->pushButton_MNController->setText(tr("6) Multi-MN (activemasternode.conf)"));
    ui->labelSubtitle_MNController->setText(tr("Cold Controller vs Single-IP Multi-Masternode Setup"));
    ui->labelContent_MNController->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "Your <b>Cold Controller Wallet</b> holds the 4,200 KRISTA collateral UTXOs safely and registers them on-chain with 1-click <b>Create Masternode Controller</b> + <b>Start</b> in the <b>MASTERNODES</b> tab (or via CLI <code>protx register</code> / <code>protx bls_generate</code>).<br/><br/>"
        "On your online Linux VPS / Hot Daemon, KristaTech supports <b>Single-IPv4:27999 Multi-Masternode</b> hosting via <code>activemasternode.conf</code> "
        "(one <code>&lt;alias&gt; &lt;privkey&gt; &lt;bls_privkey_hex&gt;</code> line per node; or <code>masternodeblsprivkey=...</code> for single-node KTDIP3). Never put <code>masternodeprivkey=...</code> inside <code>kristatech.conf</code>. "
        "Both files are accessible in 1 click from the TopBar <b>Config Files</b> menu."
        "</p></body></html>"));

    // Exit button (hidden when embedded in Settings)
    setCssProperty(ui->pushButtonExit, "btn-faq-exit");

    // Web Link
    setCssProperty(ui->pushButtonWebLink, "btn-faq-web");
    setCssProperty(ui->containerButtons, "container-faq-buttons");

    // Setup card spacing & no-results placeholder
    ui->verticalLayout_7->setSpacing(12);
    ui->verticalLayout_7->setContentsMargins(4, 4, 8, 12);
    labelNoResults = new QLabel(tr("No matching FAQ entries found."), ui->scrollAreaWidgetContents);
    labelNoResults->setAlignment(Qt::AlignCenter);
    labelNoResults->setVisible(false);
    ui->verticalLayout_7->addWidget(labelNoResults);
    ui->verticalLayout_7->addStretch(1);

    applyThemeStyles(isLightTheme());

    // Buttons
    connect(ui->pushButtonWebLink, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://krista.kristalteknoloji.com"));
    });
    connect(ui->pushButton_Intro, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Intro);});
    connect(ui->pushButton_UnspendablePIV, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_UnspendablePIV);});
    connect(ui->pushButton_Stake, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Stake);});
    connect(ui->pushButton_Support, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Support);});
    connect(ui->pushButton_Masternode, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Masternode);});
    connect(ui->pushButton_MNController, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_MNController);});
}

void SettingsFaqWidget::showEvent(QShowEvent *event)
{
    PWidget::showEvent(event);
    if (pos >= 1 && pos <= static_cast<int>(getButtons().size())) {
        QPushButton* btn = getButtons()[pos - 1];
        QMetaObject::invokeMethod(btn, "setChecked", Qt::QueuedConnection, Q_ARG(bool, true));
        QMetaObject::invokeMethod(btn, "clicked", Qt::QueuedConnection);
    }
}

void SettingsFaqWidget::setSection(int num)
{
    if (num < 1 || num > static_cast<int>(getButtons().size())) {
        pos = 0;
        if (ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
            ui->scrollAreaFaq->verticalScrollBar()->setValue(0);
        }
        return;
    }
    pos = num;
    QPushButton* btn = getButtons()[pos - 1];
    if (btn) {
        btn->setChecked(true);
        Q_EMIT btn->clicked();
    }
}

void SettingsFaqWidget::onFaqClicked(const QWidget* const widget)
{
    if (widget && ui->scrollAreaFaq && ui->scrollAreaFaq->verticalScrollBar()) {
        ui->scrollAreaFaq->verticalScrollBar()->setValue(widget->y());
    }
}

void SettingsFaqWidget::onSearchTextChanged(const QString& text)
{
    const QString query = text.trimmed();
    struct FaqEntry {
        QWidget* card;
        QLabel* title;
        QLabel* body;
        QPushButton* tocBtn;
    };
    const std::vector<FaqEntry> entries = {
        {ui->widget_Intro, ui->labelSubtitle_Intro, ui->labelContent_Intro, ui->pushButton_Intro},
        {ui->widget_UnspendablePIV, ui->labelSubtitle_UnspendablePIV, ui->labelContent_UnspendablePIV, ui->pushButton_UnspendablePIV},
        {ui->widget_Stake, ui->labelSubtitle_Stake, ui->labelContent_Stake, ui->pushButton_Stake},
        {ui->widget_Support, ui->labelSubtitle_Support, ui->labelContent_Support, ui->pushButton_Support},
        {ui->widget_Masternode, ui->labelSubtitle_Masternode, ui->labelContent_Masternode, ui->pushButton_Masternode},
        {ui->widget_MNController, ui->labelSubtitle_MNController, ui->labelContent_MNController, ui->pushButton_MNController}
    };

    int visibleCount = 0;
    for (const auto& entry : entries) {
        bool matches = query.isEmpty();
        if (!matches) {
            const QString plainTitle = entry.title ? entry.title->text() : QString();
            const QString plainBody = entry.body ? QTextDocumentFragment::fromHtml(entry.body->text()).toPlainText() : QString();
            const QString btnText = entry.tocBtn ? entry.tocBtn->text() : QString();
            matches = plainTitle.contains(query, Qt::CaseInsensitive) ||
                      plainBody.contains(query, Qt::CaseInsensitive) ||
                      btnText.contains(query, Qt::CaseInsensitive);
        }
        if (entry.card) entry.card->setVisible(matches);
        if (entry.tocBtn) entry.tocBtn->setVisible(matches);
        if (matches) ++visibleCount;
    }
    if (labelNoResults) {
        labelNoResults->setVisible(visibleCount == 0);
    }
}

void SettingsFaqWidget::applyThemeStyles(bool isLight)
{
    const QString cardBg = isLight ? "#FFFFFF" : "#182132";
    const QString cardBorder = isLight ? "#DDE5F2" : "#28354D";
    const QString badgeBg = isLight ? "#EEF3FB" : "#22304A";
    const QString badgeText = isLight ? "#4A679E" : "#8FAEE6";
    const QString badgeBorder = isLight ? "#D0DCF0" : "#34496D";
    const QString searchBg = isLight ? "#FFFFFF" : "#141C2B";
    const QString searchBorder = isLight ? "#CBD6E6" : "#2E3D59";
    const QString searchText = isLight ? "#1E293B" : "#F1F5F9";
    const QString mutedText = isLight ? "#475569" : "#CBD5E1";

    const QString cardStyle = QString(
        "QWidget#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
    );
    for (QWidget* card : {
             ui->widget_Intro,
             ui->widget_UnspendablePIV,
             ui->widget_Stake,
             ui->widget_Support,
             ui->widget_Masternode,
             ui->widget_MNController}) {
        if (card) {
            card->setStyleSheet(cardStyle.arg(card->objectName(), cardBg, cardBorder));
            if (card->layout()) {
                card->layout()->setContentsMargins(14, 12, 16, 12);
            }
        }
    }

    const QString badgeStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; "
        "border-radius: 14px; font-weight: 600; }"
    ).arg(badgeBg, badgeText, badgeBorder);
    for (QLabel* badge : {
             ui->labelNumber_Intro,
             ui->labelNumber_UnspendablePIV,
             ui->labelNumber_Stake,
             ui->labelNumber_Support,
             ui->labelNumber_Masternode,
             ui->labelNumber_MNController}) {
        if (badge) {
            badge->setStyleSheet(badgeStyle);
        }
    }

    if (searchEdit) {
        searchEdit->setStyleSheet(QString(
            "QLineEdit#faqSearchEdit { background-color: %1; color: %2; border: 1px solid %3; "
            "border-radius: 8px; padding: 0 10px; font-size: 13px; }"
            "QLineEdit#faqSearchEdit:focus { border-color: #7695CD; }"
        ).arg(searchBg, searchText, searchBorder));
    }
    if (labelNoResults) {
        labelNoResults->setStyleSheet(QString("QLabel { color: %1; padding: 24px; font-size: 14px; }").arg(mutedText));
    }
    const QString portalLinkColor = isLight ? "#3E5C94" : "#9BB4E2";
    const QString explorerLinkColor = isLight ? "#C85A4D" : "#F5897C";
    if (ui->labelContent_Support) {
        ui->labelContent_Support->setText(tr(
            "<html><head/><body><p align=\"justify\">"
            "Official Portal: <a style='color: %1; font-weight: 600;' href='https://krista.kristalteknoloji.com'>https://krista.kristalteknoloji.com</a><br/>"
            "Live Block &amp; Consensus Explorer: <a style='color: %2; font-weight: 600;' href='https://explorer.kristalteknoloji.com'>https://explorer.kristalteknoloji.com</a><br/><br/>"
            "For local node diagnostics, open <b>Settings &rarr; RPC Debug Console</b> (or the TopBar <b>Debug Console</b> button) and click the 1-click RPC chips (<code>getmininginfo</code>, <code>getadamminers</code>, <code>listmasternodes</code>, <code>getactivequorum</code>)."
            "</p></body></html>").arg(portalLinkColor, explorerLinkColor));
    }
}

void SettingsFaqWidget::changeTheme(bool isLightTheme, QString& theme)
{
    PWidget::changeTheme(isLightTheme, theme);
    applyThemeStyles(isLightTheme);
}

std::vector<QPushButton*> SettingsFaqWidget::getButtons()
{
    return {
            ui->pushButton_Intro,
            ui->pushButton_UnspendablePIV,
            ui->pushButton_Stake,
            ui->pushButton_Support,
            ui->pushButton_Masternode,
            ui->pushButton_MNController
    };
}

SettingsFaqWidget::~SettingsFaqWidget()
{
    delete ui;
}


