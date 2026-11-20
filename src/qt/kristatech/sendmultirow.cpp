// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/sendmultirow.h"

#include "addresstablemodel.h"
#include "base58.h"
#include "bitcoinunits.h"
#include "chainparams.h"
#include "guiutil.h"
#include "optionsmodel.h"
#include "qt/kristatech/qtutils.h"
#include "script/standard.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <boost/variant/get.hpp>

SendMultiRow::SendMultiRow(PWidget *parent) :
    PWidget(parent)
{
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 10);
    outerLayout->setSpacing(0);

    cardFrame = new QFrame(this);
    cardFrame->setObjectName("sendRecipientCard");
    cardFrame->setAttribute(Qt::WA_StyledBackground, true);
    outerLayout->addWidget(cardFrame);

    QVBoxLayout* cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(10);

    // Row 1: Card Header + Live Address Status Badge + Save Contact / Remove
    QHBoxLayout* headerRow = new QHBoxLayout();
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(8);

    lblTitle = new QLabel(tr("Recipient"), cardFrame);
    lblAddressStatus = new QLabel(cardFrame);
    lblAddressStatus->setVisible(false);

    btnSaveContact = new QPushButton(tr("Save Contact"), cardFrame);
    btnSaveContact->setCursor(Qt::PointingHandCursor);
    btnSaveContact->setFocusPolicy(Qt::TabFocus);
    btnSaveContact->setFixedHeight(28);
    btnSaveContact->setToolTip(tr("Save this recipient address to your Address Book"));

    btnRemoveEntry = new QPushButton(QString::fromUtf8("\xC3\x97 ") + tr("Remove"), cardFrame);
    btnRemoveEntry->setCursor(Qt::PointingHandCursor);
    btnRemoveEntry->setFocusPolicy(Qt::TabFocus);
    btnRemoveEntry->setFixedHeight(28);
    btnRemoveEntry->setToolTip(tr("Remove this recipient row"));
    btnRemoveEntry->setVisible(false);

    headerRow->addWidget(lblTitle);
    headerRow->addWidget(lblAddressStatus);
    headerRow->addStretch();
    headerRow->addWidget(btnSaveContact);
    headerRow->addWidget(btnRemoveEntry);
    cardLayout->addLayout(headerRow);

    // Row 2: Recipient Address + Paste + Contacts
    lblAddressField = new QLabel(tr("Recipient Address"), cardFrame);
    cardLayout->addWidget(lblAddressField);

    QHBoxLayout* addrRow = new QHBoxLayout();
    addrRow->setContentsMargins(0, 0, 0, 0);
    addrRow->setSpacing(8);

    lineEditAddress = new QLineEdit(cardFrame);
    lineEditAddress->setMinimumHeight(40);
    lineEditAddress->setMaximumHeight(40);
    lineEditAddress->setClearButtonEnabled(true);
    lineEditAddress->setPlaceholderText(tr("Enter KRISTA address (KT... / kta1p...) or paste payment URI..."));
    lineEditAddress->setAttribute(Qt::WA_MacShowFocusRect, false);

    btnPaste = new QPushButton(tr("Paste"), cardFrame);
    btnPaste->setCursor(Qt::PointingHandCursor);
    btnPaste->setFocusPolicy(Qt::TabFocus);
    btnPaste->setFixedHeight(40);
    btnPaste->setMinimumWidth(68);
    btnPaste->setToolTip(tr("Paste a KRISTA address or kristatech: payment URI from clipboard"));

    btnContactsBtn = new QPushButton(tr("Contacts") + QString::fromUtf8(" \xE2\x96\xBE"), cardFrame);
    btnContactsBtn->setCursor(Qt::PointingHandCursor);
    btnContactsBtn->setFocusPolicy(Qt::TabFocus);
    btnContactsBtn->setFixedHeight(40);
    btnContactsBtn->setMinimumWidth(96);
    btnContactsBtn->setToolTip(tr("Choose a recipient from your saved contacts"));

    addrRow->addWidget(lineEditAddress, 1);
    addrRow->addWidget(btnPaste);
    addrRow->addWidget(btnContactsBtn);
    cardLayout->addLayout(addrRow);

    // Row 3: Amount + Quick Chips (25% / 50% / MAX) & Optional Label side-by-side
    QHBoxLayout* amountDescGrid = new QHBoxLayout();
    amountDescGrid->setContentsMargins(0, 2, 0, 0);
    amountDescGrid->setSpacing(12);

    QVBoxLayout* amountCol = new QVBoxLayout();
    amountCol->setContentsMargins(0, 0, 0, 0);
    amountCol->setSpacing(5);
    lblAmountField = new QLabel(tr("Amount"), cardFrame);
    amountCol->addWidget(lblAmountField);

    QHBoxLayout* amountInputRow = new QHBoxLayout();
    amountInputRow->setContentsMargins(0, 0, 0, 0);
    amountInputRow->setSpacing(6);

    lineEditAmount = new QLineEdit(cardFrame);
    lineEditAmount->setMinimumHeight(40);
    lineEditAmount->setMaximumHeight(40);
    lineEditAmount->setMaxLength(17);
    lineEditAmount->setPlaceholderText(QStringLiteral("0.00 KRISTA"));
    GUIUtil::setupAmountWidget(lineEditAmount, this);

    btnPct25 = new QPushButton(QStringLiteral("25%"), cardFrame);
    btnPct25->setCursor(Qt::PointingHandCursor);
    btnPct25->setFocusPolicy(Qt::TabFocus);
    btnPct25->setFixedHeight(40);
    btnPct25->setMinimumWidth(46);
    btnPct25->setToolTip(tr("Fill 25% of your remaining spendable balance"));

    btnPct50 = new QPushButton(QStringLiteral("50%"), cardFrame);
    btnPct50->setCursor(Qt::PointingHandCursor);
    btnPct50->setFocusPolicy(Qt::TabFocus);
    btnPct50->setFixedHeight(40);
    btnPct50->setMinimumWidth(46);
    btnPct50->setToolTip(tr("Fill 50% of your remaining spendable balance"));

    btnMax = new QPushButton(tr("MAX"), cardFrame);
    btnMax->setCursor(Qt::PointingHandCursor);
    btnMax->setFocusPolicy(Qt::TabFocus);
    btnMax->setFixedHeight(40);
    btnMax->setMinimumWidth(52);
    btnMax->setToolTip(tr("Fill remaining unlocked balance and deduct network fee automatically"));

    amountInputRow->addWidget(lineEditAmount, 1);
    amountInputRow->addWidget(btnPct25);
    amountInputRow->addWidget(btnPct50);
    amountInputRow->addWidget(btnMax);
    amountCol->addLayout(amountInputRow);

    QVBoxLayout* descCol = new QVBoxLayout();
    descCol->setContentsMargins(0, 0, 0, 0);
    descCol->setSpacing(5);
    lblDescField = new QLabel(tr("Address Label (optional)"), cardFrame);
    descCol->addWidget(lblDescField);

    lineEditDescription = new QLineEdit(cardFrame);
    lineEditDescription->setMinimumHeight(40);
    lineEditDescription->setMaximumHeight(40);
    lineEditDescription->setClearButtonEnabled(true);
    lineEditDescription->setPlaceholderText(tr("Optional contact label or note"));

    descCol->addWidget(lineEditDescription);

    amountDescGrid->addLayout(amountCol, 5);
    amountDescGrid->addLayout(descCol, 4);
    cardLayout->addLayout(amountDescGrid);

    // Row 4: Subtract fee toggle + dynamic hint
    QHBoxLayout* footerRow = new QHBoxLayout();
    footerRow->setContentsMargins(0, 2, 0, 0);
    footerRow->setSpacing(10);

    checkSubtractFee = new QCheckBox(tr("Subtract fee from amount"), cardFrame);
    checkSubtractFee->setCursor(Qt::PointingHandCursor);
    checkSubtractFee->setToolTip(tr("Deduct the network transaction fee from this recipient's amount instead of your remaining balance"));

    lblSubtractHint = new QLabel(tr("Recipient receives amount minus network fee"), cardFrame);
    lblSubtractHint->setVisible(false);

    footerRow->addWidget(checkSubtractFee);
    footerRow->addStretch();
    footerRow->addWidget(lblSubtractHint);
    cardLayout->addLayout(footerRow);

    connect(lineEditAmount, &QLineEdit::textChanged, this, &SendMultiRow::amountChanged);
    connect(lineEditAddress, &QLineEdit::textChanged, this, [this](const QString& text) {
        addressChanged(text, false);
        Q_EMIT onValueChanged();
    });
    connect(btnPaste, &QPushButton::clicked, this, &SendMultiRow::onPasteClicked);
    connect(btnContactsBtn, &QPushButton::clicked, this, [this]() { Q_EMIT onContactsClicked(this); });
    connect(btnPct25, &QPushButton::clicked, this, [this]() { Q_EMIT onPercentClicked(this, 25); });
    connect(btnPct50, &QPushButton::clicked, this, [this]() { Q_EMIT onPercentClicked(this, 50); });
    connect(btnMax, &QPushButton::clicked, this, [this]() { Q_EMIT onMaxClicked(this); });
    connect(btnSaveContact, &QPushButton::clicked, this, [this]() { Q_EMIT onSaveContactClicked(this); });
    connect(btnRemoveEntry, &QPushButton::clicked, this, [this]() { Q_EMIT removeEntry(this); });
    connect(checkSubtractFee, &QCheckBox::toggled, this, [this](bool checked) {
        if (lblSubtractHint) lblSubtractHint->setVisible(checked);
        Q_EMIT onValueChanged();
    });

    applyTheme(isLightTheme());
}

void SendMultiRow::onPasteClicked()
{
    const QString clip = QApplication::clipboard()->text().trimmed();
    if (!clip.isEmpty() && lineEditAddress) {
        lineEditAddress->setText(clip);
        lineEditAmount->setFocus();
    }
}

void SendMultiRow::applyTheme(bool isLight)
{
    const QString cardBg      = isLight ? "#FFFFFF" : "#131B2E";
    const QString cardBorder  = isLight ? "#CBD5E1" : "#253554";
    const QString titleCol    = isLight ? "#1E293B" : "#F1F5F9";
    const QString labelCol    = isLight ? "#334155" : "#CBD5E1";
    const QString hintCol     = isLight ? "#B45309" : "#FBBF24";
    const QString secBtnBg    = isLight ? "#F1F5F9" : "#1C2942";
    const QString secBtnFg    = isLight ? "#1E293B" : "#E2E8F0";
    const QString secBtnBrd   = isLight ? "#CBD5E1" : "#33486E";
    const QString secBtnHov   = isLight ? "#E2E8F0" : "#26385A";
    const QString maxBg       = isLight ? "#EEF3FA" : "#213458";
    const QString maxFg       = isLight ? "#3E5B94" : "#A8C1EE";
    const QString maxBrd      = isLight ? "#7695CD" : "#5576B2";
    const QString remBrd      = isLight ? "#9E3329" : "#F08074";

    if (cardFrame) {
        cardFrame->setStyleSheet(QString(
            "QFrame#sendRecipientCard { background-color: %1; border: 1px solid %2; border-radius: 10px; }"
        ).arg(cardBg, cardBorder));
    }
    if (lblTitle) {
        lblTitle->setStyleSheet(QString("QLabel { color: %1; font-size: 14px; font-weight: 600; background: transparent; border: none; }").arg(titleCol));
    }
    const QString fieldLabelStyle = QString("QLabel { color: %1; font-size: 12px; font-weight: 600; background: transparent; border: none; }").arg(labelCol);
    if (lblAddressField) lblAddressField->setStyleSheet(fieldLabelStyle);
    if (lblAmountField)  lblAmountField->setStyleSheet(fieldLabelStyle);
    if (lblDescField)    lblDescField->setStyleSheet(fieldLabelStyle);
    if (lblSubtractHint) {
        lblSubtractHint->setStyleSheet(QString("QLabel { color: %1; font-size: 11px; font-weight: 600; background: transparent; border: none; }").arg(hintCol));
    }

    const QString focCol = isLight ? QStringLiteral("#3E5B94") : QStringLiteral("#93B4F0");
    const QString secBtnStyle = QString(
        "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 11px; font-size: 12px; font-weight: 600; }"
        "QPushButton:hover { background-color: %4; border-color: #7695CD; }"
        "QPushButton:pressed { background-color: %3; }"
        "QPushButton:focus { outline: none; border: 1.5px solid %5; }"
    ).arg(secBtnBg, secBtnFg, secBtnBrd, secBtnHov, focCol);

    if (btnPaste)       btnPaste->setStyleSheet(secBtnStyle);
    if (btnContactsBtn) btnContactsBtn->setStyleSheet(secBtnStyle);
    if (btnSaveContact) btnSaveContact->setStyleSheet(secBtnStyle);
    if (btnPct25)       btnPct25->setStyleSheet(secBtnStyle);
    if (btnPct50)       btnPct50->setStyleSheet(secBtnStyle);

    if (btnMax) {
        btnMax->setStyleSheet(QString(
            "QPushButton { background-color: %1; color: %2; border: 1px solid %3; border-radius: 7px; padding: 0 10px; font-size: 12px; font-weight: 600; }"
            "QPushButton:hover { background-color: %4; color: %5; border-color: %6; }"
            "QPushButton:focus { outline: none; border: 1.5px solid %7; }"
        ).arg(maxBg, maxFg, maxBrd,
              isLight ? QStringLiteral("#DCE7F7") : QStringLiteral("#243860"),
              isLight ? QStringLiteral("#2F497A") : QStringLiteral("#D5E3FA"),
              isLight ? QStringLiteral("#5576B2") : QStringLiteral("#93B4F0"),
              focCol));
    }

    if (btnRemoveEntry) {
        btnRemoveEntry->setStyleSheet(QString(
            "QPushButton { background-color: #C94A3E; color: #FFFFFF; border: 1px solid %1; border-radius: 6px; padding: 0 10px; font-size: 12px; font-weight: 600; }"
            "QPushButton:hover { background-color: #A6362B; }"
            "QPushButton:pressed { background-color: #882920; }"
            "QPushButton:focus { outline: none; border: 1.5px solid %2; }"
        ).arg(remBrd, focCol));
    }

    if (checkSubtractFee) {
        checkSubtractFee->setStyleSheet(QString(
            "QCheckBox { color: %1; font-size: 12px; font-weight: 500; spacing: 7px; background: transparent; }"
            "QCheckBox:focus { outline: none; color: %2; }"
        ).arg(labelCol, focCol));
    }

    styleLineEditClearButton(lineEditAddress);
    styleLineEditClearButton(lineEditDescription);
    refreshInputStyles();

    const QString trimmed = lineEditAddress ? lineEditAddress->text().trimmed() : QString();
    if (trimmed.isEmpty()) {
        if (lblAddressStatus) lblAddressStatus->setVisible(false);
    } else if (walletModel) {
        updateAddressBadge(trimmed, walletModel->validateAddress(trimmed));
    }
}

void SendMultiRow::refreshInputStyles()
{
    const bool isLight = isLightTheme();
    const QString inputBg     = isLight ? "#F8FAFC" : "#0D1424";
    const QString inputFg     = isLight ? "#1E293B" : "#F1F5F9";
    const QString inputBorder = isLight ? "#CBD5E1" : "#2D4066";
    const QString focusBorder = "#7695CD";
    const QString errBorder   = "#EF4444";

    auto styleFor = [&](bool invalid, bool monospace) {
        return QString(
            "QLineEdit { background-color: %1; color: %2; border: 1.5px solid %3; border-radius: 7px; padding: 0 10px; font-size: 13px; %5 }"
            "QLineEdit:focus { border-color: %4; }"
        ).arg(inputBg,
              inputFg,
              invalid ? errBorder : inputBorder,
              invalid ? errBorder : focusBorder,
              monospace ? QStringLiteral("font-family: monospace;") : QString());
    };

    if (lineEditAddress)     lineEditAddress->setStyleSheet(styleFor(addrInvalid, true));
    if (lineEditAmount)      lineEditAmount->setStyleSheet(styleFor(amountInvalid, false));
    if (lineEditDescription) lineEditDescription->setStyleSheet(styleFor(false, false));
}

void SendMultiRow::updateAddressBadge(const QString& trimmedStr, bool validAddress)
{
    if (!lblAddressStatus) return;
    if (trimmedStr.isEmpty()) {
        lblAddressStatus->setVisible(false);
        return;
    }

    const bool light = isLightTheme();
    QString text;
    QString fg;
    QString bg;
    QString brd;

    if (!validAddress) {
        text = tr("\xE2\x9C\x97 Invalid KRISTA address");
        fg = light ? "#B91C1C" : "#FCA5A5";
        bg = "rgba(239, 68, 68, 0.14)";
        brd = "rgba(239, 68, 68, 0.45)";
    } else {
        const std::string addrStd = trimmedStr.toStdString();
        const bool fKnownBurn = Params().GetConsensus().mBurnAddresses.count(addrStd) > 0 ||
                                trimmedStr.startsWith(QStringLiteral("KTburn"), Qt::CaseInsensitive);
        const CTxDestination dest = DecodeDestination(addrStd);
        QString label;
        if (walletModel && walletModel->getAddressTableModel()) {
            label = walletModel->getAddressTableModel()->labelForAddress(trimmedStr);
        }

        if (fKnownBurn) {
            text = tr("\xE2\x9A\xA0 Burn Address (Permanent)");
            fg = light ? "#B91C1C" : "#FCA5A5";
            bg = "rgba(239, 68, 68, 0.16)";
            brd = "rgba(239, 68, 68, 0.50)";
        } else if (boost::get<WitnessV1Taproot>(&dest) != nullptr) {
            text = tr("\xE2\x9C\x93 Taproot / MESCAL (Bech32m)");
            if (!label.isEmpty()) text += QString(" \xE2\x80\xA2 %1").arg(label);
            fg = light ? "#6D28D9" : "#C4B5FD";
            bg = "rgba(139, 92, 246, 0.15)";
            brd = "rgba(139, 92, 246, 0.45)";
        } else if (boost::get<CScriptID>(&dest) != nullptr) {
            text = tr("\xE2\x9C\x93 P2SH Script Address");
            if (!label.isEmpty()) text += QString(" \xE2\x80\xA2 %1").arg(label);
            fg = light ? "#0369A1" : "#7DD3FC";
            bg = "rgba(56, 189, 248, 0.15)";
            brd = "rgba(56, 189, 248, 0.45)";
        } else if (walletModel && walletModel->isMine(dest)) {
            text = tr("\xE2\x9C\x93 Own Wallet Address");
            if (!label.isEmpty()) text += QString(" (%1)").arg(label);
            fg = light ? "#B5463A" : "#F5897C";
            bg = "rgba(245, 137, 124, 0.13)";
            brd = "rgba(245, 137, 124, 0.42)";
        } else if (!label.isEmpty()) {
            text = tr("\xE2\x9C\x93 Contact: %1").arg(label);
            fg = light ? "#3E5B94" : "#93B4F0";
            bg = light ? "rgba(85, 118, 178, 0.10)" : "rgba(118, 149, 205, 0.16)";
            brd = light ? "rgba(85, 118, 178, 0.34)" : "rgba(118, 149, 205, 0.42)";
        } else {
            text = tr("\xE2\x9C\x93 Valid KRISTA Address");
            fg = light ? "#3E5B94" : "#93B4F0";
            bg = light ? "rgba(85, 118, 178, 0.10)" : "rgba(118, 149, 205, 0.16)";
            brd = light ? "rgba(85, 118, 178, 0.34)" : "rgba(118, 149, 205, 0.42)";
        }
    }

    lblAddressStatus->setText(text);
    lblAddressStatus->setStyleSheet(QString(
        "QLabel { color: %1; background-color: %2; border: 1px solid %3; border-radius: 10px; padding: 2px 9px; font-size: 11px; font-weight: 600; }"
    ).arg(fg, bg, brd));
    lblAddressStatus->setVisible(true);
}

void SendMultiRow::amountChanged(const QString& amount)
{
    amountInvalid = false;
    if (!amount.isEmpty()) {
        QString amountStr = amount;
        CAmount value = getAmountValue(amountStr);
        if (value > 0) {
            GUIUtil::updateWidgetTextAndCursorPosition(lineEditAmount, amountStr);
        }
    }
    refreshInputStyles();
    Q_EMIT onValueChanged();
}

/**
 * Returns -1 if the value is invalid
 */
CAmount SendMultiRow::getAmountValue(QString amount)
{
    bool isValid = false;
    CAmount value = GUIUtil::parseValue(amount, displayUnit, &isValid);
    return isValid ? value : -1;
}

bool SendMultiRow::addressChanged(const QString& str, bool fOnlyValidate)
{
    QString trimmedStr = str.trimmed();
    if (trimmedStr.isEmpty()) {
        addrInvalid = false;
        refreshInputStyles();
        if (lblAddressStatus) lblAddressStatus->setVisible(false);
        return false;
    }

    if (!walletModel) return false;

    const bool valid = walletModel->validateAddress(trimmedStr);
    if (!valid) {
        SendCoinsRecipient rcp;
        if (GUIUtil::parseBitcoinURI(trimmedStr, &rcp)) {
            lineEditAddress->setText(rcp.address);
            if (rcp.amount > 0) {
                lineEditAmount->setText(BitcoinUnits::format(displayUnit, rcp.amount, false));
            }
            QString label = walletModel->getAddressTableModel()->labelForAddress(rcp.address);
            if (!label.isNull() && !label.isEmpty()) {
                lineEditDescription->setText(label);
                lastAutoFilledLabel = label;
            } else if (!rcp.message.isEmpty()) {
                lineEditDescription->setText(rcp.message);
            }
            Q_EMIT onUriParsed(rcp);
            addrInvalid = false;
            refreshInputStyles();
            updateAddressBadge(rcp.address, walletModel->validateAddress(rcp.address));
            return true;
        }
        addrInvalid = true;
        refreshInputStyles();
        updateAddressBadge(trimmedStr, false);
        return false;
    }

    addrInvalid = false;
    refreshInputStyles();
    if (!fOnlyValidate) {
        QString label = walletModel->getAddressTableModel()->labelForAddress(trimmedStr);
        if (!label.isEmpty()) {
            lineEditDescription->setText(label);
            lastAutoFilledLabel = label;
        } else if (!lastAutoFilledLabel.isEmpty() && lineEditDescription->text() == lastAutoFilledLabel) {
            lineEditDescription->clear();
            lastAutoFilledLabel.clear();
        }
    }
    updateAddressBadge(trimmedStr, true);
    return true;
}

void SendMultiRow::loadWalletModel()
{
    if (walletModel && walletModel->getOptionsModel()) {
        displayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        connect(walletModel->getOptionsModel(), &OptionsModel::displayUnitChanged, this, &SendMultiRow::updateDisplayUnit);
    }
    clear();
    updateDisplayUnit();
}

void SendMultiRow::updateDisplayUnit()
{
    if (!walletModel || !walletModel->getOptionsModel()) return;
    displayUnit = walletModel->getOptionsModel()->getDisplayUnit();
    if (lineEditAmount) {
        lineEditAmount->setPlaceholderText(QStringLiteral("0.00 ") + BitcoinUnits::name(displayUnit));
    }
}

void SendMultiRow::deleteClicked()
{
    Q_EMIT removeEntry(this);
}

void SendMultiRow::clear()
{
    recipient = SendCoinsRecipient();
    if (lineEditAddress) lineEditAddress->clear();
    if (lineEditAmount) lineEditAmount->clear();
    if (lineEditDescription) lineEditDescription->clear();
    lastAutoFilledLabel.clear();
    if (checkSubtractFee) checkSubtractFee->setChecked(false);
    if (lblSubtractHint) lblSubtractHint->setVisible(false);
    if (lblAddressStatus) lblAddressStatus->setVisible(false);
    addrInvalid = false;
    amountInvalid = false;
    refreshInputStyles();
}

bool SendMultiRow::validate()
{
    if (!walletModel)
        return false;

    if (recipient.paymentRequest.IsInitialized())
        return true;

    bool retval = true;
    const QString address = lineEditAddress->text().trimmed();
    if (address.isEmpty()) {
        retval = false;
        addrInvalid = true;
    } else {
        retval = addressChanged(address, true);
        addrInvalid = !retval;
    }

    const CAmount value = getAmountValue(lineEditAmount->text());
    if (value <= 0 || (retval && GUIUtil::isDust(address, value))) {
        amountInvalid = true;
        retval = false;
    } else {
        amountInvalid = false;
    }

    refreshInputStyles();
    return retval;
}

SendCoinsRecipient SendMultiRow::getValue()
{
    if (recipient.paymentRequest.IsInitialized())
        return recipient;

    recipient.address = getAddress();
    recipient.label = lineEditDescription->text();
    recipient.amount = getAmountValue();
    recipient.fSubtractFeeFromAmount = isSubtractFeeFromAmount();
    return recipient;
}

QString SendMultiRow::getAddress()
{
    return lineEditAddress ? lineEditAddress->text().trimmed() : QString();
}

CAmount SendMultiRow::getAmountValue()
{
    return lineEditAmount ? getAmountValue(lineEditAmount->text()) : -1;
}

QRect SendMultiRow::getEditLineRect()
{
    return lineEditAddress ? lineEditAddress->rect() : QRect();
}

int SendMultiRow::getEditHeight()
{
    return lineEditAddress ? lineEditAddress->height() : 40;
}

int SendMultiRow::getEditWidth()
{
    return lineEditAddress ? lineEditAddress->width() : 320;
}

QPoint SendMultiRow::getAddressPopupPos(QWidget* targetParent) const
{
    if (!lineEditAddress || !targetParent) return QPoint(0, 0);
    return lineEditAddress->mapTo(targetParent, QPoint(0, lineEditAddress->height() + 4));
}

int SendMultiRow::getNumber()
{
    return number;
}

void SendMultiRow::setAddress(const QString& address)
{
    if (!lineEditAddress) return;
    lineEditAddress->setText(address);
    if (lineEditAmount) lineEditAmount->setFocus();
}

void SendMultiRow::setAmount(const QString& amount)
{
    if (lineEditAmount) lineEditAmount->setText(amount);
}

void SendMultiRow::setAddressAndLabelOrDescription(const QString& address, const QString& message)
{
    QString label = walletModel ? walletModel->getAddressTableModel()->labelForAddress(address) : QString();
    if (!label.isNull() && !label.isEmpty()) {
        lineEditDescription->setText(label);
    } else if (!message.isEmpty()) {
        lineEditDescription->setText(message);
    }
    setAddress(address);
}

void SendMultiRow::setLabel(const QString& label)
{
    if (lineEditDescription) lineEditDescription->setText(label);
}

void SendMultiRow::setSubtractFeeFromAmount(bool checked)
{
    if (checkSubtractFee) checkSubtractFee->setChecked(checked);
}

bool SendMultiRow::isSubtractFeeFromAmount() const
{
    return checkSubtractFee && checkSubtractFee->isChecked();
}

bool SendMultiRow::isClear()
{
    return !lineEditAddress || lineEditAddress->text().isEmpty();
}

void SendMultiRow::setFocus()
{
    if (lineEditAddress) lineEditAddress->setFocus();
}

void SendMultiRow::focusAmount()
{
    if (lineEditAmount) lineEditAmount->setFocus();
}

void SendMultiRow::updateHeaderTitle()
{
    if (!lblTitle) return;
    if (multiMode) {
        lblTitle->setText(tr("Recipient #%1").arg(number));
    } else {
        lblTitle->setText(tr("Recipient"));
    }
}

void SendMultiRow::setNumber(int _number)
{
    number = _number;
    updateHeaderTitle();
}

void SendMultiRow::setMultiMode(bool isMulti)
{
    multiMode = isMulti;
    updateHeaderTitle();
    setRemoveVisible(isMulti);
}

void SendMultiRow::hideLabels()
{
    setMultiMode(true);
}

void SendMultiRow::showLabels()
{
    setMultiMode(false);
}

void SendMultiRow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
}

void SendMultiRow::setRemoveVisible(bool visible)
{
    if (btnRemoveEntry) {
        btnRemoveEntry->setVisible(visible);
    }
}

void SendMultiRow::enterEvent(QEvent *)
{
    isExpanded = true;
}

void SendMultiRow::leaveEvent(QEvent *)
{
    isExpanded = false;
}

int SendMultiRow::getMenuBtnWidth()
{
    return 0;
}

SendMultiRow::~SendMultiRow()
{
}
