// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTATECH_EVO_DETERMINISTICMNS_H
#define KRISTATECH_EVO_DETERMINISTICMNS_H

#include "amount.h"
#include "crypto/bls.h"
#include "dbwrapper.h"
#include "netaddress.h"
#include "primitives/transaction.h"
#include "pubkey.h"
#include "script/script.h"
#include "serialize.h"
#include "sync.h"
#include "uint256.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class CValidationState;

namespace dip3 {

static const char PROTX_MAGIC[6] = {'K', 'T', 'D', 'I', 'P', '3'};
static const int PROTX_MIN_COLLATERAL_CONFS = 15;
static const int POSE_MAX_PENALTY = 100;

enum ProTxType : uint8_t {
    PROTX_NONE = 0,
    PROTX_REG = 1,
    PROTX_UPSERV = 2,
    PROTX_UPREG = 3,
    PROTX_UPREV = 4,
};

uint256 CalcProTxInputsHash(const CTransaction& tx);

class CProRegTx {
public:
    uint16_t nVersion{1};
    COutPoint collateralOutpoint;
    CService addr;
    CPubKey pubKeyOwner;
    CBLSPubKey blsPubKeyOperator;
    CBLSSignature blsPoP;
    CKeyID keyIDVoting;
    uint16_t nOperatorReward{0};
    CScript scriptPayout;
    uint256 inputsHash;
    std::vector<unsigned char> vchSig;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(collateralOutpoint);
        READWRITE(addr);
        READWRITE(pubKeyOwner);
        READWRITE(blsPubKeyOperator);
        if (nVersion >= 2) {
            READWRITE(blsPoP);
        }
        READWRITE(keyIDVoting);
        READWRITE(nOperatorReward);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(inputsHash);
        READWRITE(vchSig);
    }

    uint256 GetSignHash() const;
    std::string ToString() const;
};

class CProUpServTx {
public:
    uint16_t nVersion{1};
    uint256 proTxHash;
    CService addr;
    CScript scriptOperatorPayout;
    uint256 inputsHash;
    CBLSSignature blsSig;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(addr);
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
        READWRITE(inputsHash);
        READWRITE(blsSig);
    }

    uint256 GetSignHash() const;
    std::string ToString() const;
};

class CProUpRegTx {
public:
    uint16_t nVersion{1};
    uint256 proTxHash;
    CBLSPubKey blsPubKeyOperator;
    CBLSSignature blsPoP;
    CKeyID keyIDVoting;
    CScript scriptPayout;
    uint256 inputsHash;
    std::vector<unsigned char> vchSig;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(blsPubKeyOperator);
        if (nVersion >= 2) {
            READWRITE(blsPoP);
        }
        READWRITE(keyIDVoting);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(inputsHash);
        READWRITE(vchSig);
    }

    uint256 GetSignHash() const;
    std::string ToString() const;
};

class CProUpRevTx {
public:
    uint16_t nVersion{1};
    uint256 proTxHash;
    uint16_t nReason{0};
    uint256 inputsHash;
    CBLSSignature blsSig;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(proTxHash);
        READWRITE(nReason);
        READWRITE(inputsHash);
        READWRITE(blsSig);
    }

    uint256 GetSignHash() const;
    std::string ToString() const;
};

/** Extract KTDIP3 OP_RETURN payload from a transaction if present. Returns PROTX_NONE if none, or PROTX_NONE with fMalformed=true if invalid encoding. */
ProTxType ExtractProTxPayload(const CTransaction& tx, std::vector<unsigned char>& vchPayloadOut, bool& fMalformed);

/** Build an OP_RETURN scriptPubKey carrying a KTDIP3 special transaction payload. */
template <typename T>
CScript BuildProTxScript(ProTxType type, const T& payload) {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss.write(PROTX_MAGIC, sizeof(PROTX_MAGIC));
    uint8_t t = static_cast<uint8_t>(type);
    ss << t;
    ss << payload;
    std::vector<unsigned char> vch(ss.begin(), ss.end());
    return CScript() << OP_RETURN << vch;
}

class CDeterministicMNState {
public:
    int nRegisteredHeight{-1};
    int nLastPaidHeight{0};
    int nPoSePenalty{0};
    int nPoSeRevivedHeight{-1};
    int nPoSeBanHeight{-1};
    uint16_t nRevocationReason{0};

    CService addr;
    CPubKey pubKeyOwner;
    CBLSPubKey blsPubKeyOperator;
    CKeyID keyIDVoting;
    uint16_t nOperatorReward{0};
    CScript scriptPayout;
    CScript scriptOperatorPayout;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nRegisteredHeight);
        READWRITE(nLastPaidHeight);
        READWRITE(nPoSePenalty);
        READWRITE(nPoSeRevivedHeight);
        READWRITE(nPoSeBanHeight);
        READWRITE(nRevocationReason);
        READWRITE(addr);
        READWRITE(pubKeyOwner);
        READWRITE(blsPubKeyOperator);
        READWRITE(keyIDVoting);
        READWRITE(nOperatorReward);
        READWRITE(*(CScriptBase*)(&scriptPayout));
        READWRITE(*(CScriptBase*)(&scriptOperatorPayout));
    }

    bool IsBanned() const { return nPoSeBanHeight != -1; }
    bool IsRevoked() const { return nRevocationReason != 0 || !blsPubKeyOperator.IsValid(); }
    bool IsValid() const { return !IsBanned() && !IsRevoked() && pubKeyOwner.IsValid(); }
};

class CDeterministicMN {
public:
    uint256 proTxHash;
    uint64_t internalId{0};
    COutPoint collateralOutpoint;
    CDeterministicMNState state;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(proTxHash);
        READWRITE(internalId);
        READWRITE(collateralOutpoint);
        READWRITE(state);
    }
};

using CDeterministicMNCPtr = std::shared_ptr<const CDeterministicMN>;

class CDeterministicMNList {
public:
    uint256 blockHash;
    int nHeight{-1};
    uint64_t nTotalRegisteredCount{0};
    std::map<uint256, CDeterministicMN> mapMNs;
    std::map<COutPoint, uint256> mapCollateralToProTx;
    std::map<CBLSPubKey, uint256> mapOperatorToProTx;
    std::map<CPubKey, uint256> mapOwnerToProTx;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(nHeight);
        READWRITE(nTotalRegisteredCount);
        READWRITE(mapMNs);
        if (ser_action.ForRead()) {
            RebuildIndices();
        }
    }

    void RebuildIndices();
    bool HasMN(const uint256& proTxHash) const { return mapMNs.count(proTxHash) > 0; }
    bool HasCollateral(const COutPoint& outpoint) const { return mapCollateralToProTx.count(outpoint) > 0; }
    bool HasOperator(const CBLSPubKey& pk) const { return mapOperatorToProTx.count(pk) > 0; }
    bool HasOwner(const CPubKey& pk) const { return mapOwnerToProTx.count(pk) > 0; }

    const CDeterministicMN* GetMN(const uint256& proTxHash) const;
    const CDeterministicMN* GetMNByCollateral(const COutPoint& outpoint) const;
    const CDeterministicMN* GetMNByOperator(const CBLSPubKey& pk) const;
    const CDeterministicMN* GetMNByOwner(const CPubKey& pk) const;

    void AddMN(const CDeterministicMN& mn);
    void UpdateMN(const uint256& proTxHash, const CDeterministicMNState& newState);
    void RemoveMN(const uint256& proTxHash);

    size_t GetAllMNsCount() const { return mapMNs.size(); }
    size_t GetValidMNsCount() const;
    std::vector<CDeterministicMN> GetValidMNs() const;
};

class CDeterministicMNManager {
private:
    mutable RecursiveMutex cs;
    std::unique_ptr<CDBWrapper> db;
    mutable std::map<uint256, CDeterministicMNList> mapListCache;
    uint256 tipBlockHash;

    bool ReadListFromDB(const uint256& blockHash, CDeterministicMNList& listOut) const;
    bool WriteListToDB(const uint256& blockHash, const CDeterministicMNList& list, bool fSync, const uint256& pruneBlockHash = uint256());

public:
    CDeterministicMNManager(size_t nCacheSize = 4 << 20, bool fMemory = false, bool fWipe = false);

    bool HasListForBlock(const uint256& blockHash) const;
    uint256 GetTipBlockHash() const;
    bool SetTipBlockHash(const uint256& blockHash);
    bool CheckSpecialTx(const CTransaction& tx, const CBlockIndex* pindexPrev, CValidationState& state, const CCoinsViewCache& view, const CDeterministicMNList* pOverrideList = nullptr) const;
    bool ApplySpecialTxToList(const CTransaction& tx, int nHeight, CDeterministicMNList& mnList, std::set<COutPoint>* pNewCollateralsOut = nullptr) const;
    bool ProcessBlock(const CBlock& block, const CBlockIndex* pindex, CValidationState& state, const CCoinsViewCache& view, bool fJustCheck, CDeterministicMNList* pPreparedListOut = nullptr, uint256* pPruneHashOut = nullptr);
    bool CommitBlockList(const uint256& blockHash, const CDeterministicMNList& list, const uint256& pruneHash);
    bool UndoBlock(const CBlock& block, const CBlockIndex* pindex);

    CDeterministicMNList GetListForBlock(const CBlockIndex* pindex) const;
    CDeterministicMNList GetListAtChainTip() const;

    /** Apply PoSe penalty to a Masternode at current tip list (used when a committee member misses its duty). */
    void ApplyPoSePunishment(CDeterministicMNList& list, const uint256& proTxHash, int nPenalty, int nHeight) const;
};

} // namespace dip3

extern std::unique_ptr<dip3::CDeterministicMNManager> deterministicMNManager;

bool RegisterDip3MasternodeFromWallet(const COutPoint& colOut,
                                      const CService& addr,
                                      const CBLSPubKey& opPubKey,
                                      std::string& strTxHashOut,
                                      std::string& strErrOut,
                                      uint16_t nOperatorRewardBps = 0,
                                      const std::string& strPayoutAddress = "",
                                      const CBLSSignature* pBlsPoP = nullptr);

#endif // KRISTATECH_EVO_DETERMINISTICMNS_H
