// Copyright (c) 2011-2013 The Bitcoin Core developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for block-chain checkpoints
//

#include "checkpoints.h"

#include "uint256.h"
#include "test_kristatech.h"

#include <boost/test/unit_test.hpp>


BOOST_FIXTURE_TEST_SUITE(Checkpoints_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(sanity)
{
    uint256 p0 = uint256S("0x0");
    BOOST_CHECK(Checkpoints::CheckBlock(0, p0));

    // Wrong hashes at checkpoints should fail:
    BOOST_CHECK(!Checkpoints::CheckBlock(0, uint256S("0x1")));

    // ... but any hash not at a checkpoint should succeed:
    BOOST_CHECK(Checkpoints::CheckBlock(1, uint256S("0x1")));

    BOOST_CHECK(Checkpoints::GetTotalBlocksEstimate() >= 0);
}

BOOST_AUTO_TEST_SUITE_END()
