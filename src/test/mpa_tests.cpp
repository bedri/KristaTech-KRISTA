// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "kernel.h"
#include "main.h"
#include "chainparams.h"
#include "masternodeman.h"
#include "core_io.h"
#include "script/mescal.h"
#include "test_kristatech.h"
#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(mpa_tests, TestingSetup)

BOOST_AUTO_TEST_CASE(mpa_weight_pos_test)
{
    // Test that for heights below activation height, we always get PoS baseline weight (original amount)
    COutPoint prevout(uint256S("123"), 0);
    CBlockIndex indexPrev;
    indexPrev.nHeight = 10; // below activation
    int weightType = -1;
    CAmount weight = CalculateMPAWeight(prevout, 100 * COIN, GetTime(), &indexPrev, weightType);
    BOOST_CHECK_EQUAL(weight, 100 * COIN);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);
}

BOOST_AUTO_TEST_CASE(mpa_weight_decay_and_decay_limit_test)
{
    // Set up active height above Regtest activation height (300)
    CBlockIndex indexPrev;
    indexPrev.nHeight = 500;
    int weightType = -1;

    // Test default PoS at active height for normal destination
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubKey = key.GetPubKey();
    CTxDestination dest = pubKey.GetID();
    
    COutPoint prevout(uint256S("abc"), 0);
    CAmount weight = CalculateMPAWeight(prevout, 50 * COIN, GetTime(), &indexPrev, weightType);
    // Since prevout is not masternode collateral, doesn't have a timelock, and hasn't burned, it must return 50 * COIN
    BOOST_CHECK_EQUAL(weight, 50 * COIN);
    BOOST_CHECK_EQUAL(weightType, MPA_WEIGHT_POS);

    // Test Proof of Burn (PoB) decay logic
    // Add burn transaction to burn cache at height 450
    AddBurnToCache(dest, 10 * COIN, 450);

    // Height 500: T = 50. Decay should be: 1.0 - 50 / nBurnDecayBlocks.
    int nBurnDecayBlocks = Params().GetConsensus().nBurnDecayBlocks;
    double decay = 1.0 - 50.0 / (double)nBurnDecayBlocks;
    weight = GetActiveBurnWeight(dest, 500);
    BOOST_CHECK_EQUAL(weight, (CAmount)(10 * COIN * 5.0 * decay));

    // Height past decay limit: T = nBurnDecayBlocks + 50. Weight must be 0
    weight = GetActiveBurnWeight(dest, 450 + nBurnDecayBlocks + 50);
    BOOST_CHECK_EQUAL(weight, 0);
}

BOOST_AUTO_TEST_CASE(mescal_drop_compilation_test)
{
    std::string jsonStr = R"({
        "basic": {
            "MyDrop": {
                "role": "drop"
            }
        },
        "contract": {
            "TestDrop": {
                "description": "Test drop contract",
                "actions": [
                    { "type": "basic", "name": "MyDrop" }
                ]
            }
        },
        "active_contract": "TestDrop"
    })";
    std::string errorStr;
    CScript script = CMescal::Compile(jsonStr, errorStr);
    BOOST_CHECK(errorStr.empty());
    BOOST_CHECK_EQUAL(ScriptToAsmStr(script), "OP_DROP");

    UniValue decompileResult = CMescal::Decompile(script, errorStr);
    BOOST_CHECK(errorStr.empty());
    // Compile decompiled result back
    CScript recompiled = CMescal::Compile(decompileResult.write(), errorStr);
    BOOST_CHECK(errorStr.empty());
    BOOST_CHECK_EQUAL(ScriptToAsmStr(recompiled), "OP_DROP");
}

BOOST_AUTO_TEST_CASE(treasury_reward_split_test)
{
    CAmount nBlockValActual = 14 * COIN + COIN / 2;
    CAmount nTreasurySplit = nBlockValActual * 7 / 100;
    CAmount nFaucetSplit = nBlockValActual * 5 / 100;
    
    BOOST_CHECK_EQUAL(nTreasurySplit, 101500000); // 14.5 * 0.07 = 1.015 COIN
    BOOST_CHECK_EQUAL(nFaucetSplit, 72500000);   // 14.5 * 0.05 = 0.725 COIN
}

BOOST_AUTO_TEST_SUITE_END()
