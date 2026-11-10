// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"
#include <thread>
#include "adam.h"
#include "crypto/bls.h"
#include "llmq.h"
#include "llmq_messages.h"

#include "amount.h"
#include "consensus/merkle.h"
#include "consensus/tx_verify.h" // needed in case of no ENABLE_WALLET
#include "hash.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "masternodeconfig.h"
#include "net.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "base58.h"
#include "utilstrencodings.h"
#ifdef ENABLE_WALLET
#include "coincontrol.h"
#include "wallet/wallet.h"
#endif
#include "validationinterface.h"
#include "masternode-payments.h"
#include "blocksignature.h"
#include "spork.h"
#include "init.h"
#include "script/script.h"
#include "policy/policy.h"
#include "messagesigner.h"
#include "netmessagemaker.h"


#include <boost/thread.hpp>
#include <boost/tuple/tuple.hpp>

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

//////////////////////////////////////////////////////////////////////////////
//
// Miner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.
// The COrphan class keeps track of these 'temporary orphans' while
// CreateBlock is figuring out which transactions to include.
//
class COrphan
{
public:
    const CTransaction* ptx;
    std::set<uint256> setDependsOn;
    CFeeRate feeRate;
    double dPriority;

    COrphan(const CTransaction* ptxIn) : ptx(ptxIn), feeRate(0), dPriority(0)
    {
    }
};

uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;

// We want to sort transactions by priority and fee rate, so:
typedef boost::tuple<double, CFeeRate, const CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;

public:
    TxPriorityCompare(bool _byFee) : byFee(_byFee) {}

    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee) {
            if (a.get<1>() == b.get<1>())
                return a.get<0>() < b.get<0>();
            return a.get<1>() < b.get<1>();
        } else {
            if (a.get<0>() == b.get<0>())
                return a.get<1>() < b.get<1>();
            return a.get<0>() < b.get<0>();
        }
    }
};

void UpdateTime(CBlockHeader* pblock, const CBlockIndex* pindexPrev)
{
    const Consensus::Params& consensus = Params().GetConsensus();
    int nHeight = pindexPrev->nHeight + 1;
    if (consensus.IsTimeProtocolV2(nHeight)) {
        int64_t nTimeSlotLength = consensus.nTimeSlotLength;
        int64_t nTime = GetAdjustedTime();
        nTime = (nTime / nTimeSlotLength) * nTimeSlotLength;
        while (nTime <= pindexPrev->MinPastBlockTime()) {
            nTime += nTimeSlotLength;
        }
        pblock->nTime = nTime;
    } else {
        pblock->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());
    }

}

bool CreateCoinbaseTx(CBlock* pblock, const CScript& scriptPubKeyIn, CBlockIndex* pindexPrev, const std::vector<CPubKey>* pvValidMiners = nullptr)
{
    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;

    //Masternode and general budget payments
    FillBlockPayee(txNew, pindexPrev, false, pvValidMiners);

    txNew.vin[0].scriptSig = CScript() << pindexPrev->nHeight + 1 << OP_0;
    // If no payee was detected, then the whole block value goes to the first output.
    if (txNew.vout.size() == 1) {
        txNew.vout[0].nValue = CMasternode::GetBlockValue(pindexPrev->nHeight + 1);
    }

    pblock->vtx.emplace_back(txNew);
    return true;
}

bool HasCoordinatorKey(const CPubKey& coordinatorPubKey, CWallet* pwallet, CKey& coordKeyOut)
{
    if (!coordinatorPubKey.IsValid()) return false;

    // 1. Try pwallet (PoS staking wallet or thread wallet)
    if (pwallet && (pwallet->GetKey(coordinatorPubKey.GetID(), coordKeyOut) ||
                    pwallet->GetKey(GetCompressedKeyID(coordinatorPubKey), coordKeyOut))) {
        return coordKeyOut.IsValid();
    }
#ifdef ENABLE_WALLET
    // 2. Try pwalletMain (global wallet, used in PoW/gen=1 mode)
    if (pwalletMain && pwalletMain != pwallet) {
        LOCK(pwalletMain->cs_wallet);
        if (pwalletMain->GetKey(coordinatorPubKey.GetID(), coordKeyOut) ||
            pwalletMain->GetKey(GetCompressedKeyID(coordinatorPubKey), coordKeyOut)) {
            return coordKeyOut.IsValid();
        }
    }
#endif
    // 3. Try active masternodes
    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
        if (ComparePubKeys(activeMasternode.pubKeyMasternode, coordinatorPubKey)) {
            CKey key;
            CPubKey pubkey;
            if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, key, pubkey)) {
                coordKeyOut = key;
                return coordKeyOut.IsValid();
            }
        }
    }
    // 4. masternode.conf entries are intentionally NOT used: that file belongs to the cold controller
    //    wallet. Coordinator signing must only happen on the hot node (activemasternode.conf).
    // 5. Try -masternodeprivkey config directly
    std::string strMnPrivKey = GetArg("-masternodeprivkey", "");
    if (!strMnPrivKey.empty()) {
        CKey key;
        CPubKey pubkey;
        if (CMessageSigner::GetKeysFromSecret(strMnPrivKey, key, pubkey)) {
            if (ComparePubKeys(pubkey, coordinatorPubKey)) {
                coordKeyOut = key;
                return coordKeyOut.IsValid();
            }
        }
    }
    // 6. Try deterministic keys (regtest only)
    if (Params().IsRegTestNet()) {
        for (int i = 0; i < 15; ++i) {
            if (ComparePubKeys(GetAdamDeterministicPubKey(i), coordinatorPubKey)) {
                coordKeyOut = GetAdamDeterministicKey(i);
                return coordKeyOut.IsValid();
            }
        }
    }
    return false;
}

static std::mutex cs_active_proposal;
static std::map<std::pair<uint256, CPubKey>, int64_t> mapInFlightParentCoord;
static std::map<std::pair<uint256, CPubKey>, int64_t> mapProducedParentCoord;
static std::map<std::pair<uint256, CPubKey>, uint256> mapAcceptedRemoteReqId;

static void PruneParentCoordMap(std::map<std::pair<uint256, CPubKey>, int64_t>& targetMap, const uint256& keepParentHash, int64_t now, int64_t maxAgeSecs, size_t maxEntries = 256)
{
    if (targetMap.size() <= maxEntries) {
        return;
    }
    for (auto it = targetMap.begin(); it != targetMap.end(); ) {
        if (it->first.first != keepParentHash && (now - it->second) > maxAgeSecs) {
            it = targetMap.erase(it);
        } else {
            ++it;
        }
    }
    while (targetMap.size() > maxEntries) {
        auto oldestIt = targetMap.end();
        for (auto it = targetMap.begin(); it != targetMap.end(); ++it) {
            if (it->first.first == keepParentHash) continue;
            if (oldestIt == targetMap.end() || it->second < oldestIt->second) {
                oldestIt = it;
            }
        }
        if (oldestIt == targetMap.end()) {
            break;
        }
        targetMap.erase(oldestIt);
    }
}

static bool IsParentBlockProducedOrInFlight(const uint256& parentHash, const CPubKey& coordKey)
{
    std::lock_guard<std::mutex> lock(cs_active_proposal);
    const auto key = std::make_pair(parentHash, coordKey);
    if (mapProducedParentCoord.count(key) > 0) {
        return true;
    }
    auto it = mapInFlightParentCoord.find(key);
    if (it != mapInFlightParentCoord.end() && (GetTime() - it->second) < 15) {
        return true;
    }
    return false;
}

bool AcquireCoordinatorProposalSlot(const uint256& parentHash, const CPubKey& coordKey, const uint256& requestId, bool fMarkProduced)
{
    std::lock_guard<std::mutex> lock(cs_active_proposal);
    const int64_t now = GetTime();
    const auto key = std::make_pair(parentHash, coordKey);
    auto itAccepted = mapAcceptedRemoteReqId.find(key);
    if (itAccepted != mapAcceptedRemoteReqId.end() && !requestId.IsNull() && itAccepted->second == requestId) {
        return true;
    }
    if (mapProducedParentCoord.count(key) > 0) {
        return false;
    }
    auto it = mapInFlightParentCoord.find(key);
    if (it != mapInFlightParentCoord.end() && (now - it->second) < 15) {
        return false;
    }
    if (fMarkProduced && !Params().IsRegTestNet()) {
        PruneParentCoordMap(mapProducedParentCoord, parentHash, now, 600);
        mapProducedParentCoord[key] = now;
        if (!requestId.IsNull()) {
            if (mapAcceptedRemoteReqId.size() > 256) {
                mapAcceptedRemoteReqId.clear();
            }
            mapAcceptedRemoteReqId[key] = requestId;
        }
    }
    return true;
}

static void ReleaseParentBlockInFlight(const uint256& parentHash)
{
    std::lock_guard<std::mutex> lock(cs_active_proposal);
    for (auto it = mapInFlightParentCoord.begin(); it != mapInFlightParentCoord.end(); ) {
        if (it->first.first == parentHash) {
            it = mapInFlightParentCoord.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = mapProducedParentCoord.begin(); it != mapProducedParentCoord.end(); ) {
        if (it->first.first == parentHash) {
            it = mapProducedParentCoord.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = mapAcceptedRemoteReqId.begin(); it != mapAcceptedRemoteReqId.end(); ) {
        if (it->first.first == parentHash) {
            it = mapAcceptedRemoteReqId.erase(it);
        } else {
            ++it;
        }
    }
}

struct ActiveProposalGuard {
    uint256 parentHash;
    CPubKey coordKey;
    bool fEngaged{false};
    bool fAcquired{false};
    bool fSucceeded{false};

    ActiveProposalGuard() = default;

    bool Acquire(const uint256& parentHashIn, const CPubKey& coordKeyIn)
    {
        parentHash = parentHashIn;
        coordKey = coordKeyIn;
        fEngaged = true;
        std::lock_guard<std::mutex> lock(cs_active_proposal);
        const int64_t now = GetTime();
        const auto key = std::make_pair(parentHash, coordKey);
        if (mapProducedParentCoord.count(key) > 0) {
            fAcquired = false;
            return false;
        }
        auto it = mapInFlightParentCoord.find(key);
        if (it != mapInFlightParentCoord.end() && (now - it->second) < 15) {
            fAcquired = false;
            return false;
        }
        PruneParentCoordMap(mapInFlightParentCoord, parentHash, now, 60);
        mapInFlightParentCoord[key] = now;
        fAcquired = true;
        return true;
    }

    void MarkSucceeded()
    {
        fSucceeded = true;
        if (fEngaged && fAcquired && !Params().IsRegTestNet()) {
            std::lock_guard<std::mutex> lock(cs_active_proposal);
            const int64_t now = GetTime();
            PruneParentCoordMap(mapProducedParentCoord, parentHash, now, 600);
            mapProducedParentCoord[std::make_pair(parentHash, coordKey)] = now;
        }
    }

    ~ActiveProposalGuard()
    {
        if (fEngaged && fAcquired && !fSucceeded) {
            std::lock_guard<std::mutex> lock(cs_active_proposal);
            mapInFlightParentCoord.erase(std::make_pair(parentHash, coordKey));
        }
    }
};

bool SolveProofOfStake(CBlock* pblock, CBlockIndex* pindexPrev, CWallet* pwallet, std::vector<COutput>* availableCoins, const std::vector<CPubKey>* pvValidMiners = nullptr)
{
    boost::this_thread::interruption_point();
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
    CMutableTransaction txCoinStake;
    int64_t nTxNewTime = 0;
    if (!pwallet->CreateCoinStake(*pwallet, pindexPrev, pblock->nBits, txCoinStake, nTxNewTime, availableCoins, pvValidMiners)) {
        LogPrint(BCLog::STAKING, "%s : stake not found\n", __func__);
        return false;
    }
    // Stake found
    pblock->nTime = nTxNewTime;
    CMutableTransaction emptyTx;
    emptyTx.vin.resize(1);
    emptyTx.vin[0].scriptSig = CScript() << pindexPrev->nHeight + 1 << OP_0;
    emptyTx.vout.resize(1);
    emptyTx.vout[0].SetEmpty();
    pblock->vtx.emplace_back(emptyTx);
    pblock->vtx.emplace_back(txCoinStake);
    return true;
}

CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake, std::vector<COutput>* availableCoins)
{
    // Create new block
    std::unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
    if (!pblocktemplate.get()) return nullptr;
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience
    CPubKey expectedCoordinator;
    bool fNeedsRemotePoSCoordSig = false;

    // Tip
    CBlockIndex* pindexPrev = GetChainTip();
    if (!pindexPrev) return nullptr;
    const int nHeight = pindexPrev->nHeight + 1;

    // Make sure to create the correct block version
    const Consensus::Params& consensus = Params().GetConsensus();

    if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL))
        pblock->nVersion = 12;
    else if (IsAdamActive(nHeight, consensus))
        pblock->nVersion = 11;
    else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_TIME_PROTOCOL_V2))
        pblock->nVersion = 7;
    else if (consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_BIP65))
        pblock->nVersion = 5;
    else
        pblock->nVersion = 3;

    // -regtest only: allow overriding block.nVersion with
    // -blockversion=N to test forking scenarios
    if (Params().IsRegTestNet()) {
        pblock->nVersion = GetArg("-blockversion", pblock->nVersion);
    }

    uint256 adamSeed;
    int threshold = 0;
    int availableSolutions = 0;
    bool fFallbackMode = false;
    std::vector<CPubKey> vExpectedMiners;
    std::vector<CPubKey> vPrimaryExpectedMiners;
    std::vector<CPubKey> vValidSolutionMiners;
    std::map<CPubKey, std::vector<unsigned char>> verifiedSolutionsForBlock;
    ActiveProposalGuard proposalGuard;

    // Solve partial puzzles if ADAM is active before constructing coinbase/coinstake payouts
    if (pblock->nVersion >= 11) {
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
        adamSeed = GetAdamSeed(pindexPrev);
        const int64_t nCandidateTime = consensus.IsTimeProtocolV2(nHeight)
            ? GetCurrentTimeSlot()
            : std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());
        int64_t nTemplateTimeDiff = std::max<int64_t>(0, nCandidateTime - pindexPrev->GetBlockTime());
        int nActiveMns = GetActiveMasternodePoolCount(nHeight - 1, pcoinsTip);
        threshold = consensus.GetAdamEffectiveThreshold(nHeight, nTemplateTimeDiff, nActiveMns);
        fFallbackMode = (pblock->nVersion == 11) || !IsModelDActive(nHeight);

        std::map<CPubKey, std::vector<unsigned char>> solutionsForBlock;
        bool hasSolutions = false;
        {
            LOCK(cs_adam_solutions);
            auto it = mapAdamSolutionsCache.find(pblock->hashPrevBlock);
            if (it != mapAdamSolutionsCache.end()) {
                solutionsForBlock = it->second;
                hasSolutions = true;
            }
        }

        std::vector<CPubKey> vStandbyMiners;
        if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nHeight, nullptr, &vStandbyMiners)) {
            static int64_t nLastSelectFailedTime = 0;
            int64_t nNow = GetTime();
            if (nNow - nLastSelectFailedTime > 60) {
                LogPrintf("CreateNewBlock: SelectAdamNodes failed. Miner pool too small or not synchronized. (this message is throttled to 1/min)\n");
                nLastSelectFailedTime = nNow;
            }
            return nullptr;
        }
        vPrimaryExpectedMiners = vExpectedMiners;

        pblock->vAdamSolutions.clear();
        if (hasSolutions) {
            std::vector<CPubKey> vResolvedMiners;
            ResolveAdamCommitteeForTime(
                pblock->hashPrevBlock, adamSeed, nHeight, nTemplateTimeDiff,
                pblock->nBits, pblock->nVersion, vExpectedMiners, vStandbyMiners,
                solutionsForBlock, vResolvedMiners, pblock->vAdamSolutions, availableSolutions,
                nullptr, nullptr, pcoinsTip);
            vExpectedMiners = std::move(vResolvedMiners);
            for (size_t i = 0; i < vExpectedMiners.size() && i < pblock->vAdamSolutions.size(); ++i) {
                if (!pblock->vAdamSolutions[i].empty()) {
                    verifiedSolutionsForBlock[vExpectedMiners[i]] = pblock->vAdamSolutions[i];
                }
            }
        } else {
            for (size_t k = 0; k < vExpectedMiners.size(); ++k) {
                pblock->vAdamSolutions.push_back(std::vector<unsigned char>());
            }
        }

        if (Params().IsRegTestNet() && availableSolutions < threshold) {
            LogPrintf("CreateNewBlock: Regtest mode. Generating deterministic solutions on the fly to meet quorum.\n");
            for (size_t minerIndex = 0; minerIndex < vExpectedMiners.size(); ++minerIndex) {
                const auto& minerKey = vExpectedMiners[minerIndex];
                // Check if we already have a valid solution for this miner
                bool hasValidSol = false;
                if (minerIndex < pblock->vAdamSolutions.size() && !pblock->vAdamSolutions[minerIndex].empty()) {
                    if (VerifyAdamSolution(pblock->hashPrevBlock, adamSeed, minerKey, pblock->vAdamSolutions[minerIndex], pblock->nBits, pblock->nVersion, nHeight, (int)minerIndex)) {
                        hasValidSol = true;
                    }
                }
                if (!hasValidSol) {
                    // Find the deterministic index for this key
                    int detIndex = -1;
                    for (int i = 0; i < 15; ++i) {
                        if (GetAdamDeterministicPubKey(i) == minerKey) {
                            detIndex = i;
                            break;
                        }
                    }
                    if (detIndex != -1) {
                        CKey privKey = GetAdamDeterministicKey(detIndex);
                        uint32_t nNonce = 0;
                        std::vector<unsigned char> vchSig;
                        uint256 scaledTarget = ~UINT256_ZERO;
                        
                        unsigned char inputBuf[104];
                        const unsigned int keyLen = minerKey.size();
                        memcpy(inputBuf, adamSeed.begin(), 32);
                        inputBuf[32] = static_cast<unsigned char>(keyLen);
                        if (keyLen > 0) {
                            memcpy(inputBuf + 33, minerKey.begin(), keyLen);
                        }
                        const size_t inputLen = 37 + keyLen;
                        int algoIndex = (pblock->nVersion == 11) ? GetAdamPuzzleAlgo(adamSeed, minerKey, true) : 12;
                        int algo1 = -1, algo2 = -1, algo3 = -1;
                        if (pblock->nVersion >= 12) {
                            GetAdam3PermutationAlgos(pblock->hashPrevBlock, minerKey, algo1, algo2, algo3);
                        }
                        const int i_factor = (int)minerIndex + 1;
                        
                        while (true) {
                            WriteLE32(inputBuf + 33 + keyLen, nNonce);
                            
                            uint256 puzzleHash;
                            if (pblock->nVersion == 11) {
                                puzzleHash = CalculateAdamPuzzleHash(algoIndex, inputBuf, inputBuf + inputLen);
                            } else { // nVersion >= 12
                                uint256 hash3 = CalculateAdamPuzzleHash(algo3, inputBuf, inputBuf + inputLen);
                                arith_uint256 val1 = UintToArith256(hash3) * i_factor;
                                uint256 multiplied1 = ArithToUint256(val1);
                                
                                uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
                                arith_uint256 val2 = UintToArith256(hash2) * i_factor;
                                uint256 multiplied2 = ArithToUint256(val2);
                                
                                puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
                            }
                            
                             if (puzzleHash <= scaledTarget) {
                                  CBLSSecretKey blsKey = DeriveBLSFromCKey(privKey);
                                 if (blsKey.IsValid()) {
                                     SignBLSWithECDSAFallback(puzzleHash, privKey, blsKey, vchSig);
                                     break;
                                 } else {
                                     LogPrintf("CreateNewBlock: Skip solution because no direct BLS key is associated with miner key %s\n", minerKey.GetID().ToString());
                                     break;
                                 }
                             }
                            nNonce++;
                        }
                        
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss << nNonce << vchSig;
                        std::vector<unsigned char> vchSolution(ss.begin(), ss.end());
                        
                        {
                            LOCK(cs_adam_solutions);
                            mapAdamSolutionsCache[pblock->hashPrevBlock][minerKey] = vchSolution;
                        }
                        
                        if (minerIndex < pblock->vAdamSolutions.size()) {
                            pblock->vAdamSolutions[minerIndex] = vchSolution;
                        } else {
                            pblock->vAdamSolutions.push_back(vchSolution);
                        }
                        verifiedSolutionsForBlock[minerKey] = vchSolution;
                        availableSolutions++;
                    }
                }
            }
        }

        if (nHeight > consensus.nAdamQuorumEnforcementHeight && availableSolutions < threshold) {
            LogPrintf("CreateNewBlock: Quorum threshold not met (available=%d vs threshold=%d). Block template deferred.\n",
                availableSolutions, threshold);
            return nullptr;
        }

        for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
            if (i < pblock->vAdamSolutions.size() && !pblock->vAdamSolutions[i].empty()) {
                vValidSolutionMiners.push_back(vExpectedMiners[i]);
            }
        }

        // Verify coordinator ownership (or cooperative remote PoS coordinator target) and acquire proposalGuard BEFORE scanning wallet UTXOs in SolveProofOfStake
        bool fAllowCoordinatorRotation = (pblock->nVersion == 11) || (nTemplateTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS);
        CKey kTest;
        bool ownsCoordinator = HasCoordinatorKey(expectedCoordinator, pwallet, kTest);
        if (fAllowCoordinatorRotation && !ownsCoordinator && !vExpectedMiners.empty()) {
            CPubKey activeFallback, prevFallback;
            int rotationIndex = 0;
            if (GetAdamFallbackCoordinatorForSlot(adamSeed, nHeight, nTemplateTimeDiff, vExpectedMiners, verifiedSolutionsForBlock, activeFallback, prevFallback, rotationIndex)) {
                expectedCoordinator = activeFallback;
                bool ownsFallback = HasCoordinatorKey(activeFallback, pwallet, kTest);
                if (!ownsFallback && Params().IsRegTestNet()) {
                    std::vector<CPubKey> vRankedValidators = RankValidatorsByPoBLSDistance(adamSeed, nHeight, vExpectedMiners, verifiedSolutionsForBlock);
                    for (const auto& mKey : vRankedValidators) {
                        if (HasCoordinatorKey(mKey, pwallet, kTest)) {
                            expectedCoordinator = mKey;
                            ownsFallback = true;
                            break;
                        }
                    }
                }
                ownsCoordinator = ownsFallback;
                if (ownsCoordinator || fProofOfStake || Params().IsRegTestNet()) {
                    LogPrintf("CreateNewBlock: Targeting PoBLS XOR-ranked fallback coordinator (rotation index %d, address: %s, localKey=%d, fPoS=%d).\n",
                        rotationIndex, expectedCoordinator.GetID().ToString(), ownsCoordinator ? 1 : 0, fProofOfStake ? 1 : 0);
                }
            }
        }
        if (!ownsCoordinator && !fProofOfStake && !Params().IsRegTestNet()) {
            return nullptr;
        }
        if (!expectedCoordinator.IsValid() && !Params().IsRegTestNet()) {
            return nullptr;
        }

        if (!proposalGuard.Acquire(pblock->hashPrevBlock, expectedCoordinator)) {
            LogPrint(BCLog::MASTERNODE, "CreateNewBlock: Another thread is already producing or has produced a block for parent %s and coordinator %s; deferring duplicate block.\n",
                     pblock->hashPrevBlock.ToString(), expectedCoordinator.GetID().ToString());
            return nullptr;
        }
    }

    // Depending on the tip height, try to find a coinstake who solves the block or create a coinbase tx.
    CScript effectiveScriptPubKey = scriptPubKeyIn;
    if (pblock->nVersion >= 11 && !fProofOfStake && expectedCoordinator.IsValid() &&
        (effectiveScriptPubKey.empty() || effectiveScriptPubKey == (CScript() << OP_TRUE))) {
        bool resolvedMnCollateralScript = false;
        if (pcoinsTip) {
            auto itCol = mapMasternodeCollateralOutpoints.find(expectedCoordinator);
            if (itCol != mapMasternodeCollateralOutpoints.end()) {
                Coin coin;
                if (pcoinsTip->GetCoin(itCol->second, coin) && !coin.IsSpent() &&
                    coin.out.nValue == CMasternode::GetMasternodeNodeCollateral(nHeight)) {
                    effectiveScriptPubKey = coin.out.scriptPubKey;
                    resolvedMnCollateralScript = true;
                }
            }
        }
        if (!resolvedMnCollateralScript) {
            CMasternode* pmn = mnodeman.Find(expectedCoordinator);
            if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                effectiveScriptPubKey = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            } else {
                effectiveScriptPubKey = GetScriptForDestination(expectedCoordinator.GetID());
            }
        }
    }

    const std::vector<CPubKey>* pvValidMiners = (pblock->nVersion >= 11) ? &vValidSolutionMiners : nullptr;
    if (!(fProofOfStake ? SolveProofOfStake(pblock, pindexPrev, pwallet, availableCoins, pvValidMiners)
                        : CreateCoinbaseTx(pblock, effectiveScriptPubKey, pindexPrev, pvValidMiners))) {
        return nullptr;
    }
    if (!fProofOfStake)
        UpdateTime(pblock, pindexPrev);

    if (pblock->nVersion >= 11) {
        LogPrintf("CreateNewBlock: SelectAdamNodes succeeded. elected %d miners. seed:%s\n", vExpectedMiners.size(), adamSeed.ToString());
        pblock->vAdamMiners = vExpectedMiners;
        if (pblock->nVersion == 11) {
            pblock->vAdamMiners.push_back(expectedCoordinator);
        }
    }


    pblocktemplate->vTxFees.push_back(-1);   // updated at end
    pblocktemplate->vTxSigOps.push_back(-1); // updated at end

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = std::min((unsigned int)GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE), MAX_BLOCK_SIZE_CURRENT);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    unsigned int nBlockMaxSizeSpork = (unsigned int)sporkManager.GetSporkValue(SPORK_105_MAX_BLOCK_SIZE);

    nBlockMaxSize = std::max(
        (unsigned int)1000, 
        std::min( 
            nBlockMaxSizeSpork, 
            nBlockMaxSize 
        )
    );

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
    nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

    // Collect memory pool transactions into the block
    CAmount nFees = 0;

    {
        LOCK2(cs_main, mempool.cs);
        CCoinsViewCache view(pcoinsTip);

        if (fProofOfStake && pblock->vtx.size() > 1) {
            const CTransaction& txCoinStake = pblock->vtx[1];
            for (const CTxIn& txin : txCoinStake.vin) {
                if (!view.HaveCoin(txin.prevout)) {
                    extern int nMintableLastCheck;
                    nMintableLastCheck = 0;
                    return nullptr;
                }
            }
            UpdateCoins(txCoinStake, view, nHeight);
        }

        // Priority order to process transactions
        std::list<COrphan> vOrphan; // list memory doesn't move
        std::map<uint256, std::vector<COrphan*> > mapDependers;
        bool fPrintPriority = GetBoolArg("-printpriority", DEFAULT_PRINTPRIORITY);

        // This vector will be sorted into a priority queue:
        std::vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        for (CTxMemPool::indexed_transaction_set::iterator mi = mempool.mapTx.begin();
             mi != mempool.mapTx.end(); ++mi) {
            const CTransaction& tx = mi->GetTx();
            if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, nHeight)){
                continue;
            }

            COrphan* porphan = NULL;
            double dPriority = 0;
            CAmount nTotalIn = 0;
            bool fMissingInputs = false;

            const bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);
            for (const CTxIn& txin : tx.vin) {
                if (fZeroCoinPoW) {
                    continue;
                }
                // Read prev transaction
                if (!view.HaveCoin(txin.prevout)) {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!mempool.mapTx.count(txin.prevout.hash)) {
                        LogPrintf("ERROR: mempool transaction missing input\n");
                        fMissingInputs = true;
                        if (porphan)
                            vOrphan.pop_back();
                        break;
                    }

                    // Has to wait for dependencies
                    if (!porphan) {
                        // Use list for automatic deletion
                        vOrphan.push_back(COrphan(&tx));
                        porphan = &vOrphan.back();
                    }
                    mapDependers[txin.prevout.hash].push_back(porphan);
                    porphan->setDependsOn.insert(txin.prevout.hash);
                    nTotalIn += mempool.mapTx.find(txin.prevout.hash)->GetTx().vout[txin.prevout.n].nValue;
                    continue;
                }

                const Coin& coin = view.AccessCoin(txin.prevout);
                assert(!coin.IsSpent());

                CAmount nValueIn = coin.out.nValue;
                nTotalIn += nValueIn;

                int nConf = nHeight - coin.nHeight;

                dPriority = double_safe_addition(dPriority, ((double)nValueIn * nConf));
            }
            if (fMissingInputs) continue;

            // Priority is sum(valuein * age) / modified_txsize
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            dPriority = tx.ComputePriority(dPriority, nTxSize);

            uint256 hash = tx.GetHash();
            mempool.ApplyDeltas(hash, dPriority, nTotalIn);

            CFeeRate feeRate(nTotalIn - tx.GetValueOut(), nTxSize);

            if (porphan) {
                porphan->dPriority = dPriority;
                porphan->feeRate = feeRate;
            } else
                vecPriority.push_back(TxPriority(dPriority, feeRate, &mi->GetTx()));
        }

        // Collect transactions into block
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        int nZeroCoinTxInBlock = 0;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        while (!vecPriority.empty()) {
            // Take highest priority transaction off the priority queue:
            double dPriority = vecPriority.front().get<0>();
            CFeeRate feeRate = vecPriority.front().get<1>();
            const CTransaction& tx = *(vecPriority.front().get<2>());

            std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize)
                continue;

            // Legacy limits on sigOps:
            unsigned int nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
            unsigned int nTxSigOps = GetLegacySigOpCount(tx);
            if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps)
                continue;

            const bool fZeroCoinPoW = IsZeroCoinPoWTransaction(tx);
            if (fZeroCoinPoW && nZeroCoinTxInBlock >= 1)
                continue;

            // Skip free transactions if we're past the minimum block size:
            const uint256& hash = tx.GetHash();
            double dPriorityDelta = 0;
            CAmount nFeeDelta = 0;
            mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
            if (!fZeroCoinPoW && fSortedByFee && (dPriorityDelta <= 0) && (nFeeDelta <= 0) && (feeRate < ::minRelayTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
                continue;

            // Prioritise by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee && !fZeroCoinPoW &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            if (!fZeroCoinPoW && !view.HaveInputs(tx))
                continue;

            CAmount nTxFees = fZeroCoinPoW ? 0 : (view.GetValueIn(tx) - tx.GetValueOut());

            nTxSigOps += fZeroCoinPoW ? 0 : GetP2SHSigOpCount(tx, view);
            if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps)
                continue;

            // Note that flags: we don't want to set mempool/IsStandard()
            // policy here, but we still have to ensure that the block we
            // create only contains transactions that are valid in new blocks.

            CValidationState state;
            PrecomputedTransactionData precomTxData(tx);
            if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, precomTxData))
                continue;

            UpdateCoins(tx, view, nHeight);
            if (fZeroCoinPoW)
                nZeroCoinTxInBlock++;

            // Added
            pblock->vtx.push_back(tx);
            pblocktemplate->vTxFees.push_back(nTxFees);
            pblocktemplate->vTxSigOps.push_back(nTxSigOps);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;
            nFees += nTxFees;

            if (fPrintPriority) {
                LogPrintf("priority %.1f fee %s txid %s\n",
                    dPriority, feeRate.ToString(), tx.GetHash().ToString());
            }

            // Add transactions that depend on this one to the priority queue
            if (mapDependers.count(hash)) {
                for (COrphan* porphan : mapDependers[hash]) {
                    if (!porphan->setDependsOn.empty()) {
                        porphan->setDependsOn.erase(hash);
                        if (porphan->setDependsOn.empty()) {
                            vecPriority.push_back(TxPriority(porphan->dPriority, porphan->feeRate, porphan->ptx));
                            std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
                        }
                    }
                }
            }
        }

        if (!fProofOfStake) {
            // Coinbase can get the fees.
            CMutableTransaction txCoinbase(pblock->vtx[0]);
            txCoinbase.vout[0].nValue += nFees;
            pblock->vtx[0] = txCoinbase;
            pblocktemplate->vTxFees[0] = -nFees;
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;
        LogPrintf("%s : total size %u\n", __func__, nBlockSize);

        // Fill in header
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        if (!fProofOfStake && pblock->nVersion < 11)
            UpdateTime(pblock, pindexPrev);
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
        pblock->nNonce = 0;

        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        if (fProofOfStake || pblock->nVersion >= 11) {
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        }

        if (pblock->nVersion >= 11) {
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            bool fSignedLocally = false;
            if (expectedCoordinator.IsValid()) {
                CKey coordKey;
                bool gotKey = HasCoordinatorKey(expectedCoordinator, pwallet, coordKey);
                if (gotKey && coordKey.IsValid()) {
                    CBLSSecretKey blsKey = DeriveBLSFromCKey(coordKey);
                    if (blsKey.IsValid()) {
                        if (SignBLSWithECDSAFallback(adamSeed, coordKey, blsKey, pblock->vAdamVRFProof)) {
                            LogPrintf("CreateNewBlock: Signed block VRF proof for TestBlockValidity, seed: %s, size=%d\n", adamSeed.ToString(), pblock->vAdamVRFProof.size());
                        } else {
                            LogPrintf("CreateNewBlock ERROR: Failed to sign block VRF proof!\n");
                        }
                        if (SignBLSWithECDSAFallback(pblock->GetHash(), coordKey, blsKey, pblock->vAdamCoordinatorSig)) {
                            LogPrintf("CreateNewBlock: Signed block header for TestBlockValidity, hash: %s, size=%d\n", pblock->GetHash().ToString(), pblock->vAdamCoordinatorSig.size());
                            fSignedLocally = !pblock->vAdamVRFProof.empty();
                        } else {
                            LogPrintf("CreateNewBlock ERROR: Failed to sign block header!\n");
                        }
                    } else {
                        LogPrintf("CreateNewBlock ERROR: Cannot sign block VRF proof or header because no direct BLS key is associated with coordinator key %s\n", expectedCoordinator.GetID().ToString());
                    }
                }
            }
            if (!fSignedLocally && fProofOfStake && expectedCoordinator.IsValid() && pwallet != nullptr && g_connman != nullptr) {
                fNeedsRemotePoSCoordSig = true;
            } else if ((pblock->vAdamVRFProof.empty() || pblock->vAdamCoordinatorSig.empty()) && !Params().IsRegTestNet()) {
                LogPrintf("CreateNewBlock: Coordinator signature or VRF proof missing for %s, aborting block template.\n",
                    expectedCoordinator.GetID().ToString());
                return nullptr;
            }
        }

        if (!fNeedsRemotePoSCoordSig) {
            if (fProofOfStake) {
                LogPrintf("CPUMiner : proof-of-stake block found %s \n", pblock->GetHash().GetHex());
                if (!SignBlock(*pblock, *pwallet)) {
                    LogPrintf("%s: Signing new block with UTXO key failed \n", __func__);
                    return nullptr;
                }
            }

            CValidationState state;
            if (!TestBlockValidity(state, *pblock, pindexPrev, false, false)) {
                if (state.GetRejectReason() != "time-too-new") {
                    LogPrintf("CreateNewBlock() : TestBlockValidity failed before quorum signing (%s)\n", state.GetRejectReason());
                }
                extern int nMintableLastCheck;
                nMintableLastCheck = 0;
                return nullptr;
            }
        } else {
            CValidationState state;
            CCoinsViewCache viewNew(pcoinsTip);
            CBlockIndex indexDummy(*pblock);
            indexDummy.pprev = pindexPrev;
            indexDummy.nHeight = nHeight;
            if (!CheckBlock(*pblock, state, false, true, false) ||
                !ContextualCheckBlock(*pblock, state, pindexPrev) ||
                !ConnectBlock(*pblock, state, &indexDummy, viewNew, true, true)) {
                LogPrintf("CreateNewBlock() : Pre-validation failed before requesting remote PoS coordinator signature (%s)\n", FormatStateMessage(state));
                extern int nMintableLastCheck;
                nMintableLastCheck = 0;
                return nullptr;
            }
        }
    }

    if (fNeedsRemotePoSCoordSig) {
        uint256 reqHash = ComputePoSCoordRequestHash(*pblock, expectedCoordinator);
        std::vector<unsigned char> vchStakerAuthSig;
        if (!SignPoSCoordRequest(*pblock, reqHash, *pwallet, vchStakerAuthSig)) {
            LogPrintf("CreateNewBlock: SignPoSCoordRequest failed for PoS draft on height %d\n", nHeight);
            return nullptr;
        }
        {
            LOCK(cs_pos_coord_sigs);
            setSeenPoSCoordReqs.insert(reqHash);
            if (setSeenPoSCoordReqs.size() > 1000) {
                auto itEvict = setSeenPoSCoordReqs.begin();
                if (itEvict != setSeenPoSCoordReqs.end() && *itEvict == reqHash) {
                    ++itEvict;
                }
                if (itEvict != setSeenPoSCoordReqs.end()) {
                    setSeenPoSCoordReqs.erase(itEvict);
                }
            }
        }

        CPoSCoordSignRequestMsg reqMsg;
        reqMsg.block = *pblock;
        reqMsg.targetCoordinator = expectedCoordinator;
        reqMsg.vchStakerAuthSig = std::move(vchStakerAuthSig);

        LogPrintf("CreateNewBlock: Requesting cooperative PoS coordinator co-signature from %s (req=%s, height=%d)\n",
            expectedCoordinator.GetID().ToString(), reqHash.ToString(), nHeight);

        g_connman->ForEachNode([&reqMsg](CNode* pnode) {
            g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::POSCOORDREQ, reqMsg));
        });

        int nWaitCount = 0;
        const int nMaxWaitCount = 60; // 6.0s max so PROPOSEBLOCK still fits cleanly inside the 15s slot
        bool fGotCoordSig = false;
        while (nWaitCount < nMaxWaitCount) {
            boost::this_thread::interruption_point();
            if (ShutdownRequested() || GetChainTip() != pindexPrev) {
                LogPrintf("CreateNewBlock: Aborting PoS coordinator co-sign wait (shutdown or chain tip updated)\n");
                return nullptr;
            }
            {
                LOCK(cs_pos_coord_sigs);
                auto itSig = mapPoSCoordSigs.find(reqHash);
                if (itSig != mapPoSCoordSigs.end()) {
                    const CPoSCoordSigEntry entry = itSig->second;
                    pblock->vAdamVRFProof = entry.vAdamVRFProof;
                    pblock->vAdamCoordinatorSig = entry.vAdamCoordinatorSig;
                    const uint256 blockHashSigned = pblock->GetHash();
                    if (VerifyAdamVRFProof(adamSeed, pblock->vAdamVRFProof, expectedCoordinator) &&
                        VerifyAdamCoordinatorSig(*pblock, expectedCoordinator, &blockHashSigned)) {
                        fGotCoordSig = true;
                        LogPrintf("CreateNewBlock: Received valid cooperative PoS coordinator co-signature from %s after %d ms\n",
                            expectedCoordinator.GetID().ToString(), nWaitCount * 100);
                        break;
                    } else {
                        pblock->vAdamVRFProof.clear();
                        pblock->vAdamCoordinatorSig.clear();
                        mapPoSCoordSigs.erase(itSig);
                    }
                }
            }
            if (nWaitCount == 25 && g_connman) {
                g_connman->ForEachNode([&reqMsg](CNode* pnode) {
                    g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::POSCOORDREQ, reqMsg));
                });
            }
            MilliSleep(100);
            nWaitCount++;
        }

        if (!fGotCoordSig) {
            LogPrintf("CreateNewBlock: Timed out waiting for PoS coordinator co-signature from %s (req=%s)\n",
                expectedCoordinator.GetID().ToString(), reqHash.ToString());
            return nullptr;
        }

        LogPrintf("CPUMiner : proof-of-stake block found %s \n", pblock->GetHash().GetHex());
        if (!SignBlock(*pblock, *pwallet)) {
            LogPrintf("%s: Signing co-signed PoS block with UTXO key failed\n", __func__);
            return nullptr;
        }

        {
            LOCK(cs_main);
            if (pindexPrev != GetChainTip()) {
                return nullptr;
            }
            CValidationState state;
            if (!TestBlockValidity(state, *pblock, pindexPrev, false, false)) {
                if (state.GetRejectReason() != "time-too-new") {
                    LogPrintf("CreateNewBlock() : TestBlockValidity failed after remote PoS coordinator signing (%s)\n", state.GetRejectReason());
                }
                extern int nMintableLastCheck;
                nMintableLastCheck = 0;
                return nullptr;
            }
        }
    }

    fFallbackMode = (pblock->nVersion == 11) || !IsModelDActive(nHeight);
    if (pblock->nVersion >= 12 && !fFallbackMode) {
        llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight, pindexPrev);
        if (!quorum.members.empty()) {
            llmq::CQuorumSignature qsig;
            qsig.blockHash = pblock->GetHash();

            // 1. Try to sign locally first if we hold any member private keys (using deterministic BLS12-381 + secp256k1 authorization)
            //    The anti-equivocation guard is shared with PROPOSEBLOCK so a hot masternode never signs two competing blocks.
            bool fLocalSignAllowed = true;
            for (const auto& member : quorum.members) {
                CKey key;
                if (llmq::GetMasternodePrivKey(member.pubKeyMasternode, key)) {
                    if (fLocalSignAllowed && !llmq::AllowQuorumSignature(pblock->hashPrevBlock, expectedCoordinator, qsig.blockHash)) {
                        fLocalSignAllowed = false;
                    }
                    if (!fLocalSignAllowed) break;
                    std::vector<unsigned char> sig;
                    CBLSSecretKey quorumBlsKey = DeriveBLSFromCKey(key);
                    if (quorumBlsKey.IsValid() && SignBLSWithECDSAFallback(qsig.blockHash, key, quorumBlsKey, sig)) {
                        qsig.signatures.push_back({member.collateralOutpoint, sig});
                        LOCK(cs_quorum_sigs);
                        if (mapQuorumBlockSigs.size() > 1000 && !mapQuorumBlockSigs.count(qsig.blockHash)) {
                            auto itEvict = mapQuorumBlockSigs.begin();
                            if (itEvict != mapQuorumBlockSigs.end() && itEvict->first == qsig.blockHash) {
                                ++itEvict;
                            }
                            if (itEvict != mapQuorumBlockSigs.end()) {
                                mapQuorumBlockSigs.erase(itEvict);
                            }
                        }
                        mapQuorumBlockSigs[qsig.blockHash][member.collateralOutpoint] = sig;
                    }
                }
            }

            size_t nThreshold = quorum.members.size() / 2 + 1;
            if (nThreshold < 2) nThreshold = 2;
            if (quorum.members.size() < nThreshold) nThreshold = quorum.members.size();

            // 2. If local signatures are insufficient, request via P2P
            if (qsig.signatures.size() < nThreshold) {
                LogPrintf("CreateNewBlock: Local signatures (%u/%u) not enough. Broadcasting proposed block template to peers...\n",
                           qsig.signatures.size(), nThreshold);

                {
                    LOCK(cs_seen_prop);
                    setSeenProposedBlocks.insert(qsig.blockHash);
                    if (setSeenProposedBlocks.size() > 1000) {
                        auto itEvict = setSeenProposedBlocks.begin();
                        if (itEvict != setSeenProposedBlocks.end() && *itEvict == qsig.blockHash) {
                            ++itEvict;
                        }
                        if (itEvict != setSeenProposedBlocks.end()) {
                            setSeenProposedBlocks.erase(itEvict);
                        }
                    }
                }

                CBlockProposeMsg propMsg;
                propMsg.block = *pblock;

                if (g_connman) {
                    g_connman->ForEachNode([&propMsg](CNode* pnode) {
                        g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::PROPOSEBLOCK, propMsg));
                    });
                }

                // Poll mapQuorumBlockSigs for incoming signatures (wait up to 12 seconds, within one 15s TimeProtocolV2 slot)
                int nWaitCount = 0;
                const int nMaxWaitCount = 120;
                while (nWaitCount < nMaxWaitCount) {
                    boost::this_thread::interruption_point();
                    if (ShutdownRequested() || GetChainTip() != pindexPrev) {
                        LogPrintf("CreateNewBlock: Aborting quorum signature wait (shutdown or chain tip updated)\n");
                        return nullptr;
                    }
                    {
                        LOCK(cs_quorum_sigs);
                        if (mapQuorumBlockSigs.count(qsig.blockHash)) {
                            for (const auto& pair : mapQuorumBlockSigs[qsig.blockHash]) {
                                bool exists = false;
                                for (const auto& existing : qsig.signatures) {
                                    if (existing.first == pair.first) {
                                        exists = true;
                                        break;
                                    }
                                }
                                if (!exists) {
                                    CPubKey memberPubKey;
                                    bool isActiveMember = false;
                                    for (const auto& member : quorum.members) {
                                        if (member.collateralOutpoint == pair.first) {
                                            memberPubKey = member.pubKeyMasternode;
                                            isActiveMember = true;
                                            break;
                                        }
                                    }
                                    if (isActiveMember && VerifyBLSWithECDSAFallback(qsig.blockHash, memberPubKey, pair.second)) {
                                        qsig.signatures.push_back({pair.first, pair.second});
                                    }
                                }
                            }
                        }
                    }

                    if (qsig.signatures.size() >= nThreshold) {
                        LogPrintf("CreateNewBlock: Quorum threshold met! Collected %u/%u signatures after waiting %d ms\n",
                                  qsig.signatures.size(), nThreshold, nWaitCount * 100);
                        break;
                    }

                    // Re-broadcast PROPOSEBLOCK once at 4 seconds in case a peer missed the initial P2P packet
                    if (nWaitCount == 40 && g_connman) {
                        g_connman->ForEachNode([&propMsg](CNode* pnode) {
                            g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::PROPOSEBLOCK, propMsg));
                        });
                    }

                    MilliSleep(100);
                    nWaitCount++;
                }
            }

            if (qsig.signatures.size() < nThreshold) {
                LogPrintf("CreateNewBlock ERROR: Failed to collect enough quorum signatures (%u/%u) for block %s\n",
                           qsig.signatures.size(), nThreshold, qsig.blockHash.ToString());
                return nullptr;
            }

            CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
            ss << qsig;
            pblock->vQuorumSig = std::vector<unsigned char>(ss.begin(), ss.end());
            LogPrintf("CreateNewBlock: Successfully generated decentralized LLMQ block signature with %u signatures\n",
                      qsig.signatures.size());
        }
    }

    {
        LOCK(cs_main);
        if (pindexPrev != GetChainTip()) {
            LogPrintf("CreateNewBlock: chain tip changed while waiting for signatures\n");
            return nullptr;
        }
    }

    proposalGuard.MarkSucceeded();
    return pblocktemplate.release();
}

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock) {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;
    unsigned int nHeight = pindexPrev->nHeight + 1; // Height first in coinbase required for block.version=2
    CMutableTransaction txCoinbase(pblock->vtx[0]);
    txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
    assert(txCoinbase.vin[0].scriptSig.size() <= 100);

    pblock->vtx[0] = txCoinbase;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
}

#ifdef ENABLE_WALLET
//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
double dHashesPerSec = 0.0;
int64_t nHPSTimerStart = 0;

CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet)
{
    if (!chainActive.Tip())
        return nullptr;

    const int nHeightNext = chainActive.Tip()->nHeight + 1;

    // If we're building a late PoW block, don't continue
    // PoS blocks are built directly with CreateNewBlock
    if (Params().GetConsensus().NetworkUpgradeActive(nHeightNext, Consensus::UPGRADE_POS) &&
        !IsAdamActive(nHeightNext, Params().GetConsensus())) {
        LogPrintf("%s: Aborting PoW block creation during PoS phase\n", __func__);
        // sleep 1/2 a block time so we don't go into a tight loop.
        MilliSleep((Params().GetConsensus().nTargetSpacing * 1000) >> 1);
        return nullptr;
    }

    CPubKey pubkey;
    bool gotKey = false;

    std::string strMinerPubKey = GetArg("-minerpubkey", "");
    if (!strMinerPubKey.empty()) {
        if (IsHex(strMinerPubKey)) {
            CPubKey pk(ParseHex(strMinerPubKey));
            if (pk.IsValid()) {
                pubkey = pk;
                gotKey = true;
            }
        }
    }

    std::string strMinerAddress = GetArg("-mineraddress", "");
    if (!gotKey && !strMinerAddress.empty() && pwallet) {
        CTxDestination dest = DecodeDestination(strMinerAddress);
        const CKeyID* keyID = boost::get<CKeyID>(&dest);
        if (keyID) {
            CPubKey pk;
            if (pwallet->GetPubKey(*keyID, pk) && pk.IsValid()) {
                pubkey = pk;
                gotKey = true;
            }
        }
    }

    // In ADAM cooperative PoW mode on Mainnet/Testnet (unless explicitly overridden via -minerpubkey / -mineraddress),
    // defer Producer coinbase script resolution to CreateNewBlock so the exact elected/fallback expectedCoordinator
    // (or its Masternode cold collateral payout address) receives vout[0] without allocating unused reservekey addresses.
    if (!gotKey && !Params().IsRegTestNet() && IsAdamActive(nHeightNext, Params().GetConsensus())) {
        return CreateNewBlock(CScript(), pwallet, false);
    }

    if (!gotKey && (fMasterNode || !amnodeman.GetActiveMasternodes().empty())) {
        for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
            if (activeMasternode.pubKeyMasternode.IsValid()) {
                CMasternode* pmn = mnodeman.Find(activeMasternode.pubKeyMasternode);
                if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                    pubkey = pmn->pubKeyCollateralAddress;
                } else {
                    pubkey = activeMasternode.pubKeyMasternode;
                }
                gotKey = true;
                break;
            }
        }
    }
    if (!gotKey) {
        if (!reservekey.GetReservedKey(pubkey))
            return nullptr;
    }

    CScript scriptPubKey = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_CHECKSIG;
    return CreateNewBlock(scriptPubKey, pwallet, false);
}

bool ProcessBlockFound(CBlock* pblock, CWallet& wallet, Optional<CReserveKey>& reservekey)
{
    LogPrintf("%s\n", pblock->ToString());
    LogPrintf("generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

    // Found a solution
    {
        WAIT_LOCK(g_best_block_mutex, lock);
        if (pblock->hashPrevBlock != g_best_block)
            return error("Miner : generated block is stale");
    }

    // Remove key from key pool
    if (reservekey)
        reservekey->KeepKey();

    // Inform about the new block
    GetMainSignals().BlockFound(pblock->GetHash());

    // Process this block the same as if we had received it from another node
    CValidationState state;
    if (!ProcessNewBlock(state, nullptr, pblock, nullptr, g_connman.get())) {
        ReleaseParentBlockInFlight(pblock->hashPrevBlock);
        return error("Miner : ProcessNewBlock, block not accepted");
    }

    g_connman->ForEachNode([&pblock](CNode* node)
    {
        node->PushInventory(CInv(MSG_BLOCK, pblock->GetHash()));
    });

    return true;
}

bool fGenerateBitcoins = false;
static std::atomic<int> g_nConfiguredMinerThreads{0};
int GetConfiguredMinerThreads()
{
    return g_nConfiguredMinerThreads.load();
}
bool fStakeableCoins = false;
bool fMasternodeSync = false;
int nMintableLastCheck = 0;

void CheckForCoins(CWallet* pwallet, const int minutes, std::vector<COutput>* availableCoins)
{
    //control the amount of times the client will check for mintable coins
    int nTimeNow = GetTime();
    static uint256 hashLastBlock = UINT256_ZERO;
    uint256 hashTip = chainActive.Tip() ? chainActive.Tip()->GetBlockHash() : UINT256_ZERO;
    if (hashTip != hashLastBlock || (nTimeNow - nMintableLastCheck > minutes * 60)) {
        hashLastBlock = hashTip;
        nMintableLastCheck = nTimeNow;
        if (!Params().IsRegTestNet() && IsInitialBlockDownload()) {
            fMasternodeSync = false;
            fStakeableCoins = false;
            return;
        }
        fStakeableCoins = pwallet->StakeableCoins(availableCoins);
        fMasternodeSync = sporkManager.IsSporkActive(SPORK_106_STAKING_SKIP_MN_SYNC) || !masternodeSync.NotCompleted();
        if (!IsModelDActive(chainActive.Height()) || Params().IsRegTestNet()) {
            fMasternodeSync = true;
        }
    }
}
#ifdef ENABLE_WALLET
static std::mutex cs_auto_register_thread;
static std::thread autoRegisterThread;

void AutoRegisterMiner(CWallet* pwallet, const CPubKey& pubkey)
{
    if (!pwallet || !pubkey.IsValid() || !pubkey.IsCompressed() || ShutdownRequested() || !fGenerateBitcoins) return;

    if (!Params().IsRegTestNet() && (IsInitialBlockDownload() || (IsModelDActive(chainActive.Height() + 1) && !masternodeSync.IsSynced()))) {
        return;
    }

    static std::atomic<bool> fAutoRegisterRunning{false};
    bool expected = false;
    if (!fAutoRegisterRunning.compare_exchange_strong(expected, true)) {
        return;
    }
    struct AutoRegisterGuard {
        std::atomic<bool>& flag;
        ~AutoRegisterGuard() { flag.store(false); }
    } guard{fAutoRegisterRunning};

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    // Check if the wallet has already registered this miner key in the active pool
    {
        LOCK(cs_main);
        std::vector<CPubKey> pool = GetAdamMinerPool(chainActive.Height());
        for (const auto& key : pool) {
            if (ComparePubKeys(key, pubkey)) {
                // Already registered!
                return;
            }
        }
    }

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    // Check if the mempool already contains a registration for this pubkey
    {
        LOCK(mempool.cs);
        for (const auto& entry : mempool.mapTx) {
            const CTransaction& tx = entry.GetTx();
            for (const auto& vout : tx.vout) {
                CPubKey pkey;
                int64_t lockTime = 0;
                CKeyID pubkeyHash;
                if (MatchCoinLockRegistration(vout.scriptPubKey, pkey, lockTime, pubkeyHash)) {
                    if (ComparePubKeys(pkey, pubkey)) {
                        LogPrintf("AutoRegisterMiner: A Coin-Lock registration transaction for key %s is already in the mempool. Skipping.\n", pubkey.GetID().ToString());
                        return;
                    }
                }
                std::vector<unsigned char> nonce;
                uint256 challenge;
                if (MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, pkey, lockTime, pubkeyHash)) {
                    if (ComparePubKeys(pkey, pubkey)) {
                        LogPrintf("AutoRegisterMiner: A PoW-Lock registration transaction for key %s is already in the mempool. Skipping.\n", pubkey.GetID().ToString());
                        return;
                    }
                }
            }
        }
    }

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    // Check if there is an active unconfirmed registration in the wallet
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        int64_t nNow = GetAdjustedTime();
        for (auto it = pwallet->wtxOrdered.rbegin(); it != pwallet->wtxOrdered.rend(); ++it) {
            const CWalletTx* pwtx = it->second.first;
            if (!pwtx) continue;
            const CWalletTx& wtx = *pwtx;
            if (wtx.GetDepthInMainChain() == 0 && !wtx.isAbandoned() && (wtx.InMempool() || nNow - wtx.nTimeReceived < 300)) {
                for (const auto& vout : wtx.vout) {
                    CPubKey pkey;
                    int64_t lockTime = 0;
                    CKeyID pubkeyHash;
                    if (MatchCoinLockRegistration(vout.scriptPubKey, pkey, lockTime, pubkeyHash)) {
                        if (ComparePubKeys(pkey, pubkey)) {
                            LogPrintf("AutoRegisterMiner: An unconfirmed Coin-Lock registration transaction for key %s exists in the wallet. Skipping.\n", pubkey.GetID().ToString());
                            return;
                        }
                    }
                    std::vector<unsigned char> nonce;
                    uint256 challenge;
                    if (MatchPoWLockRegistration(vout.scriptPubKey, nonce, challenge, pkey, lockTime, pubkeyHash)) {
                        if (ComparePubKeys(pkey, pubkey)) {
                            LogPrintf("AutoRegisterMiner: An unconfirmed PoW-Lock registration transaction for key %s exists in the wallet. Skipping.\n", pubkey.GetID().ToString());
                            return;
                        }
                    }
                }
            }
        }
    }

    static int nLastRegSendHeight = 0;
    if (nLastRegSendHeight > 0 && chainActive.Height() - nLastRegSendHeight < 50) {
        // We already sent a registration transaction recently (less than 50 blocks ago)
        return;
    }

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    LogPrintf("AutoRegisterMiner: Miner key %s is not registered in the ADAM pool. Attempting auto-registration...\n", pubkey.GetID().ToString());

    if (pwallet->IsLocked()) {
        LogPrintf("AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.\n");
        return;
    }

    // Deterministically derive and persist the canonical BLS key from the ECDSA private key
    {
        LOCK(pwallet->cs_wallet);
        CKey minerPrivKey;
        if (pwallet->GetKey(pubkey.GetID(), minerPrivKey) && minerPrivKey.IsValid()) {
            CBLSSecretKey blsKey = DeriveBLSFromCKey(minerPrivKey);
            if (blsKey.IsValid()) {
                CBLSSecretKey existingBls;
                if (!pwallet->GetBLSKey(pubkey.GetID(), existingBls) || CBLSPubKey(existingBls) != CBLSPubKey(blsKey)) {
                    pwallet->AddBLSKey(pubkey.GetID(), blsKey);
                }
            }
        }
    }

    CScript scriptPubKey;
    CAmount nAmount = 0;

    while (true) {
        if (ShutdownRequested() || !fGenerateBitcoins) return;
        CAmount balance = pwallet->GetAvailableBalance() + pwallet->GetUnconfirmedBalance();
        int nCurrentHeight = 0;
        {
            LOCK(cs_main);
            nCurrentHeight = chainActive.Height();
        }

        if (nCurrentHeight >= 2200 && balance >= MINER_REGISTRATION_LOCK_AMOUNT + 1 * CENT) {
            // We have enough balance to do a Coin-Lock (PoL) registration!
            int64_t locktime = nCurrentHeight + 2900;
            scriptPubKey = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
            nAmount = MINER_REGISTRATION_LOCK_AMOUNT;
            LogPrintf("AutoRegisterMiner: Selecting Coin-Lock (PoL) registration (lock amount: %d KRISTA, locktime: %d blocks)\n", MINER_REGISTRATION_LOCK_AMOUNT / COIN, locktime);
            break;
        } else {
            // Fallback to PoW registration
            static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
            uint256 challengeHash;
            uint256 target;
            int64_t currentHeight = 0;
            {
                LOCK2(cs_main, pwallet->cs_wallet);
                if (chainActive.Tip()) {
                    challengeHash = chainActive.Tip()->GetBlockHash();
                } else {
                    LogPrintf("AutoRegisterMiner: Chain tip is null. Cannot perform PoW registration.\n");
                    return;
                }
                currentHeight = chainActive.Height();
                const size_t nExplicitPoolSize = GetExplicitAdamMinerPoolCount(currentHeight);
                const size_t maxBootstrapPoolSize = (size_t)(Params().GetConsensus().nAdamMinersCount + 2);
                const bool fDualTrackNext = IsAdamDualTrackActive(currentHeight + 1);

                CAmount nMatureSelected = 0;
                std::vector<COutput> vCoins;
                pwallet->AvailableCoins(&vCoins, nullptr, ALL_COINS, true);
                for (const COutput& out : vCoins) {
                    if (out.fSpendable && (!fDualTrackNext || out.nDepth >= ADAM_POW_LOCK_MIN_INPUT_DEPTH)) {
                        if (out.nDepth >= ADAM_POW_LOCK_MIN_INPUT_DEPTH || (!fDualTrackNext && nExplicitPoolSize >= maxBootstrapPoolSize)) {
                            nMatureSelected += out.Value();
                        }
                    }
                }

                if (nMatureSelected >= POW_LOCK_REGISTRATION_AMOUNT + (COIN / 100)) {
                    nAmount = POW_LOCK_REGISTRATION_AMOUNT;
                    target = GetMinerPoWLimit(Params().NetworkIDString());
                } else if (nExplicitPoolSize < maxBootstrapPoolSize) {
                    nAmount = 0;
                    target = GetZeroCoinPoWLimit(Params().NetworkIDString());
                    LOCK(mempool.cs);
                    if (mempool.mapNextTx.count(zeroCoinSentinel) > 0) {
                        LogPrintf("AutoRegisterMiner: Another Zero-Coin PoW registration is already pending in mempool; deferring until next block.\n");
                        return;
                    }
                } else {
                    LogPrintf("AutoRegisterMiner: Zero-Coin bootstrap pool is full (%u/%u explicit validators) and wallet lacks 10.01 KRISTA with %d confirmations.\n",
                              (unsigned int)nExplicitPoolSize, (unsigned int)maxBootstrapPoolSize, ADAM_POW_LOCK_MIN_INPUT_DEPTH);
                    nLastRegSendHeight = currentHeight - 30;
                    return;
                }
            }

            uint32_t nonce = 0;
            uint256 puzzleHash;
            LogPrintf("AutoRegisterMiner: Starting PoW search (%s) for challenge %s...\n",
                      nAmount == 0 ? "Zero-Coin 24-bit" : "Funded 28-bit", challengeHash.ToString());
            bool fOutdated = false;
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
                if ((nonce & 0xFFF) == 0) {
                    if (ShutdownRequested() || !fGenerateBitcoins) {
                        LogPrintf("AutoRegisterMiner: PoW search cancelled due to shutdown\n");
                        return;
                    }
                }
                if ((nonce & 0x1FFFF) == 0) {
                    int nTipHeightNow = 0;
                    {
                        LOCK(cs_main);
                        nTipHeightNow = chainActive.Height();
                    }
                    if (nTipHeightNow - currentHeight > 50) {
                        LogPrintf("AutoRegisterMiner: Challenge hash is outdated (tip height: %d vs challenge height: %d). Restarting PoW search with latest block...\n", 
                            nTipHeightNow, currentHeight);
                        fOutdated = true;
                        break;
                    }
                    // Abort PoW search if we got registered in active pool in the meantime
                    {
                        LOCK(cs_main);
                        std::vector<CPubKey> pool = GetAdamMinerPool(chainActive.Height());
                        for (const auto& key : pool) {
                            if (key == pubkey) {
                                LogPrintf("AutoRegisterMiner: Key %s is now registered in the pool. Aborting PoW search.\n", pubkey.GetID().ToString());
                                return;
                            }
                        }
                    }
                    if (ShutdownRequested() || !fGenerateBitcoins) return;
                    // Abort PoW search if a registration was broadcasted to the mempool in the meantime
                    {
                        LOCK(mempool.cs);
                        if (nAmount == 0 && mempool.mapNextTx.count(zeroCoinSentinel) > 0) {
                            LogPrintf("AutoRegisterMiner: Another Zero-Coin PoW registration entered the mempool. Deferring until next block.\n");
                            return;
                        }
                        for (const auto& entry : mempool.mapTx) {
                            const CTransaction& tx = entry.GetTx();
                            for (const auto& vout : tx.vout) {
                                CPubKey pkey;
                                int64_t lockTime = 0;
                                CKeyID pubkeyHash;
                                if (MatchCoinLockRegistration(vout.scriptPubKey, pkey, lockTime, pubkeyHash)) {
                                    if (pkey == pubkey) {
                                        LogPrintf("AutoRegisterMiner: A registration for key %s is now in the mempool. Aborting PoW search.\n", pubkey.GetID().ToString());
                                        return;
                                    }
                                }
                                std::vector<unsigned char> vchNonce;
                                uint256 challenge;
                                if (MatchPoWLockRegistration(vout.scriptPubKey, vchNonce, challenge, pkey, lockTime, pubkeyHash)) {
                                    if (pkey == pubkey) {
                                        LogPrintf("AutoRegisterMiner: A registration for key %s is now in the mempool. Aborting PoW search.\n", pubkey.GetID().ToString());
                                        return;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (fOutdated) {
                continue; // Restart the outer loop to get a fresh challenge!
            }
            
            LogPrintf("AutoRegisterMiner: Found PoW solution! nonce=%u, hash=%s\n", nonce, puzzleHash.ToString());

            int64_t locktime = currentHeight + 2900;
            CDataStream ssNonce(SER_NETWORK, PROTOCOL_VERSION);
            ssNonce << nonce;
            scriptPubKey = CScript() << std::vector<unsigned char>(ssNonce.begin(), ssNonce.end())
                                     << std::vector<unsigned char>(challengeHash.begin(), challengeHash.end())
                                     << std::vector<unsigned char>(pubkey.begin(), pubkey.end())
                                     << OP_DROP << OP_DROP << OP_DROP
                                     << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                     << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
            LogPrintf("AutoRegisterMiner: Selecting PoW-Lock registration (amount: %lld, locktime: %d blocks)\n", (long long)nAmount, locktime);
            break;
        }
    }

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    CWalletTx wtx;
    
    // Create and commit the transaction
    bool created = false;
    if (nAmount == 0) {
        static const COutPoint zeroCoinSentinel(uint256S("0000000000000000000000000000000000000000000000000000000000000001"), 0);
        {
            LOCK(mempool.cs);
            if (mempool.mapNextTx.count(zeroCoinSentinel) > 0) {
                LogPrintf("AutoRegisterMiner: Another Zero-Coin PoW registration is already in mempool; deferring until next block.\n");
                return;
            }
        }
        CMutableTransaction mtx;
        mtx.nVersion = 1;
        mtx.nLockTime = 0;
        CTxIn txin;
        txin.prevout = zeroCoinSentinel;
        mtx.vin.push_back(txin);
        CTxOut txout(0, scriptPubKey);
        mtx.vout.push_back(txout);
        wtx = CWalletTx(pwallet, mtx);
        wtx.fFromMe = true;
        created = true;
    } else {
        LOCK2(cs_main, pwallet->cs_wallet);
        if (nAmount == POW_LOCK_REGISTRATION_AMOUNT && IsAdamDualTrackActive(chainActive.Height() + 1)) {
            CCoinControl powCoinControl;
            powCoinControl.fAllowOtherInputs = false;
            std::vector<COutput> vCoins;
            pwallet->AvailableCoins(&vCoins, nullptr, ALL_COINS, true);
            CAmount nSelected = 0;
            const CAmount nMinRequired = nAmount + (COIN / 100);
            for (const COutput& out : vCoins) {
                if (out.fSpendable && out.nDepth >= ADAM_POW_LOCK_MIN_INPUT_DEPTH) {
                    powCoinControl.Select(COutPoint(out.tx->GetHash(), out.i));
                    nSelected += out.Value();
                    if (nSelected >= nMinRequired) {
                        break;
                    }
                }
            }
            if (!powCoinControl.HasSelected() || nSelected < nAmount) {
                LogPrintf("AutoRegisterMiner: Waiting for an input UTXO with at least %d confirmations for PoW-Lock registration.\n",
                          ADAM_POW_LOCK_MIN_INPUT_DEPTH);
                nLastRegSendHeight = chainActive.Height() - 30;
                return;
            }
            created = pwallet->CreateTransaction(scriptPubKey, nAmount, wtx, reservekey, nFeeRequired, strError, &powCoinControl, ALL_COINS, (CAmount)0);
        } else {
            created = pwallet->CreateTransaction(scriptPubKey, nAmount, wtx, reservekey, nFeeRequired, strError, nullptr, ALL_COINS, (CAmount)0);
        }
    }

    if (!created) {
        LogPrintf("AutoRegisterMiner ERROR: CreateTransaction failed: %s\n", strError);
        nLastRegSendHeight = chainActive.Height() - 30; // backoff 20 blocks before retrying
        return;
    }

    if (ShutdownRequested() || !fGenerateBitcoins) return;

    CWallet::CommitResult res;
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        res = pwallet->CommitTransaction(wtx, reservekey, g_connman.get());
    }

    if (res.status != CWallet::CommitStatus::OK) {
        LogPrintf("AutoRegisterMiner ERROR: CommitTransaction failed!\n");
        nLastRegSendHeight = chainActive.Height() - 30; // backoff 20 blocks before retrying
        return;
    }

    nLastRegSendHeight = chainActive.Height();
    LogPrintf("AutoRegisterMiner: Successfully broadcasted registration transaction %s\n", wtx.GetHash().GetHex());
}
#endif

void BitcoinMiner(CWallet* pwallet, bool fProofOfStake)
{
    LogPrintf("Miner started\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    util::ThreadRename("kristatech-miner");
    const Consensus::Params& consensus = Params().GetConsensus();
    const int64_t nSpacingMillis = consensus.nTargetSpacing * 1000;

    // Each thread has its own key and counter
    Optional<CReserveKey> opReservekey{nullopt};
    if (!fProofOfStake) {
        opReservekey = CReserveKey(pwallet);
    }

    // Available UTXO set
    std::vector<COutput> availableCoins;
    unsigned int nExtraNonce = 0;

    while ((fGenerateBitcoins || fProofOfStake) && !ShutdownRequested()) {
        CBlockIndex* pindexPrev = GetChainTip();
        if (!pindexPrev) {
            MilliSleep(nSpacingMillis); // sleep a block
            continue;
        }

        if (Params().MiningRequiresPeers() && g_connman && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
            MilliSleep(1000);
            continue;
        }

        if (!Params().IsRegTestNet() && (IsInitialBlockDownload() || (IsModelDActive(pindexPrev->nHeight + 1) && !masternodeSync.IsSynced()))) {
            MilliSleep(1000);
            continue;
        }

        // POW - Elected Miner Background Solving Loop
        std::atomic<bool> fHasUnsolvedLocalTasks{false};
        if (!fProofOfStake && IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
#ifdef ENABLE_WALLET
            static std::mutex cs_reg_check;
            static uint256 hashLastRegCheck;
            bool shouldCheckReg = false;
            {
                std::lock_guard<std::mutex> regLock(cs_reg_check);
                if (pwallet && pindexPrev->GetBlockHash() != hashLastRegCheck) {
                    hashLastRegCheck = pindexPrev->GetBlockHash();
                    shouldCheckReg = true;
                }
            }
            if (shouldCheckReg) {
                CPubKey minerPubKey;
                bool gotKey = false;
                std::string strMinerPubKey = GetArg("-minerpubkey", "");
                if (!strMinerPubKey.empty() && IsHex(strMinerPubKey)) {
                    CPubKey pk(ParseHex(strMinerPubKey));
                    if (pk.IsValid()) {
                        minerPubKey = pk;
                        gotKey = true;
                    }
                }
                std::string strMinerAddress = GetArg("-mineraddress", "");
                if (!gotKey && !strMinerAddress.empty() && pwallet) {
                    CTxDestination dest = DecodeDestination(strMinerAddress);
                    const CKeyID* keyID = boost::get<CKeyID>(&dest);
                    if (keyID) {
                        CPubKey pk;
                        if (pwallet->GetPubKey(*keyID, pk) && pk.IsValid()) {
                            minerPubKey = pk;
                            gotKey = true;
                        }
                    }
                }
                const bool fHasActiveMasternodes = fMasterNode || !amnodeman.GetActiveMasternodes().empty();
                if (!gotKey && !fHasActiveMasternodes && pwallet) {
                    LOCK2(cs_main, pwallet->cs_wallet);
                    std::vector<CPubKey> pool = GetAdamMinerPool(pindexPrev->nHeight);
                    for (const auto& poolKey : pool) {
                        if (poolKey.IsCompressed() && pwallet->HaveKey(poolKey.GetID())) {
                            minerPubKey = poolKey;
                            gotKey = true;
                            break;
                        }
                    }
                }
                if (!gotKey && !fHasActiveMasternodes && opReservekey) {
                    opReservekey->GetReservedKey(minerPubKey);
                    gotKey = minerPubKey.IsValid() && minerPubKey.IsCompressed();
                }

                if (gotKey && minerPubKey.IsValid() && minerPubKey.IsCompressed() && !ShutdownRequested() && fGenerateBitcoins) {
                    std::lock_guard<std::mutex> lock(cs_auto_register_thread);
                    if (autoRegisterThread.joinable()) {
                        autoRegisterThread.join();
                    }
                    if (!ShutdownRequested() && fGenerateBitcoins) {
                        autoRegisterThread = std::thread(AutoRegisterMiner, pwallet, minerPubKey);
                    }
                }
            }
#endif
            static std::mutex cs_adam_solver_dispatch;
            std::unique_lock<std::mutex> solverDispatchLock(cs_adam_solver_dispatch, std::try_to_lock);
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            std::vector<CPubKey> vStandbyMiners;
            CPubKey expectedCoordinator;
            if (solverDispatchLock.owns_lock() &&
                SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, pindexPrev->nHeight + 1, nullptr, &vStandbyMiners)) {
                int64_t nElapsedTipSecs = std::max<int64_t>(0, GetAdjustedTime() - pindexPrev->GetBlockTime());
                std::vector<CPubKey> vSolverKeys = vExpectedMiners;
                std::vector<bool> vStandbyUnlocked(vStandbyMiners.size(), false);
                if (nElapsedTipSecs >= ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS && !vStandbyMiners.empty()) {
                    int nSlot = 1 + (int)((nElapsedTipSecs - ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS) / ADAM_COMMITTEE_FALLBACK_SLOT_SECS);
                    std::set<CPubKey> mnKeys, coinLockKeys;
                    const bool fDualTrack = IsAdamDualTrackActive(pindexPrev->nHeight + 1);
                    if (fDualTrack) {
                        GetAdamMinerPool(pindexPrev->nHeight, nullptr, &mnKeys, &coinLockKeys);
                    }
                    auto getTrack = [&](const CPubKey& k) -> int {
                        if (!fDualTrack) return 0;
                        if (mnKeys.count(k) > 0) return 0;
                        if (coinLockKeys.count(k) > 0) return 1;
                        return 2;
                    };
                    size_t nPrimaryInTrack[3] = {0, 0, 0};
                    for (const auto& pk : vExpectedMiners) {
                        nPrimaryInTrack[getTrack(pk)]++;
                    }
                    size_t nStandbySeenInTrack[3] = {0, 0, 0};
                    for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
                        int t = getTrack(vStandbyMiners[s]);
                        if (nStandbySeenInTrack[t] < nPrimaryInTrack[t] + (size_t)nSlot) {
                            vStandbyUnlocked[s] = true;
                        }
                        nStandbySeenInTrack[t]++;
                        vSolverKeys.push_back(vStandbyMiners[s]);
                    }
                }
                struct AdamMinerSolveTask {
                    size_t i;
                    CPubKey myMinerKey;
                    CKey privKey;
                    CBLSSecretKey blsKey;
                    int algoIndex;
                    int algo1, algo2, algo3;
                    bool fV12;
                    uint256 scaledTarget;
                    uint32_t startNonce;
                };
                std::vector<AdamMinerSolveTask> vTasks;
                static uint256 hashLastResumeTip;
                static std::map<CPubKey, uint32_t> mapResumeNonce;
                {
                    LOCK(cs_adam_solutions);
                    if (hashLastResumeTip != pindexPrev->GetBlockHash()) {
                        hashLastResumeTip = pindexPrev->GetBlockHash();
                        mapResumeNonce.clear();
                    }
                }

                for (size_t i = 0; i < vSolverKeys.size(); ++i) {
                    if (i >= vExpectedMiners.size() && !vStandbyUnlocked[i - vExpectedMiners.size()]) {
                        continue;
                    }
                    const auto& myMinerKey = vSolverKeys[i];

                    // Check if we own this key
                    CKey privKey;
                    bool ownsKey = HasCoordinatorKey(myMinerKey, pwallet, privKey);
                    if (!ownsKey || !privKey.IsValid()) {
                        continue;
                    }
                    
                    bool alreadySolved = false;
                    bool hasSol = false;
                    std::vector<unsigned char> vchSol;
                    {
                        LOCK(cs_adam_solutions);
                        auto it = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                        if (it != mapAdamSolutionsCache.end() && it->second.count(myMinerKey)) {
                            vchSol = it->second.at(myMinerKey);
                            hasSol = true;
                        }
                    }
                    if (hasSol) {
                        bool fV12 = consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POMBL);
                        int nVersion = fV12 ? 12 : 11;
                        CBlockHeader dummyHeader;
                        dummyHeader.nVersion = nVersion;
                        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
                        if (VerifyAdamSolution(pindexPrev->GetBlockHash(), adamSeed, myMinerKey, vchSol, nBits, nVersion, pindexPrev->nHeight + 1, (int)i)) {
                            alreadySolved = true;
                        } else {
                            LogPrintf("BitcoinMiner: Cached solution for %s is invalid for version %d, erasing from cache to re-solve\n",
                                myMinerKey.GetID().ToString(), nVersion);
                            LOCK(cs_adam_solutions);
                            auto it = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                            if (it != mapAdamSolutionsCache.end()) {
                                it->second.erase(myMinerKey);
                            }
                        }
                    }
                    if (alreadySolved) continue;

                    {
                        CBLSSecretKey blsKey = DeriveBLSFromCKey(privKey);

                        bool fV12 = consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POMBL);
                        int algoIndex = 12;
                        int algo1 = -1, algo2 = -1, algo3 = -1;
                        if (!fV12) {
                            algoIndex = GetAdamPuzzleAlgo(adamSeed, myMinerKey, true);
                        } else {
                            GetAdam3PermutationAlgos(pindexPrev->GetBlockHash(), myMinerKey, algo1, algo2, algo3);
                        }

                        CBlockHeader dummyHeader;
                        int nNextHeight = pindexPrev->nHeight + 1;
                        if (fV12) {
                            dummyHeader.nVersion = 12;
                        } else {
                            dummyHeader.nVersion = 11;
                        }
                        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
                        uint256 bnTarget = uint256().SetCompact(nBits);
                        uint256 scaledTarget = bnTarget;
                        if (!Params().IsRegTestNet()) {
                            bool fFallbackMode = (dummyHeader.nVersion == 11) || !IsModelDActive(nNextHeight);
                            int shift = fFallbackMode ? consensus.nAdamDifficultyShiftV1 :
                                        ((consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_ADAM_V2)) ? 
                                         consensus.nAdamDifficultyShiftV2 : consensus.nAdamDifficultyShiftV1);
                            scaledTarget = bnTarget << shift;
                            uint256 powLimit = consensus.powLimit;
                            if (scaledTarget > powLimit || scaledTarget < bnTarget) {
                                scaledTarget = powLimit;
                            }
                        }

                        AdamMinerSolveTask task;
                        task.i = i;
                        task.myMinerKey = myMinerKey;
                        task.privKey = privKey;
                        task.blsKey = blsKey;
                        task.algoIndex = algoIndex;
                        task.algo1 = algo1;
                        task.algo2 = algo2;
                        task.algo3 = algo3;
                        task.fV12 = fV12;
                        task.scaledTarget = scaledTarget;
                        {
                            LOCK(cs_adam_solutions);
                            task.startNonce = mapResumeNonce[myMinerKey];
                        }
                        vTasks.push_back(task);
                    }
                }

                if (!vTasks.empty()) {
                    uint256 tipHash = pindexPrev->GetBlockHash();
                    const size_t nTotalBudget = std::max<size_t>(1, (size_t)std::max<int>(1, GetConfiguredMinerThreads()));
                    const size_t nWorkersPerTask = std::min<size_t>(16, std::max<size_t>(1, nTotalBudget / vTasks.size()));
                    boost::thread_group solverThreads;
                    for (const auto& task : vTasks) {
                        auto pTaskSolved = std::make_shared<std::atomic<bool>>(false);
                        for (size_t w = 0; w < nWorkersPerTask; ++w) {
                            solverThreads.create_thread([task, w, nWorkersPerTask, pTaskSolved, adamSeed, tipHash, pindexPrev, &fHasUnsolvedLocalTasks]() {
                                SetThreadPriority(THREAD_PRIORITY_LOWEST);
                                if (w == 0 && task.startNonce == 0) {
                                    if (task.fV12) {
                                        LogPrintf("BitcoinMiner: Elected miner at index %d (algos %d, %d and %d) for tip %s. Solving puzzle across %u worker(s)...\n",
                                            task.i, task.algo1, task.algo2, task.algo3, tipHash.ToString(), (unsigned int)nWorkersPerTask);
                                    } else {
                                        LogPrintf("BitcoinMiner: Elected miner at index %d (algo %d) for tip %s. Solving puzzle across %u worker(s)...\n",
                                            task.i, task.algoIndex, tipHash.ToString(), (unsigned int)nWorkersPerTask);
                                    }
                                }

                                const uint32_t stride = static_cast<uint32_t>(nWorkersPerTask);
                                uint32_t nNonce = task.startNonce + static_cast<uint32_t>(w);
                                uint32_t iter = 0;
                                const int64_t nStartSolveMs = GetTimeMillis();
                                std::vector<unsigned char> vchSig;
                                bool solved = false;
                                unsigned char inputBuf[104];
                                const unsigned int keyLen = task.myMinerKey.size();
                                memcpy(inputBuf, adamSeed.begin(), 32);
                                inputBuf[32] = static_cast<unsigned char>(keyLen);
                                if (keyLen > 0) {
                                    memcpy(inputBuf + 33, task.myMinerKey.begin(), keyLen);
                                }
                                const size_t inputLen = 37 + keyLen;
                                const int i_factor = task.i + 1;
                                while (true) {
                                    if ((++iter & 0x3F) == 0) {
                                        boost::this_thread::interruption_point();
                                        if (pTaskSolved->load(std::memory_order_relaxed) ||
                                            ShutdownRequested() || !fGenerateBitcoins || GetChainTip() != pindexPrev) {
                                            break;
                                        }
                                        const int64_t nElapsedMs = GetTimeMillis() - nStartSolveMs;
                                        if (nElapsedMs >= 1500) {
                                            fHasUnsolvedLocalTasks.store(true);
                                            if (w == 0) {
                                                LOCK(cs_adam_solutions);
                                                mapResumeNonce[task.myMinerKey] = task.startNonce + iter * stride;
                                            }
                                            break;
                                        }
                                    }
                                    WriteLE32(inputBuf + 33 + keyLen, nNonce);

                                    uint256 puzzleHash;
                                    if (!task.fV12) {
                                        puzzleHash = CalculateAdamPuzzleHash(task.algoIndex, inputBuf, inputBuf + inputLen);
                                    } else {
                                        uint256 hash3 = CalculateAdamPuzzleHash(task.algo3, inputBuf, inputBuf + inputLen);
                                        arith_uint256 val1 = UintToArith256(hash3) * i_factor;
                                        uint256 multiplied1 = ArithToUint256(val1);

                                        uint256 hash2 = CalculateAdamPuzzleHash(task.algo2, multiplied1.begin(), multiplied1.begin() + 32);
                                        arith_uint256 val2 = UintToArith256(hash2) * i_factor;
                                        uint256 multiplied2 = ArithToUint256(val2);

                                        puzzleHash = CalculateAdamPuzzleHash(task.algo1, multiplied2.begin(), multiplied2.begin() + 32);
                                    }

                                    if (puzzleHash <= task.scaledTarget) {
                                        if (!pTaskSolved->exchange(true)) {
                                            if (task.blsKey.IsValid()) {
                                                if (SignBLSWithECDSAFallback(puzzleHash, task.privKey, task.blsKey, vchSig)) {
                                                    solved = true;
                                                }
                                            } else {
                                                LogPrintf("BitcoinMiner: Solved puzzle but cannot sign because no direct BLS key is associated with myMinerKey %s\n", task.myMinerKey.GetID().ToString());
                                            }
                                        }
                                        break;
                                    }
                                    nNonce += stride;
                                }

                                if (solved && GetChainTip() == pindexPrev) {
                                    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                                    ss << nNonce << vchSig;
                                    std::vector<unsigned char> vchSolution(ss.begin(), ss.end());

                                    bool shouldBroadcast = false;
                                    {
                                        LOCK(cs_adam_solutions);
                                        auto& solutionsForBlock = mapAdamSolutionsCache[tipHash];
                                        if (solutionsForBlock.find(task.myMinerKey) == solutionsForBlock.end()) {
                                            solutionsForBlock[task.myMinerKey] = vchSolution;
                                            shouldBroadcast = true;
                                        }
                                    }

                                    if (shouldBroadcast) {
                                        CAdamSolutionMsg solMsg;
                                        solMsg.hashPrevBlock = tipHash;
                                        solMsg.minerKey = task.myMinerKey;
                                        solMsg.vchSolution = vchSolution;

                                        if (g_connman) {
                                            g_connman->ForEachNode([&solMsg](CNode* pnode) {
                                                g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::ADAMSOL, solMsg));
                                            });
                                            LogPrintf("BitcoinMiner: Broadcasted adamsol for miner key %s and tip %s\n",
                                                task.myMinerKey.GetID().ToString(), tipHash.ToString());
                                        }
                                    }
                                }
                            });
                        }
                    }
                    try {
                        solverThreads.join_all();
                    } catch (const boost::thread_interrupted&) {
                        solverThreads.interrupt_all();
                        solverThreads.join_all();
                        throw;
                    }
                }
            }
        }
        if (!fProofOfStake && IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
            if (consensus.IsTimeProtocolV2(pindexPrev->nHeight + 1) &&
                (pindexPrev->MinPastBlockTime() >= GetCurrentTimeSlot() ||
                 (!Params().IsRegTestNet() && GetCurrentTimeSlot() < pindexPrev->GetBlockTime() + consensus.nTargetSpacing))) {
                const int64_t nSlotRem = consensus.nTimeSlotLength - (GetAdjustedTime() % consensus.nTimeSlotLength);
                MilliSleep((fHasUnsolvedLocalTasks.load() || nSlotRem <= 2) ? 50 : 250);
                continue;
            }
            if (!Params().IsRegTestNet() &&
                consensus.IsTimeProtocolV2(pindexPrev->nHeight + 1) &&
                consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POS)) {
                static std::mutex cs_slot_fairness;
                static int64_t s_lastPoWObservedSlot = 0;
                static int64_t s_slotOpenedSteadyMs = 0;
                const int64_t curSlot = GetCurrentTimeSlot();
                int64_t slotOpenedMs = 0;
                {
                    std::lock_guard<std::mutex> slotLock(cs_slot_fairness);
                    if (curSlot != s_lastPoWObservedSlot) {
                        s_lastPoWObservedSlot = curSlot;
                        s_slotOpenedSteadyMs = GetTimeMillis();
                    }
                    slotOpenedMs = s_slotOpenedSteadyMs;
                }
                // Give local ThreadStakeMinter and cooperative remote PoS stakers a 300ms priority window at each 15s slot opening
                if (GetTimeMillis() - slotOpenedMs < 300) {
                    MilliSleep(50);
                    continue;
                }
            }
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            std::vector<CPubKey> vStandbyMiners;
            CPubKey expectedCoordinator;
            bool isCoordinator = false;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, pindexPrev->nHeight + 1, nullptr, &vStandbyMiners)) {
                CKey coordKey;
                isCoordinator = HasCoordinatorKey(expectedCoordinator, pwallet, coordKey);
                // Fallback coordinator election over time using PoBLS Ticket XOR Distance ranking
                if (!isCoordinator && !vExpectedMiners.empty()) {
                    CBlockHeader dummyHeader;
                    int nNextHeight = pindexPrev->nHeight + 1;
                    dummyHeader.nVersion = consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL) ? 12 : 11;
                    UpdateTime(&dummyHeader, pindexPrev);
                    int64_t timeElapsed = dummyHeader.GetBlockTime() - pindexPrev->GetBlockTime();
                    if (timeElapsed >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                        std::map<CPubKey, std::vector<unsigned char>> currentSols;
                        {
                            LOCK(cs_adam_solutions);
                            auto itSol = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                            if (itSol != mapAdamSolutionsCache.end()) {
                                currentSols = itSol->second;
                            }
                        }
                        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
                        std::vector<CPubKey> vResolvedMiners;
                        std::vector<std::vector<unsigned char>> vResolvedSols;
                        int nAvailSols = 0;
                        ResolveAdamCommitteeForTime(
                            pindexPrev->GetBlockHash(), adamSeed, nNextHeight, timeElapsed,
                            nBits, dummyHeader.nVersion, vExpectedMiners, vStandbyMiners,
                            currentSols, vResolvedMiners, vResolvedSols, nAvailSols,
                            nullptr, nullptr, pcoinsTip);
                        std::map<CPubKey, std::vector<unsigned char>> verifiedSols;
                        for (size_t i = 0; i < vResolvedMiners.size() && i < vResolvedSols.size(); ++i) {
                            if (!vResolvedSols[i].empty()) {
                                verifiedSols[vResolvedMiners[i]] = vResolvedSols[i];
                            }
                        }
                        CPubKey fallbackCoordinator, prevFallback;
                        int rotationIndex = 0;
                        if (GetAdamFallbackCoordinatorForSlot(adamSeed, nNextHeight, timeElapsed, vResolvedMiners, verifiedSols, fallbackCoordinator, prevFallback, rotationIndex)) {
                            CKey fallbackKey;
                            if (HasCoordinatorKey(fallbackCoordinator, pwallet, fallbackKey)) {
                                isCoordinator = true;
                                expectedCoordinator = fallbackCoordinator;
                            }
                            if (isCoordinator) {
                                static uint256 lastLoggedPoWTip;
                                static int lastLoggedPoWRot = -1;
                                if (lastLoggedPoWTip != pindexPrev->GetBlockHash() || lastLoggedPoWRot != rotationIndex) {
                                    lastLoggedPoWTip = pindexPrev->GetBlockHash();
                                    lastLoggedPoWRot = rotationIndex;
                                    LogPrintf("BitcoinMiner: Elected coordinator is offline. Taking over as PoBLS XOR-ranked fallback coordinator (rotation index %d, address: %s).\n", 
                                        rotationIndex, expectedCoordinator.GetID().ToString());
                                }
                            }
                        }
                    }
                }
            }
            if (!isCoordinator || IsParentBlockProducedOrInFlight(pindexPrev->GetBlockHash(), expectedCoordinator)) {
                MilliSleep(fHasUnsolvedLocalTasks.load() ? 50 : 1000);
                continue;
            }
        }

        if (fProofOfStake) {
            if (!consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POS)) {
                // The last PoW block hasn't even been mined yet.
                MilliSleep(nSpacingMillis); // sleep a block
                continue;
            }

            // update fStakeableCoins (5 minute check time);
            CheckForCoins(pwallet, 5, &availableCoins);

            while ((g_connman && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && Params().MiningRequiresPeers()) || pwallet->IsLocked() || (fProofOfStake && !fStakeableCoins) || !fMasternodeSync) {
                if (ShutdownRequested()) return;
                MilliSleep(5000);
                // Do a separate 1 minute check here to ensure fStakeableCoins and fMasternodeSync is updated
                if ((fProofOfStake && !fStakeableCoins) || !fMasternodeSync) CheckForCoins(pwallet, 1, &availableCoins);
            }

            //search our map of hashed blocks, see if bestblock has been hashed yet
            if (pwallet->pStakerStatus &&
                pwallet->pStakerStatus->GetLastHash() == pindexPrev->GetBlockHash() &&
                pwallet->pStakerStatus->GetLastTime() >= GetCurrentTimeSlot()) {
                const int64_t nSlotRem = consensus.IsTimeProtocolV2(pindexPrev->nHeight + 1)
                    ? (consensus.nTimeSlotLength - (GetAdjustedTime() % consensus.nTimeSlotLength))
                    : 5;
                MilliSleep(nSlotRem <= 2 ? 50 : 250);
                continue;
            }

            if (IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
                if (consensus.IsTimeProtocolV2(pindexPrev->nHeight + 1) &&
                    (pindexPrev->MinPastBlockTime() >= GetCurrentTimeSlot() ||
                     (!Params().IsRegTestNet() && GetCurrentTimeSlot() < pindexPrev->GetBlockTime() + consensus.nTargetSpacing))) {
                    const int64_t nSlotRem = consensus.nTimeSlotLength - (GetAdjustedTime() % consensus.nTimeSlotLength);
                    MilliSleep(nSlotRem <= 2 ? 50 : 250);
                    continue;
                }
                uint256 adamSeed = GetAdamSeed(pindexPrev);
                std::vector<CPubKey> vExpectedMiners;
                std::vector<CPubKey> vStandbyMiners;
                CPubKey expectedCoordinator;
                int nHeight = pindexPrev->nHeight + 1;
                if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nHeight, nullptr, &vStandbyMiners)) {
                    MilliSleep(500);
                    continue;
                }
                CBlockHeader dummyHeader;
                dummyHeader.nVersion = consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL) ? 12 : 11;
                UpdateTime(&dummyHeader, pindexPrev);
                int64_t timeElapsed = std::max<int64_t>(0, dummyHeader.GetBlockTime() - pindexPrev->GetBlockTime());
                int nActiveMns = GetActiveMasternodePoolCount(nHeight - 1, pcoinsTip);
                int threshold = consensus.GetAdamEffectiveThreshold(nHeight, timeElapsed, nActiveMns);

                std::map<CPubKey, std::vector<unsigned char>> currentSols;
                {
                    LOCK(cs_adam_solutions);
                    auto itSol = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                    if (itSol != mapAdamSolutionsCache.end()) {
                        currentSols = itSol->second;
                    }
                }
                unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
                std::vector<CPubKey> vResolvedMiners;
                std::vector<std::vector<unsigned char>> vResolvedSols;
                int nAvailSols = 0;
                ResolveAdamCommitteeForTime(
                    pindexPrev->GetBlockHash(), adamSeed, nHeight, timeElapsed,
                    nBits, dummyHeader.nVersion, vExpectedMiners, vStandbyMiners,
                    currentSols, vResolvedMiners, vResolvedSols, nAvailSols,
                    nullptr, nullptr, pcoinsTip);

                if (!Params().IsRegTestNet() && nHeight > consensus.nAdamQuorumEnforcementHeight && nAvailSols < threshold) {
                    MilliSleep(250);
                    continue;
                }

                bool fFallbackMode = !consensus.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL) && IsAdamActive(nHeight, consensus);
                bool fAllowCoordinatorRotation = fFallbackMode || (timeElapsed >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS);
                CKey coordKey;
                bool isCoordinator = HasCoordinatorKey(expectedCoordinator, pwallet, coordKey);
                if (!isCoordinator && fAllowCoordinatorRotation && timeElapsed >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS && !vResolvedMiners.empty()) {
                    std::map<CPubKey, std::vector<unsigned char>> verifiedSols;
                    for (size_t i = 0; i < vResolvedMiners.size() && i < vResolvedSols.size(); ++i) {
                        if (!vResolvedSols[i].empty()) {
                            verifiedSols[vResolvedMiners[i]] = vResolvedSols[i];
                        }
                    }
                    CPubKey fallbackCoordinator, prevFallback;
                    int rotationIndex = 0;
                    if (GetAdamFallbackCoordinatorForSlot(adamSeed, nHeight, timeElapsed, vResolvedMiners, verifiedSols, fallbackCoordinator, prevFallback, rotationIndex)) {
                        expectedCoordinator = fallbackCoordinator;
                        CKey fallbackKey;
                        if (HasCoordinatorKey(fallbackCoordinator, pwallet, fallbackKey)) {
                            isCoordinator = true;
                            static uint256 lastLoggedPoSTip;
                            static int lastLoggedPoSRot = -1;
                            if (lastLoggedPoSTip != pindexPrev->GetBlockHash() || lastLoggedPoSRot != rotationIndex) {
                                lastLoggedPoSTip = pindexPrev->GetBlockHash();
                                lastLoggedPoSRot = rotationIndex;
                                LogPrintf("BitcoinMiner: Elected coordinator is offline. Taking over as PoBLS XOR-ranked fallback coordinator (rotation index %d, address: %s).\n", 
                                    rotationIndex, expectedCoordinator.GetID().ToString());
                            }
                        }
                    }
                }
                if (!expectedCoordinator.IsValid() || IsParentBlockProducedOrInFlight(pindexPrev->GetBlockHash(), expectedCoordinator)) {
                    MilliSleep(250);
                    continue;
                }
            }

        } else if (pindexPrev->nHeight > 6 &&
                   consensus.NetworkUpgradeActive(pindexPrev->nHeight - 6, Consensus::UPGRADE_POS) &&
                   !IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
            // Late PoW: run for a little while longer, just in case there is a rewind on the chain.
            LogPrintf("%s: Exiting PoW Mining Thread at height: %d\n", __func__, pindexPrev->nHeight);
            return;
        }

        //
        // Create new block
        //
        unsigned int nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();

        std::unique_ptr<CBlockTemplate> pblocktemplate((fProofOfStake ?
                                                            CreateNewBlock(CScript(), pwallet, true, &availableCoins) :
                                                            CreateNewBlockWithKey(*opReservekey, pwallet)));
        if (!pblocktemplate.get()) {
            MilliSleep(1000);
            continue;
        }
        CBlock* pblock = &pblocktemplate->block;

        // POS - block found: process it
        if (fProofOfStake) {
            LogPrintf("%s : proof-of-stake block was signed %s \n", __func__, pblock->GetHash().ToString().c_str());
            SetThreadPriority(THREAD_PRIORITY_NORMAL);
            if (!ProcessBlockFound(pblock, *pwallet, opReservekey)) {
                LogPrintf("%s: New block orphaned\n", __func__);
                continue;
            }
            SetThreadPriority(THREAD_PRIORITY_LOWEST);
            continue;
        }

        // POW - miner main
        if (pblock->nVersion < 11) {
            IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);
        }

        if (pblock->nVersion >= 11) {
            // CreateNewBlock already signed the block as coordinator (including VRF and LLMQ signatures)
            if (!pblock->vAdamCoordinatorSig.empty()) {
                LogPrintf("%s: Signed ADAM block found, submitting block %s\n", __func__, pblock->GetHash().ToString());
                SetThreadPriority(THREAD_PRIORITY_NORMAL);
                ProcessBlockFound(pblock, *pwallet, opReservekey);
                SetThreadPriority(THREAD_PRIORITY_LOWEST);

                if (Params().IsRegTestNet())
                    throw boost::thread_interrupted();
                continue;
            }
            MilliSleep(500);
            continue;
        }

        LogPrintf("Running Miner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
            ::GetSerializeSize(*pblock, SER_NETWORK, PROTOCOL_VERSION));

        //
        // Search
        //
        int64_t nStart = GetTime();
        uint256 hashTarget = uint256().SetCompact(pblock->nBits);
        while (true) {
            unsigned int nHashesDone = 0;

            uint256 hash;
            while (true) {
                hash = pblock->GetHash();
                if (hash <= hashTarget) {
                    // Found a solution
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);
                    LogPrintf("%s:\n", __func__);
                    LogPrintf("proof-of-work found  \n  hash: %s  \ntarget: %s\n", hash.GetHex(), hashTarget.GetHex());
                    ProcessBlockFound(pblock, *pwallet, opReservekey);
                    SetThreadPriority(THREAD_PRIORITY_LOWEST);

                    // In regression test mode, stop mining after a block is found. This
                    // allows developers to controllably generate a block on demand.
                    if (Params().IsRegTestNet())
                        throw boost::thread_interrupted();

                    break;
                }
                pblock->nNonce += 1;
                nHashesDone += 1;
                if ((pblock->nNonce & 0xFF) == 0)
                    break;
            }

            // Meter hashes/sec
            static int64_t nHashCounter;
            if (nHPSTimerStart == 0) {
                nHPSTimerStart = GetTimeMillis();
                nHashCounter = 0;
            } else
                nHashCounter += nHashesDone;
            if (GetTimeMillis() - nHPSTimerStart > 4000) {
                static RecursiveMutex cs;
                {
                    LOCK(cs);
                    if (GetTimeMillis() - nHPSTimerStart > 4000) {
                        dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
                        nHPSTimerStart = GetTimeMillis();
                        nHashCounter = 0;
                        static int64_t nLogTime;
                        if (GetTime() - nLogTime > 30 * 60) {
                            nLogTime = GetTime();
                            LogPrintf("hashmeter %6.0f khash/s\n", dHashesPerSec / 1000.0);
                        }
                    }
                }
            }

            // Check for stop or if block needs to be rebuilt
            boost::this_thread::interruption_point();
            if ((g_connman && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && Params().MiningRequiresPeers()) || // Regtest mode doesn't require peers
                (pblock->nNonce >= 0xffff0000) ||
                (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60) ||
                (pindexPrev != chainActive.Tip())) break;

            // Update nTime every few seconds
            UpdateTime(pblock, pindexPrev);
        }
    }
}

void static ThreadBitcoinMiner(void* parg)
{
    boost::this_thread::interruption_point();
    CWallet* pwallet = (CWallet*)parg;
    try {
        BitcoinMiner(pwallet, false);
        boost::this_thread::interruption_point();
    } catch (const boost::thread_interrupted&) {
        LogPrintf("ThreadBitcoinMiner interrupted\n");
    } catch (const std::exception& e) {
        LogPrintf("Miner exception: %s\n", e.what());
    } catch (...) {
        LogPrintf("Miner exception: unknown exception\n");
    }

    LogPrintf("Miner exiting\n");
}

static std::mutex cs_async_coord_thread;
static std::thread asyncCoordAssembleThread;
static std::atomic<bool> fAsyncCoordRunning{false};
static uint256 hashAsyncCoordTargetTip;

void TryAssembleCoordinatorBlockAsync(const uint256& tipHash)
{
    if (fGenerateBitcoins || ShutdownRequested() || !pwalletMain || tipHash.IsNull()) {
        return;
    }

    const Consensus::Params& consensus = Params().GetConsensus();
    CBlockIndex* pindexPrev = GetChainTip();
    if (!pindexPrev || pindexPrev->GetBlockHash() != tipHash) {
        return;
    }
    const int nNextHeight = pindexPrev->nHeight + 1;
    if (!IsAdamActive(nNextHeight, consensus)) {
        return;
    }
    if (!Params().IsRegTestNet() && (IsInitialBlockDownload() || (IsModelDActive(nNextHeight) && !masternodeSync.IsSynced()))) {
        return;
    }

    uint256 adamSeed = GetAdamSeed(pindexPrev);
    std::vector<CPubKey> vExpectedMiners;
    std::vector<CPubKey> vStandbyMiners;
    CPubKey expectedCoordinator;
    if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nNextHeight, nullptr, &vStandbyMiners)) {
        return;
    }

    CKey testKey;
    bool fOwnsAnyCoordinatorCandidate = HasCoordinatorKey(expectedCoordinator, pwalletMain, testKey);
    if (!fOwnsAnyCoordinatorCandidate) {
        for (const auto& k : vExpectedMiners) {
            if (HasCoordinatorKey(k, pwalletMain, testKey)) {
                fOwnsAnyCoordinatorCandidate = true;
                break;
            }
        }
    }
    if (!fOwnsAnyCoordinatorCandidate) {
        return;
    }

    std::lock_guard<std::mutex> lock(cs_async_coord_thread);
    if (fAsyncCoordRunning.load() && hashAsyncCoordTargetTip == tipHash) {
        return;
    }
    if (asyncCoordAssembleThread.joinable()) {
        if (fAsyncCoordRunning.load()) {
            return;
        }
        asyncCoordAssembleThread.join();
    }

    hashAsyncCoordTargetTip = tipHash;
    fAsyncCoordRunning.store(true);
    asyncCoordAssembleThread = std::thread([tipHash]() {
        struct AsyncGuard {
            ~AsyncGuard() { fAsyncCoordRunning.store(false); }
        } guard;
        util::ThreadRename("kristatech-coord-asm");
        SetThreadPriority(THREAD_PRIORITY_LOWEST);
        const Consensus::Params& consensus = Params().GetConsensus();

        while (!ShutdownRequested() && !fGenerateBitcoins && pwalletMain) {
            CBlockIndex* pindexPrev = GetChainTip();
            if (!pindexPrev || pindexPrev->GetBlockHash() != tipHash) {
                return;
            }
            const int nNextHeight = pindexPrev->nHeight + 1;
            if (consensus.IsTimeProtocolV2(nNextHeight) &&
                (pindexPrev->MinPastBlockTime() >= GetCurrentTimeSlot() ||
                 (!Params().IsRegTestNet() && GetCurrentTimeSlot() < pindexPrev->GetBlockTime() + consensus.nTargetSpacing))) {
                MilliSleep(100);
                continue;
            }
            if (!Params().IsRegTestNet() && consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POS)) {
                MilliSleep(300);
                if (ShutdownRequested() || fGenerateBitcoins || !pwalletMain || GetChainTip() != pindexPrev) {
                    return;
                }
            }

            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            std::vector<CPubKey> vStandbyMiners;
            CPubKey expectedCoordinator;
            if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator, nNextHeight, nullptr, &vStandbyMiners)) {
                return;
            }

            CBlockHeader dummyHeader;
            dummyHeader.nVersion = consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_POMBL) ? 12 : 11;
            UpdateTime(&dummyHeader, pindexPrev);
            int64_t timeElapsed = std::max<int64_t>(0, dummyHeader.GetBlockTime() - pindexPrev->GetBlockTime());
            int nActiveMns = GetActiveMasternodePoolCount(nNextHeight - 1, pcoinsTip);
            int threshold = consensus.GetAdamEffectiveThreshold(nNextHeight, timeElapsed, nActiveMns);

            std::map<CPubKey, std::vector<unsigned char>> currentSols;
            {
                LOCK(cs_adam_solutions);
                auto itSol = mapAdamSolutionsCache.find(tipHash);
                if (itSol != mapAdamSolutionsCache.end()) {
                    currentSols = itSol->second;
                }
            }
            unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
            std::vector<CPubKey> vResolvedMiners;
            std::vector<std::vector<unsigned char>> vResolvedSols;
            int nAvailSols = 0;
            ResolveAdamCommitteeForTime(
                tipHash, adamSeed, nNextHeight, timeElapsed,
                nBits, dummyHeader.nVersion, vExpectedMiners, vStandbyMiners,
                currentSols, vResolvedMiners, vResolvedSols, nAvailSols,
                nullptr, nullptr, pcoinsTip);

            if (!Params().IsRegTestNet() && nNextHeight > consensus.nAdamQuorumEnforcementHeight && nAvailSols < threshold) {
                return;
            }

            CKey coordKey;
            bool isCoordinator = HasCoordinatorKey(expectedCoordinator, pwalletMain, coordKey);
            if (!isCoordinator && timeElapsed >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS && !vResolvedMiners.empty()) {
                std::map<CPubKey, std::vector<unsigned char>> verifiedSols;
                for (size_t i = 0; i < vResolvedMiners.size() && i < vResolvedSols.size(); ++i) {
                    if (!vResolvedSols[i].empty()) {
                        verifiedSols[vResolvedMiners[i]] = vResolvedSols[i];
                    }
                }
                CPubKey fallbackCoordinator, prevFallback;
                int rotationIndex = 0;
                if (GetAdamFallbackCoordinatorForSlot(adamSeed, nNextHeight, timeElapsed, vResolvedMiners, verifiedSols, fallbackCoordinator, prevFallback, rotationIndex)) {
                    CKey fallbackKey;
                    if (HasCoordinatorKey(fallbackCoordinator, pwalletMain, fallbackKey)) {
                        isCoordinator = true;
                        expectedCoordinator = fallbackCoordinator;
                    }
                }
            }

            if (!isCoordinator) {
                if (timeElapsed < ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                    MilliSleep(500);
                    continue;
                }
                return;
            }
            if (IsParentBlockProducedOrInFlight(tipHash, expectedCoordinator)) {
                return;
            }

            Optional<CReserveKey> opReservekey{CReserveKey(pwalletMain)};
            std::unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(*opReservekey, pwalletMain));
            if (pblocktemplate && !pblocktemplate->block.vAdamCoordinatorSig.empty()) {
                LogPrintf("TryAssembleCoordinatorBlockAsync: Assembled coordinator block %s in -gen=0 mode\n",
                    pblocktemplate->block.GetHash().ToString());
                ProcessBlockFound(&pblocktemplate->block, *pwalletMain, opReservekey);
            }
            return;
        }
    });
}

void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = NULL;
    fGenerateBitcoins = fGenerate;
    g_nConfiguredMinerThreads.store((!fGenerate || nThreads == 0) ? 0 : (nThreads < 0 ? 1 : nThreads));

    if (minerThreads != NULL) {
        minerThreads->interrupt_all();
        minerThreads->join_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate) {
        {
            std::lock_guard<std::mutex> lock(cs_auto_register_thread);
            if (autoRegisterThread.joinable()) {
                autoRegisterThread.join();
            }
        }
        if (ShutdownRequested()) {
            std::lock_guard<std::mutex> asyncLock(cs_async_coord_thread);
            if (asyncCoordAssembleThread.joinable()) {
                asyncCoordAssembleThread.join();
            }
        }
        return;
    }
    if (nThreads < 0)
        nThreads = 1;

    minerThreads = new boost::thread_group();
    for (int i = 0; i < nThreads; i++)
        minerThreads->create_thread(boost::bind(&ThreadBitcoinMiner, pwallet));
}

// ppcoin: stake minter thread
void ThreadStakeMinter()
{
    boost::this_thread::interruption_point();
    LogPrintf("ThreadStakeMinter started\n");
    CWallet* pwallet = pwalletMain;
    try {
        BitcoinMiner(pwallet, true);
        boost::this_thread::interruption_point();
    } catch (const boost::thread_interrupted&) {
        LogPrintf("ThreadStakeMinter interrupted\n");
    } catch (const std::exception& e) {
        LogPrintf("ThreadStakeMinter() exception \n");
    } catch (...) {
        LogPrintf("ThreadStakeMinter() error \n");
    }
    LogPrintf("ThreadStakeMinter exiting,\n");
}

#else
bool fGenerateBitcoins = false;
int GetConfiguredMinerThreads() { return 0; }
void TryAssembleCoordinatorBlockAsync(const uint256&) {}
#endif // ENABLE_WALLET
