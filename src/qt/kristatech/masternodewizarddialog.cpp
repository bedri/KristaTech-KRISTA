// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/masternodewizarddialog.h"
#include "qt/kristatech/forms/ui_masternodewizarddialog.h"

#include "activemasternode.h"
#include "crypto/bls.h"
#include "masternodeconfig.h"
#include "utilstrencodings.h"
#include "optionsmodel.h"
#include "pairresult.h"
#include "qt/kristatech/defaultdialog.h"
#include "qt/kristatech/mnmodel.h"
#include "qt/kristatech/guitransactionsutils.h"
#include "qt/kristatech/qtutils.h"

#include <QFile>
#include <QIntValidator>
#include <QHostAddress>
#include <QRegularExpression>
#include <QRegularExpressionValidator>

MasterNodeWizardDialog::MasterNodeWizardDialog(WalletModel *model, QWidget *parent) :
    FocusedDialog(parent),
    ui(new Ui::MasterNodeWizardDialog),
    icConfirm1(new QPushButton()),
    icConfirm3(new QPushButton()),
    icConfirm4(new QPushButton()),
    walletModel(model)
{
    ui->setupUi(this);
    setMinimumSize(720, 680);
    resize(720, 680);

    if (parent) {
        this->setStyleSheet(parent->styleSheet());
    }
    setCssProperty(ui->frame, "container-dialog");
    ui->frame->setContentsMargins(10,10,10,10);

    setCssProperty({ui->labelLine1, ui->labelLine3}, "line-purple");
    setCssProperty({ui->groupBoxName, ui->groupContainer}, "container-border");
    setCssProperty({ui->pushNumber1, ui->pushNumber3, ui->pushNumber4}, "btn-number-check");
    setCssProperty({ui->pushName1, ui->pushName3, ui->pushName4}, "btn-name-check");

    ui->pushNumber1->setEnabled(false);
    ui->pushNumber3->setEnabled(false);
    ui->pushNumber4->setEnabled(false);
    ui->pushName1->setEnabled(false);
    ui->pushName3->setEnabled(false);
    ui->pushName4->setEnabled(false);

    // Frame 1
    setCssProperty(ui->labelTitle1, "text-title-dialog");
    setCssProperty(ui->labelMessage1a, "text-main-grey");
    setCssProperty(ui->labelMessage1b, "text-main-purple");

    // Frame 3
    setCssProperty(ui->labelTitle3, "text-title-dialog");
    setCssProperty(ui->labelMessage3, "text-main-grey");

    initCssEditLine(ui->lineEditName);
    ui->lineEditName->setMinimumHeight(40);
    // MN alias must not contain spaces or "#" character
    QRegularExpression rx("^(?:(?![\\#\\s]).)*");
    ui->lineEditName->setValidator(new QRegularExpressionValidator(rx, ui->lineEditName));

    // Frame 4
    setCssProperty(ui->labelTitle4, "text-title-dialog");
    setCssProperty({ui->labelSubtitleIp, ui->labelSubtitlePort}, "text-title");
    setCssSubtitleScreen(ui->labelSubtitleAddressIp);
    ui->labelSubtitleAddressIp->setWordWrap(true);
    ui->labelSubtitleAddressIp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui->verticalLayout_5->setSpacing(6);

    initCssEditLine(ui->lineEditIpAddress);
    initCssEditLine(ui->lineEditPort);
    ui->lineEditIpAddress->setMinimumHeight(40);
    ui->lineEditPort->setMinimumHeight(40);

    const QString defaultPortQStr = QString::number(Params().GetDefaultPort());
    ui->lineEditName->setPlaceholderText(tr("e.g. mn01 (unique alias, no spaces)"));
    ui->lineEditPort->setPlaceholderText(defaultPortQStr);
    if (ui->lineEditPort->text().trimmed().isEmpty()) {
        ui->lineEditPort->setText(defaultPortQStr);
    }

    labelNameStatus = new QLabel(this);
    labelNameStatus->setWordWrap(true);
    labelNameStatus->setAlignment(Qt::AlignCenter);
    labelNameStatus->setStyleSheet("font-size: 13px; color: #64748b;");
    ui->verticalLayout_4->insertWidget(3, labelNameStatus);

    const bool fDip3Active = Params().GetConsensus().NetworkUpgradeActive(
        chainActive.Height() + 1, Consensus::UPGRADE_DIP3);

    QLabel* labelSubtitlePrivKey = new QLabel(
        fDip3Active ? tr("Operator BLS Public Key (optional)") : tr("Masternode private key (optional)"),
        this);
    labelSubtitlePrivKey->setAlignment(Qt::AlignCenter);
    setCssSubtitleScreen(labelSubtitlePrivKey);
    lineEditPrivKey = new QLineEdit(this);
    initCssEditLine(lineEditPrivKey);
    lineEditPrivKey->setMinimumHeight(40);
    lineEditPrivKey->setPlaceholderText(
        fDip3Active
            ? tr("Leave blank to auto-generate a 48-byte BLS operator keypair")
            : tr("Leave blank to generate a new key"));

    labelAddressStatus = new QLabel(this);
    labelAddressStatus->setWordWrap(true);
    labelAddressStatus->setAlignment(Qt::AlignCenter);
    labelAddressStatus->setStyleSheet("font-size: 13px; color: #64748b;");

    const int insertIdx = std::max(0, ui->verticalLayout_5->count() - 1);
    ui->verticalLayout_5->insertSpacing(insertIdx, 8);
    ui->verticalLayout_5->insertWidget(insertIdx + 1, labelSubtitlePrivKey);
    ui->verticalLayout_5->insertWidget(insertIdx + 2, lineEditPrivKey);
    ui->verticalLayout_5->insertSpacing(insertIdx + 3, 4);
    ui->verticalLayout_5->insertWidget(insertIdx + 4, labelAddressStatus);

    connect(ui->lineEditName, &QLineEdit::textChanged, this, &MasterNodeWizardDialog::updateNameValidation);
    connect(ui->lineEditName, &QLineEdit::returnPressed, this, &MasterNodeWizardDialog::accept);
    connect(ui->lineEditIpAddress, &QLineEdit::textChanged, this, &MasterNodeWizardDialog::updateAddressValidation);
    connect(ui->lineEditPort, &QLineEdit::textChanged, this, &MasterNodeWizardDialog::updateAddressValidation);
    connect(lineEditPrivKey, &QLineEdit::textChanged, this, &MasterNodeWizardDialog::updateAddressValidation);
    connect(lineEditPrivKey, &QLineEdit::returnPressed, this, &MasterNodeWizardDialog::accept);

    if (walletModel) {
        int nTipHeight = 0;
        {
            LOCK(cs_main);
            nTipHeight = chainActive.Height();
        }
        const CAmount mnCollateral = CMasternode::GetMasternodeNodeCollateral(nTipHeight);
        const int dispUnit = walletModel->getOptionsModel() ? walletModel->getOptionsModel()->getDisplayUnit() : BitcoinUnits::PIV;
        const QString collateralStr = BitcoinUnits::formatWithUnit(dispUnit, mnCollateral, false, BitcoinUnits::separatorAlways);
        COutPoint existingCollateral;
        const bool hasCandidate = walletModel->getMNCollateralCandidate(existingCollateral);
        ui->labelMessage1a->setText(fDip3Active
            ? tr("To create a KTDIP3 Deterministic Masternode you must lock %1 as collateral (%2 confirmations). "
                 "Once registered on-chain via ProRegTx, no P2P pings are required and multiple Masternodes may share an IP using distinct ports/BLS keys (Rule 8).")
                .arg(collateralStr)
                .arg(MASTERNODE_MIN_CONFIRMATIONS)
            : tr("To create a KRISTA Masternode you must lock %1 as collateral. "
                 "These coins remain in your wallet and can be unlocked at any time.\n\n"
                 "After creation, wait %2 confirmations before starting the Masternode and add the generated '<alias> <privkey>' line to activemasternode.conf on your remote node.")
                .arg(collateralStr)
                .arg(MASTERNODE_MIN_CONFIRMATIONS));
        ui->labelMessage1b->setText(
            hasCandidate
                ? tr("Ready: an unlocked %1 collateral UTXO was found in your wallet.").arg(collateralStr)
                : tr("Required collateral: %1 (+ network fee).").arg(collateralStr));
    }
    ui->stackedWidget->setCurrentIndex(pos);
    ui->lineEditPort->setEnabled(true);    // Rule 8 supports custom ports for multi-MN per IP
    ui->lineEditPort->setText(QString::fromStdString(std::to_string(Params().GetDefaultPort())));

    // Confirm icons
    ui->stackedIcon1->addWidget(icConfirm1);
    ui->stackedIcon3->addWidget(icConfirm3);
    ui->stackedIcon4->addWidget(icConfirm4);
    initBtn({icConfirm1, icConfirm3, icConfirm4});
    setCssProperty({icConfirm1, icConfirm3, icConfirm4}, "ic-step-confirm");

    // Connect btns
    setCssBtnPrimary(ui->btnNext);
    setCssProperty(ui->btnBack , "btn-dialog-cancel");
    ui->btnBack->setVisible(false);
    setCssProperty(ui->pushButtonSkip, "ic-close");

    connect(ui->pushButtonSkip, &QPushButton::clicked, this, &MasterNodeWizardDialog::close);
    connect(ui->btnNext, &QPushButton::clicked, this, &MasterNodeWizardDialog::accept);
    connect(ui->btnBack, &QPushButton::clicked, this, &MasterNodeWizardDialog::onBackClicked);
}

void MasterNodeWizardDialog::showEvent(QShowEvent *event)
{
    if (ui->btnNext) ui->btnNext->setFocus();
}

void MasterNodeWizardDialog::updateStepButtons()
{
    ui->btnNext->setText(pos == 2 ? tr("CREATE") : tr("NEXT"));
}

void MasterNodeWizardDialog::updateNameValidation()
{
    const QString raw = ui->lineEditName->text().trimmed();
    if (raw.isEmpty()) {
        setCssEditLineDialog(ui->lineEditName, true, true);
        if (labelNameStatus) {
            labelNameStatus->setStyleSheet("font-size: 13px; color: #64748b;");
            labelNameStatus->setText(tr("Enter a short alias without spaces (e.g. mn01)."));
        }
        return;
    }
    if (raw.contains(QRegExp("\\s"))) {
        setCssEditLineDialog(ui->lineEditName, false, true);
        if (labelNameStatus) {
            labelNameStatus->setStyleSheet("font-size: 13px; color: #e11d48;");
            labelNameStatus->setText(tr("Alias cannot contain spaces."));
        }
        return;
    }
    const std::string alias = raw.toStdString();
    for (const CMasternodeConfig::CMasternodeEntry& existing : masternodeConfig.getEntries()) {
        if (existing.getAlias() == alias) {
            setCssEditLineDialog(ui->lineEditName, false, true);
            if (labelNameStatus) {
                labelNameStatus->setStyleSheet("font-size: 13px; color: #e11d48;");
                labelNameStatus->setText(tr("Alias '%1' already exists in masternode.conf.").arg(raw));
            }
            return;
        }
    }
    setCssEditLineDialog(ui->lineEditName, true, true);
    if (labelNameStatus) {
        labelNameStatus->setStyleSheet("font-size: 13px; color: #16a34a;");
        labelNameStatus->setText(tr("Alias '%1' is available.").arg(raw));
    }
}

void MasterNodeWizardDialog::updateAddressValidation()
{
    const QString ipStr = ui->lineEditIpAddress->text().trimmed();
    const QString portStr = ui->lineEditPort->text().trimmed();
    const QString privKeyStr = lineEditPrivKey ? lineEditPrivKey->text().trimmed() : QString();

    bool ipOk = !ipStr.isEmpty() && MNModel::validateMNIP(ipStr);
    bool portOk = false;
    const int portNum = portStr.toInt(&portOk);
    portOk = portOk && portNum > 0 && portNum <= 65535;

    const bool fDip3Active = Params().GetConsensus().NetworkUpgradeActive(
        chainActive.Height() + 1, Consensus::UPGRADE_DIP3);

    bool keyOk = true;
    if (!privKeyStr.isEmpty()) {
        if (fDip3Active) {
            std::vector<unsigned char> vchOpPub = ParseHex(privKeyStr.toStdString());
            CBLSPubKey testPk;
            keyOk = IsHex(privKeyStr.toStdString()) && vchOpPub.size() == 48 &&
                    testPk.SetBuf(vchOpPub.data(), vchOpPub.size()) && testPk.IsValid();
        } else {
            CKey testKey = DecodeSecret(privKeyStr.toStdString());
            keyOk = testKey.IsValid();
        }
    }

    setCssEditLineDialog(ui->lineEditIpAddress, ipStr.isEmpty() || ipOk, true);
    setCssEditLineDialog(ui->lineEditPort, portStr.isEmpty() || portOk, true);
    if (lineEditPrivKey) {
        setCssEditLineDialog(lineEditPrivKey, keyOk, true);
    }

    if (!labelAddressStatus) return;

    if (ipStr.isEmpty() && privKeyStr.isEmpty()) {
        labelAddressStatus->setStyleSheet("font-size: 13px; color: #64748b;");
        labelAddressStatus->setText(fDip3Active
            ? tr("Default P2P port is %1 (Rule 8 supports custom ports). Leave BLS key blank to auto-generate.").arg(Params().GetDefaultPort())
            : tr("Default P2P port is %1. Leave private key blank to auto-generate.").arg(Params().GetDefaultPort()));
    } else if (!ipStr.isEmpty() && !ipOk) {
        labelAddressStatus->setStyleSheet("font-size: 13px; color: #e11d48;");
        labelAddressStatus->setText(tr("Enter a valid public IPv4 or IPv6 address."));
    } else if (!portOk) {
        labelAddressStatus->setStyleSheet("font-size: 13px; color: #e11d48;");
        labelAddressStatus->setText(tr("Port must be between 1 and 65535."));
    } else if (!keyOk) {
        labelAddressStatus->setStyleSheet("font-size: 13px; color: #e11d48;");
        labelAddressStatus->setText(fDip3Active
            ? tr("Invalid 48-byte (96 hex chars) BLS operator public key.")
            : tr("Invalid masternodeprivkey format."));
    } else {
        labelAddressStatus->setStyleSheet("font-size: 13px; color: #16a34a;");
        labelAddressStatus->setText(fDip3Active
            ? (privKeyStr.isEmpty()
                ? tr("Ready: a new BLS12-381 operator keypair will be generated.")
                : tr("Ready: using provided BLS operator public key."))
            : (privKeyStr.isEmpty()
                ? tr("Ready: a new masternodeprivkey will be generated.")
                : tr("Ready: using provided masternodeprivkey.")));
    }
}

void MasterNodeWizardDialog::accept()
{
    switch(pos) {
        case 0:{
            ui->stackedWidget->setCurrentIndex(1);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            ui->pushName1->setChecked(true);
            icConfirm1->setVisible(true);
            ui->pushNumber3->setChecked(true);
            ui->btnBack->setVisible(true);
            ui->lineEditName->setFocus();
            pos++;
            updateStepButtons();
            updateNameValidation();
            break;
        }
        case 1:{
            const QString rawName = ui->lineEditName->text().trimmed();
            if (rawName.isEmpty() || rawName.contains(QRegExp("\\s"))) {
                setCssEditLineDialog(ui->lineEditName, false, true);
                updateNameValidation();
                inform(rawName.isEmpty() ? tr("Please enter a Masternode alias.") : tr("Alias cannot contain spaces."));
                return;
            }
            const std::string alias = rawName.toStdString();
            for (const CMasternodeConfig::CMasternodeEntry& existing : masternodeConfig.getEntries()) {
                if (existing.getAlias() == alias) {
                    setCssEditLineDialog(ui->lineEditName, false, true);
                    updateNameValidation();
                    inform(tr("Alias '%1' already exists in masternode.conf.").arg(rawName));
                    return;
                }
            }
            setCssEditLineDialog(ui->lineEditName, true, true);

            ui->stackedWidget->setCurrentIndex(2);
            ui->pushName4->setChecked(true);
            ui->pushName3->setChecked(true);
            ui->pushName1->setChecked(true);
            icConfirm3->setVisible(true);
            ui->pushNumber4->setChecked(true);
            ui->btnBack->setVisible(true);
            ui->lineEditIpAddress->setFocus();
            pos++;
            updateStepButtons();
            updateAddressValidation();
            break;
        }
        case 2:{
            const QString ipStr = ui->lineEditIpAddress->text().trimmed();
            const QString portStr = ui->lineEditPort->text().trimmed();
            const QString privKeyStr = lineEditPrivKey ? lineEditPrivKey->text().trimmed() : QString();
            bool portOk = false;
            const int portNum = portStr.toInt(&portOk);
            if (ipStr.isEmpty() || !MNModel::validateMNIP(ipStr)) {
                setCssEditLineDialog(ui->lineEditIpAddress, false, true);
                updateAddressValidation();
                inform(tr("Please enter a valid Masternode IP address."));
                return;
            }
            if (!portOk || portNum <= 0 || portNum > 65535) {
                setCssEditLineDialog(ui->lineEditPort, false, true);
                updateAddressValidation();
                inform(tr("Please enter a valid port (1-65535)."));
                return;
            }
            if (!privKeyStr.isEmpty() && !DecodeSecret(privKeyStr.toStdString()).IsValid()) {
                setCssEditLineDialog(lineEditPrivKey, false, true);
                updateAddressValidation();
                inform(tr("Invalid masternodeprivkey. Leave blank to auto-generate one."));
                return;
            }

            icConfirm4->setVisible(true);
            ui->btnBack->setVisible(true);
            isOk = createMN();
            QDialog::accept();
            break;
        }
    }
}

bool MasterNodeWizardDialog::createMN()
{
    if (!walletModel) {
        returnStr = tr("walletModel not set");
        return false;
    }

    // 1) Validate alias & IP:port BEFORE creating any keys or sending collateral!
    QString addressLabel = ui->lineEditName->text().trimmed();
    if (addressLabel.isEmpty()) {
        returnStr = tr("Masternode alias cannot be empty");
        return false;
    }
    std::string alias = addressLabel.toStdString();

    for (const CMasternodeConfig::CMasternodeEntry& existing : masternodeConfig.getEntries()) {
        if (existing.getAlias() == alias) {
            returnStr = tr("A Masternode with alias '%1' already exists in masternode.conf.").arg(addressLabel);
            return false;
        }
    }

    QString addressStr = ui->lineEditIpAddress->text().trimmed();
    QString portStr = ui->lineEditPort->text().trimmed();
    if (addressStr.isEmpty() || portStr.isEmpty()) {
        returnStr = tr("IP or port cannot be empty");
        return false;
    }
    if (!MNModel::validateMNIP(addressStr)) {
        returnStr = tr("Invalid IP address");
        return false;
    }

    // 2) Validate masternode.conf BEFORE sending any 4,200 KRISTA collateral tx!
    std::string confErr;
    if (!masternodeConfig.validateWritable(confErr)) {
        returnStr = QString::fromStdString(confErr);
        return false;
    }

    // ip + port
    std::string ipAddress = addressStr.toStdString();
    std::string port = portStr.toStdString();

    const bool fDip3Active = Params().GetConsensus().NetworkUpgradeActive(
        chainActive.Height() + 1, Consensus::UPGRADE_DIP3);

    // create or reuse the mn key (legacy ECDSA or KTDIP3 BLS)
    std::string mnKeyString;
    std::string blsPubHex;
    std::string blsSecHex;
    const QString customKeyStr = lineEditPrivKey ? lineEditPrivKey->text().trimmed() : QString();
    if (fDip3Active) {
        if (!customKeyStr.isEmpty()) {
            std::vector<unsigned char> vchOpPub = ParseHex(customKeyStr.toStdString());
            CBLSPubKey opPk;
            if (!IsHex(customKeyStr.toStdString()) || vchOpPub.size() != 48 ||
                !opPk.SetBuf(vchOpPub.data(), vchOpPub.size()) || !opPk.IsValid()) {
                returnStr = tr("Invalid 48-byte BLS operator public key.");
                return false;
            }
            blsPubHex = customKeyStr.toStdString();
        } else {
            CBLSSecretKey opSk;
            opSk.MakeNewKey();
            CBLSPubKey opPk(opSk);
            blsSecHex = HexStr(opSk.begin(), opSk.end());
            blsPubHex = HexStr(opPk.begin(), opPk.end());
        }
        // Keep a valid placeholder key in masternode.conf for local alias tracking
        CKey dummyLegacy;
        dummyLegacy.MakeNewKey(false);
        mnKeyString = EncodeSecret(dummyLegacy);
    } else if (!customKeyStr.isEmpty()) {
        CKey customSecret = DecodeSecret(customKeyStr.toStdString());
        if (!customSecret.IsValid()) {
            returnStr = tr("Invalid masternodeprivkey format.");
            return false;
        }
        mnKeyString = customKeyStr.toStdString();
    } else {
        CKey secret;
        secret.MakeNewKey(false);
        mnKeyString = EncodeSecret(secret);
    }

    // Look for an existing unlocked collateral UTXO first
    COutPoint collateralOut;

    // If not found, prepare and confirm a new 4,200 KRISTA self-collateral transaction
    if (!walletModel->getMNCollateralCandidate(collateralOut)) {
        Destination dest;
        PairResult r = walletModel->getNewAddress(dest, alias);

        if (!r.result) {
            inform(tr(r.status->c_str()));
            return false;
        }

        int nTipHeight = 0;
        {
            LOCK(cs_main);
            nTipHeight = chainActive.Height();
        }
        const CAmount mnCollateral = CMasternode::GetMasternodeNodeCollateral(nTipHeight);

        SendCoinsRecipient sendCoinsRecipient(
                QString::fromStdString(dest.ToString()),
                QString::fromStdString(alias),
                mnCollateral,
                "");

        QList<SendCoinsRecipient> recipients;
        recipients.append(sendCoinsRecipient);
        WalletModelTransaction currentTransaction(recipients);
        WalletModel::SendCoinsReturn prepareStatus = walletModel->prepareTransaction(currentTransaction, nullptr);

        if (prepareStatus.status == WalletModel::TransactionCreationFailed) {
            QString consolErr;
            if (GuiTransactionsUtils::OfferUtxoConsolidation(this, walletModel, mnCollateral, consolErr)) {
                prepareStatus = walletModel->prepareTransaction(currentTransaction, nullptr);
            } else if (!consolErr.isEmpty()) {
                returnStr = tr("Prepare Masternode collateral failed.\n\n%1\n").arg(consolErr);
                return false;
            }
        }

        CClientUIInterface::MessageBoxFlags informType;
        QString returnMsg = GuiTransactionsUtils::ProcessSendCoinsReturn(
                this,
                prepareStatus,
                walletModel,
                informType,
                BitcoinUnits::formatWithUnit(walletModel->getOptionsModel()->getDisplayUnit(),
                                             currentTransaction.getTransactionFee()),
                true
        );

        if (prepareStatus.status != WalletModel::OK) {
            returnStr = tr("Prepare Masternode collateral failed.\n\n%1\n").arg(returnMsg);
            return false;
        }

        // Explicit confirmation before broadcasting 4,200 KRISTA self-collateral transaction
        const int dispUnit = walletModel->getOptionsModel()->getDisplayUnit();
        DefaultDialog* confirmDlg = new DefaultDialog(this);
        confirmDlg->setText(
            tr("Confirm Masternode Collateral"),
            tr("No free %1 collateral UTXO was found.\n\n"
               "Send %1 to your new wallet address (%2) to lock as collateral for '%3'?\n"
               "Network Fee: %4")
                .arg(BitcoinUnits::formatWithUnit(dispUnit, mnCollateral, false, BitcoinUnits::separatorAlways))
                .arg(QString::fromStdString(dest.ToString()))
                .arg(addressLabel)
                .arg(BitcoinUnits::formatWithUnit(dispUnit, currentTransaction.getTransactionFee(), false, BitcoinUnits::separatorAlways)),
            tr("SEND COLLATERAL"),
            tr("CANCEL")
        );
        confirmDlg->adjustSize();
        const int dlgRes = confirmDlg->exec();
        confirmDlg->deleteLater();
        if (dlgRes != QDialog::Accepted) {
            returnStr = tr("Masternode collateral transaction cancelled by user.");
            return false;
        }

        WalletModel::SendCoinsReturn sendStatus = walletModel->sendCoins(currentTransaction);
        returnMsg = GuiTransactionsUtils::ProcessSendCoinsReturn(
                this,
                sendStatus,
                walletModel,
                informType
        );

        if (sendStatus.status != WalletModel::OK) {
            if (returnMsg.contains("Transaction too large", Qt::CaseInsensitive)) {
                returnMsg += "\n\n" + tr("Tip: Consolidate small mining UTXOs in Send first, then retry.");
            }
            returnStr = tr("Cannot send collateral transaction.\n\n%1").arg(returnMsg);
            return false;
        }

        // look for the tx index of the collateral
        CWalletTx* walletTx = currentTransaction.getTransaction();
        int indexOut = -1;
        for (int i = 0; i < (int)walletTx->vout.size(); i++) {
            CTxOut& out = walletTx->vout[i];
            if (out.nValue == mnCollateral) {
                indexOut = i;
                break;
            }
        }
        if (indexOut == -1) {
            returnStr = tr("Invalid collateral output index");
            return false;
        }
        collateralOut = COutPoint(walletTx->GetHash(), indexOut);
    }

    std::string txID = collateralOut.hash.ToString();
    std::string indexOutStr = std::to_string(collateralOut.n);

    // Check IP address type
    QHostAddress hostAddress(addressStr);
    QAbstractSocket::NetworkLayerProtocol layerProtocol = hostAddress.protocol();
    if (layerProtocol == QAbstractSocket::IPv6Protocol && (ipAddress.empty() || ipAddress.front() != '[')) {
        ipAddress = "[" + ipAddress + "]";
    }

    mnEntry = masternodeConfig.appendEntryAndSave(alias, ipAddress + ":" + port, mnKeyString, txID, indexOutStr, confErr);
    if (!mnEntry) {
        returnStr = QString::fromStdString(confErr);
        return false;
    }

    // Lock collateral output
    walletModel->lockCoin(collateralOut);

    if (fDip3Active) {
        Destination ownerDest;
        PairResult ownerRes = walletModel->getNewAddress(ownerDest, alias + "_owner");
        std::string ownerAddr = ownerRes.result ? ownerDest.ToString() : std::string("<ownerAddress>");
        createdActiveMnLine = blsSecHex.empty()
            ? QString::fromStdString("operatorPubKey=" + blsPubHex)
            : QString::fromStdString("masternodeblsprivkey=" + blsSecHex);
        createdMasternodeConfLine = QString::fromStdString(
            "protx register " + txID + " " + indexOutStr + " " + ipAddress + ":" + port +
            " " + ownerAddr + " " + blsPubHex + " " + ownerAddr + " 0 " + ownerAddr);
        returnStr = tr("KTDIP3 Masternode '%1' collateral & BLS operator key prepared! Wait %2 confirmations, add the BLS secret to kristatech.conf on your node, and run the protx register command below in Debug Console.")
                        .arg(addressLabel)
                        .arg(MASTERNODE_MIN_CONFIRMATIONS);
    } else {
        createdActiveMnLine = QString::fromStdString(alias + " " + mnKeyString);
        createdMasternodeConfLine = QString::fromStdString(alias + " " + ipAddress + ":" + port + " " + mnKeyString + " " + txID + " " + indexOutStr);
        returnStr = tr("Masternode '%1' created! Wait %2 confirmations before starting it, and paste the line below into activemasternode.conf on your remote node.")
                        .arg(addressLabel)
                        .arg(MASTERNODE_MIN_CONFIRMATIONS);
    }
    return true;
}

void MasterNodeWizardDialog::onBackClicked()
{
    if (pos == 0) return;
    pos--;
    updateStepButtons();
    switch(pos) {
        case 0:{
            ui->stackedWidget->setCurrentIndex(0);
            ui->btnNext->setFocus();
            ui->pushNumber1->setChecked(true);
            ui->pushNumber4->setChecked(false);
            ui->pushNumber3->setChecked(false);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(false);
            ui->pushName1->setChecked(true);
            icConfirm1->setVisible(false);
            ui->btnBack->setVisible(false);
            break;
        }
        case 1:{
            ui->stackedWidget->setCurrentIndex(1);
            ui->lineEditName->setFocus();
            ui->pushNumber4->setChecked(false);
            ui->pushNumber3->setChecked(true);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            icConfirm3->setVisible(false);

            break;
        }
    }
}

void MasterNodeWizardDialog::inform(QString text)
{
    if (!isVisible()) return;
    if (!snackBar)
        snackBar = new SnackBar(nullptr, this);
    snackBar->setText(text);
    snackBar->resize(this->width(), snackBar->height());
    openDialog(snackBar, this);
}

QSize BUTTON_SIZE = QSize(22, 22);
void MasterNodeWizardDialog::initBtn(std::initializer_list<QPushButton*> args)
{
    for (QPushButton* btn : args) {
        btn->setMinimumSize(BUTTON_SIZE);
        btn->setMaximumSize(BUTTON_SIZE);
        btn->move(0, 0);
        btn->show();
        btn->raise();
        btn->setVisible(false);
    }
}

MasterNodeWizardDialog::~MasterNodeWizardDialog()
{
    if (snackBar) delete snackBar;
    delete ui;
}
