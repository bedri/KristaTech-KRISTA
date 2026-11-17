// Copyright (c) 2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/settings/settingsfaqwidget.h"
#include "qt/kristatech/settings/forms/ui_settingsfaqwidget.h"
#include <QDesktopServices>
#include <QScrollBar>
#include <QMetaObject>
#include <QUrl>
#include "qt/kristatech/qtutils.h"

SettingsFaqWidget::SettingsFaqWidget(KRISTATECHGUI *parent) :
    QDialog(parent),
    ui(new Ui::SettingsFaqWidget)
{
    ui->setupUi(this);
    this->setAttribute(Qt::WA_StyledBackground, true);
    ui->container->setAttribute(Qt::WA_StyledBackground, true);
    this->setStyleSheet(parent->styleSheet());

    setCssProperty(ui->container, "container-faq");
    setCssProperty(ui->labelTitle, "text-title-faq");
    setCssProperty(ui->labelWebLink, "text-faq-weblink");

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

    // Accurate KristaTech v1.5.7.0 (Protocol 70983) FAQ & Protocol Guide
    ui->labelTitle->setText(tr("KristaTech FAQ & Protocol Guide"));
    ui->pushButtonWebLink->setText("kristalteknoloji.com");

    ui->pushButton_Intro->setText(tr("1) What is KRISTA?"));
    ui->labelSubtitle_Intro->setText(tr("What is KRISTA (v1.5.7.0)?"));
    ui->labelContent_Intro->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "<b>KRISTA</b> (Protocol <code>70983</code>) is a hybrid Layer-1 blockchain combining Multi-Puzzle Aggregation (<b>MPA / ADAM</b>), "
        "Proof of Stake (<b>PoS</b>), 5-member BLS Long-Living Masternode Quorums (<b>LLMQ 3-of-5</b> chain finality), and "
        "<b>MESCAL v2 Taproot</b> (<code>kt1p...</code>) smart contracts with a <b>30-second</b> block target."
        "</p><p align=\"justify\">"
        "From Block 2 onward, <b>7.0%</b> of each block subsidy funds ecosystem development and <b>93.0%</b> enters the Net Consensus Reward Pool — "
        "distributed under Model D (Height &ge; 2,200) across Passive Masternodes (<b>35%</b>), Dual-Track MPA Validators (<b>40%</b>), "
        "the Block Producer / Coordinator (<b>15%</b>), and the 5-member BLS LLMQ Quorum (<b>10%</b>)."
        "</p></body></html>"));

    ui->pushButton_UnspendablePIV->setText(tr("2) Why are coins Locked / Pending?"));
    ui->labelSubtitle_UnspendablePIV->setText(tr("Why do balances appear under Locked / Pending?"));
    ui->labelContent_UnspendablePIV->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "Hover over the <b>Locked / Pending</b> badge in the TopBar anytime for an exact breakdown:"
        "</p><ul>"
        "<li><b>Standard Transfers (6 Confirmations):</b> Incoming transfers become spendable after 6 blocks (~3 minutes).</li>"
        "<li><b>Coinbase / Stake / Validator Maturity (100 Confirmations):</b> Newly mined, staked, or validator-earned rewards mature after 100 blocks (~50 minutes). Miner Hub lock inputs also require &ge; 100 confirmations.</li>"
        "<li><b>Refundable Collateral &amp; CLTV Time-Locks:</b> Masternode collateral (<b>4,200 KRISTA</b>), Miner Hub Coin-Lock (<b>250 KRISTA</b>), and Funded PoW-Lock (<b>10 KRISTA</b>) stay 100% inside your own wallet and unlock automatically when released or after the 2,880-block (~24h, 2,900-block wallet buffer) CLTV window expires.</li>"
        "</ul></body></html>"));

    ui->pushButton_Stake->setText(tr("3) Staking && Miner Hub Guide"));
    ui->labelSubtitle_Stake->setText(tr("How do PoS Staking and the Miner Hub work?"));
    ui->labelContent_Stake->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "<b>PoS Staking:</b> Synchronize your wallet, hold UTXOs with at least <b>600 confirmations</b>, and keep the wallet online and unlocked for staking."
        "</p><p align=\"justify\">"
        "<b>Miner Hub (Dual-Track 7 / 1 / 3 &amp; Quadratic Coordinator VRF 65 : 16 : 4):</b> Open the <b>MINER HUB</b> tab in the TopBar to participate in MPA puzzle validation:"
        "</p><ul>"
        "<li><b>Coin-Lock (250 KRISTA):</b> Dedicated <b>8.00%</b> validator seat (1 seat/block) + <b>16</b> Quadratic Coordinator VRF tickets.</li>"
        "<li><b>Funded PoW-Lock (10 KRISTA + PoW):</b> Competes for 3 Open PoW seats (<b>4.00%</b> each) + <b>4</b> Quadratic Coordinator VRF tickets.</li>"
        "<li><b>Zero-Coin PoW (0 KRISTA + PoW):</b> Open bootstrap entry when funded pool &lt; 13 validators (<b>4.00%</b> seat share, 0 Coordinator tickets).</li>"
        "</ul></body></html>"));

    const QString portalLinkColor = isLightTheme() ? "#3E5C94" : "#9BB4E2";
    const QString explorerLinkColor = isLightTheme() ? "#C85A4D" : "#F5897C";
    ui->pushButton_Support->setText(tr("4) Official Explorer && Links"));
    ui->labelSubtitle_Support->setText(tr("Official Website, Live Block Explorer & Diagnostics"));
    ui->labelContent_Support->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "Official Portal: <a style='color: %1; font-weight: 600;' href='https://kristalteknoloji.com'>https://kristalteknoloji.com</a><br/>"
        "Live Block &amp; Consensus Explorer: <a style='color: %2; font-weight: 600;' href='https://explorer.kristalteknoloji.com'>https://explorer.kristalteknoloji.com</a><br/><br/>"
        "For local node diagnostics, open <b>Settings &rarr; Debug Console</b> and click the 1-click RPC chips (<code>getmininginfo</code>, <code>getadamminers</code>, <code>listmasternodes</code>, <code>getactivequorum</code>)."
        "</p></body></html>").arg(portalLinkColor, explorerLinkColor));

    ui->pushButton_Masternode->setText(tr("5) Masternodes && LLMQ Finality"));
    ui->labelSubtitle_Masternode->setText(tr("What is a KristaTech Masternode & BLS LLMQ Quorum?"));
    ui->labelContent_Masternode->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "A Masternode locks <b>4,200 KRISTA</b> in a single UTXO (never leaving your Cold Wallet) to provide 24/7 consensus and finality:"
        "</p><ul>"
        "<li><b>35% Passive Masternode Queue</b> deterministic block payouts.</li>"
        "<li><b>20% MPA Track-1 Validator Seats</b> (7 seats per block, ~2.857% of net pool each).</li>"
        "<li><b>10% BLS LLMQ Finality</b> (5-member rotating quorum signing 3-of-5 threshold finality, 2% each).</li>"
        "<li><b>65 Quadratic Coordinator VRF Tickets</b> (highest weight in the <code>65 : 16 : 4</code> Coordinator lottery).</li>"
        "</ul><p align=\"justify\">"
        "Active Masternodes maintain on-chain activity within a <b>2,880-block</b> window (automatic <code>SendOnChainPing</code> every ~1,440 blocks)."
        "</p></body></html>"));

    ui->pushButton_MNController->setText(tr("6) Multi-MN (activemasternode.conf)"));
    ui->labelSubtitle_MNController->setText(tr("Cold Controller vs Single-IP Multi-Masternode Setup"));
    ui->labelContent_MNController->setText(tr(
        "<html><head/><body><p align=\"justify\">"
        "Your <b>Cold Controller Wallet</b> holds the 4,200 KRISTA collateral UTXOs safely and registers them on-chain via <b>KTDIP3</b> (<code>protx register</code>) or legacy <code>masternode.conf</code>.<br/><br/>"
        "On your online Linux VPS / Hot Daemon, KristaTech supports <b>Single-IPv4:27999 Multi-Masternode</b> hosting via <code>activemasternode.conf</code> "
        "(one <code>&lt;alias&gt; &lt;privkey&gt; &lt;bls_privkey_hex&gt;</code> line per node; or <code>masternodeblsprivkey=...</code> for single-node KTDIP3). Never put <code>masternodeprivkey=...</code> inside <code>kristatech.conf</code>. "
        "Both files are accessible in 1 click from the TopBar <b>Config Files</b> menu."
        "</p></body></html>"));

    // Exit button
    setCssProperty(ui->pushButtonExit, "btn-faq-exit");

    // Web Link
    setCssProperty(ui->pushButtonWebLink, "btn-faq-web");
    setCssProperty(ui->containerButtons, "container-faq-buttons");

    // Buttons
    connect(ui->pushButtonExit, &QPushButton::clicked, this, &SettingsFaqWidget::close);
    connect(ui->pushButtonWebLink, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://kristalteknoloji.com"));
    });
    connect(ui->pushButton_Intro, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Intro);});
    connect(ui->pushButton_UnspendablePIV, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_UnspendablePIV);});
    connect(ui->pushButton_Stake, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Stake);});
    connect(ui->pushButton_Support, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Support);});
    connect(ui->pushButton_Masternode, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_Masternode);});
    connect(ui->pushButton_MNController, &QPushButton::clicked, [this](){onFaqClicked(ui->widget_MNController);});

    if (parent)
        connect(parent, &KRISTATECHGUI::windowResizeEvent, this, &SettingsFaqWidget::windowResizeEvent);
}

void SettingsFaqWidget::showEvent(QShowEvent *event)
{
    if (pos != 0) {
        QPushButton* btn = getButtons()[pos - 1];
        QMetaObject::invokeMethod(btn, "setChecked", Qt::QueuedConnection, Q_ARG(bool, true));
        QMetaObject::invokeMethod(btn, "clicked", Qt::QueuedConnection);
    }
}

void SettingsFaqWidget::setSection(int num)
{
    if (num < 1 || num > 10)
        return;
    pos = num;
}

void SettingsFaqWidget::onFaqClicked(const QWidget* const widget)
{
    ui->scrollAreaFaq->verticalScrollBar()->setValue(widget->y());
}

void SettingsFaqWidget::windowResizeEvent(QResizeEvent* event)
{
    QWidget* w = qobject_cast<QWidget*>(parent());
    this->resize(w->width(), w->height());
    this->move(QPoint(0, 0));
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


