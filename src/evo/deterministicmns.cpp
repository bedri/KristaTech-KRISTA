// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "evo/deterministicmns.h"

#include "adam.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "consensus/tx_verify.h"
#include "consensus/validation.h"
#include "hash.h"
#include "llmq.h"
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
    hw << nVersion << collateralOutpoint << addr << pubKeyOwner << blsPubKeyOperator;
    if (nVersion >= 2) {
        hw << blsPoP;
    }
    hw << keyIDVoting << nOperatorReward << scriptPayout << inputsHash;
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
    hw << nVersion << proTxHash << blsPubKeyOperator;
    if (nVersion >= 2) {
        hw << blsPoP;
    }
    hw << keyIDVoting << scriptPayout << inputsHash;
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
    if (mapListCache.count(blockHash) > 0) {
        return true;
    }
    return db && db->Exists(std::make_pair('L', blockHash));
}

uint256 CDeterministicMNManager::GetTipBlockHash() const {
    LOCK(cs);
    return tipBlockHash;
}

bool CDeterministicMNManager::SetTipBlockHash(const uint256& blockHash) {
    LOCK(cs);
    if (!blockHash.IsNull() && !HasListForBlock(blockHash)) {
        return false;
    }
    tipBlockHash = blockHash;
    if (db) {
        CDBBatch batch;
        if (blockHash.IsNull()) {
            batch.Erase('T');
        } else {
            batch.Write('T', blockHash);
        }
        return db->WriteBatch(batch, true);
    }
    return true;
}

bool CDeterministicMNManager::WriteListToDB(const uint256& blockHash, const CDeterministicMNList& list, bool fSync, const uint256& pruneBlockHash) {
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
    if (!pruneBlockHash.IsNull() && pruneBlockHash != blockHash) {
        mapListCache.erase(pruneBlockHash);
        batch.Erase(std::make_pair('L', pruneBlockHash));
    }
    return db->WriteBatch(batch, fSync);
}

CDeterministicMNList CDeterministicMNManager::GetListForBlock(const CBlockIndex* pindex) const {
    LOCK(cs);
    CDeterministicMNList list;
    if (!pindex || !pindex->phashBlock) {
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

static bool IsCanonicalPayoutScript(const CScript& script) {
    if (script.empty()) {
        return false;
    }
    CTxDestination dest;
    if (!ExtractDestination(script, dest)) {
        return false;
    }
    return script == GetScriptForDestination(dest);
}

static bool IsForbiddenProTxContainer(const CTransaction& tx) {
    return tx.IsCoinBase() || tx.IsCoinStake() || IsZeroCoinPoWTransaction(tx);
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

    const int nColHeight = pindexPrev ? (pindexPrev->nHeight + 1) : 1;
    if (IsForbiddenProTxContainer(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-invalid-tx-type");
    }
    if (ptxOut.nVersion != 2) {
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
    if (!ptxOut.pubKeyOwner.IsCompressed()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-owner-uncompressed");
    }
    if (!ptxOut.blsPubKeyOperator.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-key");
    }
    if (!ptxOut.blsPoP.IsValid() || !ptxOut.blsPoP.VerifyPoP(ptxOut.blsPubKeyOperator)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-bls-pop");
    }
    if (ptxOut.nOperatorReward > 10000) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-operator-reward");
    }
    if (!IsCanonicalPayoutScript(ptxOut.scriptPayout)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-payout-script");
    }
    if (!ptxOut.pubKeyOwner.VerifyStrictDER(ptxOut.GetSignHash(), ptxOut.vchSig)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-sig");
    }

    if (ptxOut.collateralOutpoint.hash.IsNull()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-null");
    }
    const CAmount nExpectedCollateral = CMasternode::GetMasternodeNodeCollateral(nColHeight);
    const CScript expectedOwnerScript = GetScriptForDestination(ptxOut.pubKeyOwner.GetID());
    resolvedCollateralOut = ptxOut.collateralOutpoint;

    Coin coin;
    if (!view.GetCoin(resolvedCollateralOut, coin) || coin.IsSpent()) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-missing");
    }
    if (coin.out.nValue != nExpectedCollateral) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-amount");
    }
    if (coin.out.scriptPubKey != expectedOwnerScript) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-owner");
    }
    const int nConfs = pindexPrev ? (pindexPrev->nHeight - (int)coin.nHeight + 1) : 0;
    if (nConfs < PROTX_MIN_COLLATERAL_CONFS) {
        return state.DoS(10, false, REJECT_INVALID, "bad-protx-collateral-confs");
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
    if (prevList.HasOperator(ptxOut.blsPubKeyOperator) ||
        IsBLSPubKeyPinnedToOther(ptxOut.pubKeyOwner, ptxOut.blsPubKeyOperator, MakeActiveChainPinPredicate(pindexPrev))) {
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

    const int nNextHeight = pindexPrev ? (pindexPrev->nHeight + 1) : 0;
    if (IsForbiddenProTxContainer(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-invalid-tx-type");
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
    if (!ptxOut.scriptOperatorPayout.empty()) {
        if (!IsCanonicalPayoutScript(ptxOut.scriptOperatorPayout)) {
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
    if (!ptxOut.scriptOperatorPayout.empty() && pmn->state.nOperatorReward == 0) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-op-payout-not-allowed");
    }
    if (pmn->state.IsBanned()) {
        if (pmn->state.nPoSeBanHeight >= 0 &&
            (nNextHeight - pmn->state.nPoSeBanHeight) < Params().GetConsensus().nPoseReviveCooldownBlocks) {
            return state.DoS(10, false, REJECT_INVALID, "bad-protx-pose-cooldown");
        }
    }
    if (!ptxOut.blsSig.Verify(pmn->state.blsPubKeyOperator, ptxOut.GetSignHash(), GetBLSDomainTag(BLSDomain::PROTX_UPSERV))) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupserv-sig");
    }
    return true;
}

static bool CheckProUpRegTx(const CTransaction& tx,
                            const std::vector<unsigned char>& vchPayload,
                            const CBlockIndex* pindexPrev,
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

    const int nNextHeight = pindexPrev ? (pindexPrev->nHeight + 1) : 0;
    if (IsForbiddenProTxContainer(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-invalid-tx-type");
    }
    if (ptxOut.nVersion != 2) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-version");
    }
    if (ptxOut.inputsHash != CalcProTxInputsHash(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-inputs-hash");
    }
    if (!ptxOut.blsPubKeyOperator.IsValid()) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-operator-key");
    }
    if (!ptxOut.blsPoP.IsValid() || !ptxOut.blsPoP.VerifyPoP(ptxOut.blsPubKeyOperator)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-bls-pop");
    }
    if (!IsCanonicalPayoutScript(ptxOut.scriptPayout)) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-payout-script");
    }
    const CDeterministicMN* pmn = prevList.GetMN(ptxOut.proTxHash);
    if (!pmn) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-missing-mn");
    }
    if (ptxOut.blsPubKeyOperator != pmn->state.blsPubKeyOperator) {
        const int nLastOpKeyHeight = (pmn->state.nOperatorKeyHeight >= 0) ? pmn->state.nOperatorKeyHeight : pmn->state.nRegisteredHeight;
        const int nCooldown = Params().GetConsensus().nProUpRegCooldownBlocks;
        if (nLastOpKeyHeight >= 0 && nCooldown > 0 && (nNextHeight - nLastOpKeyHeight) < nCooldown) {
            return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-cooldown");
        }
    }
    if ((ptxOut.blsPubKeyOperator != pmn->state.blsPubKeyOperator && prevList.HasOperator(ptxOut.blsPubKeyOperator)) ||
        IsBLSPubKeyPinnedToOther(pmn->state.pubKeyOwner, ptxOut.blsPubKeyOperator, MakeActiveChainPinPredicate(pindexPrev))) {
        return state.DoS(10, false, REJECT_INVALID, "bad-proupreg-dup-operator");
    }
    if (!pmn->state.pubKeyOwner.VerifyStrictDER(ptxOut.GetSignHash(), ptxOut.vchSig)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-proupreg-sig");
    }
    return true;
}

static bool CheckProUpRevTx(const CTransaction& tx,
                            const std::vector<unsigned char>& vchPayload,
                            const CBlockIndex* pindexPrev,
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

    if (IsForbiddenProTxContainer(tx)) {
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-invalid-tx-type");
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
    if (!ptxOut.blsSig.Verify(pmn->state.blsPubKeyOperator, ptxOut.GetSignHash(), GetBLSDomainTag(BLSDomain::PROTX_UPREV))) {
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
    } else if (pindexPrev && pindexPrev->phashBlock) {
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
        return CheckProUpRegTx(tx, vchPayload, pindexPrev, state, prevList, ptx);
    }
    case PROTX_UPREV: {
        CProUpRevTx ptx;
        return CheckProUpRevTx(tx, vchPayload, pindexPrev, state, prevList, ptx);
    }
    default:
        return state.DoS(100, false, REJECT_INVALID, "bad-protx-type");
    }
}

bool CDeterministicMNManager::ApplySpecialTxToList(const CTransaction& tx,
                                                   int nHeight,
                                                   CDeterministicMNList& mnList,
                                                   std::set<COutPoint>* pNewCollateralsOut) const {
    if (!tx.IsCoinBase()) {
        for (const auto& txin : tx.vin) {
            const CDeterministicMN* pmnSpent = mnList.GetMNByCollateral(txin.prevout);
            if (pmnSpent) {
                mnList.RemoveMN(pmnSpent->proTxHash);
            }
        }
    }

    std::vector<unsigned char> vchPayload;
    bool fMalformed = false;
    ProTxType type = ExtractProTxPayload(tx, vchPayload, fMalformed);
    if (fMalformed || type == PROTX_NONE) {
        return !fMalformed;
    }

    try {
        CDataStream ss(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
        if (type == PROTX_REG) {
            CProRegTx ptx;
            ss >> ptx;
            if (!ss.empty()) return false;
            CDeterministicMN mn;
            mn.proTxHash = tx.GetHash();
            mn.collateralOutpoint = ptx.collateralOutpoint.hash.IsNull()
                ? COutPoint(tx.GetHash(), ptx.collateralOutpoint.n)
                : ptx.collateralOutpoint;
            mn.state.nRegisteredHeight = nHeight;
            mn.state.nOperatorKeyHeight = nHeight;
            mn.state.addr = ptx.addr;
            mn.state.pubKeyOwner = ptx.pubKeyOwner;
            mn.state.blsPubKeyOperator = ptx.blsPubKeyOperator;
            mn.state.keyIDVoting = ptx.keyIDVoting;
            mn.state.nOperatorReward = ptx.nOperatorReward;
            mn.state.scriptPayout = ptx.scriptPayout;
            mnList.AddMN(mn);
            if (pNewCollateralsOut) {
                pNewCollateralsOut->insert(mn.collateralOutpoint);
            }
            return true;
        } else if (type == PROTX_UPSERV) {
            CProUpServTx ptx;
            ss >> ptx;
            if (!ss.empty()) return false;
            const CDeterministicMN* pmn = mnList.GetMN(ptx.proTxHash);
            if (!pmn) return false;
            CDeterministicMNState st = pmn->state;
            st.addr = ptx.addr;
            st.scriptOperatorPayout = ptx.scriptOperatorPayout;
            if (st.IsBanned()) {
                st.nPoSePenalty = 0;
                st.nPoSeBanHeight = -1;
                st.nPoSeRevivedHeight = nHeight;
            }
            mnList.UpdateMN(ptx.proTxHash, st);
            if (pNewCollateralsOut) {
                pNewCollateralsOut->insert(pmn->collateralOutpoint);
            }
            return true;
        } else if (type == PROTX_UPREG) {
            CProUpRegTx ptx;
            ss >> ptx;
            if (!ss.empty()) return false;
            const CDeterministicMN* pmn = mnList.GetMN(ptx.proTxHash);
            if (!pmn) return false;
            CDeterministicMNState st = pmn->state;
            if (ptx.blsPubKeyOperator != st.blsPubKeyOperator) {
                st.nOperatorKeyHeight = nHeight;
            }
            st.blsPubKeyOperator = ptx.blsPubKeyOperator;
            st.keyIDVoting = ptx.keyIDVoting;
            st.scriptPayout = ptx.scriptPayout;
            st.scriptOperatorPayout.clear();
            st.nRevocationReason = 0;
            mnList.UpdateMN(ptx.proTxHash, st);
            if (pNewCollateralsOut) {
                pNewCollateralsOut->insert(pmn->collateralOutpoint);
            }
            return true;
        } else if (type == PROTX_UPREV) {
            CProUpRevTx ptx;
            ss >> ptx;
            if (!ss.empty()) return false;
            const CDeterministicMN* pmn = mnList.GetMN(ptx.proTxHash);
            if (!pmn) return false;
            CDeterministicMNState st = pmn->state;
            st.blsPubKeyOperator.Reset();
            st.nRevocationReason = ptx.nReason;
            st.scriptOperatorPayout.clear();
            mnList.UpdateMN(ptx.proTxHash, st);
            if (pNewCollateralsOut) {
                pNewCollateralsOut->insert(pmn->collateralOutpoint);
            }
            return true;
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool CDeterministicMNManager::ProcessBlock(const CBlock& block,
                                           const CBlockIndex* pindex,
                                           CValidationState& state,
                                           const CCoinsViewCache& view,
                                           bool fJustCheck,
                                           CDeterministicMNList* pPreparedListOut,
                                           uint256* pPruneHashOut) {
    if (!pindex) {
        return true;
    }

    // Compute sync mode, primary committee/coordinator, and LLMQ BEFORE acquiring `cs` (avoid cs -> cs_main lock order inversion).
    const bool fSync = !fJustCheck && !pPreparedListOut && !IsInitialBlockDownload();
    uint256 pruneHash;
    if ((!fJustCheck || pPruneHashOut != nullptr) && pindex->nHeight > 2000) {
        const CBlockIndex* pPrune = pindex->GetAncestor(pindex->nHeight - 2000);
        if (pPrune && pPrune->phashBlock) {
            pruneHash = pPrune->GetBlockHash();
        }
    }
    std::vector<CPubKey> vExpectedPrimary;
    CPubKey missedPrimaryCoordKey;
    if (pindex->nHeight > 0 &&
        block.nVersion >= 11 && IsAdamActive(pindex->nHeight, Params().GetConsensus()) &&
        pindex->pprev && (!block.vAdamCoordinatorSig.empty() || !block.vAdamMiners.empty())) {
        const uint256 adamSeed = GetAdamSeed(pindex->pprev);
        CPubKey expectedPrimaryCoord;
        if (!adamSeed.IsNull() &&
            SelectAdamNodes(adamSeed, Params().GetConsensus(), vExpectedPrimary, expectedPrimaryCoord, pindex->nHeight, &view) &&
            !block.vAdamCoordinatorSig.empty() &&
            expectedPrimaryCoord.IsValid() &&
            !VerifyAdamCoordinatorSig(block, expectedPrimaryCoord, nullptr, pindex->nHeight, pindex->pprev)) {
            missedPrimaryCoordKey = expectedPrimaryCoord;
        }
    }
    llmq::CQuorum preLockQuorum;
    if (pindex->nHeight > 0 &&
        IsModelDActive(pindex->nHeight) &&
        block.nVersion >= 12 &&
        !block.vQuorumSig.empty()) {
        preLockQuorum = llmq::GetActiveQuorum(pindex->nHeight, pindex->pprev, &view);
    }

    LOCK(cs);
    CDeterministicMNList newList;
    if (pindex->pprev && pindex->pprev->phashBlock) {
        const bool fParentHadDip3 = (pindex->pprev->nHeight > 0);
        if (!ReadListFromDB(pindex->pprev->GetBlockHash(), newList) && fParentHadDip3) {
            return state.Error(strprintf("evodb missing deterministic MN list for parent block %s (height %d); restart with -reindex",
                pindex->pprev->GetBlockHash().ToString(), pindex->pprev->nHeight));
        }
    }
    const uint256 blockHash = pindex->phashBlock ? pindex->GetBlockHash() : block.GetHash();
    newList.blockHash = blockHash;
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
        if (IsForbiddenProTxContainer(tx)) {
            return state.DoS(100, false, REJECT_INVALID, "bad-protx-invalid-tx-type");
        }
        if (type == PROTX_REG) {
            CProRegTx ptx;
            COutPoint resolvedCol;
            if (!CheckProRegTx(tx, vchPayload, pindex->pprev, state, view, newList, ptx, resolvedCol)) {
                return false;
            }
            if (spentOutpointsInBlock.count(resolvedCol) > 0) {
                return state.DoS(100, false, REJECT_INVALID, "bad-protx-collateral-spent-in-block");
            }
            CDeterministicMN mn;
            mn.proTxHash = tx.GetHash();
            mn.collateralOutpoint = resolvedCol;
            mn.state.nRegisteredHeight = pindex->nHeight;
            mn.state.nOperatorKeyHeight = pindex->nHeight;
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
            if (!CheckProUpRegTx(tx, vchPayload, pindex->pprev, state, newList, ptx)) {
                return false;
            }
            const CDeterministicMN* pmn = newList.GetMN(ptx.proTxHash);
            CDeterministicMNState st = pmn->state;
            if (ptx.blsPubKeyOperator != st.blsPubKeyOperator) {
                st.nOperatorKeyHeight = pindex->nHeight;
            }
            st.blsPubKeyOperator = ptx.blsPubKeyOperator;
            st.keyIDVoting = ptx.keyIDVoting;
            st.scriptPayout = ptx.scriptPayout;
            st.scriptOperatorPayout.clear();
            st.nRevocationReason = 0;
            newList.UpdateMN(ptx.proTxHash, st);
        } else if (type == PROTX_UPREV) {
            CProUpRevTx ptx;
            if (!CheckProUpRevTx(tx, vchPayload, pindex->pprev, state, newList, ptx)) {
                return false;
            }
            const CDeterministicMN* pmn = newList.GetMN(ptx.proTxHash);
            CDeterministicMNState st = pmn->state;
            st.blsPubKeyOperator.Reset();
            st.nRevocationReason = ptx.nReason;
            st.scriptOperatorPayout.clear();
            newList.UpdateMN(ptx.proTxHash, st);
        }
    }

    // 3. Deterministic on-chain PoSe scoring against committee participation
    if (pindex->nHeight > 0 && block.nVersion >= 11 && IsAdamActive(pindex->nHeight, Params().GetConsensus()) && !block.vAdamMiners.empty()) {
        const size_t nCommitteeCount = (block.nVersion == 11)
            ? (block.vAdamMiners.size() - 1)
            : block.vAdamMiners.size();
        const size_t nScored = std::min(nCommitteeCount, block.vAdamSolutions.size());
        for (size_t idx = 0; idx < nScored; ++idx) {
            const CDeterministicMN* dmn = newList.GetMNByOwner(block.vAdamMiners[idx]);
            if (!dmn || !dmn->state.IsValid()) {
                continue;
            }
            if (!block.vAdamSolutions[idx].empty()) {
                if (dmn->state.nPoSePenalty > 0) {
                    CDeterministicMNState st = dmn->state;
                    st.nPoSePenalty = std::max(0, st.nPoSePenalty - 5);
                    newList.UpdateMN(dmn->proTxHash, st);
                }
            } else {
                ApplyPoSePunishment(newList, dmn->proTxHash, 5, pindex->nHeight);
            }
        }
        // Penalize silent primary committee members displaced by standby substitution (+5 PoSe).
        if (vExpectedPrimary.size() == nCommitteeCount) {
            for (size_t idx = 0; idx < nCommitteeCount; ++idx) {
                if (block.vAdamMiners[idx] != vExpectedPrimary[idx]) {
                    const CDeterministicMN* silentPrimaryDmn = newList.GetMNByOwner(vExpectedPrimary[idx]);
                    if (silentPrimaryDmn && silentPrimaryDmn->state.IsValid()) {
                        ApplyPoSePunishment(newList, silentPrimaryDmn->proTxHash, 5, pindex->nHeight);
                    }
                }
            }
        }
    }

    // 3b. Missed primary coordinator PoSe penalty when a fallback coordinator produced the block (M-4)
    if (missedPrimaryCoordKey.IsValid()) {
        const CDeterministicMN* primaryDmn = newList.GetMNByOwner(missedPrimaryCoordKey);
        if (primaryDmn && primaryDmn->state.IsValid()) {
            ApplyPoSePunishment(newList, primaryDmn->proTxHash, 5, pindex->nHeight);
        }
    }

    // 4. Deterministic on-chain PoSe scoring against LLMQ quorum participation
    if (pindex->nHeight > 0 &&
        IsModelDActive(pindex->nHeight) &&
        block.nVersion >= 12 &&
        !block.vQuorumSig.empty()) {
        try {
            llmq::CQuorumSignature qsig;
            CDataStream ss(block.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
            ss >> qsig;
            if (ss.empty()) {
                std::set<COutPoint> signedOutpoints;
                for (const auto& sigPair : qsig.signatures) {
                    signedOutpoints.insert(sigPair.first);
                }
                for (const auto& member : preLockQuorum.members) {
                    const CDeterministicMN* dmn = newList.GetMNByOwner(member.pubKeyMasternode);
                    if (!dmn || !dmn->state.IsValid()) {
                        continue;
                    }
                    if (signedOutpoints.count(member.collateralOutpoint) > 0) {
                        if (dmn->state.nPoSePenalty > 0) {
                            CDeterministicMNState st = dmn->state;
                            st.nPoSePenalty = std::max(0, st.nPoSePenalty - 5);
                            newList.UpdateMN(dmn->proTxHash, st);
                        }
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    if (pPruneHashOut) {
        *pPruneHashOut = pruneHash;
    }
    if (pPreparedListOut) {
        *pPreparedListOut = std::move(newList);
        return true;
    }
    if (!fJustCheck) {
        if (!WriteListToDB(blockHash, newList, fSync, pruneHash)) {
            return state.Error("Failed to write deterministic MN list to evodb");
        }
    }
    return true;
}

bool CDeterministicMNManager::CommitBlockList(const uint256& blockHash, const CDeterministicMNList& list, const uint256& pruneHash) {
    const bool fSync = !IsInitialBlockDownload();
    LOCK(cs);
    return WriteListToDB(blockHash, list, fSync, pruneHash);
}

bool CDeterministicMNManager::PruneLists(const std::vector<uint256>& pruneHashes) {
    if (pruneHashes.empty()) {
        return true;
    }
    LOCK(cs);
    if (!db) {
        for (const uint256& h : pruneHashes) {
            if (!h.IsNull() && h != tipBlockHash) {
                mapListCache.erase(h);
            }
        }
        return true;
    }
    CDBBatch batch;
    bool fAny = false;
    for (const uint256& h : pruneHashes) {
        if (!h.IsNull() && h != tipBlockHash) {
            mapListCache.erase(h);
            batch.Erase(std::make_pair('L', h));
            fAny = true;
        }
    }
    return fAny ? db->WriteBatch(batch, false) : true;
}

bool CDeterministicMNManager::UndoBlock(const CBlock& block, const CBlockIndex* pindex) {
    if (!pindex) {
        return true;
    }
    const bool fSync = !IsInitialBlockDownload();
    LOCK(cs);
    const uint256 blockHash = pindex->phashBlock ? pindex->GetBlockHash() : block.GetHash();
    mapListCache.erase(blockHash);
    if (pindex->pprev && pindex->pprev->phashBlock) {
        tipBlockHash = pindex->pprev->GetBlockHash();
    } else {
        tipBlockHash.SetNull();
    }
    if (db) {
        CDBBatch batch;
        batch.Erase(std::make_pair('L', blockHash));
        batch.Write('T', tipBlockHash);
        return db->WriteBatch(batch, fSync);
    }
    return true;
}

} // namespace dip3
