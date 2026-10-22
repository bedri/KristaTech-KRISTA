// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "uritests.h"

#include "chainparams.h"
#include "guiutil.h"
#include "key.h"
#include "key_io.h"
#include "main.h"
#include "masternode.h"
#include "pubkey.h"
#include "random.h"
#include "transactionrecord.h"
#include "wallet/wallet.h"
#include "walletmodel.h"

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
    mtxReg.vout.push_back(CTxOut(1000 * COIN, coinLockScript));
    CWalletTx wtxReg(&wallet, CTransaction(mtxReg));

    QList<TransactionRecord> regRecords = TransactionRecord::decomposeTransaction(&wallet, wtxReg);
    QCOMPARE(regRecords.size(), 1);
    QCOMPARE((int)regRecords[0].type, (int)TransactionRecord::MinerRegistration);
    QCOMPARE(regRecords[0].credit, 1000 * COIN);
    QCOMPARE(QString::fromStdString(regRecords[0].address), QString::fromStdString(EncodeDestination(pubkey.GetID())));

    // 2. Test Model D reward classification (AdamReward, MNReward, LlmqReward) in CoinStake
    const int nTestHeight = 50000;
    CAmount nBlockValActual = CMasternode::GetBlockValue(nTestHeight);
    CAmount nTreasurySplit = nBlockValActual * 7 / 100;
    CAmount nFaucetSplit = nBlockValActual * 7 / 1000;
    CAmount nBlockVal = nBlockValActual - (nTreasurySplit + nFaucetSplit);
    CAmount nMNSplit = nBlockVal * 50 / 100;
    CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
    CAmount llmqBase = nLLMQSplitTotal / 5;
    CAmount adamMinerShare = (nBlockVal * 20 / 100) / 11;

    CScript p2pkhScript = GetScriptForDestination(pubkey.GetID());
    CMutableTransaction mtxStake;
    mtxStake.vin.push_back(CTxIn(COutPoint(uint256S("2222"), 0)));
    mtxStake.vout.push_back(CTxOut(0, CScript())); // Empty vout[0] marks coinstake
    mtxStake.vout.push_back(CTxOut(adamMinerShare, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(nMNSplit, p2pkhScript));
    mtxStake.vout.push_back(CTxOut(llmqBase, p2pkhScript));

    CWalletTx wtxStake(&wallet, CTransaction(mtxStake));
    uint256 fakeBlockHash = uint256S("3333333333333333333333333333333333333333333333333333333333333333");
    wtxStake.hashBlock = fakeBlockHash;

    // 2a. Simulate -resync startup where mapBlockIndex does not yet contain fakeBlockHash
    QList<TransactionRecord> resyncRecords = TransactionRecord::decomposeTransaction(&wallet, wtxStake);
    QCOMPARE(resyncRecords.size(), 3);
    QCOMPARE((int)resyncRecords[0].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[1].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[2].type, (int)TransactionRecord::MNReward);

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
    QCOMPARE((int)resyncRecords[1].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)resyncRecords[2].type, (int)TransactionRecord::LlmqReward);

    QList<TransactionRecord> stakeRecords = TransactionRecord::decomposeTransaction(&wallet, wtxStake);
    QCOMPARE(stakeRecords.size(), 3);
    QCOMPARE((int)stakeRecords[0].type, (int)TransactionRecord::AdamReward);
    QCOMPARE((int)stakeRecords[1].type, (int)TransactionRecord::MNReward);
    QCOMPARE((int)stakeRecords[2].type, (int)TransactionRecord::LlmqReward);

    {
        LOCK(cs_main);
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
        TransactionRecord syncRec(uint256S("6666"), 1700000000, 250, TransactionRecord::MinerRegistration, "", -1000 * COIN, 0);
        syncRec.updateStatus(wtxReg);
        QCOMPARE((int)syncRec.status.status, (int)TransactionStatus::Unconfirmed);
        QVERIFY(syncRec.status.fSyncing);
        QCOMPARE(QString::fromStdString(syncRec.statusToString()), QString("Syncing..."));

        chainActive.SetTip(nullptr);
    }

    ECC_Stop();
}
