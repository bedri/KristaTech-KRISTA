// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/mnmodel.h"

#include "evo/deterministicmns.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "net.h"        // for validateMasternodeIP
#include "sync.h"
#include "uint256.h"
#include "wallet/wallet.h"

MNModel::MNModel(QObject *parent) : QAbstractTableModel(parent)
{
    updateMNList();
}

void MNModel::clearNodes()
{
    for (auto it = nodes.begin(); it != nodes.end(); ++it) {
        delete it.value().second;
    }
    nodes.clear();
    walletOwnedAliases.clear();
}

bool MNModel::isMNWalletOwned(const QString& alias) const
{
    return walletOwnedAliases.contains(alias);
}

void MNModel::updateMNList()
{
    beginResetModel();
    clearNodes();
    collateralTxAccepted.clear();
    const dip3::CDeterministicMNList dip3List = deterministicMNManager
        ? deterministicMNManager->GetListAtChainTip()
        : dip3::CDeterministicMNList();
    std::set<COutPoint> seenDip3Collaterals;

    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        int nIndex;
        if (!mne.castOutputIndex(nIndex))
            continue;

        uint256 txHash = uint256S(mne.getTxHash());
        CTxIn txIn(txHash, uint32_t(nIndex));
        const dip3::CDeterministicMN* dip3Mn = dip3List.GetMNByCollateral(txIn.prevout);
        CMasternode* foundMn = mnodeman.Find(txIn);
        CMasternode* pmn = nullptr;
        if (dip3Mn) {
            seenDip3Collaterals.insert(txIn.prevout);
            pmn = new CMasternode();
            pmn->vin = txIn;
            pmn->addr = dip3Mn->state.addr;
            pmn->pubKeyCollateralAddress = dip3Mn->state.pubKeyOwner;
            pmn->pubKeyMasternode = dip3Mn->state.pubKeyOwner;
            pmn->activeState = dip3Mn->state.IsValid()
                ? CMasternode::MASTERNODE_ENABLED
                : (dip3Mn->state.IsBanned() ? CMasternode::MASTERNODE_POSE_BAN : CMasternode::MASTERNODE_EXPIRED);
        } else if (foundMn) {
            pmn = new CMasternode(*foundMn);
        } else {
            bool pendingInMempool = false;
            {
                LOCK(mempool.cs);
                for (const auto& entry : mempool.mapTx) {
                    std::vector<unsigned char> vchPayload;
                    bool fMalformed = false;
                    if (dip3::ExtractProTxPayload(entry.GetTx(), vchPayload, fMalformed) == dip3::PROTX_REG && !fMalformed) {
                        try {
                            CDataStream ds(vchPayload, SER_NETWORK, PROTOCOL_VERSION);
                            dip3::CProRegTx pendingPtx;
                            ds >> pendingPtx;
                            if (pendingPtx.collateralOutpoint == txIn.prevout) {
                                pendingInMempool = true;
                                break;
                            }
                        } catch (...) {
                        }
                    }
                }
            }
            pmn = new CMasternode();
            pmn->vin = txIn;
            pmn->activeState = pendingInMempool
                ? CMasternode::MASTERNODE_PRE_ENABLED
                : CMasternode::MASTERNODE_MISSING;
        }
        QString aliasKey = QString::fromStdString(mne.getAlias());
        if (nodes.contains(aliasKey)) {
            delete nodes[aliasKey].second;
        }
        const QString displayAddr = dip3Mn
            ? QString::fromStdString(dip3Mn->state.addr.ToString())
            : QString::fromStdString(mne.getIp());
        nodes.insert(aliasKey, std::make_pair(displayAddr, pmn));

        bool txAccepted = false;
        if (pmn && (pmn->IsEnabled() || pmn->activeState == CMasternode::MASTERNODE_PRE_ENABLED)) {
            txAccepted = true;
        } else {
            if (pwalletMain) {
                LOCK2(cs_main, pwalletMain->cs_wallet);
                const CWalletTx *walletTx = pwalletMain->GetWalletTx(txHash);
                if (walletTx && walletTx->GetDepthInMainChain() >= MASTERNODE_MIN_CONFIRMATIONS) {
                    txAccepted = true;
                }
            }
            if (!txAccepted && chainActive.Tip() && pblocktree) {
                LOCK(cs_main);
                CTransaction tx;
                uint256 hashBlock;
                if (GetTransaction(txHash, tx, hashBlock, true) && !hashBlock.IsNull()) {
                    BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
                    if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
                        int depth = chainActive.Height() - mi->second->nHeight + 1;
                        if (depth >= MASTERNODE_MIN_CONFIRMATIONS) {
                            txAccepted = true;
                        }
                    }
                }
            }
        }
        collateralTxAccepted.insert(mne.getTxHash() + ":" + mne.getOutputIndex(), txAccepted);
        walletOwnedAliases.insert(QString::fromStdString(mne.getAlias()));
    }

    // Also show on-chain KTDIP3 masternodes not present in masternode.conf
    for (const auto& kv : dip3List.mapMNs) {
        const dip3::CDeterministicMN& dmn = kv.second;
        if (seenDip3Collaterals.count(dmn.collateralOutpoint)) {
            continue;
        }
        CMasternode* pmn = new CMasternode();
        pmn->vin = CTxIn(dmn.collateralOutpoint);
        pmn->addr = dmn.state.addr;
        pmn->pubKeyCollateralAddress = dmn.state.pubKeyOwner;
        pmn->pubKeyMasternode = dmn.state.pubKeyOwner;
        pmn->activeState = dmn.state.IsValid()
            ? CMasternode::MASTERNODE_ENABLED
            : (dmn.state.IsBanned() ? CMasternode::MASTERNODE_POSE_BAN : CMasternode::MASTERNODE_EXPIRED);
        QString aliasKey = QString("dip3-%1").arg(QString::fromStdString(dmn.proTxHash.ToString().substr(0, 8)));
        if (nodes.contains(aliasKey)) {
            delete nodes[aliasKey].second;
        }
        bool isWalletOwned = false;
        if (pwalletMain) {
            LOCK(pwalletMain->cs_wallet);
            const CWalletTx* colWtx = pwalletMain->GetWalletTx(dmn.collateralOutpoint.hash);
            const bool hasColTx = (colWtx && dmn.collateralOutpoint.n < colWtx->vout.size() &&
                                   pwalletMain->IsMine(colWtx->vout[dmn.collateralOutpoint.n]) == ISMINE_SPENDABLE);
            const bool hasOwnerKey = pwalletMain->HaveKey(dmn.state.pubKeyOwner.GetID());
            const bool hasPayout = (::IsMine(*pwalletMain, dmn.state.scriptPayout) == ISMINE_SPENDABLE);
            isWalletOwned = hasColTx || hasOwnerKey || hasPayout;
        }
        if (isWalletOwned) {
            walletOwnedAliases.insert(aliasKey);
        }
        nodes.insert(aliasKey, std::make_pair(QString::fromStdString(dmn.state.addr.ToString()), pmn));
        collateralTxAccepted.insert(
            dmn.collateralOutpoint.hash.ToString() + ":" + std::to_string(dmn.collateralOutpoint.n),
            true);
    }
    endResetModel();
}

int MNModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return nodes.size();
}

int MNModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return WAS_COLLATERAL_ACCEPTED + 1;
}


QVariant MNModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
            return QVariant();

    // rec could be null, always verify it.
    CMasternode* rec = static_cast<CMasternode*>(index.internalPointer());
    bool isAvailable = rec;
    int row = index.row();
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
            case ALIAS:
                return nodes.uniqueKeys().value(row);
            case ADDRESS:
                return nodes.values().value(row).first;
            case PUB_KEY:
                return (isAvailable) ? QString::fromStdString(nodes.values().value(row).second->pubKeyMasternode.GetHash().GetHex()) : "Not available";
            case COLLATERAL_ID:
                return (isAvailable) ? QString::fromStdString(rec->vin.prevout.hash.GetHex()) : "Not available";
            case COLLATERAL_OUT_INDEX:
                return (isAvailable) ? QString::number(rec->vin.prevout.n) : "Not available";
            case STATUS: {
                std::pair<QString, CMasternode*> pair = nodes.values().value(row);
                return (pair.second) ? QString::fromStdString(pair.second->Status()) : "MISSING";
            }
            case PRIV_KEY: {
                if (!isAvailable) return "Not available";
                const std::string strOutIdx = std::to_string(rec->vin.prevout.n);
                for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
                    if (mne.getTxHash().compare(rec->vin.prevout.hash.GetHex()) == 0 &&
                        mne.getOutputIndex().compare(strOutIdx) == 0) {
                        return QString::fromStdString(mne.getPrivKey());
                    }
                }
                return "Not available";
            }
            case WAS_COLLATERAL_ACCEPTED:{
                if (!isAvailable) return false;
                if (rec->IsEnabled() || rec->activeState == CMasternode::MASTERNODE_PRE_ENABLED) {
                    return true;
                }
                std::string outpointKey = rec->vin.prevout.hash.GetHex() + ":" + std::to_string(rec->vin.prevout.n);
                if (!collateralTxAccepted.value(outpointKey)) {
                    bool txAccepted = false;
                    {
                        LOCK2(cs_main, pwalletMain ? pwalletMain->cs_wallet : cs_main);
                        if (pwalletMain) {
                            const CWalletTx *walletTx = pwalletMain->GetWalletTx(rec->vin.prevout.hash);
                            txAccepted = walletTx && walletTx->GetDepthInMainChain() > 0;
                        }
                        if (!txAccepted) {
                            CTransaction tx;
                            uint256 hashBlock;
                            if (GetTransaction(rec->vin.prevout.hash, tx, hashBlock, true) && !hashBlock.IsNull()) {
                                BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
                                if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
                                    txAccepted = (chainActive.Height() - mi->second->nHeight + 1) > 0;
                                }
                            }
                        }
                    }
                    return txAccepted;
                }
                return true;
            }
        }
    }
    return QVariant();
}

QModelIndex MNModel::index(int row, int column, const QModelIndex& parent) const
{
    Q_UNUSED(parent);
    std::pair<QString, CMasternode*> pair = nodes.values().value(row);
    CMasternode* data = pair.second;
    if (data) {
        return createIndex(row, column, data);
    } else if (!pair.first.isEmpty()) {
        return createIndex(row, column, nullptr);
    } else {
        return QModelIndex();
    }
}


bool MNModel::removeMn(const QModelIndex& modelIndex)
{
    if (!modelIndex.isValid())
        return false;
    QString alias = modelIndex.data(Qt::DisplayRole).toString();
    int idx = modelIndex.row();
    beginRemoveRows(QModelIndex(), idx, idx);
    std::pair<QString, CMasternode*> removed = nodes.take(alias);
    delete removed.second;
    endRemoveRows();
    if (!nodes.isEmpty() && idx < nodes.size()) {
        Q_EMIT dataChanged(index(idx, 0, QModelIndex()), index(idx, 5, QModelIndex()));
    }
    return true;
}

bool MNModel::addMn(CMasternodeConfig::CMasternodeEntry* mne)
{
    if (!mne)
        return false;
    int nIndex;
    if (!mne->castOutputIndex(nIndex))
        return false;

    QString alias = QString::fromStdString(mne->getAlias());
    uint256 txHash = uint256S(mne->getTxHash());
    CTxIn txIn(txHash, uint32_t(nIndex));
    CMasternode* foundMn = mnodeman.Find(txIn);
    CMasternode* pmn = nullptr;
    if (foundMn) {
        pmn = new CMasternode(*foundMn);
    } else {
        pmn = new CMasternode();
        pmn->vin = txIn;
        pmn->activeState = CMasternode::MASTERNODE_MISSING;
    }

    if (nodes.contains(alias)) {
        delete nodes[alias].second;
        nodes[alias] = std::make_pair(QString::fromStdString(mne->getIp()), pmn);
        int idx = nodes.uniqueKeys().indexOf(alias);
        if (idx >= 0) {
            Q_EMIT dataChanged(index(idx, 0, QModelIndex()), index(idx, 5, QModelIndex()));
        }
        return true;
    }

    int insertRow = 0;
    for (auto it = nodes.constBegin(); it != nodes.constEnd(); ++it) {
        if (it.key() < alias) {
            insertRow++;
        } else {
            break;
        }
    }

    beginInsertRows(QModelIndex(), insertRow, insertRow);
    nodes.insert(alias, std::make_pair(QString::fromStdString(mne->getIp()), pmn));
    endInsertRows();
    return true;
}

int MNModel::getMNState(QString mnAlias)
{
    QMap<QString, std::pair<QString, CMasternode*>>::const_iterator it = nodes.find(mnAlias);
    if (it != nodes.end()) {
        return it.value().second ? it.value().second->activeState : CMasternode::MASTERNODE_MISSING;
    }
    throw std::runtime_error(std::string("Masternode alias not found"));
}

bool MNModel::isMNInactive(QString mnAlias)
{
    int activeState = getMNState(mnAlias);
    return activeState == CMasternode::MASTERNODE_MISSING || activeState == CMasternode::MASTERNODE_EXPIRED || activeState == CMasternode::MASTERNODE_REMOVE;
}

bool MNModel::isMNActive(QString mnAlias)
{
    int activeState = getMNState(mnAlias);
    return activeState == CMasternode::MASTERNODE_PRE_ENABLED || activeState == CMasternode::MASTERNODE_ENABLED;
}

bool MNModel::isMNCollateralMature(QString mnAlias)
{
    QMap<QString, std::pair<QString, CMasternode*>>::const_iterator it = nodes.find(mnAlias);
    if (it != nodes.end() && it.value().second) {
        const COutPoint& outpoint = it.value().second->vin.prevout;
        return collateralTxAccepted.value(outpoint.hash.GetHex() + ":" + std::to_string(outpoint.n));
    }
    throw std::runtime_error(std::string("Masternode alias not found"));
}

bool MNModel::isMNsNetworkSynced()
{
    return true;
}

bool MNModel::validateMNIP(const QString& addrStr)
{
    return validateMasternodeIP(addrStr.toStdString());
}
