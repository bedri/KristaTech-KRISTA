// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"
#include "adam.h"
#include "llmq.h"
#include "chainparams.h"
#include "base58.h"
#include "evo/deterministicmns.h"
#include "masternode.h"
#include "masternode-sync.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include "core_io.h"



bool IsBlockValueValid(int nHeight, CAmount nExpectedValue, CAmount nMinted)
{
    // No superblock, regular check
    return nMinted <= nExpectedValue;
}

static void AppendDip3OrDirectPayment(
    const dip3::CDeterministicMNList* pDip3List,
    const CPubKey& ownerKey,
    const CScript& fallbackScript,
    CAmount nTotalAmt,
    std::vector<std::pair<CScript, CAmount>>& vOut)
{
    if (nTotalAmt <= 0) return;
    if (pDip3List && ownerKey.IsValid()) {
        const dip3::CDeterministicMN* dmn = pDip3List->GetMNByOwner(ownerKey);
        if (dmn && !dmn->state.scriptPayout.empty()) {
            if (dmn->state.nOperatorReward > 0 && dmn->state.nOperatorReward <= 10000 &&
                !dmn->state.scriptOperatorPayout.empty()) {
                CTxDestination opDest;
                if (ExtractDestination(dmn->state.scriptOperatorPayout, opDest)) {
                    CAmount nOpAmt = (CAmount)(((__int128)nTotalAmt * dmn->state.nOperatorReward) / 10000);
                    CAmount nOwnerAmt = nTotalAmt - nOpAmt;
                    if (nOwnerAmt > 0) {
                        vOut.push_back(std::make_pair(dmn->state.scriptPayout, nOwnerAmt));
                    }
                    if (nOpAmt > 0) {
                        vOut.push_back(std::make_pair(dmn->state.scriptOperatorPayout, nOpAmt));
                    }
                    return;
                }
            }
            vOut.push_back(std::make_pair(dmn->state.scriptPayout, nTotalAmt));
            return;
        }
    }
    if (!fallbackScript.empty()) {
        vOut.push_back(std::make_pair(fallbackScript, nTotalAmt));
    }
}

static bool GetModelDPassiveMasternodePayee(int nBlockHeight, const CBlockIndex* pindexPrev, CScript& payeeOut, const CCoinsViewCache* pView = nullptr, CPubKey* pWinnerOwnerOut = nullptr)
{
    payeeOut.clear();
    if (pWinnerOwnerOut) *pWinnerOwnerOut = CPubKey();
    if (!pindexPrev) return false;
    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;

    std::vector<std::pair<COutPoint, CPubKey>> vActiveMns;
    dip3::CDeterministicMNList mnList;
    const bool fUseDip3 = (deterministicMNManager != nullptr);
    {
        LOCK(cs_main);
        int refHeight = pindexPrev->nHeight;
        std::set<COutPoint> seenOutpoints;
        if (fUseDip3) {
            mnList = deterministicMNManager->GetListForBlock(pindexPrev);
            for (const auto& dmn : mnList.GetValidMNs()) {
                if (dmn.state.pubKeyOwner.IsValid() && seenOutpoints.insert(dmn.collateralOutpoint).second) {
                    vActiveMns.push_back(std::make_pair(dmn.collateralOutpoint, dmn.state.pubKeyOwner));
                }
            }
        } else {
            for (const auto& kv : mapMasternodeCollateralOutpoints) {
                if (kv.first.IsValid() && IsMasternodeActiveAtHeight(kv.first, refHeight, 2880)) {
                    if (!seenOutpoints.count(kv.second) &&
                        IsValidMasternodeCollateralAndAuth(kv.first, kv.second, refHeight, nullptr, coinsView)) {
                        vActiveMns.push_back(std::make_pair(kv.second, kv.first));
                        seenOutpoints.insert(kv.second);
                    }
                }
            }
        }
        std::sort(vActiveMns.begin(), vActiveMns.end());
        if (!vActiveMns.empty()) {
            uint256 seed = GetAdamSeed(pindexPrev);
            size_t winIdx = seed.GetUint64(0) % vActiveMns.size();
            const COutPoint& winOutpoint = vActiveMns[winIdx].first;
            const CPubKey& winOwner = vActiveMns[winIdx].second;
            if (pWinnerOwnerOut) *pWinnerOwnerOut = winOwner;
            if (fUseDip3) {
                const dip3::CDeterministicMN* dmn = mnList.GetMNByCollateral(winOutpoint);
                if (dmn && !dmn->state.scriptPayout.empty()) {
                    payeeOut = dmn->state.scriptPayout;
                    return true;
                }
            }
            if (coinsView) {
                Coin coin;
                if (coinsView->GetCoin(winOutpoint, coin) && !coin.IsSpent() &&
                    coin.out.nValue == CMasternode::GetMasternodeNodeCollateral(nBlockHeight)) {
                    payeeOut = coin.out.scriptPubKey;
                    return true;
                }
            }
        }
    }

    std::vector<CPubKey> vOnChainPool = GetAdamMinerPool(nBlockHeight - 1, coinsView);
    if (vOnChainPool.empty()) return false;
    uint256 seed = GetAdamSeed(pindexPrev);
    size_t winIdx = seed.GetUint64(0) % vOnChainPool.size();
    const CPubKey& winKey = vOnChainPool[winIdx];
    if (pWinnerOwnerOut) *pWinnerOwnerOut = winKey;
    if (fUseDip3) {
        const dip3::CDeterministicMN* dmn = mnList.GetMNByOwner(winKey);
        if (dmn && !dmn->state.scriptPayout.empty()) {
            payeeOut = dmn->state.scriptPayout;
            return true;
        }
    } else {
        LOCK(cs_main);
        auto itOut = mapMasternodeCollateralOutpoints.find(winKey);
        if (itOut != mapMasternodeCollateralOutpoints.end() && coinsView) {
            Coin coin;
            if (coinsView->GetCoin(itOut->second, coin) && !coin.IsSpent() &&
                coin.out.nValue == CMasternode::GetMasternodeNodeCollateral(nBlockHeight)) {
                payeeOut = coin.out.scriptPubKey;
                return true;
            }
        }
    }
    if (winKey.IsValid()) {
        payeeOut = GetScriptForDestination(winKey.GetID());
        return true;
    }
    return false;
}

static void ComputeModelDValidatorSeatPayments(
    int nBlockHeight,
    CAmount nBlockVal,
    CAmount nPartSplitTotal,
    const std::vector<CPubKey>& vCommitteeMiners,
    const std::vector<CPubKey>& vSolvedMiners,
    const CCoinsViewCache* coinsView,
    const CBlockIndex* pindexPrev,
    std::vector<std::pair<CScript, CAmount>>& vPaymentsOut,
    CAmount& nPaidPartTotalOut)
{
    vPaymentsOut.clear();
    nPaidPartTotalOut = 0;

    dip3::CDeterministicMNList dip3List;
    const dip3::CDeterministicMNList* pDip3List = nullptr;
    if (deterministicMNManager && pindexPrev) {
        dip3List = deterministicMNManager->GetListForBlock(pindexPrev);
        pDip3List = &dip3List;
    }

    auto resolveValidatorPayeeScript = [&](const CPubKey& minerKey) -> CScript {
        if (pDip3List && minerKey.IsValid()) {
            const dip3::CDeterministicMN* dmn = pDip3List->GetMNByOwner(minerKey);
            if (dmn && !dmn->state.scriptPayout.empty()) {
                return dmn->state.scriptPayout;
            }
        }
        if (!deterministicMNManager) {
            LOCK(cs_main);
            auto itOut = mapMasternodeCollateralOutpoints.find(minerKey);
            if (itOut != mapMasternodeCollateralOutpoints.end() && coinsView) {
                Coin coin;
                if (coinsView->GetCoin(itOut->second, coin) && !coin.IsSpent() &&
                    coin.out.nValue == CMasternode::GetMasternodeNodeCollateral(nBlockHeight)) {
                    return coin.out.scriptPubKey;
                }
            }
        }
        if (minerKey.IsValid()) {
            return GetScriptForDestination(minerKey.GetID());
        }
        return CScript();
    };

    const size_t nCommitteeSlots = std::max<size_t>(1, vCommitteeMiners.size());
    const CAmount nSeatBase = nPartSplitTotal / (CAmount)nCommitteeSlots;
    const CAmount nSeatRem = nPartSplitTotal % (CAmount)nCommitteeSlots;
    for (size_t idx = 0; idx < vSolvedMiners.size(); ++idx) {
        const CPubKey& minerKey = vSolvedMiners[idx];
        CScript minerScript = resolveValidatorPayeeScript(minerKey);
        if (!minerScript.empty() || (pDip3List && minerKey.IsValid() && pDip3List->GetMNByOwner(minerKey) != nullptr)) {
            const bool fGiveRem = (vSolvedMiners.size() >= nCommitteeSlots) && (idx + 1 == vSolvedMiners.size());
            const CAmount amt = nSeatBase + (fGiveRem ? nSeatRem : 0);
            AppendDip3OrDirectPayment(pDip3List, minerKey, minerScript, amt, vPaymentsOut);
            nPaidPartTotalOut += amt;
        }
    }
}

struct PrevLlmqSignersEntry {
    llmq::CQuorum quorum;
    std::set<COutPoint> signedOutpoints;
};
static RecursiveMutex cs_prev_llmq_cache;
static std::map<uint256, PrevLlmqSignersEntry> mapPrevLlmqCache;

void ClearPrevLlmqCache()
{
    LOCK(cs_prev_llmq_cache);
    mapPrevLlmqCache.clear();
}

static CScript ResolveCollateralOrPubKeyScript(
    const CPubKey& pubKey,
    const COutPoint* pPreferOutpoint,
    int nBlockHeight,
    const CCoinsViewCache* coinsView)
{
    if (coinsView) {
        LOCK(cs_main);
        Coin coin;
        const CAmount nColAmt = CMasternode::GetMasternodeNodeCollateral(nBlockHeight);
        if (pPreferOutpoint && !pPreferOutpoint->IsNull() &&
            coinsView->GetCoin(*pPreferOutpoint, coin) && !coin.IsSpent() &&
            coin.out.nValue == nColAmt) {
            return coin.out.scriptPubKey;
        }
        if (!deterministicMNManager) {
            auto itOut = mapMasternodeCollateralOutpoints.find(pubKey);
            if (itOut != mapMasternodeCollateralOutpoints.end() &&
                coinsView->GetCoin(itOut->second, coin) && !coin.IsSpent() &&
                coin.out.nValue == nColAmt) {
                return coin.out.scriptPubKey;
            }
        }
    }
    if (pubKey.IsValid()) {
        return GetScriptForDestination(pubKey.GetID());
    }
    return CScript();
}

static bool GetPrevBlockLlmqSigners(
    const CBlockIndex* pindexPrev,
    llmq::CQuorum& prevQuorumOut,
    std::set<COutPoint>& signedOutpointsOut)
{
    prevQuorumOut = llmq::CQuorum();
    signedOutpointsOut.clear();
    if (!pindexPrev || !pindexPrev->pprev || !pindexPrev->phashBlock || !IsModelDActive(pindexPrev->nHeight)) {
        return false;
    }
    const uint256 prevHash = pindexPrev->GetBlockHash();
    bool fOnActiveChain = false;
    {
        LOCK(cs_main);
        fOnActiveChain = chainActive.Contains(pindexPrev);
    }
    const bool fDip3Required = deterministicMNManager && pindexPrev->pprev->nHeight > 0;
    const bool fCanCache = fOnActiveChain &&
        (!fDip3Required || deterministicMNManager->HasListForBlock(pindexPrev->pprev->GetBlockHash()));

    if (fCanCache) {
        LOCK(cs_prev_llmq_cache);
        auto it = mapPrevLlmqCache.find(prevHash);
        if (it != mapPrevLlmqCache.end()) {
            prevQuorumOut = it->second.quorum;
            signedOutpointsOut = it->second.signedOutpoints;
            return !prevQuorumOut.members.empty();
        }
    }
    CBlock prevBlock;
    if (!ReadBlockFromDisk(prevBlock, pindexPrev)) {
        return false;
    }
    if (prevBlock.nVersion < 12 || prevBlock.vQuorumSig.empty()) {
        return false;
    }
    llmq::CQuorumSignature prevQsig;
    try {
        CDataStream ss(prevBlock.vQuorumSig, SER_NETWORK, PROTOCOL_VERSION);
        ss >> prevQsig;
        if (!ss.empty()) {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }
    prevQuorumOut = llmq::GetActiveQuorum(pindexPrev->nHeight, pindexPrev->pprev, nullptr);
    if (prevQuorumOut.members.empty()) {
        return false;
    }
    for (const auto& sigPair : prevQsig.signatures) {
        signedOutpointsOut.insert(sigPair.first);
    }
    if (fCanCache) {
        LOCK(cs_prev_llmq_cache);
        if (mapPrevLlmqCache.size() >= 256) {
            mapPrevLlmqCache.clear();
        }
        mapPrevLlmqCache[prevHash] = PrevLlmqSignersEntry{prevQuorumOut, signedOutpointsOut};
    }
    return true;
}

static void ComputeModelDLlmqPayments(
    int nBlockHeight,
    const CBlockIndex* pindexPrev,
    const CCoinsViewCache* coinsView,
    const dip3::CDeterministicMNList* pDip3List,
    CAmount nLLMQSplitTotal,
    std::vector<std::pair<CScript, CAmount>>& vLlmqPaymentsOut,
    CAmount& nPaidLlmqTotalOut)
{
    vLlmqPaymentsOut.clear();
    nPaidLlmqTotalOut = 0;

    llmq::CQuorum prevQuorum;
    std::set<COutPoint> prevSignedOutpoints;
    if (GetPrevBlockLlmqSigners(pindexPrev, prevQuorum, prevSignedOutpoints) && !prevQuorum.members.empty()) {
        const size_t nTotalMembers = prevQuorum.members.size();
        const CAmount nPerMember = nLLMQSplitTotal / (CAmount)nTotalMembers;
        const CAmount nRem = nLLMQSplitTotal % (CAmount)nTotalMembers;

        std::vector<std::pair<CPubKey, CScript>> vSignedPayees;
        for (const auto& member : prevQuorum.members) {
            if (prevSignedOutpoints.count(member.collateralOutpoint) == 0) {
                continue;
            }
            CScript fallbackScript = ResolveCollateralOrPubKeyScript(member.pubKeyMasternode, &member.collateralOutpoint, nBlockHeight, coinsView);
            if (!fallbackScript.empty() || (pDip3List && member.pubKeyMasternode.IsValid() && pDip3List->GetMNByOwner(member.pubKeyMasternode) != nullptr)) {
                vSignedPayees.push_back(std::make_pair(member.pubKeyMasternode, fallbackScript));
            }
        }
        const size_t nSignedCount = vSignedPayees.size();
        for (size_t idx = 0; idx < nSignedCount; ++idx) {
            const bool fGiveRem = (nSignedCount == nTotalMembers) && (idx + 1 == nSignedCount);
            const CAmount memberAmt = nPerMember + (fGiveRem ? nRem : 0);
            AppendDip3OrDirectPayment(pDip3List, vSignedPayees[idx].first, vSignedPayees[idx].second, memberAmt, vLlmqPaymentsOut);
            nPaidLlmqTotalOut += memberAmt;
        }
        return;
    }

    llmq::CQuorum quorum = llmq::GetActiveQuorum(nBlockHeight, pindexPrev, coinsView);
    std::vector<std::pair<CPubKey, CScript>> vLlmqBasePayees;
    for (const auto& member : quorum.members) {
        CScript fallbackScript = ResolveCollateralOrPubKeyScript(member.pubKeyMasternode, &member.collateralOutpoint, nBlockHeight, coinsView);
        if (!fallbackScript.empty() || (pDip3List && member.pubKeyMasternode.IsValid() && pDip3List->GetMNByOwner(member.pubKeyMasternode) != nullptr)) {
            vLlmqBasePayees.push_back(std::make_pair(member.pubKeyMasternode, fallbackScript));
        }
    }
    if (!vLlmqBasePayees.empty()) {
        const CAmount nLLMQPaymentPerMember = nLLMQSplitTotal / (CAmount)vLlmqBasePayees.size();
        const CAmount nLLMQRemainder = nLLMQSplitTotal % (CAmount)vLlmqBasePayees.size();
        for (size_t idx = 0; idx < vLlmqBasePayees.size(); ++idx) {
            const CAmount memberAmt = nLLMQPaymentPerMember + (idx == vLlmqBasePayees.size() - 1 ? nLLMQRemainder : 0);
            AppendDip3OrDirectPayment(pDip3List, vLlmqBasePayees[idx].first, vLlmqBasePayees[idx].second, memberAmt, vLlmqPaymentsOut);
            nPaidLlmqTotalOut += memberAmt;
        }
    }
}

static bool MatchExpectedPayees(
    const CTransaction& txNew,
    std::vector<bool>& vOutUsed,
    const std::vector<std::pair<CScript, CAmount>>& vExpected,
    const char* pszLabel,
    bool fLogMissing)
{
    for (size_t idx = 0; idx < vExpected.size(); ++idx) {
        const CScript& expectedScript = vExpected[idx].first;
        const CAmount expectedAmt = vExpected[idx].second;
        bool found = false;
        if (!expectedScript.empty()) {
            for (size_t outIdx = 0; outIdx < txNew.vout.size(); ++outIdx) {
                if (!vOutUsed[outIdx] &&
                    txNew.vout[outIdx].scriptPubKey == expectedScript &&
                    txNew.vout[outIdx].nValue == expectedAmt) {
                    vOutUsed[outIdx] = true;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            if (fLogMissing) {
                LogPrintf("IsBlockPayeeValid : Missing %s payment of %s to script %s\n",
                          pszLabel, FormatMoney(expectedAmt).c_str(), HexStr(expectedScript).c_str());
            }
            return false;
        }
    }
    return true;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight, const CCoinsViewCache* pView)
{
    LOCK(cs_main);
    const CCoinsViewCache* coinsView = pView ? pView : pcoinsTip;
    const bool fPoS = block.IsProofOfStake();
    if (nBlockHeight > 1 || IsAdamValidatorSplitActive(nBlockHeight)) {
        if (block.vtx.size() < (fPoS ? 2 : 1)) {
            return false;
        }
    }
    const CTransaction* pTxNew = (block.vtx.size() >= (fPoS ? 2 : 1)) ? &(fPoS ? block.vtx[1] : block.vtx[0]) : nullptr;

    CBlockIndex* pindexPrev = nullptr;
    if (IsAdamValidatorSplitActive(nBlockHeight) && (g_fMasternodePingStateInitialized || Params().IsRegTestNet())) {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(block.hashPrevBlock);
        if (it != mapBlockIndex.end()) {
            pindexPrev = it->second;
        }
    }

    CAmount nBlockValActual = CMasternode::GetBlockValue(nBlockHeight);
    CAmount nTreasurySplit = (nBlockHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
    CAmount nBlockVal = nBlockValActual - nTreasurySplit;
    CAmount nMNSplit = nBlockVal * 5 / 100;
    CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
    CAmount nPartSplitTotal = nBlockVal * 60 / 100;

    std::vector<std::pair<CScript, CAmount>> vPassiveMnPayments;
    std::vector<std::pair<CScript, CAmount>> vLlmqPayments;
    std::vector<std::pair<CScript, CAmount>> vExpectedPartPayments;
    std::vector<std::vector<std::pair<CScript, CAmount>>> vEligibleCoordPayOptions;
    CAmount nUnclaimedPartSplit = 0;
    CScript expectedMnPayee;
    CPubKey passiveWinnerOwner;
    const bool hasModelDPayment = IsModelDActive(nBlockHeight) &&
        (g_fMasternodePingStateInitialized || Params().IsRegTestNet()) &&
        pindexPrev &&
        GetModelDPassiveMasternodePayee(nBlockHeight, pindexPrev, expectedMnPayee, coinsView, &passiveWinnerOwner);
    const bool hasPreModelDValidatorPayment = !hasModelDPayment &&
        IsAdamValidatorSplitActive(nBlockHeight) &&
        (!IsModelDActive(nBlockHeight) || !Params().IsRegTestNet()) &&
        (g_fMasternodePingStateInitialized || Params().IsRegTestNet()) &&
        pindexPrev != nullptr;
    if (!hasModelDPayment && hasPreModelDValidatorPayment) {
        nPartSplitTotal = nBlockVal * 75 / 100;
    }
    bool hasActiveValidatorSplit = false;
    bool fRequireCoordPosPayment = false;

    dip3::CDeterministicMNList dip3List;
    const dip3::CDeterministicMNList* pDip3List = nullptr;
    if (deterministicMNManager && pindexPrev) {
        dip3List = deterministicMNManager->GetListForBlock(pindexPrev);
        pDip3List = &dip3List;
    }

    if (hasModelDPayment || hasPreModelDValidatorPayment) {
        CAmount nPaidLlmqTotal = 0;
        if (hasModelDPayment) {
            AppendDip3OrDirectPayment(pDip3List, passiveWinnerOwner, expectedMnPayee, nMNSplit, vPassiveMnPayments);
            ComputeModelDLlmqPayments(nBlockHeight, pindexPrev, coinsView, pDip3List, nLLMQSplitTotal, vLlmqPayments, nPaidLlmqTotal);
        }

        uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
        std::vector<CPubKey> vMiners;
        std::vector<CPubKey> vStandbyMiners;
        CPubKey coordinator;
        SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator, nBlockHeight, coinsView, &vStandbyMiners);

        if (hasModelDPayment || !vMiners.empty()) {
            hasActiveValidatorSplit = true;
            std::vector<CPubKey> vBlockCommittee = block.vAdamMiners;
            if (block.nVersion == 11 && vBlockCommittee.size() == vMiners.size() + 1) {
                vBlockCommittee.pop_back();
            }
            std::vector<std::vector<unsigned char>> vBlockCommitteeSols(
                block.vAdamSolutions.begin(),
                block.vAdamSolutions.begin() + std::min(block.vAdamSolutions.size(), vBlockCommittee.size()));

            const int64_t nBlockTimeDiff = std::max<int64_t>(0, block.GetBlockTime() - pindexPrev->GetBlockTime());
            if (!vBlockCommittee.empty() && vBlockCommittee != vMiners) {
                if (!ValidateAdamCommitteeForTime(
                        block.hashPrevBlock, hashAdamSeed, nBlockHeight, nBlockTimeDiff,
                        block.nBits, block.nVersion, vBlockCommittee, vBlockCommitteeSols,
                        vMiners, vStandbyMiners, nullptr, nullptr, coinsView)) {
                    LogPrintf("%s : Invalid standby validator substitution in block %s\n", __func__, block.GetHash().ToString().c_str());
                    return false;
                }
            }

            std::vector<CPubKey> vActivePartMiners;
            for (size_t i = 0; i < vMiners.size(); ++i) {
                const CPubKey& seatMiner = (i < vBlockCommittee.size() && vBlockCommittee[i].IsValid()) ? vBlockCommittee[i] : vMiners[i];
                if (i < vBlockCommitteeSols.size() && !vBlockCommitteeSols[i].empty()) {
                    int standbyPoolIdx = -1;
                    if (seatMiner != vMiners[i]) {
                        for (size_t s = 0; s < vStandbyMiners.size(); ++s) {
                            if (vStandbyMiners[s] == seatMiner) {
                                standbyPoolIdx = (int)vMiners.size() + (int)s;
                                break;
                            }
                        }
                    }
                    if (VerifyAdamSolution(block.hashPrevBlock, hashAdamSeed, seatMiner, vBlockCommitteeSols[i], block.nBits, block.nVersion, nBlockHeight, (int)i, coinsView) ||
                        (standbyPoolIdx >= 0 && VerifyAdamSolution(block.hashPrevBlock, hashAdamSeed, seatMiner, vBlockCommitteeSols[i], block.nBits, block.nVersion, nBlockHeight, standbyPoolIdx, coinsView))) {
                        vActivePartMiners.push_back(seatMiner);
                    }
                }
            }

            CAmount nPaidPartTotal = 0;
            ComputeModelDValidatorSeatPayments(
                nBlockHeight, nBlockVal, nPartSplitTotal, vMiners, vActivePartMiners,
                coinsView, pindexPrev, vExpectedPartPayments, nPaidPartTotal);

            nUnclaimedPartSplit = (nPartSplitTotal - nPaidPartTotal) + (hasModelDPayment ? (nLLMQSplitTotal - nPaidLlmqTotal) : 0);

            if (fPoS) {
                const CAmount nCoordPoSShare = hasModelDPayment ? (nBlockVal * 3 / 100) : (nBlockVal * 5 / 100);
                if (nCoordPoSShare > 0) {
                    std::vector<CPubKey> vEligibleCoords;
                    const bool fAllowRot = (block.nVersion == 11) || (nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS);
                    const uint256 hashBlock = block.GetHash();
                    auto addCoordCandidate = [&](const CPubKey& k) {
                        if (k.IsValid() && std::find(vEligibleCoords.begin(), vEligibleCoords.end(), k) == vEligibleCoords.end()) {
                            vEligibleCoords.push_back(k);
                        }
                    };
                    if (!block.vAdamCoordinatorSig.empty()) {
                        if (coordinator.IsValid() && VerifyAdamCoordinatorSig(block, coordinator, &hashBlock, nBlockHeight, pindexPrev)) {
                            addCoordCandidate(coordinator);
                        } else if (fAllowRot && !block.vAdamMiners.empty()) {
                            if (block.nVersion >= 12 && nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                                const std::map<CPubKey, std::vector<unsigned char>> emptySolutions;
                                const std::vector<CPubKey>& vFbMiners = !vMiners.empty() ? vMiners : block.vAdamMiners;
                                CPubKey activeFb, prevFb;
                                int rotIdx = 0;
                                if (GetAdamFallbackCoordinatorForSlot(hashAdamSeed, nBlockHeight, nBlockTimeDiff, vFbMiners, emptySolutions, activeFb, prevFb, rotIdx, pindexPrev)) {
                                    if (activeFb.IsValid() && VerifyAdamCoordinatorSig(block, activeFb, &hashBlock, nBlockHeight, pindexPrev)) {
                                        addCoordCandidate(activeFb);
                                    } else if (prevFb.IsValid() && VerifyAdamCoordinatorSig(block, prevFb, &hashBlock, nBlockHeight, pindexPrev)) {
                                        addCoordCandidate(prevFb);
                                    }
                                }
                            } else {
                                for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                                    if (it->IsValid() && VerifyAdamCoordinatorSig(block, *it, &hashBlock, nBlockHeight, pindexPrev)) {
                                        addCoordCandidate(*it);
                                        break;
                                    }
                                }
                            }
                        }
                    } else {
                        addCoordCandidate(coordinator);
                        if (fAllowRot && !block.vAdamMiners.empty()) {
                            if (block.nVersion >= 12 && nBlockTimeDiff >= ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS) {
                                const std::map<CPubKey, std::vector<unsigned char>> emptySolutions;
                                const std::vector<CPubKey>& vFbMiners = !vMiners.empty() ? vMiners : block.vAdamMiners;
                                CPubKey activeFb, prevFb;
                                int rotIdx = 0;
                                if (GetAdamFallbackCoordinatorForSlot(hashAdamSeed, nBlockHeight, nBlockTimeDiff, vFbMiners, emptySolutions, activeFb, prevFb, rotIdx, pindexPrev)) {
                                    addCoordCandidate(activeFb);
                                    addCoordCandidate(prevFb);
                                }
                            } else {
                                for (auto it = block.vAdamMiners.rbegin(); it != block.vAdamMiners.rend(); ++it) {
                                    addCoordCandidate(*it);
                                }
                            }
                        }
                    }
                    for (const auto& ck : vEligibleCoords) {
                        std::vector<std::pair<CScript, CAmount>> vOpt;
                        CScript fbScript = ResolveCollateralOrPubKeyScript(ck, nullptr, nBlockHeight, coinsView);
                        AppendDip3OrDirectPayment(pDip3List, ck, fbScript, nCoordPoSShare, vOpt);
                        if (!vOpt.empty()) {
                            vEligibleCoordPayOptions.push_back(std::move(vOpt));
                        }
                    }
                    fRequireCoordPosPayment = coordinator.IsValid() || !vEligibleCoordPayOptions.empty();
                }
            }
        }
    }

    std::vector<bool> vOutUsed(pTxNew ? pTxNew->vout.size() : 0, false);

    // Validate Developer Treasury and Protocol Burn splits for all blocks starting after block 1
    if (nBlockHeight > 1 && pTxNew) {
        const CTransaction& txNew = *pTxNew;
        const bool hasBurnAddr = !Params().GetConsensus().mBurnAddresses.empty();
        const CAmount nExpectedBurn = hasBurnAddr ? (nUnclaimedPartSplit / 2) : 0;
        CAmount nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn);

        // Validate Treasury output
        bool foundTreasury = false;
        CScript treasuryScript = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        for (size_t outIdx = 0; outIdx < txNew.vout.size(); ++outIdx) {
            if (!vOutUsed[outIdx] && txNew.vout[outIdx].scriptPubKey == treasuryScript && txNew.vout[outIdx].nValue == nExpectedTreasury) {
                vOutUsed[outIdx] = true;
                foundTreasury = true;
                break;
            }
        }
        if (!foundTreasury) {
            LogPrintf("%s : Missing or invalid Developer Treasury payment of %s to address %s\n",
                      __func__, FormatMoney(nExpectedTreasury).c_str(), Params().DeveloperFundAddress().c_str());
            return false;
        }

        // Validate automatic Protocol Burn output (50% of nUnclaimedPartSplit)
        if (nExpectedBurn > 0) {
            bool foundBurn = false;
            const std::string& strBurnAddr = Params().GetConsensus().mBurnAddresses.begin()->first;
            CScript burnScript = GetScriptForDestination(DecodeDestination(strBurnAddr));
            for (size_t outIdx = 0; outIdx < txNew.vout.size(); ++outIdx) {
                if (!vOutUsed[outIdx] && txNew.vout[outIdx].scriptPubKey == burnScript && txNew.vout[outIdx].nValue == nExpectedBurn) {
                    vOutUsed[outIdx] = true;
                    foundBurn = true;
                    break;
                }
            }
            if (!foundBurn) {
                LogPrintf("%s : Missing or invalid Protocol Burn payment of %s to address %s\n",
                          __func__, FormatMoney(nExpectedBurn).c_str(), strBurnAddr.c_str());
                return false;
            }
        }
    }

    if (!IsAdamValidatorSplitActive(nBlockHeight)) {
        return true;
    }

    if (!pTxNew) {
        return false;
    }
    const CTransaction& txNew = *pTxNew;

    // ADAM validator & Model D active/participant payments validation (deterministic on-chain consensus state)
    if (IsAdamValidatorSplitActive(nBlockHeight) && (g_fMasternodePingStateInitialized || Params().IsRegTestNet())) {
        if (!pindexPrev) {
            LogPrintf("%s : Failed to find parent block index for block %s\n", __func__, block.GetHash().ToString().c_str());
            return false;
        } else if (hasActiveValidatorSplit) {
            const size_t nProducerOutIdx = txNew.IsCoinStake() ? 1 : 0;
            if (nProducerOutIdx < vOutUsed.size()) {
                vOutUsed[nProducerOutIdx] = true;
                if (txNew.IsCoinStake()) {
                    for (size_t splitIdx = 2; splitIdx < txNew.vout.size(); ++splitIdx) {
                        if (txNew.vout[splitIdx].scriptPubKey == txNew.vout[1].scriptPubKey &&
                            txNew.vout[splitIdx].nValue > nBlockValActual) {
                            vOutUsed[splitIdx] = true;
                        } else {
                            break;
                        }
                    }
                }
            }

            if (hasModelDPayment) {
                // 0. Validate Passive Masternode Split (exact 5% of net block value, split if operator reward configured)
                if (!MatchExpectedPayees(txNew, vOutUsed, vPassiveMnPayments, "passive masternode", true)) {
                    return false;
                }

                // 1. Validate LLMQ Quorum Split (signer-only, or full quorum before first LLMQ-signed block)
                if (!MatchExpectedPayees(txNew, vOutUsed, vLlmqPayments, "LLMQ", true)) {
                    return false;
                }
            }

            // 2. Validate Participant Validators Split (60% Model D / 75% Pre-Model D)
            if (!MatchExpectedPayees(txNew, vOutUsed, vExpectedPartPayments, "validator participant", true)) {
                return false;
            }

            // 3. Validate PoS Signing Coordinator Split (3% Model D / 5% Pre-Model D)
            if (fRequireCoordPosPayment) {
                bool foundCoord = false;
                for (const auto& coordOpt : vEligibleCoordPayOptions) {
                    std::vector<bool> vTryUsed = vOutUsed;
                    if (MatchExpectedPayees(txNew, vTryUsed, coordOpt, "PoS coordinator", false)) {
                        vOutUsed = std::move(vTryUsed);
                        foundCoord = true;
                        break;
                    }
                }
                if (!foundCoord) {
                    LogPrintf("%s : Missing PoS coordinator payment in block %s\n", __func__, block.GetHash().ToString().c_str());
                    return false;
                }
            }
        }
    }

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, const CBlockIndex* pindexPrev, bool fProofOfStake, const std::vector<CPubKey>* pvValidMiners, const CPubKey* pCoordinatorKey)
{
    if (!pindexPrev) return;

    int nHeight = pindexPrev->nHeight + 1;
    
    // Calculate Developer Treasury split
    CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
    CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
    CAmount nUnclaimedPartSplit = 0;

    bool hasPayment = true;
    CScript payee;
    CPubKey passiveWinnerOwner;

    if (IsModelDActive(nHeight)) {
        if (!GetModelDPassiveMasternodePayee(nHeight, pindexPrev, payee, pcoinsTip, &passiveWinnerOwner)) {
            LogPrint(BCLog::MASTERNODE,"CreateNewBlock: Failed to detect on-chain masternode to pay\n");
            hasPayment = false;
        }
    } else {
        hasPayment = false;
    }

    if (hasPayment) {

        if (IsModelDActive(nHeight)) {
            CAmount nBlockVal = nBlockValActual - nTreasurySplit;
            CAmount nMNSplit = nBlockVal * 5 / 100; // Masternode passive winner gets 5%
            CAmount nLLMQSplitTotal = nBlockVal * 10 / 100; // LLMQ members share 10%
            CAmount nPartSplitTotal = nBlockVal * 60 / 100; // Validator participants share 60% (4.00% equal share across 15 seats: 8 MN + 2 Coin-Lock + 5 Open PoW)
            CAmount totalMasternodePayments = nMNSplit;

            dip3::CDeterministicMNList dip3List;
            const dip3::CDeterministicMNList* pDip3List = nullptr;
            if (deterministicMNManager && pindexPrev) {
                dip3List = deterministicMNManager->GetListForBlock(pindexPrev);
                pDip3List = &dip3List;
            }

            std::vector<std::pair<CScript, CAmount>> vAllModelDPayments;

            // 0. Passive Masternode Split (5%, split if operator reward configured)
            AppendDip3OrDirectPayment(pDip3List, passiveWinnerOwner, payee, nMNSplit, vAllModelDPayments);

            // 1. LLMQ Quorum Split (10% total; signer-only from prev block)
            std::vector<std::pair<CScript, CAmount>> vLlmqPayments;
            CAmount nPaidLlmqTotal = 0;
            ComputeModelDLlmqPayments(nHeight, pindexPrev, pcoinsTip, pDip3List, nLLMQSplitTotal, vLlmqPayments, nPaidLlmqTotal);
            vAllModelDPayments.insert(vAllModelDPayments.end(), vLlmqPayments.begin(), vLlmqPayments.end());
            totalMasternodePayments += nPaidLlmqTotal;

            // 2. Participant Validators Split (60% total)
            uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vMiners;
            CPubKey coordinator;
            SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator, nHeight, pcoinsTip);
            const std::vector<CPubKey>& vEffectivePartMiners = pvValidMiners ? *pvValidMiners : vMiners;

            std::vector<std::pair<CScript, CAmount>> vPartPayments;
            CAmount nPaidPartTotal = 0;
            ComputeModelDValidatorSeatPayments(
                nHeight, nBlockVal, nPartSplitTotal, vMiners, vEffectivePartMiners,
                pcoinsTip, pindexPrev, vPartPayments, nPaidPartTotal);
            vAllModelDPayments.insert(vAllModelDPayments.end(), vPartPayments.begin(), vPartPayments.end());

            // 3. PoS Signing Coordinator Share (3% of net pool; 22% remains with staker in Model D)
            CAmount nCoordPoSShare = 0;
            if (fProofOfStake) {
                const CPubKey& effectiveCoord = (pCoordinatorKey && pCoordinatorKey->IsValid()) ? *pCoordinatorKey : coordinator;
                if (effectiveCoord.IsValid()) {
                    nCoordPoSShare = nBlockVal * 3 / 100;
                    CScript coordFbScript = ResolveCollateralOrPubKeyScript(effectiveCoord, nullptr, nHeight, pcoinsTip);
                    AppendDip3OrDirectPayment(pDip3List, effectiveCoord, coordFbScript, nCoordPoSShare, vAllModelDPayments);
                }
            }

            nUnclaimedPartSplit = (nPartSplitTotal - nPaidPartTotal) + (nLLMQSplitTotal - nPaidLlmqTotal);
            const CAmount totalModelDSplits = nMNSplit + nLLMQSplitTotal + nPartSplitTotal;
            totalMasternodePayments = nMNSplit + nPaidLlmqTotal + nPaidPartTotal + nCoordPoSShare;

            if (fProofOfStake) {
                // 4. Create outputs and subtract from staker
                unsigned int i = txNew.vout.size();
                size_t nExtraCount = vAllModelDPayments.size();
                txNew.vout.resize(i + nExtraCount);
                for (size_t idx = 0; idx < vAllModelDPayments.size(); ++idx) {
                    txNew.vout[i + idx].scriptPubKey = vAllModelDPayments[idx].first;
                    txNew.vout[i + idx].nValue = vAllModelDPayments[idx].second;
                }

                CAmount totalAmountToSubtract = totalModelDSplits + nCoordPoSShare + nTreasurySplit;
                unsigned int outputs = i - 1;
                CAmount splitToSubtract = totalAmountToSubtract / outputs;
                CAmount remainderToSubtract = totalAmountToSubtract - (splitToSubtract * outputs);
                for (unsigned int j = 1; j <= outputs; j++) {
                    txNew.vout[j].nValue -= splitToSubtract;
                }
                txNew.vout[outputs].nValue -= remainderToSubtract;
            } else {
                size_t nSize = 1 + vAllModelDPayments.size();
                txNew.vout.resize(nSize);
                for (size_t idx = 0; idx < vAllModelDPayments.size(); ++idx) {
                    txNew.vout[1 + idx].scriptPubKey = vAllModelDPayments[idx].first;
                    txNew.vout[1 + idx].nValue = vAllModelDPayments[idx].second;
                }
                txNew.vout[0].nValue = nBlockVal - totalModelDSplits; // Exactly 25% (nCoordSplit)
            }

            CTxDestination address1;
            ExtractDestination(payee, address1);
            LogPrint(BCLog::MASTERNODE,"Model D payment: passive MN %s to %s, total splits %s\n", 
                     FormatMoney(nMNSplit).c_str(), EncodeDestination(address1).c_str(), FormatMoney(totalMasternodePayments).c_str());
        }
    } else if (IsAdamValidatorSplitActive(nHeight) && (!IsModelDActive(nHeight) || !Params().IsRegTestNet())) {
        // Pre-Model-D cooperative ADAM split: 25% Producer (20% Staker + 5% Coordinator in PoS) + 75% Active Validators;
        // unclaimed seats -> 50% Burn + 50% Treasury.
        CAmount nBlockVal = nBlockValActual - nTreasurySplit;
        CAmount nPartSplitTotal = nBlockVal * 75 / 100;
        uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
        std::vector<CPubKey> vMiners;
        CPubKey coordinator;
        SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator, nHeight, pcoinsTip);

        if (!vMiners.empty()) {
            dip3::CDeterministicMNList dip3List;
            const dip3::CDeterministicMNList* pDip3List = nullptr;
            if (deterministicMNManager) {
                dip3List = deterministicMNManager->GetListForBlock(pindexPrev);
                pDip3List = &dip3List;
            }
            const std::vector<CPubKey>& vEffectivePartMiners = pvValidMiners ? *pvValidMiners : vMiners;
            std::vector<std::pair<CScript, CAmount>> vPartPayments;
            CAmount nPaidPartTotal = 0;
            ComputeModelDValidatorSeatPayments(
                nHeight, nBlockVal, nPartSplitTotal, vMiners, vEffectivePartMiners,
                pcoinsTip, pindexPrev, vPartPayments, nPaidPartTotal);
            nUnclaimedPartSplit = nPartSplitTotal - nPaidPartTotal;

            CAmount nCoordPoSShare = 0;
            if (fProofOfStake) {
                const CPubKey& effectiveCoord = (pCoordinatorKey && pCoordinatorKey->IsValid()) ? *pCoordinatorKey : coordinator;
                if (effectiveCoord.IsValid()) {
                    nCoordPoSShare = nBlockVal * 5 / 100;
                    CScript coordFbScript = ResolveCollateralOrPubKeyScript(effectiveCoord, nullptr, nHeight, pcoinsTip);
                    AppendDip3OrDirectPayment(pDip3List, effectiveCoord, coordFbScript, nCoordPoSShare, vPartPayments);
                }
            }

            if (fProofOfStake) {
                unsigned int i = txNew.vout.size();
                txNew.vout.resize(i + vPartPayments.size());
                for (size_t idx = 0; idx < vPartPayments.size(); ++idx) {
                    txNew.vout[i + idx].scriptPubKey = vPartPayments[idx].first;
                    txNew.vout[i + idx].nValue = vPartPayments[idx].second;
                }
                if (i >= 2) {
                    CAmount totalAmountToSubtract = nPartSplitTotal + nCoordPoSShare + nTreasurySplit;
                    unsigned int outputs = i - 1;
                    CAmount splitToSubtract = totalAmountToSubtract / outputs;
                    CAmount remainderToSubtract = totalAmountToSubtract - (splitToSubtract * outputs);
                    for (unsigned int j = 1; j <= outputs; j++) {
                        txNew.vout[j].nValue -= splitToSubtract;
                    }
                    txNew.vout[outputs].nValue -= remainderToSubtract;
                }
            } else {
                txNew.vout.resize(1 + vPartPayments.size());
                for (size_t idx = 0; idx < vPartPayments.size(); ++idx) {
                    txNew.vout[1 + idx].scriptPubKey = vPartPayments[idx].first;
                    txNew.vout[1 + idx].nValue = vPartPayments[idx].second;
                }
                txNew.vout[0].nValue = nBlockVal - nPartSplitTotal; // 25% Producer under UPGRADE_FAIRNESS (60% legacy)
            }
        } else {
            if (fProofOfStake) {
                if (txNew.vout.size() >= 2) {
                    unsigned int outputs = txNew.vout.size() - 1;
                    CAmount tfSplit = nTreasurySplit / outputs;
                    CAmount tfRemainder = nTreasurySplit - (tfSplit * outputs);
                    for (unsigned int j = 1; j <= outputs; j++) {
                        txNew.vout[j].nValue -= tfSplit;
                    }
                    txNew.vout[outputs].nValue -= tfRemainder;
                }
            } else {
                txNew.vout[0].nValue = nBlockValActual - nTreasurySplit;
            }
        }
    } else {
        // If there is no payee detected, we still need to subtract treasury from miner/staker
        if (fProofOfStake) {
            if (txNew.vout.size() >= 2) {
                unsigned int outputs = txNew.vout.size() - 1;
                CAmount tfSplit = nTreasurySplit / outputs;
                CAmount tfRemainder = nTreasurySplit - (tfSplit * outputs);
                for (unsigned int j = 1; j <= outputs; j++) {
                    txNew.vout[j].nValue -= tfSplit;
                }
                txNew.vout[outputs].nValue -= tfRemainder;
            }
        } else {
            txNew.vout[0].nValue = nBlockValActual - nTreasurySplit;
        }
    }

    // Append Developer Treasury and Protocol Burn outputs
    if (nHeight > 1) {
        const bool hasBurnAddr = !Params().GetConsensus().mBurnAddresses.empty();
        const CAmount nProtocolBurnSplit = hasBurnAddr ? (nUnclaimedPartSplit / 2) : 0;
        const CAmount nUnclaimedTreasurySplit = nUnclaimedPartSplit - nProtocolBurnSplit;

        CTxOut outTreasury;
        outTreasury.scriptPubKey = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        outTreasury.nValue = nTreasurySplit + nUnclaimedTreasurySplit;
        txNew.vout.push_back(outTreasury);

        if (nProtocolBurnSplit > 0) {
            const std::string& strBurnAddr = Params().GetConsensus().mBurnAddresses.begin()->first;
            CTxOut outBurn;
            outBurn.scriptPubKey = GetScriptForDestination(DecodeDestination(strBurnAddr));
            outBurn.nValue = nProtocolBurnSplit;
            txNew.vout.push_back(outBurn);
        }
    }
}

