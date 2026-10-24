// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/smartcontractwidget.h"
#include "qt/kristatech/forms/ui_smartcontractwidget.h"
#include "qt/kristatech/kristatechgui.h"
#include "qt/kristatech/qtutils.h"
#include "guiutil.h"
#include "walletmodel.h"
#include "wallet/wallet.h"
#include "script/mescal.h"
#include "script/standard.h"
#include "net.h"
#include "main.h"       // cs_main, COIN
#include "coincontrol.h"
#include "utilstrencodings.h"
#include "primitives/transaction.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTextStream>
#include <QFile>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string UniValueToYAML(const UniValue& val, int indent = 0) {
    std::string result;
    std::string spaces(indent, ' ');
    if (val.isObject()) {
        const std::vector<std::string>& keys = val.getKeys();
        for (const std::string& key : keys) {
            const UniValue& subVal = val[key];
            if (subVal.isObject()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else if (subVal.isArray()) {
                result += spaces + key + ":\n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + key + ": " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isArray()) {
        for (unsigned int i = 0; i < val.size(); ++i) {
            const UniValue& subVal = val[i];
            if (subVal.isObject() || subVal.isArray()) {
                result += spaces + "- \n" + UniValueToYAML(subVal, indent + 2);
            } else {
                result += spaces + "- " + UniValueToYAML(subVal, 0);
            }
        }
    } else if (val.isNum()) {
        result += val.getValStr() + "\n";
    } else if (val.isBool()) {
        result += std::string(val.get_bool() ? "true" : "false") + "\n";
    } else {
        result += "\"" + val.get_str() + "\"\n";
    }
    return result;
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

SmartContractWidget::SmartContractWidget(KRISTATECHGUI* parent) :
    PWidget(parent),
    ui(new Ui::SmartContractWidget)
{
    ui->setupUi(this);

    this->setStyleSheet(parent->styleSheet());

    // ── Build tab ─────────────────────────────────────────────────────────────
    setCssProperty(ui->left, "container");
    ui->left->setContentsMargins(20, 20, 20, 20);
    setCssProperty(ui->right, "container-right");
    ui->right->setContentsMargins(20, 20, 20, 20);

    setCssProperty(ui->labelTitle, "text-title-screen");
    setCssProperty(ui->labelSubtitle1, "text-subtitle");
    setCssProperty(ui->labelTitleRight, "text-title-screen");
    setCssProperty(ui->lblYAML, "text-title");
    setCssProperty(ui->lblJSON, "text-title");

    setCssProperty({ui->lblTLName, ui->lblTLExpiry, ui->lblTLPubkey,
                    ui->lblMSName, ui->lblMSN, ui->lblMSM, ui->lblMSKeys,
                    ui->lblHLName, ui->lblHLHash, ui->lblHLPubkey,
                    ui->labelAmount, ui->lblSelectTemplate}, "text-title");

    for (QLineEdit* le : {ui->lineEditNameTimeLock, ui->lineEditExpiryTimeLock, ui->lineEditPubkeyTimeLock,
                           ui->lineEditNameMultiSig, ui->lineEditNMultiSig, ui->lineEditMMultiSig,
                           ui->lineEditNameHashLock, ui->lineEditHashHashLock, ui->lineEditPubkeyHashLock,
                           ui->lineEditAmount}) {
        setCssEditLine(le, true);
    }

    setCssBtnSecondary(ui->btnSave);
    setCssBtnPrimary(ui->btnPublish);

    initComboBox(ui->comboContracts);
    initComboBox(ui->comboTemplates);

    ui->comboTemplates->addItem(tr("Custom / Blank"));
    ui->comboTemplates->addItem(tr("Time-Locked Deposit"));
    ui->comboTemplates->addItem(tr("Escrow Multi-Signature (2-of-3)"));
    ui->comboTemplates->addItem(tr("Hash-Locked Claim"));
    ui->comboTemplates->addItem(tr("Dead Man's Switch (Inheritance)"));
    ui->comboTemplates->addItem(tr("Dual-Signature Escrow with Mediator"));
    ui->comboTemplates->addItem(tr("2-Factor Authentication (2FA) Wallet"));
    ui->comboTemplates->addItem(tr("Hash Time-Locked Swap (HTLC)"));
    ui->comboTemplates->addItem(tr("Multi-Path Security Recovery"));
    ui->comboTemplates->addItem(tr("Tokenized Asset (Escrow & Compliance)"));
    ui->comboTemplates->addItem(tr("Miner Registration (Coin-Lock)"));
    ui->comboTemplates->addItem(tr("Miner Registration (PoW-Lock)"));
    ui->comboTemplates->addItem(tr("Time-Delayed Vault (Anti-Theft)"));
    ui->comboTemplates->addItem(tr("Social Recovery Wallet"));
    ui->comboTemplates->addItem(tr("Milestone Vesting Escrow"));
    ui->comboTemplates->addItem(tr("Collateralized Loan Liquidation"));
    ui->comboTemplates->addItem(tr("Double-Deposit P2P Escrow"));
    ui->comboTemplates->addItem(tr("Pay-on-Delivery Supply Chain"));
    ui->comboTemplates->addItem(tr("Subscription Pull Authorization"));
    ui->comboTemplates->addItem(tr("Real Estate Title Transfer Escrow"));
    ui->comboTemplates->addItem(tr("Rental Deposit Trust"));
    ui->comboTemplates->addItem(tr("Corporate Governance (3-of-5)"));
    ui->comboTemplates->addItem(tr("Weighted Board Voting"));
    ui->comboTemplates->addItem(tr("DAO Ragequit Covenant"));
    ui->comboTemplates->addItem(tr("Dual-Custodian Audit Lock"));
    ui->comboTemplates->addItem(tr("P2P Prediction Market Escrow"));
    ui->comboTemplates->addItem(tr("Gaming Tournament Prize Pool"));
    ui->comboTemplates->addItem(tr("IoT EV Charging Pay-per-Use"));
    ui->comboTemplates->addItem(tr("Parametric Insurance Claim"));

    connect(ui->comboTemplates, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SmartContractWidget::onTemplateSelected);

    // Live preview connections
    connect(ui->lineEditNameTimeLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditExpiryTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyTimeLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNameMultiSig,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditMMultiSig,      &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->plainTextEditKeysMultiSig, &QPlainTextEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditNameHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditHashHashLock,   &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->lineEditPubkeyHashLock, &QLineEdit::textChanged, this, &SmartContractWidget::generateContract);
    connect(ui->tabWidget, &QTabWidget::currentChanged, this, &SmartContractWidget::generateContract);

    connect(ui->btnSave,    &QPushButton::clicked, this, &SmartContractWidget::onSaveClicked);
    connect(ui->btnPublish, &QPushButton::clicked, this, &SmartContractWidget::onPublishClicked);

    // ── Run tab ───────────────────────────────────────────────────────────────
    setCssProperty(ui->runLeft, "container");
    ui->runLeft->setContentsMargins(20, 20, 20, 20);
    setCssProperty(ui->runRight, "container-right");
    ui->runRight->setContentsMargins(20, 20, 20, 20);

    setCssProperty(ui->labelRunTitle,    "text-title-screen");
    setCssProperty(ui->labelRunSubtitle, "text-subtitle");
    setCssProperty(ui->lblSelectContract,"text-title");
    setCssProperty(ui->lblRecipAddr,     "text-title");
    setCssProperty(ui->lblUnlockParams,  "text-title-screen");
    setCssProperty({ui->lblParam1, ui->lblParam2, ui->lblParam3}, "text-title");
    setCssProperty(ui->labelRunRightTitle, "text-title-screen");
    setCssProperty(ui->lblDecompiledTitle, "text-title");
    setCssProperty(ui->lblRawScript,       "text-title");

    // UTXO info labels
    setCssProperty({ui->lblUtxoTxId, ui->lblUtxoVout,
                    ui->lblUtxoAmount, ui->lblUtxoConfs}, "text-title");

    setCssEditLine(ui->lineEditRecipAddr, true);
    setCssEditLine(ui->lineEditParam1,    true);
    setCssEditLine(ui->lineEditParam2,    true);
    setCssEditLine(ui->lineEditRawScript, true);

    setCssBtnSecondary(ui->btnRefreshContracts);
    setCssBtnPrimary(ui->btnRunContract);

    connect(ui->btnRefreshContracts, &QPushButton::clicked, this, &SmartContractWidget::onRefreshContractsClicked);
    connect(ui->comboContracts, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SmartContractWidget::onContractSelected);
    connect(ui->btnRunContract, &QPushButton::clicked, this, &SmartContractWidget::onRunContractClicked);
    connect(ui->tabWidgetMode, &QTabWidget::currentChanged, this, [this](int index){
        if (index == 1) {
            onRefreshContractsClicked();
        }
    });

    // ── Custom tab styling & connections ──────────────────────────────────────
    setCssBtnSecondary(ui->btnAddAction);
    setCssBtnSecondary(ui->btnRemoveAction);
    setCssBtnSecondary(ui->btnMoveActionUp);
    setCssBtnSecondary(ui->btnMoveActionDown);
    setCssBtnPrimary(ui->btnOpenVisualDesigner);

    if (ui->treeCustomActions->header()) {
        ui->treeCustomActions->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        ui->treeCustomActions->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    }
    ui->treeCustomActions->setToolTip(tr("Double-click any step to edit its parameters."));

    connect(ui->btnAddAction, &QPushButton::clicked, this, &SmartContractWidget::onAddActionClicked);
    connect(ui->treeCustomActions, &QTreeWidget::itemDoubleClicked, this, &SmartContractWidget::onEditAction);
    connect(ui->btnRemoveAction, &QPushButton::clicked, this, &SmartContractWidget::onRemoveActionClicked);
    connect(ui->btnMoveActionUp, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionUp);
    connect(ui->btnMoveActionDown, &QPushButton::clicked, this, &SmartContractWidget::onMoveActionDown);
    connect(ui->btnOpenVisualDesigner, &QPushButton::clicked, this, &SmartContractWidget::onOpenVisualDesignerClicked);
    connect(ui->cbDeveloperMode, &QCheckBox::toggled, this, &SmartContractWidget::onDeveloperModeToggled);
    connect(ui->textEditYAML, &QTextEdit::textChanged, this, &SmartContractWidget::onYAMLTextChanged);

    customActions = UniValue(UniValue::VARR);

    // Initial state
    generateContract();
    clearRunDetails();
}

SmartContractWidget::~SmartContractWidget()
{
    delete ui;
}

// ─── loadWalletModel ──────────────────────────────────────────────────────────

void SmartContractWidget::loadWalletModel()
{
    if (walletModel) {
        CPubKey pubKey;
        if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
            std::string pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
            ui->lineEditPubkeyTimeLock->setText(QString::fromStdString(pubkeyHex));
            ui->lineEditPubkeyHashLock->setText(QString::fromStdString(pubkeyHex));
            if (ui->lineEditRecipAddr->text().trimmed().isEmpty()) {
                ui->lineEditRecipAddr->setText(QString::fromStdString(EncodeDestination(pubKey.GetID())));
            }
        }
        {
            LOCK(cs_main);
            if (chainActive.Height() > 0 && ui->lineEditExpiryTimeLock->text() == "150") {
                ui->lineEditExpiryTimeLock->setText(QString::number(chainActive.Height() + 100));
            }
        }
        generateContract();

        // Automatically refresh when wallet balance/transactions change
        connect(walletModel, &WalletModel::balanceChanged, this, &SmartContractWidget::onRefreshContractsClicked);
        onRefreshContractsClicked();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  BUILD TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::generateContract()
{
    int tabIndex = ui->tabWidget->currentIndex();
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);

    if (tabIndex == 0) { // Time-Locked
        std::string name     = ui->lineEditNameTimeLock->text().toStdString();
        std::string expiryStr= ui->lineEditExpiryTimeLock->text().toStdString();
        std::string pubkey   = ui->lineEditPubkeyTimeLock->text().toStdString();

        activeName = name;

        UniValue expiryVal;
        try { expiryVal = UniValue((int64_t)std::stoll(expiryStr)); }
        catch (...) { expiryVal = UniValue(expiryStr); }

        UniValue tlInputs(UniValue::VARR);
        UniValue tlInput1(UniValue::VOBJ);
        tlInput1.pushKV("name",  "Lock-Until");
        tlInput1.pushKV("type",  "timestamp-or-block-height");
        tlInput1.pushKV("value", expiryVal);
        tlInputs.push_back(tlInput1);

        UniValue tlSpec(UniValue::VOBJ);
        tlSpec.pushKV("role",   "lock-time");
        tlSpec.pushKV("inputs", tlInputs);
        basic.pushKV("Lock-Time", tlSpec);

        UniValue osInputs(UniValue::VARR);
        UniValue osInput1(UniValue::VOBJ);
        osInput1.pushKV("name",  "Pubkey");
        osInput1.pushKV("type",  "pubkey");
        osInput1.pushKV("value", pubkey);
        osInputs.push_back(osInput1);

        UniValue osSpec(UniValue::VOBJ);
        osSpec.pushKV("role",   "check-signature-verification");
        osSpec.pushKV("inputs", osInputs);
        basic.pushKV("Owner-Sig", osSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Funds locked requiring owner signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Lock-Time");  actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Owner-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 1) { // Multi-Signature
        std::string name = ui->lineEditNameMultiSig->text().toStdString();
        int n = ui->lineEditNMultiSig->text().toInt();
        int m = ui->lineEditMMultiSig->text().toInt();
        QStringList keysList = ui->plainTextEditKeysMultiSig->toPlainText()
                                   .split('\n', QString::SkipEmptyParts);

        activeName = name;

        UniValue msInputs(UniValue::VARR);
        UniValue msInput1(UniValue::VOBJ); msInput1.pushKV("name","m"); msInput1.pushKV("type","number"); msInput1.pushKV("value",m); msInputs.push_back(msInput1);
        UniValue msInput2(UniValue::VOBJ); msInput2.pushKV("name","n"); msInput2.pushKV("type","number"); msInput2.pushKV("value",n); msInputs.push_back(msInput2);

        UniValue keysArray(UniValue::VARR);
        for (const QString& k : keysList)
            keysArray.push_back(k.trimmed().toStdString());

        UniValue msInput3(UniValue::VOBJ); msInput3.pushKV("name","Signatures"); msInput3.pushKV("type","array"); msInput3.pushKV("value",keysArray); msInputs.push_back(msInput3);

        UniValue msSpec(UniValue::VOBJ);
        msSpec.pushKV("role",   "multi-signature");
        msSpec.pushKV("inputs", msInputs);
        basic.pushKV("Multi-Signature", msSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", std::to_string(m)+"-of-"+std::to_string(n)+" multi-signature smart contract.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Multi-Signature"); actions.push_back(action1);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);

    } else if (tabIndex == 2) { // Hash-Locked
        std::string name    = ui->lineEditNameHashLock->text().toStdString();
        std::string hash160 = ui->lineEditHashHashLock->text().toStdString();
        std::string pubkey  = ui->lineEditPubkeyHashLock->text().toStdString();

        activeName = name;

        UniValue hcInputs(UniValue::VARR);
        UniValue hcInput1(UniValue::VOBJ); hcInput1.pushKV("name","Hash160"); hcInput1.pushKV("type","string-or-number"); hcInput1.pushKV("value",hash160); hcInputs.push_back(hcInput1);

        UniValue hcSpec(UniValue::VOBJ);
        hcSpec.pushKV("role",   "hash160");
        hcSpec.pushKV("inputs", hcInputs);
        basic.pushKV("Hash-Check", hcSpec);

        UniValue rsInputs(UniValue::VARR);
        UniValue rsInput1(UniValue::VOBJ); rsInput1.pushKV("name","Pubkey"); rsInput1.pushKV("type","pubkey"); rsInput1.pushKV("value",pubkey); rsInputs.push_back(rsInput1);

        UniValue rsSpec(UniValue::VOBJ);
        rsSpec.pushKV("role",   "check-signature-verification");
        rsSpec.pushKV("inputs", rsInputs);
        basic.pushKV("Recipient-Sig", rsSpec);

        UniValue cSpec(UniValue::VOBJ);
        cSpec.pushKV("description", "Requires revealing preimage and recipient signature.");
        UniValue actions(UniValue::VARR);
        UniValue action1(UniValue::VOBJ); action1.pushKV("type","basic"); action1.pushKV("name","Hash-Check");     actions.push_back(action1);
        UniValue action2(UniValue::VOBJ); action2.pushKV("type","basic"); action2.pushKV("name","Recipient-Sig");  actions.push_back(action2);
        cSpec.pushKV("actions", actions);
        contract.pushKV(name, cSpec);

        doc.pushKV("basic",           basic);
        doc.pushKV("contract",        contract);
        doc.pushKV("active_contract", name);
    } else if (tabIndex == 3) { // Custom Builder
        buildContractFromCustom();
        return;
    }

    activeDoc = doc;
    updatePreviews();
}

void SmartContractWidget::updatePreviews()
{
    ui->textEditJSON->setText(QString::fromStdString(activeDoc.write(2)));
    {
        QSignalBlocker blocker(ui->textEditYAML);
        ui->textEditYAML->setText(QString::fromStdString(UniValueToYAML(activeDoc, 0)));
    }

    std::string compileErr;
    CScript compiled = CMescal::Compile(activeDoc.write(), compileErr);
    if (!compileErr.empty()) {
        ui->labelStatus->setStyleSheet("color: #ff3d00;");
        ui->labelStatus->setText(tr("✗ Status: Compilation Error — %1").arg(QString::fromStdString(compileErr)));
    } else {
        ui->labelStatus->setStyleSheet("color: #26a69a;");
        ui->labelStatus->setText(tr("✓ Status: Valid MESCAL Contract (%1 bytes)").arg(compiled.size()));
    }
}

void SmartContractWidget::onSaveClicked()
{
    if (activeName.empty()) {
        QMessageBox::critical(this, tr("Error"), tr("Contract name is empty."));
        return;
    }
    QString defaultFileName = QString::fromStdString(activeName) + ".json";
    QString path = QFileDialog::getSaveFileName(this, tr("Save Smart Contract"), defaultFileName, tr("JSON Files (*.json)"));
    if (path.isEmpty()) return;

    QFile jsonFile(path);
    if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&jsonFile);
        out << QString::fromStdString(activeDoc.write(2));
        jsonFile.close();
    }

    QString yamlPath = path;
    if (yamlPath.endsWith(".json")) { yamlPath.chop(5); yamlPath.append(".yaml"); }
    else { yamlPath.append(".yaml"); }

    QFile yamlFile(yamlPath);
    if (yamlFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&yamlFile);
        out << QString::fromStdString(UniValueToYAML(activeDoc, 0));
        yamlFile.close();
    }

    inform(tr("Contract spec saved to:\n%1\n%2").arg(path).arg(yamlPath));
}

void SmartContractWidget::onPublishClicked()
{
    if (!walletModel) {
        inform(tr("Wallet model not loaded."));
        return;
    }

    // 1. Compile
    std::string jsonStr = activeDoc.write();
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    if (!errorStr.empty()) {
        QMessageBox::critical(this, tr("Compilation Error"),
            tr("MESCAL compilation failed:\n%1").arg(QString::fromStdString(errorStr)));
        return;
    }

    // 2. Validate Amount
    bool amountOk = false;
    double amountDouble = ui->lineEditAmount->text().toDouble(&amountOk);
    if (!amountOk || amountDouble < 0) {
        QMessageBox::critical(this, tr("Invalid Amount"), tr("Please enter a valid amount of KRISTA."));
        return;
    }
    CAmount amount = amountDouble * COIN;

    // 3. Unlock Wallet
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        inform(tr("Wallet unlock failed"));
        return;
    }

    // 4. Send transaction
    if (!pwalletMain) {
        inform(tr("Wallet database not initialized."));
        return;
    }

    std::vector<CRecipient> vecSend;
    vecSend.push_back(CRecipient{script, amount, false});

    CWalletTx wtx;
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosInOut = -1;
    std::string strFailReason;
    CCoinControl coinControl;

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        bool fCreated = pwalletMain->CreateTransaction(vecSend,
                                                      wtx,
                                                      reservekey,
                                                      nFeeRequired,
                                                      nChangePosInOut,
                                                      strFailReason,
                                                      &coinControl,
                                                      ALL_COINS,
                                                      true,
                                                      0);
        if (!fCreated) {
            inform(tr("Transaction creation failed: %1").arg(QString::fromStdString(strFailReason)));
            return;
        }
    }

    CWallet::CommitResult res = pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get());
    if (res.status != CWallet::CommitStatus::OK) {
        QMessageBox::critical(this, tr("Commit Failed"),
            tr("Transaction commit failed:\n%1").arg(QString::fromStdString(res.ToString())));
        return;
    }

    // 5. Notify UI
    walletModel->emitBalanceChanged();

    std::string txid = res.hashTx.GetHex();
    ui->labelStatus->setText(tr("Status: Published! TxID: %1").arg(QString::fromStdString(txid)));
    inform(tr("Contract published successfully!\nTxID: %1").arg(QString::fromStdString(txid)));
}

void SmartContractWidget::onTemplateSelected(int index)
{
    if (index == 0) {
        // Custom / Blank
        return;
    }

    if (index == 1) { // Time-Locked Deposit
        ui->tabWidget->setCurrentIndex(0);
        ui->lineEditNameTimeLock->setText("TimeLockedDeposit");
        ui->lineEditExpiryTimeLock->setText("1780718400");
        generateContract();
    }
    else if (index == 2) { // Escrow Multi-Signature (2-of-3)
        ui->tabWidget->setCurrentIndex(1);
        ui->lineEditNameMultiSig->setText("Escrow2of3");
        ui->lineEditNMultiSig->setText("3");
        ui->lineEditMMultiSig->setText("2");
        QStringList sampleKeys;
        sampleKeys << "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"
                   << "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"
                   << "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234";
        ui->plainTextEditKeysMultiSig->setPlainText(sampleKeys.join("\n"));
        generateContract();
    }
    else if (index == 3) { // Hash-Locked Claim
        ui->tabWidget->setCurrentIndex(2);
        ui->lineEditNameHashLock->setText("HashLockedClaim");
        ui->lineEditHashHashLock->setText("b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
        generateContract();
    }
    else {
        // Custom Builder advanced templates
        ui->tabWidget->setCurrentIndex(3);
        
        customActions.clear();
        customActions.setArray();
        
        if (index == 4) { // Dead Man's Switch (Inheritance)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Heir
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", (int64_t)1780718400); // Expiry
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Owner
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 5) { // Dual-Signature Escrow with Mediator (2-of-3)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Buyer
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Seller
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Mediator
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 6) { // 2-Factor Authentication (2FA) Wallet
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Primary
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Backup
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 7) { // Hash Time-Locked Swap (HTLC)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Hash160");
            exprInp.pushKV("type", "string-or-number");
            exprInp.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Recipient
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Sender
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);

            // HTLC also has a lock time component for timeout. In customActions, we can append a step for Timeout Check!
            UniValue timeNode(UniValue::VOBJ);
            timeNode.pushKV("role", "lock-time");
            UniValue timeInputs(UniValue::VARR);
            UniValue timeInp(UniValue::VOBJ);
            timeInp.pushKV("name", "Lock-Until");
            timeInp.pushKV("type", "timestamp-or-block-height");
            timeInp.pushKV("value", (int64_t)1780718400); // Expiry
            timeInputs.push_back(timeInp);
            timeNode.pushKV("inputs", timeInputs);
            
            customActions.push_back(timeNode);
        }
        else if (index == 8) { // Multi-Path Security Recovery
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Recovery Key 1
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Recovery Key 2
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235"); // Recovery Key 3
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", (int64_t)1780718400); // Delay
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Owner
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 9) { // Tokenized Asset (Escrow & Compliance)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            // Expression: Seller Signature verification (Owner/Seller key)
            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Seller
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            // True Action: lock-time (Dispute Timeout)
            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "lock-time");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Lock-Until");
            trueInp.pushKV("type", "timestamp-or-block-height");
            trueInp.pushKV("value", (int64_t)1780718400); // Expiry
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            // False Action: 2-of-3 multi-signature (Seller, Buyer, Mediator)
            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "multi-signature");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseM(UniValue::VOBJ); falseM.pushKV("name", "m"); falseM.pushKV("type", "number"); falseM.pushKV("value", 2); falseInputs.push_back(falseM);
            UniValue falseN(UniValue::VOBJ); falseN.pushKV("name", "n"); falseN.pushKV("type", "number"); falseN.pushKV("value", 3); falseInputs.push_back(falseN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f"); // Seller
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a"); // Buyer
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234"); // Mediator
            UniValue falseSigs(UniValue::VOBJ); falseSigs.pushKV("name", "Signatures"); falseSigs.pushKV("type", "array"); falseSigs.pushKV("value", keysArray); falseInputs.push_back(falseSigs);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 10) { // Miner Registration (Coin-Lock)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "coin-lock-miner");
            UniValue inputs(UniValue::VARR);

            CPubKey pubKey;
            std::string pubkeyHex = "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f";
            std::string pubkeyHashHex = "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba";
            if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
                pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
                pubkeyHashHex = HexStr(pubKey.GetID());
            }

            UniValue inpPk(UniValue::VOBJ);
            inpPk.pushKV("name", "Pubkey");
            inpPk.pushKV("type", "pubkey");
            inpPk.pushKV("value", pubkeyHex);
            inputs.push_back(inpPk);

            UniValue inpLt(UniValue::VOBJ);
            inpLt.pushKV("name", "Lock-Until");
            inpLt.pushKV("type", "timestamp-or-block-height");
            inpLt.pushKV("value", (int64_t)1780718400); // Expiry
            inputs.push_back(inpLt);

            UniValue inpPkh(UniValue::VOBJ);
            inpPkh.pushKV("name", "PubkeyHash");
            inpPkh.pushKV("type", "pubkeyhash");
            inpPkh.pushKV("value", pubkeyHashHex);
            inputs.push_back(inpPkh);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 11) { // Miner Registration (PoW-Lock)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "pow-miner");
            UniValue inputs(UniValue::VARR);

            CPubKey pubKey;
            std::string pubkeyHex = "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f";
            std::string pubkeyHashHex = "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba";
            if (pwalletMain && pwalletMain->GetKeyFromPool(pubKey)) {
                pubkeyHex = HexStr(pubKey.begin(), pubKey.end());
                pubkeyHashHex = HexStr(pubKey.GetID());
            }

            std::string tipHashHex = "0000000000000000000000000000000000000000000000000000000000000000";
            if (chainActive.Tip()) {
                tipHashHex = chainActive.Tip()->GetBlockHash().GetHex();
            }

            UniValue inpNonce(UniValue::VOBJ);
            inpNonce.pushKV("name", "Nonce");
            inpNonce.pushKV("type", "nonce");
            inpNonce.pushKV("value", "0000000000000000"); // 8 bytes hex placeholder
            inputs.push_back(inpNonce);

            UniValue inpChallenge(UniValue::VOBJ);
            inpChallenge.pushKV("name", "Challenge");
            inpChallenge.pushKV("type", "challenge");
            inpChallenge.pushKV("value", tipHashHex);
            inputs.push_back(inpChallenge);

            UniValue inpPk(UniValue::VOBJ);
            inpPk.pushKV("name", "Pubkey");
            inpPk.pushKV("type", "pubkey");
            inpPk.pushKV("value", pubkeyHex);
            inputs.push_back(inpPk);

            UniValue inpLt(UniValue::VOBJ);
            inpLt.pushKV("name", "Lock-Until");
            inpLt.pushKV("type", "timestamp-or-block-height");
            inpLt.pushKV("value", (int64_t)1780718400); // Expiry
            inputs.push_back(inpLt);

            UniValue inpPkh(UniValue::VOBJ);
            inpPkh.pushKV("name", "PubkeyHash");
            inpPkh.pushKV("type", "pubkeyhash");
            inpPkh.pushKV("value", pubkeyHashHex);
            inputs.push_back(inpPkh);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 12) { // Time-Delayed Vault (Anti-Theft)
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "03bb9f1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234a1");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);
            
            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1780977600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 13) { // Social Recovery Wallet
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "multi-signature");
            UniValue falseInputs1(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); falseInputs1.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); falseInputs1.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs1.push_back(inpSigs);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "lock-time");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Lock-Until");
            falseInp2.pushKV("type", "timestamp-or-block-height");
            falseInp2.pushKV("value", (int64_t)1783310400);
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 14) { // Milestone Vesting Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "lock-time");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Lock-Until");
            exprInp.pushKV("type", "timestamp-or-block-height");
            exprInp.pushKV("value", (int64_t)1780718400);
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "multi-signature");
            UniValue trueInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); trueInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); trueInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); trueInputs.push_back(inpSigs);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 15) { // Collateralized Loan Liquidation
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1788489600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 16) { // Double-Deposit P2P Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 2); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1782273600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 17) { // Pay-on-Delivery Supply Chain
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Hash160");
            exprInp.pushKV("type", "string-or-number");
            exprInp.pushKV("value", "a5c9f285d893ce71ab9de8f5c09d765ee982ba34");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1782014400);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 18) { // Subscription Pull Authorization
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VARR);

            UniValue expr1(UniValue::VOBJ);
            expr1.pushKV("role", "check-signature-verification");
            UniValue exprInputs1(UniValue::VARR);
            UniValue exprInp1(UniValue::VOBJ);
            exprInp1.pushKV("name", "Pubkey");
            exprInp1.pushKV("type", "pubkey");
            exprInp1.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            exprInputs1.push_back(exprInp1);
            expr1.pushKV("inputs", exprInputs1);
            expr.push_back(expr1);

            UniValue expr2(UniValue::VOBJ);
            expr2.pushKV("role", "lock-time");
            UniValue exprInputs2(UniValue::VARR);
            UniValue exprInp2(UniValue::VOBJ);
            exprInp2.pushKV("name", "Lock-Until");
            exprInp2.pushKV("type", "timestamp-or-block-height");
            exprInp2.pushKV("value", (int64_t)1783310400);
            exprInputs2.push_back(exprInp2);
            expr2.pushKV("inputs", exprInputs2);
            expr.push_back(expr2);

            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "check-signature-verification");
            UniValue falseInputs(UniValue::VARR);
            UniValue falseInp(UniValue::VOBJ);
            falseInp.pushKV("name", "Pubkey");
            falseInp.pushKV("type", "pubkey");
            falseInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            falseInputs.push_back(falseInp);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            customActions.push_back(condNode);
        }
        else if (index == 19) { // Real Estate Title Transfer Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); exprInputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); exprInputs.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); exprInputs.push_back(inpSigs);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1782273600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 20) { // Rental Deposit Trust
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 21) { // Corporate Governance (3-of-5)
            UniValue act(UniValue::VOBJ);
            act.pushKV("role", "multi-signature");
            UniValue inputs(UniValue::VARR);

            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); inputs.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); inputs.push_back(inpN);

            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);

            act.pushKV("inputs", inputs);
            customActions.push_back(act);
        }
        else if (index == 22) { // Weighted Board Voting
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VOBJ);
            falseAct.pushKV("role", "multi-signature");
            UniValue falseInputs(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 3); falseInputs.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 3); falseInputs.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray2.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs.push_back(inpSigs2);
            falseAct.pushKV("inputs", falseInputs);
            condNode.pushKV("false_action", falseAct);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 23) { // DAO Ragequit Covenant
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "check-signature-verification");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Pubkey");
            exprInp.pushKV("type", "pubkey");
            exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1780977600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 3); falseInputs2.push_back(inpM);
            UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 5); falseInputs2.push_back(inpN);
            UniValue keysArray(UniValue::VARR);
            keysArray.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            keysArray.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1239");
            UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs2.push_back(inpSigs);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 24) { // Dual-Custodian Audit Lock
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1793310400);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 2); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 2); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 25) { // P2P Prediction Market Escrow
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 3); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            keysArray1.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1781102400);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 2); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 2); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray2.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 26) { // Gaming Tournament Prize Pool
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "multi-signature");
            UniValue exprInputs(UniValue::VARR);
            UniValue inpM1(UniValue::VOBJ); inpM1.pushKV("name", "m"); inpM1.pushKV("type", "number"); inpM1.pushKV("value", 2); exprInputs.push_back(inpM1);
            UniValue inpN1(UniValue::VOBJ); inpN1.pushKV("name", "n"); inpN1.pushKV("type", "number"); inpN1.pushKV("value", 2); exprInputs.push_back(inpN1);
            UniValue keysArray1(UniValue::VARR);
            keysArray1.push_back("02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            keysArray1.push_back("03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            UniValue inpSigs1(UniValue::VOBJ); inpSigs1.pushKV("name", "Signatures"); inpSigs1.pushKV("type", "array"); inpSigs1.pushKV("value", keysArray1); exprInputs.push_back(inpSigs1);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VARR);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1782273600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "multi-signature");
            UniValue falseInputs2(UniValue::VARR);
            UniValue inpM2(UniValue::VOBJ); inpM2.pushKV("name", "m"); inpM2.pushKV("type", "number"); inpM2.pushKV("value", 3); falseInputs2.push_back(inpM2);
            UniValue inpN2(UniValue::VOBJ); inpN2.pushKV("name", "n"); inpN2.pushKV("type", "number"); inpN2.pushKV("value", 4); falseInputs2.push_back(inpN2);
            UniValue keysArray2(UniValue::VARR);
            keysArray2.push_back("03ab89ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1235");
            keysArray2.push_back("02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1236");
            keysArray2.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1237");
            keysArray2.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1238");
            UniValue inpSigs2(UniValue::VOBJ); inpSigs2.pushKV("name", "Signatures"); inpSigs2.pushKV("type", "array"); inpSigs2.pushKV("value", keysArray2); falseInputs2.push_back(inpSigs2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }
        else if (index == 27) { // IoT EV Charging Pay-per-Use
            UniValue act1(UniValue::VOBJ);
            act1.pushKV("role", "hash160");
            UniValue inputs1(UniValue::VARR);
            UniValue inp1(UniValue::VOBJ);
            inp1.pushKV("name", "Hash160");
            inp1.pushKV("type", "string-or-number");
            inp1.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            inputs1.push_back(inp1);
            act1.pushKV("inputs", inputs1);
            customActions.push_back(act1);

            UniValue act2(UniValue::VOBJ);
            act2.pushKV("role", "check-signature-verification");
            UniValue inputs2(UniValue::VARR);
            UniValue inp2(UniValue::VOBJ);
            inp2.pushKV("name", "Pubkey");
            inp2.pushKV("type", "pubkey");
            inp2.pushKV("value", "02cd98ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1234");
            inputs2.push_back(inp2);
            act2.pushKV("inputs", inputs2);
            customActions.push_back(act2);
        }
        else if (index == 28) { // Parametric Insurance Claim
            UniValue condNode(UniValue::VOBJ);
            condNode.pushKV("role", "if-condition");

            UniValue expr(UniValue::VOBJ);
            expr.pushKV("role", "hash160");
            UniValue exprInputs(UniValue::VARR);
            UniValue exprInp(UniValue::VOBJ);
            exprInp.pushKV("name", "Hash160");
            exprInp.pushKV("type", "string-or-number");
            exprInp.pushKV("value", "b5a9c9f285d893ce71ab9de8f5c09d765ee982ba");
            exprInputs.push_back(exprInp);
            expr.pushKV("inputs", exprInputs);
            condNode.pushKV("expression", expr);

            UniValue trueAct(UniValue::VOBJ);
            trueAct.pushKV("role", "check-signature-verification");
            UniValue trueInputs(UniValue::VARR);
            UniValue trueInp(UniValue::VOBJ);
            trueInp.pushKV("name", "Pubkey");
            trueInp.pushKV("type", "pubkey");
            trueInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
            trueInputs.push_back(trueInp);
            trueAct.pushKV("inputs", trueInputs);
            condNode.pushKV("true_action", trueAct);

            UniValue falseAct(UniValue::VARR);

            UniValue falseAct1(UniValue::VOBJ);
            falseAct1.pushKV("role", "lock-time");
            UniValue falseInputs1(UniValue::VARR);
            UniValue falseInp1(UniValue::VOBJ);
            falseInp1.pushKV("name", "Lock-Until");
            falseInp1.pushKV("type", "timestamp-or-block-height");
            falseInp1.pushKV("value", (int64_t)1782273600);
            falseInputs1.push_back(falseInp1);
            falseAct1.pushKV("inputs", falseInputs1);
            falseAct.push_back(falseAct1);

            UniValue falseAct2(UniValue::VOBJ);
            falseAct2.pushKV("role", "check-signature-verification");
            UniValue falseInputs2(UniValue::VARR);
            UniValue falseInp2(UniValue::VOBJ);
            falseInp2.pushKV("name", "Pubkey");
            falseInp2.pushKV("type", "pubkey");
            falseInp2.pushKV("value", "03ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660a");
            falseInputs2.push_back(falseInp2);
            falseAct2.pushKV("inputs", falseInputs2);
            falseAct.push_back(falseAct2);

            condNode.pushKV("false_action", falseAct);
            customActions.push_back(condNode);
        }

        updateCustomTree();
        buildContractFromCustom();
    }
}

// ═══════════════════════════════════════════════════════════════════
//  RUN TAB
// ═══════════════════════════════════════════════════════════════════

void SmartContractWidget::onRefreshContractsClicked()
{
    if (!pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    // Preserve previous selection if possible
    uint256 prevTxid;
    int prevVout = -1;
    int prevIdx = ui->comboContracts->currentIndex();
    if (prevIdx >= 0 && prevIdx < m_utxos.size()) {
        prevTxid = m_utxos[prevIdx].txid;
        prevVout = m_utxos[prevIdx].vout;
    }

    m_utxos.clear();
    {
        QSignalBlocker blocker(ui->comboContracts);
        ui->comboContracts->clear();

        LOCK2(cs_main, pwalletMain->cs_wallet);

        for (const auto& kv : pwalletMain->mapWallet) {
            const CWalletTx& wtx = kv.second;
            int depth = wtx.GetDepthInMainChain();

            for (int i = 0; i < (int)wtx.vout.size(); i++) {
                const CTxOut& out = wtx.vout[i];

                // Skip if already spent
                if (pwalletMain->IsSpent(kv.first, i))
                    continue;

                // Accept MESCAL contracts, ADAM lock contracts, multisig contracts, and non-standard scripts;
                // skip regular P2PKH, P2PK, P2SH, OP_RETURN, and Zerocoin outputs.
                txnouttype scriptType;
                std::vector<std::vector<unsigned char>> vSolutions;
                if (Solver(out.scriptPubKey, scriptType, vSolutions)) {
                    if (scriptType != TX_NONSTANDARD &&
                        scriptType != TX_MESCAL_CONTRACT &&
                        scriptType != TX_ADAM_COINLOCK &&
                        scriptType != TX_ADAM_POWLOCK &&
                        scriptType != TX_MULTISIG)
                        continue;
                }

                // Look up or compute decompilation + round-trip check in cache
                auto cacheIt = m_scriptCache.find(out.scriptPubKey);
                if (cacheIt == m_scriptCache.end()) {
                    CachedDecompile entry;
                    std::string decompileErr;
                    entry.decompiled = CMescal::Decompile(out.scriptPubKey, decompileErr);
                    if (decompileErr.empty() && entry.decompiled.exists("actions")) {
                        const UniValue& acts = entry.decompiled["actions"];
                        if (acts.isArray() && !acts.empty()) {
                            std::string recompileErr;
                            CScript recompiled = CMescal::Compile(entry.decompiled.write(), recompileErr);
                            if (recompileErr.empty() && recompiled == out.scriptPubKey) {
                                entry.isValidContract = true;
                            }
                        }
                    }
                    cacheIt = m_scriptCache.emplace(out.scriptPubKey, std::move(entry)).first;
                }

                if (!cacheIt->second.isValidContract)
                    continue;

                ContractUtxo utxo;
                utxo.txid         = kv.first;
                utxo.vout         = i;
                utxo.amount       = out.nValue;
                utxo.depth        = depth;
                utxo.scriptPubKey = out.scriptPubKey;
                utxo.decompiled   = cacheIt->second.decompiled;

                m_utxos.append(utxo);

                QString label = QString("%1...:%2  |  %3 KRISTA  (%4 confs)")
                    .arg(QString::fromStdString(kv.first.GetHex()).left(8))
                    .arg(i)
                    .arg(out.nValue / (double)COIN, 0, 'f', 4)
                    .arg(depth);

                ui->comboContracts->addItem(label);
            }
        }
    }

    if (m_utxos.isEmpty()) {
        clearRunDetails();
        ui->labelRunStatus->setText(tr("Status: No unspent contract UTXOs found in wallet."));
    } else {
        ui->labelRunStatus->setText(tr("Status: Found %1 contract UTXO(s).").arg(m_utxos.size()));
        int selectIdx = 0;
        if (prevVout >= 0) {
            for (int i = 0; i < m_utxos.size(); ++i) {
                if (m_utxos[i].txid == prevTxid && m_utxos[i].vout == prevVout) {
                    selectIdx = i;
                    break;
                }
            }
        }
        {
            QSignalBlocker blocker(ui->comboContracts);
            ui->comboContracts->setCurrentIndex(selectIdx);
        }
        // Only reset input fields if the selected contract changed or if details were empty
        if (prevVout != m_utxos[selectIdx].vout || prevTxid != m_utxos[selectIdx].txid || ui->labelUtxoTxId->text() == "—") {
            onContractSelected(selectIdx);
        } else {
            ui->labelUtxoConfs->setText(QString::number(m_utxos[selectIdx].depth));
        }
    }
}

void SmartContractWidget::onContractSelected(int index)
{
    if (index < 0 || index >= m_utxos.size()) {
        clearRunDetails();
        return;
    }
    populateRunDetails(m_utxos[index]);
}

void SmartContractWidget::populateRunDetails(const ContractUtxo& utxo)
{
    ui->labelUtxoTxId->setText(QString::fromStdString(utxo.txid.GetHex()));
    ui->labelUtxoVout->setText(QString::number(utxo.vout));
    ui->labelUtxoAmount->setText(tr("%1 KRISTA").arg(utxo.amount / (double)COIN, 0, 'f', 8));
    ui->labelUtxoConfs->setText(QString::number(utxo.depth));

    // Decompiled JSON
    ui->textEditDecompiled->setText(QString::fromStdString(utxo.decompiled.write(2)));

    // Raw script hex
    std::vector<unsigned char> scriptBytes(utxo.scriptPubKey.begin(), utxo.scriptPubKey.end());
    ui->lineEditRawScript->setText(QString::fromStdString(HexStr(scriptBytes)));

    detectAutoSignCapabilities(utxo);
}

void SmartContractWidget::clearRunDetails()
{
    ui->labelUtxoTxId->setText("—");
    ui->labelUtxoVout->setText("—");
    ui->labelUtxoAmount->setText("—");
    ui->labelUtxoConfs->setText("—");
    ui->textEditDecompiled->clear();
    ui->lineEditRawScript->clear();

    ui->lblAutoSignStatus->setText("");
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();
    ui->plainTextExtraSigs->setPlaceholderText(tr("Additional DER signatures for multi-sig (one hex per line)"));
}

void SmartContractWidget::detectAutoSignCapabilities(const ContractUtxo& utxo)
{
    // Reset status and inputs
    ui->lblAutoSignStatus->setStyleSheet("");
    ui->lblAutoSignStatus->setText("");
    ui->lineEditParam1->setEnabled(true);
    ui->lineEditParam1->clear();
    ui->lineEditParam1->setPlaceholderText(tr("DER-encoded signature or preimage (hex)"));
    ui->lineEditParam2->setEnabled(true);
    ui->lineEditParam2->clear();
    ui->lineEditParam2->setPlaceholderText(tr("Compressed public key (hex, for sig-check contracts)"));
    ui->plainTextExtraSigs->setEnabled(true);
    ui->plainTextExtraSigs->clear();

    if (!pwalletMain) return;

    // Detect contract type from decompiled
    bool isTimeLock = false;
    bool isSigCheck = false;
    bool isPkhCheck = false;
    bool isMultiSig = false;
    bool isHashLock = false;
    bool isMinerReg = false;
    bool isIfCond   = false;

    std::string timeLockOwnerPubkey = "";
    std::string pkhHex = "";
    std::vector<std::string> multiSigPubkeys;
    int multiSigM = 0;
    int multiSigN = 0;
    std::string hashLockPubkey = "";
    int64_t lockTimeVal = 0;
    bool ifExprIsHashLock = false;
    std::string ifTruePubkey = "";
    std::string ifFalsePubkey = "";
    int64_t ifFalseLockTime = 0;

    if (utxo.decompiled.exists("actions")) {
        const UniValue& actions = utxo.decompiled["actions"];
        if (actions.isArray()) {
            for (unsigned int i = 0; i < actions.size(); i++) {
                const UniValue& act = actions[i];
                if (!act.isObject()) continue;
                std::string role = act.exists("role") ? act["role"].get_str() : "";
                UniValue inputs = act.exists("inputs") ? act["inputs"] : UniValue();
                if (role == "lock-time") {
                    isTimeLock = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        lockTimeVal = inputs[0]["value"].isNum() ? inputs[0]["value"].get_int64() : atoll(inputs[0]["value"].getValStr().c_str());
                    }
                } else if (role == "check-signature-verification") {
                    isSigCheck = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        std::string pk = inputs[0]["value"].getValStr();
                        if (isHashLock) hashLockPubkey = pk;
                        else timeLockOwnerPubkey = pk;
                    }
                } else if (role == "equalverify-checksig") {
                    isPkhCheck = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        pkhHex = inputs[0]["value"].getValStr();
                    }
                } else if (role == "multi-signature") {
                    isMultiSig = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            std::string inputName = inputs[j]["name"].get_str();
                            if (inputName == "m") {
                                multiSigM = inputs[j]["value"].get_int();
                            } else if (inputName == "n") {
                                multiSigN = inputs[j]["value"].get_int();
                            } else if (inputName == "Signatures") {
                                const UniValue& sigsVal = inputs[j]["value"];
                                if (sigsVal.isArray()) {
                                    for (unsigned int k = 0; k < sigsVal.size(); k++) {
                                        multiSigPubkeys.push_back(sigsVal[k].get_str());
                                    }
                                }
                            }
                        }
                    }
                } else if (role == "hash160") {
                    isHashLock = true;
                } else if (role == "coin-lock-miner" || role == "pow-miner") {
                    isMinerReg = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            if (!inputs[j].isObject() || !inputs[j].exists("name") || !inputs[j].exists("value")) continue;
                            std::string n = inputs[j]["name"].get_str();
                            if (n == "OwnerPubKey") timeLockOwnerPubkey = inputs[j]["value"].getValStr();
                            else if (n == "LockHeight") lockTimeVal = inputs[j]["value"].isNum() ? inputs[j]["value"].get_int64() : atoll(inputs[j]["value"].getValStr().c_str());
                        }
                    }
                } else if (role == "if-condition") {
                    isIfCond = true;
                    if (act.exists("expressions") && act["expressions"].isArray()) {
                        for (unsigned int k = 0; k < act["expressions"].size(); ++k) {
                            const UniValue& ex = act["expressions"][k];
                            if (ex.isObject() && ex.exists("role") && ex["role"].get_str() == "hash160") {
                                ifExprIsHashLock = true;
                            }
                        }
                    }
                    const UniValue& trueArr = act.exists("true") ? act["true"] : (act.exists("true_action") ? act["true_action"] : UniValue());
                    if (trueArr.isArray()) {
                        for (unsigned int k = 0; k < trueArr.size(); ++k) {
                            const UniValue& ta = trueArr[k];
                            if (ta.isObject() && ta.exists("role") && ta["role"].get_str() == "check-signature-verification" &&
                                ta.exists("inputs") && ta["inputs"].isArray() && !ta["inputs"].empty()) {
                                ifTruePubkey = ta["inputs"][0]["value"].getValStr();
                            }
                        }
                    }
                    const UniValue& falseArr = act.exists("false") ? act["false"] : (act.exists("false_action") ? act["false_action"] : UniValue());
                    if (falseArr.isArray()) {
                        for (unsigned int k = 0; k < falseArr.size(); ++k) {
                            const UniValue& fa = falseArr[k];
                            if (!fa.isObject() || !fa.exists("role")) continue;
                            if (fa["role"].get_str() == "lock-time" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                                ifFalseLockTime = fa["inputs"][0]["value"].isNum() ? fa["inputs"][0]["value"].get_int64() : atoll(fa["inputs"][0]["value"].getValStr().c_str());
                            } else if (fa["role"].get_str() == "check-signature-verification" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                                ifFalsePubkey = fa["inputs"][0]["value"].getValStr();
                            }
                        }
                    }
                }
            }
        }
    }

    LOCK(pwalletMain->cs_wallet);

    if ((isTimeLock || isMinerReg || (isSigCheck && !isHashLock)) && !timeLockOwnerPubkey.empty()) {
        CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
        if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
            if (isMinerReg) {
                int64_t nHeight = chainActive.Height();
                int64_t remaining = lockTimeVal - nHeight;
                if (remaining > 0) {
                    if (remaining <= 240) { // Under 2 hours warning (orange)
                        ui->lblAutoSignStatus->setStyleSheet("color: #ff9100; font-weight: bold;");
                        ui->lblAutoSignStatus->setText(tr("⚠ Miner kaydı yakında bitecek! Kalan Kilit Süresi: %1 blok (~%2 dakika). Otomatik imzalanacaktır.").arg(remaining).arg(remaining * 30 / 60));
                    } else { // Active (green)
                        ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
                        ui->lblAutoSignStatus->setText(tr("✓ Gerekli imza anahtarı bulundu. Kalan Kilit Süresi: %1 blok (~%2 saat). Otomatik imzalanacaktır.").arg(remaining).arg(QString::number(remaining * 30 / 3600.0, 'f', 1)));
                    }
                } else { // Expired (bright green)
                    ui->lblAutoSignStatus->setStyleSheet("color: #00e676; font-weight: bold;");
                    ui->lblAutoSignStatus->setText(tr("✓ Kilit süresi doldu! Miner kayıt depozitonuzu geri çekmek için bu işlemi otomatik imzalayabilirsiniz."));
                }
            } else {
                ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
                ui->lblAutoSignStatus->setText(tr("✓ Gerekli imza anahtarı cüzdanınızda bulundu. İşlem otomatik olarak imzalanacaktır."));
            }
            
            // Disable manual parameters since they are not needed
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam1->setPlaceholderText(tr("(Cüzdan tarafından otomatik imzalanacak)"));
            ui->lineEditParam2->setText(QString::fromStdString(timeLockOwnerPubkey));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
            if (isMinerReg) {
                int64_t nHeight = chainActive.Height();
                int64_t remaining = lockTimeVal - nHeight;
                if (remaining > 0) {
                    ui->lblAutoSignStatus->setText(tr("⚠ Harcama anahtarı bu cüzdanda değil. Kalan Kilit Süresi: %1 blok. Manuel imzalayabilirsiniz.").arg(remaining));
                } else {
                    ui->lblAutoSignStatus->setText(tr("⚠ Harcama anahtarı bu cüzdanda değil. Kilit süresi doldu! Manuel imzalayarak çekebilirsiniz."));
                }
            } else {
                ui->lblAutoSignStatus->setText(tr("⚠ Harcama anahtarı bu cüzdanda değil. İmza parametrelerini manuel girmelisiniz."));
            }
        }
    } else if (isPkhCheck && pkhHex.size() == 40 && IsHex(pkhHex)) {
        std::vector<unsigned char> pkhBytes = ParseHex(pkhHex);
        CKeyID keyId{uint160(pkhBytes)};
        CPubKey pubKey;
        if (pwalletMain->HaveKey(keyId) && pwalletMain->GetPubKey(keyId, pubKey)) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ PubKeyHash anahtarı cüzdanınızda bulundu. İşlem otomatik olarak imzalanacaktır."));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam1->setPlaceholderText(tr("(Cüzdan tarafından otomatik imzalanacak)"));
            ui->lineEditParam2->setText(QString::fromStdString(HexStr(pubKey.begin(), pubKey.end())));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
            ui->lblAutoSignStatus->setText(tr("⚠ PubKeyHash anahtarı bu cüzdanda değil. İmza ve açık anahtarı manuel girmelisiniz."));
        }
    } else if (isMultiSig) {
        int ownedKeys = 0;
        for (const auto& pkHex : multiSigPubkeys) {
            CPubKey pubKey(ParseHex(pkHex));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ownedKeys++;
            }
        }

        if (ownedKeys >= multiSigM) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ Cüzdanda %1/%2 imza anahtarı bulundu (%3 imza yeterli). İşlem otomatik imzalanacaktır.").arg(ownedKeys).arg(multiSigN).arg(multiSigM));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else if (ownedKeys > 0) {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
            ui->lblAutoSignStatus->setText(tr("⚠ Cüzdanda %1/%2 imza anahtarı bulundu. Eksik %3 imza için lütfen diğer imzaları aşağıya girin.").arg(ownedKeys).arg(multiSigN).arg(multiSigM - ownedKeys));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setPlaceholderText(tr("Lütfen eksik %1 imzayı buraya (satır başına bir tane) girin").arg(multiSigM - ownedKeys));
        } else {
            ui->lblAutoSignStatus->setStyleSheet("color: #ff3d00;");
            ui->lblAutoSignStatus->setText(tr("✗ Cüzdanda hiç imza anahtarı bulunamadı. Lütfen tüm %1 imzayı manuel girin.").arg(multiSigM));
        }
    } else if (isHashLock) {
        ui->lineEditParam1->setPlaceholderText(tr("Şifre Çözücü Metin (Preimage) girin"));
        ui->lineEditParam2->setEnabled(false);
        if (!hashLockPubkey.empty()) {
            CPubKey pubKey(ParseHex(hashLockPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
                ui->lblAutoSignStatus->setText(tr("✓ Alıcı imza anahtarı cüzdanda bulundu. Sadece Şifre Çözücü Metin (Preimage) girmelisiniz."));
                ui->lineEditParam2->setText(QString::fromStdString(hashLockPubkey));
            } else {
                ui->lblAutoSignStatus->setStyleSheet("color: #ff9100;");
                ui->lblAutoSignStatus->setText(tr("⚠ Alıcı imza anahtarı cüzdanda değil. Şifre Çözücü Metin ve manuel Alıcı İmzası girmelisiniz."));
                ui->lineEditParam2->setEnabled(true);
            }
        }
    } else if (isIfCond) {
        CPubKey truePk(ParseHex(ifTruePubkey));
        CPubKey falsePk(ParseHex(ifFalsePubkey));
        bool hasTrueKey = truePk.IsValid() && pwalletMain->HaveKey(truePk.GetID());
        bool hasFalseKey = falsePk.IsValid() && pwalletMain->HaveKey(falsePk.GetID());
        if (hasTrueKey && ifExprIsHashLock) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ Koşullu (IF) sözleşme anahtarı cüzdanda bulundu. TRUE dalı için Preimage girin (veya FALSE dalı için boş bırakın)."));
            ui->lineEditParam1->setPlaceholderText(tr("TRUE dalı için Preimage girin (FALSE/İade dalı için boş bırakın)"));
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        } else if (hasFalseKey) {
            ui->lblAutoSignStatus->setStyleSheet("color: #26a69a; font-weight: bold;");
            ui->lblAutoSignStatus->setText(tr("✓ Koşullu (ELSE/İade) sözleşme anahtarı cüzdanda bulundu. Otomatik imzalanacaktır."));
            ui->lineEditParam1->setEnabled(false);
            ui->lineEditParam2->setEnabled(false);
            ui->plainTextExtraSigs->setEnabled(false);
        }
    }
}

void SmartContractWidget::onRunContractClicked()
{
    int idx = ui->comboContracts->currentIndex();
    if (idx < 0 || idx >= m_utxos.size()) {
        ui->labelRunStatus->setText(tr("Status: No contract selected."));
        return;
    }
    if (!walletModel || !pwalletMain) {
        ui->labelRunStatus->setText(tr("Status: Wallet not loaded."));
        return;
    }

    const ContractUtxo& utxo = m_utxos[idx];

    // ── Validate recipient ────────────────────────────────────────────────────
    QString recipAddrStr = ui->lineEditRecipAddr->text().trimmed();
    if (recipAddrStr.isEmpty()) {
        QMessageBox::critical(this, tr("Missing Input"), tr("Please enter a recipient address."));
        return;
    }
    CTxDestination dest = DecodeDestination(recipAddrStr.toStdString());
    if (!IsValidDestination(dest)) {
        QMessageBox::critical(this, tr("Invalid Address"), tr("The recipient address is not valid."));
        return;
    }
    CScript recipScript = GetScriptForDestination(dest);

    // ── Build spending transaction ────────────────────────────────────────────
    CMutableTransaction rawTx;
    rawTx.nVersion = 1;

    // Input: spend the contract UTXO (start with empty scriptSig)
    CTxIn txIn(COutPoint(utxo.txid, utxo.vout));
    // For time-locked contracts, set nSequence to allow CLTV
    txIn.nSequence = std::numeric_limits<uint32_t>::max() - 1;
    rawTx.vin.push_back(txIn);

    // Calculate fee (estimate: 10000 satoshis for standard regtest/mainnet)
    CAmount feeEstimate = 10000;
    CAmount sendAmount = utxo.amount - feeEstimate;
    if (sendAmount <= 0) {
        QMessageBox::critical(this, tr("Error"), tr("UTXO amount too small to cover fee."));
        return;
    }

    // Output: recipient
    rawTx.vout.push_back(CTxOut(sendAmount, recipScript));

    // Detect contract type from decompiled for locktime setting
    bool isTimeLock = false;
    bool isSigCheck = false;
    bool isPkhCheck = false;
    bool isMultiSig = false;
    bool isHashLock = false;
    bool isMinerReg = false;
    bool isIfCond   = false;
    int64_t lockTimeVal = 0;
    std::string timeLockOwnerPubkey = "";
    std::string pkhHex = "";
    std::vector<std::string> multiSigPubkeys;
    int multiSigM = 0;
    int multiSigN = 0;
    std::string hashLockPubkey = "";
    bool ifExprIsHashLock = false;
    std::string ifTruePubkey = "";
    std::string ifFalsePubkey = "";
    int64_t ifFalseLockTime = 0;

    if (utxo.decompiled.exists("actions")) {
        const UniValue& actions = utxo.decompiled["actions"];
        if (actions.isArray()) {
            for (unsigned int i = 0; i < actions.size(); i++) {
                const UniValue& act = actions[i];
                if (!act.isObject()) continue;
                std::string role = act.exists("role") ? act["role"].get_str() : "";
                UniValue inputs = act.exists("inputs") ? act["inputs"] : UniValue();
                if (role == "lock-time") {
                    isTimeLock = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        lockTimeVal = inputs[0]["value"].isNum() ? inputs[0]["value"].get_int64() : atoll(inputs[0]["value"].getValStr().c_str());
                    }
                } else if (role == "check-signature-verification") {
                    isSigCheck = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        std::string pk = inputs[0]["value"].getValStr();
                        if (isHashLock) hashLockPubkey = pk;
                        else timeLockOwnerPubkey = pk;
                    }
                } else if (role == "equalverify-checksig") {
                    isPkhCheck = true;
                    if (inputs.isArray() && !inputs.empty() && inputs[0].exists("value")) {
                        pkhHex = inputs[0]["value"].getValStr();
                    }
                } else if (role == "multi-signature") {
                    isMultiSig = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            std::string inputName = inputs[j]["name"].get_str();
                            if (inputName == "m") {
                                multiSigM = inputs[j]["value"].get_int();
                            } else if (inputName == "n") {
                                multiSigN = inputs[j]["value"].get_int();
                            } else if (inputName == "Signatures") {
                                const UniValue& sigsVal = inputs[j]["value"];
                                if (sigsVal.isArray()) {
                                    for (unsigned int k = 0; k < sigsVal.size(); k++) {
                                        multiSigPubkeys.push_back(sigsVal[k].get_str());
                                    }
                                }
                            }
                        }
                    }
                } else if (role == "hash160") {
                    isHashLock = true;
                } else if (role == "coin-lock-miner" || role == "pow-miner") {
                    isMinerReg = true;
                    if (inputs.isArray()) {
                        for (unsigned int j = 0; j < inputs.size(); j++) {
                            if (!inputs[j].isObject() || !inputs[j].exists("name") || !inputs[j].exists("value")) continue;
                            std::string n = inputs[j]["name"].get_str();
                            if (n == "OwnerPubKey") timeLockOwnerPubkey = inputs[j]["value"].getValStr();
                            else if (n == "LockHeight") lockTimeVal = inputs[j]["value"].isNum() ? inputs[j]["value"].get_int64() : atoll(inputs[j]["value"].getValStr().c_str());
                        }
                    }
                } else if (role == "if-condition") {
                    isIfCond = true;
                    if (act.exists("expressions") && act["expressions"].isArray()) {
                        for (unsigned int k = 0; k < act["expressions"].size(); ++k) {
                            const UniValue& ex = act["expressions"][k];
                            if (ex.isObject() && ex.exists("role") && ex["role"].get_str() == "hash160") {
                                ifExprIsHashLock = true;
                            }
                        }
                    }
                    const UniValue& trueArr = act.exists("true") ? act["true"] : (act.exists("true_action") ? act["true_action"] : UniValue());
                    if (trueArr.isArray()) {
                        for (unsigned int k = 0; k < trueArr.size(); ++k) {
                            const UniValue& ta = trueArr[k];
                            if (ta.isObject() && ta.exists("role") && ta["role"].get_str() == "check-signature-verification" &&
                                ta.exists("inputs") && ta["inputs"].isArray() && !ta["inputs"].empty()) {
                                ifTruePubkey = ta["inputs"][0]["value"].getValStr();
                            }
                        }
                    }
                    const UniValue& falseArr = act.exists("false") ? act["false"] : (act.exists("false_action") ? act["false_action"] : UniValue());
                    if (falseArr.isArray()) {
                        for (unsigned int k = 0; k < falseArr.size(); ++k) {
                            const UniValue& fa = falseArr[k];
                            if (!fa.isObject() || !fa.exists("role")) continue;
                            if (fa["role"].get_str() == "lock-time" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                                ifFalseLockTime = fa["inputs"][0]["value"].isNum() ? fa["inputs"][0]["value"].get_int64() : atoll(fa["inputs"][0]["value"].getValStr().c_str());
                            } else if (fa["role"].get_str() == "check-signature-verification" && fa.exists("inputs") && fa["inputs"].isArray() && !fa["inputs"].empty()) {
                                ifFalsePubkey = fa["inputs"][0]["value"].getValStr();
                            }
                        }
                    }
                }
            }
        }
    }

    QString param1Input = ui->lineEditParam1->text().trimmed();
    if ((isTimeLock || isMinerReg) && lockTimeVal > 0) {
        rawTx.nLockTime = (uint32_t)lockTimeVal;
    } else if (isIfCond && param1Input.isEmpty() && ifFalseLockTime > 0) {
        rawTx.nLockTime = (uint32_t)ifFalseLockTime;
    }

    // ── Unlock Wallet ─────────────────────────────────────────────────────────
    WalletModel::UnlockContext ctx(walletModel->requestUnlock());
    if (!ctx.isValid()) {
        ui->labelRunStatus->setText(tr("Status: Wallet unlock failed."));
        return;
    }

    // ── Build scriptSig (unlock) ──────────────────────────────────────────────
    CScript scriptSig;

    // Check if auto-signing is possible
    bool useAutoSign = false;
    int ownedMultiSigKeys = 0;
    {
        LOCK(pwalletMain->cs_wallet);
        if ((isTimeLock || isMinerReg || (isSigCheck && !isHashLock)) && !timeLockOwnerPubkey.empty()) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                useAutoSign = true;
            }
        } else if (isPkhCheck && pkhHex.size() == 40 && IsHex(pkhHex)) {
            CKeyID keyId(uint160(ParseHex(pkhHex)));
            if (pwalletMain->HaveKey(keyId)) {
                useAutoSign = true;
            }
        } else if (isMultiSig) {
            for (const auto& pkHex : multiSigPubkeys) {
                CPubKey pubKey(ParseHex(pkHex));
                if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                    ownedMultiSigKeys++;
                }
            }
            if (ownedMultiSigKeys > 0) {
                useAutoSign = true;
            }
        } else if (isHashLock && !hashLockPubkey.empty()) {
            CPubKey pubKey(ParseHex(hashLockPubkey));
            if (pubKey.IsValid() && pwalletMain->HaveKey(pubKey.GetID())) {
                useAutoSign = true;
            }
        } else if (isIfCond) {
            CPubKey truePk(ParseHex(ifTruePubkey));
            CPubKey falsePk(ParseHex(ifFalsePubkey));
            if ((!param1Input.isEmpty() && truePk.IsValid() && pwalletMain->HaveKey(truePk.GetID())) ||
                (param1Input.isEmpty() && falsePk.IsValid() && pwalletMain->HaveKey(falsePk.GetID()))) {
                useAutoSign = true;
            }
        }
    }

    if (useAutoSign) {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        uint256 hash = SignatureHash(utxo.scriptPubKey, rawTx, 0, SIGHASH_ALL, 0, SIGVERSION_BASE);

        if (isMinerReg) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    // Pushes [sig] [pubkey]
                    scriptSig << vchSig << ParseHex(timeLockOwnerPubkey);
                }
            }
        } else if (isTimeLock || (isSigCheck && !isHashLock)) {
            CPubKey pubKey(ParseHex(timeLockOwnerPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    scriptSig << OP_1 << vchSig;
                }
            }
        } else if (isPkhCheck) {
            CKeyID keyId(uint160(ParseHex(pkhHex)));
            CKey key;
            CPubKey pubKey;
            if (pwalletMain->GetKey(keyId, key) && pwalletMain->GetPubKey(keyId, pubKey)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    scriptSig << vchSig << ToByteVector(pubKey);
                }
            }
        } else if (isMultiSig) {
            std::vector<std::vector<unsigned char>> sigs;
            int foundSigs = 0;
            for (const auto& pkHex : multiSigPubkeys) {
                CPubKey pubKey(ParseHex(pkHex));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        sigs.push_back(vchSig);
                        foundSigs++;
                    }
                }
                if (foundSigs >= multiSigM) break;
            }
            // Append any extra signatures provided manually by user
            QString extraSigsText = ui->plainTextExtraSigs->toPlainText().trimmed();
            if (!extraSigsText.isEmpty() && foundSigs < multiSigM) {
                QStringList extraSigs = extraSigsText.split('\n', QString::SkipEmptyParts);
                for (const QString& sig : extraSigs) {
                    std::string sigHex = sig.trimmed().toStdString();
                    if (!IsHex(sigHex)) {
                        QMessageBox::critical(this, tr("Invalid Input"),
                            tr("Extra signature is not valid hex: %1").arg(sig));
                        return;
                    }
                    sigs.push_back(ParseHex(sigHex));
                    foundSigs++;
                    if (foundSigs >= multiSigM) break;
                }
            }
            scriptSig << OP_0;
            for (const auto& sig : sigs) {
                scriptSig << sig;
            }
        } else if (isHashLock) {
            // Get preimage from Param1
            QString preimageStr = ui->lineEditParam1->text().trimmed();
            std::vector<unsigned char> preimageBytes;
            if (IsHex(preimageStr.toStdString())) {
                preimageBytes = ParseHex(preimageStr.toStdString());
            } else {
                std::string s = preimageStr.toStdString();
                preimageBytes.assign(s.begin(), s.end());
            }

            CPubKey pubKey(ParseHex(hashLockPubkey));
            CKey key;
            if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                std::vector<unsigned char> vchSig;
                if (key.Sign(hash, vchSig)) {
                    vchSig.push_back((unsigned char)SIGHASH_ALL);
                    // Stack: OP_1 <sig> <preimage>
                    scriptSig << OP_1 << vchSig << preimageBytes;
                }
            }
        } else if (isIfCond) {
            if (!param1Input.isEmpty() && ifExprIsHashLock) {
                std::vector<unsigned char> preimageBytes;
                if (IsHex(param1Input.toStdString())) {
                    preimageBytes = ParseHex(param1Input.toStdString());
                } else {
                    std::string s = param1Input.toStdString();
                    preimageBytes.assign(s.begin(), s.end());
                }
                CPubKey pubKey(ParseHex(ifTruePubkey));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        scriptSig << OP_1 << vchSig << preimageBytes;
                    }
                }
            } else {
                CPubKey pubKey(ParseHex(ifFalsePubkey));
                CKey key;
                if (pwalletMain->GetKey(pubKey.GetID(), key)) {
                    std::vector<unsigned char> vchSig;
                    if (key.Sign(hash, vchSig)) {
                        vchSig.push_back((unsigned char)SIGHASH_ALL);
                        scriptSig << OP_1 << vchSig << OP_0;
                    }
                }
            }
        }
    } else {
        // ── Manual Parameter Fallback ─────────────────────────────────────────
        if (isMinerReg) {
            QString sigStr = ui->lineEditParam1->text().trimmed();
            QString pubkeyStr = ui->lineEditParam2->text().trimmed();
            if (!sigStr.isEmpty() && !pubkeyStr.isEmpty()) {
                if (!IsHex(sigStr.toStdString()) || !IsHex(pubkeyStr.toStdString())) {
                    QMessageBox::critical(this, tr("Invalid Input"), tr("Signature and Public key must be valid hex."));
                    return;
                }
                // Order: <sig> <pubkey>
                scriptSig << ParseHex(sigStr.toStdString()) << ParseHex(pubkeyStr.toStdString());
            }
        } else {
            // Extra sigs (multisig: needs OP_0 first)
            QString extraSigsText = ui->plainTextExtraSigs->toPlainText().trimmed();
            QStringList extraSigs;
            if (!extraSigsText.isEmpty())
                extraSigs = extraSigsText.split('\n', QString::SkipEmptyParts);

            if (!extraSigs.isEmpty()) {
                scriptSig << OP_0;
                for (const QString& sig : extraSigs) {
                    std::string sigHex = sig.trimmed().toStdString();
                    if (!IsHex(sigHex)) {
                        QMessageBox::critical(this, tr("Invalid Input"),
                            tr("Extra signature is not valid hex: %1").arg(sig));
                        return;
                    }
                    scriptSig << ParseHex(sigHex);
                }
            }

            // Param2: pubkey (optional)
            QString param2Str = ui->lineEditParam2->text().trimmed();
            if (!param2Str.isEmpty()) {
                if (!IsHex(param2Str.toStdString())) {
                    QMessageBox::critical(this, tr("Invalid Input"), tr("Public key is not valid hex."));
                    return;
                }
                scriptSig << ParseHex(param2Str.toStdString());
            }

            // Param1: signature or preimage (optional)
            QString param1Str = ui->lineEditParam1->text().trimmed();
            if (!param1Str.isEmpty()) {
                if (!IsHex(param1Str.toStdString())) {
                    QMessageBox::critical(this, tr("Invalid Input"), tr("Signature / preimage is not valid hex."));
                    return;
                }
                scriptSig << ParseHex(param1Str.toStdString());
            }
        }
    }

    rawTx.vin[0].scriptSig = scriptSig;

    // ── Sign via wallet key store ─────────────────────────────────────────────
    // For contracts where the wallet holds the key, use SignSignature
    CTransaction prevTxFull;
    bool foundPrev = false;
    {
        LOCK(cs_main);
        LOCK(pwalletMain->cs_wallet);
        auto it = pwalletMain->mapWallet.find(utxo.txid);
        if (it != pwalletMain->mapWallet.end()) {
            prevTxFull = *static_cast<const CTransaction*>(&it->second);
            foundPrev = true;
        }
    }

    if (!foundPrev) {
        QMessageBox::critical(this, tr("Error"), tr("Could not find the contract transaction in wallet."));
        return;
    }

    // ── Commit ────────────────────────────────────────────────────────────────
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        // Wrap CMutableTransaction → CTransaction → CWalletTx
        CTransaction finalTx(rawTx);
        CWalletTx wtxSpend(pwalletMain, finalTx);
        wtxSpend.fTimeReceivedIsTxTime = 1;

        CReserveKey reservekey(pwalletMain);
        CWallet::CommitResult res = pwalletMain->CommitTransaction(wtxSpend, reservekey, g_connman.get());

        if (res.status != CWallet::CommitStatus::OK) {
            QString errMsg = QString::fromStdString(res.ToString());
            QMessageBox::critical(this, tr("Commit Failed"),
                tr("Failed to broadcast spending transaction:\n%1\n\nNote: For time-locked contracts, the lock time must have passed.").arg(errMsg));
            return;
        }

        walletModel->emitBalanceChanged();

        std::string txid = res.hashTx.GetHex();
        ui->labelRunStatus->setText(tr("Status: Contract executed! Spending TxID: %1")
                                        .arg(QString::fromStdString(txid)));
        inform(tr("Contract executed successfully!\nSpending TxID: %1").arg(QString::fromStdString(txid)));
    }

    // Refresh the UTXO list since the contract is now spent
    onRefreshContractsClicked();
}

// ═══════════════════════════════════════════════════════════════════
//  CUSTOM BUILDER & DEVELOPER MODE
// ═══════════════════════════════════════════════════════════════════

#include "qt/kristatech/visualdesignerdialog.h"
#include <QInputDialog>

void SmartContractWidget::onAddActionClicked()
{
    QStringList items;
    items << tr("Time-Locked (lock-time)")
          << tr("Signature Check (check-signature-verification)")
          << tr("Address / PubKeyHash Check (equalverify-checksig)")
          << tr("Multi-Signature (multi-signature)")
          << tr("Hash-Locked (hash160)")
          << tr("IF-Condition (if-condition)")
          << tr("Miner Registration: Coin-Lock (coin-lock-miner)")
          << tr("Miner Registration: PoW-Lock (pow-miner)")
          << tr("Push Number / True (number)");

    bool ok;
    QString item = getCustomItem(this, tr("Add Step"), tr("Select step type:"), items, 0, false, &ok);
    if (!ok || item.isEmpty()) return;

    QString defaultPubkey = ui->lineEditPubkeyTimeLock->text().trimmed();
    QString defaultPubkeyHash;
    if (!defaultPubkey.isEmpty() && IsHex(defaultPubkey.toStdString())) {
        CPubKey pk(ParseHex(defaultPubkey.toStdString()));
        if (pk.IsValid()) {
            CKeyID id = pk.GetID();
            defaultPubkeyHash = QString::fromStdString(HexStr(id.begin(), id.end()));
        }
    }
    int defaultLock = 150;
    {
        LOCK(cs_main);
        if (chainActive.Height() > 0) defaultLock = chainActive.Height() + 100;
    }

    UniValue node(UniValue::VOBJ);
    UniValue inputs(UniValue::VARR);

    if (item.contains("lock-time")) {
        int64_t lockTime = QInputDialog::getInt(this, tr("Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
        if (!ok) return;

        node.pushKV("role", "lock-time");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Lock-Until");
        inp.pushKV("type", "timestamp-or-block-height");
        inp.pushKV("value", lockTime);
        inputs.push_back(inp);
    }
    else if (item.contains("check-signature-verification")) {
        QString pubkey = QInputDialog::getText(this, tr("Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
        if (!ok || pubkey.isEmpty()) return;

        node.pushKV("role", "check-signature-verification");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Pubkey");
        inp.pushKV("type", "pubkey");
        inp.pushKV("value", pubkey.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (item.contains("equalverify-checksig")) {
        QString pkh = QInputDialog::getText(this, tr("Address / PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
        if (!ok || pkh.isEmpty()) return;

        node.pushKV("role", "equalverify-checksig");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "PubKeyHash");
        inp.pushKV("type", "string");
        inp.pushKV("value", pkh.trimmed().toStdString());
        inputs.push_back(inp);
    }
    else if (item.contains("multi-signature")) {
        bool ok1, ok2, ok3;
        int m = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
        if (!ok1) return;
        int n = QInputDialog::getInt(this, tr("Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
        if (!ok2) return;
        QString keysStr = QInputDialog::getText(this, tr("Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || keysStr.isEmpty()) return;

        node.pushKV("role", "multi-signature");
        UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); inputs.push_back(inpM);
        UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); inputs.push_back(inpN);

        UniValue keysArray(UniValue::VARR);
        QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
        for (const QString& key : keysList) {
            keysArray.push_back(key.trimmed().toStdString());
        }
        UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); inputs.push_back(inpSigs);
    }
    else if (item.contains("hash160")) {
        bool ok1, ok2;
        QString hashHex = QInputDialog::getText(this, tr("Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
        if (!ok1 || hashHex.isEmpty()) return;
        QString pubkey = QInputDialog::getText(this, tr("Hash-Locked"), tr("Recipient Pubkey (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || pubkey.isEmpty()) return;

        node.pushKV("role", "hash160");
        UniValue inpHash(UniValue::VOBJ);
        inpHash.pushKV("name", "Hash160");
        inpHash.pushKV("type", "string-or-number");
        inpHash.pushKV("value", hashHex.trimmed().toStdString());
        inputs.push_back(inpHash);
        node.pushKV("inputs", inputs);
        customActions.push_back(node);

        // Also add signature verification for hash-locked recipient sig
        UniValue sigNode(UniValue::VOBJ);
        sigNode.pushKV("role", "check-signature-verification");
        UniValue inpSig(UniValue::VOBJ);
        inpSig.pushKV("name", "Pubkey");
        inpSig.pushKV("type", "pubkey");
        inpSig.pushKV("value", pubkey.trimmed().toStdString());
        UniValue sigInputs(UniValue::VARR);
        sigInputs.push_back(inpSig);
        sigNode.pushKV("inputs", sigInputs);
        customActions.push_back(sigNode);

        updateCustomTree();
        generateContract();
        return;
    }
    else if (item.contains("if-condition")) {
        // 1. Get Expression
        QStringList exprTypes;
        exprTypes << tr("Hash-Locked (hash160)")
                  << tr("Signature Check (check-signature-verification)")
                  << tr("Stack Value / Number (number)");
        QString exprType = getCustomItem(this, tr("IF-Condition - Expression"), tr("Select condition check type:"), exprTypes, 0, false, &ok);
        if (!ok || exprType.isEmpty()) return;

        UniValue exprAct(UniValue::VOBJ);
        UniValue exprInputs(UniValue::VARR);
        if (exprType.contains("check-signature-verification")) {
            QString pubkey = QInputDialog::getText(this, tr("Expression - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            exprAct.pushKV("role", "check-signature-verification");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString());
            exprInputs.push_back(inp);
        } else if (exprType.contains("number")) {
            int val = QInputDialog::getInt(this, tr("Expression - Number"), tr("Integer Value (1 = TRUE, 0 = FALSE):"), 1, -1, 16, 1, &ok);
            if (!ok) return;
            exprAct.pushKV("role", "number");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Value"); inp.pushKV("type", "number"); inp.pushKV("value", val);
            exprInputs.push_back(inp);
        } else {
            bool ok1;
            QString hashHex = QInputDialog::getText(this, tr("Expression - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
            if (!ok1 || hashHex.isEmpty()) return;
            exprAct.pushKV("role", "hash160");
            UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Hash160"); inp.pushKV("type", "string-or-number"); inp.pushKV("value", hashHex.trimmed().toStdString());
            exprInputs.push_back(inp);
        }
        exprAct.pushKV("inputs", exprInputs);

        // 2. Get True Action
        QStringList actionTypes;
        actionTypes << tr("Signature Check (check-signature-verification)")
                    << tr("Time-Locked + Signature Check (lock-time + check-signature-verification)")
                    << tr("Address / PubKeyHash Check (equalverify-checksig)")
                    << tr("Multi-Signature (multi-signature)")
                    << tr("Time-Locked (lock-time)")
                    << tr("Hash-Locked (hash160)");
        QString trueType = getCustomItem(this, tr("IF-Condition - True Branch"), tr("Select action when TRUE:"), actionTypes, 0, false, &ok);
        if (!ok || trueType.isEmpty()) return;

        UniValue trueBranch(UniValue::VARR);
        if (trueType.contains("lock-time + check-signature-verification")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("True Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
            if (!ok) return;
            QString pubkey = QInputDialog::getText(this, tr("True Branch - Signature Check"), tr("Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            UniValue ltAct(UniValue::VOBJ), ltInputs(UniValue::VARR), ltInp(UniValue::VOBJ);
            ltAct.pushKV("role", "lock-time");
            ltInp.pushKV("name", "Lock-Until"); ltInp.pushKV("type", "timestamp-or-block-height"); ltInp.pushKV("value", lockTime);
            ltInputs.push_back(ltInp); ltAct.pushKV("inputs", ltInputs);
            trueBranch.push_back(ltAct);

            UniValue sigAct(UniValue::VOBJ), sigInputs(UniValue::VARR), sigInp(UniValue::VOBJ);
            sigAct.pushKV("role", "check-signature-verification");
            sigInp.pushKV("name", "Pubkey"); sigInp.pushKV("type", "pubkey"); sigInp.pushKV("value", pubkey.trimmed().toStdString());
            sigInputs.push_back(sigInp); sigAct.pushKV("inputs", sigInputs);
            trueBranch.push_back(sigAct);
        } else {
            UniValue trueAct(UniValue::VOBJ);
            UniValue trueInputs(UniValue::VARR);
            if (trueType.contains("lock-time")) {
                int64_t lockTime = QInputDialog::getInt(this, tr("True Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
                if (!ok) return;
                trueAct.pushKV("role", "lock-time");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); trueInputs.push_back(inp);
            } else if (trueType.contains("check-signature-verification")) {
                QString pubkey = QInputDialog::getText(this, tr("True Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
                if (!ok || pubkey.isEmpty()) return;
                trueAct.pushKV("role", "check-signature-verification");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); trueInputs.push_back(inp);
            } else if (trueType.contains("equalverify-checksig")) {
                QString pkh = QInputDialog::getText(this, tr("True Branch - PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
                if (!ok || pkh.isEmpty()) return;
                trueAct.pushKV("role", "equalverify-checksig");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "PubKeyHash"); inp.pushKV("type", "string"); inp.pushKV("value", pkh.trimmed().toStdString()); trueInputs.push_back(inp);
            } else if (trueType.contains("multi-signature")) {
                bool ok1, ok2, ok3;
                int m = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
                if (!ok1) return;
                int n = QInputDialog::getInt(this, tr("True Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
                if (!ok2) return;
                QString keysStr = QInputDialog::getText(this, tr("True Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
                if (!ok3 || keysStr.isEmpty()) return;
                trueAct.pushKV("role", "multi-signature");
                UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); trueInputs.push_back(inpM);
                UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); trueInputs.push_back(inpN);
                UniValue keysArray(UniValue::VARR);
                QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
                for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
                UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); trueInputs.push_back(inpSigs);
            } else if (trueType.contains("hash160")) {
                bool ok1;
                QString hashHex = QInputDialog::getText(this, tr("True Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.isEmpty()) return;
                trueAct.pushKV("role", "hash160");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); trueInputs.push_back(inpHash);
            }
            trueAct.pushKV("inputs", trueInputs);
            trueBranch.push_back(trueAct);
        }

        // 3. Get False Action (Optional)
        QStringList falseActionTypes;
        falseActionTypes << tr("Time-Locked + Signature Check (lock-time + check-signature-verification)")
                         << tr("Signature Check (check-signature-verification)")
                         << tr("Address / PubKeyHash Check (equalverify-checksig)")
                         << tr("Multi-Signature (multi-signature)")
                         << tr("Time-Locked (lock-time)")
                         << tr("Hash-Locked (hash160)")
                         << tr("None");
        QString falseType = getCustomItem(this, tr("IF-Condition - False Branch"), tr("Select action when FALSE:"), falseActionTypes, 0, false, &ok);
        if (!ok || falseType.isEmpty()) return;

        UniValue falseBranch(UniValue::VARR);
        bool hasFalse = false;
        if (falseType.contains("lock-time + check-signature-verification")) {
            int64_t lockTime = QInputDialog::getInt(this, tr("False Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
            if (!ok) return;
            QString pubkey = QInputDialog::getText(this, tr("False Branch - Signature Check"), tr("Refund Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
            if (!ok || pubkey.isEmpty()) return;
            UniValue ltAct(UniValue::VOBJ), ltInputs(UniValue::VARR), ltInp(UniValue::VOBJ);
            ltAct.pushKV("role", "lock-time");
            ltInp.pushKV("name", "Lock-Until"); ltInp.pushKV("type", "timestamp-or-block-height"); ltInp.pushKV("value", lockTime);
            ltInputs.push_back(ltInp); ltAct.pushKV("inputs", ltInputs);
            falseBranch.push_back(ltAct);

            UniValue sigAct(UniValue::VOBJ), sigInputs(UniValue::VARR), sigInp(UniValue::VOBJ);
            sigAct.pushKV("role", "check-signature-verification");
            sigInp.pushKV("name", "Pubkey"); sigInp.pushKV("type", "pubkey"); sigInp.pushKV("value", pubkey.trimmed().toStdString());
            sigInputs.push_back(sigInp); sigAct.pushKV("inputs", sigInputs);
            falseBranch.push_back(sigAct);
            hasFalse = true;
        } else if (falseType != tr("None")) {
            UniValue falseAct(UniValue::VOBJ);
            UniValue falseInputs(UniValue::VARR);
            if (falseType.contains("lock-time")) {
                int64_t lockTime = QInputDialog::getInt(this, tr("False Branch - Time-Locked"), tr("Lock Until (Block or Timestamp):"), defaultLock, 0, 2000000000, 1, &ok);
                if (!ok) return;
                falseAct.pushKV("role", "lock-time");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Lock-Until"); inp.pushKV("type", "timestamp-or-block-height"); inp.pushKV("value", lockTime); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("check-signature-verification")) {
                QString pubkey = QInputDialog::getText(this, tr("False Branch - Signature Check"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok);
                if (!ok || pubkey.isEmpty()) return;
                falseAct.pushKV("role", "check-signature-verification");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "Pubkey"); inp.pushKV("type", "pubkey"); inp.pushKV("value", pubkey.trimmed().toStdString()); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("equalverify-checksig")) {
                QString pkh = QInputDialog::getText(this, tr("False Branch - PubKeyHash Check"), tr("PubKeyHash (40-char Hex):"), QLineEdit::Normal, defaultPubkeyHash, &ok);
                if (!ok || pkh.isEmpty()) return;
                falseAct.pushKV("role", "equalverify-checksig");
                UniValue inp(UniValue::VOBJ); inp.pushKV("name", "PubKeyHash"); inp.pushKV("type", "string"); inp.pushKV("value", pkh.trimmed().toStdString()); falseInputs.push_back(inp);
                hasFalse = true;
            } else if (falseType.contains("multi-signature")) {
                bool ok1, ok2, ok3;
                int m = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Required Signatures (m):"), 2, 1, 20, 1, &ok1);
                if (!ok1) return;
                int n = QInputDialog::getInt(this, tr("False Branch - Multi-Signature"), tr("Total Keys (n):"), 3, 1, 20, 1, &ok2);
                if (!ok2) return;
                QString keysStr = QInputDialog::getText(this, tr("False Branch - Multi-Signature"), tr("Public Keys (space-separated):"), QLineEdit::Normal, defaultPubkey, &ok3);
                if (!ok3 || keysStr.isEmpty()) return;
                falseAct.pushKV("role", "multi-signature");
                UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", m); falseInputs.push_back(inpM);
                UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", n); falseInputs.push_back(inpN);
                UniValue keysArray(UniValue::VARR);
                QStringList keysList = keysStr.split(' ', QString::SkipEmptyParts);
                for (const QString& key : keysList) keysArray.push_back(key.trimmed().toStdString());
                UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", keysArray); falseInputs.push_back(inpSigs);
                hasFalse = true;
            } else if (falseType.contains("hash160")) {
                bool ok1;
                QString hashHex = QInputDialog::getText(this, tr("False Branch - Hash-Locked"), tr("Hash160 of Preimage (Hex):"), QLineEdit::Normal, "", &ok1);
                if (!ok1 || hashHex.isEmpty()) return;
                falseAct.pushKV("role", "hash160");
                UniValue inpHash(UniValue::VOBJ); inpHash.pushKV("name", "Hash160"); inpHash.pushKV("type", "string-or-number"); inpHash.pushKV("value", hashHex.trimmed().toStdString()); falseInputs.push_back(inpHash);
                hasFalse = true;
            }
            if (hasFalse) {
                falseAct.pushKV("inputs", falseInputs);
                falseBranch.push_back(falseAct);
            }
        }

        UniValue condNode(UniValue::VOBJ);
        condNode.pushKV("role", "if-condition");
        condNode.pushKV("expression", exprAct);
        condNode.pushKV("true_action", trueBranch);
        if (hasFalse) {
            condNode.pushKV("false_action", falseBranch);
        }

        customActions.push_back(condNode);
        updateCustomTree();
        generateContract();
        return;
    }
    else if (item.contains("coin-lock-miner")) {
        bool ok1, ok2, ok3;
        int64_t lockHeight = QInputDialog::getInt(this, tr("Coin-Lock Miner"), tr("Lock Until Block Height:"), defaultLock, 0, 2000000000, 1, &ok1);
        if (!ok1) return;
        QString ownerPubkey = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || ownerPubkey.isEmpty()) return;
        QString minerPubkey = QInputDialog::getText(this, tr("Coin-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || minerPubkey.isEmpty()) return;

        node.pushKV("role", "coin-lock-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "LockHeight"); inp1.pushKV("type", "timestamp-or-block-height"); inp1.pushKV("value", lockHeight); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "OwnerPubKey"); inp2.pushKV("type", "pubkey"); inp2.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "MinerPubKey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", minerPubkey.trimmed().toStdString()); inputs.push_back(inp3);
    }
    else if (item.contains("pow-miner")) {
        bool ok1, ok2, ok3, ok4, ok5;
        int64_t lockHeight = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("Lock Until Block Height:"), defaultLock, 0, 2000000000, 1, &ok1);
        if (!ok1) return;
        QString ownerPubkey = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Owner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok2);
        if (!ok2 || ownerPubkey.isEmpty()) return;
        QString minerPubkey = QInputDialog::getText(this, tr("PoW-Lock Miner"), tr("Miner Public Key (Hex):"), QLineEdit::Normal, defaultPubkey, &ok3);
        if (!ok3 || minerPubkey.isEmpty()) return;
        int nonce = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("PoW Nonce:"), 0, 0, 2147483647, 1, &ok4);
        if (!ok4) return;
        int targetBits = QInputDialog::getInt(this, tr("PoW-Lock Miner"), tr("Target Difficulty Bits (1-64):"), 16, 1, 64, 1, &ok5);
        if (!ok5) return;

        node.pushKV("role", "pow-miner");
        UniValue inp1(UniValue::VOBJ); inp1.pushKV("name", "LockHeight"); inp1.pushKV("type", "timestamp-or-block-height"); inp1.pushKV("value", lockHeight); inputs.push_back(inp1);
        UniValue inp2(UniValue::VOBJ); inp2.pushKV("name", "OwnerPubKey"); inp2.pushKV("type", "pubkey"); inp2.pushKV("value", ownerPubkey.trimmed().toStdString()); inputs.push_back(inp2);
        UniValue inp3(UniValue::VOBJ); inp3.pushKV("name", "MinerPubKey"); inp3.pushKV("type", "pubkey"); inp3.pushKV("value", minerPubkey.trimmed().toStdString()); inputs.push_back(inp3);
        UniValue inp4(UniValue::VOBJ); inp4.pushKV("name", "PoWNonce"); inp4.pushKV("type", "number"); inp4.pushKV("value", nonce); inputs.push_back(inp4);
        UniValue inp5(UniValue::VOBJ); inp5.pushKV("name", "TargetDifficultyBits"); inp5.pushKV("type", "number"); inp5.pushKV("value", targetBits); inputs.push_back(inp5);
    }
    else if (item.contains("number")) {
        int val = QInputDialog::getInt(this, tr("Push Number / True"), tr("Integer Value (-1 to 16):"), 1, -1, 16, 1, &ok);
        if (!ok) return;

        node.pushKV("role", "number");
        UniValue inp(UniValue::VOBJ);
        inp.pushKV("name", "Value");
        inp.pushKV("type", "number");
        inp.pushKV("value", val);
        inputs.push_back(inp);
    }

    node.pushKV("inputs", inputs);
    customActions.push_back(node);

    updateCustomTree();
    generateContract();
}

void SmartContractWidget::onEditAction(QTreeWidgetItem* item, int /*column*/)
{
    if (!item) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(item);
    if (index < 0 || index >= (int)customActions.size()) return;

    UniValue act = customActions[index];
    if (!act.isObject() || !act.exists("role")) return;
    std::string role = act["role"].get_str();

    if (act.exists("inputs") && act["inputs"].isArray() && !act["inputs"].empty()) {
        UniValue newInputs(UniValue::VARR);
        const UniValue& oldInputs = act["inputs"];
        for (unsigned int j = 0; j < oldInputs.size(); ++j) {
            UniValue inp = oldInputs[j];
            if (!inp.isObject() || !inp.exists("name")) {
                newInputs.push_back(inp);
                continue;
            }
            QString name = QString::fromStdString(inp["name"].get_str());
            std::string type = (inp.exists("type") && inp["type"].isStr()) ? inp["type"].get_str() : "string";
            bool ok = false;

            UniValue updatedInp(UniValue::VOBJ);
            updatedInp.pushKV("name", inp["name"]);
            updatedInp.pushKV("type", type);

            if (inp.exists("value") && inp["value"].isArray()) {
                QStringList currentList;
                for (unsigned int k = 0; k < inp["value"].size(); ++k) {
                    currentList << QString::fromStdString(inp["value"][k].getValStr());
                }
                QString edited = QInputDialog::getText(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1 (space-separated):").arg(name), QLineEdit::Normal, currentList.join(" "), &ok);
                if (!ok) return;
                UniValue arr(UniValue::VARR);
                for (const QString& part : edited.split(' ', QString::SkipEmptyParts)) {
                    arr.push_back(part.trimmed().toStdString());
                }
                updatedInp.pushKV("value", arr);
            } else if (type == "number" || type == "timestamp-or-block-height" || (inp.exists("value") && inp["value"].isNum())) {
                int curVal = inp.exists("value") ? (inp["value"].isNum() ? inp["value"].get_int() : atoi(inp["value"].getValStr().c_str())) : 0;
                int newVal = QInputDialog::getInt(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1:").arg(name), curVal, -1, 2000000000, 1, &ok);
                if (!ok) return;
                updatedInp.pushKV("value", newVal);
            } else {
                QString curVal = inp.exists("value") ? QString::fromStdString(inp["value"].getValStr()) : "";
                QString newVal = QInputDialog::getText(this, tr("Edit %1").arg(QString::fromStdString(role)),
                    tr("%1:").arg(name), QLineEdit::Normal, curVal, &ok);
                if (!ok) return;
                updatedInp.pushKV("value", newVal.trimmed().toStdString());
            }
            newInputs.push_back(updatedInp);
        }

        UniValue updatedAct(UniValue::VOBJ);
        updatedAct.pushKV("role", role);
        updatedAct.pushKV("inputs", newInputs);

        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < customActions.size(); ++i) {
            newActions.push_back((int)i == index ? updatedAct : customActions[i]);
        }
        customActions = newActions;
        updateCustomTree();
        generateContract();
    } else {
        QMessageBox::information(this, tr("Edit Step"), tr("To modify an IF-Condition branch structure, you can edit it directly in Developer Mode (YAML) or replace the IF-Condition step."));
    }
}

void SmartContractWidget::onRemoveActionClicked()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) {
        QMessageBox::warning(this, tr("Selection Required"), tr("Please select a step from the tree to remove."));
        return;
    }
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (unsigned int i = 0; i < customActions.size(); ++i) {
            if ((int)i != index) {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        generateContract();
    }
}

void SmartContractWidget::onMoveActionUp()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index > 0 && index < (int)customActions.size()) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index - 1) {
                newActions.push_back(customActions[index]);
            } else if (i == index) {
                newActions.push_back(customActions[index - 1]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index - 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onMoveActionDown()
{
    QTreeWidgetItem* current = ui->treeCustomActions->currentItem();
    if (!current) return;
    int index = ui->treeCustomActions->indexOfTopLevelItem(current);
    if (index >= 0 && index < (int)customActions.size() - 1) {
        UniValue newActions(UniValue::VARR);
        for (int i = 0; i < (int)customActions.size(); ++i) {
            if (i == index) {
                newActions.push_back(customActions[index + 1]);
            } else if (i == index + 1) {
                newActions.push_back(customActions[index]);
            } else {
                newActions.push_back(customActions[i]);
            }
        }
        customActions = newActions;
        updateCustomTree();
        
        QTreeWidgetItem* newCurrent = ui->treeCustomActions->topLevelItem(index + 1);
        ui->treeCustomActions->setCurrentItem(newCurrent);
        
        generateContract();
    }
}

void SmartContractWidget::onOpenVisualDesignerClicked()
{
    try {
        VisualDesignerDialog dialog(customActions, this);
        if (dialog.exec() == QDialog::Accepted) {
            customActions = dialog.getActions();
            updateCustomTree();
            generateContract();
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Visual Designer Error"), tr("An error occurred in Visual Designer: %1").arg(e.what()));
    } catch (...) {
        QMessageBox::critical(this, tr("Visual Designer Error"), tr("An unknown error occurred in Visual Designer."));
    }
}

void SmartContractWidget::onDeveloperModeToggled(bool checked)
{
    ui->textEditYAML->setReadOnly(!checked);
    if (checked) {
        ui->textEditYAML->setStyleSheet("background-color: #1e1e2f; color: #f8f8f2; font-family: monospace;");
    } else {
        ui->textEditYAML->setStyleSheet("");
        generateContract();
    }
}

// Simple YAML Node and Parser implementation
struct YamlNode {
    int lineNum;
    int indent;
    std::string key;
    std::string value;
    bool isListItem;
    bool isQuoted{false};
    std::vector<YamlNode> children;
};

static bool ParseYamlLines(const std::string& yamlStr, std::vector<YamlNode>& rootNodes, std::string& /*error*/) {
    std::vector<std::string> lines;
    {
        std::string currentLine;
        for (char c : yamlStr) {
            if (c == '\n' || c == '\r') {
                if (!currentLine.empty() || c == '\n') {
                    lines.push_back(currentLine);
                    currentLine.clear();
                }
            } else {
                currentLine.push_back(c);
            }
        }
        if (!currentLine.empty()) {
            lines.push_back(currentLine);
        }
    }

    struct StackItem {
        int indent;
        YamlNode* node;
    };
    std::vector<StackItem> stack;

    for (size_t lineIdx = 0; lineIdx < lines.size(); ++lineIdx) {
        std::string line = lines[lineIdx];
        
        int indent = 0;
        while (indent < (int)line.size() && line[indent] == ' ') {
            indent++;
        }
        
        std::string content = line.substr(indent);
        size_t hashPos = content.find('#');
        if (hashPos != std::string::npos) {
            content = content.substr(0, hashPos);
        }
        
        while (!content.empty() && (content.back() == ' ' || content.back() == '\t')) {
            content.pop_back();
        }
        
        if (content.empty()) {
            continue;
        }

        YamlNode node;
        node.lineNum = lineIdx + 1;
        node.indent = indent;
        node.isListItem = false;
        node.isQuoted = false;

        if (content[0] == '-') {
            node.isListItem = true;
            content = content.substr(1);
            size_t start = 0;
            while (start < content.size() && (content[start] == ' ' || content[start] == '\t')) {
                start++;
            }
            content = content.substr(start);
        }

        size_t colonPos = content.find(':');
        if (colonPos != std::string::npos) {
            node.key = content.substr(0, colonPos);
            while (!node.key.empty() && (node.key.back() == ' ' || node.key.back() == '\t')) {
                node.key.pop_back();
            }
            std::string valPart = content.substr(colonPos + 1);
            size_t valStart = 0;
            while (valStart < valPart.size() && (valPart[valStart] == ' ' || valPart[valStart] == '\t')) {
                valStart++;
            }
            node.value = valPart.substr(valStart);
        } else {
            node.value = content;
        }

        if (node.value.size() >= 2 &&
            ((node.value.front() == '"' && node.value.back() == '"') ||
             (node.value.front() == '\'' && node.value.back() == '\''))) {
            node.value = node.value.substr(1, node.value.size() - 2);
            node.isQuoted = true;
        }

        // If this is an inline `- key: value` list item, convert it into a list item with a child `key: value`
        if (node.isListItem && !node.key.empty()) {
            YamlNode firstProp;
            firstProp.lineNum = node.lineNum;
            firstProp.indent = indent + 2;
            firstProp.isListItem = false;
            firstProp.key = node.key;
            firstProp.value = node.value;
            firstProp.isQuoted = node.isQuoted;
            node.key.clear();
            node.value.clear();
            node.isQuoted = false;
            node.children.push_back(firstProp);
        }

        while (!stack.empty() && stack.back().indent >= indent) {
            stack.pop_back();
        }

        if (stack.empty()) {
            rootNodes.push_back(node);
            stack.push_back({indent, &rootNodes.back()});
        } else {
            stack.back().node->children.push_back(node);
            stack.push_back({indent, &stack.back().node->children.back()});
        }
    }
    return true;
}

static UniValue ParsePrimitive(const std::string& val, bool isQuoted = false) {
    if (isQuoted) return UniValue(val);
    if (val == "true") return UniValue(true);
    if (val == "false") return UniValue(false);
    
    if (!val.empty()) {
        bool isNum = true;
        bool hasDot = false;
        size_t start = 0;
        if (val[0] == '-') start = 1;
        if (start >= val.size()) isNum = false;
        for (size_t i = start; i < val.size(); ++i) {
            if (val[i] == '.') {
                if (hasDot) { isNum = false; break; }
                hasDot = true;
            } else if (!std::isdigit(static_cast<unsigned char>(val[i]))) {
                isNum = false;
                break;
            }
        }
        if (isNum) {
            try {
                if (hasDot) {
                    return UniValue(std::stod(val));
                } else {
                    return UniValue((int64_t)std::stoll(val));
                }
            } catch (...) {}
        }
    }
    return UniValue(val);
}

static UniValue ConvertNodeChildrenToUniValue(const std::vector<YamlNode>& children, std::string& error) {
    if (children.empty()) {
        return UniValue("");
    }

    bool isArray = false;
    for (const auto& child : children) {
        if (child.isListItem) {
            isArray = true;
            break;
        }
    }

    if (isArray) {
        UniValue arr(UniValue::VARR);
        for (const auto& child : children) {
            if (!child.children.empty()) {
                arr.push_back(ConvertNodeChildrenToUniValue(child.children, error));
            } else {
                arr.push_back(ParsePrimitive(child.value, child.isQuoted));
            }
        }
        return arr;
    } else {
        UniValue obj(UniValue::VOBJ);
        for (const auto& child : children) {
            if (child.key.empty()) {
                if (children.size() == 1) {
                    return ParsePrimitive(child.value, child.isQuoted);
                }
                continue;
            }
            if (!child.children.empty()) {
                obj.pushKV(child.key, ConvertNodeChildrenToUniValue(child.children, error));
            } else {
                obj.pushKV(child.key, ParsePrimitive(child.value, child.isQuoted));
            }
        }
        return obj;
    }
}

void SmartContractWidget::onYAMLTextChanged()
{
    if (!ui->cbDeveloperMode->isChecked()) {
        return;
    }

    std::string yamlStr = ui->textEditYAML->toPlainText().toStdString();
    std::vector<YamlNode> rootNodes;
    std::string error;
    
    if (!ParseYamlLines(yamlStr, rootNodes, error)) {
        ui->textEditJSON->setText(tr("YAML Parse Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    UniValue doc = ConvertNodeChildrenToUniValue(rootNodes, error);
    if (!error.empty()) {
        ui->textEditJSON->setText(tr("YAML Conversion Error: %1").arg(QString::fromStdString(error)));
        return;
    }

    activeDoc = doc;
    
    if (activeDoc.exists("active_contract") && activeDoc["active_contract"].isStr()) {
        activeName = activeDoc["active_contract"].get_str();
    } else {
        activeName = "CustomContract";
    }

    ui->textEditJSON->setText(QString::fromStdString(activeDoc.write(2)));

    std::string compileErr;
    CScript compiled = CMescal::Compile(activeDoc.write(), compileErr);
    if (!compileErr.empty()) {
        ui->labelStatus->setStyleSheet("color: #ff3d00;");
        ui->labelStatus->setText(tr("✗ Status: Compilation Error — %1").arg(QString::fromStdString(compileErr)));
    } else {
        ui->labelStatus->setStyleSheet("color: #26a69a;");
        ui->labelStatus->setText(tr("✓ Status: Valid MESCAL Contract (%1 bytes)").arg(compiled.size()));
    }
}

void SmartContractWidget::updateCustomTree()
{
    ui->treeCustomActions->clear();
    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        std::string role = act.exists("role") ? act["role"].get_str() : "unknown";
        
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->treeCustomActions);
        
        if (role == "if-condition") {
            item->setText(0, QString("Step %1: IF-Condition").arg(i + 1));
            
            QString details = "IF [";
            UniValue expr = act.exists("expression") ? act["expression"] : (act.exists("expressions") ? act["expressions"] : UniValue());
            if (expr.isArray()) {
                QStringList subDetails;
                for (unsigned int k = 0; k < expr.size(); ++k) {
                    if (expr[k].isObject() && expr[k].exists("role"))
                        subDetails << QString::fromStdString(expr[k]["role"].get_str());
                }
                details += subDetails.join(" AND ") + "] THEN [";
            } else {
                details += QString::fromStdString(expr.exists("role") ? expr["role"].get_str() : "") + "] THEN [";
            }
            
            UniValue trueAct = act.exists("true_action") ? act["true_action"] : (act.exists("true") ? act["true"] : UniValue());
            if (trueAct.isArray()) {
                QStringList subDetails;
                for (unsigned int k = 0; k < trueAct.size(); ++k) {
                    if (trueAct[k].isObject() && trueAct[k].exists("role"))
                        subDetails << QString::fromStdString(trueAct[k]["role"].get_str());
                }
                details += subDetails.join(", ") + "]";
            } else {
                details += QString::fromStdString(trueAct.exists("role") ? trueAct["role"].get_str() : "") + "]";
            }
            
            UniValue falseAct = act.exists("false_action") ? act["false_action"] : (act.exists("false") ? act["false"] : UniValue());
            if (!falseAct.isNull()) {
                if (falseAct.isArray() && !falseAct.empty()) {
                    QStringList subDetails;
                    for (unsigned int k = 0; k < falseAct.size(); ++k) {
                        if (falseAct[k].isObject() && falseAct[k].exists("role"))
                            subDetails << QString::fromStdString(falseAct[k]["role"].get_str());
                    }
                    details += " ELSE [" + subDetails.join(", ") + "]";
                } else if (falseAct.isObject() && falseAct.exists("role")) {
                    details += " ELSE [" + QString::fromStdString(falseAct["role"].get_str()) + "]";
                }
            }
            item->setText(1, details);
        } else {
            item->setText(0, QString("Step %1: %2").arg(i + 1).arg(QString::fromStdString(role)));
            
            QString details = "";
            const UniValue& inputs = act["inputs"];
            if (inputs.isArray()) {
                for (unsigned int j = 0; j < inputs.size(); ++j) {
                    if (!inputs[j].isObject()) continue;
                    QString inpName = inputs[j].exists("name") ? QString::fromStdString(inputs[j]["name"].get_str()) : "";
                    QString inpVal;
                    if (inputs[j].exists("value")) {
                        const UniValue& v = inputs[j]["value"];
                        if (v.isArray()) {
                            inpVal = QString("[%1 keys]").arg(v.size());
                        } else {
                            inpVal = QString::fromStdString(v.getValStr());
                        }
                    }
                    details += QString("%1: %2 | ").arg(inpName).arg(inpVal);
                }
                if (details.endsWith(" | ")) {
                    details.chop(3);
                }
            }
            item->setText(1, details);
        }
    }
}

void SmartContractWidget::buildContractFromCustom()
{
    activeName = "CustomContract";
    UniValue doc(UniValue::VOBJ);
    UniValue basic(UniValue::VOBJ);
    UniValue conditions(UniValue::VOBJ);
    UniValue contract(UniValue::VOBJ);
    UniValue actions(UniValue::VARR);

    int stepIndex = 1;
    int condIndex = 1;

    for (unsigned int i = 0; i < customActions.size(); ++i) {
        const UniValue& act = customActions[i];
        std::string role = act["role"].get_str();

        if (role == "if-condition") {
            std::string condName = "Cond-" + std::to_string(condIndex++);
            UniValue condSpec(UniValue::VOBJ);
            condSpec.pushKV("role", "if-condition");

            // 1. Expression Action(s)
            UniValue exprRefs(UniValue::VARR);
            UniValue exprValue = act.exists("expression") ? act["expression"] : (act.exists("expressions") ? act["expressions"] : UniValue());
            if (exprValue.isArray()) {
                for (unsigned int k = 0; k < exprValue.size(); ++k) {
                    const UniValue& exprAct = exprValue[k];
                    if (exprAct.isObject()) {
                        std::string exprStep = "Step-" + std::to_string(stepIndex++);
                        UniValue exprSpec(UniValue::VOBJ);
                        exprSpec.pushKV("role", exprAct["role"].get_str());
                        exprSpec.pushKV("inputs", exprAct["inputs"]);
                        basic.pushKV(exprStep, exprSpec);

                        UniValue exprRef(UniValue::VOBJ);
                        exprRef.pushKV("type", "basic");
                        exprRef.pushKV("name", exprStep);
                        exprRefs.push_back(exprRef);
                    }
                }
            } else if (exprValue.isObject()) {
                std::string exprStep = "Step-" + std::to_string(stepIndex++);
                UniValue exprSpec(UniValue::VOBJ);
                exprSpec.pushKV("role", exprValue["role"].get_str());
                exprSpec.pushKV("inputs", exprValue["inputs"]);
                basic.pushKV(exprStep, exprSpec);

                UniValue exprRef(UniValue::VOBJ);
                exprRef.pushKV("type", "basic");
                exprRef.pushKV("name", exprStep);
                exprRefs.push_back(exprRef);
            }
            condSpec.pushKV("expressions", exprRefs);

            // 2. True Action(s)
            UniValue trueRefs(UniValue::VARR);
            UniValue trueValue = act.exists("true_action") ? act["true_action"] : (act.exists("true") ? act["true"] : UniValue());
            if (trueValue.isArray()) {
                for (unsigned int k = 0; k < trueValue.size(); ++k) {
                    const UniValue& trueAct = trueValue[k];
                    if (trueAct.isObject()) {
                        std::string trueStep = "Step-" + std::to_string(stepIndex++);
                        UniValue trueSpec(UniValue::VOBJ);
                        trueSpec.pushKV("role", trueAct["role"].get_str());
                        trueSpec.pushKV("inputs", trueAct["inputs"]);
                        basic.pushKV(trueStep, trueSpec);

                        UniValue trueRef(UniValue::VOBJ);
                        trueRef.pushKV("type", "basic");
                        trueRef.pushKV("name", trueStep);
                        trueRefs.push_back(trueRef);
                    }
                }
            } else if (trueValue.isObject()) {
                std::string trueStep = "Step-" + std::to_string(stepIndex++);
                UniValue trueSpec(UniValue::VOBJ);
                trueSpec.pushKV("role", trueValue["role"].get_str());
                trueSpec.pushKV("inputs", trueValue["inputs"]);
                basic.pushKV(trueStep, trueSpec);

                UniValue trueRef(UniValue::VOBJ);
                trueRef.pushKV("type", "basic");
                trueRef.pushKV("name", trueStep);
                trueRefs.push_back(trueRef);
            }
            condSpec.pushKV("true", trueRefs);

            // 3. False Action(s) (Optional)
            UniValue falseValue = act.exists("false_action") ? act["false_action"] : (act.exists("false") ? act["false"] : UniValue());
            if (!falseValue.isNull()) {
                UniValue falseRefs(UniValue::VARR);
                if (falseValue.isArray()) {
                    for (unsigned int k = 0; k < falseValue.size(); ++k) {
                        const UniValue& falseAct = falseValue[k];
                        if (falseAct.isObject()) {
                            std::string falseStep = "Step-" + std::to_string(stepIndex++);
                            UniValue falseSpec(UniValue::VOBJ);
                            falseSpec.pushKV("role", falseAct["role"].get_str());
                            falseSpec.pushKV("inputs", falseAct["inputs"]);
                            basic.pushKV(falseStep, falseSpec);

                            UniValue falseRef(UniValue::VOBJ);
                            falseRef.pushKV("type", "basic");
                            falseRef.pushKV("name", falseStep);
                            falseRefs.push_back(falseRef);
                        }
                    }
                    if (!falseRefs.empty()) {
                        condSpec.pushKV("false", falseRefs);
                    }
                } else if (falseValue.isObject()) {
                    std::string falseStep = "Step-" + std::to_string(stepIndex++);
                    UniValue falseSpec(UniValue::VOBJ);
                    falseSpec.pushKV("role", falseValue["role"].get_str());
                    falseSpec.pushKV("inputs", falseValue["inputs"]);
                    basic.pushKV(falseStep, falseSpec);

                    UniValue falseRef(UniValue::VOBJ);
                    falseRef.pushKV("type", "basic");
                    falseRef.pushKV("name", falseStep);
                    falseRefs.push_back(falseRef);
                    condSpec.pushKV("false", falseRefs);
                }
            }

            conditions.pushKV(condName, condSpec);

            UniValue condRef(UniValue::VOBJ);
            condRef.pushKV("type", "condition");
            condRef.pushKV("name", condName);
            actions.push_back(condRef);
        } else {
            std::string stepName = "Step-" + std::to_string(stepIndex++);
            UniValue stepSpec(UniValue::VOBJ);
            stepSpec.pushKV("role", act["role"].get_str());
            stepSpec.pushKV("inputs", act["inputs"]);
            basic.pushKV(stepName, stepSpec);

            UniValue actRef(UniValue::VOBJ);
            actRef.pushKV("type", "basic");
            actRef.pushKV("name", stepName);
            actions.push_back(actRef);
        }
    }

    UniValue cSpec(UniValue::VOBJ);
    cSpec.pushKV("description", "Custom smart contract built using Visual/Custom Builder.");
    cSpec.pushKV("actions", actions);
    contract.pushKV(activeName, cSpec);

    doc.pushKV("basic", basic);
    if (!conditions.empty()) {
        doc.pushKV("condition", conditions);
    }
    doc.pushKV("contract", contract);
    doc.pushKV("active_contract", activeName);

    activeDoc = doc;
    updatePreviews();
}
