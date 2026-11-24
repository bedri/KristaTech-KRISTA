// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2016-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLETDB_H
#define BITCOIN_WALLETDB_H

#include "amount.h"
#include "wallet/db.h"
#include "wallet/hdchain.h"
#include "key.h"
#include "keystore.h"
#include "primitives/transaction.h"
#include "script/keyorigin.h"

#include <list>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

static const bool DEFAULT_FLUSHWALLET = true;

class CAccount;
class CAccountingEntry;
struct CBlockLocator;
class CKeyPool;
class CMasterKey;
class CScript;
class CWallet;
class CWalletTx;
class uint160;
class uint256;
class CBLSSecretKey;

/** Error statuses for the wallet database */
enum DBErrors {
    DB_LOAD_OK,
    DB_CORRUPT,
    DB_NONCRITICAL_ERROR,
    DB_TOO_NEW,
    DB_LOAD_FAIL,
    DB_NEED_REWRITE
};

class CKeyMetadata
{
public:
    // Metadata versions
    static const int VERSION_BASIC = 1;
    static const int VERSION_WITH_KEY_ORIGIN = 12;
    // Active version
    static const int CURRENT_VERSION = VERSION_WITH_KEY_ORIGIN;

    int nVersion;
    int64_t nCreateTime; // 0 means unknown
    CKeyID hd_seed_id; //id of the HD seed used to derive this key
    KeyOriginInfo key_origin; // Key origin info with path and fingerprint

    CKeyMetadata()
    {
        SetNull();
    }
    CKeyMetadata(int64_t nCreateTime_)
    {
        SetNull();
        nCreateTime = nCreateTime_;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(nCreateTime);
        if (HasKeyOrigin()) {
            READWRITE(hd_seed_id);
            READWRITE(key_origin);
        }
    }

    void SetNull()
    {
        nVersion = CKeyMetadata::CURRENT_VERSION;
        nCreateTime = 0;
        hd_seed_id.SetNull();
        key_origin.clear();
    }

    bool HasKeyOrigin() const
    {
        return this->nVersion >= VERSION_WITH_KEY_ORIGIN;
    }
};

/**
 * Local Cold Controller metadata for a Deterministic Masternode or 15-conf draft.
 * Private operator keys are NEVER serialized in this record; they live inside
 * standard encrypted wallet key records (ckey / cblskey) referenced by operatorKeyID.
 */
class CMasternodeWalletRecord
{
public:
    static const int CURRENT_VERSION = 2;

    int nVersion;
    COutPoint collateralOutpoint;
    std::string strAlias;
    std::string strService;
    std::string strOperatorBlsPubKey;
    std::string strOperatorBlsPoP;
    CKeyID operatorKeyID;
    uint256 proTxHash;
    uint16_t nOperatorRewardBps;
    std::string strPayoutAddress;
    int64_t nCreateTime;

    CMasternodeWalletRecord()
    {
        SetNull();
    }

    void SetNull()
    {
        nVersion = CURRENT_VERSION;
        collateralOutpoint.SetNull();
        strAlias.clear();
        strService.clear();
        strOperatorBlsPubKey.clear();
        strOperatorBlsPoP.clear();
        operatorKeyID.SetNull();
        proTxHash.SetNull();
        nOperatorRewardBps = 0;
        strPayoutAddress.clear();
        nCreateTime = 0;
    }

    bool IsNull() const
    {
        return collateralOutpoint.IsNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(nVersion);
        READWRITE(collateralOutpoint);
        READWRITE(LIMITED_STRING(strAlias, 256));
        READWRITE(LIMITED_STRING(strService, 256));
        READWRITE(LIMITED_STRING(strOperatorBlsPubKey, 256));
        READWRITE(operatorKeyID);
        READWRITE(proTxHash);
        READWRITE(nOperatorRewardBps);
        READWRITE(LIMITED_STRING(strPayoutAddress, 256));
        READWRITE(nCreateTime);
        if (nVersion >= 2) {
            READWRITE(LIMITED_STRING(strOperatorBlsPoP, 512));
        } else if (ser_action.ForRead()) {
            strOperatorBlsPoP.clear();
        }
    }
};

/** Access to the wallet database */
class CWalletDB : public CDB
{
public:
    CWalletDB(const std::string& strFilename, const char* pszMode = "r+", bool fFlushOnClose = true) : CDB(strFilename, pszMode, fFlushOnClose)
    {
    }

    bool WriteName(const std::string& strAddress, const std::string& strName);
    bool EraseName(const std::string& strAddress);

    bool WritePurpose(const std::string& strAddress, const std::string& purpose);
    bool ErasePurpose(const std::string& strAddress);

    bool WriteTx(const CWalletTx& wtx);
    bool EraseTx(uint256 hash);

    bool WriteKey(const CPubKey& vchPubKey, const CPrivKey& vchPrivKey, const CKeyMetadata& keyMeta);
    bool WriteBLSKey(const CKeyID& keyid, const CBLSSecretKey& blsKey);
    bool WriteCryptedBLSKey(const CKeyID& keyid, const std::vector<unsigned char>& vchCryptedSecret);
    bool ReadBLSKey(const CKeyID& keyid, CBLSSecretKey& blsKey);
    bool EraseBLSKey(const CKeyID& keyid);
    bool WriteKeyMetadata(const CPubKey& vchPubKey, const CKeyMetadata& keyMeta);
    bool WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret, const CKeyMetadata& keyMeta);
    bool WriteMasterKey(unsigned int nID, const CMasterKey& kMasterKey);

    bool WriteCScript(const uint160& hash, const CScript& redeemScript);

    bool WriteWatchOnly(const CScript& script);
    bool EraseWatchOnly(const CScript& script);

    bool WriteMultiSig(const CScript& script);
    bool EraseMultiSig(const CScript& script);

    bool WriteBestBlock(const CBlockLocator& locator);
    bool ReadBestBlock(CBlockLocator& locator);

    bool WriteOrderPosNext(int64_t nOrderPosNext);

    bool WriteStakeSplitThreshold(const CAmount& nStakeSplitThreshold);
    bool WriteUseCustomFee(bool fUse);
    bool WriteCustomFeeValue(const CAmount& nCustomFee);
    bool WriteAutoCombineSettings(bool fEnable, CAmount nCombineThreshold);

    bool ReadPool(int64_t nPool, CKeyPool& keypool);
    bool WritePool(int64_t nPool, const CKeyPool& keypool);
    bool ErasePool(int64_t nPool);

    bool WriteMinVersion(int nVersion);

    /// This writes directly to the database, and will not update the CWallet's cached accounting entries!
    /// Use wallet.AddAccountingEntry instead, to write *and* update its caches.
    bool WriteAccountingEntry_Backend(const CAccountingEntry& acentry);

    bool ReadAccount(const std::string& strAccount, CAccount& account);
    bool WriteAccount(const std::string& strAccount, const CAccount& account);
    bool EraseAccount(const std::string& strAccount);

    //! write the hdchain model (external/internal chain child index counter)
    bool WriteHDChain(const CHDChain& chain);

    /// Write destination data key,value tuple to database
    bool WriteDestData(const std::string& address, const std::string& key, const std::string& value);
    /// Erase destination data tuple from wallet database
    bool EraseDestData(const std::string& address, const std::string& key);

    /// Write / erase local Masternode controller metadata keyed by collateral outpoint
    bool WriteMasternodeMeta(const COutPoint& outpoint, const CMasternodeWalletRecord& record);
    bool EraseMasternodeMeta(const COutPoint& outpoint);

    CAmount GetAccountCreditDebit(const std::string& strAccount);
    void ListAccountCreditDebit(const std::string& strAccount, std::list<CAccountingEntry>& acentries);

    DBErrors ReorderTransactions(CWallet* pwallet);
    DBErrors LoadWallet(CWallet* pwallet);
    DBErrors FindWalletTx(CWallet* pwallet, std::vector<uint256>& vTxHash, std::vector<CWalletTx>& vWtx);
    DBErrors ZapWalletTx(CWallet* pwallet, std::vector<CWalletTx>& vWtx);
    static bool Recover(CDBEnv& dbenv, std::string filename, bool fOnlyKeys);
    static bool Recover(CDBEnv& dbenv, std::string filename);

    static void IncrementUpdateCounter();
    static unsigned int GetUpdateCounter();
private:
    CWalletDB(const CWalletDB&);
    void operator=(const CWalletDB&);

    bool WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry);
};

void NotifyBacked(const CWallet& wallet, bool fSuccess, std::string strMessage);
bool BackupWallet(const CWallet& wallet, const fs::path& strDest, bool fEnableCustom = true);
bool AttemptBackupWallet(const CWallet& wallet, const fs::path& pathSrc, const fs::path& pathDest);

void ThreadFlushWalletDB();

#endif // BITCOIN_WALLETDB_H
