// Copyright (c) 2011-2013 The PPCoin developers
// Copyright (c) 2013-2014 The NovaCoin Developers
// Copyright (c) 2014-2018 The BlackCoin Developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTATECH_KERNEL_H
#define KRISTATECH_KERNEL_H

#include "main.h"
#include "stakeinput.h"

#define HASH_DRIFT 45

class CStakeKernel {
public:
    /**
     * CStakeKernel Constructor
     *
     * @param[in]   pindexPrev      index of the parent of the kernel block
     * @param[in]   stakeInput      input for the coinstake of the kernel block
     * @param[in]   nBits           target difficulty bits of the kernel block
     * @param[in]   nTimeTx         time of the kernel block
     */
    CStakeKernel(const CBlockIndex* const pindexPrev, CStakeInput* stakeInput, unsigned int nBits, int nTimeTx, const CCoinsViewCache* pView = nullptr);

    // Return stake kernel hash
    uint256 GetHash() const;

    // Set time for iterative time slot evaluation
    void SetTime(int nTimeIn) { nTime = nTimeIn; }

    // Check that the kernel hash meets the target required
    bool CheckKernelHash(bool fSkipLog = false) const;

private:
    const CBlockIndex* pindexPrev{nullptr};
    // kernel message hashed
    CDataStream stakeModifier{CDataStream(SER_GETHASH, 0)};
    int nTimeBlockFrom{0};
    CDataStream stakeUniqueness{CDataStream(SER_GETHASH, 0)};
    int nTime{0};
    // hash target
    unsigned int nBits{0};     // difficulty for the target
    CAmount stakeValue{0};     // target multiplier
};

/* PoS Validation */

/*
 * Stake                Check if stakeInput can stake a block on top of pindexPrev
 *
 * @param[in]   pindexPrev      index of the parent block of the block being staked
 * @param[in]   stakeInput      input for the coinstake
 * @param[in]   nBits           target difficulty bits
 * @param[in]   nTimeTx         new blocktime
 * @return      bool            true if stake kernel hash meets target protocol
 */
bool Stake(const CBlockIndex* pindexPrev, CStakeInput* stakeInput, unsigned int nBits, int64_t& nTimeTx);

/*
 * CheckProofOfStake    Check if block has valid proof of stake
 *
 * @param[in]   block           block with the proof being verified
 * @param[out]  strError        string returning error message (if any, else empty)
 * @param[in]   pindexPrev      index of the parent block
 *                              (if nullptr, it will be searched in mapBlockIndex)
 * @return      bool            true if the block has a valid proof of stake
 */
bool CheckProofOfStake(const CBlock& block, std::string& strError, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);

/*
 * GetStakeKernelHash   Return stake kernel of a block
 *
 * @param[out]  hashRet         hash of the kernel (set by this function)
 * @param[in]   block           block with the kernel to return
 * @param[in]   pindexPrev      index of the parent block
 *                              (if nullptr, it will be searched in mapBlockIndex)
 * @return      bool            false if kernel cannot be initialized, true otherwise
 */
bool GetStakeKernelHash(uint256& hashRet, const CBlock& block, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);

enum MPAWeightType {
    MPA_WEIGHT_POS = 0,
    MPA_WEIGHT_POL = 1,
    MPA_WEIGHT_POB = 2,
    MPA_WEIGHT_POM = 3
};

struct CBurnCoins {
    CAmount nAmount{0};
    int nHeight{0};

    CBurnCoins() = default;
    CBurnCoins(CAmount nAmountIn, int nHeightIn) : nAmount(nAmountIn), nHeight(nHeightIn) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nAmount);
        READWRITE(nHeight);
    }
};

extern std::map<COutPoint, std::vector<CBurnCoins>> mapOutpointBurns;
extern RecursiveMutex cs_burnCache;

void AddOutpointBurnToCache(const COutPoint& outpoint, CAmount nAmount, int nHeight);
void RemoveOutpointBurnFromCache(const COutPoint& outpoint, CAmount nAmount, int nHeight);
bool HasActiveOutpointBurn(const COutPoint& outpoint, int nHeight);
CAmount GetOutpointBurnWeight(const COutPoint& outpoint, int nHeight);
bool GetTransactionBurnAmount(const CTransaction& tx, int nHeight, CAmount& nBurnAmountOut);
bool ResolveVoluntaryBurnTargetOutpoint(const CTransaction& tx, int nHeight, const CScript& spenderScriptPubKey, const CCoinsViewCache* pView, COutPoint& targetOut);
void TransferOutpointBurnOnCoinstake(const COutPoint& fromOut, const COutPoint& toOut, int nHeight, const uint256& blockHash);
bool UndoOutpointBurnTransferForBlock(const CBlock& block, int nHeight, const uint256& blockHash, const CScript& prevStakeScript, CAmount nPrevStakeAmount);
extern bool g_fBurnCacheInitialized;
bool FlushBurnCacheToDisk(const uint256& hashBestBlock);
void InitializeBurnCache();
class CBurnCacheScopedBackup
{
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    CBurnCacheScopedBackup();
    ~CBurnCacheScopedBackup();
    CBurnCacheScopedBackup(const CBurnCacheScopedBackup&) = delete;
    CBurnCacheScopedBackup& operator=(const CBurnCacheScopedBackup&) = delete;
};
bool ResolveCoinOnBranch(const COutPoint& prevout, CTxOut& txoutOut, int& nHeightOut, CBlockIndex** ppindexFromOut = nullptr, const CBlockIndex* pindexPrev = nullptr, const CCoinsViewCache* pView = nullptr);
bool IsValidPoLSelfRenewal(const CTransaction& tx, unsigned int nIn, const CTxOut& prevout, int nSpendHeight);
CAmount CalculateMPAWeight(const COutPoint& prevout, CAmount nAmount, int nTimeTx, const CBlockIndex* pindexPrev, int& nWeightType, const CCoinsViewCache* pView = nullptr);

#endif // KRISTATECH_KERNEL_H
