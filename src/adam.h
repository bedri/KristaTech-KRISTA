// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_ADAM_H
#define BITCOIN_ADAM_H

#include "pubkey.h"
#include "key.h"
#include "uint256.h"
#include "crypto/bls.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "netbase.h"
#include "sync.h"
#include "script/script.h"
#include <vector>
#include <map>
#include <set>

class CCoinsViewCache;
class CKeyStore;

struct COnChainMasternodeDelegation {
    CPubKey pubKeyCollateralAddress;
    CPubKey pubKeyMasternode;
    CService addr;
    int64_t sigTime = 0;
    int protocolVersion = 0;
    int nMessVersion = 0;
    std::vector<unsigned char> vchCollateralSig;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(pubKeyCollateralAddress);
        READWRITE(pubKeyMasternode);
        READWRITE(addr);
        READWRITE(sigTime);
        READWRITE(protocolVersion);
        READWRITE(nMessVersion);
        READWRITE(vchCollateralSig);
    }
};

extern RecursiveMutex cs_adam_seeds;
extern std::map<uint256, uint256> mapAdamSeeds;

extern RecursiveMutex cs_recent_vrf_proofs;
extern std::map<uint256, std::vector<unsigned char>> mapRecentVRFProofs;

struct COnChainMasternodeStateCache {
    uint256 hashBestBlock;
    std::map<CPubKey, int> mapLastActiveHeight;
    std::map<CPubKey, std::set<int>> mapActiveHeights;
    std::map<CPubKey, COutPoint> mapCollateralOutpoints;
    std::map<COutPoint, COnChainMasternodeDelegation> mapCollateralDelegations;
    std::map<CPubKey, std::pair<CBLSPubKey, int>> mapPinnedBLS;
    std::map<COutPoint, int> mapCollateralDelegationFirstHeight;
    std::map<COutPoint, std::vector<std::pair<int, COnChainMasternodeDelegation>>> mapCollateralDelegationHistory;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashBestBlock);
        READWRITE(mapLastActiveHeight);
        READWRITE(mapActiveHeights);
        READWRITE(mapCollateralOutpoints);
        READWRITE(mapCollateralDelegations);
        READWRITE(mapPinnedBLS);
        try {
            READWRITE(mapCollateralDelegationFirstHeight);
            READWRITE(mapCollateralDelegationHistory);
        } catch (...) {}
    }
};

struct CCachedBlockRegEntry {
    bool isCoinLock{false};
    COutPoint outpoint;
    CAmount nValue{0};
    CPubKey pubkey;
    int64_t lockTime{0};
    uint256 challenge;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(isCoinLock);
        READWRITE(outpoint);
        READWRITE(nValue);
        READWRITE(pubkey);
        READWRITE(lockTime);
        READWRITE(challenge);
    }
};

struct CAdamBlockRegStateCache {
    uint256 hashBestBlock;
    uint256 cachedGenesisHash;
    int cachedBootstrapScannedUpTo{0};
    std::set<CPubKey> cachedBootstrapKeys;
    std::vector<std::pair<uint256, std::vector<CCachedBlockRegEntry>>> vRecentBlockRegs;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashBestBlock);
        READWRITE(cachedGenesisHash);
        READWRITE(cachedBootstrapScannedUpTo);
        READWRITE(cachedBootstrapKeys);
        READWRITE(vRecentBlockRegs);
    }
};

struct CAdamValidatorPoolInfo {
    CPubKey pubkey;
    std::string track;
    int64_t nUnlockHeight{0};
};

extern std::map<CPubKey, int> mapMasternodeLastActiveHeight;
extern std::map<CPubKey, std::set<int>> mapMasternodeActiveHeights;
extern std::map<CPubKey, COutPoint> mapMasternodeCollateralOutpoints;
extern std::map<COutPoint, COnChainMasternodeDelegation> mapOnChainCollateralDelegations;
extern bool g_fMasternodePingStateInitialized;
void InitializeOnChainMasternodePings(bool fForceRescan = false);
bool FlushMasternodePingStateToDisk(const uint256& hashBestBlock);
void InitializeAdamMinerRegCache(bool fForceRescan = false);
bool FlushAdamMinerRegCacheToDisk(const uint256& hashBestBlock);
CScript GetMasternodePingScript(const CPubKey& pubKeyMasternode);
bool ExtractOnChainMasternodePing(const CTransaction& tx, CPubKey& pubKeyMasternodeOut, COutPoint& collateralOutpointOut, bool fRecordDelegation = false, COnChainMasternodeDelegation* pDelegationOut = nullptr, const CCoinsViewCache* pView = nullptr, int nHeight = -1, bool fStrictRelayFormat = false);
bool IsValidMasternodeCollateralAndAuth(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight = -1, const COnChainMasternodeDelegation* pInlineDelegation = nullptr, const CCoinsViewCache* pView = nullptr);
bool IsMasternodeActiveAtHeight(const CPubKey& pubKeyMasternode, int nQueryHeight, int nWindow = 2880);
bool RegisterOnChainMasternodePing(const CPubKey& pubKeyMasternode, const COutPoint& collateralOutpoint, int nHeight, const COnChainMasternodeDelegation* pInlineDelegation = nullptr, const CCoinsViewCache* pView = nullptr);
void BumpMasternodePingStateGen();
void RegisterBlockMasternodeActivity(const CBlock& block, int nHeight, const CCoinsViewCache* pView = nullptr);
void RevertOnChainCollateralDelegation(const COutPoint& collateralOutpoint, const CPubKey& disconnectedPubKey, int nDisconnectedHeight, CPubKey* pRestoredOldKeyOut = nullptr);
bool RestoreMasternodeActivityFromBlock(const CBlock& block, int nHeight, const CPubKey& targetPubKey, const COutPoint& targetCollateral, const CCoinsViewCache* pView = nullptr, CPubKey* pRestoredPubKeyOut = nullptr);
uint256 ComputeNextAdamSeed(const uint256& prevSeed, const std::vector<unsigned char>& vAdamVRFProof);
bool MatchCoinLockRegistration(const CScript& script, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut);
bool MatchPoWLockRegistration(const CScript& script, std::vector<unsigned char>& nonceOut, uint256& challengeOut, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut);

// Helper functions for ADAM (A Decentralized Approach Model) cooperative consensus

static const CAmount MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN;
static const CAmount POW_LOCK_REGISTRATION_AMOUNT = 10 * COIN;
static const int64_t ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45;
static const int ADAM_COLLATERALIZED_TRACK_QUOTA = Consensus::Params::ADAM_COLLATERALIZED_TRACK_QUOTA;
static const int ADAM_MASTERNODE_TRACK_QUOTA = Consensus::Params::ADAM_MASTERNODE_TRACK_QUOTA;
static const int ADAM_COIN_LOCK_TRACK_QUOTA = Consensus::Params::ADAM_COIN_LOCK_TRACK_QUOTA;
static const int ADAM_OPEN_POW_TRACK_QUOTA = Consensus::Params::ADAM_OPEN_POW_TRACK_QUOTA;
static const int64_t ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = Consensus::Params::ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS;
static const int64_t ADAM_COMMITTEE_FALLBACK_SLOT_SECS = Consensus::Params::ADAM_COMMITTEE_FALLBACK_SLOT_SECS;
static const int64_t ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = Consensus::Params::ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS;
static const int64_t ADAM_THRESHOLD_RELAXATION_SLOT_SECS = Consensus::Params::ADAM_THRESHOLD_RELAXATION_SLOT_SECS;
static const int ADAM_MIN_RELAXED_THRESHOLD = Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD;
static const int ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP;
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Collateralized sub-track quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD,
              "Masternode track quota must be at least ADAM_MAX_CONSENSUS_THRESHOLD so Masternodes alone can always form a quorum");
static_assert(Consensus::Params::ADAM_MIN_RELAXED_THRESHOLD > ADAM_COIN_LOCK_TRACK_QUOTA + ADAM_OPEN_POW_TRACK_QUOTA,
              "Post-bootstrap relaxed threshold floor must exceed non-Masternode seats so at least 1 Masternode is always required");
static const int ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100;
static const int ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1;
static const int ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100;
static const int ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1;
static const size_t MAX_ZEROCOIN_BOOTSTRAP_MINERS = 128;
static const size_t MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16;
static const int ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240;
static const int ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10;
static const int MASTERNODE_PING_RELAY_COOLDOWN_BLOCKS = 360;

/**
 * Check if ADAM consensus is active at the given height.
 */
inline bool IsAdamActive(int nHeight, const Consensus::Params& params) {
    return params.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ADAM);
}

/**
 * Check if ADAM Dual-Track (8 Collateralized + 3 Open PoW) committee selection
 * and anti-Sybil PoW-Lock rules are active at the given block height.
 * Controlled by Consensus::Params::nAdamDualTrackHeight.
 */
bool IsAdamDualTrackActive(int nHeight);

/**
 * Check if Model D reward split is active at the given height.
 */
bool IsModelDActive(int nHeight);

/**
 * Check if cooperative ADAM validator seat payouts (40% validator pool + unclaimed burn/treasury split)
 * are active at the given height (active throughout ADAM from UPGRADE_HARDENING_V2 onward and in Model D).
 */
bool IsAdamValidatorSplitActive(int nHeight);

/**
 * Generate a deterministic private key from an index.
 */
CKey GetAdamDeterministicKey(int index);

/**
 * Generate a deterministic public key from an index.
 */
CPubKey GetAdamDeterministicPubKey(int index);

/**
 * Get the pool of potential miners/coordinators, optionally outputting the sets of
 * Active Masternode keys (4,200 KRISTA), Coin-Lock keys (250 KRISTA), the count of
 * explicitly registered validators, and the count of funded validators (Active MNs + Coin-Lock + Funded PoW-Lock).
 */
std::vector<CPubKey> GetAdamMinerPool(int nHeight = -1, const CCoinsViewCache* pView = nullptr, std::set<CPubKey>* pMasternodeKeysOut = nullptr, std::set<CPubKey>* pCoinLockKeysOut = nullptr, size_t* pExplicitCountOut = nullptr, size_t* pFundedCountOut = nullptr, std::set<CPubKey>* pFundedPoWKeysOut = nullptr, std::set<CPubKey>* pBootstrapKeysOut = nullptr);

/**
 * Return detailed read-only validator pool entries (pubkey, track, unlock_height) for RPC / UI.
 */
std::vector<CAdamValidatorPoolInfo> GetAdamMinerPoolDetailed(int nHeight = -1);

/**
 * Return the number of explicitly registered validators (Active Masternodes + Coin-Lock +
 * Funded PoW-Lock + Zero-Coin PoW, excluding Block 1..199 Classic PoW coinbase filler keys) at nHeight.
 */
size_t GetExplicitAdamMinerPoolCount(int nHeight = -1, const CCoinsViewCache* pView = nullptr);

/**
 * Return the number of funded validators (Active Masternodes + 250 KRISTA Coin-Lock +
 * 10 KRISTA Funded PoW-Lock, excluding Zero-Coin PoW, Block 1..199 bootstrap, and Regtest synthetic keys) at nHeight.
 */
size_t GetFundedAdamMinerPoolCount(int nHeight = -1, const CCoinsViewCache* pView = nullptr);

/**
 * Return the number of Active Masternodes (Track 1A: 4,200 KRISTA) in the ADAM validator pool at nHeight.
 */
int GetActiveMasternodePoolCount(int nHeight = -1, const CCoinsViewCache* pView = nullptr);

/**
 * Pure helper to elect the 11-validator committee using the 8+3 Dual-Track rule
 * (7 Active Masternode + 1 Coin-Lock + 3 Open PoW) with bidirectional spillover,
 * optionally outputting the ordered standby validator list (remaining MNs -> Coin-Locks -> Open PoW).
 */
void ElectDualTrackValidators(
    const uint256& hashAdamSeed,
    const std::vector<CPubKey>& pool,
    const std::set<CPubKey>& primaryColKeys,
    const CPubKey& coordinatorKey,
    int minCount,
    std::vector<CPubKey>& vSelectedMinersOut,
    const std::set<CPubKey>& secondaryColKeys = std::set<CPubKey>(),
    std::vector<CPubKey>* pvStandbyMinersOut = nullptr,
    const std::set<CPubKey>* pFundedPoWKeys = nullptr,
    const std::set<CPubKey>* pBootstrapKeys = nullptr);

class CBlockIndex;

/**
 * Get the ADAM rolling seed for a block index.
 */
uint256 GetAdamSeed(const CBlockIndex* pindex);

/**
 * Select n miners and 1 coordinator deterministically using the rolling seed,
 * optionally outputting the ordered standby validator list.
 */
bool SelectAdamNodes(
    const uint256& hashAdamSeed,
    const Consensus::Params& params,
    std::vector<CPubKey>& vSelectedMinersOut,
    CPubKey& coordinatorOut,
    int nBlockHeight = -1,
    const CCoinsViewCache* pView = nullptr,
    std::vector<CPubKey>* pvStandbyMinersOut = nullptr);

/**
 * Resolve the effective 11-validator committee and solution vector for a given elapsed block time,
 * substituting unlocked same-track standby validators in sequential rank order into silent primary seats
 * when nBlockTimeDiff >= ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS.
 */
void ResolveAdamCommitteeForTime(
    const uint256& hashPrevBlock,
    const uint256& hashAdamSeed,
    int nHeight,
    int64_t nBlockTimeDiff,
    unsigned int nBits,
    int nVersion,
    const std::vector<CPubKey>& vExpectedMiners,
    const std::vector<CPubKey>& vStandbyMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions,
    std::vector<CPubKey>& vEffectiveMinersOut,
    std::vector<std::vector<unsigned char>>& vEffectiveSolutionsOut,
    int& nValidSolutionsOut,
    const std::set<CPubKey>* pPrimaryColKeys = nullptr,
    const std::set<CPubKey>* pSecondaryColKeys = nullptr,
    const CCoinsViewCache* pView = nullptr);

/**
 * Validate a block's vAdamMiners against the primary committee and unlocked same-track standby validators.
 */
bool ValidateAdamCommitteeForTime(
    const uint256& hashPrevBlock,
    const uint256& hashAdamSeed,
    int nHeight,
    int64_t nBlockTimeDiff,
    unsigned int nBits,
    int nVersion,
    const std::vector<CPubKey>& vBlockMiners,
    const std::vector<std::vector<unsigned char>>& vBlockSolutions,
    const std::vector<CPubKey>& vExpectedMiners,
    const std::vector<CPubKey>& vStandbyMiners,
    const std::set<CPubKey>* pPrimaryColKeys = nullptr,
    const std::set<CPubKey>* pSecondaryColKeys = nullptr,
    const CCoinsViewCache* pView = nullptr);

/**
 * Get the deterministic puzzle hash algorithm index for an elected miner.
 */
int GetAdamPuzzleAlgo(const uint256& hashAdamSeed, const CPubKey& minerKey, bool fFallbackMode = false);

/**
 * Get the name of a puzzle hash algorithm.
 */
std::string GetAdamPuzzleAlgoName(int algoIndex);

/**
 * Get the deterministic 3-permutation algorithm indices for an elected miner based on hashPrevBlock.
 */
void GetAdam3PermutationAlgos(const uint256& hashPrevBlock, const CPubKey& minerKey, int& algo1, int& algo2, int& algo3);

/**
 * Calculate the puzzle hash using the chosen algorithm.
 */
uint256 CalculateAdamPuzzleHash(int algoIndex, const unsigned char* pbegin, const unsigned char* pend);

/**
 * Verify a partial PoW solution submitted by a miner.
 */
bool VerifyAdamSolution(const uint256& hashPrevBlock, const uint256& hashAdamSeed, const CPubKey& minerKey, const std::vector<unsigned char>& vchSolution, unsigned int nBits, int nVersion = 0, int nHeight = -1, int knownMinerIdx = -1);

/**
 * Verify the Coordinator's VRF proof signature.
 */
bool VerifyAdamVRFProof(const uint256& prevSeed, const std::vector<unsigned char>& vchProof, const CPubKey& coordinatorKey);

/**
 * Verify the Coordinator's signature on the block header hash.
 */
bool VerifyAdamCoordinatorSig(const CBlockHeader& block, const CPubKey& coordinatorKey, const uint256* pBlockHash = nullptr);

struct CAdamSolutionMsg {
    uint256 hashPrevBlock;
    CPubKey minerKey;
    std::vector<unsigned char> vchSolution;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(hashPrevBlock);
        READWRITE(minerKey);
        READWRITE(vchSolution);
    }
};

extern RecursiveMutex cs_adam_solutions;
extern std::map<uint256, std::map<CPubKey, std::vector<unsigned char>>> mapAdamSolutionsCache;
extern std::map<uint256, std::vector<CAdamSolutionMsg>> mapOrphanAdamSolutions;

void ProcessOrphanAdamSolutions(const uint256& hash);

uint256 GetMinerPoWLimit(const std::string& networkId, bool fHardeningV2 = false);
uint256 GetZeroCoinPoWLimit(const std::string& networkId, bool fHardeningV2 = false);
int GetZeroCoinRegPeriod(int nHeight);
void BuildMinerPoWRegistrationHeaderPrefix(const uint256& challenge, const CPubKey& pubkey, unsigned char outPrefix76[76]);
uint256 ComputeMinerPoWRegistrationHash(uint32_t nNonce, const uint256& challenge, const CPubKey& pubkey, bool fHardeningV2);
void BuildZeroCoinRegistrationHeaderPrefix(const uint256& challenge, const CPubKey& pubkey, unsigned char outPrefix76[76]);
uint256 ComputeZeroCoinRegistrationHash(uint32_t nNonce, const uint256& challenge, const CPubKey& pubkey, bool fHardeningV2);

// Proof of BLS (PoBLS) Ticket & XOR Metric Distance Lottery functions
uint256 ComputePoBLSTicket(const CBLSPubKey& blsPubKey, const CBLSSignature& blsSig, int nBlockHeight);
bool ExtractPoBLSTicketFromSolution(const std::vector<unsigned char>& vchSolution, int nBlockHeight, uint256& ticketHashOut, const CPubKey* pMinerKey = nullptr);
uint256 ComputePoBLSTarget(const uint256& hashAdamSeed, int nBlockHeight);
uint256 ComputePoBLSXORDistance(const uint256& ticketHash, const uint256& targetHash);
std::vector<CPubKey> RankValidatorsByPoBLSDistance(
    const uint256& hashAdamSeed,
    int nBlockHeight,
    const std::vector<CPubKey>& vMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions);

bool GetAdamFallbackCoordinatorForSlot(
    const uint256& hashAdamSeed,
    int nBlockHeight,
    int64_t nTimeElapsed,
    const std::vector<CPubKey>& vMiners,
    const std::map<CPubKey, std::vector<unsigned char>>& mapSolutions,
    CPubKey& activeFallbackOut,
    CPubKey& prevFallbackOut,
    int& rotationIndexOut);

// Decentralized Cooperative PoS Coordinator Co-Signing state & helpers
struct CPoSCoordSigEntry {
    CPubKey coordinatorPubKey;
    std::vector<unsigned char> vAdamVRFProof;
    std::vector<unsigned char> vAdamCoordinatorSig;
};

struct CSeenPoSCoordReqEntry {
    CPubKey targetCoordinator;
    CBlockHeader draftHeader;
    uint256 adamSeed;
    int64_t nTime = 0;
};

extern RecursiveMutex cs_pos_coord_sigs;
extern std::map<uint256, CPoSCoordSigEntry> mapPoSCoordSigs;
extern std::map<uint256, CSeenPoSCoordReqEntry> mapSeenPoSCoordReqs;

bool RecordSeenPoSCoordReq(const uint256& reqHash, const CPubKey& targetCoordinator, const CBlockHeader& draftHeader, const uint256& adamSeed);
uint256 ComputePoSCoordRequestHash(const CBlock& block, const CPubKey& targetCoordinator);
bool SignPoSCoordRequest(const CBlock& block, const uint256& reqHash, const CKeyStore& keystore, std::vector<unsigned char>& vchSigOut);
bool VerifyPoSCoordRequestAuth(const CBlock& block, const uint256& reqHash, const std::vector<unsigned char>& vchSig, bool enableP2PKH);

#endif // BITCOIN_ADAM_H
