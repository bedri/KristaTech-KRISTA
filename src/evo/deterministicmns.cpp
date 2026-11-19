// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/deterministicmns.h"

#include "chain.h"
#include "coins.h"
#include "consensus/validation.h"
#include "hash.h"
#include "main.h"
#include "masternode.h"
#include "primitives/block.h"
#include "script/standard.h"
#include "streams.h"
#include "util.h"
#include "utilstrencodings.h"

#include <cstring>

std::unique_ptr<dip3::CDeterministicMNManager> deterministicMNManager;

namespace dip3 {

uint256 CalcProTxInputsHash(const CTransaction& tx) {
    CHashWriter hw(SER_GETHASH, 0);
    for (const auto& txin : tx.vin) {
        hw << txin.prevout;
    }
    return hw.GetHash();
}

uint256 CProRegTx::GetSignHash() const {
    CHashWriter hw(SER_GETHASH, 0);
    hw << nVersion << collateralOutpoint << addr << pubKeyOwner << blsPubKeyOperator
       << keyIDVoting << nOperatorReward << scriptPayout << inputsHash;
    return hw.GetHash();
}

std::string CProRegTx::ToString() const {
    return strprintf("CProRegTx(nVersion=%d, collateral=%s, addr=%s, owner=%s, nOperatorReward=%d)",
                     nVersion, collateralOutpoint.ToString(), addr.ToString(), pubKeyOwner.GetID().ToString(), nOperatorReward);
}

uint256 CProUpServTx::GetSignHash() const {
    CHashWriter hw(SER_GETHASH, 0);
    hw << nVersion << proTxHash << addr << scriptOperatorPayout << inputsHash;
    return hw.GetHash();
}

std::string CProUpServTx::ToString() const {
    return strprintf("CProUpServTx(nVersion=%d, proTxHash=%s, addr=%s)",
                     nVersion, proTxHash.ToString(), addr.ToString());
}

uint256 CProUpRegTx::GetSignHash() const {
    CHashWriter hw(SER_GETHASH, 0);
    hw << nVersion << proTxHash << blsPubKeyOperator << keyIDVoting << scriptPayout << inputsHash;
    return hw.GetHash();
}

std::string CProUpRegTx::ToString() const {
    return strprintf("CProUpRegTx(nVersion=%d, proTxHash=%s, keyIDVoting=%s)",
                     nVersion, proTxHash.ToString(), keyIDVoting.ToString());
}

uint256 CProUpRevTx::GetSignHash() const {
    CHashWriter hw(SER_GETHASH, 0);
    hw << nVersion << proTxHash << nReason << inputsHash;
    return hw.GetHash();
}

std::string CProUpRevTx::ToString() const {
    return strprintf("CProUpRevTx(nVersion=%d, proTxHash=%s, nReason=%d)",
                     nVersion, proTxHash.ToString(), nReason);
}

ProTxType ExtractProTxPayload(const CTransaction& tx, std::vector<unsigned char>& vchPayloadOut, bool& fMalformed) {
    fMalformed = false;
    vchPayloadOut.clear();
    ProTxType foundType = PROTX_NONE;
    int nFoundCount = 0;

    for (const auto& txout : tx.vout) {
        const CScript& script = txout.scriptPubKey;
        if (script.empty() || script[0] != OP_RETURN) {
            continue;
        }
        CScript::const_iterator pc = script.begin() + 1;
        opcodetype opcode;
        std::vector<unsigned char> vchData;
        if (!script.GetOp(pc, opcode, vchData)) {
            continue;
        }
        if (vchData.size() < sizeof(PROTX_MAGIC) + 1) {
            continue;
        }
        if (std::memcmp(vchData.data(), PROTX_MAGIC, sizeof(PROTX_MAGIC)) != 0) {
            continue;
        }
        if (txout.nValue != 0) {
            fMalformed = true;
            return PROTX_NONE;
        }
        uint8_t rawType = vchData[sizeof(PROTX_MAGIC)];
        if (rawType < PROTX_REG || rawType > PROTX_UPREV) {
            fMalformed = true;
            return PROTX_NONE;
        }
        ++nFoundCount;
        if (nFoundCount > 1) {
            fMalformed = true;
            return PROTX_NONE;
        }
        foundType = static_cast<ProTxType>(rawType);
        vchPayloadOut.assign(vchData.begin() + sizeof(PROTX_MAGIC) + 1, vchData.end());
    }
    return foundType;
}

void CDeterministicMNList::RebuildIndices() {
    mapCollateralToProTx.clear();
    mapOperatorToProTx.clear();
    mapOwnerToProTx.clear();
    for (const auto& kv : mapMNs) {
        const CDeterministicMN& mn = kv.second;
        mapCollateralToProTx[mn.collateralOutpoint] = mn.proTxHash;
        mapOwnerToProTx[mn.state.pubKeyOwner] = mn.proTxHash;
        if (mn.state.blsPubKeyOperator.IsValid()) {
            mapOperatorToProTx[mn.state.blsPubKeyOperator] = mn.proTxHash;
        }
    }
}

const CDeterministicMN* CDeterministicMNList::GetMN(const uint256& proTxHash) const {
    auto it = mapMNs.find(proTxHash);
    return (it != mapMNs.end()) ? &it->second : nullptr;
}

const CDeterministicMN* CDeterministicMNList::GetMNByCollateral(const COutPoint& outpoint) const {
    auto it = mapCollateralToProTx.find(outpoint);
    return (it != mapCollateralToProTx.end()) ? GetMN(it->second) : nullptr;
}

const CDeterministicMN* CDeterministicMNList::GetMNByOperator(const CBLSPubKey& pk) const {
    auto it = mapOperatorToProTx.find(pk);
    return (it != mapOperatorToProTx.end()) ? GetMN(it->second) : nullptr;
}

const CDeterministicMN* CDeterministicMNList::GetMNByOwner(const CPubKey& pk) const {
    auto it = mapOwnerToProTx.find(pk);
    return (it != mapOwnerToProTx.end()) ? GetMN(it->second) : nullptr;
}

void CDeterministicMNList::AddMN(const CDeterministicMN& mn) {
    CDeterministicMN copy = mn;
    if (copy.internalId == 0) {
        copy.internalId = ++nTotalRegisteredCount;
    }
    mapMNs[copy.proTxHash] = copy;
    mapCollateralToProTx[copy.collateralOutpoint] = copy.proTxHash;
    mapOwnerToProTx[copy.state.pubKeyOwner] = copy.proTxHash;
    if (copy.state.blsPubKeyOperator.IsValid()) {
        mapOperatorToProTx[copy.state.blsPubKeyOperator] = copy.proTxHash;
    }
}

void CDeterministicMNList::UpdateMN(const uint256& proTxHash, const CDeterministicMNState& newState) {
    auto it = mapMNs.find(proTxHash);
    if (it == mapMNs.end()) {
        return;
    }
    const CDeterministicMNState& oldState = it->second.state;
    if (oldState.blsPubKeyOperator != newState.blsPubKeyOperator) {
        if (oldState.blsPubKeyOperator.IsValid()) {
            mapOperatorToProTx.erase(oldState.blsPubKeyOperator);
        }
        if (newState.blsPubKeyOperator.IsValid()) {
            mapOperatorToProTx[newState.blsPubKeyOperator] = proTxHash;
        }
    }
    if (oldState.pubKeyOwner != newState.pubKeyOwner) {
        mapOwnerToProTx.erase(oldState.pubKeyOwner);
        mapOwnerToProTx[newState.pubKeyOwner] = proTxHash;
    }
    it->second.state = newState;
}

void CDeterministicMNList::RemoveMN(const uint256& proTxHash) {
    auto it = mapMNs.find(proTxHash);
    if (it == mapMNs.end()) {
        return;
    }
    mapCollateralToProTx.erase(it->second.collateralOutpoint);
    mapOwnerToProTx.erase(it->second.state.pubKeyOwner);
    if (it->second.state.blsPubKeyOperator.IsValid()) {
        mapOperatorToProTx.erase(it->second.state.blsPubKeyOperator);
    }
    mapMNs.erase(it);
}

size_t CDeterministicMNList::GetValidMNsCount() const {
    size_t count = 0;
    for (const auto& kv : mapMNs) {
        if (kv.second.state.IsValid()) {
            ++count;
        }
    }
    return count;
}

std::vector<CDeterministicMN> CDeterministicMNList::GetValidMNs() const {
    std::vector<CDeterministicMN> result;
    result.reserve(mapMNs.size());
    for (const auto& kv : mapMNs) {
        if (kv.second.state.IsValid()) {
            result.push_back(kv.second);
        }
    }
    return result;
}

CDeterministicMNManager::CDeterministicMNManager(size_t nCacheSize, bool fMemory, bool fWipe) {
    db.reset(new CDBWrapper(GetDataDir() / "evodb", nCacheSize, fMemory, fWipe));
    db->Read('T', tipBlockHash);
}

bool CDeterministicMNManager::ReadListFromDB(const uint256& blockHash, CDeterministicMNList& listOut) const {
    if (blockHash.IsNull()) {
        return false;
    }
    auto it = mapListCache.find(blockHash);
    if (it != mapListCache.end()) {
        listOut = it->second;
        return true;
    }
    if (!db) {
        return false;
    }
    if (db->Read(std::make_pair('L', blockHash), listOut)) {
        if (mapListCache.size() > 64) {
            mapListCache.clear();
        }
        mapListCache[blockHash] = listOut;
        return true;
    }
    return false;
}

bool CDeterministicMNManager::HasListForBlock(const uint256& blockHash) const {
    if (blockHash.IsNull()) {
        return false;
    }
    LOCK(cs);
    CDeterministicMNList tmp;
    return ReadListFromDB(blockHash, tmp);
}

bool CDeterministicMNManager::WriteListToDB(const uint256& blockHash, const CDeterministicMNList& list) {
    if (mapListCache.size() > 64) {
        mapListCache.clear();
    }
    mapListCache[blockHash] = list;
    tipBlockHash = blockHash;
    if (!db) {
        return true;
    }
    CDBBatch batch;
    batch.Write(std::make_pair('L', blockHash), list);
    batch.Write('T', blockHash);
    const bool fSync = !IsInitialBlockDownload();
    return db->WriteBatch(batch, fSync);
}

CDeterministicMNList CDeterministicMNManager::GetListForBlock(const CBlockIndex* pindex) const {
    LOCK(cs);
    CDeterministicMNList list;
    if (!pindex) {
        return list;
    }
    if (ReadListFromDB(pindex->GetBlockHash(), list)) {
        return list;
    }
    return list;
}

CDeterministicMNList CDeterministicMNManager::GetListAtChainTip() const {
    LOCK(cs);
    CDeterministicMNList list;
    if (!tipBlockHash.IsNull() && ReadListFromDB(tipBlockHash, list)) {
        return list;
    }
    return list;
}

void CDeterministicMNManager::ApplyPoSePunishment(CDeterministicMNList& list, const uint256& proTxHash, int nPenalty, int nHeight) const {
    const CDeterministicMN* pmn = list.GetMN(proTxHash);
    if (!pmn || pmn->state.IsBanned()) {
        return;
    }
    CDeterministicMNState newState = pmn->state;
    newState.nPoSePenalty += nPenalty;
    if (newState.nPoSePenalty >= POSE_MAX_PENALTY) {
        newState.nPoSePenalty = POSE_MAX_PENALTY;
        newState.nPoSeBanHeight = nHeight;
    }
    list.UpdateMN(proTxHash, newState);
}

static bool CheckProRegTx(const CTransaction& tx,
                          const std::vector<unsigned char>& vchPayload,
                          const CBlockIndex* pindexPrev,
                          CValidationState& state,
                          const CCoinsViewCache& view,
                          const CDeterministicMNList& prevList,
                          CProRegTx& ptxOut,
                          COutPoint& resolvedCollateralOut) {
    try {
        CDataStream ss(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
        ss >> ptxOut;
        if (!ss.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload-trailing");
        }
    } catch (const std::exception&) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-payload-deserialize");
    }

    if (ptxOut.nVersion != 1) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-version");
    }
    if (ptxOut.inputsHash != CalcProTxInputsHash(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-inputs-hash");
    }
    if (!ptxOut.addr.IsValid() || ptxOut.addr.GetPort() == 0) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-addr");
    }
    if (!ptxOut.pubKeyOwner.IsFullyValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-owner-key");
    }
    if (!ptxOut.blsPubKeyOperator.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-key");
    }
    if (ptxOut.nOperatorReward > 10000) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-reward");
    }
    CTxDestination payoutDest;
    if (ptxOut.scriptPayout.empty() || !ExtractDestination(ptxOut.scriptPayout, payoutDest)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payout-script");
    }
    if (!ptxOut.pubKeyOwner.Verify(ptxOut.GetSignHash(), ptxOut.vchSig)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig");
    }

    const int nNextHeight = pindexPrev ? (pindexPrev->nHeight + 1) : 1;
    const CAmount nExpectedCollateral = CMasternode::GetMasternodeNodeCollateral(nNextHeight);
    const bool fDip3V2 = Params().GetConsensus().IsDip3V2Active(nNextHeight);
    const CScript expectedOwnerScript = GetScriptForDestination(ptxOut.pubKeyOwner.GetID());
    resolvedCollateralOut = ptxOut.collateralOutpoint;

    if (resolvedCollateralOut.hash.IsNull()) {
        // Collateral created inside this same transaction
        if (resolvedCollateralOut.n >= tx.vout.size()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-index");
        }
        if (tx.vout[resolvedCollateralOut.n].nValue != nExpectedCollateral) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-amount");
        }
        if (fDip3V2 && (tx.vout[resolvedCollateralOut.n].scriptPubKey.IsUnspendable() ||
                        tx.vout[resolvedCollateralOut.n].scriptPubKey != expectedOwnerScript)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-owner");
        }
        resolvedCollateralOut.hash = tx.GetHash();
    } else {
        Coin coin;
        if (!view.GetCoin(resolvedCollateralOut, coin) || coin.IsSpent()) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-missing");
        }
        if (coin.out.nValue != nExpectedCollateral) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-amount");
        }
        if (fDip3V2 && coin.out.scriptPubKey != expectedOwnerScript) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-owner");
        }
        const int nConfs = pindexPrev ? (pindexPrev->nHeight - (int)coin.nHeight + 1) : 0;
        if (nConfs < PROTX_MIN_COLLATERAL_CONFS) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-confs");
        }
    }

    // Rule 8: Do NOT enforce unique IP/addr; enforce unique proTxHash, collateral, owner, operator
    if (prevList.HasMN(tx.GetHash())) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-dup-hash");
    }
    if (prevList.HasCollateral(resolvedCollateralOut)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-dup-collateral");
    }
    if (prevList.HasOwner(ptxOut.pubKeyOwner)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-dup-owner");
    }
    if (prevList.HasOperator(ptxOut.blsPubKeyOperator)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-dup-operator");
    }
    return true;
}

static bool CheckProUpServTx(const CTransaction& tx,
                             const std::vector<unsigned char>& vchPayload,
                             const CBlockIndex* pindexPrev,
                             CValidationState& state,
                             const CDeterministicMNList& prevList,
                             CProUpServTx& ptxOut) {
    try {
        CDataStream ss(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
        ss >> ptxOut;
        if (!ss.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-payload-trailing");
        }
    } catch (const std::exception&) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-payload-deserialize");
    }

    if (ptxOut.nVersion != 1) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-version");
    }
    if (ptxOut.inputsHash != CalcProTxInputsHash(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-inputs-hash");
    }
    if (!ptxOut.addr.IsValid() || ptxOut.addr.GetPort() == 0) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupserv-addr");
    }
    const int nNextHeight = pindexPrev ? (pindexPrev->nHeight + 1) : 1;
    if (Params().GetConsensus().IsDip3V2Active(nNextHeight) && !ptxOut.scriptOperatorPayout.empty()) {
        CTxDestination opDest;
        if (!ExtractDestination(ptxOut.scriptOperatorPayout, opDest)) {
            return state.DoS(10, false, REJECT_INVALID, "bad-proupserv-operator-payout");
        }
    }
    const CDeterministicMN* pmn = prevList.GetMN(ptxOut.proTxHash);
    if (!pmn) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupserv-missing-mn");
    }
    if (!pmn->state.blsPubKeyOperator.IsValid()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupserv-revoked-mn");
    }
    if (!ptxOut.blsSig.Verify(pmn->state.blsPubKeyOperator, ptxOut.GetSignHash())) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-sig");
    }
    return true;
}

static bool CheckProUpRegTx(const CTransaction& tx,
                            const std::vector<unsigned char>& vchPayload,
                            CValidationState& state,
                            const CDeterministicMNList& prevList,
                            CProUpRegTx& ptxOut) {
    try {
        CDataStream ss(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
        ss >> ptxOut;
        if (!ss.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-payload-trailing");
        }
    } catch (const std::exception&) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-payload-deserialize");
    }

    if (ptxOut.nVersion != 1) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-version");
    }
    if (ptxOut.inputsHash != CalcProTxInputsHash(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-inputs-hash");
    }
    if (!ptxOut.blsPubKeyOperator.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-operator-key");
    }
    const CDeterministicMN* pmn = prevList.GetMN(ptxOut.proTxHash);
    if (!pmn) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-missing-mn");
    }
    if (ptxOut.blsPubKeyOperator != pmn->state.blsPubKeyOperator && prevList.HasOperator(ptxOut.blsPubKeyOperator)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-dup-operator");
    }
    if (!pmn->state.pubKeyOwner.Verify(ptxOut.GetSignHash(), ptxOut.vchSig)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-sig");
    }
    return true;
}

static bool CheckProUpRevTx(const CTransaction& tx,
                            const std::vector<unsigned char>& vchPayload,
                            CValidationState& state,
                            const CDeterministicMNList& prevList,
                            CProUpRevTx& ptxOut) {
    try {
        CDataStream ss(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
        ss >> ptxOut;
        if (!ss.empty()) {
            return state.DoS(100, false, REJECT_INVALID, "bad-prouprev-payload-trailing");
        }
    } catch (const std::exception&) {
        return state.DoS(100, false, REJECT_INVALID, "bad-prouprev-payload-deserialize");
    }

    if (ptxOut.nVersion != 1 || ptxOut.nReason == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-prouprev-version-reason");
    }
    if (ptxOut.inputsHash != CalcProTxInputsHash(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-prouprev-inputs-hash");
    }
    const CDeterministicMN* pmn = prevList.GetMN(ptxOut.proTxHash);
    if (!pmn) {
        return state.DoS(10, false, REJECT_INVALID, "bad-prouprev-missing-mn");
    }
    if (!pmn->state.blsPubKeyOperator.IsValid()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-prouprev-already-revoked");
    }
    if (!ptxOut.blsSig.Verify(pmn->state.blsPubKeyOperator, ptxOut.GetSignHash())) {
        return state.DoS(100, false, REJECT_INVALID, "bad-prouprev-sig");
    }
    return true;
}

bool CDeterministicMNManager::CheckSpecialTx(const CTransaction& tx,
                                             const CBlockIndex* pindexPrev,
                                             CValidationState& state,
                                             const CCoinsViewCache& view,
                                             const CDeterministicMNList* pOverrideList) const {
    std::vector<unsigned char> vchPayload;
    bool fMalformed = false;
    ProTxType type = ExtractProTxPayload(tx, vchPayload, fMalformed);
    if (fMalformed) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-malformed");
    }
    if (type == PROTX_NONE) {
        return true;
    }

    LOCK(cs);
    CDeterministicMNList prevList;
    if (pOverrideList) {
        prevList = *pOverrideList;
    } else if (pindexPrev) {
        ReadListFromDB(pindexPrev->GetBlockHash(), prevList);
    }

    switch (type) {
    case PROTX_REG: {
        CProRegTx ptx;
        COutPoint colOut;
        return CheckProRegTx(tx, vchPayload, pindexPrev, state, view, prevList, ptx, colOut);
    }
    case PROTX_UPSERV: {
        CProUpServTx ptx;
        return CheckProUpServTx(tx, vchPayload, pindexPrev, state, prevList, ptx);
    }
    case PROTX_UPREG: {
        CProUpRegTx ptx;
        return CheckProUpRegTx(tx, vchPayload, state, prevList, ptx);
    }
    case PROTX_UPREV: {
        CProUpRevTx ptx;
        return CheckProUpRevTx(tx, vchPayload, state, prevList, ptx);
    }
    default:
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }
}

bool CDeterministicMNManager::ProcessBlock(const CBlock& block,
                                           const CBlockIndex* pindex,
                                           CValidationState& state,
                                           const CCoinsViewCache& view,
                                           bool fJustCheck) {
    if (!pindex) {
        return true;
    }

    LOCK(cs);
    CDeterministicMNList newList;
    if (pindex->pprev) {
        ReadListFromDB(pindex->pprev->GetBlockHash(), newList);
    }
    newList.blockHash = pindex->GetBlockHash();
    newList.nHeight = pindex->nHeight;

    // 1. Collect all spent outpoints and remove any existing MN whose collateral UTXO was spent in this block
    std::set<COutPoint> spentOutpointsInBlock;
    for (const auto& tx : block.vtx) {
        if (tx.IsCoinBase()) {
            continue;
        }
        for (const auto& txin : tx.vin) {
            spentOutpointsInBlock.insert(txin.prevout);
            const CDeterministicMN* pmn = newList.GetMNByCollateral(txin.prevout);
            if (pmn) {
                newList.RemoveMN(pmn->proTxHash);
            }
        }
    }

    // 2. Validate and apply all ProTx special transactions in block order
    const bool fDip3V2Block = Params().GetConsensus().IsDip3V2Active(pindex->nHeight);
    for (const auto& tx : block.vtx) {
        std::vector<unsigned char> vchPayload;
        bool fMalformed = false;
        ProTxType type = ExtractProTxPayload(tx, vchPayload, fMalformed);
        if (fMalformed) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-malformed");
        }
        if (type == PROTX_NONE) {
            continue;
        }
        if (type == PROTX_REG) {
            CProRegTx ptx;
            COutPoint resolvedCol;
            if (!CheckProRegTx(tx, vchPayload, pindex->pprev, state, view, newList, ptx, resolvedCol)) {
                return false;
            }
            if (fDip3V2Block && spentOutpointsInBlock.count(resolvedCol) > 0) {
                return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-spent-in-block");
            }
            CDeterministicMN mn;
            mn.proTxHash = tx.GetHash();
            mn.collateralOutpoint = resolvedCol;
            mn.state.nRegisteredHeight = pindex->nHeight;
            mn.state.addr = ptx.addr;
            mn.state.pubKeyOwner = ptx.pubKeyOwner;
            mn.state.blsPubKeyOperator = ptx.blsPubKeyOperator;
            mn.state.keyIDVoting = ptx.keyIDVoting;
            mn.state.nOperatorReward = ptx.nOperatorReward;
            mn.state.scriptPayout = ptx.scriptPayout;
            newList.AddMN(mn);
        } else if (type == PROTX_UPSERV) {
            CProUpServTx ptx;
            if (!CheckProUpServTx(tx, vchPayload, pindex->pprev, state, newList, ptx)) {
                return false;
            }
            const CDeterministicMN* pmn = newList.GetMN(ptx.proTxHash);
            CDeterministicMNState st = pmn->state;
            st.addr = ptx.addr;
            st.scriptOperatorPayout = ptx.scriptOperatorPayout;
            if (st.IsBanned()) {
                st.nPoSePenalty = 0;
                st.nPoSeBanHeight = -1;
                st.nPoSeRevivedHeight = pindex->nHeight;
            }
            newList.UpdateMN(ptx.proTxHash, st);
        } else if (type == PROTX_UPREG) {
            CProUpRegTx ptx;
            if (!CheckProUpRegTx(tx, vchPayload, state, newList, ptx)) {
                return false;
            }
            const CDeterministicMN* pmn = newList.GetMN(ptx.proTxHash);
            CDeterministicMNState st = pmn->state;
            st.blsPubKeyOperator = ptx.blsPubKeyOperator;
            st.keyIDVoting = ptx.keyIDVoting;
            st.scriptPayout = ptx.scriptPayout;
            st.nRevocationReason = 0;
            newList.UpdateMN(ptx.proTxHash, st);
        } else if (type == PROTX_UPREV) {
            CProUpRevTx ptx;
            if (!CheckProUpRevTx(tx, vchPayload, state, newList, ptx)) {
                return false;
            }
            const CDeterministicMN* pmn = newList.GetMN(ptx.proTxHash);
            CDeterministicMNState st = pmn->state;
            st.blsPubKeyOperator.Reset();
            st.nRevocationReason = ptx.nReason;
            newList.UpdateMN(ptx.proTxHash, st);
        }
    }

    if (!fJustCheck) {
        if (!WriteListToDB(pindex->GetBlockHash(), newList)) {
            return state.Error("Failed to write deterministic MN list to evodb");
        }
    }
    return true;
}

bool CDeterministicMNManager::UndoBlock(const CBlock& block, const CBlockIndex* pindex) {
    if (!pindex) {
        return true;
    }
    LOCK(cs);
    const uint256 blockHash = pindex->GetBlockHash();
    mapListCache.erase(blockHash);
    if (pindex->pprev) {
        tipBlockHash = pindex->pprev->GetBlockHash();
    } else {
        tipBlockHash.SetNull();
    }
    if (db) {
        CDBBatch batch;
        batch.Erase(std::make_pair('L', blockHash));
        batch.Write('T', tipBlockHash);
        const bool fSync = !IsInitialBlockDownload();
        return db->WriteBatch(batch, fSync);
    }
    return true;
}

} // namespace dip3
