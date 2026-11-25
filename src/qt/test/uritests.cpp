// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uritests.h"

#include "chainparams.h"
#include "guiutil.h"
#include "key.h"
#include "key_io.h"
#include "main.h"
#include "masternode.h"
#include "adam.h"
#include "pubkey.h"
#include "random.h"
#include "transactionrecord.h"
#include "wallet/wallet.h"
#include "walletmodel.h"
#include "psbt.h"
#include "script/mescal.h"
#include "utilitydialog.h"
#include "qt/kristatech/masternodewizarddialog.h"
#include "qt/kristatech/qtutils.h"
#include "qt/kristatech/visualdesignerdialog.h"
#include "optionsmodel.h"
#include <univalue.h>

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>

void URITests::uriTests()
{
    SendCoinsRecipient rv;
    QUrl uri;
    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?req-dontexist="));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?dontexist="));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?label=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString("Some Example Address"));
    QVERIFY(rv.amount == 0);

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?amount=0.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100000);

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?amount=1.001"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString());
    QVERIFY(rv.amount == 100100000);

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?amount=100&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.amount == 10000000000LL);
    QVERIFY(rv.label == QString("Some Example"));

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString());

    QVERIFY(GUIUtil::parseBitcoinURI("kristatech://D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?message=Some Example Address", &rv));
    QVERIFY(rv.address == QString("D72dLgywmL73JyTwQBfuU29CADz9yCJ99v"));
    QVERIFY(rv.label == QString());

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?req-message=Some Example Address"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?amount=1,000&label=Some Example"));
    QVERIFY(GUIUtil::parseBitcoinURI(uri, &rv));

    uri.setUrl(QString("kristatech:D72dLgywmL73JyTwQBfuU29CADz9yCJ99v?amount=1,000.0&label=Some Example"));
    QVERIFY(!GUIUtil::parseBitcoinURI(uri, &rv));
}

void URITests::transactionRecordTests()
{
    RandomInit();
    ECC_Start();
    ECCVerifyHandle verifyHandle;
    SelectParams(CBaseChainParams::MAIN);

    CWallet wallet;
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.AddKeyPubKey(key, pubkey));
    }

    // 1. Test MinerRegistration decomposition (Coin-Lock registration output)
    CScript coinLockScript = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                       << OP_DROP
                                       << CScriptNum(5000) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                       << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.AddWatchOnly(coinLockScript));
    }

    CMutableTransaction mtxReg;
    mtxReg.vin.push_back(CTxIn(COutPoint(uint256S("1111"), 0)));
    mtxReg.vout.push_back(CTxOut(MINER_REGISTRATION_LOCK_AMOUNT, coinLockScript));
    CWalletTx wtxReg(&wallet, CTransaction(mtxReg));

    QList<TransactionRecord> regRecords = TransactionRecord::decomposeTransaction(&wallet, wtxReg);
    QCOMPARE(regRecords.size(), 1);
    QCOMPARE((int)regRecords[0].type, (int)TransactionRecord::MinerRegistration);
    QCOMPARE(regRecords[0].credit, MINER_REGISTRATION_LOCK_AMOUNT);
    QCOMPARE(QString::fromStdString(regRecords[0].address), QString::fromStdString(EncodeDestination(pubkey.GetID())));

    // 2. Test Balanced Model D reward classification (AdamReward 4%, MNReward 5%, LlmqReward 10%/N, StakeMint 22%/25%) in CoinStake
    const int nTestHeight = 50000;
    CAmount nBlockValActual = CMasternode::GetBlockValue(nTestHeight);
    CAmount nTreasurySplit = nBlockValActual * 7 / 100;
    CAmount nBlockVal = nBlockValActual - nTreasurySplit;
    CAmount nMNSplit = nBlockVal * 5 / 100;
    CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
    CAmount llmqBase = nLLMQSplitTotal / 5;
    CAmount nParticipantSplit = nBlockVal * 60 / 100;
    CAmount adamSeatShare = nParticipantSplit / 15; // 4.00% equal share across 15 seats

    CScript p2pkhScript = GetScriptForDestination(pubkey.GetID());
    CMutableTransaction mtxStake;
    mtxStake.vin.push_back(CTxIn(COutPoint(uint256S("2222"), 0)));
    mtxStake.vout.push_back(CTxOut(0, CScript())); // Empty vout[0] marks coinstake
    mtxStake.vout.push_back(CTxOut(adamSeatShare, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(adamSeatShare, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(adamSeatShare, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(nMNSplit, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(llmqBase, p2pkhScript));

    CWalletTx wtxStake(&wallet, CTransaction(mtxStake));
    uint256 fakeBlockHash = uint256S("3333333333333333333333333333333333333333333333333333333333333333");
    wtxStake.hashBlock = fakeBlockHash;

    // 2a. Simulate -resync startup where mapBlockIndex does not yet contain fakeBlockHash
    QList<TransactionRecord> resyncRecords = TransactionRecord::decomposeTransaction(&wallet, wtxStake);
    QCOMPARE(resyncRecords.size(), 5);
    QCOMPARE((int)resyncRecords[0].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[1].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[2].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[3].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[4].type, (int)TransactionRecord::MNReward);

    CBlockIndex fakeBlockIndex;
    fakeBlockIndex.nHeight = nTestHeight;
    {
        LOCK(cs_main);
        mapBlockIndex[fakeBlockHash] = &fakeBlockIndex;
        for (TransactionRecord& rec : resyncRecords) {
            rec.updateStatus(wtxStake);
        }
    }
    QCOMPARE((int)resyncRecords[0].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)resyncRecords[1].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)resyncRecords[2].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)resyncRecords[3].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[4].type, (int)TransactionRecord::LlmqReward);

    QList<TransactionRecord> stakeRecords = TransactionRecord::decomposeTransaction(&wallet, wtxStake);
    QCOMPARE(stakeRecords.size(), 5);
    QCOMPARE((int)stakeRecords[0].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)stakeRecords[1].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)stakeRecords[2].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)stakeRecords[3].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)stakeRecords[4].type, (int)TransactionRecord::LlmqReward);

    // 4a. Staker CoinStake with co-owned MN + LLMQ + ADAM payouts must NOT double-count into StakeMint
    CAmount producerShare = nBlockVal * 25 / 100;
    CMutableTransaction mtxFund;
    mtxFund.vin.push_back(CTxIn(COutPoint(uint256S("aaaa"), 0)));
    mtxFund.vout.push_back(CTxOut(100 * COIN, p2pkhScript));
    CWalletTx wtxFund(&wallet, CTransaction(mtxFund));
    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.LoadToWallet(wtxFund));
    }

    CMutableTransaction mtxStaker;
    mtxStaker.vin.push_back(CTxIn(COutPoint(wtxFund.GetHash(), 0)));
    mtxStaker.vout.push_back(CTxOut(0, CScript()));
    mtxStaker.vout.push_back(CTxOut(100 * COIN + producerShare, p2pkhScript));
    mtxStaker.vout.push_back(CTxOut(nMNSplit, p2pkhScript));
    mtxStaker.vout.push_back(CTxOut(llmqBase, p2pkhScript));
    mtxStaker.vout.push_back(CTxOut(adamSeatShare, p2pkhScript));
    CWalletTx wtxStaker(&wallet, CTransaction(mtxStaker));
    wtxStaker.hashBlock = fakeBlockHash;

    // 4b. Split-output Staker CoinStake must merge split stake outputs for StakeMint and still separate MNReward
    CMutableTransaction mtxFundSplit;
    mtxFundSplit.vin.push_back(CTxIn(COutPoint(uint256S("bbbb"), 0)));
    mtxFundSplit.vout.push_back(CTxOut(2000 * COIN, p2pkhScript));
    CWalletTx wtxFundSplit(&wallet, CTransaction(mtxFundSplit));
    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.LoadToWallet(wtxFundSplit));
    }

    CAmount totalSplitReturn = 2000 * COIN + producerShare;
    CAmount halfSplit = totalSplitReturn / 2;
    CMutableTransaction mtxSplit;
    mtxSplit.vin.push_back(CTxIn(COutPoint(wtxFundSplit.GetHash(), 0)));
    mtxSplit.vout.push_back(CTxOut(0, CScript()));
    mtxSplit.vout.push_back(CTxOut(halfSplit, p2pkhScript));
    mtxSplit.vout.push_back(CTxOut(totalSplitReturn - halfSplit, p2pkhScript));
    mtxSplit.vout.push_back(CTxOut(nMNSplit, p2pkhScript));
    CWalletTx wtxSplit(&wallet, CTransaction(mtxSplit));
    wtxSplit.hashBlock = fakeBlockHash;

    {
        LOCK(cs_main);
        mapBlockIndex[fakeBlockHash] = &fakeBlockIndex;
        QList<TransactionRecord> stakerRecords = TransactionRecord::decomposeTransaction(&wallet, wtxStaker);
        QCOMPARE(stakerRecords.size(), 4);
        QCOMPARE((int)stakerRecords[0].type, (int)TransactionRecord::StakeMint);
        QCOMPARE(stakerRecords[0].credit, producerShare);
        QCOMPARE((int)stakerRecords[1].type, (int)TransactionRecord::MNReward);
        QCOMPARE(stakerRecords[1].credit, nMNSplit);
        QCOMPARE((int)stakerRecords[2].type, (int)TransactionRecord::LlmqReward);
        QCOMPARE(stakerRecords[2].credit, llmqBase);
        QCOMPARE((int)stakerRecords[3].type, (int)TransactionRecord::AdamReward);
        QCOMPARE(stakerRecords[3].credit, adamSeatShare);

        QList<TransactionRecord> splitRecords = TransactionRecord::decomposeTransaction(&wallet, wtxSplit);
        QCOMPARE(splitRecords.size(), 2);
        QCOMPARE((int)splitRecords[0].type, (int)TransactionRecord::StakeMint);
        QCOMPARE(splitRecords[0].credit, producerShare);
        QCOMPARE((int)splitRecords[1].type, (int)TransactionRecord::MNReward);
        QCOMPARE(splitRecords[1].credit, nMNSplit);

        mapBlockIndex.erase(fakeBlockHash);
    }

    // 3. Test TransactionRecord::statusUpdateNeeded() fast-path for already-Confirmed records
    {
        LOCK(cs_main);
        CBlockIndex idx0, idx10, idx15;
        idx0.nHeight = 0;
        idx10.nHeight = 10;
        idx10.pprev = &idx0;
        idx15.nHeight = 15;
        idx15.pprev = &idx10;

        chainActive.SetTip(&idx10);
        QCOMPARE(chainActive.Height(), 10);

        TransactionRecord confirmedRec(uint256S("4444"), 1700000000, 250);
        confirmedRec.status.status = TransactionStatus::Confirmed;
        confirmedRec.status.cur_num_blocks = 10;
        confirmedRec.status.depth = 20;

        // Same block height -> no update needed
        QVERIFY(!confirmedRec.statusUpdateNeeded());

        // Advance chain to height 15 -> Confirmed record fast-updates depth in O(1) and returns false
        chainActive.SetTip(&idx15);
        QCOMPARE(chainActive.Height(), 15);
        QVERIFY(!confirmedRec.statusUpdateNeeded());
        QCOMPARE(confirmedRec.status.depth, 25);
        QCOMPARE(confirmedRec.status.cur_num_blocks, 15);

        // Unconfirmed record at height 10 must return true when chain advances to 15
        TransactionRecord unconfRec(uint256S("5555"), 1700000000, 250);
        unconfRec.status.status = TransactionStatus::Unconfirmed;
        unconfRec.status.cur_num_blocks = 10;
        unconfRec.status.depth = 0;
        QVERIFY(unconfRec.statusUpdateNeeded());

        // During sync (IsInitialBlockDownload() == true), an un-synced transaction must report Unconfirmed / "Syncing..." instead of Conflicted
        TransactionRecord syncRec(uint256S("6666"), 1700000000, 250, TransactionRecord::MinerRegistration, "", -MINER_REGISTRATION_LOCK_AMOUNT, 0);
        syncRec.updateStatus(wtxReg);
        QCOMPARE((int)syncRec.status.status, (int)TransactionStatus::Unconfirmed);
        QVERIFY(syncRec.status.fSyncing);
        QCOMPARE(QString::fromStdString(syncRec.statusToString()), QString("Syncing..."));

        // Once synced (e.g. recent tip on Regtest where IsInitialBlockDownload() == false), an abandoned tx must report "Abandoned" (not "Syncing...")
        SelectParams(CBaseChainParams::REGTEST);
        idx15.nTime = GetTime();
        idx15.nStatus = BLOCK_VALID_TREE;
        pindexBestHeader = &idx15;
        CWalletTx wtxAbandoned = wtxReg;
        wtxAbandoned.setAbandoned();
        TransactionRecord abandonedRec(uint256S("7777"), 1700000000, 250, TransactionRecord::MinerRegistration, "", 0, 0);
        abandonedRec.updateStatus(wtxAbandoned);
        QVERIFY(!abandonedRec.status.fSyncing);
        QVERIFY(abandonedRec.status.fAbandoned);
        QCOMPARE((int)abandonedRec.status.status, (int)TransactionStatus::Conflicted);
        QCOMPARE(QString::fromStdString(abandonedRec.statusToString()), QString("Abandoned"));
        pindexBestHeader = nullptr;
        SelectParams(CBaseChainParams::MAIN);

        chainActive.SetTip(nullptr);
    }

    ECC_Stop();
}

void URITests::taprootAndPsbtUiTests()
{
    RandomInit();
    ECC_Start();
    ECCVerifyHandle verifyHandle;
    SelectParams(CBaseChainParams::MAIN);

    // 1. Taproot address generation & IsMine check
    CWallet wallet;
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.AddKeyPubKey(key, pubkey));
        WitnessV1Taproot taprootDest{CXOnlyPubKey(pubkey)};
        std::string taprootAddr = EncodeDestination(taprootDest);
        QVERIFY(taprootAddr.rfind("kt1p", 0) == 0);
        CTxDestination decoded = DecodeDestination(taprootAddr);
        QVERIFY(IsValidDestination(decoded));
        CScript tapScript = GetScriptForDestination(decoded);
        QVERIFY(wallet.IsMine(CTxOut(COIN, tapScript)) == ISMINE_WATCH_ONLY);
    }

    // 2. PSBT Base64 encode/decode roundtrip and malformed input rejection
    CMutableTransaction mtx;
    mtx.nVersion = 2;
    mtx.vin.emplace_back(uint256S("1111111111111111111111111111111111111111111111111111111111111111"), 0);
    mtx.vout.emplace_back(10 * COIN, GetScriptForDestination(WitnessV1Taproot(CXOnlyPubKey(pubkey))));
    PartiallySignedTransaction psbt(mtx);
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << psbt;
    std::string ssStr = ss.str();
    std::string b64 = EncodeBase64((const unsigned char*)ssStr.data(), ssStr.size());
    PartiallySignedTransaction decodedPsbt;
    std::string err;
    QVERIFY(DecodeBase64PSBT(decodedPsbt, b64, err));
    QCOMPARE((int)decodedPsbt.tx.vin.size(), 1);
    QCOMPARE((int)decodedPsbt.tx.vout.size(), 1);
    QCOMPARE((int)decodedPsbt.inputs.size(), 1);
    QCOMPARE((int)decodedPsbt.outputs.size(), 1);
    QVERIFY(!DecodeBase64PSBT(decodedPsbt, "not_valid_psbt!!", err));

    // 3. Interactive Command-line options dialog smoke test (when QApplication is available)
    if (qobject_cast<QApplication*>(QCoreApplication::instance())) {
        HelpMessageDialog helpDlg(nullptr, false);
        QLineEdit* searchEdit = helpDlg.findChild<QLineEdit*>();
        QComboBox* categoryCombo = helpDlg.findChild<QComboBox*>();
        QTableWidget* optTable = helpDlg.findChild<QTableWidget*>();
        QVERIFY(searchEdit != nullptr);
        QVERIFY(categoryCombo != nullptr);
        QVERIFY(optTable != nullptr);
        int totalRows = optTable->rowCount();
        QVERIFY(totalRows > 10);
        searchEdit->setText("-rpcauth");
        QVERIFY(optTable->rowCount() >= 1);
        QVERIFY(optTable->rowCount() < totalRows);
    }

    ECC_Stop();
}

void URITests::wizardAndConsolidationTests()
{
    RandomInit();
    ECC_Start();
    ECCVerifyHandle verifyHandle;
    SelectParams(CBaseChainParams::MAIN);

    // 1. MasterNodeWizardDialog step flow & validation tests
    if (qobject_cast<QApplication*>(QCoreApplication::instance())) {
        MasterNodeWizardDialog dlg(nullptr, nullptr);
        QStackedWidget* stack = dlg.findChild<QStackedWidget*>(QStringLiteral("stackedWidget"));
        QPushButton* btnNext = dlg.findChild<QPushButton*>(QStringLiteral("btnNext"));
        QPushButton* btnBack = dlg.findChild<QPushButton*>(QStringLiteral("btnBack"));
        QLineEdit* editName = dlg.findChild<QLineEdit*>(QStringLiteral("lineEditName"));
        QLineEdit* editIp = dlg.findChild<QLineEdit*>(QStringLiteral("lineEditIpAddress"));
        QLineEdit* editPort = dlg.findChild<QLineEdit*>(QStringLiteral("lineEditPort"));

        QVERIFY(stack != nullptr);
        QVERIFY(btnNext != nullptr);
        QVERIFY(btnBack != nullptr);
        QVERIFY(editName != nullptr);
        QVERIFY(editIp != nullptr);
        QVERIFY(editPort != nullptr);

        QCOMPARE(stack->currentIndex(), 0);
        QCOMPARE(btnNext->text(), QStringLiteral("NEXT"));
        QCOMPARE(editPort->text(), QString::number(Params().GetDefaultPort()));

        // Step 1 -> Step 2
        btnNext->click();
        QCOMPARE(stack->currentIndex(), 1);
        QCOMPARE(btnNext->text(), QStringLiteral("NEXT"));

        // Reject empty and whitespace aliases
        editName->setText(QStringLiteral(""));
        btnNext->click();
        QCOMPARE(stack->currentIndex(), 1);

        editName->setText(QStringLiteral("mn 01"));
        btnNext->click();
        QCOMPARE(stack->currentIndex(), 1);

        // Valid unique alias advances to Step 3 with CREATE button
        editName->setText(QStringLiteral("unit_test_mn_99"));
        btnNext->click();
        QCOMPARE(stack->currentIndex(), 2);
        QCOMPARE(btnNext->text(), QStringLiteral("CREATE"));

        // Reject invalid IP and stay on Step 3
        editIp->setText(QStringLiteral("not_an_ip"));
        btnNext->click();
        QCOMPARE(stack->currentIndex(), 2);

        // Back button returns to Step 2 and restores NEXT
        btnBack->click();
        QCOMPARE(stack->currentIndex(), 1);
        QCOMPARE(btnNext->text(), QStringLiteral("NEXT"));
    }

    // 2. WalletModel::previewConsolidation batching test
    CWallet wallet;
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    {
        LOCK(wallet.cs_wallet);
        QVERIFY(wallet.AddKeyPubKey(key, pubkey));
        CScript p2pkh = GetScriptForDestination(pubkey.GetID());

        CBlockIndex tipIndex;
        tipIndex.nHeight = 100;
        tipIndex.pprev = nullptr;
        chainActive.SetTip(&tipIndex);

        for (int i = 0; i < 3; ++i) {
            CMutableTransaction mtx;
            mtx.vin.emplace_back(COutPoint(uint256S(strprintf("000000000000000000000000000000000000000000000000000000000000000%d", i + 1)), 0));
            mtx.vout.emplace_back(10 * COIN, p2pkh);
            CWalletTx wtx(&wallet, CTransaction(mtx));
            wtx.hashBlock = uint256S("0000000000000000000000000000000000000000000000000000000000000abc");
            wtx.nIndex = 0;
            tipIndex.phashBlock = &wtx.hashBlock;
            mapBlockIndex[wtx.hashBlock] = &tipIndex;
            wallet.AddToWallet(wtx);
        }

        OptionsModel optionsModel;
        WalletModel walletModel(&wallet, &optionsModel);
        WalletModel::ConsolidationPlan plan;
        QString err;
        QVERIFY2(walletModel.previewConsolidation(25 * COIN, plan, err), err.toUtf8().constData());
        QCOMPARE(plan.batchCount, 1);
        QCOMPARE(plan.inputCount, 3);
        QCOMPARE(plan.totalInputAmount, 30 * COIN);
        QVERIFY(plan.estimatedFee > 0);

        mapBlockIndex.erase(uint256S("0000000000000000000000000000000000000000000000000000000000000abc"));
        chainActive.SetTip(nullptr);
    }

    ECC_Stop();
}

void URITests::visualDesignerLifecycleTests()
{
    if (!qobject_cast<QApplication*>(QCoreApplication::instance())) {
        return;
    }

    // Build initial 2-block flow matching SmartContractWidget customActions
    UniValue actions(UniValue::VARR);

    UniValue lockNode(UniValue::VOBJ);
    lockNode.pushKV("role", "lock-time");
    UniValue lockInputs(UniValue::VARR);
    UniValue lockInp(UniValue::VOBJ);
    lockInp.pushKV("name", "Lock-Until");
    lockInp.pushKV("type", "timestamp-or-block-height");
    lockInp.pushKV("value", 250000);
    lockInputs.push_back(lockInp);
    lockNode.pushKV("inputs", lockInputs);
    actions.push_back(lockNode);

    UniValue sigNode(UniValue::VOBJ);
    sigNode.pushKV("role", "check-signature-verification");
    UniValue sigInputs(UniValue::VARR);
    UniValue sigInp(UniValue::VOBJ);
    sigInp.pushKV("name", "Pubkey");
    sigInp.pushKV("type", "pubkey");
    sigInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
    sigInputs.push_back(sigInp);
    sigNode.pushKV("inputs", sigInputs);
    actions.push_back(sigNode);

    struct ThemeGuard {
        bool prevLight;
        explicit ThemeGuard(bool prev) : prevLight(prev) {}
        ~ThemeGuard() { setTheme(prevLight); }
    } themeGuard(isLightTheme());

    // 1. Dark theme lifecycle + Block Library card theme integrity
    setTheme(false);
    {
        VisualDesignerDialog dlg(actions, nullptr);
        QCOMPARE((int)dlg.getActions().size(), 2);

        int libCardCount = 0;
        QPushButton* firstLibBtn = nullptr;
        const QList<QPushButton*> buttons = dlg.findChildren<QPushButton*>();
        for (QPushButton* btn : buttons) {
            if (btn->property("vdLibCard").toBool()) {
                ++libCardCount;
                QVERIFY(btn->styleSheet().contains(QStringLiteral("#182238")));
                if (!firstLibBtn) {
                    firstLibBtn = btn;
                }
            }
        }
        QVERIFY(libCardCount >= 9);
        QVERIFY(firstLibBtn != nullptr);
        firstLibBtn->click();
        QCOMPARE((int)dlg.getActions().size(), 3);
    }

    // 2. Light theme + empty dialog lifecycle
    setTheme(true);
    {
        VisualDesignerDialog emptyDlg(UniValue(UniValue::VARR), nullptr);
        QCOMPARE((int)emptyDlg.getActions().size(), 0);
        bool sawLightCard = false;
        const QList<QPushButton*> buttons = emptyDlg.findChildren<QPushButton*>();
        for (QPushButton* btn : buttons) {
            if (btn->property("vdLibCard").toBool()) {
                QVERIFY(btn->styleSheet().contains(QStringLiteral("#F8FAFC")));
                sawLightCard = true;
                break;
            }
        }
        QVERIFY(sawLightCard);
    }

    // 3. Locale selector display label helper (CJK fallback when CJK font missing + preserved native Latin/Cyrillic)
    {
        const QString trLabel = formatLocaleDisplayName(QStringLiteral("tr"));
        QVERIFY(trLabel.contains(QStringLiteral("Türkçe")));
        QVERIFY(trLabel.endsWith(QStringLiteral("(tr)")));

        const QString ruLabel = formatLocaleDisplayName(QStringLiteral("ru"));
        QVERIFY(ruLabel.contains(QStringLiteral("русский")));
        QVERIFY(ruLabel.endsWith(QStringLiteral("(ru)")));

        QFontDatabase fdb;
        const QString koLabel = formatLocaleDisplayName(QStringLiteral("ko_KR"));
        QVERIFY(koLabel.endsWith(QStringLiteral("(ko_KR)")));
        if (fdb.families(QFontDatabase::Korean).isEmpty()) {
            QVERIFY(koLabel.contains(QStringLiteral("Korean")));
        }

        const QString zhLabel = formatLocaleDisplayName(QStringLiteral("zh_CN"));
        QVERIFY(zhLabel.endsWith(QStringLiteral("(zh_CN)")));
        if (fdb.families(QFontDatabase::SimplifiedChinese).isEmpty()) {
            QVERIFY(zhLabel.contains(QStringLiteral("Chinese")));
        }
    }

    // 4. Cached stylesheet consistency across Light and Dark themes
    {
        Q_INIT_RESOURCE(kristatech);
        setTheme(false);
        const QString dark1 = GUIUtil::loadStyleSheet();
        const QString dark2 = GUIUtil::loadStyleSheet();
        QVERIFY(!dark1.isEmpty());
        QCOMPARE(dark1, dark2);

        setTheme(true);
        const QString light1 = GUIUtil::loadStyleSheet();
        const QString light2 = GUIUtil::loadStyleSheet();
        QVERIFY(!light1.isEmpty());
        QCOMPARE(light1, light2);
        QVERIFY(light1 != dark1);

        const QString sLight1 = buildSettingsPageStyleSheet(true);
        const QString sLight2 = buildSettingsPageStyleSheet(true);
        const QString sDark1  = buildSettingsPageStyleSheet(false);
        const QString sDark2  = buildSettingsPageStyleSheet(false);
        QVERIFY(!sLight1.isEmpty());
        QVERIFY(!sDark1.isEmpty());
        QCOMPARE(sLight1, sLight2);
        QCOMPARE(sDark1, sDark2);
        QVERIFY(sLight1 != sDark1);
    }

    // 5. H-04 VisualDesignerDialog IF-Condition & Valid On-Curve Key Compilation without ECC_Start()
    {
        UniValue ifActions(UniValue::VARR);

        UniValue condNode(UniValue::VOBJ);
        condNode.pushKV("role", "if-condition");
        UniValue expr(UniValue::VOBJ);
        expr.pushKV("role", "check-signature-verification");
        UniValue exprInps(UniValue::VARR);
        UniValue exprInp(UniValue::VOBJ);
        exprInp.pushKV("name", "Pubkey");
        exprInp.pushKV("type", "pubkey");
        exprInp.pushKV("value", "02ee1fb80068f574b0d110009f110f703161cd358889a7bc48c613aa898136660f");
        exprInps.push_back(exprInp);
        expr.pushKV("inputs", exprInps);
        condNode.pushKV("expression", expr);
        condNode.pushKV("true_action", UniValue(UniValue::VARR)); // Auto-filled with number 1

        UniValue falseAct(UniValue::VOBJ);
        falseAct.pushKV("role", "multi-signature");
        UniValue falseInps(UniValue::VARR);
        UniValue inpM(UniValue::VOBJ); inpM.pushKV("name", "m"); inpM.pushKV("type", "number"); inpM.pushKV("value", 2); falseInps.push_back(inpM);
        UniValue inpN(UniValue::VOBJ); inpN.pushKV("name", "n"); inpN.pushKV("type", "number"); inpN.pushKV("value", 3); falseInps.push_back(inpN);
        UniValue sigArr(UniValue::VARR);
        sigArr.push_back("03de76ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1201");
        sigArr.push_back("02ef54ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1201");
        sigArr.push_back("03fa32ef1234a567bcde0123ef5678cd12345678ab12345678cd12345678ef1200");
        UniValue inpSigs(UniValue::VOBJ); inpSigs.pushKV("name", "Signatures"); inpSigs.pushKV("type", "array"); inpSigs.pushKV("value", sigArr); falseInps.push_back(inpSigs);
        falseAct.pushKV("inputs", falseInps);
        condNode.pushKV("false_action", falseAct);
        ifActions.push_back(condNode);

        VisualDesignerDialog condDlg(ifActions, nullptr);
        bool sawSuccessStatus = false;
        QString statusMsgs;
        const QList<QLabel*> labels = condDlg.findChildren<QLabel*>();
        for (QLabel* lbl : labels) {
            const QString tone = lbl->property("vdStatusTone").toString();
            if (!tone.isEmpty()) {
                statusMsgs += QStringLiteral("[%1] %2 | ").arg(tone, lbl->text());
            }
            if (tone == QStringLiteral("success") &&
                lbl->text().contains(QStringLiteral("Valid MESCAL Contract"))) {
                sawSuccessStatus = true;
                break;
            }
        }
        QVERIFY2(sawSuccessStatus, qPrintable(statusMsgs));

        // Verify expression branch selector excludes non-boolean lock-time role
        bool foundExprCombo = false;
        const QList<QComboBox*> combos = condDlg.findChildren<QComboBox*>();
        for (QComboBox* cb : combos) {
            if (cb->property("vdCombo").toBool() && cb->findData(QStringLiteral("number")) >= 0) {
                QCOMPARE(cb->findData(QStringLiteral("lock-time")), -1);
                foundExprCombo = true;
            }
        }
        QVERIFY(foundExprCombo);

        // Verify lock-time inside IF expression is flagged as error
        UniValue badIfActions(UniValue::VARR);
        UniValue badCond(UniValue::VOBJ);
        badCond.pushKV("role", "if-condition");
        badCond.pushKV("expression", lockNode);
        badCond.pushKV("true_action", sigNode);
        badIfActions.push_back(badCond);

        VisualDesignerDialog badDlg(badIfActions, nullptr);
        bool sawErrorStatus = false;
        const QList<QLabel*> badLabels = badDlg.findChildren<QLabel*>();
        for (QLabel* lbl : badLabels) {
            if (lbl->property("vdStatusTone").toString() == QStringLiteral("error")) {
                sawErrorStatus = true;
                break;
            }
        }
        QVERIFY(sawErrorStatus);
    }
}
