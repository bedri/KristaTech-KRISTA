// Copyright (c) 2012-2013 The Bitcoin Core developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "coins.h"
#include "consensus/tx_verify.h"
#include "pubkey.h"
#include "key.h"
#include "script/script.h"
#include "script/standard.h"
#include "uint256.h"
#include "test_kristatech.h"

#include <vector>

#include <boost/test/unit_test.hpp>


// Helpers:
static std::vector<unsigned char>
Serialize(const CScript& s)
{
    std::vector<unsigned char> sSerialized(s.begin(), s.end());
    return sSerialized;
}

BOOST_FIXTURE_TEST_SUITE(sigopcount_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(GetSigOpCount)
{
    // Test CScript::GetSigOpCount()
    CScript s1;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false), 0U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 0U);

    uint160 dummy;
    s1 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << OP_2 << OP_CHECKMULTISIG;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 2U);
    s1 << OP_IF << OP_CHECKSIG << OP_ENDIF;
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(true), 3U);
    BOOST_CHECK_EQUAL(s1.GetSigOpCount(false), 21U);

    CScript p2sh = GetScriptForDestination(CScriptID(s1));
    CScript scriptSig;
    scriptSig << OP_0 << Serialize(s1);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig), 3U);

    std::vector<CPubKey> keys;
    for (int i = 0; i < 3; i++)
    {
        CKey k;
        k.MakeNewKey(true);
        keys.push_back(k.GetPubKey());
    }
    CScript s2 = GetScriptForMultisig(1, keys);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(true), 3U);
    BOOST_CHECK_EQUAL(s2.GetSigOpCount(false), 20U);

    p2sh = GetScriptForDestination(CScriptID(s2));
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(true), 0U);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(false), 0U);
    CScript scriptSig2;
    scriptSig2 << OP_1 << ToByteVector(dummy) << ToByteVector(dummy) << Serialize(s2);
    BOOST_CHECK_EQUAL(p2sh.GetSigOpCount(scriptSig2), 3U);

    // H-05: OP_CHECKSIGADD counting and Taproot GetP2SHSigOpCount (key-path & script-path)
    CScript csaLeaf;
    std::vector<unsigned char> xonly32(32, 0x11);
    csaLeaf << OP_0 << xonly32 << OP_CHECKSIGADD << xonly32 << OP_CHECKSIGADD << OP_2 << OP_EQUAL;
    BOOST_CHECK_EQUAL(csaLeaf.GetSigOpCount(true), 2U);
    BOOST_CHECK_EQUAL(csaLeaf.GetSigOpCount(false), 2U);

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    COutPoint tapOut(uint256S("abcd"), 0);
    CScript tapSpk = CScript() << OP_1 << xonly32;
    view.AddCoin(tapOut, Coin(CTxOut(COIN, tapSpk), 10, false, false), false);

    CMutableTransaction mtxKeyPath;
    mtxKeyPath.vin.emplace_back(tapOut, CScript() << std::vector<unsigned char>(64, 0x22));
    BOOST_CHECK_EQUAL(GetP2SHSigOpCount(CTransaction(mtxKeyPath), view), 1U);

    CMutableTransaction mtxScriptPath;
    mtxScriptPath.vin.emplace_back(tapOut, CScript() << std::vector<unsigned char>(64, 0x33) << Serialize(csaLeaf) << std::vector<unsigned char>(33, 0xc0));
    BOOST_CHECK_EQUAL(GetP2SHSigOpCount(CTransaction(mtxScriptPath), view), 2U);
}

BOOST_AUTO_TEST_SUITE_END()
