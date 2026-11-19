// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/send.h"
#include "qt/kristatech/forms/ui_send.h"
#include "qt/kristatech/addnewcontactdialog.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/sendchangeaddressdialog.h"
#include "qt/kristatech/optionbutton.h"
#include "qt/kristatech/sendconfirmdialog.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/myaddressrow.h"
#include "qt/kristatech/guitransactionsutils.h"
#include "clientmodel.h"
#include "optionsmodel.h"
#include "addresstablemodel.h"
#include "coincontrol.h"
#include "chainparams.h"
#include "main.h"
#include "masternode.h"
#include "masternodeconfig.h"
#include "wallet/wallet.h"
#include "script/standard.h"
#include "openuridialog.h"
#include "rpc/server.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>

SendWidget::SendWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::send),
    coinIcon(new QPushButton()),
    btnContacts(new QPushButton())
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    /* Containers */
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(0,20,0,20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20,10,20,20);

    /* Light Font */
    QFont fontLight;
    fontLight.setWeight(QFont::Light);

    /* Title */
    setCssProperty(ui->labelTitle, "text-title-screen");
    ui->labelTitle->setFont(fontLight);

    /* Subtitle */
    setCssProperty(ui->labelSubtitle1, "text-subtitle");

    /* Address - Amount*/
    setCssProperty({ui->labelSubtitleAddress, ui->labelSubtitleAmount}, "text-title");

    /* Buttons */
    setCssBtnSecondary(ui->pushButtonFee);
    setCssProperty(ui->pushButtonClear, "btn-secundary-clear");
    setCssProperty(ui->pushButtonAddRecipient, "btn-secundary-add");
    setCssBtnPrimary(ui->pushButtonSave);
    setCssBtnSecondary(ui->pushButtonReset);

    // Coin control
    ui->btnCoinControl->setTitleClassAndText("btn-title-grey", tr("Coin Control"));
    ui->btnCoinControl->setSubTitleClassAndText("text-subtitle", tr("Select the source of the coins"));

    // Change address option
    ui->btnChangeAddress->setTitleClassAndText("btn-title-grey", tr("Change Address"));
    ui->btnChangeAddress->setSubTitleClassAndText("text-subtitle", tr("Customize the change address"));

    // Uri
    ui->btnUri->setTitleClassAndText("btn-title-grey", tr("Open URI"));
    ui->btnUri->setSubTitleClassAndText("text-subtitle", tr("Parse a payment request"));

    // Contacts Option
    ui->btnContacts->setTitleClassAndText("btn-title-grey", tr("Contacts"));
    ui->btnContacts->setSubTitleClassAndText("text-subtitle", tr("Manage your address book"));

    // PSBT Option
    if (ui->btnContacts->parentWidget() && ui->btnContacts->parentWidget()->layout()) {
        btnPsbt = new OptionButton(ui->btnContacts->parentWidget());
        btnPsbt->setMinimumHeight(ui->btnContacts->minimumHeight());
        btnPsbt->setTitleClassAndText("btn-title-grey", tr("Sign / Broadcast PSBT"));
        btnPsbt->setSubTitleClassAndText("text-subtitle", tr("Inspect, sign, or finalize BIP174 PSBT"));
        if (QVBoxLayout* vbox = qobject_cast<QVBoxLayout*>(ui->btnContacts->parentWidget()->layout())) {
            int idx = vbox->indexOf(ui->btnContacts);
            vbox->insertWidget(idx >= 0 ? idx + 1 : vbox->count(), btnPsbt);
        } else {
            ui->btnContacts->parentWidget()->layout()->addWidget(btnPsbt);
        }
        connect(btnPsbt, &OptionButton::clicked, this, &SendWidget::onPsbtClicked);
    }

    connect(ui->pushButtonFee, &QPushButton::clicked, this, &SendWidget::onChangeCustomFeeClicked);
    connect(ui->btnCoinControl, &OptionButton::clicked, this, &SendWidget::onCoinControlClicked);
    connect(ui->btnChangeAddress, &OptionButton::clicked, this, &SendWidget::onChangeAddressClicked);
    connect(ui->btnUri, &OptionButton::clicked, this, &SendWidget::onOpenUriClicked);
    connect(ui->btnContacts, &OptionButton::clicked, this, &SendWidget::onContactsOptionClicked);
    connect(ui->pushButtonReset, &QPushButton::clicked, [this](){ onResetCustomOptions(true); });

    setCssProperty(ui->coinWidget, "container-coin-type");
    setCssProperty(ui->labelLine, "container-divider");


    // Total Send
    setCssProperty(ui->labelTitleTotalSend, "text-title");
    setCssProperty(ui->labelAmountSend, "text-body1");

    // Total Remaining
    setCssProperty(ui->labelTitleTotalRemaining, "text-title");
    setCssProperty(ui->labelAmountRemaining, "text-body1");

    // Icon Send
    ui->stackedWidget->addWidget(coinIcon);
    coinIcon->show();
    coinIcon->raise();

    setCssProperty(coinIcon, "coin-icon-piv");

    QSize BUTTON_SIZE = QSize(24, 24);
    coinIcon->setMinimumSize(BUTTON_SIZE);
    coinIcon->setMaximumSize(BUTTON_SIZE);

    int posX = 0;
    int posY = 20;
    coinIcon->move(posX, posY);

    // Entry
    addEntry();

    // Init custom fee false (updated in loadWalletModel)
    setCustomFeeSelected(false);

    // Connect
    connect(ui->pushButtonSave, &QPushButton::clicked, this, &SendWidget::onSendClicked);
    connect(ui->pushButtonAddRecipient, &QPushButton::clicked, this, &SendWidget::onAddEntryClicked);
    connect(ui->pushButtonClear, &QPushButton::clicked, [this](){clearAll(true);});

    coinControlDialog = new CoinControlDialog();
}

void SendWidget::refreshAmounts()
{
    CAmount total = 0;
    QMutableListIterator<SendMultiRow*> it(entries);
    while (it.hasNext()) {
        SendMultiRow* entry = it.next();
        CAmount amount = entry->getAmountValue();
        if (amount > 0)
            total += amount;
    }

    nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();

    ui->labelAmountSend->setText(GUIUtil::formatBalance(total, nDisplayUnit));

    CAmount totalAmount = 0;
    if (coinControlDialog->coinControl->HasSelected()) {
        // Set remaining balance to the sum of the coinControl selected inputs
        totalAmount = walletModel->getBalance(coinControlDialog->coinControl) - total;
        ui->labelTitleTotalRemaining->setText(tr("Total remaining from the selected UTXO"));
    } else {
        // Wallet's unlocked balance
        totalAmount = walletModel->getUnlockedBalance() - total;
        ui->labelTitleTotalRemaining->setText(tr("Unlocked remaining"));
    }
    ui->labelAmountRemaining->setText(
            GUIUtil::formatBalance(
                    totalAmount,
                    nDisplayUnit
                    )
    );
}

void SendWidget::loadClientModel()
{
    if (clientModel) {
        connect(clientModel, &ClientModel::numBlocksChanged, [this](){
            if (customFeeDialog) customFeeDialog->updateFee();
        });
    }
}

void SendWidget::loadWalletModel()
{
    if (walletModel) {
        coinControlDialog->setModel(walletModel);
        if (walletModel->getOptionsModel()) {
            // display unit
            nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        }

        // set walletModel for entries
        for (SendMultiRow *entry : entries) {
            if (entry) {
                entry->setWalletModel(walletModel);
            }
        }

        // Restore custom fee from wallet Settings
        CAmount nCustomFee;
        if (walletModel->getWalletCustomFee(nCustomFee)) {
            setCustomFeeSelected(true, nCustomFee);
        }

        // Refresh
        refreshAmounts();

        // TODO: This only happen when the coin control features are modified in other screen, check before do this if the wallet has another screen modifying it.
        // Coin Control
        //connect(walletModel->getOptionsModel(), &OptionsModel::coinControlFeaturesChanged, [this](){});
        //ui->frameCoinControl->setVisible(model->getOptionsModel()->getCoinControlFeatures());
        //coinControlUpdateLabels();
    }
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
    coinControlDialog->coinControl->SetNull();
    ui->btnChangeAddress->setActive(false);
    ui->btnCoinControl->setActive(false);
    if (fRefreshAmounts) {
        refreshAmounts();
    }
}

void SendWidget::clearEntries()
{
    int num = entries.length();
    for (int i = 0; i < num; ++i) {
        ui->scrollAreaWidgetContents->layout()->takeAt(0)->widget()->deleteLater();
    }
    entries.clear();

    addEntry();
}

void SendWidget::addEntry()
{
    if (entries.isEmpty()) {
        createEntry();
    } else {
        if (entries.length() == 1) {
            SendMultiRow *entry = entries.at(0);
            entry->hideLabels();
            entry->setNumber(1);
        } else if (entries.length() == MAX_SEND_POPUP_ENTRIES) {
            inform(tr("Maximum amount of outputs reached"));
            return;
        }

        SendMultiRow *sendMultiRow = createEntry();
        sendMultiRow->setNumber(entries.length());
        sendMultiRow->hideLabels();
    }
    setFocusOnLastEntry();
}

SendMultiRow* SendWidget::createEntry()
{
    SendMultiRow *sendMultiRow = new SendMultiRow(this);
    if (this->walletModel) sendMultiRow->setWalletModel(this->walletModel);
    entries.append(sendMultiRow);
    ui->scrollAreaWidgetContents->layout()->addWidget(sendMultiRow);
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
    connect(sendMultiRow, &SendMultiRow::onValueChanged, this, &SendWidget::onValueChanged);
    const bool showRemove = entries.size() > 1;
    for (SendMultiRow* row : entries) {
        if (row) row->setRemoveVisible(showRemove);
    }
    return sendMultiRow;
}

void SendWidget::onAddEntryClicked()
{
    // Check prev valid entries before add a new one.
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
    // Set focus on last recipient address when Send-window is displayed
    setFocusOnLastEntry();
}

void SendWidget::setFocusOnLastEntry()
{
    if (!entries.isEmpty()) entries.last()->setFocus();
}

void SendWidget::onSendClicked()
{
    if (!walletModel || !walletModel->getOptionsModel())
        return;

    QList<SendCoinsRecipient> recipients;

    for (SendMultiRow* entry : entries) {
        // TODO: Check UTXO splitter here..
        // Validate send..
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
        // Unlock wallet was cancelled
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

    if (prepareStatus.status == WalletModel::TransactionCreationFailed && !coinControlDialog->coinControl->HasSelected()) {
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

    // process prepareStatus and on error generate message shown to user
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

    QString warningStr = QString();

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
            const std::string prevIdxStr = std::to_string(txin.prevout.n);
            for (const CMasternodeConfig::CMasternodeEntry& mne : masternodeConfig.getEntries()) {
                if (mne.getTxHash() == prevHashHex && mne.getOutputIndex() == prevIdxStr) {
                    fSpendsMNCollateral = true;
                    matchedOutpointStr = QString("%1 (%2:%3)").arg(QString::fromStdString(mne.getAlias()),
                                                                   QString::fromStdString(prevHashHex).left(14) + "...",
                                                                   QString::fromStdString(prevIdxStr));
                    break;
                }
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
        // now send the prepared transaction
        WalletModel::SendCoinsReturn sendStatus = dialog->getStatus();
        // process sendStatus and on error generate message shown to user
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
    QString s = "";
    for (SendCoinsRecipient rec : recipients) {
        s += rec.address + " -> " + BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(), rec.amount, false, BitcoinUnits::separatorAlways) + "\n";
    }
    return s;
}

void SendWidget::updateEntryLabels(QList<SendCoinsRecipient> recipients)
{
    for (SendCoinsRecipient rec : recipients) {
        QString label = rec.label;
        if (!label.isNull()) {
            QString labelOld = walletModel->getAddressTableModel()->labelForAddress(rec.address);
            if (label.compare(labelOld) != 0) {
                CTxDestination dest = DecodeDestination(rec.address.toStdString());
                if (!walletModel->updateAddressBookLabels(dest, label.toStdString(),
                                                          this->walletModel->isMine(dest) ?
                                                                  AddressBook::AddressBookPurpose::RECEIVE :
                                                                  AddressBook::AddressBookPurpose::SEND)) {
                    // Label update failed
                    Q_EMIT message("", tr("Address label update failed for address: %1").arg(rec.address), CClientUIInterface::MSG_ERROR);
                    return;
                }
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
        // User clicked Cancel or ESC — preserve existing custom change address!
        dialog->deleteLater();
        return;
    }

    CTxDestination destChange = dialog->getDestination();

    if (!IsValidDestination(destChange)) {
        // User explicitly cleared the change address
        ui->btnChangeAddress->setActive(false);
    } else {
        // Ask confirmation if external address
        if (!walletModel->isMine(destChange) && !ask(tr("Warning!"),
                tr("The change address doesn't belong to this wallet.\n\nDo you want to continue?"))) {
            dialog->deleteLater();
            return;
        }
        ui->btnChangeAddress->setActive(true);
    }

    // save change address in coin control
    coinControlDialog->coinControl->destChange = destChange;
    dialog->deleteLater();
}

void SendWidget::onOpenUriClicked()
{
    showHideOp(true);
    OpenURIDialog *dlg = new OpenURIDialog(window);
    if (openDialogWithOpaqueBackgroundY(dlg, window, 3, 5)) {

        SendCoinsRecipient rcp;
        if (!GUIUtil::parseBitcoinURI(dlg->getURI(), &rcp)) {
            inform(tr("Invalid URI"));
            return;
        }
        if (!walletModel->validateAddress(rcp.address)) {
            inform(tr("Invalid address in URI"));
            return;
        }

        int listSize = entries.size();
        if (listSize == 1) {
            SendMultiRow *entry = entries[0];
            entry->setAddressAndLabelOrDescription(rcp.address, rcp.message);
            entry->setAmount(BitcoinUnits::format(nDisplayUnit, rcp.amount, false));
        } else {
            // Use the last one if it's invalid or add a new one
            SendMultiRow *entry = entries[listSize - 1];
            if (!entry->validate()) {
                addEntry();
                entry = entries[listSize];
            }
            entry->setAddressAndLabelOrDescription(rcp.address, rcp.message);
            entry->setAmount(BitcoinUnits::format(nDisplayUnit, rcp.amount, false));
        }
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
    }
}

void SendWidget::onCoinControlClicked()
{
    if (walletModel->getBalance() > 0) {
        setCoinControlPayAmounts();
        coinControlDialog->refreshDialog();
        coinControlDialog->exec();
        ui->btnCoinControl->setActive(coinControlDialog->coinControl->HasSelected());
        refreshAmounts();
    } else {
        inform(tr("You don't have any %1 to select.").arg(CURRENCY_UNIT.c_str()));
    }
}

void SendWidget::setCoinControlPayAmounts()
{
    if (!coinControlDialog) return;
    coinControlDialog->clearPayAmounts();
    QMutableListIterator<SendMultiRow*> it(entries);
    while (it.hasNext()) {
        coinControlDialog->addPayAmount(it.next()->getAmountValue());
    }
}

void SendWidget::onValueChanged()
{
    refreshAmounts();
}

void SendWidget::onContactsClicked(SendMultiRow* entry)
{
    focusedEntry = entry;
    if (menu && menu->isVisible()) {
        menu->hide();
    }

    int contactsSize = walletModel->getAddressTableModel()->sizeSend();
    if (contactsSize == 0) {
        inform(tr("No contacts available, you can go to the contacts screen and add some there!"));
        return;
    }

    int height = (contactsSize <= 2) ? entry->getEditHeight() * ( 2 * (contactsSize + 1 )) : entry->getEditHeight() * 6;
    int width = entry->getEditWidth();

    if (!menuContacts) {
        menuContacts = new ContactsDropdown(
                    width,
                    height,
                    this
        );
        menuContacts->setWalletModel(walletModel, AddressTableModel::Send);
        connect(menuContacts, &ContactsDropdown::contactSelected, [this](QString address, QString label) {
            if (focusedEntry) {
                if (label != "(no label)")
                    focusedEntry->setLabel(label);
                focusedEntry->setAddress(address);
            }
        });

    }

    if (menuContacts->isVisible()) {
        menuContacts->hide();
        return;
    }

    menuContacts->resizeList(width, height);
    menuContacts->setStyleSheet(this->styleSheet());
    menuContacts->adjustSize();

    QPoint pos;
    if (entries.size() > 1) {
        pos = entry->pos();
        pos.setY((pos.y() + (focusedEntry->getEditHeight() - 12) * 4));
    } else {
        pos = focusedEntry->getEditLineRect().bottomLeft();
        pos.setY((pos.y() + (focusedEntry->getEditHeight() - 12) * 3));
    }
    pos.setX(pos.x() + 20);
    menuContacts->move(pos);
    menuContacts->show();
}

void SendWidget::onMenuClicked(SendMultiRow* entry)
{
    focusedEntry = entry;
    if (menuContacts && menuContacts->isVisible()) {
        menuContacts->hide();
    }
    QPoint pos = entry->pos();
    pos.setX(pos.x() + (entry->width() - entry->getMenuBtnWidth()));
    pos.setY(pos.y() + entry->height() + (entry->getMenuBtnWidth()));

    if (!this->menu) {
        this->menu = new TooltipMenu(window, this);
        this->menu->setCopyBtnVisible(false);
        this->menu->setEditBtnText(tr("Save contact"));
        this->menu->setMinimumSize(this->menu->width() + 30,this->menu->height());
        connect(this->menu, &TooltipMenu::message, this, &AddressesWidget::message);
        connect(this->menu, &TooltipMenu::onEditClicked, this, &SendWidget::onContactMultiClicked);
        connect(this->menu, &TooltipMenu::onDeleteClicked, this, &SendWidget::onDeleteClicked);
    } else {
        this->menu->hide();
    }
    this->menu->setDeleteBtnVisible(entries.size() > 1);
    menu->move(pos);
    menu->show();
}

void SendWidget::onMaxClicked(SendMultiRow* entry)
{
    if (!entry || !walletModel) return;

    CAmount otherTotal = 0;
    for (SendMultiRow* e : entries) {
        if (e && e != entry) {
            CAmount val = e->getAmountValue();
            if (val > 0) otherTotal += val;
        }
    }

    const CAmount available = coinControlDialog->coinControl->HasSelected()
                                  ? walletModel->getBalance(coinControlDialog->coinControl)
                                  : walletModel->getUnlockedBalance();
    const CAmount remaining = available - otherTotal;
    if (remaining <= 0) {
        inform(tr("No unlocked balance remaining to fill."));
        return;
    }

    entry->setAmount(BitcoinUnits::format(nDisplayUnit, remaining, false, BitcoinUnits::separatorNever));
    entry->setSubtractFeeFromAmount(true);
    refreshAmounts();
}

void SendWidget::onContactMultiClicked()
{
    if (focusedEntry) {
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
        QString label = walletModel->getAddressTableModel()->labelForAddress(address);
        if (!label.isNull()) {
            dialog->setTexts(tr("Update Contact"), "Edit label for the selected address:\n%1");
            dialog->setData(address, label);
        } else {
            dialog->setTexts(tr("Create New Contact"), "Save label for the selected address:\n%1");
            dialog->setData(address, "");
        }
        openDialogWithOpaqueBackgroundY(dialog, window, 3, 5);
        if (dialog->res) {
            if (label == dialog->getLabel()) {
                return;
            }
            if (walletModel->updateAddressBookLabels(pivAdd, dialog->getLabel().toStdString(),
                    AddressBook::AddressBookPurpose::SEND)) {
                inform(tr("New Contact Stored"));
            } else {
                inform(tr("Error Storing Contact"));
            }
        }
        dialog->deleteLater();
    }

}

void SendWidget::onDeleteClicked()
{
    if (focusedEntry) {
        focusedEntry->hide();
        focusedEntry->deleteLater();
        int entryNumber = focusedEntry->getNumber();

        // remove selected entry and update row number for the others
        QMutableListIterator<SendMultiRow*> it(entries);
        while (it.hasNext()) {
            SendMultiRow* entry = it.next();
            if (focusedEntry == entry) {
                it.remove();
            } else if (focusedEntry && entry->getNumber() > entryNumber) {
                entry->setNumber(entry->getNumber() - 1);
            }
        }

        if (entries.size() == 1) {
            SendMultiRow* sendMultiRow = QMutableListIterator<SendMultiRow*>(entries).next();
            sendMultiRow->setNumber(entries.length());
            sendMultiRow->showLabels();
            sendMultiRow->setRemoveVisible(false);
        }

        focusedEntry = nullptr;

        // Update total amounts
        refreshAmounts();
        setFocusOnLastEntry();
    }
}

void SendWidget::resizeMenu()
{
    if (menuContacts && menuContacts->isVisible() && focusedEntry) {
        int width = focusedEntry->getEditWidth();
        menuContacts->resizeList(width, menuContacts->height());
        menuContacts->resize(width, menuContacts->height());
        QPoint pos = focusedEntry->getEditLineRect().bottomLeft();
        pos.setX(pos.x() + 20);
        pos.setY(pos.y() + ((focusedEntry->getEditHeight() - 12)  * 3));
        menuContacts->move(pos);
    }
}

void SendWidget::setCustomFeeSelected(bool isSelected, const CAmount& customFee)
{
    isCustomFeeSelected = isSelected;
    ui->pushButtonFee->setText(isCustomFeeSelected ?
                    tr("Custom Fee %1").arg(BitcoinUnits::formatWithUnit(nDisplayUnit, customFee) + "/kB") :
                    tr("Customize Fee"));
    if (walletModel)
        walletModel->setWalletDefaultFee(customFee);
}

void SendWidget::onPsbtClicked()
{
    const bool light = isLightTheme();
    const QString bg      = light ? "#F8FAFC" : "#0D1321";
    const QString cardBg  = light ? "#FFFFFF" : "#111A2E";
    const QString textCol = light ? "#0F172A" : "#F1F5F9";
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

    btnSign->setStyleSheet("QPushButton { background-color: #5576B2; color: #FFFFFF; border: 1px solid #48669E; border-radius: 6px; padding: 6px 14px; font-weight: 600; } QPushButton:hover { background-color: #48669E; }");
    btnSend->setStyleSheet("QPushButton { background-color: #059669; color: #FFFFFF; border: 1px solid #047857; border-radius: 6px; padding: 6px 14px; font-weight: 600; } QPushButton:hover { background-color: #047857; }");

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
        QByteArray raw = f.readAll();
        if (raw.startsWith("psbt\xff")) {
            editPsbt->setPlainText(QString::fromLatin1(raw.toBase64()));
        } else {
            editPsbt->setPlainText(QString::fromUtf8(raw).trimmed());
        }
    });

    connect(btnDecode, &QPushButton::clicked, &dlg, [&]() {
        QString b64 = editPsbt->toPlainText().trimmed();
        if (b64.isEmpty()) return;
        try {
            UniValue p(UniValue::VARR);
            p.push_back(b64.toStdString());
            UniValue res = runRpc("decodepsbt", p);
            editResult->setPlainText(QString::fromStdString(res.write(2)));
        } catch (const UniValue& err) {
            editResult->setPlainText(QString::fromStdString(find_value(err, "message").get_str()));
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
            editResult->setPlainText(QString::fromStdString(find_value(err, "message").get_str()));
        } catch (const std::exception& e) {
            editResult->setPlainText(QString::fromStdString(e.what()));
        }
    });

    connect(btnCopy, &QPushButton::clicked, &dlg, [&]() {
        QApplication::clipboard()->setText(editPsbt->toPlainText().trimmed());
        inform(tr("PSBT copied to clipboard"));
    });

    connect(btnSend, &QPushButton::clicked, &dlg, [&]() {
        QString b64 = editPsbt->toPlainText().trimmed();
        if (b64.isEmpty()) return;
        try {
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
                       "• Raw Transaction Size: %1 bytes\n\n"
                       "Once broadcast, this transaction cannot be reversed.")
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
            editResult->setPlainText(QString::fromStdString(find_value(err, "message").get_str()));
        } catch (const std::exception& e) {
            editResult->setPlainText(QString::fromStdString(e.what()));
        }
    });

    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    dlg.exec();
}

void SendWidget::changeTheme(bool isLightTheme, QString& theme)
{
    coinControlDialog->setStyleSheet(theme);
}

SendWidget::~SendWidget()
{
    delete ui;
    delete coinControlDialog;
}
