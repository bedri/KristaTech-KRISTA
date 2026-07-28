// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "base58.h"
#include "chainparams.h"
#include "core_io.h"
#include "init.h"
#include "main.h"
#include "miner.h"
#include "masternodeman.h"
#include "messagesigner.h"

#include "adam.h"
#include "crypto/bls.h"
#include "key_io.h"
#include "net.h"
#include "pow.h"
#include "spork.h"
#include "netmessagemaker.h"
#include "rpc/server.h"
#include "util.h"
#include "validationinterface.h"
#ifdef ENABLE_WALLET
#include "wallet/db.h"
#include "wallet/wallet.h"
#endif

#include <stdint.h>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

static CKeyID GetCompressedKeyID(const CPubKey& pubkey) {
    if (pubkey.IsCompressed()) return pubkey.GetID();
    if (pubkey.size() == 65) {
        unsigned char comp_vch[33];
        comp_vch[0] = (pubkey[64] % 2 == 0) ? 0x02 : 0x03;
        memcpy(comp_vch + 1, pubkey.begin() + 1, 32);
        return CPubKey(comp_vch, comp_vch + 33).GetID();
    }
    return pubkey.GetID();
}

static bool ComparePubKeys(const CPubKey& pk1, const CPubKey& pk2) {
    if (pk1 == pk2) return true;
    if (!pk1.IsValid() || !pk2.IsValid()) return false;
    return CXOnlyPubKey(pk1) == CXOnlyPubKey(pk2);
}


/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or from the last difficulty change if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
UniValue GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (pb == NULL || !pb->nHeight)
        return 0;

    // If lookup is -1, then use blocks since last difficulty change.
    if (lookup <= 0)
        lookup = pb->nHeight % 2016 + 1;

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex* pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return (int64_t)(workDiff.getdouble() / timeDiff);
}

UniValue getnetworkhashps(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2)
        throw std::runtime_error(
            "getnetworkhashps ( blocks height )\n"
            "\nReturns the estimated network hashes per second based on the last n blocks.\n"
            "Pass in [blocks] to override # of blocks, -1 specifies since last difficulty change.\n"
            "Pass in [height] to estimate the network speed at the time when a certain block was found.\n"

            "\nArguments:\n"
            "1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks since last difficulty change.\n"
            "2. height     (numeric, optional, default=-1) To estimate at the time of the given height.\n"

            "\nResult:\n"
            "x             (numeric) Hashes per second estimated\n"
            "\nExamples:\n" +
            HelpExampleCli("getnetworkhashps", "") + HelpExampleRpc("getnetworkhashps", ""));

    LOCK(cs_main);
    return GetNetworkHashPS(request.params.size() > 0 ? request.params[0].get_int() : 120, request.params.size() > 1 ? request.params[1].get_int() : -1);
}

#ifdef ENABLE_WALLET
UniValue getgenerate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getgenerate\n"
            "\nReturn if the server is set to generate coins or not. The default is false.\n"
            "It is set with the command line argument -gen (or kristatech.conf setting gen)\n"
            "It can also be set with the setgenerate call.\n"

            "\nResult\n"
            "true|false      (boolean) If the server is set to generate coins or not\n"

            "\nExamples:\n" +
            HelpExampleCli("getgenerate", "") + HelpExampleRpc("getgenerate", ""));

    LOCK(cs_main);
    return GetBoolArg("-gen", false);
}

UniValue generate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 1)
        throw std::runtime_error(
            "generate numblocks\n"
            "\nMine blocks immediately (before the RPC call returns)\n"

            "\nArguments:\n"
            "1. numblocks    (numeric, required) How many blocks to generate.\n"

            "\nResult\n"
            "[ blockhashes ]     (array) hashes of blocks generated\n"

            "\nExamples:\n"
            "\nGenerate 11 blocks\n"
            + HelpExampleCli("generate", "11")
        );

    const int nGenerate = request.params[0].get_int();
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = chainActive.Height();
        nHeightEnd = nHeight + nGenerate;
    }

    const Consensus::Params& consensus = Params().GetConsensus();
    bool fPoS = consensus.NetworkUpgradeActive(nHeight + 1, Consensus::UPGRADE_POS);

    if (fPoS) {
        // If we are in PoS, wallet must be unlocked.
        EnsureWalletIsUnlocked();
    }

    UniValue blockHashes(UniValue::VARR);
    CReserveKey reservekey(pwalletMain);
    unsigned int nExtraNonce = 0;

    while (nHeight < nHeightEnd && !ShutdownRequested()) {

        // Get available coins
        std::vector<COutput> availableCoins;
        if (fPoS && !pwalletMain->StakeableCoins(&availableCoins)) {
            throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "No available coins to stake");
        }

        std::unique_ptr<CBlockTemplate> pblocktemplate(fPoS ?
                                                       CreateNewBlock(CScript(), pwalletMain, true, &availableCoins) :
                                                       CreateNewBlockWithKey(reservekey, pwalletMain));
        if (!pblocktemplate.get()) break;
        CBlock *pblock = &pblocktemplate->block;

        if(!fPoS) {
            if (pblock->nVersion < 11) {
                LOCK(cs_main);
                IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
            }
            if (pblock->nVersion >= 11) {
                // For ADAM blocks, sign the block with the coordinator's key
                uint256 adamSeed = GetAdamSeed(chainActive.Tip());
                std::vector<CPubKey> vExpectedMiners;
                CPubKey expectedCoordinator;
                if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                    CKey coordKey;
                    bool gotKey = false;
                    if (pwalletMain && pwalletMain->GetKey(expectedCoordinator.GetID(), coordKey)) {
                        gotKey = true;
                    } else {
                        for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                            if (activeMasternode.pubKeyMasternode == expectedCoordinator) {
                                CKey key;
                                CPubKey pubkey;
                                if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, key, pubkey)) {
                                    coordKey = key;
                                    gotKey = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (!gotKey && Params().IsRegTestNet()) {
                        for (int i = 0; i < 15; ++i) {
                            if (GetAdamDeterministicPubKey(i) == expectedCoordinator) {
                                coordKey = GetAdamDeterministicKey(i);
                                gotKey = true;
                                break;
                            }
                        }
                    }
                    if (gotKey && coordKey.IsValid()) {
                        CBLSSecretKey blsKey;
#ifdef ENABLE_WALLET
                        if (pwalletMain) {
                            LOCK(pwalletMain->cs_wallet);
                            pwalletMain->GetBLSKey(expectedCoordinator.GetID(), blsKey);
                        }
#endif
                        if (!blsKey.IsValid()) {
                            for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                                if (activeMasternode.pubKeyMasternode == expectedCoordinator && activeMasternode.blsKeyMasternode.IsValid()) {
                                    blsKey = activeMasternode.blsKeyMasternode;
                                    break;
                                }
                            }
                        }
                        if (blsKey.IsValid()) {
                            if (!SignBLSWithECDSAFallback(adamSeed, coordKey, blsKey, pblock->vAdamVRFProof)) {
                                LogPrintf("generate RPC: Failed to sign VRF proof as coordinator\n");
                            }
                            if (!SignBLSWithECDSAFallback(pblock->GetHash(), coordKey, blsKey, pblock->vAdamCoordinatorSig)) {
                                LogPrintf("generate RPC: Failed to sign ADAM block as coordinator\n");
                            } else {
                                LogPrintf("generate RPC: Signed ADAM block as coordinator, hash: %s\n", pblock->GetHash().ToString());
                            }
                        } else {
                            LogPrintf("generate RPC ERROR: Cannot sign because no direct BLS key is associated with expected coordinator %s\n", expectedCoordinator.GetID().ToString());
                        }
                    }
                }
            } else {
                while (pblock->nNonce < std::numeric_limits<uint32_t>::max() &&
                        !CheckProofOfWork(pblock->GetHash(), pblock->nBits)) {
                    ++pblock->nNonce;
                }
                if (ShutdownRequested()) break;
                if (pblock->nNonce == std::numeric_limits<uint32_t>::max()) continue;
            }
        }

        CValidationState state;
        if (!ProcessNewBlock(state, nullptr, pblock, nullptr, g_connman.get()))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");

        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        // Check PoS if needed.
        if (!fPoS)
            fPoS = consensus.NetworkUpgradeActive(nHeight + 1, Consensus::UPGRADE_POS);
    }

    const int nGenerated = blockHashes.size();
    if (nGenerated == 0 || (!fPoS && nGenerated < nGenerate))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Couldn't create new blocks");

    return blockHashes;
}

UniValue setgenerate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "setgenerate generate ( genproclimit )\n"
            "\nSet 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is unlimited.\n"
            "See the getgenerate call for the current setting.\n"

            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.\n"

            "\nExamples:\n"
            "\nSet the generation on with a limit of one processor\n" +
            HelpExampleCli("setgenerate", "true 1") +
            "\nCheck the setting\n" + HelpExampleCli("getgenerate", "") +
            "\nTurn off generation\n" + HelpExampleCli("setgenerate", "false") +
            "\nUsing json rpc\n" + HelpExampleRpc("setgenerate", "true, 1"));

    if (pwalletMain == NULL)
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");

    if (Params().IsRegTestNet())
        throw JSONRPCError(RPC_INVALID_REQUEST, "Use the generate method instead of setgenerate on regtest");

    bool fGenerate = true;
    if (request.params.size() > 0)
        fGenerate = request.params[0].get_bool();


    int nGenProcLimit = -1;
    if (request.params.size() > 1) {
        nGenProcLimit = request.params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs["-genproclimit"] = itostr(nGenProcLimit);
    GenerateBitcoins(fGenerate, pwalletMain, nGenProcLimit);

    return NullUniValue;
}

UniValue gethashespersec(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "gethashespersec\n"
            "\nReturns a recent hashes per second performance measurement while generating.\n"
            "See the getgenerate and setgenerate calls to turn generation on and off.\n"

            "\nResult:\n"
            "n            (numeric) The recent hashes per second when generation is on (will return 0 if generation is off)\n"

            "\nExamples:\n" +
            HelpExampleCli("gethashespersec", "") + HelpExampleRpc("gethashespersec", ""));

    if (GetTimeMillis() - nHPSTimerStart > 8000)
        return (int64_t)0;
    return (int64_t)dHashesPerSec;
}
#endif


UniValue getmininginfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0)
        throw std::runtime_error(
            "getmininginfo\n"
            "\nReturns a json object containing mining-related information."

            "\nResult:\n"
            "{\n"
            "  \"blocks\": nnn,             (numeric) The current block\n"
            "  \"currentblocksize\": nnn,   (numeric) The last block size\n"
            "  \"currentblocktx\": nnn,     (numeric) The last block transaction\n"
            "  \"difficulty\": xxx.xxxxx    (numeric) The current difficulty\n"
            "  \"errors\": \"...\"          (string) Current errors\n"
            "  \"generate\": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate calls)\n"
            "  \"genproclimit\": n          (numeric) The processor limit for generation. -1 if no generation. (see getgenerate or setgenerate calls)\n"
            "  \"hashespersec\": n          (numeric) The hashes per second of the generation, or 0 if no generation.\n"
            "  \"pooledtx\": n              (numeric) The size of the mem pool\n"
            "  \"testnet\": true|false      (boolean) If using testnet or not\n"
            "  \"chain\": \"xxxx\",         (string) current network name as defined in BIP70 (main, test, regtest)\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getmininginfo", "") + HelpExampleRpc("getmininginfo", ""));

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.push_back(Pair("blocks", (int)chainActive.Height()));
    obj.push_back(Pair("currentblocksize", (uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx", (uint64_t)nLastBlockTx));
    obj.push_back(Pair("difficulty", (double)GetDifficulty()));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));
    obj.push_back(Pair("genproclimit", (int)GetArg("-genproclimit", -1)));
    obj.push_back(Pair("networkhashps", getnetworkhashps(request)));
    obj.push_back(Pair("pooledtx", (uint64_t)mempool.size()));
    obj.push_back(Pair("testnet", Params().NetworkID() == CBaseChainParams::TESTNET));
    obj.push_back(Pair("chain", Params().NetworkIDString()));
#ifdef ENABLE_WALLET
    obj.push_back(Pair("generate", getgenerate(request)));
    obj.push_back(Pair("hashespersec", gethashespersec(request)));
#endif
    return obj;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using satoshi amounts
UniValue prioritisetransaction(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 3)
        throw std::runtime_error(
            "prioritisetransaction <txid> <priority delta> <fee delta>\n"
            "Accepts the transaction into mined blocks at a higher (or lower) priority\n"

            "\nArguments:\n"
            "1. \"txid\"       (string, required) The transaction id.\n"
            "2. priority delta (numeric, required) The priority to add or subtract.\n"
            "                  The transaction selection algorithm considers the tx as it would have a higher priority.\n"
            "                  (priority of a transaction is calculated: coinage * value_in_uKRISTA / txsize) \n"
            "3. fee delta      (numeric, required) The fee value (in uKRISTA) to add (or subtract, if negative).\n"
            "                  The fee is not actually paid, only the algorithm for selecting transactions into a block\n"
            "                  considers the transaction as it would have paid a higher (or lower) fee.\n"

            "\nResult\n"
            "true              (boolean) Returns true\n"

            "\nExamples:\n" +
            HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000") + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000"));

    LOCK(cs_main);

    uint256 hash = ParseHashStr(request.params[0].get_str(), "txid");
    CAmount nAmount = request.params[2].get_int64();

    mempool.PrioritiseTransaction(hash, request.params[0].get_str(), request.params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid()) {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

UniValue getblocktemplate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1)
        throw std::runtime_error(
            "getblocktemplate ( \"jsonrequestobject\" )\n"
            "\nIf the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.\n"
            "It returns data needed to construct a block to work on.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments:\n"
            "1. \"jsonrequestobject\"       (string, optional) A json object in the following spec\n"
            "     {\n"
            "       \"mode\":\"template\"    (string, optional) This must be set to \"template\" or omitted\n"
            "       \"capabilities\":[       (array, optional) A list of strings\n"
            "           \"support\"           (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'\n"
            "           ,...\n"
            "         ]\n"
            "     }\n"
            "\n"

            "\nResult:\n"
            "{\n"
            "  \"version\" : n,                    (numeric) The block version\n"
            "  \"previousblockhash\" : \"xxxx\",    (string) The hash of current highest block\n"
            "  \"transactions\" : [                (array) contents of non-coinbase transactions that should be included in the next block\n"
            "      {\n"
            "         \"data\" : \"xxxx\",          (string) transaction data encoded in hexadecimal (byte-for-byte)\n"
            "         \"hash\" : \"xxxx\",          (string) hash/id encoded in little-endian hexadecimal\n"
            "         \"depends\" : [              (array) array of numbers \n"
            "             n                        (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is\n"
            "             ,...\n"
            "         ],\n"
            "         \"fee\": n,                   (numeric) difference in value between transaction inputs and outputs (in uKRISTA); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one\n"
            "         \"sigops\" : n,               (numeric) total number of SigOps, as counted for purposes of block limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any\n"
            "         \"required\" : true|false     (boolean) if provided and true, this transaction must be in the final block\n"
            "      }\n"
            "      ,...\n"
            "  ],\n"
            "  \"coinbaseaux\" : {                  (json object) data that should be included in the coinbase's scriptSig content\n"
            "      \"flags\" : \"flags\"            (string) \n"
            "  },\n"
            "  \"coinbasevalue\" : n,               (numeric) maximum allowable input to coinbase transaction, including the generation award and transaction fees (in uKRISTA)\n"
            "  \"coinbasetxn\" : { ... },           (json object) information for coinbase transaction\n"
            "  \"target\" : \"xxxx\",               (string) The hash target\n"
            "  \"mintime\" : xxx,                   (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"mutable\" : [                      (array of string) list of ways the block template may be changed \n"
            "     \"value\"                         (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'\n"
            "     ,...\n"
            "  ],\n"
            "  \"noncerange\" : \"00000000ffffffff\",   (string) A range of valid nonces\n"
            "  \"sigoplimit\" : n,                 (numeric) limit of sigops in blocks\n"
            "  \"sizelimit\" : n,                  (numeric) limit of block size\n"
            "  \"curtime\" : ttt,                  (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)\n"
            "  \"bits\" : \"xxx\",                 (string) compressed target of next block\n"
            "  \"height\" : n                      (numeric) The height of the next block\n"
            "  \"payee\" : \"xxx\",                (string) required payee for the next block\n"
            "  \"payee_amount\" : n,               (numeric) required amount to pay\n"
            "  \"votes\" : [\n                     (array) show vote candidates\n"
            "        { ... }                       (json object) vote candidate\n"
            "        ,...\n"
            "  ],\n"
            "  \"enforce_masternode_payments\" : true|false  (boolean) true, if masternode payments are enforced\n"
            "}\n"

            "\nExamples:\n" +
            HelpExampleCli("getblocktemplate", "") + HelpExampleRpc("getblocktemplate", ""));

    LOCK(cs_main);

    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    if (request.params.size() > 0) {
        const UniValue& oparam = request.params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull()) {
            /* Do nothing */
        } else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal") {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end()) {
                CBlockIndex* pindex = mi->second;
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";
            CValidationState state;
            TestBlockValidity(state, block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    CBlockIndex* pindexPrevTmp = chainActive.Tip();
    if (pindexPrevTmp && IsAdamActive(pindexPrevTmp->nHeight + 1, Params().GetConsensus())) {
        uint256 adamSeed = GetAdamSeed(pindexPrevTmp);
        const Consensus::Params& consensus = Params().GetConsensus();
        std::vector<CPubKey> vExpectedMiners;
        CPubKey expectedCoordinator;
        if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
            int minerIdx = -1;
            CPubKey myMinerKey;
            for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                bool hasKey = false;
#ifdef ENABLE_WALLET
                if (pwalletMain && pwalletMain->HaveKey(vExpectedMiners[i].GetID())) {
                    hasKey = true;
                }
#endif
                if (!hasKey && Params().IsRegTestNet()) {
                    for (int k = 0; k < 15; ++k) {
                        if (GetAdamDeterministicPubKey(k) == vExpectedMiners[i]) {
                            hasKey = true;
                            break;
                        }
                    }
                }
                if (hasKey) {
                    bool alreadySolved = false;
                    {
                        LOCK(cs_adam_solutions);
                        auto it = mapAdamSolutionsCache.find(pindexPrevTmp->GetBlockHash());
                        if (it != mapAdamSolutionsCache.end()) {
                            if (it->second.count(vExpectedMiners[i])) {
                                alreadySolved = true;
                            }
                        }
                    }
                    if (!alreadySolved) {
                        minerIdx = i;
                        myMinerKey = vExpectedMiners[i];
                        break;
                    }
                }
            }

            bool fV12 = consensus.NetworkUpgradeActive(pindexPrevTmp->nHeight + 1, Consensus::UPGRADE_POMBL);
            int algoIndex = 12;
            int algo1 = -1, algo2 = -1, algo3 = -1;
            if (minerIdx >= 0) {
                if (!fV12) {
                    algoIndex = GetAdamPuzzleAlgo(adamSeed, myMinerKey, true);
                } else {
                    GetAdam3PermutationAlgos(pindexPrevTmp->GetBlockHash(), myMinerKey, algo1, algo2, algo3);
                }

                CBlockHeader dummyHeader;
                int nNextHeight = pindexPrevTmp->nHeight + 1;
                if (consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
                    dummyHeader.nVersion = 12;
                } else {
                    dummyHeader.nVersion = 11;
                }
                unsigned int nBits = GetNextWorkRequired(pindexPrevTmp, &dummyHeader);
                uint256 bnTarget = uint256().SetCompact(nBits);
                uint256 scaledTarget = bnTarget;
                if (!Params().IsRegTestNet()) {
                    bool fFallbackMode = (dummyHeader.nVersion == 11) || !IsModelDActive(nNextHeight) || (IsModelDActive(nNextHeight) && mnodeman.CountEnabled() < 11);
                    int shift = fFallbackMode ? consensus.nAdamDifficultyShiftV1 :
                                ((consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_ADAM_V2)) ? 
                                 consensus.nAdamDifficultyShiftV2 : consensus.nAdamDifficultyShiftV1);
                    scaledTarget = bnTarget << shift;
                    uint256 powLimit = consensus.powLimit;
                    if (scaledTarget > powLimit || scaledTarget < bnTarget) {
                        scaledTarget = powLimit;
                    }
                } else {
                    scaledTarget = ~UINT256_ZERO;
                }

                CDataStream ssInput(SER_GETHASH, 0);
                ssInput << adamSeed;
                ssInput << myMinerKey;

                std::vector<unsigned char> puzzleHeader(80, 0);
                memcpy(&puzzleHeader[0], &ssInput[0], ssInput.size());

                UniValue aCaps(UniValue::VARR);
                UniValue result(UniValue::VOBJ);
                result.push_back(Pair("capabilities", aCaps));
                result.push_back(Pair("version", dummyHeader.nVersion));
                result.push_back(Pair("previousblockhash", pindexPrevTmp->GetBlockHash().GetHex()));
                result.push_back(Pair("transactions", UniValue(UniValue::VARR)));
                result.push_back(Pair("target", scaledTarget.GetHex()));
                result.push_back(Pair("bits", strprintf("%08x", nBits)));
                result.push_back(Pair("height", (int64_t)nNextHeight));
                result.push_back(Pair("curtime", (int64_t)GetTime()));
                result.push_back(Pair("puzzleheader", HexStr(puzzleHeader.begin(), puzzleHeader.end())));
                std::string powalgo = !fV12 ? GetAdamPuzzleAlgoName(algoIndex) : (GetAdamPuzzleAlgoName(algo1) + "+" + GetAdamPuzzleAlgoName(algo2) + "+" + GetAdamPuzzleAlgoName(algo3) + "+" + std::to_string(minerIdx));
                result.push_back(Pair("powalgo", powalgo));
                return result;
            }
        }
    }

    if(!g_connman)
        throw JSONRPCError(RPC_CLIENT_P2P_DISABLED, "Error: Peer-to-peer functionality missing or disabled");

    // if (g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0)
    //     throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "KRISTA is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "KRISTA is downloading blocks...");

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull()) {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        std::chrono::steady_clock::time_point checktxtime;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr()) {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = atoi64(lpstr.substr(64));
        } else {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            checktxtime = std::chrono::steady_clock::now() + std::chrono::minutes(1);

            WAIT_LOCK(g_best_block_mutex, lock);
            while (g_best_block == hashWatchedChain && IsRPCRunning()) {
                if (g_best_block_cv.wait_until(lock, checktxtime) == std::cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += std::chrono::seconds(10);
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlockTemplate* pblocktemplate;
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5)) {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = NULL;

        // Store the chainActive.Tip() used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        // Create new block
        if (pblocktemplate) {
            delete pblocktemplate;
            pblocktemplate = NULL;
        }
        CScript scriptDummy = CScript() << OP_TRUE;
        LEAVE_CRITICAL_SECTION(cs_main);
        pblocktemplate = CreateNewBlock(scriptDummy, pwalletMain, false);
        ENTER_CRITICAL_SECTION(cs_main);
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience

    // Update nTime
    UpdateTime(pblock, pindexPrev);
    pblock->nNonce = 0;

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue transactions(UniValue::VARR);
    std::map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (CTransaction& tx : pblock->vtx) {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase())
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.push_back(Pair("data", EncodeHexTx(tx)));

        entry.push_back(Pair("hash", txHash.GetHex()));

        UniValue deps(UniValue::VARR);
        for (const CTxIn& in : tx.vin) {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.push_back(Pair("depends", deps));

        int index_in_template = i - 1;
        entry.push_back(Pair("fee", pblocktemplate->vTxFees[index_in_template]));
        entry.push_back(Pair("sigops", pblocktemplate->vTxSigOps[index_in_template]));

        transactions.push_back(entry);
    }

    UniValue aux(UniValue::VOBJ);
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    uint256 hashTarget = uint256().SetCompact(pblock->nBits);

    static UniValue aMutable(UniValue::VARR);
    if (aMutable.empty()) {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue aVotes(UniValue::VARR);

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("capabilities", aCaps));
    result.push_back(Pair("version", pblock->nVersion));
    result.push_back(Pair("previousblockhash", pblock->hashPrevBlock.GetHex()));
    result.push_back(Pair("transactions", transactions));
    result.push_back(Pair("coinbaseaux", aux));
    result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0].GetValueOut()));
    result.push_back(Pair("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast)));
    result.push_back(Pair("target", hashTarget.GetHex()));
    result.push_back(Pair("mintime", (int64_t)pindexPrev->GetMedianTimePast() + 1));
    result.push_back(Pair("mutable", aMutable));
    result.push_back(Pair("noncerange", "00000000ffffffff"));
//    result.push_back(Pair("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS));
//    result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_SIZE));
    result.push_back(Pair("curtime", pblock->GetBlockTime()));
    result.push_back(Pair("bits", strprintf("%08x", pblock->nBits)));
    result.push_back(Pair("height", (int64_t)(pindexPrev->nHeight + 1)));
    result.push_back(Pair("votes", aVotes));
    result.push_back(Pair("enforce_masternode_payments", true));

    if (pblock->nVersion >= 11) {
        UniValue miners(UniValue::VARR);
        for (const auto& key : pblock->vAdamMiners) {
            miners.push_back(HexStr(key.begin(), key.end()));
        }
        result.push_back(Pair("adamminers", miners));

        UniValue solutions(UniValue::VARR);
        for (const auto& sol : pblock->vAdamSolutions) {
            solutions.push_back(HexStr(sol.begin(), sol.end()));
        }
        result.push_back(Pair("adamsolutions", solutions));

        result.push_back(Pair("adamvrfproof", HexStr(pblock->vAdamVRFProof.begin(), pblock->vAdamVRFProof.end())));
        result.push_back(Pair("adamcoordinatorsig", HexStr(pblock->vAdamCoordinatorSig.begin(), pblock->vAdamCoordinatorSig.end())));
    }

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256& hashIn) : hash(hashIn), found(false), state(){};

protected:
    virtual void BlockChecked(const CBlock& block, const CValidationState& stateIn)
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue submitblock(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "submitblock \"hexdata\" ( \"jsonparametersobject\" )\n"
            "\nAttempts to submit new block to network.\n"
            "The 'jsonparametersobject' parameter is currently ignored.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.\n"

            "\nArguments\n"
            "1. \"hexdata\"    (string, required) the hex-encoded block data to submit\n"
            "2. \"jsonparametersobject\"     (string, optional) object of optional parameters\n"
            "    {\n"
            "      \"workid\" : \"id\"    (string, optional) if the server provided a workid, it MUST be included with submissions\n"
            "    }\n"

            "\nResult:\n"

            "\nExamples:\n" +
            HelpExampleCli("submitblock", "\"mydata\"") + HelpExampleRpc("submitblock", "\"mydata\""));

    std::string hexData = request.params[0].get_str();
    if (hexData.size() == 160) {
        std::vector<unsigned char> puzData = ParseHex(hexData);
        uint256 adamSeed;
        memcpy(adamSeed.begin(), &puzData[0], 32);
        
        CPubKey minerKey;
        minerKey.Set(&puzData[33], &puzData[33] + 33);
        
        uint32_t nNonce;
        memcpy(&nNonce, &puzData[76], 4);

        CKey privKey;
#ifdef ENABLE_WALLET
        if (pwalletMain && (pwalletMain->GetKey(minerKey.GetID(), privKey) ||
                            pwalletMain->GetKey(GetCompressedKeyID(minerKey), privKey))) {
            // Found key in wallet
        } else {
            for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                if (ComparePubKeys(activeMasternode.pubKeyMasternode, minerKey)) {
                    CKey key;
                    CPubKey pubkey;
                    if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, key, pubkey)) {
                        privKey = key;
                        break;
                    }
                }
            }
        }
#endif
        if (!privKey.IsValid() && Params().IsRegTestNet()) {
            for (int k = 0; k < 15; ++k) {
                if (ComparePubKeys(GetAdamDeterministicPubKey(k), minerKey)) {
                    privKey = GetAdamDeterministicKey(k);
                    break;
                }
            }
        }

        if (!privKey.IsValid()) {
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Private key for miner not found");
        }

        CDataStream ssInput(SER_GETHASH, 0);
        ssInput << adamSeed;
        ssInput << minerKey;
        ssInput << nNonce;

        CBlockIndex* pindexPrev = chainActive.Tip();
        bool fV12 = pindexPrev && Params().GetConsensus().NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POMBL);
        int algoIndex = 12;
        int algo1 = -1, algo2 = -1, algo3 = -1;
        int minerIdx = -1;
        if (pindexPrev) {
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            if (SelectAdamNodes(adamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator)) {
                for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                    if (vExpectedMiners[i] == minerKey) {
                        minerIdx = i;
                        break;
                    }
                }
                if (minerIdx >= 0) {
                    if (!fV12) {
                        algoIndex = GetAdamPuzzleAlgo(adamSeed, minerKey, true);
                    } else {
                        GetAdam3PermutationAlgos(pindexPrev->GetBlockHash(), minerKey, algo1, algo2, algo3);
                    }
                }
            }
        }

        uint256 puzzleHash;
        if (!fV12) {
            puzzleHash = CalculateAdamPuzzleHash(algoIndex, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
        } else {
            if (minerIdx < 0) {
                throw JSONRPCError(RPC_VERIFY_ERROR, "Miner was not elected for this block height");
            }
            uint256 hash3 = CalculateAdamPuzzleHash(algo3, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
            int i_factor = minerIdx + 1;
            arith_uint256 val1 = UintToArith256(hash3) * i_factor;
            uint256 multiplied1 = ArithToUint256(val1);
            
            uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
            arith_uint256 val2 = UintToArith256(hash2) * i_factor;
            uint256 multiplied2 = ArithToUint256(val2);
            
            puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
        }

        CBlockHeader dummyHeader;
        int nNextHeight = pindexPrev ? pindexPrev->nHeight + 1 : 0;
        if (Params().GetConsensus().NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL)) {
            dummyHeader.nVersion = 12;
        } else {
            dummyHeader.nVersion = 11;
        }
        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
        uint256 bnTarget = uint256().SetCompact(nBits);
        uint256 scaledTarget = bnTarget;
        if (!Params().IsRegTestNet()) {
            const auto& consensusParams = Params().GetConsensus();
            bool fFallbackMode = (dummyHeader.nVersion == 11) || !IsModelDActive(nNextHeight) || (IsModelDActive(nNextHeight) && mnodeman.CountEnabled() < 11);
            int shift = fFallbackMode ? consensusParams.nAdamDifficultyShiftV1 :
                        ((consensusParams.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_ADAM_V2)) ? 
                         consensusParams.nAdamDifficultyShiftV2 : consensusParams.nAdamDifficultyShiftV1);
            scaledTarget = bnTarget << shift;
            uint256 powLimit = Params().GetConsensus().powLimit;
            if (scaledTarget > powLimit || scaledTarget < bnTarget) {
                scaledTarget = powLimit;
            }
        } else {
            scaledTarget = ~UINT256_ZERO;
        }

        if (puzzleHash > scaledTarget) {
            LogPrintf("submitblock: Puzzle hash does not meet difficulty target. puzzleHash=%s, scaledTarget=%s, nBits=%08x, fV12=%d\n",
                puzzleHash.ToString(), scaledTarget.ToString(), nBits, fV12);
            throw JSONRPCError(RPC_VERIFY_ERROR, "Puzzle hash does not meet difficulty target");
        }

        std::vector<unsigned char> vchSig;
        CBLSSecretKey blsKey;
#ifdef ENABLE_WALLET
        if (pwalletMain) {
            LOCK(pwalletMain->cs_wallet);
            if (!pwalletMain->GetBLSKey(minerKey.GetID(), blsKey) || !blsKey.IsValid()) {
                pwalletMain->GetBLSKey(GetCompressedKeyID(minerKey), blsKey);
            }
        }
#endif
        if (!blsKey.IsValid()) {
            for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                if (ComparePubKeys(activeMasternode.pubKeyMasternode, minerKey) && activeMasternode.blsKeyMasternode.IsValid()) {
                    blsKey = activeMasternode.blsKeyMasternode;
                    break;
                }
            }
        }
        if (blsKey.IsValid()) {
            if (!SignBLSWithECDSAFallback(puzzleHash, privKey, blsKey, vchSig)) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to sign puzzle hash");
            }
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "No direct BLS key is associated with the miner key");
        }

        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << nNonce << vchSig;
        std::vector<unsigned char> vchSolution(ss.begin(), ss.end());

        {
            LOCK(cs_adam_solutions);
            mapAdamSolutionsCache[pindexPrev->GetBlockHash()][minerKey] = vchSolution;
        }

        CAdamSolutionMsg solMsg;
        solMsg.hashPrevBlock = pindexPrev->GetBlockHash();
        solMsg.minerKey = minerKey;
        solMsg.vchSolution = vchSolution;

        if (g_connman) {
            g_connman->ForEachNode([&solMsg](CNode* pnode) {
                g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::ADAMSOL, solMsg));
            });
            LogPrintf("submitblock: Broadcasted adamsol solved via cpuminer-opt for miner key %s and tip %s\n",
                minerKey.GetID().ToString(), pindexPrev->GetBlockHash().ToString());
        }
        return "valid";
    }

    CBlock block;
    if (!DecodeHexBlk(block, hexData))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    if (block.vtx.empty() || !block.vtx[0].IsCoinBase()) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block does not start with a coinbase");
    }

    if (block.nVersion >= 11 && block.vAdamCoordinatorSig.empty()) {
        uint256 adamSeed = GetAdamSeed(chainActive.Tip());
        std::vector<CPubKey> vExpectedMiners;
        CPubKey expectedCoordinator;
        if (SelectAdamNodes(adamSeed, Params().GetConsensus(), vExpectedMiners, expectedCoordinator)) {
            CKey coordKey;
            bool gotKey = false;
#ifdef ENABLE_WALLET
            if (pwalletMain && (pwalletMain->GetKey(expectedCoordinator.GetID(), coordKey) ||
                                pwalletMain->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey))) {
                gotKey = true;
            } else {
                for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                    if (ComparePubKeys(activeMasternode.pubKeyMasternode, expectedCoordinator)) {
                        CKey key;
                        CPubKey pubkey;
                        if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, key, pubkey)) {
                            coordKey = key;
                            gotKey = true;
                            break;
                        }
                    }
                }
            }
#endif
            if (!gotKey && Params().IsRegTestNet()) {
                for (int i = 0; i < 15; ++i) {
                    if (ComparePubKeys(GetAdamDeterministicPubKey(i), expectedCoordinator)) {
                        coordKey = GetAdamDeterministicKey(i);
                        gotKey = true;
                        break;
                    }
                }
            }
            if (gotKey && coordKey.IsValid()) {
                CBLSSecretKey blsKey;
#ifdef ENABLE_WALLET
                if (pwalletMain) {
                    LOCK(pwalletMain->cs_wallet);
                    if (!pwalletMain->GetBLSKey(expectedCoordinator.GetID(), blsKey) || !blsKey.IsValid()) {
                        pwalletMain->GetBLSKey(GetCompressedKeyID(expectedCoordinator), blsKey);
                    }
                }
#endif
                if (!blsKey.IsValid()) {
                    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                        if (ComparePubKeys(activeMasternode.pubKeyMasternode, expectedCoordinator) && activeMasternode.blsKeyMasternode.IsValid()) {
                            blsKey = activeMasternode.blsKeyMasternode;
                            break;
                        }
                    }
                }
                if (blsKey.IsValid()) {
                    if (SignBLSWithECDSAFallback(block.GetHash(), coordKey, blsKey, block.vAdamCoordinatorSig)) {
                        LogPrintf("submitblock: Signed ADAM block as coordinator, hash: %s\n", block.GetHash().ToString());
                    } else {
                        LogPrintf("submitblock ERROR: Failed to sign ADAM block as coordinator\n");
                    }
                } else {
                    LogPrintf("submitblock ERROR: Cannot sign block as coordinator because no direct BLS key is associated with expected coordinator %s\n", expectedCoordinator.GetID().ToString());
                }
            }
        }
    }

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        BlockMap::iterator mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.end()) {
            CBlockIndex* pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(state, nullptr, &block, nullptr, g_connman.get());
    UnregisterValidationInterface(&sc);
    if (fBlockPresent) {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted) {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue estimatefee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "estimatefee nblocks\n"
            "\nEstimates the approximate fee per kilobyte\n"
            "needed for a transaction to begin confirmation\n"
            "within nblocks blocks.\n"

            "\nArguments:\n"
            "1. nblocks     (numeric)\n"

            "\nResult:\n"
            "n :    (numeric) estimated fee-per-kilobyte\n"
            "\n"
            "-1.0 is returned if not enough transactions and\n"
            "blocks have been observed to make an estimate.\n"

            "\nExample:\n" +
            HelpExampleCli("estimatefee", "6"));

    RPCTypeCheck(request.params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = request.params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatesmartfee(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
                "estimatesmartfee nblocks\n"
                "\nDEPRECATED. WARNING: This interface is unstable and may disappear or change!\n"
                "\nEstimates the approximate fee per kilobyte needed for a transaction to begin\n"
                "confirmation within nblocks blocks if possible and return the number of blocks\n"
                "for which the estimate is valid.\n"
                "\nArguments:\n"
                "1. nblocks     (numeric)\n"
                "\nResult:\n"
                "{\n"
                "  \"feerate\" : x.x,     (numeric) estimate fee-per-kilobyte (in BTC)\n"
                "  \"blocks\" : n         (numeric) block number where estimate was found\n"
                "}\n"
                "\n"
                "A negative value is returned if not enough transactions and blocks\n"
                "have been observed to make an estimate for any number of blocks.\n"
                "However it will not return a value below the mempool reject fee.\n"
                "\nExample:\n"
                + HelpExampleCli("estimatesmartfee", "6")
        );

    RPCTypeCheck(request.params, boost::assign::list_of(UniValue::VNUM));

    int nBlocks = request.params[0].get_int();

    UniValue result(UniValue::VOBJ);
    int answerFound;
    CFeeRate feeRate = mempool.estimateSmartFee(nBlocks, &answerFound);
    result.push_back(Pair("feerate", feeRate == CFeeRate(0) ? -1.0 : ValueFromAmount(feeRate.GetFeePerK())));
    result.push_back(Pair("blocks", answerFound));
    return result;
}

UniValue getadamminers(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getadamminers\n"
            "\nReturns the current pool of public keys for ADAM cooperative consensus.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"index\" : n,\n"
            "    \"address\" : \"xxxx\",\n"
            "    \"pubkey\" : \"xxxx\"\n"
            "  },...\n"
            "]\n"
        );

    std::vector<CPubKey> pool = GetAdamMinerPool();
    UniValue result(UniValue::VARR);
    for (size_t i = 0; i < pool.size(); ++i) {
        CPubKey pubkey = pool[i];
        std::string addr = EncodeDestination(pubkey.GetID());

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("index", (int)i);
        obj.pushKV("address", addr);
        obj.pushKV("pubkey", HexStr(pubkey));
        result.push_back(obj);
    }
    return result;
}

UniValue registerminer(const JSONRPCRequest& request)
{
#ifndef ENABLE_WALLET
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
#else
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 3)
        throw std::runtime_error(
            "registerminer \"type\" ( parameter \"address_or_pubkey\" )\n"
            "\nRegister a miner address/pubkey in the ADAM miner pool.\n"
            "\nArguments:\n"
            "1. \"type\"               (string, required) The registration type: 'lock' or 'pow'\n"
            "2. \"parameter\"          (numeric/string, optional)\n"
            "                          For 'lock': The lock height (default: current height + 2880)\n"
            "                          For 'pow': The challenge block hash (default: active chain tip hash)\n"
            "3. \"address_or_pubkey\"  (string, optional) Hex-encoded compressed public key or base58 address to register\n"
            "                          (default: derived deterministic public key index 0)\n"
            "\nResult:\n"
            "\"txid\"                  (string) The registration transaction ID\n"
            "\nExamples:\n"
            + HelpExampleCli("registerminer", "lock")
            + HelpExampleCli("registerminer", "pow")
            + HelpExampleCli("registerminer", "lock 10000 default")
        );

    EnsureWallet();
    EnsureWalletIsUnlocked();

    std::string type = request.params[0].get_str();
    if (type != "lock" && type != "pow") {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid registration type, must be 'lock' or 'pow'");
    }

    CPubKey pubkey;
    std::string strAddrOrPubKey = "";
    if (request.params.size() > 2) {
        strAddrOrPubKey = request.params[2].get_str();
    }

    if (strAddrOrPubKey.empty() || strAddrOrPubKey == "default") {
        pubkey = GetAdamDeterministicPubKey(0);
    } else {
        if (IsHex(strAddrOrPubKey)) {
            pubkey = CPubKey(ParseHex(strAddrOrPubKey));
        } else {
            CTxDestination dest = DecodeDestination(strAddrOrPubKey);
            if (IsValidDestination(dest)) {
                const CKeyID* keyID = boost::get<CKeyID>(&dest);
                if (keyID) {
                    LOCK(pwalletMain->cs_wallet);
                    pwalletMain->GetPubKey(*keyID, pubkey);
                }
            }
        }
        if (!pubkey.IsValid()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid public key or address specified");
        }
    }

    // Automatically trigger BLS key generation if we own the private key
    {
        LOCK(pwalletMain->cs_wallet);
        CKeyID keyID = pubkey.GetID();
        if (pwalletMain->HaveKey(keyID)) {
            CBLSSecretKey blsKey;
            pwalletMain->GetBLSKey(keyID, blsKey);
        }
    }

    CScript scriptPubKey;
    CAmount nAmount = 0;

    if (type == "lock") {
        int64_t locktime = 0;
        {
            LOCK(cs_main);
            locktime = chainActive.Height() + 2900;
        }
        if (request.params.size() > 1 && !request.params[1].isNull()) {
            if (request.params[1].isNum()) {
                locktime = request.params[1].get_int64();
            } else if (request.params[1].isStr()) {
                try {
                    locktime = std::stoll(request.params[1].get_str());
                } catch (const std::exception& e) {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, "Locktime must be a valid integer value");
                }
            } else {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Locktime must be an integer or string");
            }
        }
        {
            LOCK(cs_main);
            if (locktime < chainActive.Height() + 2880) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Locktime must be at least 2880 blocks in the future");
            }
        }

        scriptPubKey = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_DROP
                                 << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                 << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        nAmount = MINER_REGISTRATION_LOCK_AMOUNT;
    } else if (type == "pow") {
        uint256 challengeHash;
        uint256 target;
        int64_t currentHeight = 0;
        {
            LOCK(cs_main);
            if (request.params.size() > 1 && !request.params[1].isNull()) {
                challengeHash.SetHex(request.params[1].get_str());
            } else {
                challengeHash = chainActive.Tip()->GetBlockHash();
            }

            if (!mapBlockIndex.count(challengeHash)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Challenge block hash not found in main chain");
            }
            CBlockIndex* pindexChallenge = mapBlockIndex[challengeHash];
            if (!chainActive.Contains(pindexChallenge)) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Challenge block hash is not in active main chain");
            }

            target = GetMinerPoWLimit(Params().NetworkIDString());
            currentHeight = chainActive.Height();
        }

        uint32_t nonce = 0;
        uint256 puzzleHash;
        LogPrintf("registerminer pow: Starting CPU search for challenge %s...\n", challengeHash.ToString());
        while (true) {
            CHashWriter ss(SER_GETHASH, 0);
            ss << nonce;
            ss << challengeHash;
            ss << pubkey;
            puzzleHash = ss.GetHash();
            if (puzzleHash <= target) {
                break;
            }
            nonce++;
            if (nonce % 100000 == 0 && ShutdownRequested()) {
                throw JSONRPCError(RPC_INTERNAL_ERROR, "PoW search cancelled");
            }
        }
        LogPrintf("registerminer pow: Found solution! nonce=%u, hash=%s\n", nonce, puzzleHash.ToString());

        int64_t locktime = currentHeight + 2900;

        CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
        ssNonce << nonce;
        scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                 << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                 << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                 << OP_DROP << OP_DROP << OP_DROP
                                 << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                 << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        CAmount balance = pwalletMain->GetAvailableBalance();
        nAmount = (balance == 0) ? 0 : 10000;
    }

    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    std::string strError;
    CWalletTx wtx;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        bool created = false;
        if (nAmount == 0) {
            CMutableTransaction mtx;
            mtx.nVersion = 1;
            mtx.nLockTime = 0;
            CTxIn txin;
            txin.prevout = COutPoint(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
            mtx.vin.push_back(txin);
            CTxOut txout(0, scriptPubKey);
            mtx.vout.push_back(txout);
            wtx = CWalletTx(pwalletMain, mtx);
            created = true;
        } else {
            created = pwalletMain->CreateTransaction(scriptPubKey, nAmount, wtx, reservekey, nFeeRequired, strError, nullptr, ALL_COINS, (CAmount)0);
        }
        if (!created) {
            throw JSONRPCError(RPC_WALLET_ERROR, strError);
        }
        const CWallet::CommitResult&& res = pwalletMain->CommitTransaction(wtx, reservekey, g_connman.get());
        if (res.status != CWallet::CommitStatus::OK) {
            throw JSONRPCError(RPC_WALLET_ERROR, res.ToString());
        }
    }
    return wtx.GetHash().GetHex();
#endif
}

UniValue setblsprivkey(const JSONRPCRequest& request)
{
#ifndef ENABLE_WALLET
    throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
#else
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 2)
        throw std::runtime_error(
            "setblsprivkey \"address\" ( \"bls_privkey_hex\" )\n"
            "\nConfigure or import a native BLS private key associated with a wallet ECDSA address.\n"
            "If no BLS private key is provided, the wallet will generate a new one automatically.\n"
            "\nArguments:\n"
            "1. \"address\"          (string, required) The base58 ECDSA address of the miner or coordinator\n"
            "2. \"bls_privkey_hex\"   (string, optional) Hex-encoded BLS private key (32 bytes / 64 hex characters)\n"
            "\nResult:\n"
            "\"hex\"                  (string) The hex-encoded BLS private key configured for the address\n"
            "\nExamples:\n"
            + HelpExampleCli("setblsprivkey", "\"KTXj95tYUCFhCKfNcTPP5BEZJhxPjDUsean\"")
            + HelpExampleCli("setblsprivkey", "\"KTXj95tYUCFhCKfNcTPP5BEZJhxPjDUsean\" \"0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20\"")
        );

    EnsureWallet();
    EnsureWalletIsUnlocked();

    std::string strAddress = request.params[0].get_str();
    CBLSSecretKey blsKey;

    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid ECDSA address");
    }

    const CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address does not refer to a key");
    }

    if (!pwalletMain->HaveKey(*keyID)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Address is not in the wallet");
    }

    if (request.params.size() == 2) {
        std::string strBLSKeyHex = request.params[1].get_str();
        if (!IsHex(strBLSKeyHex)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "BLS private key must be a hex string");
        }
        std::vector<unsigned char> vchBLSKey = ParseHex(strBLSKeyHex);
        if (!blsKey.SetBuf(vchBLSKey.data(), vchBLSKey.size())) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid BLS private key size (must be 32 bytes)");
        }
    } else {
        // Trigger GetBLSKey which will generate and save it automatically since HaveKey is true
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->GetBLSKey(*keyID, blsKey)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to retrieve or generate BLS key");
        }
    }

    if (request.params.size() == 2) {
        LOCK(pwalletMain->cs_wallet);
        if (!pwalletMain->AddBLSKey(*keyID, blsKey)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Failed to write BLS key to database");
        }
    }

    return HexStr(blsKey.begin(), blsKey.end());
#endif
}


