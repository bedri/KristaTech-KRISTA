// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef ACTIVEMASTERNODES_H
#define ACTIVEMASTERNODES_H

#include "activemasternode.h"
#include "sync.h"

#include <vector>

// Responsible for activating the Masternode and pinging the network
class CActiveMasternodeMan
{
private:
    mutable RecursiveMutex cs;
    std::vector<CActiveMasternode> vActiveMasternodes;
public:
    /// Manage status of all Masternodes
    void ManageStatus();
    void ResetStatus();
    std::vector<CActiveMasternode>& GetActiveMasternodes() { return vActiveMasternodes; }
    std::vector<CActiveMasternode> GetActiveMasternodesCopy() const;
    void EnableHotColdForPubKey(const CPubKey& pubKey, const CTxIn& vin, const CService& addr);
    bool CheckAndEnableHotCold(const CTxIn& vin, const CPubKey& pubKey, const CService& addr, int protocolVersion);
    void Add(CActiveMasternode activeMasternode);
};

#endif //ACTIVEMASTERNODES_H
