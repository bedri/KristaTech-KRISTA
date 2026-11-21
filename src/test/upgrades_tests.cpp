// Copyright (c) 2017 The Zcash Core developers
// Copyright (c) 2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "consensus/upgrades.h"
#include "optional.h"
#include "test/test_kristatech.h"

#include <boost/test/unit_test.hpp>

struct UpgradesTest : public TestingSetup
{
    int DefaultActivation[Consensus::MAX_NETWORK_UPGRADES];
    UpgradesTest()
    {
        SelectParams(CBaseChainParams::REGTEST);
        // Save activation heights in DefaultActivation and set all upgrades to inactive.
        for (uint32_t idx = Consensus::BASE_NETWORK + 1; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
            DefaultActivation[idx] = Params().GetConsensus().vUpgrades[idx].nActivationHeight;
            UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(idx), Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        }
    }
    ~UpgradesTest()
    {
        // Revert to default
        for (uint32_t idx = Consensus::BASE_NETWORK + 1; idx < Consensus::MAX_NETWORK_UPGRADES; idx++) {
            UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex(idx), DefaultActivation[idx]);
        }
        SelectParams(CBaseChainParams::MAIN);
    }
};

BOOST_FIXTURE_TEST_SUITE(network_upgrades_tests, UpgradesTest)

BOOST_AUTO_TEST_CASE(networkUpgradeStateTest)
{
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_DISABLED);
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_DISABLED);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_ACTIVE);
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_ACTIVE);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(0, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_PENDING);
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(nActivationHeight - 1, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_PENDING);
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(nActivationHeight, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_ACTIVE);
    BOOST_CHECK_EQUAL(
            NetworkUpgradeState(1000000, params, Consensus::UPGRADE_TESTDUMMY),
            UPGRADE_ACTIVE);
}

BOOST_AUTO_TEST_CASE(IsActivationHeightTest)
{
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    BOOST_CHECK(!IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    BOOST_CHECK(!IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    BOOST_CHECK(!IsActivationHeight(-1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(0, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(nActivationHeight - 1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(IsActivationHeight(nActivationHeight, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(nActivationHeight + 1, params, Consensus::UPGRADE_TESTDUMMY));
    BOOST_CHECK(!IsActivationHeight(1000000, params, Consensus::UPGRADE_TESTDUMMY));
}

BOOST_AUTO_TEST_CASE(IsActivationHeightForAnyUpgradeTest)
{
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(-1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(0, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1000000, params));

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(-1, params));
    BOOST_CHECK(IsActivationHeightForAnyUpgrade(0, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1000000, params));

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(-1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(0, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(nActivationHeight - 1, params));
    BOOST_CHECK(IsActivationHeightForAnyUpgrade(nActivationHeight, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(nActivationHeight + 1, params));
    BOOST_CHECK(!IsActivationHeightForAnyUpgrade(1000000, params));
}

BOOST_AUTO_TEST_CASE(NextActivationHeightTest)
{
    const Consensus::Params& params = Params().GetConsensus();

    // Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT
    BOOST_CHECK(NextActivationHeight(-1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(0, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(1000000, params) == nullopt);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);

    BOOST_CHECK(NextActivationHeight(-1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(0, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(1000000, params) == nullopt);

    int nActivationHeight = 100;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_TESTDUMMY, nActivationHeight);

    BOOST_CHECK(NextActivationHeight(-1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(0, params) == nActivationHeight);
    BOOST_CHECK(NextActivationHeight(1, params) == nActivationHeight);
    BOOST_CHECK(NextActivationHeight(nActivationHeight - 1, params) == nActivationHeight);
    BOOST_CHECK(NextActivationHeight(nActivationHeight, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(nActivationHeight + 1, params) == nullopt);
    BOOST_CHECK(NextActivationHeight(1000000, params) == nullopt);
}

BOOST_AUTO_TEST_CASE(HardeningV2UpgradeMetadataTest)
{
    BOOST_CHECK_EQUAL(std::string(NetworkUpgradeInfo[Consensus::UPGRADE_HARDENING_V2].strName), "Hardening_v2");
    BOOST_CHECK_EQUAL(DefaultActivation[Consensus::UPGRADE_HARDENING_V2], 1);

    SelectParams(CBaseChainParams::MAIN);
    BOOST_CHECK_EQUAL(Params().GetConsensus().vUpgrades[Consensus::UPGRADE_HARDENING_V2].nActivationHeight, 1);
    BOOST_CHECK(!Params().GetConsensus().NetworkUpgradeActive(0, Consensus::UPGRADE_HARDENING_V2));
    BOOST_CHECK(Params().GetConsensus().NetworkUpgradeActive(1, Consensus::UPGRADE_HARDENING_V2));

    SelectParams(CBaseChainParams::TESTNET);
    BOOST_CHECK_EQUAL(Params().GetConsensus().vUpgrades[Consensus::UPGRADE_HARDENING_V2].nActivationHeight, 1);
    BOOST_CHECK(Params().GetConsensus().NetworkUpgradeActive(1, Consensus::UPGRADE_HARDENING_V2));

    SelectParams(CBaseChainParams::REGTEST);
}

BOOST_AUTO_TEST_CASE(FairnessUpgradeMetadataTest)
{
    BOOST_CHECK_EQUAL(std::string(NetworkUpgradeInfo[Consensus::UPGRADE_FAIRNESS].strName), "Fairness");
    BOOST_CHECK_EQUAL(DefaultActivation[Consensus::UPGRADE_FAIRNESS], 1);

    for (CBaseChainParams::Network net : {CBaseChainParams::MAIN, CBaseChainParams::TESTNET}) {
        SelectParams(net);
        BOOST_CHECK_EQUAL(Params().GetConsensus().vUpgrades[Consensus::UPGRADE_FAIRNESS].nActivationHeight, 1);
        BOOST_CHECK(!Params().GetConsensus().NetworkUpgradeActive(0, Consensus::UPGRADE_FAIRNESS));
        BOOST_CHECK(Params().GetConsensus().NetworkUpgradeActive(1, Consensus::UPGRADE_FAIRNESS));
    }

    SelectParams(CBaseChainParams::REGTEST);
}

BOOST_AUTO_TEST_CASE(OutOfOrderEpochProgressionTest)
{
    const Consensus::Params& mainParams = Params(CBaseChainParams::MAIN).GetConsensus();
    BOOST_CHECK_EQUAL(CurrentEpoch(0, mainParams), (int)Consensus::BASE_NETWORK);
    BOOST_CHECK(NextActivationHeight(0, mainParams) == Optional<int>(1));
    BOOST_CHECK_EQUAL(CurrentEpoch(1, mainParams), (int)Consensus::UPGRADE_FAIRNESS);
    BOOST_CHECK_EQUAL(mainParams.nAdamBootstrapLimit, 2500);
    BOOST_CHECK(NextActivationHeight(1, mainParams) == Optional<int>(200));
    BOOST_CHECK_EQUAL(CurrentEpoch(200, mainParams), (int)Consensus::UPGRADE_ADAM);
    BOOST_CHECK(NextEpoch(200, mainParams) == Optional<int>(Consensus::UPGRADE_POMBL));
    BOOST_CHECK(NextActivationHeight(200, mainParams) == Optional<int>(4800));
    BOOST_CHECK_EQUAL(CurrentEpoch(4800, mainParams), (int)Consensus::UPGRADE_POMBL);
    BOOST_CHECK(NextEpoch(4800, mainParams) == Optional<int>(Consensus::UPGRADE_ADAM_V2));
    BOOST_CHECK(NextActivationHeight(4800, mainParams) == Optional<int>(5000));
    BOOST_CHECK_EQUAL(CurrentEpoch(5000, mainParams), (int)Consensus::UPGRADE_MODELD);
    BOOST_CHECK(NextEpoch(5000, mainParams) == Optional<int>(Consensus::UPGRADE_STAKE_MIN_DEPTH_V2));
    BOOST_CHECK(NextActivationHeight(5000, mainParams) == Optional<int>(5001));
    BOOST_CHECK_EQUAL(CurrentEpoch(5001, mainParams), (int)Consensus::UPGRADE_STAKE_MIN_DEPTH_V2);
    BOOST_CHECK(NextEpoch(5001, mainParams) == nullopt);
    BOOST_CHECK(NextActivationHeight(5001, mainParams) == nullopt);

    const Consensus::Params& testParams = Params(CBaseChainParams::TESTNET).GetConsensus();
    BOOST_CHECK(NextActivationHeight(200, testParams) == Optional<int>(400));
    BOOST_CHECK(NextEpoch(200, testParams) == Optional<int>(Consensus::UPGRADE_POMBL));
    BOOST_CHECK(NextActivationHeight(400, testParams) == Optional<int>(500));
    BOOST_CHECK(NextActivationHeight(500, testParams) == Optional<int>(5001));
    BOOST_CHECK_EQUAL(CurrentEpoch(500, testParams), (int)Consensus::UPGRADE_MODELD);
    BOOST_CHECK_EQUAL(CurrentEpoch(5001, testParams), (int)Consensus::UPGRADE_STAKE_MIN_DEPTH_V2);
    BOOST_CHECK(NextActivationHeight(5001, testParams) == nullopt);
}

BOOST_AUTO_TEST_CASE(RegtestExplicitUpgradeDefaultsTest)
{
    BOOST_CHECK_EQUAL(DefaultActivation[Consensus::UPGRADE_STAKE_MIN_DEPTH_V2],
                      (int)Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    BOOST_CHECK_EQUAL(DefaultActivation[Consensus::UPGRADE_ADAM_V2], 200);

    const Consensus::Params& regParams = Params(CBaseChainParams::REGTEST).GetConsensus();
    BOOST_CHECK_EQUAL(regParams.nStakeMinDepth, 2);
    BOOST_CHECK_EQUAL(regParams.nStakeMinDepthV2, 2);
}

BOOST_AUTO_TEST_CASE(StakeMinDepthV2ConsensusTest)
{
    const Consensus::Params& mainParams = Params(CBaseChainParams::MAIN).GetConsensus();
    BOOST_CHECK(!mainParams.HasStakeMinAgeOrDepth(5000, 5000, 4900, 5000 - 3599));
    BOOST_CHECK(mainParams.HasStakeMinAgeOrDepth(5000, 5000, 4900, 5000 - 3600));
    BOOST_CHECK(!mainParams.HasStakeMinAgeOrDepth(5001, 100000, 5001 - 599, 0));
    BOOST_CHECK(mainParams.HasStakeMinAgeOrDepth(5001, 100000, 5001 - 600, 100000));

    const Consensus::Params& testParams = Params(CBaseChainParams::TESTNET).GetConsensus();
    BOOST_CHECK(testParams.HasStakeMinAgeOrDepth(5000, 100, 4999, 100));
    BOOST_CHECK(!testParams.HasStakeMinAgeOrDepth(5001, 100000, 5001 - 29, 0));
    BOOST_CHECK(testParams.HasStakeMinAgeOrDepth(5001, 100000, 5001 - 30, 100000));

    const Consensus::Params& regParams = Params(CBaseChainParams::REGTEST).GetConsensus();
    BOOST_CHECK(regParams.HasStakeMinAgeOrDepth(6000, 100, 5999, 100));
}

BOOST_AUTO_TEST_SUITE_END()
