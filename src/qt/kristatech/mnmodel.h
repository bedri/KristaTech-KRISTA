// Copyright (c) 2019-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MNMODEL_H
#define MNMODEL_H

#include <QAbstractTableModel>
#include <QSet>
#include "masternode.h"
#include "wallet/walletdb.h"

class MNModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit MNModel(QObject *parent = nullptr);
    ~MNModel() override {
        clearNodes();
        collateralTxAccepted.clear();
        collateralTxConfs.clear();
    }

    enum ColumnIndex {
        ALIAS = 0,  /**< User specified MN alias */
        ADDRESS = 1, /**< Node address */
        PROTO_VERSION = 2, /**< Node protocol version */
        STATUS = 3, /**< Node status */
        ACTIVE_TIMESTAMP = 4, /**<  */
        PUB_KEY = 5,
        COLLATERAL_ID = 6,
        COLLATERAL_OUT_INDEX = 7,
        PRIV_KEY = 8,
        WAS_COLLATERAL_ACCEPTED = 9
    };

    struct Dip3Meta {
        bool isDip3{false};
        QString proTxHash;
        QString operatorBlsPubKey;
        QString payoutAddress;
        int posePenalty{0};
        int poseBanHeight{-1};
        int lastPaidHeight{0};
        int registeredHeight{0};
        uint16_t operatorRewardBps{0};
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    bool removeMn(const QModelIndex& index);
    bool addMn(const CMasternodeWalletRecord& record);
    void updateMNList();


    bool isMNsNetworkSynced();
    // Returns the MN activeState field.
    int getMNState(QString alias);
    // Checks if the masternode is inactive
    bool isMNInactive(QString mnAlias);
    // Masternode is active if it's in PRE_ENABLED OR ENABLED state
    bool isMNActive(QString mnAlias);
    // Masternode collateral has enough confirmations
    bool isMNCollateralMature(QString mnAlias);
    // Returns true if the MN has local wallet.dat metadata or its keys/collateral are owned by pwalletMain
    bool isMNWalletOwned(const QString& alias) const;
    // Returns true if the MN is currently registered on-chain via KTDIP3
    bool isMNOnChainDip3(const QString& alias) const;
    // Returns true if the MN has a local metadata record in wallet.dat
    bool isMNInWalletMeta(const QString& alias) const;
    // Returns true if the MN has local operator private keys stored in wallet.dat
    bool hasLocalOperatorKey(const QString& alias) const;
    // Returns KTDIP3 metadata for an alias (if registered on-chain)
    bool getDip3Meta(const QString& alias, Dip3Meta& out) const;
    // Validate string representing a masternode IP address
    static bool validateMNIP(const QString& addrStr);


private:
    void clearNodes();
    int getCollateralConfirmations(const uint256& txHash, int& requiredConfsOut) const;
    // alias mn node ---> pair <ip, master node>
    QMap<QString, std::pair<QString, CMasternode*>> nodes;
    mutable QMap<std::string, bool> collateralTxAccepted;
    mutable QMap<std::string, int> collateralTxConfs;
    mutable int requiredCollateralConfs{15};
    QSet<QString> walletOwnedAliases;
    QSet<QString> dip3RegisteredAliases;
    QSet<QString> walletMetaAliases;
    QSet<QString> localOperatorKeyAliases;
    QMap<QString, Dip3Meta> dip3Meta;
};

#endif // MNMODEL_H
