// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/masternodewizarddialog.h"
#include "qt/kristatech/forms/ui_masternodewizarddialog.h"

#include "activemasternode.h"
#include "masternodeconfig.h"
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

    this->setStyleSheet(parent->styleSheet());
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
    // MN alias must not contain spaces or "#" character
    QRegularExpression rx("^(?:(?![\\#\\s]).)*");
    ui->lineEditName->setValidator(new QRegularExpressionValidator(rx, ui->lineEditName));

    // Frame 4
    setCssProperty(ui->labelTitle4, "text-title-dialog");
    setCssProperty({ui->labelSubtitleIp, ui->labelSubtitlePort}, "text-title");
    setCssSubtitleScreen(ui->labelSubtitleAddressIp);

    initCssEditLine(ui->lineEditIpAddress);
    initCssEditLine(ui->lineEditPort);
    ui->stackedWidget->setCurrentIndex(pos);
    ui->lineEditPort->setEnabled(false);    // use default port number
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
            break;
        }
        case 1:{

            // No empty names accepted.
            if (ui->lineEditName->text().isEmpty()) {
                setCssEditLine(ui->lineEditName, false, true);
                return;
            }
            setCssEditLine(ui->lineEditName, true, true);

            ui->stackedWidget->setCurrentIndex(2);
            ui->pushName4->setChecked(false);
            ui->pushName3->setChecked(true);
            ui->pushName1->setChecked(true);
            icConfirm3->setVisible(true);
            ui->pushNumber4->setChecked(true);
            ui->btnBack->setVisible(true);
            ui->lineEditIpAddress->setFocus();
            break;
        }
        case 2:{

            // No empty address accepted
            if (ui->lineEditIpAddress->text().isEmpty()) {
                return;
            }

            icConfirm4->setVisible(true);
            ui->btnBack->setVisible(true);
            ui->btnBack->setVisible(true);
            isOk = createMN();
            QDialog::accept();
        }
    }
    pos++;
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

    // 2) Validate and prepare masternode.conf BEFORE sending any 4,200 KRISTA collateral tx!
    const std::string defaultPortStr = std::to_string(Params().GetDefaultPort());
    const std::string defaultConfHeader =
        "# Masternode config file (Cold Controller Wallet)\n"
        "# Note: Remote Hot Masternodes use activemasternode.conf (<alias> <privkey>) for single-IP multi-MN;\n"
        "#       never place masternodeprivkey=... inside kristatech.conf on the remote server.\n"
        "# Format: alias IP:port masternodeprivkey collateral_output_txid collateral_output_index\n"
        "# Example: mn1 127.0.0.2:" + defaultPortStr + " 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 2bcd3c84c84f87eaa86e4e56834c92927a07f9e18718810b92e0d0324456a67c 0\n";

    fs::path pathMasternodeConfigFile = GetMasternodeConfigFile();
    if (!fs::exists(pathMasternodeConfigFile)) {
        FILE* initFile = fsbridge::fopen(pathMasternodeConfigFile, "w");
        if (!initFile) {
            returnStr = tr("Cannot create masternode.conf in data directory.");
            return false;
        }
        fwrite(defaultConfHeader.c_str(), defaultConfHeader.size(), 1, initFile);
        fclose(initFile);
    }

    fs::ifstream streamConfig(pathMasternodeConfigFile);
    if (!streamConfig.good()) {
        returnStr = tr("Invalid or unreadable masternode.conf file");
        return false;
    }

    int linenumber = 1;
    std::string lineCopy = "";
    for (std::string line; std::getline(streamConfig, line); linenumber++) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string comment, existingAlias, ip, privKey, txHash, outputIndex;

        if (iss >> comment) {
            if (comment.at(0) == '#') continue;
            iss.str(line);
            iss.clear();
        }

        if (!(iss >> existingAlias >> ip >> privKey >> txHash >> outputIndex)) {
            iss.str(line);
            iss.clear();
            if (!(iss >> existingAlias >> ip >> privKey >> txHash >> outputIndex)) {
                streamConfig.close();
                returnStr = tr("Error parsing masternode.conf at line %1").arg(linenumber);
                return false;
            }
        }
        lineCopy += line + "\n";
    }
    streamConfig.close();

    if (lineCopy.empty()) {
        lineCopy = defaultConfHeader;
    }
    lineCopy += "\n";

    // ip + port
    std::string ipAddress = addressStr.toStdString();
    std::string port = portStr.toStdString();

    // create the mn key
    CKey secret;
    secret.MakeNewKey(false);
    std::string mnKeyString = EncodeSecret(secret);

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

    fs::path pathConfigFile = AbsPathForConfigVal(fs::path("masternode_temp.conf"));
    FILE* configFile = fsbridge::fopen(pathConfigFile, "w");
    if (!configFile) {
        returnStr = tr("Failed to write temporary masternode_temp.conf file.");
        return false;
    }
    lineCopy += alias + " " + ipAddress + ":" + port + " " + mnKeyString + " " + txID + " " + indexOutStr + "\n";
    fwrite(lineCopy.c_str(), lineCopy.size(), 1, configFile);
    fclose(configFile);

    fs::path pathOldConfFile = AbsPathForConfigVal(fs::path("old_masternode.conf"));
    if (fs::exists(pathOldConfFile)) {
        fs::remove(pathOldConfFile);
    }
    rename(pathMasternodeConfigFile, pathOldConfFile);

    fs::path pathNewConfFile = AbsPathForConfigVal(fs::path("masternode.conf"));
    rename(pathConfigFile, pathNewConfFile);

    mnEntry = masternodeConfig.add(alias, ipAddress + ":" + port, mnKeyString, txID, indexOutStr);

    // Lock collateral output
    walletModel->lockCoin(collateralOut);

    returnStr = tr("Masternode created! Wait %1 confirmations before starting it, and paste '<alias> <privkey>' into activemasternode.conf on your remote node.").arg(MASTERNODE_MIN_CONFIRMATIONS);
    return true;
}

void MasterNodeWizardDialog::onBackClicked()
{
    if (pos == 0) return;
    pos--;
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
