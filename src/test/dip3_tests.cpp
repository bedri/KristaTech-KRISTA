// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "evo/deterministicmns.h"
#include "hash.h"
#include "key.h"
#include "masternode.h"
#include "netbase.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "streams.h"
#include "test/test_kristatech.h"

#ifdef ENABLE_WALLET
#include "base58.h"
#include "wallet/test/wallet_test_fixture.h"
#include "wallet/wallet.h"
#include <fstream>
#ifndef WIN32
#include <sys/stat.h>
#endif
#endif

#include <boost/test/unit_test.hpp>
#include <deque>

BOOST_FIXTURE_TEST_SUITE(dip3_tests, BasicTestingSetup)

static CTransaction BuildProRegTransaction(const COutPoint& collateralOut,
                                           const CService& addr,
                                           const CKey& ownerKey,
                                           const CBLSSecretKey& opSk,
                                           const CScript& scriptPayout,
                                           const COutPoint& feeInputOut)
{
    CMutableTransaction mtx;
    mtx.vin.push_back(CTxIn(feeInputOut));

    dip3::CProRegTx ptx;
    ptx.nVersion = 1;
    ptx.collateralOutpoint = collateralOut;
    ptx.addr = addr;
    ptx.pubKeyOwner = ownerKey.GetPubKey();
    ptx.blsPubKeyOperator = CBLSPubKey(opSk);
    ptx.keyIDVoting = ownerKey.GetPubKey().GetID();
    ptx.nOperatorReward = 500; // 5%
    ptx.scriptPayout = scriptPayout;
    ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtx));
    BOOST_REQUIRE(ownerKey.Sign(ptx.GetSignHash(), ptx.vchSig));

    mtx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));
    return CTransaction(mtx);
}

BOOST_AUTO_TEST_CASE(dip3_proreg_rule8_multi_mn_same_ip)
{
    dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);

    CKey owner1, owner2, owner3;
    owner1.MakeNewKey(true);
    owner2.MakeNewKey(true);
    owner3.MakeNewKey(true);

    CBLSSecretKey opSk1, opSk2;
    opSk1.MakeNewKey();
    opSk2.MakeNewKey();

    CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(20);
    COutPoint col1(GetRandHash(), 0);
    COutPoint col2(GetRandHash(), 0);
    COutPoint colImmature(GetRandHash(), 0);
    COutPoint fee1(GetRandHash(), 1);
    COutPoint fee2(GetRandHash(), 1);
    COutPoint fee3(GetRandHash(), 1);

    view.AddCoin(col1, Coin(CTxOut(colAmt, GetScriptForDestination(owner1.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(col2, Coin(CTxOut(colAmt, GetScriptForDestination(owner2.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(colImmature, Coin(CTxOut(colAmt, GetScriptForDestination(owner3.GetPubKey().GetID())), 15, false, false), false);
    view.AddCoin(fee1, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner1.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee2, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner2.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee3, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner3.GetPubKey().GetID())), 1, false, false), false);

    CBlockIndex idxPrev;
    idxPrev.nHeight = 19;
    uint256 prevHash = GetRandHash();
    idxPrev.phashBlock = &prevHash;

    // 1. Register two MNs sharing the same IP (Rule 8)
    CService addr1 = LookupNumeric("192.168.10.50", 27999);
    CService addr2 = LookupNumeric("192.168.10.50", 28000);
    CTransaction tx1 = BuildProRegTransaction(col1, addr1, owner1, opSk1, GetScriptForDestination(owner1.GetPubKey().GetID()), fee1);
    CTransaction tx2 = BuildProRegTransaction(col2, addr2, owner2, opSk2, GetScriptForDestination(owner2.GetPubKey().GetID()), fee2);

    CBlock block1;
    block1.vtx.push_back(tx1);
    block1.vtx.push_back(tx2);

    CBlockIndex idx1;
    idx1.nHeight = 20;
    idx1.pprev = &idxPrev;
    uint256 b1Hash = GetRandHash();
    idx1.phashBlock = &b1Hash;

    CValidationState state;
    BOOST_CHECK(mgr.ProcessBlock(block1, &idx1, state, view, false));
    dip3::CDeterministicMNList list1 = mgr.GetListForBlock(&idx1);
    BOOST_CHECK_EQUAL(list1.GetAllMNsCount(), 2U);
    BOOST_CHECK_EQUAL(list1.GetValidMNsCount(), 2U);

    // 2. Reject immature collateral (<15 confs)
    CBLSSecretKey opSk3;
    opSk3.MakeNewKey();
    CTransaction txImmature = BuildProRegTransaction(colImmature, LookupNumeric("192.168.10.50", 28001), owner3, opSk3, GetScriptForDestination(owner3.GetPubKey().GetID()), fee3);
    CValidationState stateImm;
    BOOST_CHECK(!mgr.CheckSpecialTx(txImmature, &idx1, stateImm, view));
    BOOST_CHECK_EQUAL(stateImm.GetRejectReason(), "bad-protx-collateral-confs");

    // 3. Reject duplicate operator BLS key
    CTransaction txDupOp = BuildProRegTransaction(colImmature, LookupNumeric("192.168.10.50", 28002), owner3, opSk1, GetScriptForDestination(owner3.GetPubKey().GetID()), fee3);
    // Make colImmature mature by advancing idx1.nHeight
    CBlockIndex idxFuture;
    idxFuture.nHeight = 50;
    idxFuture.pprev = &idx1;
    uint256 bFutureHash = GetRandHash();
    idxFuture.phashBlock = &bFutureHash;
    CValidationState stateDup;
    BOOST_CHECK(!mgr.CheckSpecialTx(txDupOp, &idxFuture, stateDup, view, &list1));
    BOOST_CHECK_EQUAL(stateDup.GetRejectReason(), "bad-protx-dup-operator");
}

BOOST_AUTO_TEST_CASE(dip3_upserv_upreg_uprev_and_reorg_undo)
{
    dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);

    CKey owner;
    owner.MakeNewKey(true);
    CBLSSecretKey opSk1, opSk2;
    opSk1.MakeNewKey();
    opSk2.MakeNewKey();

    CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(20);
    COutPoint col(GetRandHash(), 0);
    COutPoint fee1(GetRandHash(), 1);
    COutPoint fee2(GetRandHash(), 1);
    COutPoint fee3(GetRandHash(), 1);
    COutPoint fee4(GetRandHash(), 1);

    view.AddCoin(col, Coin(CTxOut(colAmt, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee1, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee2, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee3, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee4, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);

    CBlockIndex idxPrev;
    idxPrev.nHeight = 19;
    uint256 prevHash = GetRandHash();
    idxPrev.phashBlock = &prevHash;

    // Block 1: ProRegTx
    CTransaction txReg = BuildProRegTransaction(col, LookupNumeric("10.0.0.1", 27999), owner, opSk1, GetScriptForDestination(owner.GetPubKey().GetID()), fee1);
    CBlock block1;
    block1.vtx.push_back(txReg);
    CBlockIndex idx1;
    idx1.nHeight = 20;
    idx1.pprev = &idxPrev;
    uint256 b1Hash = GetRandHash();
    idx1.phashBlock = &b1Hash;

    CValidationState state;
    BOOST_REQUIRE(mgr.ProcessBlock(block1, &idx1, state, view, false));
    dip3::CDeterministicMNList list1 = mgr.GetListForBlock(&idx1);
    BOOST_REQUIRE_EQUAL(list1.GetValidMNsCount(), 1U);

    // Block 2: ProUpRevTx (operator revokes)
    CMutableTransaction mtxRev;
    mtxRev.vin.push_back(CTxIn(fee2));
    dip3::CProUpRevTx revTx;
    revTx.nVersion = 1;
    revTx.proTxHash = txReg.GetHash();
    revTx.nReason = 1;
    revTx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxRev));
    BOOST_REQUIRE(revTx.blsSig.Sign(opSk1, revTx.GetSignHash()));
    mtxRev.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPREV, revTx)));

    CBlock block2;
    block2.vtx.push_back(CTransaction(mtxRev));
    CBlockIndex idx2;
    idx2.nHeight = 21;
    idx2.pprev = &idx1;
    uint256 b2Hash = GetRandHash();
    idx2.phashBlock = &b2Hash;

    BOOST_REQUIRE(mgr.ProcessBlock(block2, &idx2, state, view, false));
    dip3::CDeterministicMNList list2 = mgr.GetListForBlock(&idx2);
    BOOST_CHECK_EQUAL(list2.GetAllMNsCount(), 1U);
    BOOST_CHECK_EQUAL(list2.GetValidMNsCount(), 0U);
    BOOST_CHECK(list2.GetMN(txReg.GetHash())->state.IsRevoked());

    // Block 3: ProUpRegTx (owner assigns new operator opSk2)
    CMutableTransaction mtxUpReg;
    mtxUpReg.vin.push_back(CTxIn(fee3));
    dip3::CProUpRegTx upRegTx;
    upRegTx.nVersion = 1;
    upRegTx.proTxHash = txReg.GetHash();
    upRegTx.blsPubKeyOperator = CBLSPubKey(opSk2);
    upRegTx.keyIDVoting = owner.GetPubKey().GetID();
    upRegTx.scriptPayout = GetScriptForDestination(owner.GetPubKey().GetID());
    upRegTx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxUpReg));
    BOOST_REQUIRE(owner.Sign(upRegTx.GetSignHash(), upRegTx.vchSig));
    mtxUpReg.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPREG, upRegTx)));

    CBlock block3;
    block3.vtx.push_back(CTransaction(mtxUpReg));
    CBlockIndex idx3;
    idx3.nHeight = 22;
    idx3.pprev = &idx2;
    uint256 b3Hash = GetRandHash();
    idx3.phashBlock = &b3Hash;

    BOOST_REQUIRE(mgr.ProcessBlock(block3, &idx3, state, view, false));
    dip3::CDeterministicMNList list3 = mgr.GetListForBlock(&idx3);
    BOOST_CHECK_EQUAL(list3.GetValidMNsCount(), 1U);

    // Block 4: Spend collateral -> MN removed
    CMutableTransaction mtxSpend;
    mtxSpend.vin.push_back(CTxIn(col));
    mtxSpend.vout.push_back(CTxOut(colAmt - 1000, GetScriptForDestination(owner.GetPubKey().GetID())));

    CBlock block4;
    block4.vtx.push_back(CTransaction(mtxSpend));
    CBlockIndex idx4;
    idx4.nHeight = 23;
    idx4.pprev = &idx3;
    uint256 b4Hash = GetRandHash();
    idx4.phashBlock = &b4Hash;

    BOOST_REQUIRE(mgr.ProcessBlock(block4, &idx4, state, view, false));
    BOOST_CHECK_EQUAL(mgr.GetListForBlock(&idx4).GetAllMNsCount(), 0U);

    // Reorg UndoBlock(block4) restores list3 cleanly
    BOOST_REQUIRE(mgr.UndoBlock(block4, &idx4));
    BOOST_CHECK_EQUAL(mgr.GetListAtChainTip().GetValidMNsCount(), 1U);
    BOOST_CHECK_EQUAL(mgr.GetListForBlock(&idx3).GetValidMNsCount(), 1U);
}

BOOST_AUTO_TEST_CASE(dip3_v2_collateral_ownership_and_spend)
{
    dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);

    CKey victimOwner, attackerOwner, feeKey;
    victimOwner.MakeNewKey(true);
    attackerOwner.MakeNewKey(true);
    feeKey.MakeNewKey(true);

    CBLSSecretKey opSk1, opSk2;
    opSk1.MakeNewKey();
    opSk2.MakeNewKey();

    CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(5005);
    COutPoint colVictim(GetRandHash(), 0);
    COutPoint fee1(GetRandHash(), 1);
    COutPoint fee2(GetRandHash(), 1);

    view.AddCoin(colVictim, Coin(CTxOut(colAmt, GetScriptForDestination(victimOwner.GetPubKey().GetID())), 4950, false, false), false);
    view.AddCoin(fee1, Coin(CTxOut(colAmt + 1 * COIN, GetScriptForDestination(feeKey.GetPubKey().GetID())), 4950, false, false), false);
    view.AddCoin(fee2, Coin(CTxOut(1 * COIN, GetScriptForDestination(feeKey.GetPubKey().GetID())), 4950, false, false), false);

    CBlockIndex idxPrev;
    idxPrev.nHeight = 5004;
    uint256 prevHash = GetRandHash();
    idxPrev.phashBlock = &prevHash;

    // 1. Hijack attempt on existing UTXO belonging to victimOwner, signed by attackerOwner
    CTransaction txHijack = BuildProRegTransaction(
        colVictim, LookupNumeric("198.51.100.10", 27999),
        attackerOwner, opSk1, GetScriptForDestination(attackerOwner.GetPubKey().GetID()), fee1);
    CValidationState stateHijack;
    BOOST_CHECK(!mgr.CheckSpecialTx(txHijack, &idxPrev, stateHijack, view));
    BOOST_CHECK_EQUAL(stateHijack.GetRejectReason(), "bad-protx-collateral-owner");

    // 2. Under UPGRADE_HARDENING_V2 (Block 1+), null-hash in-tx collateral ProRegTx is rejected so 15-conf collateral rule cannot be bypassed
    {
        CMutableTransaction mtxInTx;
        mtxInTx.vin.push_back(CTxIn(fee1));
        mtxInTx.vout.push_back(CTxOut(colAmt, GetScriptForDestination(attackerOwner.GetPubKey().GetID())));

        dip3::CProRegTx ptx;
        ptx.nVersion = 1;
        ptx.collateralOutpoint = COutPoint(uint256(), 0);
        ptx.addr = LookupNumeric("198.51.100.10", 27999);
        ptx.pubKeyOwner = attackerOwner.GetPubKey();
        ptx.blsPubKeyOperator = CBLSPubKey(opSk1);
        ptx.keyIDVoting = attackerOwner.GetPubKey().GetID();
        ptx.nOperatorReward = 0;
        ptx.scriptPayout = GetScriptForDestination(attackerOwner.GetPubKey().GetID());
        ptx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxInTx));
        BOOST_REQUIRE(attackerOwner.Sign(ptx.GetSignHash(), ptx.vchSig));
        mtxInTx.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_REG, ptx)));

        CValidationState stateInTx;
        BOOST_CHECK(!mgr.CheckSpecialTx(CTransaction(mtxInTx), &idxPrev, stateInTx, view));
        BOOST_CHECK_EQUAL(stateInTx.GetRejectReason(), "bad-protx-collateral-null");
    }

    // 3. Same-block confirmed collateral registration + spend in same block -> rejected with bad-protx-collateral-spent-in-block
    {
        COutPoint colSame(GetRandHash(), 0);
        view.AddCoin(colSame, Coin(CTxOut(colAmt, GetScriptForDestination(attackerOwner.GetPubKey().GetID())), 4950, false, false), false);
        CTransaction txReg = BuildProRegTransaction(
            colSame, LookupNumeric("198.51.100.10", 27999),
            attackerOwner, opSk2, GetScriptForDestination(attackerOwner.GetPubKey().GetID()), fee2);

        CMutableTransaction mtxSpend;
        mtxSpend.vin.push_back(CTxIn(colSame));
        mtxSpend.vout.push_back(CTxOut(colAmt - 1000, GetScriptForDestination(attackerOwner.GetPubKey().GetID())));

        CBlock blockSameSpend;
        blockSameSpend.vtx.push_back(txReg);
        blockSameSpend.vtx.push_back(CTransaction(mtxSpend));

        CBlockIndex idxSame;
        idxSame.nHeight = 5005;
        idxSame.pprev = &idxPrev;
        uint256 hSame = GetRandHash();
        idxSame.phashBlock = &hSame;

        CValidationState stateSame;
        BOOST_CHECK(!mgr.ProcessBlock(blockSameSpend, &idxSame, stateSame, view, true));
        BOOST_CHECK_EQUAL(stateSame.GetRejectReason(), "bad-protx-collateral-spent-in-block");
    }
}

BOOST_AUTO_TEST_CASE(dip3_v2_bls_validation_and_operator_only_signing)
{
    // 1. Serialization validation rejects all-zero SK and invalid curve G1/G2 points
    {
        std::vector<unsigned char> zeroSk(32, 0x00);
        CDataStream ssSk(zeroSk, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSecretKey sk;
        ssSk >> sk;
        BOOST_CHECK(!sk.IsValid());

        std::vector<unsigned char> badPkBytes(48, 0xFF);
        CDataStream ssPk(badPkBytes, SER_NETWORK, PROTOCOL_VERSION);
        CBLSPubKey pk;
        ssPk >> pk;
        BOOST_CHECK(!pk.IsValid());

        std::vector<unsigned char> badSigBytes(96, 0xEE);
        CDataStream ssSig(badSigBytes, SER_NETWORK, PROTOCOL_VERSION);
        CBLSSignature sig;
        ssSig >> sig;
        BOOST_CHECK(!sig.IsValid());
    }

    // 2. Pure Hot-Node BLS operator signing succeeds without Cold owner ECDSA signature
    {
        CBLSSecretKey opSk, wrongSk;
        opSk.MakeNewKey();
        wrongSk.MakeNewKey();
        CBLSPubKey opPk(opSk), wrongPk(wrongSk);

        const std::string sMsg = "ktdip3-v2-operator-sign";
        const std::string sWrong = "ktdip3-v2-wrong-hash";
        uint256 msgHash = Hash(sMsg.begin(), sMsg.end());
        uint256 wrongHash = Hash(sWrong.begin(), sWrong.end());
        std::vector<unsigned char> vchSig;
        BOOST_REQUIRE(SignBLSOperatorOnly(msgHash, opSk, vchSig));

        CBLSPubKey extractedPk;
        CBLSSignature extractedSig;
        BOOST_CHECK(VerifyDip3OperatorBLS(msgHash, opPk, vchSig, &extractedPk, &extractedSig));
        BOOST_CHECK(extractedPk == opPk);
        BOOST_CHECK(extractedSig.IsValid());

        CBLSPubKey selfContainedPk;
        BOOST_CHECK(VerifySelfContainedBLSSignedData(msgHash, vchSig, selfContainedPk));
        BOOST_CHECK(selfContainedPk == opPk);

        BOOST_CHECK(!VerifyDip3OperatorBLS(msgHash, wrongPk, vchSig));
        BOOST_CHECK(!VerifyDip3OperatorBLS(wrongHash, opPk, vchSig));
    }

    // 3. CheckProUpServTx validates scriptOperatorPayout at DIP3 v2 and preserves update
    {
        dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);
        CCoinsView coinsDummy;
        CCoinsViewCache view(&coinsDummy);

        CKey owner, opPayKey;
        owner.MakeNewKey(true);
        opPayKey.MakeNewKey(true);
        CBLSSecretKey opSk;
        opSk.MakeNewKey();

        CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(5005);
        COutPoint col(GetRandHash(), 0);
        COutPoint fee1(GetRandHash(), 1);
        COutPoint fee2(GetRandHash(), 1);
        view.AddCoin(col, Coin(CTxOut(colAmt, GetScriptForDestination(owner.GetPubKey().GetID())), 4950, false, false), false);
        view.AddCoin(fee1, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 4950, false, false), false);
        view.AddCoin(fee2, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 4950, false, false), false);

        CBlockIndex idxPrev;
        idxPrev.nHeight = 5004;
        uint256 prevHash = GetRandHash();
        idxPrev.phashBlock = &prevHash;

        CTransaction txReg = BuildProRegTransaction(
            col, LookupNumeric("198.51.100.20", 27999),
            owner, opSk, GetScriptForDestination(owner.GetPubKey().GetID()), fee1);
        CBlock blk1;
        blk1.vtx.push_back(txReg);
        CBlockIndex idx1;
        idx1.nHeight = 5005;
        idx1.pprev = &idxPrev;
        uint256 h1 = GetRandHash();
        idx1.phashBlock = &h1;

        CValidationState st1;
        BOOST_REQUIRE(mgr.ProcessBlock(blk1, &idx1, st1, view, false));

        // Unspendable OP_RETURN operator payout rejected
        CMutableTransaction mtxBadServ;
        mtxBadServ.vin.push_back(CTxIn(fee2));
        dip3::CProUpServTx badServ;
        badServ.nVersion = 1;
        badServ.proTxHash = txReg.GetHash();
        badServ.addr = LookupNumeric("198.51.100.21", 27999);
        badServ.scriptOperatorPayout = CScript() << OP_RETURN << std::vector<unsigned char>{1, 2, 3};
        badServ.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxBadServ));
        BOOST_REQUIRE(badServ.blsSig.Sign(opSk, badServ.GetSignHash()));
        mtxBadServ.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPSERV, badServ)));

        CValidationState stBad;
        BOOST_CHECK(!mgr.CheckSpecialTx(CTransaction(mtxBadServ), &idx1, stBad, view));
        BOOST_CHECK_EQUAL(stBad.GetRejectReason(), "bad-proupserv-operator-payout");

        // Valid P2PKH operator payout accepted and stored
        CMutableTransaction mtxGoodServ;
        mtxGoodServ.vin.push_back(CTxIn(fee2));
        dip3::CProUpServTx goodServ;
        goodServ.nVersion = 1;
        goodServ.proTxHash = txReg.GetHash();
        goodServ.addr = LookupNumeric("198.51.100.21", 27999);
        goodServ.scriptOperatorPayout = GetScriptForDestination(opPayKey.GetPubKey().GetID());
        goodServ.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxGoodServ));
        BOOST_REQUIRE(goodServ.blsSig.Sign(opSk, goodServ.GetSignHash()));
        mtxGoodServ.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPSERV, goodServ)));

        CBlock blk2;
        blk2.vtx.push_back(CTransaction(mtxGoodServ));
        CBlockIndex idx2;
        idx2.nHeight = 5006;
        idx2.pprev = &idx1;
        uint256 h2 = GetRandHash();
        idx2.phashBlock = &h2;

        CValidationState stGood;
        BOOST_REQUIRE(mgr.ProcessBlock(blk2, &idx2, stGood, view, false));
        const dip3::CDeterministicMN* updated = mgr.GetListForBlock(&idx2).GetMN(txReg.GetHash());
        BOOST_REQUIRE(updated != nullptr);
        BOOST_CHECK(updated->state.scriptOperatorPayout == goodServ.scriptOperatorPayout);
    }
}

BOOST_AUTO_TEST_CASE(dip3_process_block_null_phashblock_just_check)
{
    dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);

    CKey owner;
    owner.MakeNewKey(true);
    CBLSSecretKey opSk;
    opSk.MakeNewKey();

    CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(20);
    COutPoint col(GetRandHash(), 0);
    COutPoint fee1(GetRandHash(), 1);
    view.AddCoin(col, Coin(CTxOut(colAmt, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);
    view.AddCoin(fee1, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 1, false, false), false);

    CBlockIndex idxRoot;
    idxRoot.nHeight = 19;
    uint256 rootHash = GetRandHash();
    idxRoot.phashBlock = &rootHash;

    // Commit one valid MN at idxPrev (height 20)
    CTransaction txReg = BuildProRegTransaction(col, LookupNumeric("10.0.0.1", 27999), owner, opSk, GetScriptForDestination(owner.GetPubKey().GetID()), fee1);
    CBlock blkPrev;
    blkPrev.vtx.push_back(txReg);
    CBlockIndex idxPrev;
    idxPrev.nHeight = 20;
    idxPrev.pprev = &idxRoot;
    uint256 prevHash = blkPrev.GetHash();
    idxPrev.phashBlock = &prevHash;

    CValidationState stPrev;
    BOOST_REQUIRE(mgr.ProcessBlock(blkPrev, &idxPrev, stPrev, view, /*fJustCheck=*/false));
    BOOST_REQUIRE_EQUAL(mgr.GetListAtChainTip().GetValidMNsCount(), 1U);

    // Dummy candidate CBlockIndex matching TestBlockValidity: phashBlock == nullptr
    CBlock blkCandidate;
    CBlockIndex idxDummy(blkCandidate);
    idxDummy.pprev = &idxPrev;
    idxDummy.nHeight = 21;
    BOOST_REQUIRE(idxDummy.phashBlock == nullptr);

    BOOST_CHECK_EQUAL(mgr.GetListForBlock(&idxDummy).GetAllMNsCount(), 0U);

    CValidationState stCheck;
    BOOST_CHECK(mgr.ProcessBlock(blkCandidate, &idxDummy, stCheck, view, /*fJustCheck=*/true));
    BOOST_CHECK(stCheck.IsValid());
    BOOST_CHECK(mgr.GetListAtChainTip().blockHash == prevHash);
    BOOST_CHECK_EQUAL(mgr.GetListAtChainTip().GetValidMNsCount(), 1U);
}

BOOST_AUTO_TEST_CASE(dip3_activation_heights_parity)
{
    const Consensus::Params& mainConsensus = Params(CBaseChainParams::MAIN).GetConsensus();
    BOOST_CHECK(!mainConsensus.NetworkUpgradeActive(0, Consensus::UPGRADE_DIP3));
    BOOST_CHECK(mainConsensus.NetworkUpgradeActive(1, Consensus::UPGRADE_DIP3));

    const Consensus::Params& testConsensus = Params(CBaseChainParams::TESTNET).GetConsensus();
    BOOST_CHECK(!testConsensus.NetworkUpgradeActive(0, Consensus::UPGRADE_DIP3));
    BOOST_CHECK(testConsensus.NetworkUpgradeActive(1, Consensus::UPGRADE_DIP3));

    const Consensus::Params& regConsensus = Params(CBaseChainParams::REGTEST).GetConsensus();
    BOOST_CHECK(regConsensus.NetworkUpgradeActive(1, Consensus::UPGRADE_DIP3));
}

BOOST_AUTO_TEST_CASE(dip3_faz3_canonical_payout_and_pose_lifecycle)
{
    dip3::CDeterministicMNManager mgr(1 << 20, /*fMemory=*/true, /*fWipe=*/true);
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);

    CKey owner;
    owner.MakeNewKey(true);
    CBLSSecretKey opSk1, opSk2;
    opSk1.MakeNewKey();
    opSk2.MakeNewKey();

    CAmount colAmt = CMasternode::GetMasternodeNodeCollateral(200);
    COutPoint col(GetRandHash(), 0);
    COutPoint fee1(GetRandHash(), 1);
    COutPoint fee2(GetRandHash(), 1);
    COutPoint fee3(GetRandHash(), 1);
    view.AddCoin(col, Coin(CTxOut(colAmt, GetScriptForDestination(owner.GetPubKey().GetID())), 100, false, false), false);
    view.AddCoin(fee1, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 100, false, false), false);
    view.AddCoin(fee2, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 100, false, false), false);
    view.AddCoin(fee3, Coin(CTxOut(1 * COIN, GetScriptForDestination(owner.GetPubKey().GetID())), 100, false, false), false);

    CBlockIndex idxPrev;
    idxPrev.nHeight = 199;
    uint256 prevHash = GetRandHash();
    idxPrev.phashBlock = &prevHash;

    // Non-canonical payout script (P2PK instead of canonical P2PKH/P2SH/P2TR) rejected in ProRegTx
    CScript nonCanonicalP2PK = CScript() << ToByteVector(owner.GetPubKey()) << OP_CHECKSIG;
    CTransaction txBadReg = BuildProRegTransaction(col, LookupNumeric("10.0.0.1", 27999), owner, opSk1, nonCanonicalP2PK, fee1);
    CValidationState stBadReg;
    BOOST_CHECK(!mgr.CheckSpecialTx(txBadReg, &idxPrev, stBadReg, view));
    BOOST_CHECK_EQUAL(stBadReg.GetRejectReason(), "bad-protx-payout-script");

    // Register with canonical P2PKH script
    CTransaction txReg = BuildProRegTransaction(col, LookupNumeric("10.0.0.1", 27999), owner, opSk1, GetScriptForDestination(owner.GetPubKey().GetID()), fee1);
    CBlock blkReg;
    blkReg.vtx.push_back(txReg);
    CBlockIndex idxReg;
    idxReg.nHeight = 200;
    idxReg.pprev = &idxPrev;
    uint256 hReg = GetRandHash();
    idxReg.phashBlock = &hReg;
    CValidationState stReg;
    BOOST_REQUIRE(mgr.ProcessBlock(blkReg, &idxReg, stReg, view, false));

    // Non-canonical operator payout rejected in ProUpServTx
    CMutableTransaction mtxBadServ;
    mtxBadServ.vin.push_back(CTxIn(fee2));
    dip3::CProUpServTx badServ;
    badServ.nVersion = 1;
    badServ.proTxHash = txReg.GetHash();
    badServ.addr = LookupNumeric("10.0.0.2", 27999);
    badServ.scriptOperatorPayout = nonCanonicalP2PK;
    badServ.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxBadServ));
    BOOST_REQUIRE(badServ.blsSig.Sign(opSk1, badServ.GetSignHash()));
    mtxBadServ.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPSERV, badServ)));
    CValidationState stBadServ;
    BOOST_CHECK(!mgr.CheckSpecialTx(CTransaction(mtxBadServ), &idxReg, stBadServ, view));
    BOOST_CHECK_EQUAL(stBadServ.GetRejectReason(), "bad-proupserv-operator-payout");

    // Non-canonical payout script rejected in ProUpRegTx
    CMutableTransaction mtxBadUpReg;
    mtxBadUpReg.vin.push_back(CTxIn(fee2));
    dip3::CProUpRegTx badUpReg;
    badUpReg.nVersion = 1;
    badUpReg.proTxHash = txReg.GetHash();
    badUpReg.blsPubKeyOperator = CBLSPubKey(opSk2);
    badUpReg.keyIDVoting = owner.GetPubKey().GetID();
    badUpReg.scriptPayout = nonCanonicalP2PK;
    badUpReg.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxBadUpReg));
    BOOST_REQUIRE(owner.Sign(badUpReg.GetSignHash(), badUpReg.vchSig));
    mtxBadUpReg.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPREG, badUpReg)));
    CValidationState stBadUpReg;
    BOOST_CHECK(!mgr.CheckSpecialTx(CTransaction(mtxBadUpReg), &idxReg, stBadUpReg, view));
    BOOST_CHECK_EQUAL(stBadUpReg.GetRejectReason(), "bad-proupreg-payout-script");

    // Deterministic PoSe scoring: 19 missed committee blocks -> 95 penalty; 1 solved -> 90; 2 missed -> 100 (PoSeBanned) -> ProUpServTx revival
    std::deque<uint256> vHashes;
    std::vector<CBlockIndex*> vIdxs;
    CBlockIndex* pCursor = &idxReg;
    auto pushPoseBlock = [&](bool solved) {
        CBlock blk;
        blk.nVersion = 12;
        blk.vAdamMiners.push_back(owner.GetPubKey());
        blk.vAdamSolutions.push_back(solved ? std::vector<unsigned char>{1, 2, 3} : std::vector<unsigned char>{});
        CBlockIndex* idx = new CBlockIndex();
        idx->nHeight = pCursor->nHeight + 1;
        idx->pprev = pCursor;
        uint256 h = GetRandHash();
        vHashes.push_back(h);
        idx->phashBlock = &vHashes.back();
        vIdxs.push_back(idx);
        CValidationState st;
        BOOST_REQUIRE(mgr.ProcessBlock(blk, idx, st, view, false));
        pCursor = idx;
    };

    for (int i = 0; i < 19; ++i) {
        pushPoseBlock(false);
    }
    const dip3::CDeterministicMN* mnAt95 = mgr.GetListForBlock(pCursor).GetMN(txReg.GetHash());
    BOOST_REQUIRE(mnAt95 != nullptr);
    BOOST_CHECK_EQUAL(mnAt95->state.nPoSePenalty, 95);
    BOOST_CHECK(!mnAt95->state.IsBanned());

    pushPoseBlock(true); // Penalty decays by 5 -> 90
    const dip3::CDeterministicMN* mnAt90 = mgr.GetListForBlock(pCursor).GetMN(txReg.GetHash());
    BOOST_REQUIRE(mnAt90 != nullptr);
    BOOST_CHECK_EQUAL(mnAt90->state.nPoSePenalty, 90);

    pushPoseBlock(false);
    pushPoseBlock(false); // Penalty reaches 100 -> PoSeBanned!
    const dip3::CDeterministicMN* mnBanned = mgr.GetListForBlock(pCursor).GetMN(txReg.GetHash());
    BOOST_REQUIRE(mnBanned != nullptr);
    BOOST_CHECK_EQUAL(mnBanned->state.nPoSePenalty, 100);
    BOOST_CHECK(mnBanned->state.IsBanned());
    BOOST_CHECK_EQUAL(mgr.GetListForBlock(pCursor).GetValidMNsCount(), 0U);

    // Revive via ProUpServTx
    CMutableTransaction mtxRevive;
    mtxRevive.vin.push_back(CTxIn(fee3));
    dip3::CProUpServTx reviveTx;
    reviveTx.nVersion = 1;
    reviveTx.proTxHash = txReg.GetHash();
    reviveTx.addr = LookupNumeric("10.0.0.1", 27999);
    reviveTx.inputsHash = dip3::CalcProTxInputsHash(CTransaction(mtxRevive));
    BOOST_REQUIRE(reviveTx.blsSig.Sign(opSk1, reviveTx.GetSignHash()));
    mtxRevive.vout.push_back(CTxOut(0, dip3::BuildProTxScript(dip3::PROTX_UPSERV, reviveTx)));
    CBlock blkRevive;
    blkRevive.vtx.push_back(CTransaction(mtxRevive));
    CBlockIndex idxRevive;
    idxRevive.nHeight = pCursor->nHeight + 1;
    idxRevive.pprev = pCursor;
    uint256 hRevive = GetRandHash();
    idxRevive.phashBlock = &hRevive;
    CValidationState stRevive;
    BOOST_REQUIRE(mgr.ProcessBlock(blkRevive, &idxRevive, stRevive, view, false));

    const dip3::CDeterministicMN* mnRevived = mgr.GetListForBlock(&idxRevive).GetMN(txReg.GetHash());
    BOOST_REQUIRE(mnRevived != nullptr);
    BOOST_CHECK(!mnRevived->state.IsBanned());
    BOOST_CHECK_EQUAL(mnRevived->state.nPoSePenalty, 0);
    BOOST_CHECK_EQUAL(mgr.GetListForBlock(&idxRevive).GetValidMNsCount(), 1U);

    for (CBlockIndex* p : vIdxs) delete p;
}

#ifdef ENABLE_WALLET
BOOST_FIXTURE_TEST_CASE(wallet_mnmeta_persistence_encryption_and_migration, WalletTestingSetup)
{
    BOOST_REQUIRE(pwalletMain != nullptr);

    // 1. Store operator ECDSA+BLS keypair + mnmeta draft and verify persistence across reload
    CKey opKey1;
    opKey1.MakeNewKey(false);
    CBLSSecretKey opBls1 = DeriveBLSFromCKey(opKey1);
    BOOST_REQUIRE(opBls1.IsValid());

    CMasternodeWalletRecord rec;
    rec.collateralOutpoint = COutPoint(GetRandHash(), 0);
    rec.strAlias = "mn_alpha";
    rec.strService = "127.0.0.1:27999";
    rec.nOperatorRewardBps = 500;
    rec.strPayoutAddress = EncodeDestination(opKey1.GetPubKey().GetID());
    BOOST_REQUIRE(pwalletMain->StoreMasternodeOperatorKeyPair(opKey1, rec.operatorKeyID, rec.strOperatorBlsPubKey));
    BOOST_REQUIRE(!rec.operatorKeyID.IsNull());
    BOOST_REQUIRE(pwalletMain->AddOrUpdateMasternodeMeta(rec));

    BOOST_CHECK(pwalletMain->HasMasternodeAlias("mn_alpha"));
    BOOST_CHECK(pwalletMain->HasMasternodeOperatorKey(rec.collateralOutpoint));

    std::string aliasErr;
    BOOST_CHECK(!pwalletMain->SetMasternodeAlias(rec.collateralOutpoint, "bad alias", aliasErr));
    BOOST_CHECK(!pwalletMain->SetMasternodeAlias(rec.collateralOutpoint, "", aliasErr));
    BOOST_REQUIRE(pwalletMain->SetMasternodeAlias(rec.collateralOutpoint, "mn_beta", aliasErr));
    BOOST_CHECK(!pwalletMain->HasMasternodeAlias("mn_alpha"));
    BOOST_CHECK(pwalletMain->HasMasternodeAlias("mn_beta"));

    {
        CWallet reloaded(pwalletMain->strWalletFile);
        LOCK(reloaded.cs_wallet);
        BOOST_REQUIRE_EQUAL(CWalletDB(reloaded.strWalletFile).LoadWallet(&reloaded), DB_LOAD_OK);
        CMasternodeWalletRecord loadedRec;
        BOOST_REQUIRE(reloaded.GetMasternodeMeta(rec.collateralOutpoint, loadedRec));
        BOOST_CHECK_EQUAL(loadedRec.strAlias, "mn_beta");
        BOOST_CHECK_EQUAL(loadedRec.strService, "127.0.0.1:27999");
        BOOST_CHECK_EQUAL(loadedRec.strOperatorBlsPubKey, rec.strOperatorBlsPubKey);
        BOOST_CHECK(loadedRec.operatorKeyID == rec.operatorKeyID);
        BOOST_CHECK_EQUAL(loadedRec.nOperatorRewardBps, 500U);
        BOOST_CHECK(reloaded.HasMasternodeOperatorKey(rec.collateralOutpoint));

        CKey fetchedKey;
        CBLSSecretKey fetchedBls;
        BOOST_REQUIRE(reloaded.GetMasternodeOperatorKeys(rec.collateralOutpoint, fetchedKey, fetchedBls));
        BOOST_CHECK(fetchedKey == opKey1);
        BOOST_CHECK(std::memcmp(fetchedBls.begin(), opBls1.begin(), 32) == 0);
        fetchedBls.Reset();
    }

    // 2. EncryptWallet -> metadata accessible while locked, operator secrets strictly gated behind Unlock()
    BOOST_REQUIRE(pwalletMain->EncryptWallet(SecureString("MnMetaPass#2026")));
    BOOST_CHECK(pwalletMain->IsCrypted());
    BOOST_CHECK(pwalletMain->IsLocked());
    BOOST_CHECK(pwalletMain->HasMasternodeOperatorKey(rec.collateralOutpoint));
    {
        CKey lockedKey;
        CBLSSecretKey lockedBls;
        BOOST_CHECK(!pwalletMain->GetMasternodeOperatorKeys(rec.collateralOutpoint, lockedKey, lockedBls));
    }

    {
        CWallet reloadedEnc(pwalletMain->strWalletFile);
        LOCK(reloadedEnc.cs_wallet);
        BOOST_REQUIRE_EQUAL(CWalletDB(reloadedEnc.strWalletFile).LoadWallet(&reloadedEnc), DB_LOAD_OK);
        BOOST_CHECK(reloadedEnc.IsCrypted());
        BOOST_CHECK(reloadedEnc.IsLocked());

        CMasternodeWalletRecord loadedRec;
        BOOST_REQUIRE(reloadedEnc.GetMasternodeMetaByAlias("mn_beta", loadedRec));
        BOOST_CHECK(loadedRec.collateralOutpoint == rec.collateralOutpoint);

        CKey fetchedKey;
        CBLSSecretKey fetchedBls;
        BOOST_CHECK(!reloadedEnc.GetMasternodeOperatorKeys(rec.collateralOutpoint, fetchedKey, fetchedBls));
        BOOST_REQUIRE(reloadedEnc.Unlock(SecureString("MnMetaPass#2026")));
        BOOST_REQUIRE(reloadedEnc.GetMasternodeOperatorKeys(rec.collateralOutpoint, fetchedKey, fetchedBls));
        BOOST_CHECK(fetchedKey == opKey1);
        BOOST_CHECK(std::memcmp(fetchedBls.begin(), opBls1.begin(), 32) == 0);
        fetchedBls.Reset();
    }

    // 3. Two-phase legacy masternode.conf migration: locked (deferred secret import) -> unlocked (.migrated 0600)
    CKey legKey;
    legKey.MakeNewKey(false);
    CBLSSecretKey legBls = DeriveBLSFromCKey(legKey);
    CBLSPubKey legBlsPub(legBls);
    const std::string legWif = EncodeSecret(legKey);
    const uint256 legColTx = GetRandHash();
    const COutPoint legColOut(legColTx, 1);

    const fs::path legacyConfPath = pathTemp / "masternode.conf";
    {
        std::ofstream ofs(legacyConfPath.string(), std::ios::out | std::ios::trunc);
        BOOST_REQUIRE(ofs.good());
        ofs << "# legacy masternode.conf test fixture\n\n"
            << "mn_legacy 127.0.0.2:27999 " << legWif << " " << legColTx.ToString() << " 1\n";
        ofs.close();
    }

    // Phase A: Wallet locked -> metadata imported without private keys; legacy masternode.conf preserved (0600)
    BOOST_REQUIRE(pwalletMain->IsLocked());
    BOOST_REQUIRE(MigrateLegacyMasternodeConfToWallet(pwalletMain, legacyConfPath));
    BOOST_CHECK(fs::exists(legacyConfPath));
    BOOST_CHECK(!fs::exists(legacyConfPath.string() + ".migrated"));
#ifndef WIN32
    {
        struct stat st{};
        BOOST_REQUIRE_EQUAL(stat(legacyConfPath.string().c_str(), &st), 0);
        BOOST_CHECK_EQUAL(st.st_mode & 0777, static_cast<mode_t>(0600));
    }
#endif
    {
        CMasternodeWalletRecord legRec;
        BOOST_REQUIRE(pwalletMain->GetMasternodeMetaByAlias("mn_legacy", legRec));
        BOOST_CHECK(legRec.collateralOutpoint == legColOut);
        BOOST_CHECK_EQUAL(legRec.strService, "127.0.0.2:27999");
        BOOST_CHECK_EQUAL(legRec.strOperatorBlsPubKey, HexStr(legBlsPub.begin(), legBlsPub.end()));
        BOOST_CHECK(legRec.operatorKeyID.IsNull());
        BOOST_CHECK(!pwalletMain->HasMasternodeOperatorKey(legColOut));
    }

    // Phase B: Wallet unlocked -> keys encrypted/imported into wallet.dat & legacy file renamed to .migrated (0600)
    BOOST_REQUIRE(pwalletMain->Unlock(SecureString("MnMetaPass#2026")));
    BOOST_REQUIRE(MigrateLegacyMasternodeConfToWallet(pwalletMain, legacyConfPath));
    BOOST_CHECK(!fs::exists(legacyConfPath));
    const fs::path migratedConfPath(legacyConfPath.string() + ".migrated");
    BOOST_CHECK(fs::exists(migratedConfPath));
#ifndef WIN32
    {
        struct stat st{};
        BOOST_REQUIRE_EQUAL(stat(migratedConfPath.string().c_str(), &st), 0);
        BOOST_CHECK_EQUAL(st.st_mode & 0777, static_cast<mode_t>(0600));
    }
#endif
    {
        CMasternodeWalletRecord legRec;
        BOOST_REQUIRE(pwalletMain->GetMasternodeMetaByAlias("mn_legacy", legRec));
        BOOST_CHECK(!legRec.operatorKeyID.IsNull());
        BOOST_CHECK(pwalletMain->HasMasternodeOperatorKey(legColOut));
        CKey fetchedLegKey;
        CBLSSecretKey fetchedLegBls;
        BOOST_REQUIRE(pwalletMain->GetMasternodeOperatorKeys(legColOut, fetchedLegKey, fetchedLegBls));
        BOOST_CHECK(fetchedLegKey == legKey);
        BOOST_CHECK(std::memcmp(fetchedLegBls.begin(), legBls.begin(), 32) == 0);
        fetchedLegBls.Reset();
    }

    // 4. RemoveMasternodeMeta persistence across reload
    BOOST_REQUIRE(pwalletMain->RemoveMasternodeMeta(legColOut));
    BOOST_CHECK(!pwalletMain->HasMasternodeAlias("mn_legacy"));
    {
        CWallet reloadedAfterDelete(pwalletMain->strWalletFile);
        LOCK(reloadedAfterDelete.cs_wallet);
        BOOST_REQUIRE_EQUAL(CWalletDB(reloadedAfterDelete.strWalletFile).LoadWallet(&reloadedAfterDelete), DB_LOAD_OK);
        CMasternodeWalletRecord missingRec;
        BOOST_CHECK(!reloadedAfterDelete.GetMasternodeMeta(legColOut, missingRec));
        BOOST_CHECK(reloadedAfterDelete.GetMasternodeMeta(rec.collateralOutpoint, missingRec));
    }
}
#endif // ENABLE_WALLET

BOOST_AUTO_TEST_SUITE_END()
