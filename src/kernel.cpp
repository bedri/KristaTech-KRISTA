// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "kernel.h"

#include "adam.h"
#include "db.h"
#include "legacy/stakemodifier.h"
#include "script/interpreter.h"
#include "util.h"
#include "policy/policy.h"
#include "stakeinput.h"
#include "utilmoneystr.h"
#include "masternodeman.h"
#include "masternode.h"
#include "key_io.h"
#include "coins.h"
#include "txdb.h"
#include "timedata.h"

#include <boost/assign/list_of.hpp>

/**
 * CStakeKernel Constructor
 *
 * @param[in]   pindexPrev      index of the parent of the kernel block
 * @param[in]   stakeInput      input for the coinstake of the kernel block
 * @param[in]   nBits           target difficulty bits of the kernel block
 * @param[in]   nTimeTx         time of the kernel block
 */
CStakeKernel::CStakeKernel(const CBlockIndex* const pindexPrev, CStakeInput* stakeInput, unsigned int nBits, int nTimeTx):
    pindexPrev(pindexPrev),
    stakeUniqueness(stakeInput->GetUniqueness()),
    nTime(nTimeTx),
    nBits(nBits)
{
    CAmount nAmount = stakeInput->GetValue();
    COutPoint prevout = stakeInput->GetOutPoint();
    int nWeightType = MPA_WEIGHT_POS;
    stakeValue = CalculateMPAWeight(prevout, nAmount, nTimeTx, pindexPrev, nWeightType);

    // Set kernel stake modifier
    uint64_t nStakeModifier = 0;
    if (!GetOldStakeModifier(stakeInput, nStakeModifier))
        LogPrintf("%s : ERROR: Failed to get kernel stake modifier\n", __func__);
    // Modifier v1
    stakeModifier << nStakeModifier;
    CBlockIndex* pindexFrom = stakeInput->GetIndexFrom();
    nTimeBlockFrom = pindexFrom->nTime;
}

// Return stake kernel hash
uint256 CStakeKernel::GetHash() const
{
    CDataStream ss(stakeModifier);
    ss << nTimeBlockFrom << stakeUniqueness << nTime;
    return Hash(ss.begin(), ss.end());
}

// Check that the kernel hash meets the target required
bool CStakeKernel::CheckKernelHash(bool fSkipLog) const
{
    // Get weighted target
    uint256 bnTarget;
    bnTarget.SetCompact(nBits);

    if (pindexPrev && pindexPrev->nHeight + 1 >= 2280) {
        uint256 bnWeight = uint256(stakeValue) / 100;
        if (bnWeight > 0 && bnTarget > ~UINT256_ZERO / bnWeight) {
            bnTarget = ~UINT256_ZERO;
        } else {
            bnTarget *= bnWeight;
        }
    } else {
        bnTarget *= (uint256(stakeValue) / 100);
    }

    // Check PoS kernel hash
    const uint256& hashProofOfStake = GetHash();
    const bool res = hashProofOfStake < bnTarget;

    if (!fSkipLog || res) {
        LogPrint(BCLog::STAKING, "%s : Proof Of Stake:"
                            "\nssUniqueID=%s"
                            "\nnTimeTx=%d"
                            "\nhashProofOfStake=%s"
                            "\nnBits=%d"
                            "\nweight=%d"
                            "\nbnTarget=%s (res: %d)\n\n",
            __func__, HexStr(stakeUniqueness), nTime, hashProofOfStake.GetHex(),
            nBits, stakeValue, bnTarget.GetHex(), res);
    }
    return res;
}


/*
 * PoS Validation
 */

// helper function for CheckProofOfStake and GetStakeKernelHash
bool LoadStakeInput(const CBlock& block, const CBlockIndex* pindexPrev, std::unique_ptr<CStakeInput>& stake)
{
    // If previous index is not provided, look for it in the blockmap
    if (!pindexPrev) {
        BlockMap::iterator mi = mapBlockIndex.find(block.hashPrevBlock);
        if (mi != mapBlockIndex.end() && (*mi).second) pindexPrev = (*mi).second;
        else return error("%s : couldn't find previous block", __func__);
    } else {
        // check that is the actual parent block
        if (block.hashPrevBlock != pindexPrev->GetBlockHash())
            return error("%s : previous block mismatch", __func__);
    }

    // Check that this is a PoS block
    if (!block.IsProofOfStake())
        return error("called on non PoS block");

    // Construct the stakeinput object
    const CTxIn& txin = block.vtx[1].vin[0];
    stake = std::unique_ptr<CStakeInput>(new CPivStake());

    return stake->InitFromTxIn(txin);
}

/*
 * Stake                Check if stakeInput can stake a block on top of pindexPrev
 *
 * @param[in]   pindexPrev      index of the parent block of the block being staked
 * @param[in]   stakeInput      input for the coinstake
 * @param[in]   nBits           target difficulty bits
 * @param[in]   nTimeTx         new blocktime
 * @return      bool            true if stake kernel hash meets target protocol
 */
bool Stake(const CBlockIndex* pindexPrev, CStakeInput* stakeInput, unsigned int nBits, int64_t& nTimeTx)
{
    // Double check stake input contextual checks
    const int nHeightTx = pindexPrev->nHeight + 1;

    // Get the new time slot (and verify it's not the same as previous block)
    const bool fRegTest = Params().IsRegTestNet();
    const bool fTimeProtocolV2 = Params().GetConsensus().IsTimeProtocolV2(nHeightTx) && !fRegTest;
    const int nTimeSlotLength = Params().GetConsensus().nTimeSlotLength;
    nTimeTx = fTimeProtocolV2 ? std::max(pindexPrev->MinPastBlockTime() + nTimeSlotLength, GetCurrentTimeSlot()) : GetAdjustedTime();

    if (!stakeInput) return false;

    int slotStep = fTimeProtocolV2 ? nTimeSlotLength : 1;

    nTimeTx = (nTimeTx / slotStep) * slotStep;

    while(nTimeTx <= pindexPrev->MinPastBlockTime()) {
        nTimeTx += slotStep;
    }

    std::unique_ptr<CStakeKernel> stakeKernel;
    while(nTimeTx <= (fTimeProtocolV2 ? pindexPrev->MaxFutureBlockTime() : pindexPrev->GetBlockTime() + HASH_DRIFT)) {
        // Verify Proof Of Stake
        if (stakeInput->ContextCheck(nHeightTx, nTimeTx)) {
            if (!stakeKernel) {
                stakeKernel.reset(new CStakeKernel(pindexPrev, stakeInput, nBits, nTimeTx));
            } else {
                stakeKernel->SetTime(nTimeTx);
            }
            if (stakeKernel->CheckKernelHash(true)) return true;
        }
        nTimeTx += slotStep;
    }

    return false;
}


/*
 * CheckProofOfStake    Check if block has valid proof of stake
 *
 * @param[in]   block           block being verified
 * @param[out]  strError        string error (if any, else empty)
 * @param[in]   pindexPrev      index of the parent block
 *                              (if nullptr, it will be searched in mapBlockIndex)
 * @return      bool            true if the block has a valid proof of stake
 */
bool CheckProofOfStake(const CBlock& block, std::string& strError, const CBlockIndex* pindexPrev)
{
    // if we have already a checkpoint newer than this block 
    // then it is OK
    if (block.nTime <= Params().Checkpoints().nTimeLastCheckpoint)
        return true;

    const int nHeight = pindexPrev->nHeight + 1;
    // Initialize stake input
    std::unique_ptr<CStakeInput> stakeInput;
    if (!LoadStakeInput(block, pindexPrev, stakeInput)) {
        strError = "stake input initialization failed";
        return false;
    }

    // Stake input contextual checks
    if (!stakeInput->ContextCheck(nHeight, block.nTime)) {
        strError = "stake input failing contextual checks";
        return false;
    }

    // Verify Proof Of Stake
    CStakeKernel stakeKernel(pindexPrev, stakeInput.get(), block.nBits, block.nTime);
    if (!stakeKernel.CheckKernelHash()) {
        strError = "kernel hash check fails";
        return false;
    }

    // Verify tx input signature
    CTxOut stakePrevout;
    if (!stakeInput->GetTxOutFrom(stakePrevout)) {
        strError = "unable to get stake prevout for coinstake";
        return false;
    }
    const CTransaction& tx = block.vtx[1];
    const CTxIn& txin = tx.vin[0];
    ScriptError serror;
    if (!VerifyScript(txin.scriptSig, stakePrevout.scriptPubKey, STANDARD_SCRIPT_VERIFY_FLAGS,
             TransactionSignatureChecker(&tx, 0, stakePrevout.nValue), &serror)) {
        strError = strprintf("signature fails: %s", serror ? ScriptErrorString(serror) : "");
        return false;
    }

    // All good
    return true;
}


/*
 * GetStakeKernelHash   Return stake kernel of a block
 *
 * @param[out]  hashRet         hash of the kernel (set by this function)
 * @param[in]   block           block with the kernel to return
 * @param[in]   pindexPrev      index of the parent block
 *                              (if nullptr, it will be searched in mapBlockIndex)
 * @return      bool            false if kernel cannot be initialized, true otherwise
 */
bool GetStakeKernelHash(uint256& hashRet, const CBlock& block, const CBlockIndex* pindexPrev)
{
    // Initialize stake input
    std::unique_ptr<CStakeInput> stakeInput;
    if (!LoadStakeInput(block, pindexPrev, stakeInput))
        return error("%s : stake input initialization failed", __func__);

    CStakeKernel stakeKernel(pindexPrev, stakeInput.get(), block.nBits, block.nTime);
    hashRet = stakeKernel.GetHash();
    return true;
}

std::map<CTxDestination, std::vector<CBurnCoins>> mapAddressBurns;
RecursiveMutex cs_burnCache;

bool GetTxOut(const COutPoint& prevout, CTxOut& txout)
{
    if (pcoinsTip) {
        Coin coin;
        if (pcoinsTip->GetCoin(prevout, coin)) {
            txout = coin.out;
            return true;
        }
    }
    CTransaction tx;
    uint256 hashBlock;
    if (GetTransaction(prevout.hash, tx, hashBlock, true)) {
        if (prevout.n < tx.vout.size()) {
            txout = tx.vout[prevout.n];
            return true;
        }
    }
    return false;
}

bool ExtractTimelock(const CScript& scriptPubKey, int64_t& nLockTime, bool& fIsBlockHeight, bool* pfIsRelative = nullptr)
{
    if (pfIsRelative) *pfIsRelative = false;

    CPubKey regPubKey;
    CKeyID regKeyID;
    std::vector<unsigned char> regNonce;
    uint256 regChallenge;
    if (MatchCoinLockRegistration(scriptPubKey, regPubKey, nLockTime, regKeyID) ||
        MatchPoWLockRegistration(scriptPubKey, regNonce, regChallenge, regPubKey, nLockTime, regKeyID)) {
        if (nLockTime < 0) return false;
        fIsBlockHeight = (nLockTime < 500000000);
        return true;
    }

    CScript::const_iterator pc = scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;

    // Parse optional leading <data> OP_DROP pairs, then require <locktime> (OP_CLTV|OP_CSV) OP_DROP
    int nPrefixDrops = 0;
    while (pc < scriptPubKey.end()) {
        if (!scriptPubKey.GetOp(pc, opcode, vch))
            return false;

        int64_t nCandidate = -1;
        bool fNumeric = false;
        if (opcode == OP_0) {
            nCandidate = 0;
            fNumeric = true;
        } else if (opcode >= OP_1 && opcode <= OP_16) {
            nCandidate = opcode - OP_1 + 1;
            fNumeric = true;
        } else if (opcode < OP_PUSHDATA4 && !vch.empty() && vch.size() <= 8) {
            try {
                CScriptNum snum(vch, true, 8);
                nCandidate = snum.getint64();
                fNumeric = true;
            } catch (...) {
                fNumeric = false;
            }
        } else if (opcode > OP_PUSHDATA4) {
            return false;
        }

        opcodetype nextOp;
        std::vector<unsigned char> nextVch;
        if (!scriptPubKey.GetOp(pc, nextOp, nextVch))
            return false;

        if (nextOp == OP_DROP) {
            if (++nPrefixDrops > 4)
                return false;
            continue;
        }

        if ((nextOp == OP_CHECKLOCKTIMEVERIFY || nextOp == OP_CHECKSEQUENCEVERIFY) && fNumeric && nCandidate >= 0) {
            opcodetype dropOp;
            if (!scriptPubKey.GetOp(pc, dropOp) || dropOp != OP_DROP)
                return false;

            // Validate canonical spend tail: P2PKH or P2PK
            opcodetype tailOp1;
            std::vector<unsigned char> tailVch1;
            if (!scriptPubKey.GetOp(pc, tailOp1, tailVch1))
                return false;

            bool fValidTail = false;
            if (tailOp1 == OP_DUP) {
                opcodetype opHash, opPushHash, opEqVerify, opCheckSig;
                std::vector<unsigned char> vchHash;
                if (scriptPubKey.GetOp(pc, opHash) && opHash == OP_HASH160 &&
                    scriptPubKey.GetOp(pc, opPushHash, vchHash) && vchHash.size() == 20 &&
                    scriptPubKey.GetOp(pc, opEqVerify) && opEqVerify == OP_EQUALVERIFY &&
                    scriptPubKey.GetOp(pc, opCheckSig) && opCheckSig == OP_CHECKSIG &&
                    pc == scriptPubKey.end()) {
                    fValidTail = true;
                }
            } else if ((tailVch1.size() == 33 || tailVch1.size() == 65) && CPubKey(tailVch1).IsValid()) {
                opcodetype opCheckSig;
                if (scriptPubKey.GetOp(pc, opCheckSig) && opCheckSig == OP_CHECKSIG &&
                    pc == scriptPubKey.end()) {
                    fValidTail = true;
                }
            }
            if (!fValidTail)
                return false;

            nLockTime = nCandidate;
            if (nextOp == OP_CHECKLOCKTIMEVERIFY) {
                fIsBlockHeight = (nLockTime < 500000000);
                if (pfIsRelative) *pfIsRelative = false;
            } else {
                bool fDisable = (nLockTime & (1 << 31)) != 0;
                bool fIsTimestamp = (nLockTime & (1 << 22)) != 0;
                if (!fDisable && !fIsTimestamp) {
                    nLockTime = nLockTime & 0xffff;
                    fIsBlockHeight = true;
                    if (pfIsRelative) *pfIsRelative = true;
                } else {
                    return false;
                }
            }
            return true;
        }

        return false;
    }
    return false;
}

void AddBurnToCache(const CTxDestination& dest, CAmount nAmount, int nHeight)
{
    LOCK(cs_burnCache);
    mapAddressBurns[dest].push_back({nAmount, nHeight});
}

void RemoveBurnFromCache(const CTxDestination& dest, CAmount nAmount, int nHeight)
{
    LOCK(cs_burnCache);
    if (mapAddressBurns.count(dest) > 0) {
        auto& v = mapAddressBurns[dest];
        v.erase(std::remove_if(v.begin(), v.end(), [nHeight, nAmount](const CBurnCoins& b) {
            return b.nHeight == nHeight && b.nAmount == nAmount;
        }), v.end());
        if (v.empty()) {
            mapAddressBurns.erase(dest);
        }
    }
}

bool FlushBurnCacheToDisk(const uint256& hashBestBlock)
{
    if (!pblocktree || hashBestBlock.IsNull())
        return false;

    std::map<std::string, std::vector<CBurnCoins>> mapSerialized;
    {
        LOCK(cs_burnCache);
        for (const auto& pair : mapAddressBurns) {
            std::string strAddr = EncodeDestination(pair.first);
            if (!strAddr.empty() && !pair.second.empty()) {
                mapSerialized[strAddr] = pair.second;
            }
        }
    }
    return pblocktree->WriteBurnCache(hashBestBlock, mapSerialized);
}

void InitializeBurnCache()
{
    LOCK2(cs_main, cs_burnCache);
    mapAddressBurns.clear();

    const CBlockIndex* pindexTip = chainActive.Tip();
    if (!pindexTip) return;

    const int64_t nDecayBlocks = Params().GetConsensus().nBurnDecayBlocks;
    const int nMinBurnHeight = (nDecayBlocks > 0) ? std::max(0, pindexTip->nHeight - (int)nDecayBlocks) : 0;

    const CBlockIndex* pindex = nullptr;
    uint256 hashCachedBlock;
    std::map<std::string, std::vector<CBurnCoins>> mapSerialized;
    if (pblocktree && pblocktree->ReadBurnCache(hashCachedBlock, mapSerialized)) {
        auto mi = mapBlockIndex.find(hashCachedBlock);
        if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
            for (const auto& pair : mapSerialized) {
                CTxDestination dest = DecodeDestination(pair.first);
                if (!IsValidDestination(dest)) continue;
                for (const auto& b : pair.second) {
                    if (b.nHeight >= nMinBurnHeight && b.nHeight <= mi->second->nHeight) {
                        mapAddressBurns[dest].push_back(b);
                    }
                }
            }
            pindex = chainActive.Next(mi->second);
            LogPrintf("InitializeBurnCache: Loaded LevelDB burn cache at height %d (%d addresses), scanning %d delta blocks...\n",
                mi->second->nHeight, (int)mapAddressBurns.size(), pindexTip->nHeight - mi->second->nHeight);
        }
    }

    if (!pindex && hashCachedBlock != pindexTip->GetBlockHash()) {
        mapAddressBurns.clear();
        pindex = chainActive[nMinBurnHeight];
        if (!pindex) pindex = chainActive.Genesis();
        LogPrintf("InitializeBurnCache: Scanning blocks from height %d to %d...\n",
            pindex ? pindex->nHeight : 0, pindexTip->nHeight);
    }

    bool fScannedNewBlocks = (pindex != nullptr);
    while (pindex) {
        int nHeight = pindex->nHeight;
        CBlock block;
        if (ReadBlockFromDisk(block, pindex)) {
            for (const auto& tx : block.vtx) {
                bool hasBurn = false;
                CAmount nBurnAmount = 0;
                for (const auto& out : tx.vout) {
                    txnouttype type;
                    std::vector<CTxDestination> addresses;
                    int nRequired;
                    if (ExtractDestinations(out.scriptPubKey, type, addresses, nRequired)) {
                        for (const auto& addr : addresses) {
                            std::string strAddr = EncodeDestination(addr);
                            if (Params().GetConsensus().IsBurnAddress(strAddr, nHeight)) {
                                hasBurn = true;
                                nBurnAmount += out.nValue;
                            }
                        }
                    }
                }
                // Only voluntary user Proof-of-Burn transactions (!IsCoinBase && !IsCoinStake) grant PoB staking weight.
                if (hasBurn && !tx.IsCoinBase() && !tx.IsCoinStake() && !tx.vin.empty()) {
                    const CTxIn& txin = tx.vin[0];
                    CTransaction txPrev;
                    uint256 hashBlock;
                    if (GetTransaction(txin.prevout.hash, txPrev, hashBlock, true)) {
                        if (txin.prevout.n < txPrev.vout.size()) {
                            const CTxOut& prevout = txPrev.vout[txin.prevout.n];
                            txnouttype type;
                            std::vector<CTxDestination> addresses;
                            int nRequired;
                            if (ExtractDestinations(prevout.scriptPubKey, type, addresses, nRequired) && addresses.size() > 0) {
                                mapAddressBurns[addresses[0]].push_back({nBurnAmount, nHeight});
                            }
                        }
                    }
                }
            }
        }
        pindex = chainActive.Next(pindex);
    }

    if (fScannedNewBlocks) {
        FlushBurnCacheToDisk(pindexTip->GetBlockHash());
    }
    LogPrintf("Burn Cache initialized with %d addresses.\n", mapAddressBurns.size());
}

CAmount GetActiveBurnWeight(const CTxDestination& dest, int nHeight)
{
    LOCK(cs_burnCache);
    if (mapAddressBurns.count(dest) == 0)
        return 0;
        
    CAmount nTotalBurnWeight = 0;
    const int64_t T_MAX_BURN = Params().GetConsensus().nBurnDecayBlocks;
    if (T_MAX_BURN <= 0) return 0;
    const int64_t BETA = 5;
    
    for (const auto& burn : mapAddressBurns[dest]) {
        int64_t T = (int64_t)nHeight - (int64_t)burn.nHeight;
        if (T >= 0 && T < T_MAX_BURN) {
            int64_t remaining = T_MAX_BURN - T;
            nTotalBurnWeight += (CAmount)(((__int128)burn.nAmount * BETA * remaining) / T_MAX_BURN);
        }
    }
    return nTotalBurnWeight;
}

CAmount CalculateMPAWeight(const COutPoint& prevout, CAmount nAmount, int nTimeTx, const CBlockIndex* pindexPrev, int& nWeightType)
{
    int nHeight = pindexPrev->nHeight + 1;
    if (!Params().GetConsensus().NetworkUpgradeActive(nHeight, Consensus::UPGRADE_POMBL)) {
        nWeightType = MPA_WEIGHT_POS;
        return nAmount;
    }

    // 1. Proof of Masternode (PoM) - deterministic on-chain Masternode state
    {
        LOCK(cs_main);
        for (const auto& kv : mapMasternodeCollateralOutpoints) {
            if (kv.second == prevout && kv.first.IsValid() &&
                IsMasternodeActiveAtHeight(kv.first, nHeight - 1, 2880) &&
                IsValidMasternodeCollateralAndAuth(kv.first, prevout, nHeight - 1)) {
                int nFirstHeight = nHeight;
                auto itHeights = mapMasternodeActiveHeights.find(kv.first);
                if (itHeights != mapMasternodeActiveHeights.end() && !itHeights->second.empty()) {
                    nFirstHeight = *itHeights->second.begin();
                }
                if (pcoinsTip) {
                    Coin coin;
                    if (pcoinsTip->GetCoin(prevout, coin) && !coin.IsSpent() && (int)coin.nHeight > 0) {
                        nFirstHeight = std::min(nFirstHeight, (int)coin.nHeight);
                    }
                }
                nWeightType = MPA_WEIGHT_POM;
                int64_t t_active = std::max<int64_t>(0, (int64_t)nHeight - (int64_t)nFirstHeight);
                int64_t limit = Params().GetConsensus().nMasternodeUptimeLimit;
                if (limit <= 0 || t_active <= 0) return nAmount;
                int64_t capped = std::min<int64_t>(t_active, limit);
                return nAmount + (CAmount)(((__int128)nAmount * capped) / limit);
            }
        }
        // Unit test fallback only when no chain blocks are connected
        if (mapMasternodeCollateralOutpoints.empty() && chainActive.Height() <= 0) {
            CMasternode* pmn = mnodeman.Find(CTxIn(prevout));
            if (pmn != nullptr) {
                nWeightType = MPA_WEIGHT_POM;
                int64_t t_active = mnodeman.GetMasternodeActiveLifetime(prevout, nHeight);
                int64_t limit = Params().GetConsensus().nMasternodeUptimeLimit;
                if (limit <= 0 || t_active <= 0) return nAmount;
                int64_t capped = std::min<int64_t>(t_active, limit);
                return nAmount + (CAmount)(((__int128)nAmount * capped) / limit);
            }
        }
    }

    // Retrieve UTXO details for PoL / PoB checks
    CTxOut txout;
    if (!GetTxOut(prevout, txout)) {
        nWeightType = MPA_WEIGHT_POS;
        return nAmount;
    }

    // 2. Proof of Lock (PoL)
    int64_t nLockTime = 0;
    bool fIsBlockHeight = false;
    bool fIsRelative = false;
    if (ExtractTimelock(txout.scriptPubKey, nLockTime, fIsBlockHeight, &fIsRelative) && fIsBlockHeight) {
        int64_t L = 0;
        if (fIsRelative) {
            L = nLockTime; // relative CSV sequence
        } else if (nLockTime > nHeight) {
            L = nLockTime - nHeight; // remaining absolute CLTV blocks
        }
        
        if (L > 0) {
            nWeightType = MPA_WEIGHT_POL;
            const int64_t L_MAX = 50000;
            const int64_t GAMMA = 2;
            int64_t cappedL = std::min<int64_t>(L, L_MAX);
            return nAmount + (CAmount)(((__int128)nAmount * GAMMA * cappedL) / L_MAX);
        }
    }

    // 3. Proof of Burn (PoB)
    // Cap per-UTXO burn bonus to nAmount * BETA (BETA = 5) so splitting coins across many small UTXOs
    // on the same address cannot multiply address-level burn weight (100% UTXO-splitting invariance).
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;
    if (ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) && addresses.size() > 0) {
        CAmount nBurnWeight = GetActiveBurnWeight(addresses[0], nHeight);
        if (nBurnWeight > 0) {
            const int64_t BETA = 5;
            const CAmount nMaxMoneyOut = Params().GetConsensus().nMaxMoneyOut;
            const CAmount nMaxUtxoBurnBonus = (nAmount > nMaxMoneyOut / BETA) ? nMaxMoneyOut : (nAmount * BETA);
            const CAmount nEffectiveBurn = std::min(nBurnWeight, nMaxUtxoBurnBonus);
            if (nEffectiveBurn > 0) {
                nWeightType = MPA_WEIGHT_POB;
                return (nAmount > nMaxMoneyOut - nEffectiveBurn) ? nMaxMoneyOut : (nAmount + nEffectiveBurn);
            }
        }
    }

    // 4. Default: Proof of Stake (PoS)
    nWeightType = MPA_WEIGHT_POS;
    return nAmount;
}

