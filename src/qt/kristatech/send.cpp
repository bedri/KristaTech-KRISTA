// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/send.h"

#include "addresstablemodel.h"
#include "base58.h"
#include "bitcoinunits.h"
#include "chainparams.h"
#include "clientmodel.h"
#include "coincontrol.h"
#include "guiutil.h"
#include "main.h"
#include "masternode.h"
#include "openuridialog.h"
#include "optionsmodel.h"
#include "qt/kristatech/addnewcontactdialog.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/guitransactionsutils.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/sendchangeaddressdialog.h"
#include "qt/kristatech/sendconfirmdialog.h"
#include "rpc/server.h"
#include "script/standard.h"
#include "wallet/wallet.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
#include <algorithm>

class AutoHeightScrollArea : public QScrollArea
{
public:
    using QScrollArea::QScrollArea;

    QSize contentSizeHint() const
    {
        const int baseW = QScrollArea::sizeHint().width();
        const QWidget* w = widget();
        if (!w) {
            return QSize(baseW, 220);
        }
        const QSize wHint = w->sizeHint();
        const QSize lHint = w->layout() ? w->layout()->sizeHint() : QSize();
        const int h = std::max(wHint.height(), lHint.height());
        return QSize(baseW, (h > 0 ? h : 220) + 2 * frameWidth());
    }

    QSize viewportSizeHint() const override
    {
        return contentSizeHint();
    }

    QSize sizeHint() const override
    {
        return contentSizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return QSize(200, 180);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        const bool handled = QScrollArea::eventFilter(obj, event);
        if (obj == widget() && event &&
            (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Resize)) {
            const int h = contentSizeHint().height();
            if (h != m_lastContentHeight) {
                m_lastContentHeight = h;
                updateGeometry();
            }
        }
        return handled;
    }

private:
    int m_lastContentHeight{-1};
};

static void applyChipDangerHoverStyle(QPushButton* btn, bool isLight)
{
    if (!btn) return;
    const QString bg = isLight ? "#FEF2F2" : "#2D1820";
    const QString fg = isLight ? "#B91C1C" : "#FCA5A5";
    const QString brd = isLight ? "#FCA5A5" : "#7F2933";
    const QString hov = isLight ? "#FEE2E2" : "#3E202B";
    const QString foc = isLight ? "#3E5B94" : "#93B4F0";
    btn->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 6px; padding: 5px 10px; font-size: 11px; font-weight: 600; text-align: left; }"
        "QPushButton:hover { background-color: %4; border-color: #EF4444; }"
        "QPushButton:focus { outline: none; border: 1.5px solid %5; }"
    ).arg(bg, fg, brd, hov, foc));
}

QPushButton* SendWidget::createToolRowButton(QWidget* parent,
                                             const QString& title,
                                             const QString& subtitle,
                                             const QString& defaultBadge,
                                             QLabel*& badgeLabelOut)
{
    QPushButton* btn = new QPushButton(parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::TabFocus);
    btn->setMinimumHeight(48);
    btn->setProperty("sendToolRow", true);

    QHBoxLayout* row = new QHBoxLayout(btn);
    row->setContentsMargins(12, 7, 12, 7);
    row->setSpacing(8);

    QVBoxLayout* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(1);

    QLabel* lblMain = new QLabel(title, btn);
    lblMain->setProperty("toolTitle", true);
    lblMain->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QLabel* lblSub = new QLabel(subtitle, btn);
    lblSub->setProperty("toolSub", true);
    lblSub->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    textCol->addWidget(lblMain);
    textCol->addWidget(lblSub);

    badgeLabelOut = new QLabel(defaultBadge, btn);
    badgeLabelOut->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    badgeLabelOut->setAlignment(Qt::AlignCenter);

    row->addLayout(textCol, 1);
    row->addWidget(badgeLabelOut, 0, Qt::AlignVCenter);
    return btn;
}

SendWidget::SendWidget(KRISTATECHGUI* parent) :
    PWidget(parent)
{
    QHBoxLayout* rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(22, 18, 22, 18);
    rootLayout->setSpacing(18);

    // =========================================================================
    // LEFT COLUMN: Header, Recipient Cards Scroll Area, Primary Action Bar
    // =========================================================================
    leftColumn = new QWidget(this);
    leftColumn->setObjectName("sendLeftColumn");
    QVBoxLayout* leftLayout = new QVBoxLayout(leftColumn);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(14);

    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(12);

    QVBoxLayout* titleCol = new QVBoxLayout();
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(3);

    lblTitle = new QLabel(tr("Send KRISTA"), leftColumn);
    lblSubtitle = new QLabel(tr("Send KRISTA quickly to any KT... or kt1p... address with live fee and safety checks."), leftColumn);
    lblSubtitle->setToolTip(tr("Supports standard Base58 (KT...) and Bech32m Taproot / MESCAL (kt1p...) addresses with automatic fee estimation."));
    lblSubtitle->setWordWrap(true);
    titleCol->addWidget(lblTitle);
    titleCol->addWidget(lblSubtitle);

    lblHeaderBalance = new QLabel(tr("Spendable: --"), leftColumn);
    lblHeaderBalance->setAlignment(Qt::AlignCenter);

    headerRow->addLayout(titleCol, 1);
    headerRow->addWidget(lblHeaderBalance, 0, Qt::AlignTop);
    leftLayout->addLayout(headerRow);

    scrollArea = new AutoHeightScrollArea(leftColumn);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidgetResizable(true);
    scrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    scrollArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    scrollContents = new QWidget(scrollArea);
    scrollContents->setObjectName(QStringLiteral("sendRecipientsScrollContents"));
    scrollContents->setStyleSheet("QWidget#sendRecipientsScrollContents { background: transparent; border: none; }");
    recipientsLayout = new QVBoxLayout(scrollContents);
    recipientsLayout->setContentsMargins(0, 0, 4, 0);
    recipientsLayout->setSpacing(0);
    scrollArea->setWidget(scrollContents);

    leftLayout->addWidget(scrollArea, 0);

    QHBoxLayout* bottomActions = new QHBoxLayout();
    bottomActions->setContentsMargins(0, 2, 0, 0);
    bottomActions->setSpacing(10);

    btnAddRecipient = new QPushButton(QStringLiteral("+ ") + tr("Add Recipient"), leftColumn);
    btnAddRecipient->setCursor(Qt::PointingHandCursor);
    btnAddRecipient->setFocusPolicy(Qt::TabFocus);
    btnAddRecipient->setMinimumHeight(40);
    btnAddRecipient->setToolTip(tr("Add another recipient output to this transaction"));

    btnClearAll = new QPushButton(tr("Clear All"), leftColumn);
    btnClearAll->setCursor(Qt::PointingHandCursor);
    btnClearAll->setFocusPolicy(Qt::TabFocus);
    btnClearAll->setMinimumHeight(40);
    btnClearAll->setToolTip(tr("Clear recipients and reset custom transaction options"));

    btnSend = new QPushButton(tr("Send KRISTA"), leftColumn);
    btnSend->setCursor(Qt::PointingHandCursor);
    btnSend->setFocusPolicy(Qt::TabFocus);
    btnSend->setMinimumHeight(44);
    btnSend->setMinimumWidth(200);

    bottomActions->addWidget(btnAddRecipient);
    bottomActions->addWidget(btnClearAll);
    bottomActions->addStretch();
    bottomActions->addWidget(btnSend);
    leftLayout->addLayout(bottomActions);
    leftLayout->addStretch(1);

    // =========================================================================
    // RIGHT COLUMN: Transaction Summary Card + Transaction Tools Card
    // =========================================================================
    rightColumn = new QFrame(this);
    rightColumn->setObjectName("sendRightColumn");
    rightColumn->setMinimumWidth(330);
    rightColumn->setMaximumWidth(390);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(14);

    // Card 1: Transaction Summary
    summaryCard = new QFrame(rightColumn);
    summaryCard->setObjectName("sendSummaryCard");
    summaryCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* sumLayout = new QVBoxLayout(summaryCard);
    sumLayout->setContentsMargins(16, 14, 16, 14);
    sumLayout->setSpacing(10);

    lblSummaryTitle = new QLabel(tr("Transaction Summary"), summaryCard);
    sumLayout->addWidget(lblSummaryTitle);

    QGridLayout* sumGrid = new QGridLayout();
    sumGrid->setContentsMargins(0, 0, 0, 0);
    sumGrid->setHorizontalSpacing(10);
    sumGrid->setVerticalSpacing(8);

    lblRecipientCountKey = new QLabel(tr("Recipients"), summaryCard);
    lblRecipientCountVal = new QLabel(QStringLiteral("1"), summaryCard);
    lblRecipientCountVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    lblTotalSendKey = new QLabel(tr("Total to Send"), summaryCard);
    lblAmountSend = new QLabel(QStringLiteral("0.00 KRISTA"), summaryCard);
    lblAmountSend->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    lblEstFeeKey = new QLabel(tr("Estimated Fee"), summaryCard);
    lblEstFeeVal = new QLabel(QStringLiteral("~0.0001 KRISTA"), summaryCard);
    lblEstFeeVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    sumGrid->addWidget(lblRecipientCountKey, 0, 0);
    sumGrid->addWidget(lblRecipientCountVal, 0, 1);
    sumGrid->addWidget(lblTotalSendKey, 1, 0);
    sumGrid->addWidget(lblAmountSend, 1, 1);
    sumGrid->addWidget(lblEstFeeKey, 2, 0);
    sumGrid->addWidget(lblEstFeeVal, 2, 1);
    sumLayout->addLayout(sumGrid);

    summaryDivider = new QFrame(summaryCard);
    summaryDivider->setFrameShape(QFrame::HLine);
    summaryDivider->setFixedHeight(1);
    sumLayout->addWidget(summaryDivider);

    QHBoxLayout* remRow = new QHBoxLayout();
    remRow->setContentsMargins(0, 0, 0, 0);
    remRow->setSpacing(8);
    lblTitleRemaining = new QLabel(tr("Unlocked Remaining"), summaryCard);
    lblAmountRemaining = new QLabel(QStringLiteral("--"), summaryCard);
    lblAmountRemaining->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    remRow->addWidget(lblTitleRemaining);
    remRow->addWidget(lblAmountRemaining, 1);
    sumLayout->addLayout(remRow);

    lblBalanceWarning = new QLabel(summaryCard);
    lblBalanceWarning->setWordWrap(true);
    lblBalanceWarning->setVisible(false);
    sumLayout->addWidget(lblBalanceWarning);

    activeOptionsBox = new QWidget(summaryCard);
    activeOptionsBox->setVisible(false);
    QVBoxLayout* activeLayout = new QVBoxLayout(activeOptionsBox);
    activeLayout->setContentsMargins(0, 4, 0, 0);
    activeLayout->setSpacing(6);

    btnChipCoinControl = new QPushButton(activeOptionsBox);
    btnChipCoinControl->setCursor(Qt::PointingHandCursor);
    btnChipCoinControl->setFocusPolicy(Qt::TabFocus);
    btnChipCoinControl->setToolTip(tr("Click to clear selected Coin Control inputs"));
    btnChipCoinControl->setVisible(false);

    btnChipChangeAddr = new QPushButton(activeOptionsBox);
    btnChipChangeAddr->setCursor(Qt::PointingHandCursor);
    btnChipChangeAddr->setFocusPolicy(Qt::TabFocus);
    btnChipChangeAddr->setToolTip(tr("Click to clear custom change address"));
    btnChipChangeAddr->setVisible(false);

    btnChipCustomFee = new QPushButton(activeOptionsBox);
    btnChipCustomFee->setCursor(Qt::PointingHandCursor);
    btnChipCustomFee->setFocusPolicy(Qt::TabFocus);
    btnChipCustomFee->setToolTip(tr("Click to reset back to recommended fee"));
    btnChipCustomFee->setVisible(false);

    activeLayout->addWidget(btnChipCoinControl);
    activeLayout->addWidget(btnChipChangeAddr);
    activeLayout->addWidget(btnChipCustomFee);
    sumLayout->addWidget(activeOptionsBox);

    rightLayout->addWidget(summaryCard);

    // Card 2: Transaction Tools
    toolsCard = new QFrame(rightColumn);
    toolsCard->setObjectName("sendToolsCard");
    toolsCard->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout* toolsLayout = new QVBoxLayout(toolsCard);
    toolsLayout->setContentsMargins(16, 14, 16, 14);
    toolsLayout->setSpacing(8);

    lblToolsTitle = new QLabel(tr("Transaction Tools"), toolsCard);
    toolsLayout->addWidget(lblToolsTitle);

    btnToolFee = createToolRowButton(toolsCard,
                                     tr("Fee Settings"),
                                     tr("Recommended or custom rate per kB"),
                                     tr("Normal"),
                                     lblBadgeFee);
    btnToolCoinControl = createToolRowButton(toolsCard,
                                             tr("Coin Control"),
                                             tr("Select exact UTXOs as inputs"),
                                             tr("Auto"),
                                             lblBadgeCoinControl);
    btnToolChangeAddr = createToolRowButton(toolsCard,
                                            tr("Change Address"),
                                            tr("Specify custom address for change"),
                                            tr("Default"),
                                            lblBadgeChangeAddr);
    btnToolUri = createToolRowButton(toolsCard,
                                     tr("Open Payment URI"),
                                     tr("Parse a kristatech: payment request"),
                                     tr("Parse"),
                                     lblBadgeUri);
    btnToolPsbt = createToolRowButton(toolsCard,
                                      tr("Sign / Broadcast PSBT"),
                                      tr("Inspect, sign, or finalize BIP174 PSBT"),
                                      tr("BIP174"),
                                      lblBadgePsbt);
    btnToolContacts = createToolRowButton(toolsCard,
                                          tr("Address Book"),
                                          tr("Browse and manage saved contacts"),
                                          tr("Open"),
                                          lblBadgeContacts);

    toolsLayout->addWidget(btnToolCoinControl);
    toolsLayout->addWidget(btnToolFee);
    toolsLayout->addWidget(btnToolChangeAddr);
    toolsLayout->addWidget(btnToolUri);
    toolsLayout->addWidget(btnToolPsbt);
    toolsLayout->addWidget(btnToolContacts);

    btnResetOptions = new QPushButton(tr("Reset Custom Options"), toolsCard);
    btnResetOptions->setCursor(Qt::PointingHandCursor);
    btnResetOptions->setFocusPolicy(Qt::TabFocus);
    btnResetOptions->setMinimumHeight(34);
    btnResetOptions->setToolTip(tr("Reset Coin Control, Change Address, and Custom Fee to defaults"));
    toolsLayout->addSpacing(4);
    toolsLayout->addWidget(btnResetOptions);

    rightLayout->addWidget(toolsCard);
    rightLayout->addStretch(1);

    rootLayout->addWidget(leftColumn, 17);
    rootLayout->addWidget(rightColumn, 9);

    coinControlDialog = new CoinControlDialog();

    connect(btnToolFee, &QPushButton::clicked, this, &SendWidget::onChangeCustomFeeClicked);
    connect(btnToolCoinControl, &QPushButton::clicked, this, &SendWidget::onCoinControlClicked);
    connect(btnToolChangeAddr, &QPushButton::clicked, this, &SendWidget::onChangeAddressClicked);
    connect(btnToolUri, &QPushButton::clicked, this, &SendWidget::onOpenUriClicked);
    connect(btnToolPsbt, &QPushButton::clicked, this, &SendWidget::onPsbtClicked);
    connect(btnToolContacts, &QPushButton::clicked, this, &SendWidget::onContactsOptionClicked);
    connect(btnResetOptions, &QPushButton::clicked, this, [this]() {
        onResetCustomOptions(false);
        onResetSettings();
        refreshAmounts();
    });

    connect(btnChipCoinControl, &QPushButton::clicked, this, [this]() {
        if (coinControlDialog && coinControlDialog->coinControl) {
            coinControlDialog->coinControl->UnSelectAll();
        }
        refreshAmounts();
    });
    connect(btnChipChangeAddr, &QPushButton::clicked, this, [this]() {
        if (coinControlDialog && coinControlDialog->coinControl) {
            coinControlDialog->coinControl->destChange = CNoDestination();
        }
        refreshAmounts();
    });
    connect(btnChipCustomFee, &QPushButton::clicked, this, [this]() {
        onResetSettings();
        refreshAmounts();
    });

    connect(btnSend, &QPushButton::clicked, this, &SendWidget::onSendClicked);
    connect(btnAddRecipient, &QPushButton::clicked, this, &SendWidget::onAddEntryClicked);
    connect(btnClearAll, &QPushButton::clicked, this, [this]() { clearAll(true); });

    addEntry();
    setCustomFeeSelected(false);
    applyTheme(isLightTheme());
}

void SendWidget::applyTheme(bool isLight)
{
    const QString cardBg     = isLight ? "#FFFFFF" : "#131B2E";
    const QString cardBrd    = isLight ? "#CBD5E1" : "#253554";
    const QString titleCol   = isLight ? "#1E293B" : "#F1F5F9";
    const QString subCol     = isLight ? "#475569" : "#CBD5E1";
    const QString keyCol     = isLight ? "#475569" : "#B8C7DC";
    const QString divCol     = isLight ? "#E2E8F0" : "#243452";
    const QString secBg      = isLight ? "#F1F5F9" : "#1C2942";
    const QString secFg      = isLight ? "#1E293B" : "#E2E8F0";
    const QString secBrd     = isLight ? "#CBD5E1" : "#33486E";
    const QString secHov     = isLight ? "#E2E8F0" : "#26385A";
    const QString toolBg     = isLight ? "#F8FAFC" : "#0F172A";
    const QString toolHov    = isLight ? "#EEF3FA" : "#18243E";
    const QString toolBrd    = isLight ? "#E2E8F0" : "#233354";
    const QString balBg      = isLight ? "#EEF3FA" : "#1B2B4A";
    const QString balFg      = isLight ? "#3E5B94" : "#A8C1EE";
    const QString balBrd     = isLight ? "#B8CBE8" : "#3B5998";

    if (lblTitle) {
        lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 22px; font-weight: 500; background: transparent; border: none; }").arg(titleCol));
    }
    if (lblSubtitle) {
        lblSubtitle->setStyleSheet(QString("QLabel { color: %1; font-size: 13px; font-weight: 400; background: transparent; border: none; }").arg(subCol));
    }
    if (lblHeaderBalance) {
        lblHeaderBalance->setStyleSheet(QString(
            "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 7px 14px; font-size: 13px; font-weight: 600; }"
        ).arg(balBg, balFg, balBrd));
    }

    const QString cardStyle = QString(
        "QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 10px; }"
    );
    if (summaryCard) summaryCard->setStyleSheet(cardStyle.arg("sendSummaryCard", cardBg, cardBrd));
    if (toolsCard)   toolsCard->setStyleSheet(cardStyle.arg("sendToolsCard", cardBg, cardBrd));

    const QString secTitleStyle = QString("QLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; border: none; }").arg(titleCol);
    if (lblSummaryTitle) lblSummaryTitle->setStyleSheet(secTitleStyle);
    if (lblToolsTitle)   lblToolsTitle->setStyleSheet(secTitleStyle);

    const QString keyStyle = QString("QLabel { color: %1; font-size: 12px; font-weight: 500; background: transparent; border: none; }").arg(keyCol);
    const QString valStyle = QString("QLabel { color: %1; font-size: 13px; font-weight: 600; background: transparent; border: none; }").arg(titleCol);
    const QString strongValStyle = QString("QLabel { color: %1; font-size: 15px; font-weight: 500; background: transparent; border: none; }").arg(titleCol);

    if (lblRecipientCountKey) lblRecipientCountKey->setStyleSheet(keyStyle);
    if (lblTotalSendKey)      lblTotalSendKey->setStyleSheet(keyStyle);
    if (lblEstFeeKey)         lblEstFeeKey->setStyleSheet(keyStyle);
    if (lblTitleRemaining)    lblTitleRemaining->setStyleSheet(keyStyle);
    if (lblRecipientCountVal) lblRecipientCountVal->setStyleSheet(valStyle);
    if (lblEstFeeVal)         lblEstFeeVal->setStyleSheet(valStyle);
    if (lblAmountSend)        lblAmountSend->setStyleSheet(strongValStyle);
    if (summaryDivider)       summaryDivider->setStyleSheet(QString("QFrame { background-color: %1; border: none; }").arg(divCol));

    const QString focCol = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString secBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 8px; padding: 0 16px; font-size: 13px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
        "QPushButton:focus { outline: none; border: 1.5px solid %5; }"
    ).arg(secBg, secFg, secBrd, secHov, focCol);
    if (btnAddRecipient) btnAddRecipient->setStyleSheet(secBtnStyle);
    if (btnClearAll)     btnClearAll->setStyleSheet(secBtnStyle);
    if (btnResetOptions) btnResetOptions->setStyleSheet(secBtnStyle);

    if (btnSend) {
        btnSend->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: #FFFFFF; border: 1px solid %2; border-radius: 8px; padding: 0 22px; font-size: 14px; font-weight: 600; }"
            "QPushButton:hover { background-color: %3; border-color: %4; }"
            "QPushButton:pressed { background-color: %5; border-color: %6; }"
            "QPushButton:disabled { background-color: %7; color: %8; border-color: %7; }"
            "QPushButton:focus { outline: none; border: 2px solid %9; }"
        ).arg(isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#4A6AB0"),
              isLight ? QStringLiteral("#354F82") : QStringLiteral("#5B7CC4"),
              isLight ? QStringLiteral("#2F4A7D") : QStringLiteral("#5576B2"),
              isLight ? QStringLiteral("#2A4270") : QStringLiteral("#6B8CD4"),
              isLight ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94"),
              isLight ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94"),
              isLight ? QStringLiteral("#CBD5E1") : QStringLiteral("#1E293B"),
              isLight ? QStringLiteral("#64748B") : QStringLiteral("#475569"),
              isLight ? QStringLiteral("#1E3A8A") : QStringLiteral("#93B4F0")));
    }

    const QString toolBtnStyle = QString(
        "QPushButton[sendToolRow=\"true\"] { background-color: %1; border: 1px solid %2; border-radius: 8px; text-align: left; }"
        "QPushButton[sendToolRow=\"true\"]:hover { background-color: %3; border-color: #7695CD; }"
        "QPushButton[sendToolRow=\"true\"]:focus { outline: none; border: 1.5px solid %6; }"
        "QPushButton[sendToolRow=\"true\"] QLabel[toolTitle=\"true\"] { color: %4; font-size: 13px; font-weight: 600; background: transparent; }"
        "QPushButton[sendToolRow=\"true\"] QLabel[toolSub=\"true\"] { color: %5; font-size: 11px; background: transparent; }"
    ).arg(toolBg, toolBrd, toolHov, titleCol, subCol, focCol);

    for (QPushButton* tb : {btnToolFee, btnToolCoinControl, btnToolChangeAddr, btnToolUri, btnToolPsbt, btnToolContacts}) {
        if (tb) tb->setStyleSheet(toolBtnStyle);
    }

    applyChipDangerHoverStyle(btnChipCoinControl, isLight);
    applyChipDangerHoverStyle(btnChipChangeAddr, isLight);
    applyChipDangerHoverStyle(btnChipCustomFee, isLight);

    for (SendMultiRow* entry : entries) {
        if (entry) entry->applyTheme(isLight);
    }
    updateActiveOptionsUi();
    refreshAmounts();
}

CAmount SendWidget::estimateCurrentFee(CAmount& feeRatePerKbOut) const
{
    if (isCustomFeeSelected && currentCustomFeePerKb > 0) {
        feeRatePerKbOut = currentCustomFeePerKb;
    } else {
        CFeeRate est = mempool.estimateFee(5);
        if (est < CWallet::minTxFee) est = CWallet::minTxFee;
        feeRatePerKbOut = est.GetFeePerK();
    }
    const int inCount = (coinControlDialog && coinControlDialog->coinControl && coinControlDialog->coinControl->HasSelected())
        ? std::max(1, static_cast<int>(coinControlDialog->coinControl->QuantitySelected()))
        : 1;
    const int outCount = std::max(1, static_cast<int>(entries.size())) + 1;
    const unsigned int estBytes = static_cast<unsigned int>(10 + inCount * 148 + outCount * 34);
    return std::max(CWallet::GetRequiredFee(estBytes), CFeeRate(feeRatePerKbOut).GetFee(estBytes));
}

void SendWidget::updateActiveOptionsUi()
{
    const bool light = isLightTheme();
    const bool hasCoinCtrl = coinControlDialog && coinControlDialog->coinControl && coinControlDialog->coinControl->HasSelected();
    const bool hasChangeAddr = coinControlDialog && coinControlDialog->coinControl && IsValidDestination(coinControlDialog->coinControl->destChange);

    const QString idleBadgeStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
    ).arg(light ? "#E2E8F0" : "#1E293B",
          light ? "#475569" : "#CBD5E1",
          light ? "#CBD5E1" : "#334155");

    const QString activeBadgeStyle = QString(
        "QLabel { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
    ).arg(light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
          light ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"));

    if (lblBadgeUri)      lblBadgeUri->setStyleSheet(idleBadgeStyle);
    if (lblBadgePsbt)     lblBadgePsbt->setStyleSheet(idleBadgeStyle);
    if (lblBadgeContacts) lblBadgeContacts->setStyleSheet(idleBadgeStyle);

    if (lblBadgeFee) {
        if (isCustomFeeSelected) {
            lblBadgeFee->setText(tr("Custom"));
            lblBadgeFee->setStyleSheet(activeBadgeStyle);
        } else {
            lblBadgeFee->setText(tr("Normal"));
            lblBadgeFee->setStyleSheet(idleBadgeStyle);
        }
    }

    if (lblBadgeCoinControl) {
        if (hasCoinCtrl) {
            lblBadgeCoinControl->setText(tr("%1 UTXO").arg(coinControlDialog->coinControl->QuantitySelected()));
            lblBadgeCoinControl->setStyleSheet(activeBadgeStyle);
        } else {
            lblBadgeCoinControl->setText(tr("Auto"));
            lblBadgeCoinControl->setStyleSheet(idleBadgeStyle);
        }
    }

    if (lblBadgeChangeAddr) {
        if (hasChangeAddr) {
            lblBadgeChangeAddr->setText(tr("Custom"));
            lblBadgeChangeAddr->setStyleSheet(activeBadgeStyle);
        } else {
            lblBadgeChangeAddr->setText(tr("Default"));
            lblBadgeChangeAddr->setStyleSheet(idleBadgeStyle);
        }
    }

    if (btnChipCoinControl) {
        btnChipCoinControl->setVisible(hasCoinCtrl);
        if (hasCoinCtrl) {
            btnChipCoinControl->setText(tr("Coin Control: %1 UTXO selected  \xC3\x97").arg(coinControlDialog->coinControl->QuantitySelected()));
        }
    }
    if (btnChipChangeAddr) {
        btnChipChangeAddr->setVisible(hasChangeAddr);
        if (hasChangeAddr) {
            QString addr = QString::fromStdString(EncodeDestination(coinControlDialog->coinControl->destChange));
            QString shortAddr = addr.length() > 18 ? (addr.left(9) + "..." + addr.right(7)) : addr;
            btnChipChangeAddr->setText(tr("Custom Change: %1  \xC3\x97").arg(shortAddr));
        }
    }
    if (btnChipCustomFee) {
        btnChipCustomFee->setVisible(isCustomFeeSelected);
        if (isCustomFeeSelected) {
            btnChipCustomFee->setText(tr("Custom Fee: %1/kB  \xC3\x97").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, currentCustomFeePerKb)));
        }
    }

    const bool anyCustom = hasCoinCtrl || hasChangeAddr || isCustomFeeSelected;
    if (activeOptionsBox) activeOptionsBox->setVisible(anyCustom);
    if (btnResetOptions)  btnResetOptions->setEnabled(anyCustom);
}

static QString formatFeeWithPrecision(CAmount amt, int unit)
{
    QString formatted = BitcoinUnits::format(unit, amt, false, BitcoinUnits::separatorNever);
    const int dotIdx = formatted.indexOf(QLatin1Char('.'));
    if (dotIdx >= 0) {
        int lastNonZero = formatted.length() - 1;
        while (lastNonZero > dotIdx + 4 && formatted.at(lastNonZero) == QLatin1Char('0')) {
            --lastNonZero;
        }
        formatted.truncate(lastNonZero + 1);
    }
    return formatted + QLatin1Char(' ') + BitcoinUnits::name(unit);
}

void SendWidget::refreshAmounts()
{
    if (!walletModel || !walletModel->getOptionsModel()) return;

    CAmount total = 0;
    bool hasPositive = false;
    bool allSubtractFee = true;
    for (SendMultiRow* entry : entries) {
        if (!entry) continue;
        CAmount amount = entry->getAmountValue();
        if (amount > 0) {
            total += amount;
            hasPositive = true;
            if (!entry->isSubtractFeeFromAmount()) allSubtractFee = false;
        }
    }
    if (!hasPositive) allSubtractFee = false;

    nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
    const bool light = isLightTheme();
    const bool hasCoinCtrl = coinControlDialog && coinControlDialog->coinControl && coinControlDialog->coinControl->HasSelected();

    const CAmount unlockedBalance = walletModel->getUnlockedBalance();
    const CAmount available = hasCoinCtrl
        ? walletModel->getBalance(coinControlDialog->coinControl)
        : unlockedBalance;

    CAmount feeRatePerKb = 0;
    const CAmount estFee = estimateCurrentFee(feeRatePerKb);
    const CAmount estTotalOut = (total > 0 && !allSubtractFee) ? (total + estFee) : total;
    const CAmount remaining = available - estTotalOut;

    if (lblHeaderBalance) {
        lblHeaderBalance->setText(tr("Spendable: %1").arg(GUIUtil::formatBalance(unlockedBalance, nDisplayUnit)));
    }
    if (lblRecipientCountVal) {
        lblRecipientCountVal->setText(QString::number(entries.size()));
    }
    if (lblAmountSend) {
        lblAmountSend->setText(GUIUtil::formatBalance(total, nDisplayUnit));
    }
    if (lblEstFeeVal) {
        lblEstFeeVal->setText(QString("~%1 (%2/kB)")
            .arg(formatFeeWithPrecision(estFee, nDisplayUnit),
                 formatFeeWithPrecision(feeRatePerKb, nDisplayUnit)));
    }
    if (lblTitleRemaining) {
        lblTitleRemaining->setText(hasCoinCtrl ? tr("Remaining Selected UTXOs") : tr("Unlocked Remaining"));
    }
    if (lblAmountRemaining) {
        lblAmountRemaining->setText(GUIUtil::formatBalance(remaining, nDisplayUnit));
        const QString remColor = (remaining < 0)
            ? (light ? "#B93E33" : "#F08074")
            : (light ? "#1E293B" : "#F1F5F9");
        lblAmountRemaining->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: 500; background: transparent; border: none; }").arg(remColor));
    }

    if (lblBalanceWarning) {
        const QString warnFg = light ? "#B91C1C" : "#FCA5A5";
        lblBalanceWarning->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; border: none; }").arg(warnFg));
        if (total > available) {
            lblBalanceWarning->setText(tr("Insufficient balance for this transaction."));
            lblBalanceWarning->setVisible(true);
        } else if (total > 0 && !allSubtractFee && (total + estFee) > available) {
            lblBalanceWarning->setText(tr("Remaining balance may not cover the network fee — consider checking 'Subtract fee from amount' or using MAX."));
            lblBalanceWarning->setVisible(true);
        } else {
            lblBalanceWarning->setVisible(false);
        }
    }

    if (btnSend) {
        btnSend->setText(total > 0
            ? tr("Send %1").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, total, false, BitcoinUnits::separatorAlways))
            : tr("Send KRISTA"));
    }

    updateActiveOptionsUi();
}

void SendWidget::loadClientModel()
{
    if (clientModel) {
        connect(clientModel, &ClientModel::numBlocksChanged, this, [this]() {
            if (customFeeDialog) customFeeDialog->updateFee();
            refreshAmounts();
        }, Qt::UniqueConnection);
    }
}

void SendWidget::loadWalletModel()
{
    if (walletModel) {
        coinControlDialog->setModel(walletModel);
        if (walletModel->getOptionsModel()) {
            nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
            connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged,
                    this, [this](int) { refreshAmounts(); }, Qt::UniqueConnection);
        }

        connect(walletModel, &WalletModel::balanceChanged,
                this, [this](const interfaces::WalletBalances&) { refreshAmounts(); }, Qt::UniqueConnection);

        for (SendMultiRow *entry : entries) {
            if (entry) {
                entry->setWalletModel(walletModel);
            }
        }

        CAmount nCustomFee = DEFAULT_TRANSACTION_FEE;
        if (walletModel->getWalletCustomFee(nCustomFee)) {
            setCustomFeeSelected(true, nCustomFee);
        }

        refreshAmounts();
    }
}

void SendWidget::setRecipientAddress(const QString& address, const QString& label)
{
    if (address.trimmed().isEmpty()) return;
    SendMultiRow* target = nullptr;
    for (SendMultiRow* e : entries) {
        if (e && e->isClear()) {
            target = e;
            break;
        }
    }
    if (!target) {
        if (entries.size() >= MAX_SEND_POPUP_ENTRIES) {
            target = entries.last();
        } else {
            addEntry();
            target = entries.isEmpty() ? nullptr : entries.last();
        }
    }
    if (!target) return;
    target->setAddress(address.trimmed());
    if (!label.isEmpty()) {
        target->setLabel(label);
    }
    refreshAmounts();
    target->focusAmount();
}

void SendWidget::clearAll(bool fClearSettings)
{
    onResetCustomOptions(false);
    if (fClearSettings) onResetSettings();
    clearEntries();
    refreshAmounts();
}

void SendWidget::onResetSettings()
{
    if (customFeeDialog) customFeeDialog->clear();
    setCustomFeeSelected(false);
    if (walletModel) walletModel->setWalletCustomFee(false, DEFAULT_TRANSACTION_FEE);
}

void SendWidget::onResetCustomOptions(bool fRefreshAmounts)
{
    if (coinControlDialog && coinControlDialog->coinControl) {
        coinControlDialog->coinControl->SetNull();
    }
    updateActiveOptionsUi();
    if (fRefreshAmounts) {
        refreshAmounts();
    }
}

void SendWidget::clearEntries()
{
    for (SendMultiRow* row : entries) {
        if (recipientsLayout) recipientsLayout->removeWidget(row);
        if (row) {
            row->hide();
            row->deleteLater();
        }
    }
    entries.clear();
    focusedEntry = nullptr;
    addEntry();
    if (recipientsLayout) recipientsLayout->activate();
    if (scrollContents) scrollContents->adjustSize();
    if (scrollArea) scrollArea->updateGeometry();
}

void SendWidget::addEntry()
{
    if (entries.isEmpty()) {
        createEntry();
    } else {
        if (entries.length() >= MAX_SEND_POPUP_ENTRIES) {
            inform(tr("Maximum amount of outputs reached"));
            return;
        }
        if (entries.length() == 1 && entries.at(0)) {
            entries.at(0)->setNumber(1);
            entries.at(0)->setMultiMode(true);
        }
        SendMultiRow *sendMultiRow = createEntry();
        sendMultiRow->setNumber(entries.length());
        sendMultiRow->setMultiMode(true);
    }
    setFocusOnLastEntry();
    refreshAmounts();
}

SendMultiRow* SendWidget::createEntry()
{
    SendMultiRow *sendMultiRow = new SendMultiRow(this);
    if (this->walletModel) sendMultiRow->setWalletModel(this->walletModel);
    sendMultiRow->setNumber(entries.length() + 1);
    sendMultiRow->setMultiMode(!entries.isEmpty());
    entries.append(sendMultiRow);

    if (recipientsLayout) {
        recipientsLayout->addWidget(sendMultiRow);
    }
    if (isVisible()) {
        sendMultiRow->show();
    }
    if (recipientsLayout) recipientsLayout->activate();
    if (scrollContents) scrollContents->adjustSize();
    if (scrollArea) scrollArea->updateGeometry();

    connect(sendMultiRow, &SendMultiRow::onContactsClicked, this, &SendWidget::onContactsClicked);
    connect(sendMultiRow, &SendMultiRow::onSaveContactClicked, this, [this](SendMultiRow* entry) {
        focusedEntry = entry;
        onContactMultiClicked();
    });
    connect(sendMultiRow, &SendMultiRow::removeEntry, this, [this](SendMultiRow* entry) {
        focusedEntry = entry;
        onDeleteClicked();
    });
    connect(sendMultiRow, &SendMultiRow::onMenuClicked, this, &SendWidget::onMenuClicked);
    connect(sendMultiRow, &SendMultiRow::onMaxClicked, this, &SendWidget::onMaxClicked);
    connect(sendMultiRow, &SendMultiRow::onPercentClicked, this, &SendWidget::onPercentClicked);
    connect(sendMultiRow, &SendMultiRow::onValueChanged, this, &SendWidget::onValueChanged);
    connect(sendMultiRow, &SendMultiRow::onUriParsed, this, [this](const SendCoinsRecipient&) {
        refreshAmounts();
    });

    const bool isMulti = entries.size() > 1;
    for (SendMultiRow* row : entries) {
        if (row) row->setMultiMode(isMulti);
    }
    return sendMultiRow;
}

void SendWidget::onAddEntryClicked()
{
    for (SendMultiRow* entry : entries) {
        if (!entry || !entry->validate()) {
            inform(tr("Invalid entry, previous entries must be valid before add a new one"));
            return;
        }
    }
    addEntry();
}

void SendWidget::resizeEvent(QResizeEvent *event)
{
    resizeMenu();
    QWidget::resizeEvent(event);
}

void SendWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (scrollArea) scrollArea->updateGeometry();
    setFocusOnLastEntry();
    refreshAmounts();
}

void SendWidget::setFocusOnLastEntry()
{
    if (!entries.isEmpty() && entries.last()) entries.last()->setFocus();
}

void SendWidget::onSendClicked()
{
    if (!walletModel || !walletModel->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;
    for (SendMultiRow* entry : entries) {
        if (entry && entry->validate()) {
            recipients.append(entry->getValue());
        } else {
            inform(tr("Invalid entry"));
            return;
        }
    }

    if (recipients.isEmpty()) {
        inform(tr("No set recipients"));
        return;
    }

    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        inform(tr("Cannot send, wallet locked"));
        return;
    }

    if (send(recipients)) {
        updateEntryLabels(recipients);
    }
    setFocusOnLastEntry();
}

bool SendWidget::send(QList<SendCoinsRecipient> recipients)
{
    // Safety Guard 1: Permanent Burn Address Warning
    for (const SendCoinsRecipient& rec : recipients) {
        const std::string addrStd = rec.address.trimmed().toStdString();
        const bool fKnownBurn = Params().GetConsensus().mBurnAddresses.count(addrStd) > 0 ||
                                rec.address.trimmed().startsWith("KTburn", Qt::CaseInsensitive);
        if (fKnownBurn) {
            showHideOp(true);
            DefaultDialog* burnDlg = new DefaultDialog(window);
            burnDlg->setDanger(true);
            burnDlg->setText(
                tr("Permanent Coin Burn Warning"),
                tr("You are sending %1 to a protocol burn address:\n%2\n\n"
                   "Coins sent here are PERMANENTLY DESTROYED and can never be recovered.\n"
                   "Note: Proof-of-Burn (PoB) staking weight (5x boost) is credited strictly to the address of the FIRST input UTXO (vin[0]). "
                   "Use Coin Control to select your exact staking UTXO if burning for PoB.")
                    .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), rec.amount, false, BitcoinUnits::separatorAlways))
                    .arg(rec.address.trimmed()),
                tr("BURN PERMANENTLY"),
                tr("CANCEL")
            );
            burnDlg->adjustSize();
            bool confirmedBurn = openDialogWithOpaqueBackgroundY(burnDlg, window, 3, 5);
            burnDlg->deleteLater();
            if (!confirmedBurn) {
                return false;
            }
        }
    }

    // prepare transaction for getting txFee earlier
    WalletModelTransaction currentTransaction(recipients);
    WalletModel::SendCoinsReturn prepareStatus;

    prepareStatus = walletModel->prepareTransaction(currentTransaction, coinControlDialog->coinControl);

    if ((prepareStatus.status == WalletModel::TransactionTooLarge ||
         prepareStatus.status == WalletModel::TransactionCreationFailed) &&
        !coinControlDialog->coinControl->HasSelected()) {
        CAmount totalSend = 0;
        for (const SendCoinsRecipient& r : recipients) totalSend += r.amount;
        QString consolErr;
        if (GuiTransactionsUtils::OfferUtxoConsolidation(this, walletModel, totalSend, consolErr)) {
            prepareStatus = walletModel->prepareTransaction(currentTransaction, coinControlDialog->coinControl);
        } else if (!consolErr.isEmpty()) {
            inform(consolErr);
            return false;
        }
    }

    GuiTransactionsUtils::ProcessSendCoinsReturnAndInform(
            this,
            prepareStatus,
            walletModel,
            BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(),
                                         currentTransaction.getTransactionFee()),
            true
    );

    if (prepareStatus.status != WalletModel::OK) {
        inform(tr("Cannot create transaction."));
        return false;
    }

    QString warningStr;

    // Safety Guard 2: Unlocked 4,200 KRISTA Masternode Collateral Input Warning
    if (currentTransaction.getTransaction() && pwalletMain) {
        int nTipHeight = 0;
        {
            LOCK(cs_main);
            nTipHeight = chainActive.Height();
        }
        const CAmount mnCollateral = CMasternode::GetMasternodeNodeCollateral(nTipHeight);
        bool fSpendsMNCollateral = false;
        QString matchedOutpointStr;

        LOCK(pwalletMain->cs_wallet);
        for (const CTxIn& txin : currentTransaction.getTransaction()->vin) {
            const std::string prevHashHex = txin.prevout.hash.GetHex();
            CMasternodeWalletRecord mnMeta;
            if (pwalletMain->GetMasternodeMeta(txin.prevout, mnMeta)) {
                fSpendsMNCollateral = true;
                const QString aliasStr = mnMeta.strAlias.empty()
                    ? tr("Masternode")
                    : QString::fromStdString(mnMeta.strAlias);
                matchedOutpointStr = QString("%1 (%2:%3)").arg(aliasStr,
                                                               QString::fromStdString(prevHashHex).left(14) + "...",
                                                               QString::number(txin.prevout.n));
            }
            if (!fSpendsMNCollateral && mnCollateral > 0) {
                auto itPrev = pwalletMain->mapWallet.find(txin.prevout.hash);
                if (itPrev != pwalletMain->mapWallet.end() &&
                    txin.prevout.n < itPrev->second.vout.size() &&
                    itPrev->second.vout[txin.prevout.n].nValue == mnCollateral) {
                    fSpendsMNCollateral = true;
                    matchedOutpointStr = QString("%1:%2").arg(QString::fromStdString(prevHashHex).left(16) + "...",
                                                              QString::number(txin.prevout.n));
                }
            }
            if (fSpendsMNCollateral) break;
        }

        if (fSpendsMNCollateral) {
            showHideOp(true);
            DefaultDialog* mnDlg = new DefaultDialog(window);
            mnDlg->setDanger(true);
            mnDlg->setText(
                tr("Masternode Collateral Input Warning"),
                tr("This transaction spends a %1 Masternode collateral output:\n%2\n\n"
                   "Broadcasting this transaction will immediately invalidate and stop that Masternode. "
                   "Use Coin Control to lock or exclude Masternode collaterals if this was unintended.")
                    .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), mnCollateral, false, BitcoinUnits::separatorAlways))
                    .arg(matchedOutpointStr),
                tr("SPEND COLLATERAL"),
                tr("CANCEL")
            );
            mnDlg->adjustSize();
            bool confirmedCollateralSpend = openDialogWithOpaqueBackgroundY(mnDlg, window, 3, 5);
            mnDlg->deleteLater();
            if (!confirmedCollateralSpend) {
                return false;
            }
            warningStr = tr("Warning: Spends %1 Masternode collateral (%2)")
                             .arg(BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), mnCollateral),
                                  matchedOutpointStr);
        }
    }

    showHideOp(true);
    TxDetailDialog* dialog = new TxDetailDialog(window, true, warningStr);
    dialog->setDisplayUnit(walletModel->getOptionsModel()->getDisplayUnit());
    dialog->setData(walletModel, currentTransaction);
    dialog->adjustSize();
    openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);

    if (dialog->isConfirm()) {
        WalletModel::SendCoinsReturn sendStatus = dialog->getStatus();
        GuiTransactionsUtils::ProcessSendCoinsReturnAndInform(
                this,
                sendStatus,
                walletModel
        );

        if (sendStatus.status == WalletModel::OK) {
            clearAll(false);
            inform(tr("Transaction sent"));
            dialog->deleteLater();
            return true;
        }
    }

    dialog->deleteLater();
    return false;
}

QString SendWidget::recipientsToString(QList<SendCoinsRecipient> recipients)
{
    QString s;
    for (SendCoinsRecipient rec : recipients) {
        s += rec.address + " -> " + BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), rec.amount, false, BitcoinUnits::separatorAlways) + "\n";
    }
    return s;
}

void SendWidget::updateEntryLabels(QList<SendCoinsRecipient> recipients)
{
    if (!walletModel || !walletModel->getAddressTableModel()) return;
    for (const SendCoinsRecipient& rec : recipients) {
        const QString label = rec.label.trimmed();
        if (label.isEmpty()) continue;
        const QString labelOld = walletModel->getAddressTableModel()->labelForAddress(rec.address);
        if (label != labelOld) {
            CTxDestination dest = DecodeDestination(rec.address.toStdString());
            if (!walletModel->updateAddressBookLabels(dest, label.toStdString(),
                                                      this->walletModel->isMine(dest) ?
                                                              AddressBook::AddressBookPurpose::RECEIVE :
                                                              AddressBook::AddressBookPurpose::SEND)) {
                Q_EMIT message("", tr("Address label update failed for address: %1").arg(rec.address), CClientUIInterface::MSG_ERROR);
                return;
            }
        }
    }
}

void SendWidget::onChangeAddressClicked()
{
    showHideOp(true);
    SendChangeAddressDialog* dialog = new SendChangeAddressDialog(window, walletModel);
    if (IsValidDestination(coinControlDialog->coinControl->destChange)) {
        dialog->setAddress(QString::fromStdString(EncodeDestination(coinControlDialog->coinControl->destChange)));
    }

    if (!openDialogWithOpaqueBackgroundY(dialog, window, 3, 5)) {
        dialog->deleteLater();
        return;
    }

    CTxDestination destChange = dialog->getDestination();

    if (IsValidDestination(destChange)) {
        if (!walletModel->isMine(destChange) && !ask(tr("Warning!"),
                tr("The change address doesn't belong to this wallet.\n\nDo you want to continue?"))) {
            dialog->deleteLater();
            return;
        }
    }

    coinControlDialog->coinControl->destChange = destChange;
    dialog->deleteLater();
    updateActiveOptionsUi();
}

void SendWidget::onOpenUriClicked()
{
    showHideOp(true);
    OpenURIDialog *dlg = new OpenURIDialog(window);
    if (openDialogWithOpaqueBackgroundY(dlg, window, 3, 5)) {
        SendCoinsRecipient rcp;
        if (!GUIUtil::parseBitcoinURI(dlg->getURI(), &rcp)) {
            inform(tr("Invalid URI"));
            dlg->deleteLater();
            return;
        }
        if (!walletModel->validateAddress(rcp.address)) {
            inform(tr("Invalid address in URI"));
            dlg->deleteLater();
            return;
        }

        if (entries.isEmpty()) {
            addEntry();
        }
        if (!entries.isEmpty()) {
            SendMultiRow* entry = entries.last();
            if (entry && !entry->isClear()) {
                addEntry();
                entry = entries.last();
            }
            if (entry) {
                const QString uriLabelOrMsg = !rcp.label.trimmed().isEmpty() ? rcp.label.trimmed() : rcp.message.trimmed();
                entry->setAddressAndLabelOrDescription(rcp.address, uriLabelOrMsg);
                if (rcp.amount > 0) {
                    entry->setAmount(BitcoinUnits::format(nDisplayUnit, rcp.amount, false, BitcoinUnits::separatorNever));
                }
            }
        }
        refreshAmounts();
        Q_EMIT receivedURI(dlg->getURI());
    }
    dlg->deleteLater();
}

void SendWidget::onContactsOptionClicked()
{
    window->goToAddresses();
}

void SendWidget::onChangeCustomFeeClicked()
{
    showHideOp(true);
    if (!customFeeDialog) {
        customFeeDialog = new SendCustomFeeDialog(window, walletModel);
    }
    if (openDialogWithOpaqueBackgroundY(customFeeDialog, window, 3, 5)) {
        const CAmount& nFeePerKb = customFeeDialog->getFeeRate().GetFeePerK();
        setCustomFeeSelected(customFeeDialog->isCustomFeeChecked(), nFeePerKb);
        refreshAmounts();
    }
}

void SendWidget::onCoinControlClicked()
{
    if (walletModel->getBalance() > 0) {
        setCoinControlPayAmounts();
        coinControlDialog->refreshDialog();
        coinControlDialog->exec();
        refreshAmounts();
    } else {
        inform(tr("You don't have any %1 to select.").arg(CURRENCY_UNIT.c_str()));
    }
}

void SendWidget::setCoinControlPayAmounts()
{
    if (!coinControlDialog) return;
    coinControlDialog->clearPayAmounts();
    for (SendMultiRow* entry : entries) {
        if (entry) coinControlDialog->addPayAmount(entry->getAmountValue());
    }
}

void SendWidget::onValueChanged()
{
    refreshAmounts();
}

void SendWidget::onContactsClicked(SendMultiRow* entry)
{
    if (!entry || !walletModel) return;
    const bool sameEntry = (focusedEntry == entry);
    focusedEntry = entry;
    if (menu && menu->isVisible()) {
        menu->hide();
    }

    int contactsSize = walletModel->getAddressTableModel()->sizeSend();
    if (contactsSize == 0) {
        inform(tr("No contacts available, you can go to the contacts screen and add some there!"));
        return;
    }

    if (menuContacts && menuContacts->isVisible() && sameEntry) {
        menuContacts->hide();
        return;
    }

    int height = (contactsSize <= 2) ? entry->getEditHeight() * (2 * (contactsSize + 1)) : entry->getEditHeight() * 6;
    height = qBound(140, height, 260);
    int width = qBound(300, entry->getEditWidth(), this->width() - 40);

    if (!menuContacts) {
        menuContacts = new ContactsDropdown(width, height, this);
        menuContacts->setWalletModel(walletModel, AddressTableModel::Send);
        connect(menuContacts, &ContactsDropdown::contactSelected, this, [this](QString address, QString label) {
            if (focusedEntry) {
                if (label != "(no label)")
                    focusedEntry->setLabel(label);
                focusedEntry->setAddress(address);
            }
        });
    }

    menuContacts->resizeList(width, height);
    menuContacts->setStyleSheet(window ? window->styleSheet() : GUIUtil::loadStyleSheet());
    menuContacts->adjustSize();

    QPoint pos = entry->getAddressPopupPos(this);
    if (pos.x() + width > this->width() - 16) {
        pos.setX(qMax(16, this->width() - 16 - width));
    }
    if (pos.y() + height > this->height() - 16) {
        pos.setY(qMax(16, pos.y() - entry->getEditHeight() - height - 8));
    }
    menuContacts->move(pos);
    menuContacts->show();
    menuContacts->raise();
}

void SendWidget::onMenuClicked(SendMultiRow* entry)
{
    focusedEntry = entry;
}

void SendWidget::onPercentClicked(SendMultiRow* entry, int percent)
{
    if (!entry || !walletModel) return;

    CAmount otherTotal = 0;
    for (SendMultiRow* e : entries) {
        if (e && e != entry) {
            CAmount val = e->getAmountValue();
            if (val > 0) otherTotal += val;
        }
    }

    const CAmount available = (coinControlDialog && coinControlDialog->coinControl && coinControlDialog->coinControl->HasSelected())
                                  ? walletModel->getBalance(coinControlDialog->coinControl)
                                  : walletModel->getUnlockedBalance();
    const CAmount remaining = available - otherTotal;
    if (remaining <= 0) {
        inform(tr("No unlocked balance remaining to fill."));
        return;
    }

    const CAmount fillAmount = (percent >= 100) ? remaining : ((remaining * percent) / 100);
    if (fillAmount <= 0) {
        inform(tr("Calculated share is too small to fill."));
        return;
    }

    entry->setAmount(BitcoinUnits::format(nDisplayUnit, fillAmount, false, BitcoinUnits::separatorNever));
    entry->setSubtractFeeFromAmount(percent >= 100);
    refreshAmounts();
}

void SendWidget::onMaxClicked(SendMultiRow* entry)
{
    onPercentClicked(entry, 100);
}

void SendWidget::onContactMultiClicked()
{
    if (!focusedEntry || !walletModel) return;

    QString address = focusedEntry->getAddress();
    if (address.isEmpty()) {
        inform(tr("Address field is empty"));
        return;
    }
    if (!walletModel->validateAddress(address)) {
        inform(tr("Invalid address"));
        return;
    }
    CTxDestination pivAdd = DecodeDestination(address.toStdString());
    if (walletModel->isMine(pivAdd)) {
        inform(tr("Cannot store your own address as contact"));
        return;
    }

    showHideOp(true);
    AddNewContactDialog *dialog = new AddNewContactDialog(window);
    const QString savedLabel = walletModel->getAddressTableModel()->labelForAddress(address);
    const QString typedLabel = focusedEntry ? focusedEntry->getValue().label.trimmed() : QString();
    const bool hasExistingLabel = !savedLabel.isEmpty();
    if (hasExistingLabel) {
        dialog->setTexts(tr("Update Contact"), "Edit label for the selected address:\n%1");
        dialog->setData(address, savedLabel);
    } else {
        dialog->setTexts(tr("Create New Contact"), "Save label for the selected address:\n%1");
        dialog->setData(address, typedLabel);
    }
    openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);
    if (dialog->res) {
        const QString newLabel = dialog->getLabel().trimmed();
        if (!hasExistingLabel || newLabel != savedLabel) {
            if (walletModel->updateAddressBookLabels(pivAdd, newLabel.toStdString(),
                    AddressBook::AddressBookPurpose::SEND)) {
                inform(hasExistingLabel ? tr("Contact Updated") : tr("New Contact Stored"));
                if (focusedEntry) {
                    focusedEntry->setLabel(newLabel);
                    focusedEntry->applyTheme(isLightTheme());
                }
            } else {
                inform(tr("Error Storing Contact"));
            }
        }
    }
    dialog->deleteLater();
}

void SendWidget::onDeleteClicked()
{
    if (!focusedEntry) return;
    if (entries.size() <= 1) return;

    if (recipientsLayout) recipientsLayout->removeWidget(focusedEntry);
    focusedEntry->hide();
    focusedEntry->deleteLater();

    entries.removeAll(focusedEntry);
    focusedEntry = nullptr;
    if (recipientsLayout) recipientsLayout->activate();
    if (scrollContents) scrollContents->adjustSize();
    if (scrollArea) scrollArea->updateGeometry();

    const bool isMulti = entries.size() > 1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i]) {
            entries[i]->setNumber(i + 1);
            entries[i]->setMultiMode(isMulti);
        }
    }

    refreshAmounts();
    setFocusOnLastEntry();
}

void SendWidget::resizeMenu()
{
    if (menuContacts && menuContacts->isVisible() && focusedEntry) {
        int width = qBound(300, focusedEntry->getEditWidth(), this->width() - 40);
        int height = menuContacts->height();
        menuContacts->resizeList(width, height);
        menuContacts->resize(width, height);
        QPoint pos = focusedEntry->getAddressPopupPos(this);
        if (pos.x() + width > this->width() - 16) {
            pos.setX(qMax(16, this->width() - 16 - width));
        }
        menuContacts->move(pos);
    }
}

void SendWidget::setCustomFeeSelected(bool isSelected, const CAmount& customFee)
{
    isCustomFeeSelected = isSelected;
    currentCustomFeePerKb = isSelected ? customFee : DEFAULT_TRANSACTION_FEE;
    if (walletModel) {
        walletModel->setWalletDefaultFee(currentCustomFeePerKb);
    }
    updateActiveOptionsUi();
}

void SendWidget::onPsbtClicked()
{
    const bool light = isLightTheme();
    const QString bg      = light ? "#F8FAFC" : "#0D1321";
    const QString cardBg  = light ? "#FFFFFF" : "#111A2E";
    const QString textCol = light ? "#1E293B" : "#F1F5F9";
    const QString subCol  = light ? "#475569" : "#CBD5E1";
    const QString border  = light ? "#CBD5E1" : "#263859";

    QDialog dlg(this);
    dlg.setWindowTitle(tr("KristaTech - Sign & Broadcast PSBT (BIP174)"));
    dlg.resize(680, 500);
    dlg.setStyleSheet(QString(
        "QDialog { background-color: %1; color: %2; }"
        "QLabel { color: %2; background: transparent; }"
        "QPlainTextEdit { background-color: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 8px; font-family: monospace; font-size: 12px; }"
        "QPushButton { background-color: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 12px; }"
        "QPushButton:hover { border-color: #5576B2; color: #5576B2; }"
    ).arg(bg, textCol, cardBg, border));

    QVBoxLayout* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(18, 16, 18, 16);
    root->setSpacing(10);

    QLabel* lblHint = new QLabel(tr("Paste a Base64-encoded BIP174 PSBT or load a .psbt file to inspect, sign with this wallet, or finalize & broadcast."), &dlg);
    lblHint->setWordWrap(true);
    lblHint->setStyleSheet(QString("color: %1; font-size: 12px;").arg(subCol));

    QPlainTextEdit* editPsbt = new QPlainTextEdit(&dlg);
    editPsbt->setPlaceholderText(tr("cHNidP8BAHECAAAA..."));
    editPsbt->setMinimumHeight(130);

    QPlainTextEdit* editResult = new QPlainTextEdit(&dlg);
    editResult->setReadOnly(true);
    editResult->setPlaceholderText(tr("Decoded PSBT summary, signature status, or broadcast TXID will appear here..."));
    editResult->setMinimumHeight(170);

    QHBoxLayout* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    QPushButton* btnLoad   = new QPushButton(tr("Load .psbt"), &dlg);
    QPushButton* btnDecode = new QPushButton(tr("Inspect"), &dlg);
    QPushButton* btnSign   = new QPushButton(tr("Sign PSBT"), &dlg);
    QPushButton* btnCopy   = new QPushButton(tr("Copy PSBT"), &dlg);
    QPushButton* btnSend   = new QPushButton(tr("Finalize && Broadcast"), &dlg);
    QPushButton* btnClose  = new QPushButton(tr("Close"), &dlg);

    btnSign->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid #7695CD; border-radius: 6px; padding: 6px 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: %3; border-color: %4; }"
    ).arg(light ? QStringLiteral("#EEF3FA") : QStringLiteral("#1B2B4A"),
          light ? QStringLiteral("#3E5B94") : QStringLiteral("#A8C1EE"),
          light ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860"),
          light ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0")));
    btnSend->setStyleSheet(QString(
        "QPushButton { background-color: %1; color: #FFFFFF; border: 1px solid %2; border-radius: 6px; padding: 6px 14px; font-weight: 600; }"
        "QPushButton:hover { background-color: %3; border-color: %4; }"
        "QPushButton:pressed { background-color: %5; border-color: %6; }"
        "QPushButton:disabled { background-color: %7; color: %8; border-color: %7; }"
        "QPushButton:focus { outline: none; border: 2px solid %9; }"
    ).arg(light ? QStringLiteral("#3E5B94") : QStringLiteral("#4A6AB0"),
          light ? QStringLiteral("#354F82") : QStringLiteral("#5B7CC4"),
          light ? QStringLiteral("#2F4A7D") : QStringLiteral("#5576B2"),
          light ? QStringLiteral("#2A4270") : QStringLiteral("#6B8CD4"),
          light ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94"),
          light ? QStringLiteral("#253B66") : QStringLiteral("#3E5B94"),
          light ? QStringLiteral("#CBD5E1") : QStringLiteral("#1E293B"),
          light ? QStringLiteral("#64748B") : QStringLiteral("#475569"),
          light ? QStringLiteral("#1E3A8A") : QStringLiteral("#93B4F0")));

    btnRow->addWidget(btnLoad);
    btnRow->addWidget(btnDecode);
    btnRow->addWidget(btnSign);
    btnRow->addWidget(btnCopy);
    btnRow->addWidget(btnSend);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);

    root->addWidget(lblHint);
    root->addWidget(editPsbt);
    root->addLayout(btnRow);
    root->addWidget(editResult, 1);

    auto runRpc = [](const std::string& method, const UniValue& params) -> UniValue {
        JSONRPCRequest req;
        req.strMethod = method;
        req.params = params;
        return tableRPC.execute(req);
    };

    connect(btnLoad, &QPushButton::clicked, &dlg, [&]() {
        QString path = QFileDialog::getOpenFileName(&dlg, tr("Open PSBT File"), QString(), tr("PSBT Files (*.psbt);;All Files (*)"));
        if (path.isEmpty()) return;
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            editResult->setPlainText(tr("Failed to open file: %1").arg(path));
            return;
        }
        if (f.size() > 10 * 1024 * 1024) {
            editResult->setPlainText(tr("PSBT file exceeds maximum supported size (10 MB)."));
            return;
        }
        QByteArray raw = f.readAll();
        if (raw.startsWith("psbt\xff")) {
            editPsbt->setPlainText(QString::fromLatin1(raw.toBase64()));
        } else {
            editPsbt->setPlainText(QString::fromUtf8(raw).trimmed());
        }
    });

    auto extractRpcError = [](const UniValue& err) -> QString {
        const UniValue& msgVal = find_value(err, "message");
        return msgVal.isStr() ? QString::fromStdString(msgVal.get_str()) : QString::fromStdString(err.write());
    };

    connect(btnDecode, &QPushButton::clicked, &dlg, [&]() {
        QString b64 = editPsbt->toPlainText().trimmed();
        if (b64.isEmpty()) return;
        try {
            UniValue p(UniValue::VARR);
            p.push_back(b64.toStdString());
            UniValue res = runRpc("decodepsbt", p);
            editResult->setPlainText(QString::fromStdString(res.write(2)));
        } catch (const UniValue& err) {
            editResult->setPlainText(extractRpcError(err));
        } catch (const std::exception& e) {
            editResult->setPlainText(QString::fromStdString(e.what()));
        }
    });

    connect(btnSign, &QPushButton::clicked, &dlg, [&]() {
        QString b64 = editPsbt->toPlainText().trimmed();
        if (b64.isEmpty() || !walletModel) return;
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());
        if (!ctx.isValid()) return;
        try {
            UniValue p(UniValue::VARR);
            p.push_back(b64.toStdString());
            p.push_back(true);
            UniValue res = runRpc("walletprocesspsbt", p);
            std::string updated = find_value(res, "psbt").get_str();
            bool complete = find_value(res, "complete").get_bool();
            editPsbt->setPlainText(QString::fromStdString(updated));
            editResult->setPlainText(tr("PSBT signed successfully.\nComplete: %1").arg(complete ? tr("Yes (ready to finalize & broadcast)") : tr("No (more signatures required)")));
        } catch (const UniValue& err) {
            editResult->setPlainText(extractRpcError(err));
        } catch (const std::exception& e) {
            editResult->setPlainText(QString::fromStdString(e.what()));
        }
    });

    connect(btnCopy, &QPushButton::clicked, &dlg, [&]() {
        GUIUtil::setClipboard(editPsbt->toPlainText().trimmed());
        inform(tr("PSBT copied to clipboard"));
    });

    connect(btnSend, &QPushButton::clicked, &dlg, [&]() {
        QString b64 = editPsbt->toPlainText().trimmed();
        if (b64.isEmpty()) return;
        try {
            const int dispUnit = (walletModel && walletModel->getOptionsModel())
                ? walletModel->getOptionsModel()->getDisplayUnit()
                : BitcoinUnits::PIV;

            QString psbtSummaryLines;
            try {
                UniValue pDec(UniValue::VARR);
                pDec.push_back(b64.toStdString());
                UniValue decRes = runRpc("decodepsbt", pDec);
                const UniValue& txObj = find_value(decRes, "tx");
                const UniValue& voutArr = txObj.isObject() ? find_value(txObj, "vout") : NullUniValue;
                const UniValue& vinArr = txObj.isObject() ? find_value(txObj, "vin") : NullUniValue;
                const UniValue& feeVal = find_value(decRes, "fee");
                CAmount totalOut = 0;
                QStringList destSummaries;
                if (voutArr.isArray()) {
                    for (unsigned int k = 0; k < voutArr.size(); ++k) {
                        const UniValue& o = voutArr[k];
                        const UniValue& valUv = find_value(o, "value");
                        CAmount amt = 0;
                        if (valUv.isNum()) {
                            amt = AmountFromValue(valUv);
                            totalOut += amt;
                        }
                        QString destDesc = tr("(non-address / OP_RETURN / contract)");
                        const UniValue& spkObj = find_value(o, "scriptPubKey");
                        if (spkObj.isObject()) {
                            const UniValue& addrs = find_value(spkObj, "addresses");
                            if (addrs.isArray() && !addrs.empty() && addrs[0].isStr()) {
                                destDesc = QString::fromStdString(addrs[0].get_str());
                            } else if (find_value(spkObj, "type").isStr()) {
                                destDesc = QString::fromStdString(find_value(spkObj, "type").get_str());
                            }
                        }
                        if (destSummaries.size() < 6) {
                            destSummaries << QString("  • %1 -> %2").arg(BitcoinUnits::formatWithUnit(dispUnit, amt), destDesc);
                        }
                    }
                    if (voutArr.size() > 6) {
                        destSummaries << tr("  ... and %1 more output(s)").arg(voutArr.size() - 6);
                    }
                }
                QString feeStr = feeVal.isNum()
                    ? BitcoinUnits::formatWithUnit(dispUnit, AmountFromValue(feeVal))
                    : tr("Unknown (verify all UTXOs before broadcasting!)");
                psbtSummaryLines = tr("• Inputs: %1  |  Outputs: %2\n"
                                      "• Total Output: %3\n"
                                      "• Fee: %4\n%5\n")
                                       .arg(vinArr.isArray() ? static_cast<int>(vinArr.size()) : 0)
                                       .arg(voutArr.isArray() ? static_cast<int>(voutArr.size()) : 0)
                                       .arg(BitcoinUnits::formatWithUnit(dispUnit, totalOut), feeStr,
                                            destSummaries.isEmpty() ? QString() : (tr("• Destinations:\n") + destSummaries.join("\n") + "\n"));
            } catch (...) {
                psbtSummaryLines = tr("• Outputs/Fee: Unavailable (decodepsbt failed)\n");
            }

            UniValue pFin(UniValue::VARR);
            pFin.push_back(b64.toStdString());
            pFin.push_back(true);
            UniValue finRes = runRpc("finalizepsbt", pFin);
            bool complete = find_value(finRes, "complete").get_bool();
            if (!complete) {
                editResult->setPlainText(tr("PSBT is not yet complete; additional signatures are required before broadcast."));
                return;
            }
            std::string rawHex = find_value(finRes, "hex").get_str();
            {
                DefaultDialog psbtConfirm(&dlg);
                psbtConfirm.setText(
                    tr("Confirm PSBT Broadcast"),
                    tr("Finalize and broadcast this signed BIP174 PSBT transaction to the KristaTech network?\n\n"
                       "%1"
                       "• Raw Transaction Size: %2 bytes\n\n"
                       "Once broadcast, this transaction cannot be reversed.")
                        .arg(psbtSummaryLines)
                        .arg(rawHex.size() / 2),
                    tr("BROADCAST TX"),
                    tr("CANCEL")
                );
                psbtConfirm.adjustSize();
                if (psbtConfirm.exec() != QDialog::Accepted) {
                    return;
                }
            }
            UniValue pSend(UniValue::VARR);
            pSend.push_back(rawHex);
            UniValue txid = runRpc("sendrawtransaction", pSend);
            editResult->setPlainText(tr("Transaction Broadcast Successfully!\nTXID: %1").arg(QString::fromStdString(txid.get_str())));
            inform(tr("PSBT Broadcast! TXID: %1").arg(QString::fromStdString(txid.get_str()).left(20) + "..."));
        } catch (const UniValue& err) {
            editResult->setPlainText(extractRpcError(err));
        } catch (const std::exception& e) {
            editResult->setPlainText(QString::fromStdString(e.what()));
        }
    });

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void SendWidget::changeTheme(bool isLightTheme, QString& theme)
{
    if (coinControlDialog) coinControlDialog->applyTheme(isLightTheme);
    applyTheme(isLightTheme);
}

SendWidget::~SendWidget()
{
    delete coinControlDialog;
}
