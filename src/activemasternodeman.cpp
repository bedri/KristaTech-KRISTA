// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "activemasternode.h"
#include "activemasternodeman.h"
#include "activemasternodeconfig.h"

#include <vector>

void CActiveMasternodeMan::ManageStatus() {
    LOCK(cs);
    for(auto& amn : vActiveMasternodes) {
        amn.ManageStatus();
    }
}

void CActiveMasternodeMan::ResetStatus() {
    LOCK(cs);
    for(auto& amn : vActiveMasternodes) {
        amn.ResetStatus();
    }
}

std::vector<CActiveMasternode> CActiveMasternodeMan::GetActiveMasternodesCopy() const {
    LOCK(cs);
    return vActiveMasternodes;
}

void CActiveMasternodeMan::EnableHotColdForPubKey(const CPubKey& pubKey, const CTxIn& vin, const CService& addr) {
    LOCK(cs);
    for (auto& activeMasternode : vActiveMasternodes) {
        if (pubKey == activeMasternode.pubKeyMasternode) {
            CTxIn vinCopy(vin);
            CService addrCopy(addr);
            activeMasternode.EnableHotColdMasterNode(vinCopy, addrCopy);
        }
    }
}

bool CActiveMasternodeMan::CheckAndEnableHotCold(const CTxIn& vin, const CPubKey& pubKey, const CService& addr, int protocolVersion) {
    LOCK(cs);
    for (auto& activeMasternode : vActiveMasternodes) {
        if (fMasterNode &&
            activeMasternode.vin != nullopt &&
            vin.prevout == activeMasternode.vin->prevout &&
            pubKey == activeMasternode.pubKeyMasternode) {
            if (protocolVersion == PROTOCOL_VERSION) {
                CTxIn vinCopy(vin);
                CService addrCopy(addr);
                activeMasternode.EnableHotColdMasterNode(vinCopy, addrCopy);
            }
            return true;
        }
    }
    return false;
}

void CActiveMasternodeMan::Add(CActiveMasternode activeMasternode) {
    LOCK(cs);
    vActiveMasternodes.push_back(activeMasternode);
}
