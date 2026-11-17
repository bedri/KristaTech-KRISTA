// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "crypto/bls.h"
#include "evo/deterministicmns.h"
#include "key.h"
#include "masternode.h"
#include "netbase.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "test/test_kristatech.h"

#include <boost/test/unit_test.hpp>

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

BOOST_AUTO_TEST_SUITE_END()
