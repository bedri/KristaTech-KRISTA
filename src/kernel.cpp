// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "kernel.h"

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
    bnTarget *= (uint256(stakeValue) / 100);

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
    nTimeTx = fTimeProtocolV2 ? pindexPrev->MinPastBlockTime() : GetAdjustedTime();

    if (!stakeInput) return false;

    int slotStep = fTimeProtocolV2 ? nTimeSlotLength : 1;

    nTimeTx = (nTimeTx / slotStep) * slotStep;

    while(nTimeTx <= pindexPrev->MinPastBlockTime()) {
        nTimeTx += slotStep;
    }

    while(nTimeTx <= (fTimeProtocolV2 ? pindexPrev->MaxFutureBlockTime() : pindexPrev->GetBlockTime() + HASH_DRIFT)) {
        // Verify Proof Of Stake
        if (stakeInput->ContextCheck(nHeightTx, nTimeTx)) {
            CStakeKernel stakeKernel(pindexPrev, stakeInput, nBits, nTimeTx);
            if(stakeKernel.CheckKernelHash(true)) return true;
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

bool ExtractTimelock(const CScript& scriptPubKey, int64_t& nLockTime, bool& fIsBlockHeight)
{
    CScript::const_iterator pc = scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> vch;
    
    int64_t nLastInteger = -1;
    
    while (pc < scriptPubKey.end()) {
        if (!scriptPubKey.GetOp(pc, opcode, vch))
            break;
            
        if (opcode >= OP_0 && opcode <= OP_16) {
            nLastInteger = opcode - OP_0;
        } else if (vch.size() > 0) {
            try {
                CScriptNum snum(vch, true);
                nLastInteger = snum.getint64();
            } catch (...) {
                // Ignore parsing errors for non-numeric pushes
            }
        }
        
        if (opcode == OP_CHECKLOCKTIMEVERIFY || opcode == OP_CHECKSEQUENCEVERIFY) {
            if (nLastInteger >= 0) {
                nLockTime = nLastInteger;
                if (opcode == OP_CHECKLOCKTIMEVERIFY) {
                    fIsBlockHeight = (nLockTime < 500000000);
                } else { // OP_CHECKSEQUENCEVERIFY
                    bool fDisable = (nLockTime & (1 << 31)) != 0;
                    bool fIsTimestamp = (nLockTime & (1 << 22)) != 0;
                    if (!fDisable && !fIsTimestamp) {
                        nLockTime = nLockTime & 0xffff;
                        fIsBlockHeight = true;
                    } else {
                        return false;
                    }
                }
                return true;
            }
        }
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

void InitializeBurnCache()
{
    LOCK(cs_burnCache);
    mapAddressBurns.clear();
    
    const CBlockIndex* pindex = chainActive.Genesis();
    if (!pindex) return;
    
    LogPrintf("Initializing Burn Cache...\n");
    int nHeight = 0;
    while (pindex) {
        nHeight = pindex->nHeight;
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
                if (hasBurn && !tx.vin.empty()) {
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
    LogPrintf("Burn Cache initialized with %d addresses.\n", mapAddressBurns.size());
}

CAmount GetActiveBurnWeight(const CTxDestination& dest, int nHeight)
{
    LOCK(cs_burnCache);
    if (mapAddressBurns.count(dest) == 0)
        return 0;
        
    CAmount nTotalBurnWeight = 0;
    const int T_MAX_BURN = Params().GetConsensus().nBurnDecayBlocks;
    const double beta = 5.0;
    
    for (const auto& burn : mapAddressBurns[dest]) {
        int T = nHeight - burn.nHeight;
        if (T >= 0 && T < T_MAX_BURN) {
            double decay = 1.0 - (double)T / (double)T_MAX_BURN;
            nTotalBurnWeight += (CAmount)(burn.nAmount * beta * decay);
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

    // 1. Proof of Masternode (PoM)
    CMasternode* pmn = mnodeman.Find(CTxIn(prevout));
    if (pmn != nullptr) {
        nWeightType = MPA_WEIGHT_POM;
        int t_active = mnodeman.GetMasternodeActiveLifetime(prevout);
        double divisor = (double)Params().GetConsensus().nMasternodeUptimeLimit;
        double factor = 1.0 + 1.0 * std::min((double)t_active / divisor, 1.0);
        return (CAmount)(nAmount * factor);
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
    if (ExtractTimelock(txout.scriptPubKey, nLockTime, fIsBlockHeight) && fIsBlockHeight) {
        nWeightType = MPA_WEIGHT_POL;
        int64_t L = 0;
        if (nLockTime > nHeight) {
            L = nLockTime - nHeight;
        } else {
            L = nLockTime; // relative CSV sequence
        }
        
        if (L > 0) {
            const int64_t L_MAX = 50000;
            const double gamma = 2.0;
            double factor = 1.0 + gamma * std::min((double)L / (double)L_MAX, 1.0);
            return (CAmount)(nAmount * factor);
        }
    }

    // 3. Proof of Burn (PoB)
    txnouttype type;
    std::vector<CTxDestination> addresses;
    int nRequired;
    if (ExtractDestinations(txout.scriptPubKey, type, addresses, nRequired) && addresses.size() > 0) {
        CAmount nBurnWeight = GetActiveBurnWeight(addresses[0], nHeight);
        if (nBurnWeight > 0) {
            nWeightType = MPA_WEIGHT_POB;
            return nAmount + nBurnWeight;
        }
    }

    // 4. Default: Proof of Stake (PoS)
    nWeightType = MPA_WEIGHT_POS;
    return nAmount;
}

