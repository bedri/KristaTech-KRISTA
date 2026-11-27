// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"
#include "main.h"
#include "primitives/transaction.h"
#include "script/sign.h"
#include "test_kristatech.h"
#include <boost/test/unit_test.hpp>
#include "masternode.h"
#include "crypto/bls.h"
#include "adam.h"
#include "chainparams.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include <limits>
#include "kernel.h"
#include "legacy/stakemodifier.h"
#include "policy/policy.h"
#include "spork.h"
#include "sporkdb.h"
#include "stakeinput.h"

BOOST_FIXTURE_TEST_SUITE(main_tests, TestingSetup)

enum BlockSignatureType{
    P2PK,
    P2PKH
};

CScript GetScriptForType(CPubKey pubKey, BlockSignatureType type)
{
    switch(type){
        case P2PK:
            return CScript() << pubKey << OP_CHECKSIG;
        default:
            return GetScriptForDestination(pubKey.GetID());
    }
}

std::vector<unsigned char> CreateDummyScriptSigWithKey(CPubKey pubKey)
{
    std::vector<unsigned char> vchSig;
    const CScript scriptCode;
    DummySignatureCreator(nullptr).CreateSig(vchSig, pubKey.GetID(), scriptCode, SIGVERSION_BASE);
    return vchSig;
}

CScript GetDummyScriptSigByType(CPubKey pubKey, bool isP2PK)
{
    CScript script = CScript() << CreateDummyScriptSigWithKey(pubKey);
    if (!isP2PK)
        script << ToByteVector(pubKey);
    return script;
}

CBlock CreateDummyBlockWithSignature(CKey stakingKey, BlockSignatureType type, bool useInputP2PK)
{
    CMutableTransaction txCoinStake;
    // Dummy input
    CTxIn input(uint256(), 0);
    // P2PKH input
    input.scriptSig = GetDummyScriptSigByType(stakingKey.GetPubKey(), useInputP2PK);
    // Add dummy input
    txCoinStake.vin.emplace_back(input);
    // Empty first output
    txCoinStake.vout.emplace_back(CTxOut(0, CScript()));
    // P2PK staking output
    CScript scriptPubKey = GetScriptForType(stakingKey.GetPubKey(), type);
    txCoinStake.vout.emplace_back(CTxOut(0, scriptPubKey));

    // Now the block.
    CBlock block;
    block.nTime = Params().Checkpoints().nTimeLastCheckpoint + 1;
    block.vtx.emplace_back(CTransaction()); // dummy first tx
    block.vtx.emplace_back(txCoinStake);
    SignBlockWithKey(block, stakingKey, DeriveBLSFromCKey(stakingKey));

    return block;
}

bool TestBlockSignaturePreEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, false);
}

bool TestBlockSignaturePostEnforcementV5(const CBlock& block)
{
    return CheckBlockSignature(block, true);
}

BOOST_AUTO_TEST_CASE(block_signature_test)
{
    for (int i = 0; i < 20; ++i) {
        CKey stakingKey;
        stakingKey.MakeNewKey(true);
        bool useInputP2PK = i % 2 == 0;

        // Test P2PK block signature pre enforcement.
        CBlock block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePreEnforcementV5(block));

        // Test P2PK block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PK, useInputP2PK);
        BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));

        // Test P2PKH block signature pre enforcement ---> must fail.
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        BOOST_CHECK(!TestBlockSignaturePreEnforcementV5(block));

        // Test P2PKH block signature post enforcement
        block = CreateDummyBlockWithSignature(stakingKey, BlockSignatureType::P2PKH, useInputP2PK);
        if (useInputP2PK) {
            // If it's using a P2PK scriptsig as input and a P2PKH output
            // The block doesn't contain the public key to verify the sig anywhere.
            // Must fail.
            BOOST_CHECK(!TestBlockSignaturePostEnforcementV5(block));
        } else {
            BOOST_CHECK(TestBlockSignaturePostEnforcementV5(block));
        }
    }
}

CAmount nMoneySupplyPoWEnd = 43199500 * COIN;

BOOST_AUTO_TEST_CASE(subsidy_limit_test)
{
    // Check height 0 (returns starting reward: 17 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(0) == 1700000000LL);

    // Check height 1 (Premine)
    BOOST_CHECK(CMasternode::GetBlockValue(1) == 0LL);

    // Check heights in Period 0 (Blocks 2..259201 -> 17 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(2) == 1700000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(2200) == 1700000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(4800) == 1700000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(5000) == 1700000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(10000) == 1700000000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(259201) == 1700000000LL);

    // Check heights in Period 1 (-2.15% decay -> 16.6345 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(259202) == 1663450000LL);
    BOOST_CHECK(CMasternode::GetBlockValue(350000) == 1663450000LL);

    // Check height at Period 2 (16.27685825 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(518402) == 1627685825LL);

    // Check height at Period 4 (15.58447732 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(1036802) == 1558447732LL);

    // Check height at Period 8 (14.28681961 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(2073602) == 1428681961LL);

    // Check height at Period 12 (13.09721272 * COIN)
    BOOST_CHECK(CMasternode::GetBlockValue(3110402) == 1309721272LL);
}

bool ReturnFalse() { return false; }
bool ReturnTrue() { return true; }

BOOST_AUTO_TEST_CASE(test_combiner_all)
{
    boost::signals2::signal<bool(), CombinerAll> Test;
    BOOST_CHECK(Test());
    Test.connect(&ReturnFalse);
    BOOST_CHECK(!Test());
    Test.connect(&ReturnTrue);
    BOOST_CHECK(!Test());
    Test.disconnect(&ReturnFalse);
    BOOST_CHECK(Test());
    Test.disconnect(&ReturnTrue);
    BOOST_CHECK(Test());
}

BOOST_AUTO_TEST_CASE(debug_block_hashing)
{
    CBlock block;
    block.nVersion = 11;
    block.hashPrevBlock = uint256S("0768e3a54adc76099a5c0c9f78eb72999f11120e98178493ef577045cbad3240");
    block.hashMerkleRoot = uint256S("cda6ecfc5d14c88f86e29fce1cf1a300c259d7b2d11e7a1d29b7857b9ac5aae9");
    block.nTime = 1780825609;
    block.nBits = 0x1e0ffff0;
    block.nNonce = 0;

    std::cout << "\n=== DEBUG BLOCK HASHING WITH EXACT FIELDS ===" << std::endl;
    
    // Case 1: All ADAM fields empty
    block.vAdamMiners.clear();
    block.vAdamSolutions.clear();
    block.vAdamVRFProof.clear();
    block.vAdamCoordinatorSig.clear();
    std::cout << "1. Empty ADAM fields:" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    std::cout << "   SerializeHash(CBlockHeader) = " << SerializeHash(block.GetBlockHeader()).ToString() << std::endl;

    // Case 2: Populating vAdamMiners with 14 dummy public keys (but empty solutions/vrf)
    for (int i = 0; i < 14; ++i) {
        CKey key;
        key.MakeNewKey(true);
        block.vAdamMiners.push_back(key.GetPubKey());
    }
    std::cout << "2. Only vAdamMiners populated (14 miners):" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    
    CDataStream ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << block.GetBlockHeader();
    std::cout << "   Serialized Hex (SER_GETHASH) = " << HexStr(ss.begin(), ss.end()) << std::endl;
    
    const unsigned char* pbegin = (const unsigned char*)&ss[0];
    const unsigned char* pend = pbegin + ss.size();
    std::cout << "   DoubleSHA256 of Serialized = " << Hash(pbegin, pend).ToString() << std::endl;
    std::cout << "   X11KVS of Serialized = " << HashX11KVS(pbegin, pend).ToString() << std::endl;

    // Case 3: Populating vAdamCoordinatorSig
    block.vAdamCoordinatorSig = std::vector<unsigned char>(71, 1);
    std::cout << "3. Populated vAdamCoordinatorSig:" << std::endl;
    std::cout << "   GetHash() = " << block.GetHash().ToString() << std::endl;
    
    CDataStream ss3(SER_GETHASH, PROTOCOL_VERSION);
    ss3 << block.GetBlockHeader();
    std::cout << "   Serialized Hex 3 (SER_GETHASH) = " << HexStr(ss3.begin(), ss3.end()) << std::endl;
    
    std::cout << "=== END DEBUG BLOCK HASHING ===\n" << std::endl;
}

BOOST_AUTO_TEST_CASE(adam_pow_limits_test)
{
    // Test GetZeroCoinPoWLimit for Mainnet
    uint256 mainZeroLimit = GetZeroCoinPoWLimit("main");
    BOOST_CHECK_EQUAL(mainZeroLimit.GetHex(), uint256S("000000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetZeroCoinPoWLimit for Testnet
    uint256 testZeroLimit = GetZeroCoinPoWLimit("test");
    BOOST_CHECK_EQUAL(testZeroLimit.GetHex(), uint256S("00007fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Test GetZeroCoinPoWLimit for Regtest
    uint256 regtestZeroLimit = GetZeroCoinPoWLimit("regtest");
    BOOST_CHECK_EQUAL(regtestZeroLimit.GetHex(), uint256S("00ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff").GetHex());

    // Verify GetZeroCoinPoWLimit is a larger target (fewer leading zero bits) than GetMinerPoWLimit (both legacy and V2)
    uint256 mainMinerLimit = GetMinerPoWLimit("main", false);
    BOOST_CHECK(mainZeroLimit > mainMinerLimit);
    uint256 mainZeroLimitV2 = GetZeroCoinPoWLimit("main", true);
    uint256 mainMinerLimitV2 = GetMinerPoWLimit("main", true);
    BOOST_CHECK(mainZeroLimitV2 > mainMinerLimitV2);
}

BOOST_AUTO_TEST_CASE(adam_pow_registration_script_matching_test)
{
    // Construct a valid PoW Registration script
    uint32_t nonce = 12345;
    uint256 challengeHash = uint256S("11223344556677889900aabbccddeeff11223344556677889900aabbccddeeff");
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    int64_t locktime = 5000;

    CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
    ssNonce << nonce;

    CScript scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                     << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                     << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                     << OP_DROP << OP_DROP << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    std::vector<unsigned char> nonceOut;
    uint256 challengeOut;
    CPubKey pubkeyOut;
    int64_t lockTimeOut = 0;
    CKeyID pubkeyHashOut;

    bool matchResult = MatchPoWLockRegistration(scriptPubKey, nonceOut, challengeOut, pubkeyOut, lockTimeOut, pubkeyHashOut);
    BOOST_CHECK(matchResult);
    BOOST_CHECK(pubkeyOut == pubkey);
    BOOST_CHECK_EQUAL(lockTimeOut, locktime);
    BOOST_CHECK(challengeOut == challengeHash);
    BOOST_CHECK(pubkeyHashOut == pubkey.GetID());
}

BOOST_AUTO_TEST_CASE(adam_zero_coin_pow_tx_validation_test)
{
    // Construct a Zero-Coin PoW Registration Transaction with Null Prevout
    uint32_t nonce = 67890;
    uint256 challengeHash = uint256S("aabbccddeeff11223344556677889900aabbccddeeff11223344556677889900");
    CKey key;
    key.MakeNewKey(true);
    CPubKey pubkey = key.GetPubKey();
    int64_t locktime = 6000;

    CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
    ssNonce << nonce;

    CScript scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                     << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                     << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                     << OP_DROP << OP_DROP << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;

    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.nLockTime = 0;

    // Sentinel input (zero-coin registration input with non-null dummy outpoint)
    CTxIn txin;
    txin.prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
    mtx.vin.push_back(txin);

    // Output with 0 value
    CTxOut txout(0, scriptPubKey);
    mtx.vout.push_back(txout);

    CTransaction tx(mtx);
    CValidationState state;

    // CheckTransaction must accept null prevouts for Zero-Coin PoW Registration tx
    bool checkRes = CheckTransaction(tx, state);
    BOOST_CHECK_MESSAGE(checkRes, "CheckTransaction should accept Zero-Coin PoW Registration transaction");
}

BOOST_AUTO_TEST_CASE(pow_block_signature_bypass_test)
{
    // Construct a dummy PoW block (IsProofOfStake() == false)
    CBlock powBlock;
    powBlock.nTime = Params().Checkpoints().nTimeLastCheckpoint + 1;
    powBlock.vtx.emplace_back(CTransaction()); // Only coinbase tx

    BOOST_CHECK(!powBlock.IsProofOfStake());
    BOOST_CHECK(powBlock.vchBlockSig.empty());

    // CheckBlockSignature for PoW blocks should return true when checked in ProcessNewBlock
    // even though vchBlockSig is empty.
    bool powSigResult = powBlock.IsProofOfStake() ? CheckBlockSignature(powBlock, true) : true;
    BOOST_CHECK(powSigResult);

    // For a PoS block without signature, CheckBlockSignature must fail.
    CMutableTransaction txCoinStake;
    txCoinStake.vin.emplace_back(CTxIn(uint256S("0x1"), 0));
    txCoinStake.vout.emplace_back(CTxOut(0, CScript())); // empty first vout marks coinstake
    txCoinStake.vout.emplace_back(CTxOut(10 * COIN, CScript() << OP_TRUE));

    CBlock posBlock;
    posBlock.nTime = Params().Checkpoints().nTimeLastCheckpoint + 1;
    posBlock.vtx.emplace_back(CTransaction()); // coinbase
    posBlock.vtx.emplace_back(txCoinStake); // coinstake

    BOOST_CHECK(posBlock.IsProofOfStake());
    BOOST_CHECK(posBlock.vchBlockSig.empty());
    BOOST_CHECK(!CheckBlockSignature(posBlock, true));
}

BOOST_AUTO_TEST_CASE(block_signature_malformed_p2pkh_test)
{
    CKey stakingKey;
    stakingKey.MakeNewKey(true);

    CMutableTransaction txCoinStake;
    txCoinStake.vin.emplace_back(CTxIn(uint256S("0x2"), 0));
    txCoinStake.vout.emplace_back(CTxOut(0, CScript()));
    txCoinStake.vout.emplace_back(CTxOut(10 * COIN, GetScriptForDestination(stakingKey.GetPubKey().GetID())));

    CBlock malformedBlock;
    malformedBlock.nTime = Params().Checkpoints().nTimeLastCheckpoint + 1;
    malformedBlock.vtx.emplace_back(CTransaction());
    malformedBlock.vtx.emplace_back(txCoinStake);
    malformedBlock.vchBlockSig = {0x30, 0x06}; // Non-empty dummy signature

    // Empty P2PKH scriptSig must return false safely
    BOOST_CHECK(!CheckBlockSignature(malformedBlock, true));

    // Malformed P2PKH scriptSig with oversized push length byte exceeding scriptSig bounds must not OOB-read
    CMutableTransaction txBadSig = txCoinStake;
    txBadSig.vin[0].scriptSig = CScript() << std::vector<unsigned char>(10, 0xaa);
    txBadSig.vin[0].scriptSig[0] = 0xff;
    malformedBlock.vtx[1] = CTransaction(txBadSig);
    BOOST_CHECK(!CheckBlockSignature(malformedBlock, true));

    // Reversed iterator range passed to CPubKey must yield an invalid key cleanly
    std::vector<unsigned char> buf(33, 0x02);
    CPubKey reversedKey;
    reversedKey.Set(buf.data() + 10, buf.data() + 2);
    BOOST_CHECK(!reversedKey.IsValid());
}

BOOST_AUTO_TEST_CASE(phase2_m3_m7_l1_regression_test)
{
    const CBaseChainParams::Network prevNet = Params().NetworkID();
    const CAmount savedSupply = nMoneySupply;
    CBlockIndex* const pOrigTip = chainActive.Tip();
    CSporkDB* const origSporkDb = pSporkDB;
    std::vector<uint256> vHashes;
    std::vector<CBlockIndex*> vIndices;
    struct Cleanup {
        CBaseChainParams::Network net;
        CAmount supply;
        CBlockIndex* tip;
        CSporkDB* sporkDb;
        std::vector<uint256>& hashes;
        std::vector<CBlockIndex*>& idxs;
        ~Cleanup() {
            UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, 1);
            nMoneySupply = supply;
            pSporkDB = sporkDb;
            chainActive.SetTip(tip);
            for (size_t i = 0; i < idxs.size(); ++i) {
                mapBlockIndex.erase(hashes[i]);
                delete idxs[i];
            }
            sporkManager.Clear();
            SelectParams(net);
        }
    } cleanup{prevNet, savedSupply, pOrigTip, origSporkDb, vHashes, vIndices};

    // 1. L-1: CMasternode::GetBlockValue under UPGRADE_HARDENING_V3 is height-pure and independent of global nMoneySupply
    nMoneySupply = Params().GetConsensus().nMaxMoneyOut + 1000 * COIN;
    BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2), 17 * COIN);
    BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(259202), 1663450000LL);
    {
        SelectParams(CBaseChainParams::REGTEST);
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
        nMoneySupply = Params().GetConsensus().nMaxMoneyOut;
        BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2), 0); // Pre-V3 still checked mutable nMoneySupply
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_HARDENING_V3, 1);
        SelectParams(CBaseChainParams::MAIN);
    }
    nMoneySupply = 0;
    BOOST_CHECK_EQUAL(CMasternode::GetBlockValue(2), 17 * COIN);

    // 2. M-3: GetStakeModifierV1 no overflow on short/4B vStakeModifier, SetNewStakeModifier(prevoutId) V2 256-bit determinism,
    // and CheckProofOfStake rejects missing V2 stake modifier under V3
    {
        CBlockIndex idxZero;
        uint256 hZero = GetRandHash();
        idxZero.phashBlock = &hZero;
        BOOST_CHECK_EQUAL(idxZero.GetStakeModifierV1(), 0ULL);
        idxZero.SetStakeModifier(0x0102030405060708ULL, true);
        BOOST_CHECK_EQUAL(idxZero.GetStakeModifierV1(), 0x0102030405060708ULL);

        CBlockIndex idxV2A, idxV2B;
        idxV2A.pprev = &idxZero;
        idxV2B.pprev = &idxZero;
        idxV2A.SetNewStakeModifier(uint256S("1111111111111111111111111111111111111111111111111111111111111111"));
        idxV2B.SetNewStakeModifier(uint256S("2222222222222222222222222222222222222222222222222222222222222222"));
        BOOST_CHECK(idxV2A.GeneratedStakeModifier());
        BOOST_CHECK(!idxV2A.GetStakeModifierV2().IsNull());
        BOOST_CHECK(idxV2A.GetStakeModifierV2() != idxV2B.GetStakeModifierV2());

        // Verify CheckProofOfStake fails with "missing-v2-stake-modifier" when pindexPrev has null V2 modifier under V3
        SelectParams(CBaseChainParams::REGTEST);
        BOOST_REQUIRE(pOrigTip != nullptr);
        CBlockIndex idxNoV2;
        idxNoV2.nHeight = 500;
        idxNoV2.pprev = pOrigTip;
        uint256 noV2Hash = GetRandHash();
        idxNoV2.phashBlock = &noV2Hash;
        idxNoV2.vStakeModifier.clear();
        BOOST_REQUIRE(idxNoV2.GetStakeModifierV2().IsNull());

        CKey sk;
        sk.MakeNewKey(true);
        const COutPoint stakePrevOut(GetRandHash(), 0);
        pcoinsTip->AddCoin(stakePrevOut, Coin(CTxOut(100 * COIN, GetScriptForDestination(sk.GetPubKey().GetID())), 0, false, false), true);
        CMutableTransaction mtxCs;
        mtxCs.vin.push_back(CTxIn(stakePrevOut));
        mtxCs.vout.push_back(CTxOut(0, CScript()));
        mtxCs.vout.push_back(CTxOut(101 * COIN, GetScriptForDestination(sk.GetPubKey().GetID())));
        CBlock posBlk;
        posBlk.nVersion = 12;
        posBlk.hashPrevBlock = noV2Hash;
        posBlk.nTime = pOrigTip->nTime + 30;
        posBlk.nBits = 0x207fffff;
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << 501 << OP_0));
        cb.vout.push_back(CTxOut(0, CScript()));
        posBlk.vtx.push_back(cb);
        posBlk.vtx.push_back(mtxCs);
        std::string posErr;
        BOOST_CHECK(!CheckProofOfStake(posBlk, posErr, &idxNoV2, pcoinsTip));
        BOOST_CHECK_EQUAL(posErr, "missing-v2-stake-modifier");
        pcoinsTip->SpendCoin(stakePrevOut);

        // M-3 Fork/Branch Isolation in ComputeNextStakeModifier (chainActive vs non-active fork)
        CBlock forkBlk;
        CBlockIndex* pFork1 = new CBlockIndex(forkBlk);
        pFork1->nHeight = pOrigTip->nHeight + 1;
        pFork1->pprev = pOrigTip;
        pFork1->SetStakeModifier(0x123456789abcdef0ULL, true);
        vHashes.push_back(GetRandHash());
        auto ins1 = mapBlockIndex.insert(std::make_pair(vHashes.back(), pFork1));
        pFork1->phashBlock = &(ins1.first->first);
        pFork1->BuildSkip();
        vIndices.push_back(pFork1);

        uint64_t nextMod = 0;
        bool fGenMod = false;
        BOOST_CHECK(ComputeNextStakeModifier(pFork1, nextMod, fGenMod));
    }

    // 3. M-7: Spork mutex/DB safety & SPORK_105_MAX_BLOCK_SIZE ignored after UPGRADE_HARDENING_V3
    {
        SelectParams(CBaseChainParams::REGTEST);
        CSporkDB memSporkDb(0, true, false);
        pSporkDB = &memSporkDb;
        CSporkMessage corrupt(SPORK_105_MAX_BLOCK_SIZE, 1000, GetTime());
        corrupt.SetVchSig({0x01, 0x02, 0x03});
        BOOST_REQUIRE(pSporkDB->WriteSpork(SPORK_105_MAX_BLOCK_SIZE, corrupt));
        sporkManager.Clear();
        sporkManager.LoadSporksFromDB();
        // Corrupt spork entry must NOT be loaded as active
        BOOST_CHECK(!sporkManager.HaveSpork(corrupt.GetHash()));

        // ProcessSpork ignores SPORK_105_MAX_BLOCK_SIZE after UPGRADE_HARDENING_V3
        CBlockIndex* pRecent = new CBlockIndex();
        pRecent->nHeight = 10;
        pRecent->pprev = pOrigTip;
        pRecent->nTime = static_cast<unsigned int>(GetTime());
        vHashes.push_back(GetRandHash());
        auto ins = mapBlockIndex.insert(std::make_pair(vHashes.back(), pRecent));
        pRecent->phashBlock = &(ins.first->first);
        vIndices.push_back(pRecent);
        chainActive.SetTip(pRecent);

        CSporkMessage p2pSpork105(SPORK_105_MAX_BLOCK_SIZE, 262144, GetTime());
        CDataStream ssSpk(SER_NETWORK, PROTOCOL_VERSION);
        ssSpk << p2pSpork105;
        std::string cmd = NetMsgType::SPORK;
        sporkManager.ProcessSpork(nullptr, cmd, ssSpk);
        BOOST_CHECK(!sporkManager.HaveSpork(p2pSpork105.GetHash()));
    }

    // 4. V4 Stage 1 regressions: C-1 out-of-order StakeModifier guard, H-3 PoW coinstake ban, H-1/M-6 overflow guards
    {
        SelectParams(CBaseChainParams::REGTEST);
        BOOST_REQUIRE(pOrigTip != nullptr);

        // C-1: SetNewStakeModifier must refuse computing V2 modifier when parent V2 modifier is missing
        CBlockIndex unlinkedParent;
        unlinkedParent.nHeight = 5;
        unlinkedParent.pprev = pOrigTip;
        uint256 unlinkedHash = GetRandHash();
        unlinkedParent.phashBlock = &unlinkedHash;
        unlinkedParent.vStakeModifier.clear();
        unlinkedParent.nFlags = 0;

        CBlockIndex unlinkedChild;
        unlinkedChild.nHeight = 6;
        unlinkedChild.pprev = &unlinkedParent;
        uint256 childHash = GetRandHash();
        unlinkedChild.phashBlock = &childHash;
        unlinkedChild.SetNewStakeModifier(childHash);
        BOOST_CHECK(!unlinkedChild.GeneratedStakeModifier());
        BOOST_CHECK(unlinkedChild.GetStakeModifierV2().IsNull());

        // Once parent receives its valid V2 modifier, child computes deterministically
        unlinkedParent.SetNewStakeModifier(unlinkedHash);
        BOOST_REQUIRE(unlinkedParent.GeneratedStakeModifier());
        unlinkedChild.SetNewStakeModifier(childHash);
        BOOST_CHECK(unlinkedChild.GeneratedStakeModifier());
        BOOST_CHECK(!unlinkedChild.GetStakeModifierV2().IsNull());

        // H-3: PoW block carrying an IsCoinStake() transaction at vtx[2] must be rejected with bad-cs-in-pow
        CMutableTransaction cb;
        cb.vin.push_back(CTxIn(COutPoint(), CScript() << 10 << OP_0));
        cb.vout.push_back(CTxOut(50 * COIN, CScript() << OP_TRUE));
        CMutableTransaction normalTx;
        normalTx.vin.push_back(CTxIn(COutPoint(GetRandHash(), 0)));
        normalTx.vout.push_back(CTxOut(10 * COIN, CScript() << OP_TRUE));
        CMutableTransaction fakeCs;
        fakeCs.vin.push_back(CTxIn(COutPoint(GetRandHash(), 1)));
        fakeCs.vout.push_back(CTxOut(0, CScript())); // Empty vout[0] makes IsCoinStake() true
        fakeCs.vout.push_back(CTxOut(20 * COIN, CScript() << OP_TRUE));
        BOOST_REQUIRE(CTransaction(fakeCs).IsCoinStake());

        CBlock powWithCs;
        powWithCs.nVersion = 12;
        powWithCs.hashPrevBlock = pOrigTip->GetBlockHash();
        powWithCs.nTime = pOrigTip->nTime + 30;
        powWithCs.nBits = pOrigTip->nBits;
        powWithCs.vtx = {CTransaction(cb), CTransaction(normalTx), CTransaction(fakeCs)};
        powWithCs.hashMerkleRoot = BlockMerkleRoot(powWithCs);
        BOOST_REQUIRE(!powWithCs.IsProofOfStake());
        CValidationState stPowCs;
        BOOST_CHECK(!CheckBlock(powWithCs, stPowCs, false, true, false));
        BOOST_CHECK_EQUAL(stPowCs.GetRejectReason(), "bad-cs-in-pow");

        // H-1: Huge nTimeElapsed must not wrap nRelaxSteps negative inside GetAdamEffectiveThreshold
        const int extremeThreshold = Params(CBaseChainParams::MAIN).GetConsensus().GetAdamEffectiveThreshold(10000, 1000000000000LL, 8);
        BOOST_CHECK_EQUAL(extremeThreshold, 8);

        // M-6: CTransaction::GetValueOut() overflow protection throws std::runtime_error cleanly
        CMutableTransaction overflowTx;
        overflowTx.vout.push_back(CTxOut(std::numeric_limits<CAmount>::max() - 10, CScript() << OP_TRUE));
        overflowTx.vout.push_back(CTxOut(20, CScript() << OP_TRUE));
        BOOST_CHECK_THROW(CTransaction(overflowTx).GetValueOut(), std::runtime_error);
    }
}

BOOST_AUTO_TEST_SUITE_END()
