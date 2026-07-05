// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"
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

bool CreateCoinbaseTx(CBlock* pblock, const CScript& scriptPubKeyIn, CBlockIndex* pindexPrev)
{
    // Create coinbase tx
    CMutableTransaction txNew;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);
    txNew.vout[0].scriptPubKey = scriptPubKeyIn;

    //Masternode and general budget payments
    FillBlockPayee(txNew, pindexPrev, false);

    txNew.vin[0].scriptSig = CScript() << pindexPrev->nHeight + 1 << OP_0;
    // If no payee was detected, then the whole block value goes to the first output.
    if (txNew.vout.size() == 1) {
        txNew.vout[0].nValue = CMasternode::GetBlockValue(pindexPrev->nHeight + 1);
    }

    pblock->vtx.emplace_back(txNew);
    return true;
}

bool SolveProofOfStake(CBlock* pblock, CBlockIndex* pindexPrev, CWallet* pwallet, std::vector<COutput>* availableCoins)
{
    boost::this_thread::interruption_point();
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
    CMutableTransaction txCoinStake;
    int64_t nTxNewTime = 0;
    if (!pwallet->CreateCoinStake(*pwallet, pindexPrev, pblock->nBits, txCoinStake, nTxNewTime, availableCoins)) {
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

    // Depending on the tip height, try to find a coinstake who solves the block or create a coinbase tx.
    if (!(fProofOfStake ? SolveProofOfStake(pblock, pindexPrev, pwallet, availableCoins)
                        : CreateCoinbaseTx(pblock, scriptPubKeyIn, pindexPrev))) {
        return nullptr;
    }

    // Solve partial puzzles if ADAM is active
    if (pblock->nVersion >= 11) {
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
        uint256 adamSeed = GetAdamSeed(pindexPrev);
        std::vector<CPubKey> vExpectedMiners;
        CPubKey expectedCoordinator;
        if (!SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
            static int64_t nLastSelectFailedTime = 0;
            int64_t nNow = GetTime();
            if (nNow - nLastSelectFailedTime > 60) {
                LogPrintf("CreateNewBlock: SelectAdamNodes failed. Miner pool too small or not synchronized. (this message is throttled to 1/min)\n");
                nLastSelectFailedTime = nNow;
            }
            return nullptr;
        }

        LogPrintf("CreateNewBlock: SelectAdamNodes succeeded. elected %d miners. seed:%s\n", vExpectedMiners.size(), adamSeed.ToString());
        pblock->vAdamMiners = vExpectedMiners;
        if (pblock->nVersion == 11) {
            pblock->vAdamMiners.push_back(expectedCoordinator);
        }
        
        pblock->vAdamSolutions.clear();
        int availableSolutions = 0;
        int threshold = consensus.nAdamThreshold;
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
        if (hasSolutions) {
            for (const auto& minerKey : vExpectedMiners) {
                auto solIt = solutionsForBlock.find(minerKey);
                if (solIt != solutionsForBlock.end()) {
                    pblock->vAdamSolutions.push_back(solIt->second);
                    if (VerifyAdamSolution(pblock->hashPrevBlock, adamSeed, minerKey, solIt->second, pblock->nBits, pblock->nVersion, nHeight)) {
                        availableSolutions++;
                    }
                } else {
                    pblock->vAdamSolutions.push_back(std::vector<unsigned char>()); // Empty solution placeholder
                }
            }
        } else {
            for (size_t k = 0; k < vExpectedMiners.size(); ++k) {
                pblock->vAdamSolutions.push_back(std::vector<unsigned char>());
            }
        }

            if ((Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET) && availableSolutions < threshold) {
                LogPrintf("CreateNewBlock: Regtest/Testnet mode. Generating deterministic solutions on the fly to meet quorum.\n");
                for (size_t minerIndex = 0; minerIndex < vExpectedMiners.size(); ++minerIndex) {
                    const auto& minerKey = vExpectedMiners[minerIndex];
                    // Check if we already have a valid solution for this miner
                    bool hasValidSol = false;
                    if (minerIndex < pblock->vAdamSolutions.size() && !pblock->vAdamSolutions[minerIndex].empty()) {
                        if (VerifyAdamSolution(pblock->hashPrevBlock, adamSeed, minerKey, pblock->vAdamSolutions[minerIndex], pblock->nBits, pblock->nVersion, nHeight)) {
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
                            
                            while (true) {
                                CDataStream ssInput(SER_GETHASH, 0);
                                ssInput << adamSeed;
                                ssInput << minerKey;
                                ssInput << nNonce;
                                
                                uint256 puzzleHash;
                                if (pblock->nVersion == 11) {
                                    int algoIndex = GetAdamPuzzleAlgo(adamSeed, minerKey, true);
                                    puzzleHash = CalculateAdamPuzzleHash(algoIndex, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
                                } else { // nVersion >= 12
                                    int algo1 = -1, algo2 = -1, algo3 = -1;
                                    GetAdam3PermutationAlgos(pblock->hashPrevBlock, minerKey, algo1, algo2, algo3);
                                    uint256 hash3 = CalculateAdamPuzzleHash(algo3, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
                                    int i_factor = minerIndex + 1;
                                    arith_uint256 val1 = UintToArith256(hash3) * i_factor;
                                    uint256 multiplied1 = ArithToUint256(val1);
                                    
                                    uint256 hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
                                    arith_uint256 val2 = UintToArith256(hash2) * i_factor;
                                    uint256 multiplied2 = ArithToUint256(val2);
                                    
                                    puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
                                }
                                
                                 if (puzzleHash <= scaledTarget) {
                                      CBLSSecretKey blsKey;
                                      if (pwalletMain) {
                                          LOCK(pwalletMain->cs_wallet);
                                          pwalletMain->GetBLSKey(minerKey.GetID(), blsKey);
                                      }
                                      if (!blsKey.IsValid()) {
                                          for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                                              if (activeMasternode.pubKeyMasternode == minerKey && activeMasternode.blsKeyMasternode.IsValid()) {
                                                  blsKey = activeMasternode.blsKeyMasternode;
                                                  break;
                                              }
                                          }
                                      }
                                      if (!blsKey.IsValid() && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
                                          blsKey = DeriveBLSFromCKey(privKey);
                                      }
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
                            availableSolutions++;
                        }
                    }
                }
            }

            if (availableSolutions < threshold) {
                LogPrintf("CreateNewBlock: Quorum threshold not met (available=%d vs threshold=%d). Block template deferred.\n",
                    availableSolutions, threshold);
                return nullptr;
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

            for (const CTxIn& txin : tx.vin) {
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

            // Skip free transactions if we're past the minimum block size:
            const uint256& hash = tx.GetHash();
            double dPriorityDelta = 0;
            CAmount nFeeDelta = 0;
            mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
            if (fSortedByFee && (dPriorityDelta <= 0) && (nFeeDelta <= 0) && (feeRate < ::minRelayTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
                continue;

            // Prioritise by fee once past the priority size or we run out of high-priority
            // transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            if (!view.HaveInputs(tx))
                continue;

            CAmount nTxFees = view.GetValueIn(tx) - tx.GetValueOut();

            nTxSigOps += GetP2SHSigOpCount(tx, view);
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
        if (!fProofOfStake)
            UpdateTime(pblock, pindexPrev);
        pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
        pblock->nNonce = 0;

        pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

        if (fProofOfStake || pblock->nVersion >= 11) {
            pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
        }

        if (pblock->nVersion >= 11) {
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                CKey coordKey;
                bool gotKey = false;
                // 1. Try pwallet (PoS staking wallet)
                if (pwallet && (pwallet->GetKey(expectedCoordinator.GetID(), coordKey) ||
                                pwallet->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey))) {
                    gotKey = true;
                }
#ifdef ENABLE_WALLET
                // 2. Try pwalletMain (global wallet, used in PoW/gen=1 mode)
                if (!gotKey && pwalletMain) {
                    LOCK(pwalletMain->cs_wallet);
                    if (pwalletMain->GetKey(expectedCoordinator.GetID(), coordKey) ||
                        pwalletMain->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey)) {
                        gotKey = true;
                    }
                }
#endif
                // 3. Try active masternodes (masternodeprivkey config)
                if (!gotKey) {
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
                // 4. Try masternodeprivkey config directly
                if (!gotKey) {
                    std::string strMnPrivKey = GetArg("-masternodeprivkey", "");
                    if (!strMnPrivKey.empty()) {
                        CKey key;
                        CPubKey pubkey;
                        if (CMessageSigner::GetKeysFromSecret(strMnPrivKey, key, pubkey)) {
                            if (ComparePubKeys(pubkey, expectedCoordinator)) {
                                coordKey = key;
                                gotKey = true;
                            }
                        }
                    }
                }
                // 5. Try deterministic keys (regtest/fallback)
                if (!gotKey) {
                    for (int i = 0; i < 15; ++i) {
                        if (ComparePubKeys(GetAdamDeterministicPubKey(i), expectedCoordinator)) {
                            coordKey = GetAdamDeterministicKey(i);
                            gotKey = true;
                            break;
                        }
                    }
                }
                LogPrintf("CreateNewBlock DIAGNOSTIC: expectedCoordinator=%s, KeyID=%s, gotKey=%d, coordKeyValid=%d\n",
                    HexStr(expectedCoordinator.begin(), expectedCoordinator.end()),
                    expectedCoordinator.GetID().ToString(),
                    gotKey, gotKey ? coordKey.IsValid() : 0);
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
                    if (!blsKey.IsValid() && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
                        blsKey = DeriveBLSFromCKey(coordKey);
                    }
                    if (blsKey.IsValid()) {
                        LogPrintf("CreateNewBlock DIAGNOSTIC: blsKeyValid=%d\n", blsKey.IsValid());
                        if (SignBLSWithECDSAFallback(adamSeed, coordKey, blsKey, pblock->vAdamVRFProof)) {
                            LogPrintf("CreateNewBlock: Signed block VRF proof for TestBlockValidity, seed: %s, size=%d\n", adamSeed.ToString(), pblock->vAdamVRFProof.size());
                        } else {
                            LogPrintf("CreateNewBlock ERROR: Failed to sign block VRF proof!\n");
                        }
                        if (SignBLSWithECDSAFallback(pblock->GetHash(), coordKey, blsKey, pblock->vAdamCoordinatorSig)) {
                            LogPrintf("CreateNewBlock: Signed block header for TestBlockValidity, hash: %s, size=%d\n", pblock->GetHash().ToString(), pblock->vAdamCoordinatorSig.size());
                        } else {
                            LogPrintf("CreateNewBlock ERROR: Failed to sign block header!\n");
                        }
                    } else {
                        LogPrintf("CreateNewBlock ERROR: Cannot sign block VRF proof or header because no direct BLS key is associated with coordinator key %s\n", expectedCoordinator.GetID().ToString());
                    }
                }
            }
        }

        if (fProofOfStake) {
            LogPrintf("CPUMiner : proof-of-stake block found %s \n", pblock->GetHash().GetHex());
            if (!SignBlock(*pblock, *pwallet)) {
                LogPrintf("%s: Signing new block with UTXO key failed \n", __func__);
                return nullptr;
            }
        }
    }

    if (pblock->nVersion >= 12) {
        llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight);
        if (!quorum.members.empty()) {
            llmq::CQuorumSignature qsig;
            qsig.blockHash = pblock->GetHash();

            // 1. Try to sign locally first if we hold any member private keys
            for (const auto& member : quorum.members) {
                CKey key;
                if (llmq::GetMasternodePrivKey(member.pubKeyMasternode, key)) {
                    std::vector<unsigned char> sig;
                    if (key.Sign(qsig.blockHash, sig)) {
                        qsig.signatures.push_back({member.collateralOutpoint, sig});
                    }
                }
            }

            size_t nThreshold = 2;
            if (Params().NetworkID() == CBaseChainParams::TESTNET || Params().NetworkID() == CBaseChainParams::REGTEST) {
                if (nHeight < Params().GetConsensus().vUpgrades[Consensus::UPGRADE_MODELD].nActivationHeight) {
                    nThreshold = 0;
                }
            } else {
                nThreshold = quorum.members.size() / 2 + 1;
            }

            // 2. If local signatures are insufficient, request via P2P
            if (qsig.signatures.size() < nThreshold) {
                LogPrintf("CreateNewBlock: Local signatures (%u/%u) not enough. Broadcasting proposed block template to peers...\n",
                           qsig.signatures.size(), nThreshold);

                CBlockProposeMsg propMsg;
                propMsg.block = *pblock;

                if (g_connman) {
                    g_connman->ForEachNode([&propMsg](CNode* pnode) {
                        g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::PROPOSEBLOCK, propMsg));
                    });
                }

                // Poll mapQuorumBlockSigs for incoming signatures (wait up to 15 seconds)
                int nWaitCount = 0;
                while (nWaitCount < 150) {
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
                                    qsig.signatures.push_back({pair.first, pair.second});
                                }
                            }
                        }
                    }

                    if (qsig.signatures.size() >= nThreshold) {
                        LogPrintf("CreateNewBlock: Quorum threshold met! Collected %u/%u signatures after waiting %d ms\n",
                                  qsig.signatures.size(), nThreshold, nWaitCount * 100);
                        break;
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
        CValidationState state;
        if (!TestBlockValidity(state, *pblock, pindexPrev, false, false)) {
            if (state.GetRejectReason() == "time-too-new") {
                MilliSleep(1000);
            } else {
                LogPrintf("CreateNewBlock() : TestBlockValidity failed (%s)\n", state.GetRejectReason());
            }
            extern int nMintableLastCheck;
            nMintableLastCheck = 0;
            return nullptr;
        }
    }

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
        return error("Miner : ProcessNewBlock, block not accepted");
    }

    g_connman->ForEachNode([&pblock](CNode* node)
    {
        node->PushInventory(CInv(MSG_BLOCK, pblock->GetHash()));
    });

    return true;
}

bool fGenerateBitcoins = false;
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
        fStakeableCoins = pwallet->StakeableCoins(availableCoins);
        fMasternodeSync = sporkManager.IsSporkActive(SPORK_106_STAKING_SKIP_MN_SYNC) || !masternodeSync.NotCompleted();
        if (chainActive.Height() < 1200 || mnodeman.CountEnabled() == 0) {
            fMasternodeSync = true;
        }
    }
}
#ifdef ENABLE_WALLET
void AutoRegisterMiner(CWallet* pwallet, const CPubKey& pubkey)
{
    if (!pwallet) return;

    // Check if the wallet has already registered this miner key in the active pool
    {
        LOCK(cs_main);
        std::vector<CPubKey> pool = GetAdamMinerPool(chainActive.Height());
        for (const auto& key : pool) {
            if (key == pubkey) {
                // Already registered!
                return;
            }
        }
    }

    static int nLastRegSendHeight = 0;
    if (nLastRegSendHeight > 0 && chainActive.Height() - nLastRegSendHeight < 50) {
        // We already sent a registration transaction recently (less than 50 blocks ago)
        return;
    }

    LogPrintf("AutoRegisterMiner: Miner key %s is not registered in the ADAM pool. Attempting auto-registration...\n", pubkey.GetID().ToString());

    // Generate/Get BLS key for this miner key
    CBLSSecretKey blsKey;
    {
        LOCK(pwallet->cs_wallet);
        pwallet->GetBLSKey(pubkey.GetID(), blsKey);
    }

    if (pwallet->IsLocked()) {
        LogPrintf("AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.\n");
        return;
    }

    CScript scriptPubKey;
    CAmount nAmount = 0;

    // Decide whether to do PoL (lock) or PoW (pow) based on balance
    CAmount balance = pwallet->GetAvailableBalance();
    if (balance >= MINER_REGISTRATION_LOCK_AMOUNT + 1 * CENT) {
        // We have enough balance to do a Coin-Lock (PoL) registration!
        int64_t locktime = 0;
        {
            LOCK(cs_main);
            locktime = chainActive.Height() + 2900;
        }
        scriptPubKey = CScript() << std::vector<unsigned char>(pubkey.begin(), pubkey.end()) << OP_DROP
                                 << CScriptNum(locktime) << OP_CHECKLOCKTIMEVERIFY << OP_DROP
                                 << OP_DUP << OP_HASH160 << ToByteVector(pubkey.GetID()) << OP_EQUALVERIFY << OP_CHECKSIG;
        nAmount = MINER_REGISTRATION_LOCK_AMOUNT;
        LogPrintf("AutoRegisterMiner: Selecting Coin-Lock (PoL) registration (lock amount: %d KRISTA, locktime: %d blocks)\n", MINER_REGISTRATION_LOCK_AMOUNT / COIN, locktime);
    } else {
        // Fallback to PoW registration
        uint256 challengeHash;
        uint256 target;
        int64_t currentHeight = 0;
        {
            LOCK(cs_main);
            if (chainActive.Tip()) {
                challengeHash = chainActive.Tip()->GetBlockHash();
            } else {
                LogPrintf("AutoRegisterMiner: Chain tip is null. Cannot perform PoW registration.\n");
                return;
            }
            target = GetMinerPoWLimit(Params().NetworkIDString());
            currentHeight = chainActive.Height();
        }

        uint32_t nonce = 0;
        uint256 puzzleHash;
        LogPrintf("AutoRegisterMiner: Starting PoW search for challenge %s...\n", challengeHash.ToString());
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
                LogPrintf("AutoRegisterMiner: PoW search cancelled due to shutdown\n");
                return;
            }
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
        nAmount = 10000; // 0.0001 COIN
        LogPrintf("AutoRegisterMiner: Selecting PoW-Lock registration (amount: 0.0001 KRISTA, locktime: %d blocks)\n", locktime);
    }

    CReserveKey reservekey(pwallet);
    CAmount nFeeRequired;
    std::string strError;
    CWalletTx wtx;
    
    // Create and commit the transaction
    bool created = false;
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        created = pwallet->CreateTransaction(scriptPubKey, nAmount, wtx, reservekey, nFeeRequired, strError, nullptr, ALL_COINS, (CAmount)0);
    }

    if (!created) {
        LogPrintf("AutoRegisterMiner ERROR: CreateTransaction failed: %s\n", strError);
        return;
    }

    CWallet::CommitResult res;
    {
        LOCK2(cs_main, pwallet->cs_wallet);
        res = pwallet->CommitTransaction(wtx, reservekey, g_connman.get());
    }

    if (res.status != CWallet::CommitStatus::OK) {
        LogPrintf("AutoRegisterMiner ERROR: CommitTransaction failed!\n");
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

    while (fGenerateBitcoins || fProofOfStake) {
        CBlockIndex* pindexPrev = GetChainTip();
        if (!pindexPrev) {
            MilliSleep(nSpacingMillis); // sleep a block
            continue;
        }

        if (Params().MiningRequiresPeers() && g_connman && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0) {
            MilliSleep(1000);
            continue;
        }

        // POW - Elected Miner Background Solving Loop
        if (!fProofOfStake && IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
#ifdef ENABLE_WALLET
            static uint256 hashLastRegCheck;
            if (pwallet && pindexPrev->GetBlockHash() != hashLastRegCheck) {
                hashLastRegCheck = pindexPrev->GetBlockHash();
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
                if (!gotKey && (fMasterNode || !amnodeman.GetActiveMasternodes().empty())) {
                    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                        if (activeMasternode.pubKeyMasternode.IsValid()) {
                            CMasternode* pmn = mnodeman.Find(activeMasternode.pubKeyMasternode);
                            if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                                minerPubKey = pmn->pubKeyCollateralAddress;
                            } else {
                                minerPubKey = activeMasternode.pubKeyMasternode;
                            }
                            gotKey = true;
                            break;
                        }
                    }
                }
                if (!gotKey && opReservekey) {
                    opReservekey->GetReservedKey(minerPubKey);
                    gotKey = minerPubKey.IsValid();
                }

                if (gotKey && minerPubKey.IsValid()) {
                    AutoRegisterMiner(pwallet, minerPubKey);
                }
            }
#endif
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                int minerIdx = -1;
                CPubKey myMinerKey;
                
                // 1. Check if we have the private key for any of the elected miners in our wallet or deterministic keys
                for (size_t i = 0; i < vExpectedMiners.size(); ++i) {
                    bool alreadySolved = false;
                    bool hasSol = false;
                    std::vector<unsigned char> vchSol;
                    {
                        LOCK(cs_adam_solutions);
                        auto it = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                        if (it != mapAdamSolutionsCache.end() && it->second.count(vExpectedMiners[i])) {
                            vchSol = it->second.at(vExpectedMiners[i]);
                            hasSol = true;
                        }
                    }
                    if (hasSol) {
                        bool fV12 = consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POMBL);
                        int nVersion = fV12 ? 12 : 11;
                        CBlockHeader dummyHeader;
                        dummyHeader.nVersion = nVersion;
                        unsigned int nBits = GetNextWorkRequired(pindexPrev, &dummyHeader);
                        if (VerifyAdamSolution(pindexPrev->GetBlockHash(), adamSeed, vExpectedMiners[i], vchSol, nBits, nVersion, pindexPrev->nHeight + 1)) {
                            alreadySolved = true;
                        } else {
                            LogPrintf("BitcoinMiner: Cached solution for %s is invalid for version %d, erasing from cache to re-solve\n",
                                vExpectedMiners[i].GetID().ToString(), nVersion);
                            LOCK(cs_adam_solutions);
                            auto it = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                            if (it != mapAdamSolutionsCache.end()) {
                                it->second.erase(vExpectedMiners[i]);
                            }
                        }
                    }
                    if (alreadySolved) continue;

                    // Check wallet
                    if (pwallet && pwallet->HaveKey(vExpectedMiners[i].GetID())) {
                        minerIdx = i;
                        myMinerKey = vExpectedMiners[i];
                        break;
                    }
                    // Check active masternodes config (hot wallet mode)
                    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                        if (activeMasternode.pubKeyMasternode == vExpectedMiners[i]) {
                            minerIdx = i;
                            myMinerKey = vExpectedMiners[i];
                            break;
                        }
                    }
                    if (minerIdx >= 0) break;
                    // Check deterministic keys
                    for (int k = 0; k < 15; ++k) {
                        if (GetAdamDeterministicPubKey(k) == vExpectedMiners[i]) {
                            minerIdx = i;
                            myMinerKey = vExpectedMiners[i];
                            break;
                        }
                    }
                    if (minerIdx >= 0) break;
                }



                if (minerIdx >= 0) {
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
                        if (VerifyAdamSolution(pindexPrev->GetBlockHash(), adamSeed, myMinerKey, vchSol, nBits, nVersion, pindexPrev->nHeight + 1)) {
                            alreadySolved = true;
                        } else {
                            LogPrintf("BitcoinMiner: Cached solution for myMinerKey %s is invalid for version %d, erasing from cache to re-solve\n",
                                myMinerKey.GetID().ToString(), nVersion);
                            LOCK(cs_adam_solutions);
                            auto it = mapAdamSolutionsCache.find(pindexPrev->GetBlockHash());
                            if (it != mapAdamSolutionsCache.end()) {
                                it->second.erase(myMinerKey);
                            }
                        }
                    }

                    if (!alreadySolved) {
                        bool fV12 = consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POMBL);
                        int algoIndex = 12;
                        int algo1 = -1, algo2 = -1, algo3 = -1;
                        if (!fV12) {
                            algoIndex = GetAdamPuzzleAlgo(adamSeed, myMinerKey, true);
                            LogPrintf("BitcoinMiner: Elected miner at index %d (algo %d) for tip %s. Solving puzzle...\n",
                                minerIdx, algoIndex, pindexPrev->GetBlockHash().ToString());
                        } else {
                            GetAdam3PermutationAlgos(pindexPrev->GetBlockHash(), myMinerKey, algo1, algo2, algo3);
                            LogPrintf("BitcoinMiner: Elected miner at index %d (algos %d, %d and %d) for tip %s. Solving puzzle...\n",
                                minerIdx, algo1, algo2, algo3, pindexPrev->GetBlockHash().ToString());
                        }

                        CKey privKey;
                        if (pwallet && pwallet->GetKey(myMinerKey.GetID(), privKey)) {
                            // Key found in wallet
                        } else {
                            for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                                if (activeMasternode.pubKeyMasternode == myMinerKey) {
                                    CKey key;
                                    CPubKey pubkey;
                                    if (CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, key, pubkey)) {
                                        privKey = key;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!privKey.IsValid()) {
                            // Try deterministic key lookup
                            for (int k = 0; k < 15; ++k) {
                                if (GetAdamDeterministicPubKey(k) == myMinerKey) {
                                    privKey = GetAdamDeterministicKey(k);
                                    break;
                                }
                            }
                        }

                        if (privKey.IsValid()) {
                            uint32_t nNonce = 0;
                            std::vector<unsigned char> vchSig;
                            
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
                                int shift = (consensus.NetworkUpgradeActive(nNextHeight, Consensus::UPGRADE_ADAM_V2)) ? 
                                            consensus.nAdamDifficultyShiftV2 : consensus.nAdamDifficultyShiftV1;
                                scaledTarget = bnTarget << shift;
                                uint256 powLimit = consensus.powLimit;
                                if (scaledTarget > powLimit || scaledTarget < bnTarget) {
                                    scaledTarget = powLimit;
                                }
                            } else {
                                scaledTarget = ~UINT256_ZERO;
                            }

                            std::string algoName = !fV12 ? GetAdamPuzzleAlgoName(algoIndex) : (GetAdamPuzzleAlgoName(algo1) + "+" + GetAdamPuzzleAlgoName(algo2) + "+" + GetAdamPuzzleAlgoName(algo3));

                            bool solved = false;
                            while (fGenerateBitcoins && !boost::this_thread::interruption_requested()) {
                                if (GetChainTip() != pindexPrev) {
                                    break;
                                }

                                CDataStream ssInput(SER_GETHASH, 0);
                                ssInput << adamSeed;
                                ssInput << myMinerKey;
                                ssInput << nNonce;
                                
                                uint256 puzzleHash;
                                uint256 hash3;
                                uint256 multiplied1;
                                uint256 hash2;
                                uint256 multiplied2;
                                if (!fV12) {
                                    puzzleHash = CalculateAdamPuzzleHash(algoIndex, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
                                } else {
                                    hash3 = CalculateAdamPuzzleHash(algo3, (const unsigned char*)&ssInput[0], (const unsigned char*)&ssInput[0] + ssInput.size());
                                    int i_factor = minerIdx + 1;
                                    arith_uint256 val1 = UintToArith256(hash3) * i_factor;
                                    multiplied1 = ArithToUint256(val1);
                                    
                                    hash2 = CalculateAdamPuzzleHash(algo2, multiplied1.begin(), multiplied1.begin() + 32);
                                    arith_uint256 val2 = UintToArith256(hash2) * i_factor;
                                    multiplied2 = ArithToUint256(val2);
                                    
                                    puzzleHash = CalculateAdamPuzzleHash(algo1, multiplied2.begin(), multiplied2.begin() + 32);
                                }
                                
                                if (puzzleHash <= scaledTarget) {
                                    if (fV12) {
                                        LogPrintf("BitcoinMiner DEBUG SOLVED: height=%d, minerIdx=%d, algos=%d,%d,%d, factor=%d, seed=%s, input_hash=%s, hash3=%s, mult1=%s, hash2=%s, mult2=%s, puzzleHash=%s, nonce=%u\n",
                                            nNextHeight, minerIdx, algo1, algo2, algo3, minerIdx + 1, adamSeed.ToString(), Hash(ssInput.begin(), ssInput.end()).ToString(), hash3.ToString(), multiplied1.ToString(), hash2.ToString(), multiplied2.ToString(), puzzleHash.ToString(), nNonce);
                                    }
                                    CBLSSecretKey blsKey;
                                    if (pwallet) {
                                        LOCK(pwallet->cs_wallet);
                                        pwallet->GetBLSKey(myMinerKey.GetID(), blsKey);
                                    }
                                    if (!blsKey.IsValid()) {
                                        for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                                            if (activeMasternode.pubKeyMasternode == myMinerKey && activeMasternode.blsKeyMasternode.IsValid()) {
                                                blsKey = activeMasternode.blsKeyMasternode;
                                                break;
                                            }
                                        }
                                    }
                                    if (!blsKey.IsValid() && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
                                        blsKey = DeriveBLSFromCKey(privKey);
                                    }
                                    if (blsKey.IsValid()) {
                                        if (SignBLSWithECDSAFallback(puzzleHash, privKey, blsKey, vchSig)) {
                                            solved = true;
                                        }
                                    } else {
                                        LogPrintf("BitcoinMiner: Solved puzzle but cannot sign because no direct BLS key is associated with myMinerKey %s\n", myMinerKey.GetID().ToString());
                                    }
                                    break;
                                }
                                nNonce++;
                            }

                            if (solved && GetChainTip() == pindexPrev) {
                                CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                                ss << nNonce << vchSig;
                                std::vector<unsigned char> vchSolution(ss.begin(), ss.end());

                                {
                                    LOCK(cs_adam_solutions);
                                    mapAdamSolutionsCache[pindexPrev->GetBlockHash()][myMinerKey] = vchSolution;
                                }

                                CAdamSolutionMsg solMsg;
                                solMsg.hashPrevBlock = pindexPrev->GetBlockHash();
                                solMsg.minerKey = myMinerKey;
                                solMsg.vchSolution = vchSolution;

                                if (g_connman) {
                                    g_connman->ForEachNode([&solMsg](CNode* pnode) {
                                        g_connman->PushMessage(pnode, CNetMsgMaker(pnode->GetSendVersion()).Make(NetMsgType::ADAMSOL, solMsg));
                                    });
                                    LogPrintf("BitcoinMiner: Broadcasted adamsol for miner key %s and tip %s\n",
                                        myMinerKey.GetID().ToString(), pindexPrev->GetBlockHash().ToString());
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!fProofOfStake && IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            bool isCoordinator = false;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                CKey coordKey;
                if (pwallet && (pwallet->GetKey(expectedCoordinator.GetID(), coordKey) ||
                                pwallet->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey))) {
                    isCoordinator = true;
                } else {
                    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                        if (ComparePubKeys(activeMasternode.pubKeyMasternode, expectedCoordinator)) {
                            isCoordinator = true;
                            break;
                        }
                    }
                }
                if (!isCoordinator && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
                    for (int i = 0; i < 15; ++i) {
                        if (ComparePubKeys(GetAdamDeterministicPubKey(i), expectedCoordinator)) {
                            isCoordinator = true;
                            break;
                        }
                    }
                }
            }
            if (!isCoordinator) {
                MilliSleep(1000);
                continue;
            }
        }

        if (fProofOfStake) {
            if (!consensus.NetworkUpgradeActive(pindexPrev->nHeight + 1, Consensus::UPGRADE_POS)) {
                // The last PoW block hasn't even been mined yet.
                MilliSleep(nSpacingMillis); // sleep a block
                continue;
            }

            if (IsAdamActive(pindexPrev->nHeight + 1, consensus)) {
                uint256 adamSeed = GetAdamSeed(pindexPrev);
                std::vector<CPubKey> vExpectedMiners;
                CPubKey expectedCoordinator;
                bool isCoordinator = false;
                if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                    CKey coordKey;
                    if (pwallet && (pwallet->GetKey(expectedCoordinator.GetID(), coordKey) ||
                                    pwallet->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey))) {
                        isCoordinator = true;
                    } else {
                        for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
                            if (ComparePubKeys(activeMasternode.pubKeyMasternode, expectedCoordinator)) {
                                isCoordinator = true;
                                break;
                            }
                        }
                    }
                    if (!isCoordinator) {
                        for (int i = 0; i < 15; ++i) {
                            if (ComparePubKeys(GetAdamDeterministicPubKey(i), expectedCoordinator)) {
                                isCoordinator = true;
                                break;
                            }
                        }
                    }
                }
                if (!isCoordinator) {
                    MilliSleep(1000);
                    continue;
                }
            }

            // update fStakeableCoins (5 minute check time);
            CheckForCoins(pwallet, 5, &availableCoins);

            while ((g_connman && g_connman->GetNodeCount(CConnman::CONNECTIONS_ALL) == 0 && Params().MiningRequiresPeers()) || pwallet->IsLocked() || (fProofOfStake && !fStakeableCoins) || !fMasternodeSync) {
                MilliSleep(5000);
                // Do a separate 1 minute check here to ensure fStakeableCoins and fMasternodeSync is updated
                if ((fProofOfStake && !fStakeableCoins) || !fMasternodeSync) CheckForCoins(pwallet, 1, &availableCoins);
            }

            //search our map of hashed blocks, see if bestblock has been hashed yet
            if (pwallet->pStakerStatus &&
                pwallet->pStakerStatus->GetLastHash() == pindexPrev->GetBlockHash() &&
                pwallet->pStakerStatus->GetLastTime() >= GetCurrentTimeSlot()) {
                MilliSleep(2000);
                continue;
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
            uint256 adamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vExpectedMiners;
            CPubKey expectedCoordinator;
            if (SelectAdamNodes(adamSeed, consensus, vExpectedMiners, expectedCoordinator)) {
                CKey coordKey;
                bool gotKey = false;
                if (pwallet && (pwallet->GetKey(expectedCoordinator.GetID(), coordKey) ||
                                pwallet->GetKey(GetCompressedKeyID(expectedCoordinator), coordKey))) {
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
                if (!gotKey && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
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
                    if (pwallet) {
                        LOCK(pwallet->cs_wallet);
                        if (!pwallet->GetBLSKey(expectedCoordinator.GetID(), blsKey) || !blsKey.IsValid()) {
                            pwallet->GetBLSKey(GetCompressedKeyID(expectedCoordinator), blsKey);
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
                    if (!blsKey.IsValid() && (Params().IsRegTestNet() || Params().NetworkID() == CBaseChainParams::TESTNET)) {
                        blsKey = DeriveBLSFromCKey(coordKey);
                    }
                    if (blsKey.IsValid()) {
                        if (SignBLSWithECDSAFallback(pblock->GetHash(), coordKey, blsKey, pblock->vAdamCoordinatorSig)) {
                            LogPrintf("%s: Signed ADAM block as coordinator, hash: %s\n", 
                                __func__, pblock->GetHash().ToString());
                            LogPrintf("%s details: ver=%d, prev=%s, merkle=%s, time=%u, bits=%08x, nonce=%u, miners=%d, solutions=%d, vrf=%d, sig=%d\n",
                                __func__, pblock->nVersion, pblock->hashPrevBlock.ToString(), pblock->hashMerkleRoot.ToString(), pblock->nTime, pblock->nBits, pblock->nNonce,
                                pblock->vAdamMiners.size(), pblock->vAdamSolutions.size(), pblock->vAdamVRFProof.size(), pblock->vAdamCoordinatorSig.size());
                            SetThreadPriority(THREAD_PRIORITY_NORMAL);
                            ProcessBlockFound(pblock, *pwallet, opReservekey);
                            SetThreadPriority(THREAD_PRIORITY_LOWEST);

                            if (Params().IsRegTestNet())
                                throw boost::thread_interrupted();
                        } else {
                            LogPrintf("%s: Failed to sign ADAM block as coordinator\n", __func__);
                        }
                    } else {
                        LogPrintf("%s: Cannot sign ADAM block as coordinator because no direct BLS key is associated with expected coordinator %s\n",
                            __func__, expectedCoordinator.GetID().ToString());
                    }
                } else {
                    LogPrintf("%s: Wallet does not contain key for the elected coordinator %s\n", __func__, expectedCoordinator.GetID().ToString());
                }
            }
            MilliSleep(1000);
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

void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads)
{
    static boost::thread_group* minerThreads = NULL;
    fGenerateBitcoins = fGenerate;

    if (minerThreads != NULL) {
        minerThreads->interrupt_all();
        delete minerThreads;
        minerThreads = NULL;
    }

    if (nThreads == 0 || !fGenerate)
        return;

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

#endif // ENABLE_WALLET
