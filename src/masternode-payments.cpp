// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "masternode-payments.h"
#include "adam.h"
#include "llmq.h"
#include "addrman.h"
#include "chainparams.h"
#include "base58.h"
#include "fs.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "netmessagemaker.h"
#include "spork.h"
#include "sync.h"
#include "util.h"
#include "utilmoneystr.h"
#include "core_io.h"


/** Object for who's going to get paid on which blocks */
CMasternodePayments masternodePayments;

RecursiveMutex cs_vecPayments;
RecursiveMutex cs_mapMasternodeBlocks;
RecursiveMutex cs_mapMasternodePayeeVotes;

//
// CMasternodePaymentDB
//

CMasternodePaymentDB::CMasternodePaymentDB()
{
    pathDB = GetDataDir() / "mnpayments.dat";
    strMagicMessage = "MasternodePayments";
}

bool CMasternodePaymentDB::Write(const CMasternodePayments& objToSave)
{
    int64_t nStart = GetTimeMillis();

    // serialize, checksum data up to that point, then append checksum
    CDataStream ssObj(SER_DISK, CLIENT_VERSION);
    ssObj << strMagicMessage;                   // masternode cache file specific magic message
    ssObj << FLATDATA(Params().MessageStart()); // network specific magic number
    ssObj << objToSave;
    uint256 hash = Hash(ssObj.begin(), ssObj.end());
    ssObj << hash;

    // open output file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "wb");
    CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
    if (fileout.IsNull())
        return error("%s : Failed to open file %s", __func__, pathDB.string());

    // Write and commit header, data
    try {
        fileout << ssObj;
    } catch (const std::exception& e) {
        return error("%s : Serialize or I/O error - %s", __func__, e.what());
    }
    fileout.fclose();

    LogPrint(BCLog::MASTERNODE,"Written info to mnpayments.dat  %dms\n", GetTimeMillis() - nStart);

    return true;
}

CMasternodePaymentDB::ReadResult CMasternodePaymentDB::Read(CMasternodePayments& objToLoad, bool fDryRun)
{
    int64_t nStart = GetTimeMillis();
    // open input file, and associate with CAutoFile
    FILE* file = fsbridge::fopen(pathDB, "rb");
    CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
    if (filein.IsNull()) {
        error("%s : Failed to open file %s", __func__, pathDB.string());
        return FileError;
    }

    // use file size to size memory buffer
    int fileSize = fs::file_size(pathDB);
    int dataSize = fileSize - sizeof(uint256);
    // Don't try to resize to a negative number if file is small
    if (dataSize < 0)
        dataSize = 0;
    std::vector<unsigned char> vchData;
    vchData.resize(dataSize);
    uint256 hashIn;

    // read data and checksum from file
    try {
        filein.read((char*)&vchData[0], dataSize);
        filein >> hashIn;
    } catch (const std::exception& e) {
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return HashReadError;
    }
    filein.fclose();

    CDataStream ssObj(vchData, SER_DISK, CLIENT_VERSION);

    // verify stored checksum matches input data
    uint256 hashTmp = Hash(ssObj.begin(), ssObj.end());
    if (hashIn != hashTmp) {
        error("%s : Checksum mismatch, data corrupted", __func__);
        return IncorrectHash;
    }

    unsigned char pchMsgTmp[4];
    std::string strMagicMessageTmp;
    try {
        // de-serialize file header (masternode cache file specific magic message) and ..
        ssObj >> strMagicMessageTmp;

        // ... verify the message matches predefined one
        if (strMagicMessage != strMagicMessageTmp) {
            error("%s : Invalid masternode payement cache magic message", __func__);
            return IncorrectMagicMessage;
        }


        // de-serialize file header (network specific magic number) and ..
        ssObj >> FLATDATA(pchMsgTmp);

        // ... verify the network matches ours
        if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp))) {
            error("%s : Invalid network magic number", __func__);
            return IncorrectMagicNumber;
        }

        // de-serialize data into CMasternodePayments object
        ssObj >> objToLoad;
    } catch (const std::exception& e) {
        objToLoad.Clear();
        error("%s : Deserialize or I/O error - %s", __func__, e.what());
        return IncorrectFormat;
    }

    LogPrint(BCLog::MASTERNODE,"Loaded info from mnpayments.dat  %dms\n", GetTimeMillis() - nStart);
    LogPrint(BCLog::MASTERNODE,"  %s\n", objToLoad.ToString());
    if (!fDryRun) {
        LogPrint(BCLog::MASTERNODE,"Masternode payments manager - cleaning....\n");
        objToLoad.CleanPaymentList();
        LogPrint(BCLog::MASTERNODE,"Masternode payments manager - result:\n");
        LogPrint(BCLog::MASTERNODE,"  %s\n", objToLoad.ToString());
    }

    return Ok;
}

uint256 CMasternodePaymentWinner::GetHash() const
{
    CHashWriter ss(SER_GETHASH, PROTOCOL_VERSION);
    ss << std::vector<unsigned char>(payee.begin(), payee.end());
    ss << nBlockHeight;
    ss << vinMasternode.prevout;
    return ss.GetHash();
}

std::string CMasternodePaymentWinner::GetStrMessage() const
{
    if (Params().GetConsensus().NetworkUpgradeActive(chainActive.Tip()->nHeight, Consensus::UPGRADE_TIME_PROTOCOL_V2)) {
        return vinMasternode.prevout.ToStringShort() + std::to_string(nBlockHeight) + HexStr(payee);
    } else {
        return vinMasternode.prevout.ToStringShort() + std::to_string(nBlockHeight) + ScriptToAsmStr(payee);
    }
}

bool CMasternodePaymentWinner::IsValid(CNode* pnode, std::string& strError)
{
    CMasternode* pmn = mnodeman.Find(vinMasternode);

    if (!pmn) {
        strError = strprintf("Unknown Masternode %s", vinMasternode.prevout.ToStringShort());
        LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
        mnodeman.AskForMN(pnode, vinMasternode);
        return false;
    }

    if (pmn->protocolVersion < ActiveProtocol()) {
        strError = strprintf("Masternode protocol too old %d - req %d", pmn->protocolVersion, ActiveProtocol());
        LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
        return false;
    }

    if (sporkManager.IsSporkActive(SPORK_102_FORCE_ENABLED_MASTERNODE)) {
        if (pmn->Status() != "ENABLED") {
            strError = strprintf("Masternode is not in ENABLED state - Status(): %d", pmn->Status());
            LogPrint(BCLog::MASTERNODE, "CMasternodePaymentWinner::IsValid - Force masternode requirement to have ENABLED status instead of ACTIVE - %s\n", strError);
            return false;
        }
    }

    int n = mnodeman.GetMasternodeRank(vinMasternode, nBlockHeight - 100, ActiveProtocol());

    if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
        //It's common to have masternodes mistakenly think they are in the top 10
        // We don't want to print all of these messages, or punish them unless they're way off
        if (n > MNPAYMENTS_SIGNATURES_TOTAL * 2) {
            strError = strprintf("Masternode not in the top %d (%d)", MNPAYMENTS_SIGNATURES_TOTAL * 2, n);
            LogPrint(BCLog::MASTERNODE,"CMasternodePaymentWinner::IsValid - %s\n", strError);
            //if (masternodeSync.IsSynced()) Misbehaving(pnode->GetId(), 20);
        }
        return false;
    }

    return true;
}

void CMasternodePaymentWinner::Relay()
{
    CInv inv(MSG_MASTERNODE_WINNER, GetHash());
    g_connman->RelayInv(inv);
}

void DumpMasternodePayments()
{
    int64_t nStart = GetTimeMillis();

    CMasternodePaymentDB paymentdb;
    CMasternodePayments tempPayments;

    LogPrint(BCLog::MASTERNODE,"Verifying mnpayments.dat format...\n");
    CMasternodePaymentDB::ReadResult readResult = paymentdb.Read(tempPayments, true);
    // there was an error and it was not an error on file opening => do not proceed
    if (readResult == CMasternodePaymentDB::FileError)
        LogPrint(BCLog::MASTERNODE,"Missing budgets file - mnpayments.dat, will try to recreate\n");
    else if (readResult != CMasternodePaymentDB::Ok) {
        LogPrint(BCLog::MASTERNODE,"Error reading mnpayments.dat: ");
        if (readResult == CMasternodePaymentDB::IncorrectFormat)
            LogPrint(BCLog::MASTERNODE,"magic is ok but data has invalid format, will try to recreate\n");
        else {
            LogPrint(BCLog::MASTERNODE,"file format is unknown or invalid, please fix it manually\n");
            return;
        }
    }
    LogPrint(BCLog::MASTERNODE,"Writting info to mnpayments.dat...\n");
    paymentdb.Write(masternodePayments);

    LogPrint(BCLog::MASTERNODE,"Budget dump finished  %dms\n", GetTimeMillis() - nStart);
}

bool IsBlockValueValid(int nHeight, CAmount nExpectedValue, CAmount nMinted)
{
    // No superblock, regular check
    return nMinted <= nExpectedValue;
}

bool IsBlockPayeeValid(const CBlock& block, int nBlockHeight)
{
    // Validate Developer Treasury and Bootstrap Faucet splits for all blocks starting after block 199
    if (nBlockHeight > 199) {
        CAmount nBlockValActual = CMasternode::GetBlockValue(nBlockHeight);
        CAmount nExpectedTreasury = nBlockValActual * 7 / 100;
        CAmount nExpectedFaucet = (nBlockHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;

        const bool fPoS = block.IsProofOfStake();
        if (block.vtx.size() < (fPoS ? 2 : 1)) {
            return false;
        }
        const CTransaction& txNew = (fPoS ? block.vtx[1] : block.vtx[0]);

        // Validate Treasury output
        bool foundTreasury = false;
        CScript treasuryScript = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        for (const auto& out : txNew.vout) {
            if (out.scriptPubKey == treasuryScript && out.nValue == nExpectedTreasury) {
                foundTreasury = true;
                break;
            }
        }
        if (!foundTreasury) {
            LogPrintf("%s : Missing or invalid Developer Treasury payment of %s to address %s\n",
                      __func__, FormatMoney(nExpectedTreasury).c_str(), Params().DeveloperFundAddress().c_str());
            return false;
        }

        // Validate Faucet output
        if (nExpectedFaucet > 0) {
            bool foundFaucet = false;
            CScript faucetScript = GetScriptForDestination(DecodeDestination(Params().BootstrapFaucetAddress()));
            for (const auto& out : txNew.vout) {
                if (out.scriptPubKey == faucetScript && out.nValue == nExpectedFaucet) {
                    foundFaucet = true;
                    break;
                }
            }
            if (!foundFaucet) {
                LogPrintf("%s : Missing or invalid Bootstrap Faucet payment of %s to address %s\n",
                          __func__, FormatMoney(nExpectedFaucet).c_str(), Params().BootstrapFaucetAddress().c_str());
                return false;
            }
        }
    }

    if (nBlockHeight < 1200 || mnodeman.CountEnabled() == 0) {
        return true;
    }

    if (!masternodeSync.IsSynced()) { //there is no budget data to use to check anything -- find the longest chain
        LogPrint(BCLog::MASTERNODE, "Client not synced, skipping block payee checks\n");
        return true;
    }

    const bool fPoS = block.IsProofOfStake();
    if (block.vtx.size() < (fPoS ? 2 : 1)) {
        return false;
    }
    const CTransaction& txNew = (fPoS ? block.vtx[1] : block.vtx[0]);

    bool fMasternodePaymentValid = masternodePayments.IsTransactionValid(txNew, nBlockHeight);
    if (!fMasternodePaymentValid) {
        LogPrint(BCLog::MASTERNODE,"Invalid mn payment detected %s\n", txNew.ToString().c_str());
        if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
            return false;
    }

    // Model D active/participant payments validation
    if (IsModelDActive(nBlockHeight)) {
        CBlockIndex* pindexPrev = nullptr;
        {
            LOCK(cs_main);
            auto it = mapBlockIndex.find(block.hashPrevBlock);
            if (it != mapBlockIndex.end()) {
                pindexPrev = it->second;
            }
        }
        if (!pindexPrev) {
            LogPrintf("%s : Failed to find parent block index for block %s\n", __func__, block.GetHash().ToString().c_str());
            if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
                return false;
        } else {
            CAmount nBlockVal = CMasternode::GetBlockValue(nBlockHeight);
            CAmount nLLMQSplitTotal = nBlockVal * 10 / 100;
            CAmount nPartSplitTotal = nBlockVal * 25 / 100;

            // 1. Validate LLMQ Quorum Split
            llmq::CQuorum quorum = llmq::GetActiveQuorum(nBlockHeight);
            std::vector<CScript> vLlmqPayees;
            for (const auto& member : quorum.members) {
                CMasternode* pmn = mnodeman.Find(member.pubKeyMasternode);
                if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                    vLlmqPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                }
            }
            if (!vLlmqPayees.empty()) {
                CAmount nLLMQPaymentPerMember = nLLMQSplitTotal / vLlmqPayees.size();
                CAmount nLLMQRemainder = nLLMQSplitTotal % vLlmqPayees.size();
                for (size_t idx = 0; idx < vLlmqPayees.size(); ++idx) {
                    CAmount expectedAmt = nLLMQPaymentPerMember + (idx == vLlmqPayees.size() - 1 ? nLLMQRemainder : 0);
                    bool foundLlmqPayee = false;
                    for (const auto& out : txNew.vout) {
                        if (out.scriptPubKey == vLlmqPayees[idx] && out.nValue == expectedAmt) {
                            foundLlmqPayee = true;
                            break;
                        }
                    }
                    if (!foundLlmqPayee) {
                        LogPrintf("%s : Missing LLMQ payment of %s to script %s\n",
                                  __func__, FormatMoney(expectedAmt).c_str(), HexStr(vLlmqPayees[idx]).c_str());
                        if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
                            return false;
                    }
                }
            }

            // 2. Validate Participant Validators Split
            CScript producerScript = txNew.vout[txNew.IsCoinStake() ? 1 : 0].scriptPubKey;
            uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vMiners;
            CPubKey coordinator;
            SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator);

            std::vector<CScript> vPartPayees;
            if (txNew.IsCoinStake()) {
                for (const auto& minerKey : vMiners) {
                    CMasternode* pmn = mnodeman.Find(minerKey);
                    if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                        CScript minerScript = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
                        if (minerScript != producerScript) {
                            vPartPayees.push_back(minerScript);
                        }
                    }
                }
            } else {
                for (const auto& minerKey : vMiners) {
                    CMasternode* pmn = mnodeman.Find(minerKey);
                    if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                        vPartPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                    }
                }
            }

            if (!vPartPayees.empty()) {
                CAmount nPartPaymentPerMember = nPartSplitTotal / vPartPayees.size();
                CAmount nPartRemainder = nPartSplitTotal % vPartPayees.size();
                for (size_t idx = 0; idx < vPartPayees.size(); ++idx) {
                    CAmount expectedAmt = nPartPaymentPerMember + (idx == vPartPayees.size() - 1 ? nPartRemainder : 0);
                    bool foundPartPayee = false;
                    for (const auto& out : txNew.vout) {
                        if (out.scriptPubKey == vPartPayees[idx] && out.nValue == expectedAmt) {
                            foundPartPayee = true;
                            break;
                        }
                    }
                    if (!foundPartPayee) {
                        LogPrintf("%s : Missing validator participant payment of %s to script %s\n",
                                  __func__, FormatMoney(expectedAmt).c_str(), HexStr(vPartPayees[idx]).c_str());
                        if (sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT))
                            return false;
                    }
                }
            }
        }
    }

    if (!fMasternodePaymentValid && !sporkManager.IsSporkActive(SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT)) {
        LogPrint(BCLog::MASTERNODE,"Masternode payment enforcement is disabled, accepting block\n");
    }

    return true;
}


void FillBlockPayee(CMutableTransaction& txNew, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    masternodePayments.FillBlockPayee(txNew, pindexPrev, fProofOfStake);
}

std::string GetRequiredPaymentsString(int nBlockHeight)
{
    return masternodePayments.GetRequiredPaymentsString(nBlockHeight);
}

void CMasternodePayments::FillBlockPayee(CMutableTransaction& txNew, const CBlockIndex* pindexPrev, bool fProofOfStake)
{
    if (!pindexPrev) return;

    int nHeight = pindexPrev->nHeight + 1;
    
    // Calculate Treasury and Faucet splits
    CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
    CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
    CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
    CAmount nTotalTreasuryFaucet = nTreasurySplit + nFaucetSplit;

    bool hasPayment = true;
    CScript payee;

    //spork
    if (!masternodePayments.GetBlockPayee(nHeight, payee)) {
        //no masternode detected
        CMasternode* winningNode = mnodeman.GetCurrentMasterNode(1);
        if (winningNode) {
            payee = GetScriptForDestination(winningNode->pubKeyCollateralAddress.GetID());
        } else {
            LogPrint(BCLog::MASTERNODE,"CreateNewBlock: Failed to detect masternode to pay\n");
            hasPayment = false;
        }
    }

    if (hasPayment) {
        CAmount masternodePayment = CMasternode::GetMasternodePayment(nHeight);

        if (IsModelDActive(nHeight)) {
            CAmount nBlockVal = nBlockValActual - nTotalTreasuryFaucet;
            CAmount nMNSplit = nBlockVal * 50 / 100; // Masternode passive winner gets 50%
            CAmount nLLMQSplitTotal = nBlockVal * 10 / 100; // LLMQ members share 10%
            CAmount nPartSplitTotal = nBlockVal * 25 / 100; // Validator participants share 25%
            CAmount totalMasternodePayments = nMNSplit;

            std::vector<std::pair<CScript, CAmount>> vExtraPayments;

            // 1. LLMQ Quorum Split (10% total)
            llmq::CQuorum quorum = llmq::GetActiveQuorum(nHeight);
            std::vector<CScript> vLlmqPayees;
            for (const auto& member : quorum.members) {
                CMasternode* pmn = mnodeman.Find(member.pubKeyMasternode);
                if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                    vLlmqPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                }
            }
            if (!vLlmqPayees.empty()) {
                CAmount nLLMQPaymentPerMember = nLLMQSplitTotal / vLlmqPayees.size();
                CAmount nLLMQRemainder = nLLMQSplitTotal % vLlmqPayees.size();
                for (size_t idx = 0; idx < vLlmqPayees.size(); ++idx) {
                    CAmount amt = nLLMQPaymentPerMember + (idx == vLlmqPayees.size() - 1 ? nLLMQRemainder : 0);
                    vExtraPayments.push_back(std::make_pair(vLlmqPayees[idx], amt));
                }
                totalMasternodePayments += nLLMQSplitTotal;
            }

            // 2. Participant Validators Split
            CScript producerScript = txNew.vout[fProofOfStake ? 1 : 0].scriptPubKey;
            uint256 hashAdamSeed = GetAdamSeed(pindexPrev);
            std::vector<CPubKey> vMiners;
            CPubKey coordinator;
            SelectAdamNodes(hashAdamSeed, Params().GetConsensus(), vMiners, coordinator);

            if (fProofOfStake) {
                std::vector<CScript> vPartPayees;
                for (const auto& minerKey : vMiners) {
                    CMasternode* pmn = mnodeman.Find(minerKey);
                    if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                        CScript minerScript = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
                        if (minerScript != producerScript) {
                            vPartPayees.push_back(minerScript);
                        }
                    }
                }
                if (!vPartPayees.empty()) {
                    CAmount nPartPaymentPerMember = nPartSplitTotal / vPartPayees.size();
                    CAmount nPartRemainder = nPartSplitTotal % vPartPayees.size();
                    for (size_t idx = 0; idx < vPartPayees.size(); ++idx) {
                        CAmount amt = nPartPaymentPerMember + (idx == vPartPayees.size() - 1 ? nPartRemainder : 0);
                        vExtraPayments.push_back(std::make_pair(vPartPayees[idx], amt));
                    }
                    totalMasternodePayments += nPartSplitTotal;
                }

                // 3. Create outputs and subtract from staker
                unsigned int i = txNew.vout.size();
                size_t nExtraCount = 1 + vExtraPayments.size();
                txNew.vout.resize(i + nExtraCount);
                txNew.vout[i].scriptPubKey = payee;
                txNew.vout[i].nValue = nMNSplit;
                for (size_t idx = 0; idx < vExtraPayments.size(); ++idx) {
                    txNew.vout[i + 1 + idx].scriptPubKey = vExtraPayments[idx].first;
                    txNew.vout[i + 1 + idx].nValue = vExtraPayments[idx].second;
                }

                CAmount totalAmountToSubtract = totalMasternodePayments + nTotalTreasuryFaucet;
                unsigned int outputs = i - 1;
                CAmount splitToSubtract = totalAmountToSubtract / outputs;
                CAmount remainderToSubtract = totalAmountToSubtract - (splitToSubtract * outputs);
                for (unsigned int j = 1; j <= outputs; j++) {
                    txNew.vout[j].nValue -= splitToSubtract;
                }
                txNew.vout[outputs].nValue -= remainderToSubtract;
            } else {
                CAmount nPoWMinersSplitTotal = nBlockVal * 25 / 100; // 13 miners split 25% total
                std::vector<CScript> vPartPayees;
                for (const auto& minerKey : vMiners) {
                    CMasternode* pmn = mnodeman.Find(minerKey);
                    if (pmn && pmn->pubKeyCollateralAddress.IsValid()) {
                        vPartPayees.push_back(GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID()));
                    }
                }
                if (!vPartPayees.empty()) {
                    CAmount nPartPaymentPerMember = nPoWMinersSplitTotal / vPartPayees.size();
                    CAmount nPartRemainder = nPoWMinersSplitTotal % vPartPayees.size();
                    for (size_t idx = 0; idx < vPartPayees.size(); ++idx) {
                        CAmount amt = nPartPaymentPerMember + (idx == vPartPayees.size() - 1 ? nPartRemainder : 0);
                        vExtraPayments.push_back(std::make_pair(vPartPayees[idx], amt));
                    }
                    totalMasternodePayments += nPoWMinersSplitTotal;
                }

                size_t nSize = 2 + vExtraPayments.size();
                txNew.vout.resize(nSize);
                txNew.vout[1].scriptPubKey = payee;
                txNew.vout[1].nValue = nMNSplit;
                for (size_t idx = 0; idx < vExtraPayments.size(); ++idx) {
                    txNew.vout[2 + idx].scriptPubKey = vExtraPayments[idx].first;
                    txNew.vout[2 + idx].nValue = vExtraPayments[idx].second;
                }
                txNew.vout[0].nValue = nBlockVal - totalMasternodePayments; // Exactly 15% (nCoordSplit)
            }

            CTxDestination address1;
            ExtractDestination(payee, address1);
            LogPrint(BCLog::MASTERNODE,"Model D payment: passive MN %s to %s, total splits %s\n", 
                     FormatMoney(nMNSplit).c_str(), EncodeDestination(address1).c_str(), FormatMoney(totalMasternodePayments).c_str());
        } else {
            // Legacy/standard payout split
            if (fProofOfStake) {
                unsigned int i = txNew.vout.size();
                txNew.vout.resize(i + 1);
                txNew.vout[i].scriptPubKey = payee;
                txNew.vout[i].nValue = masternodePayment;

                CAmount totalAmountToSubtract = masternodePayment + nTotalTreasuryFaucet;
                if (i == 2) {
                    txNew.vout[i - 1].nValue -= totalAmountToSubtract;
                } else if (i > 2) {
                    unsigned int outputs = i-1;
                    CAmount mnPaymentSplit = totalAmountToSubtract / outputs;
                    CAmount mnPaymentRemainder = totalAmountToSubtract - (mnPaymentSplit * outputs);
                    for (unsigned int j=1; j<=outputs; j++) {
                        txNew.vout[j].nValue -= mnPaymentSplit;
                    }
                    txNew.vout[outputs].nValue -= mnPaymentRemainder;
                }
            } else {
                txNew.vout.resize(2);
                txNew.vout[1].scriptPubKey = payee;
                txNew.vout[1].nValue = masternodePayment;
                txNew.vout[0].nValue = nBlockValActual - masternodePayment - nTotalTreasuryFaucet;
            }

            CTxDestination address1;
            ExtractDestination(payee, address1);
            LogPrint(BCLog::MASTERNODE,"Masternode payment of %s to %s\n", FormatMoney(masternodePayment).c_str(), EncodeDestination(address1).c_str());
        }
    } else {
        // If there is no payee detected, we still need to subtract treasury/faucet from miner/staker
        if (fProofOfStake) {
            if (txNew.vout.size() >= 2) {
                unsigned int outputs = txNew.vout.size() - 1;
                CAmount tfSplit = nTotalTreasuryFaucet / outputs;
                CAmount tfRemainder = nTotalTreasuryFaucet - (tfSplit * outputs);
                for (unsigned int j = 1; j <= outputs; j++) {
                    txNew.vout[j].nValue -= tfSplit;
                }
                txNew.vout[outputs].nValue -= tfRemainder;
            }
        } else {
            txNew.vout[0].nValue = nBlockValActual - nTotalTreasuryFaucet;
        }
    }

    // Append Developer Treasury and Bootstrap Faucet outputs
    if (nHeight > 1) {
        int nTreasuryIdx = txNew.vout.size();
        txNew.vout.resize(nTreasuryIdx + (nFaucetSplit > 0 ? 2 : 1));
        
        // Treasury output
        txNew.vout[nTreasuryIdx].scriptPubKey = GetScriptForDestination(DecodeDestination(Params().DeveloperFundAddress()));
        txNew.vout[nTreasuryIdx].nValue = nTreasurySplit;
        
        // Faucet output if applicable
        if (nFaucetSplit > 0) {
            txNew.vout[nTreasuryIdx + 1].scriptPubKey = GetScriptForDestination(DecodeDestination(Params().BootstrapFaucetAddress()));
            txNew.vout[nTreasuryIdx + 1].nValue = nFaucetSplit;
        }
    }
}

void CMasternodePayments::ProcessMessageMasternodePayments(CNode* pfrom, std::string& strCommand, CDataStream& vRecv)
{
    if (!masternodeSync.IsBlockchainSynced()) return;

    if (fLiteMode) return; //disable all Masternode related functionality


    if (strCommand == NetMsgType::GETMNWINNERS) { //Masternode Payments Request Sync
        if (fLiteMode) return;   //disable all Masternode related functionality

        int nCountNeeded;
        vRecv >> nCountNeeded;

        if (Params().NetworkID() == CBaseChainParams::MAIN) {
            if (pfrom->HasFulfilledRequest(NetMsgType::GETMNWINNERS)) {
                LogPrintf("CMasternodePayments::ProcessMessageMasternodePayments() : mnget - peer already asked me for the list\n");
                return;
            }
        }

        pfrom->FulfilledRequest(NetMsgType::GETMNWINNERS);
        masternodePayments.Sync(pfrom, nCountNeeded);
        LogPrint(BCLog::MASTERNODE, "mnget - Sent Masternode winners to peer %i\n", pfrom->GetId());
    } else if (strCommand == NetMsgType::MNWINNER) { //Masternode Payments Declare Winner
        //this is required in litemodef
        CMasternodePaymentWinner winner;
        vRecv >> winner;

        if (pfrom->nVersion < ActiveProtocol()) return;

        int nHeight;
        {
            TRY_LOCK(cs_main, locked);
            if (!locked || chainActive.Tip() == NULL) return;
            nHeight = chainActive.Tip()->nHeight;
        }

        if (masternodePayments.mapMasternodePayeeVotes.count(winner.GetHash())) {
            LogPrint(BCLog::MASTERNODE, "mnw - Already seen - %s bestHeight %d\n", winner.GetHash().ToString().c_str(), nHeight);
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
            return;
        }

        int nFirstBlock = nHeight - (mnodeman.CountEnabled() * 1.25);
        if (winner.nBlockHeight < nFirstBlock || winner.nBlockHeight > nHeight + 20) {
            LogPrint(BCLog::MASTERNODE, "mnw - winner out of range - FirstBlock %d Height %d bestHeight %d\n", nFirstBlock, winner.nBlockHeight, nHeight);
            return;
        }

        // reject old signature version
        if (Params().GetConsensus().NetworkUpgradeActive(chainActive.Tip()->nHeight, Consensus::UPGRADE_TIME_PROTOCOL_V2) &&
            winner.nMessVersion != MessageVersion::MESS_VER_HASH) {
            LogPrint(BCLog::MASTERNODE, "mnw - rejecting old message version %d\n", winner.nMessVersion);
            return;
        }

        std::string strError = "";
        if (!winner.IsValid(pfrom, strError)) {
            // if(strError != "") LogPrint(BCLog::MASTERNODE,"mnw - invalid message - %s\n", strError);
            return;
        }

        if (!masternodePayments.CanVote(winner.vinMasternode.prevout, winner.nBlockHeight)) {
            //  LogPrint(BCLog::MASTERNODE,"mnw - masternode already voted - %s\n", winner.vinMasternode.prevout.ToStringShort());
            return;
        }

        if (!winner.CheckSignature()) {
            if (masternodeSync.IsSynced()) {
                LogPrintf("CMasternodePayments::ProcessMessageMasternodePayments() : mnw - invalid signature\n");
                LOCK(cs_main);
                Misbehaving(pfrom->GetId(), 20);
            }
            // it could just be a non-synced masternode
            mnodeman.AskForMN(pfrom, winner.vinMasternode);
            return;
        }

        CTxDestination address1;
        ExtractDestination(winner.payee, address1);

        LogPrint(BCLog::MASTERNODE, "mnw - winning vote - Addr %s Height %d bestHeight %d - %s\n", EncodeDestination(address1), winner.nBlockHeight, nHeight, winner.vinMasternode.prevout.ToStringShort());

        if (masternodePayments.AddWinningMasternode(winner)) {
            winner.Relay();
            masternodeSync.AddedMasternodeWinner(winner.GetHash());
        }
    }
}

bool CMasternodePayments::GetBlockPayee(int nBlockHeight, CScript& payee)
{
    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetPayee(payee);
    }

    return false;
}

// Is this masternode scheduled to get paid soon?
// -- Only look ahead up to 8 blocks to allow for propagation of the latest 2 winners
bool CMasternodePayments::IsScheduled(CMasternode& mn, int nNotBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return false;
        nHeight = chainActive.Tip()->nHeight;
    }

    CScript mnpayee;
    mnpayee = GetScriptForDestination(mn.pubKeyCollateralAddress.GetID());

    CScript payee;
    for (int64_t h = nHeight; h <= nHeight + 8; h++) {
        if (h == nNotBlockHeight) continue;
        if (mapMasternodeBlocks.count(h)) {
            if (mapMasternodeBlocks[h].GetPayee(payee)) {
                if (mnpayee == payee) {
                    return true;
                }
            }
        }
    }

    return false;
}

bool CMasternodePayments::AddWinningMasternode(CMasternodePaymentWinner& winnerIn)
{
    uint256 blockHash;
    if (!GetBlockHash(blockHash, winnerIn.nBlockHeight - 100)) {
        return false;
    }

    {
        LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

        if (mapMasternodePayeeVotes.count(winnerIn.GetHash())) {
            return false;
        }

        mapMasternodePayeeVotes[winnerIn.GetHash()] = winnerIn;

        if (!mapMasternodeBlocks.count(winnerIn.nBlockHeight)) {
            CMasternodeBlockPayees blockPayees(winnerIn.nBlockHeight);
            mapMasternodeBlocks[winnerIn.nBlockHeight] = blockPayees;
        }
    }

    mapMasternodeBlocks[winnerIn.nBlockHeight].AddPayee(winnerIn.payee, 1);

    return true;
}

bool CMasternodeBlockPayees::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_vecPayments);

    //require at least 6 signatures
    int nMaxSignatures = 0;
    for (CMasternodePayee& payee : vecPayments)
        if (payee.nVotes >= nMaxSignatures && payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED)
            nMaxSignatures = payee.nVotes;

    // if we don't have at least 6 signatures on a payee, approve whichever is the longest chain
    if (nMaxSignatures < MNPAYMENTS_SIGNATURES_REQUIRED) return true;

    std::string strPayeesPossible = "";
    CAmount requiredMasternodePayment = CMasternode::GetMasternodePayment(nBlockHeight);

    for (CMasternodePayee& payee : vecPayments) {
        bool found = false;
        for (CTxOut out : txNew.vout) {
            if (payee.scriptPubKey == out.scriptPubKey) {
                if(out.nValue == requiredMasternodePayment)
                    found = true;
                else
                    LogPrintf("%s : Masternode payment value (%s) different from required value (%s).\n",
                            __func__, FormatMoney(out.nValue).c_str(), FormatMoney(requiredMasternodePayment).c_str());
            }
        }

        if (payee.nVotes >= MNPAYMENTS_SIGNATURES_REQUIRED) {
            if (found) return true;

            CTxDestination address1;
            ExtractDestination(payee.scriptPubKey, address1);

            if (strPayeesPossible != "")
                strPayeesPossible += ",";

            strPayeesPossible += EncodeDestination(address1);
        }
    }

    LogPrint(BCLog::MASTERNODE,"CMasternodePayments::IsTransactionValid - Missing required payment of %s to %s\n", FormatMoney(requiredMasternodePayment).c_str(), strPayeesPossible.c_str());
    return false;
}

std::string CMasternodeBlockPayees::GetRequiredPaymentsString()
{
    LOCK(cs_vecPayments);

    std::string ret = "Unknown";

    for (CMasternodePayee& payee : vecPayments) {
        CTxDestination address1;
        ExtractDestination(payee.scriptPubKey, address1);
        if (ret != "Unknown") {
            ret += ", ";
        }
        ret = EncodeDestination(address1) + ":" + std::to_string(payee.nVotes);
    }

    return ret;
}

std::string CMasternodePayments::GetRequiredPaymentsString(int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].GetRequiredPaymentsString();
    }

    return "Unknown";
}

bool CMasternodePayments::IsTransactionValid(const CTransaction& txNew, int nBlockHeight)
{
    LOCK(cs_mapMasternodeBlocks);

    if (mapMasternodeBlocks.count(nBlockHeight)) {
        return mapMasternodeBlocks[nBlockHeight].IsTransactionValid(txNew, nBlockHeight);
    }

    return true;
}

void CMasternodePayments::CleanPaymentList()
{
    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    LOCK2(cs_mapMasternodePayeeVotes, cs_mapMasternodeBlocks);

    //keep up to five cycles for historical sake
    int nLimit = std::max(int(mnodeman.size() * 1.25), 1000);

    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;

        if (nHeight - winner.nBlockHeight > nLimit) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::CleanPaymentList - Removing old Masternode payment - block %d\n", winner.nBlockHeight);
            masternodeSync.mapSeenSyncMNW.erase((*it).first);
            mapMasternodePayeeVotes.erase(it++);
            mapMasternodeBlocks.erase(winner.nBlockHeight);
        } else {
            ++it;
        }
    }
}

void CMasternodePayments::ProcessBlock(int nBlockHeight)
{
    if (!fMasterNode) return;

    for (auto& activeMasternode : amnodeman.GetActiveMasternodes()) {
        if (activeMasternode.vin == nullopt) {
            LogPrint(BCLog::MASTERNODE, "%s: Active Masternode not initialized.", __func__);
            continue;
        }

        //reference node - hybrid mode

        int n = mnodeman.GetMasternodeRank(*(activeMasternode.vin), nBlockHeight - 100, ActiveProtocol());

        if (n == -1 || n == INT_MAX) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Unknown Masternode\n");
            continue;
        }

        if (n > MNPAYMENTS_SIGNATURES_TOTAL) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock - Masternode not in the top %d (%d)\n", MNPAYMENTS_SIGNATURES_TOTAL, n);
            continue;
        }

        if (nBlockHeight <= nLastBlockHeight) continue;

        CMasternodePaymentWinner newWinner(*(activeMasternode.vin));

        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Start nHeight %d - vin %s. \n", nBlockHeight, activeMasternode.vin->prevout.ToStringShort());

        // pay to the oldest MN that still had no payment but its input is old enough and it was active long enough
        int nCount = 0;
        CMasternode* pmn = mnodeman.GetNextMasternodeInQueueForPayment(nBlockHeight, true, nCount);

        if (pmn != NULL) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Found by FindOldestNotInVec \n");

            newWinner.nBlockHeight = nBlockHeight;

            CScript payee = GetScriptForDestination(pmn->pubKeyCollateralAddress.GetID());
            newWinner.AddPayee(payee);

            CTxDestination address1;
            ExtractDestination(payee, address1);

            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Winner payee %s nHeight %d. \n", EncodeDestination(address1).c_str(), newWinner.nBlockHeight);
        } else {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() Failed to find masternode to pay\n");
        }

        std::string errorMessage;
        CPubKey pubKeyMasternode;
        CKey keyMasternode;

        if (!CMessageSigner::GetKeysFromSecret(activeMasternode.strMasterNodePrivKey, keyMasternode, pubKeyMasternode)) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() - Error upon calling GetKeysFromSecret.\n");
            continue;
        }

        LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() - Signing Winner\n");
        if (newWinner.Sign(keyMasternode, pubKeyMasternode)) {
            LogPrint(BCLog::MASTERNODE, "CMasternodePayments::ProcessBlock() - AddWinningMasternode\n");

            if (AddWinningMasternode(newWinner)) {
                newWinner.Relay();
                nLastBlockHeight = nBlockHeight;
            }
        }
    }
}

void CMasternodePayments::Sync(CNode* node, int nCountNeeded)
{
    int nHeight;
    {
        TRY_LOCK(cs_main, locked);
        if (!locked || chainActive.Tip() == NULL) return;
        nHeight = chainActive.Tip()->nHeight;
    }

    LOCK(cs_mapMasternodePayeeVotes);

    int nCount = (mnodeman.CountEnabled() * 1.25);
    if (nCountNeeded > nCount) nCountNeeded = nCount;

    int nInvCount = 0;
    std::map<uint256, CMasternodePaymentWinner>::iterator it = mapMasternodePayeeVotes.begin();
    while (it != mapMasternodePayeeVotes.end()) {
        CMasternodePaymentWinner winner = (*it).second;
        if (winner.nBlockHeight >= nHeight - nCountNeeded && winner.nBlockHeight <= nHeight + 20) {
            node->PushInventory(CInv(MSG_MASTERNODE_WINNER, winner.GetHash()));
            nInvCount++;
        }
        ++it;
    }
    g_connman->PushMessage(node, CNetMsgMaker(node->GetSendVersion()).Make(NetMsgType::SYNCSTATUSCOUNT, MASTERNODE_SYNC_MNW, nInvCount));
}

std::string CMasternodePayments::ToString() const
{
    std::ostringstream info;

    info << "Votes: " << (int)mapMasternodePayeeVotes.size() << ", Blocks: " << (int)mapMasternodeBlocks.size();

    return info.str();
}
