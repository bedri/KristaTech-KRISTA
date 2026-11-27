// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "qt/kristatech/mnmodel.h"

#include "evo/deterministicmns.h"
#include "key_io.h"
#include "main.h"
#include "masternode-sync.h"
#include "masternodeman.h"
#include "net.h"        // for validateMasternodeIP
#include "sync.h"
#include "uint256.h"
#include "utilstrencodings.h"
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
    dip3RegisteredAliases.clear();
    walletMetaAliases.clear();
    localOperatorKeyAliases.clear();
    dip3Meta.clear();
    collateralTxConfs.clear();
}

int MNModel::getCollateralConfirmations(const uint256& txHash, int& requiredConfsOut) const
{
    LOCK(cs_main);
    const bool fDip3Active = Params().GetConsensus().NetworkUpgradeActive(
        chainActive.Height() + 1, Consensus::UPGRADE_DIP3);
    requiredConfsOut = fDip3Active ? dip3::PROTX_MIN_COLLATERAL_CONFS : 1;

    if (pwalletMain) {
        LOCK(pwalletMain->cs_wallet);
        const CWalletTx* walletTx = pwalletMain->GetWalletTx(txHash);
        if (walletTx) {
            const int depth = walletTx->GetDepthInMainChain();
            if (depth > 0 || walletTx->InMempool()) {
                return depth;
            }
        }
    }

    {
        LOCK(mempool.cs);
        if (mempool.exists(txHash)) {
            return 0;
        }
    }

    CTransaction tx;
    uint256 hashBlock;
    if (GetTransaction(txHash, tx, hashBlock, true)) {
        if (hashBlock.IsNull()) {
            return 0;
        }
        BlockMap::iterator mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.end() && chainActive.Contains(mi->second)) {
            return chainActive.Height() - mi->second->nHeight + 1;
        }
    }

    return -1;
}

bool MNModel::isMNWalletOwned(const QString& alias) const
{
    return walletOwnedAliases.contains(alias);
}

bool MNModel::isMNOnChainDip3(const QString& alias) const
{
    return dip3RegisteredAliases.contains(alias);
}

bool MNModel::isMNInWalletMeta(const QString& alias) const
{
    return walletMetaAliases.contains(alias);
}

bool MNModel::hasLocalOperatorKey(const QString& alias) const
{
    return localOperatorKeyAliases.contains(alias);
}

bool MNModel::getDip3Meta(const QString& alias, Dip3Meta& out) const
{
    auto it = dip3Meta.constFind(alias);
    if (it == dip3Meta.constEnd()) {
        return false;
    }
    out = it.value();
    return true;
}

void MNModel::updateMNList()
{
    beginResetModel();
    clearNodes();
    collateralTxAccepted.clear();
    collateralTxConfs.clear();
    const dip3::CDeterministicMNList dip3List = deterministicMNManager
        ? deterministicMNManager->GetListAtChainTip()
        : dip3::CDeterministicMNList();
    std::set<COutPoint> seenDip3Collaterals;

    auto extractDip3Meta = [](const dip3::CDeterministicMN& dmn) -> Dip3Meta {
        Dip3Meta meta;
        meta.isDip3 = true;
        meta.proTxHash = QString::fromStdString(dmn.proTxHash.ToString());
        meta.operatorBlsPubKey = QString::fromStdString(HexStr(dmn.state.blsPubKeyOperator.begin(), dmn.state.blsPubKeyOperator.end()));
        CTxDestination payoutDest;
        if (ExtractDestination(dmn.state.scriptPayout, payoutDest)) {
            meta.payoutAddress = QString::fromStdString(EncodeDestination(payoutDest));
        }
        meta.posePenalty = dmn.state.nPoSePenalty;
        meta.poseBanHeight = dmn.state.nPoSeBanHeight;
        meta.lastPaidHeight = dmn.state.nLastPaidHeight;
        meta.registeredHeight = dmn.state.nRegisteredHeight;
        meta.operatorRewardBps = dmn.state.nOperatorReward;
        return meta;
    };

    std::set<COutPoint> mempoolPendingOutpoints;
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
                    mempoolPendingOutpoints.insert(pendingPtx.collateralOutpoint);
                } catch (...) {
                }
            }
        }
    }

    const std::vector<CMasternodeWalletRecord> walletMetaList = pwalletMain
        ? pwalletMain->GetAllMasternodeMeta()
        : std::vector<CMasternodeWalletRecord>();

    for (const CMasternodeWalletRecord& rec : walletMetaList) {
        const COutPoint& colOut = rec.collateralOutpoint;
        if (colOut.IsNull())
            continue;

        const uint256& txHash = colOut.hash;
        CTxIn txIn(colOut);
        const dip3::CDeterministicMN* dip3Mn = dip3List.GetMNByCollateral(colOut);
        CMasternode foundMn;
        const bool hasFoundMn = mnodeman.Get(txIn, foundMn);
        CMasternode* pmn = nullptr;
        if (dip3Mn) {
            seenDip3Collaterals.insert(colOut);
            pmn = new CMasternode();
            pmn->vin = txIn;
            pmn->addr = dip3Mn->state.addr;
            pmn->pubKeyCollateralAddress = dip3Mn->state.pubKeyOwner;
            pmn->pubKeyMasternode = dip3Mn->state.pubKeyOwner;
            pmn->activeState = dip3Mn->state.IsValid()
                ? CMasternode::MASTERNODE_ENABLED
                : (dip3Mn->state.IsBanned() ? CMasternode::MASTERNODE_POSE_BAN : CMasternode::MASTERNODE_EXPIRED);
        } else if (hasFoundMn) {
            pmn = new CMasternode(foundMn);
        } else {
            const bool pendingInMempool = mempoolPendingOutpoints.count(colOut) > 0;
            pmn = new CMasternode();
            pmn->vin = txIn;
            pmn->activeState = pendingInMempool
                ? CMasternode::MASTERNODE_PRE_ENABLED
                : CMasternode::MASTERNODE_MISSING;
        }
        QString aliasKey = QString::fromStdString(rec.strAlias);
        if (aliasKey.isEmpty()) {
            const uint256 hashHint = dip3Mn ? dip3Mn->proTxHash : rec.proTxHash;
            if (!hashHint.IsNull()) {
                aliasKey = QString("dip3-%1").arg(QString::fromStdString(hashHint.ToString().substr(0, 8)));
            } else {
                aliasKey = QString("draft-%1").arg(QString::fromStdString(txHash.ToString().substr(0, 8)));
            }
        }
        if (nodes.contains(aliasKey)) {
            delete nodes[aliasKey].second;
        }
        const QString displayAddr = dip3Mn
            ? QString::fromStdString(dip3Mn->state.addr.ToString())
            : QString::fromStdString(rec.strService);
        nodes.insert(aliasKey, std::make_pair(displayAddr, pmn));

        const std::string outpointKey = txHash.ToString() + ":" + std::to_string(colOut.n);
        int reqConfs = MASTERNODE_MIN_CONFIRMATIONS;
        const int txConfs = getCollateralConfirmations(txHash, reqConfs);
        requiredCollateralConfs = reqConfs;
        bool txAccepted = (txConfs >= reqConfs);
        if (pmn && (pmn->IsEnabled() || pmn->activeState == CMasternode::MASTERNODE_PRE_ENABLED)) {
            txAccepted = true;
        }
        collateralTxConfs.insert(outpointKey, (txConfs < 0 && txAccepted) ? reqConfs : txConfs);
        collateralTxAccepted.insert(outpointKey, txAccepted);
        walletOwnedAliases.insert(aliasKey);
        walletMetaAliases.insert(aliasKey);
        if (pwalletMain && pwalletMain->HasMasternodeOperatorKey(colOut)) {
            localOperatorKeyAliases.insert(aliasKey);
        }
        if (dip3Mn) {
            dip3RegisteredAliases.insert(aliasKey);
            dip3Meta.insert(aliasKey, extractDip3Meta(*dip3Mn));
        }
    }

    // Also show on-chain KTDIP3 masternodes without a wallet mnmeta record
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
        dip3RegisteredAliases.insert(aliasKey);
        dip3Meta.insert(aliasKey, extractDip3Meta(dmn));
        nodes.insert(aliasKey, std::make_pair(QString::fromStdString(dmn.state.addr.ToString()), pmn));
        const std::string dip3OutKey = dmn.collateralOutpoint.hash.ToString() + ":" + std::to_string(dmn.collateralOutpoint.n);
        collateralTxConfs.insert(dip3OutKey, requiredCollateralConfs);
        collateralTxAccepted.insert(dip3OutKey, true);
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
    const QString rowAlias = nodes.uniqueKeys().value(row);
    if (role == Qt::UserRole + 1) {
        return isMNWalletOwned(rowAlias);
    }
    if (role == Qt::UserRole + 2) {
        return isMNOnChainDip3(rowAlias);
    }
    if (role == Qt::UserRole + 3) {
        return isMNInWalletMeta(rowAlias);
    }
    if (role == Qt::UserRole + 12) {
        return hasLocalOperatorKey(rowAlias);
    }
    if (role == Qt::UserRole + 13 || role == Qt::UserRole + 14) {
        if (role == Qt::UserRole + 14) {
            return requiredCollateralConfs;
        }
        if (!isAvailable) {
            return -1;
        }
        const std::string outpointKey = rec->vin.prevout.hash.GetHex() + ":" + std::to_string(rec->vin.prevout.n);
        auto itConf = collateralTxConfs.constFind(outpointKey);
        if (itConf != collateralTxConfs.constEnd()) {
            const int cached = itConf.value();
            if (cached >= 0) {
                return cached;
            }
            if (rec->IsEnabled() || rec->activeState == CMasternode::MASTERNODE_PRE_ENABLED) {
                return requiredCollateralConfs;
            }
        }
        int reqConfs = requiredCollateralConfs;
        const int confs = getCollateralConfirmations(rec->vin.prevout.hash, reqConfs);
        requiredCollateralConfs = reqConfs;
        collateralTxConfs[outpointKey] = confs;
        if (confs >= reqConfs) {
            collateralTxAccepted[outpointKey] = true;
        }
        if (confs < 0 && (rec->IsEnabled() || rec->activeState == CMasternode::MASTERNODE_PRE_ENABLED)) {
            return reqConfs;
        }
        return confs;
    }
    if (role >= Qt::UserRole + 4 && role <= Qt::UserRole + 11) {
        const auto metaIt = dip3Meta.constFind(rowAlias);
        const bool hasMeta = (metaIt != dip3Meta.constEnd());
        switch (role) {
            case Qt::UserRole + 4:  return hasMeta ? metaIt->proTxHash : QString();
            case Qt::UserRole + 5:  return hasMeta ? metaIt->posePenalty : 0;
            case Qt::UserRole + 6:  return hasMeta ? metaIt->lastPaidHeight : 0;
            case Qt::UserRole + 7:  return hasMeta ? metaIt->payoutAddress : QString();
            case Qt::UserRole + 8:  return hasMeta ? metaIt->operatorBlsPubKey : QString();
            case Qt::UserRole + 9:  return hasMeta ? metaIt->registeredHeight : -1;
            case Qt::UserRole + 10: return hasMeta ? metaIt->poseBanHeight : -1;
            case Qt::UserRole + 11: return hasMeta ? static_cast<uint>(metaIt->operatorRewardBps) : 0U;
            default: break;
        }
    }
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
                return hasLocalOperatorKey(rowAlias) ? QStringLiteral("Encrypted in wallet.dat") : QStringLiteral("Not available");
            }
            case WAS_COLLATERAL_ACCEPTED:{
                if (!isAvailable) return false;
                if (rec->IsEnabled() || rec->activeState == CMasternode::MASTERNODE_PRE_ENABLED) {
                    return true;
                }
                const std::string outpointKey = rec->vin.prevout.hash.GetHex() + ":" + std::to_string(rec->vin.prevout.n);
                if (!collateralTxAccepted.value(outpointKey)) {
                    int reqConfs = requiredCollateralConfs;
                    const int confs = getCollateralConfirmations(rec->vin.prevout.hash, reqConfs);
                    requiredCollateralConfs = reqConfs;
                    collateralTxConfs[outpointKey] = confs;
                    const bool txAccepted = (confs >= reqConfs);
                    if (txAccepted) {
                        collateralTxAccepted[outpointKey] = true;
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

bool MNModel::addMn(const CMasternodeWalletRecord& record)
{
    if (record.collateralOutpoint.IsNull())
        return false;
    updateMNList();
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
