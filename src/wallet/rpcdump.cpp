// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "bip38.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "rpc/server.h"
#include "script/script.h"
#include "script/standard.h"
#include "support/cleanse.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
#include "wallet/wallet.h"

#include <cstdio>
#include <errno.h>
#include <fstream>
#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <secp256k1.h>
#include <stdint.h>

#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <univalue.h>


std::string static EncodeDumpTime(int64_t nTime)
{
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

int64_t static DecodeDumpTime(const std::string& str)
{
    static const boost::posix_time::ptime epoch = boost::posix_time::from_time_t(0);
    static const std::locale loc(std::locale::classic(),
        new boost::posix_time::time_input_facet("%Y-%m-%dT%H:%M:%SZ"));
    std::istringstream iss(str);
    iss.imbue(loc);
    boost::posix_time::ptime ptime(boost::date_time::not_a_date_time);
    iss >> ptime;
    if (ptime.is_not_a_date_time())
        return 0;
    return (ptime - epoch).total_seconds();
}

std::string static EncodeDumpString(const std::string& str)
{
    std::stringstream ret;
    for (unsigned char c : str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(&c, &c + 1);
        } else {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string& str)
{
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++) {
        unsigned char c = str[pos];
        if (c == '%' && pos + 2 < str.length()) {
            c = (((str[pos + 1] >> 6) * 9 + ((str[pos + 1] - '0') & 15)) << 4) |
                ((str[pos + 2] >> 6) * 9 + ((str[pos + 2] - '0') & 15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

UniValue importprivkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "importprivkey \"kristatechprivkey\" ( \"label\" rescan fStakingAddress )\n"
            "\nAdds a private key (as returned by dumpprivkey) to your wallet.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"kristatechprivkey\"      (string, required) The private key (see dumpprivkey)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"

            "\nNote: This call can take minutes to complete if rescan is true.\n"

            "\nExamples:\n"
            "\nDump a private key\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") +
            "\nImport the private key with rescan\n" +
            HelpExampleCli("importprivkey", "\"mykey\"") +
            "\nImport using a label and without rescan\n" +
            HelpExampleCli("importprivkey", "\"mykey\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importprivkey", "\"mykey\", \"testing\", false"));

    const std::string strSecret = request.params[0].get_str();
    const std::string strLabel = (request.params.size() > 1 ? request.params[1].get_str() : "");
    const bool fRescan = (request.params.size() > 2 ? request.params[2].get_bool() : true);

    CKey key = DecodeSecret(strSecret);
    if (!key.IsValid()) throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid private key encoding");

    CPubKey pubkey = key.GetPubKey();
    assert(key.VerifyPubKey(pubkey));
    CKeyID vchAddress = pubkey.GetID();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        EnsureWalletIsUnlocked();

        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, strLabel, AddressBook::AddressBookPurpose::RECEIVE);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            return NullUniValue;

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        if (fRescan) {
            CBlockIndex *pindex = chainActive.Genesis();
            pwalletMain->ScanForWalletTransactions(pindex, true);
        }
    }

    return NullUniValue;
}

void ImportAddress(const CTxDestination& dest, const std::string& strLabel, const std::string& strPurpose);

void ImportScript(const CScript& script, const std::string& strLabel, bool isRedeemScript)
{
    if (!isRedeemScript && ::IsMine(*pwalletMain, script) == ISMINE_SPENDABLE)
        throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");

    pwalletMain->MarkDirty();

    if (!pwalletMain->HaveWatchOnly(script) && !pwalletMain->AddWatchOnly(script))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");

    if (isRedeemScript) {
        if (!pwalletMain->HaveCScript(script) && !pwalletMain->AddCScript(script))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding p2sh redeemScript to wallet");
        ImportAddress(CScriptID(script), strLabel,  "receive");
    }
}

void ImportAddress(const CTxDestination& dest, const std::string& strLabel, const std::string& strPurpose)
{
    CScript script = GetScriptForDestination(dest);
    ImportScript(script, strLabel, false);
    // add to address book or update label
    if (IsValidDestination(dest)) {
        pwalletMain->SetAddressBook(dest, strLabel, strPurpose);
    }
}

UniValue importaddress(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "importaddress \"script\" ( \"label\" rescan )\n"
            "\nAdds a script (in hex), or address, that can be watched as if it were in your wallet but cannot be used to spend.\n"

            "\nArguments:\n"
            "1. \"script\"           (string, required) hex-encoded script (or address)\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "4. p2sh                 (boolean, optional, default=false) Add the P2SH version of the script as well\n"

            "\nNote: This call can take minutes to complete if rescan is true.\n"
            "If you have the full public key, you should call importpublickey instead of this.\n"

            "\nExamples:\n"
            "\nImport a script with rescan\n" +
            HelpExampleCli("importaddress", "\"myscript\"") +
            "\nImport using a label without rescan\n" +
            HelpExampleCli("importaddress", "\"myscript\" \"testing\" false") +
            "\nAs a JSON-RPC call\n" +
            HelpExampleRpc("importaddress", "\"myscript\", \"testing\", false"));

    const std::string strLabel = (request.params.size() > 1 ? request.params[1].get_str() : "");
    // Whether to perform rescan after import
    const bool fRescan = (request.params.size() > 2 ? request.params[2].get_bool() : true);
    // Whether to import a p2sh version, too
    const bool fP2SH = (request.params.size() > 3 ? request.params[3].get_bool() : false);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CTxDestination dest = DecodeDestination(request.params[0].get_str());

    if (IsValidDestination(dest)) {
        if (fP2SH)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Cannot use the p2sh flag with an address - use a script instead");
        ImportAddress(dest, strLabel, AddressBook::AddressBookPurpose::RECEIVE);

    } else if (IsHex(request.params[0].get_str())) {
        std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
        ImportScript(CScript(data.begin(), data.end()), strLabel, fP2SH);

    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid  address or script");
    }

    if (fRescan) {
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return NullUniValue;
}

UniValue importpubkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 1 || request.params.size() > 4)
        throw std::runtime_error(
            "importpubkey \"pubkey\" ( \"label\" rescan )\n"
            "\nAdds a public key (in hex) that can be watched as if it were in your wallet but cannot be used to spend.\n"
            "\nArguments:\n"
            "1. \"pubkey\"           (string, required) The hex-encoded public key\n"
            "2. \"label\"            (string, optional, default=\"\") An optional label\n"
            "3. rescan               (boolean, optional, default=true) Rescan the wallet for transactions\n"
            "\nNote: This call can take minutes to complete if rescan is true.\n"
            "\nExamples:\n"
            "\nImport a public key with rescan\n"
            + HelpExampleCli("importpubkey", "\"mypubkey\"") +
            "\nImport using a label without rescan\n"
            + HelpExampleCli("importpubkey", "\"mypubkey\" \"testing\" false") +
            "\nAs a JSON-RPC call\n"
            + HelpExampleRpc("importpubkey", "\"mypubkey\", \"testing\", false")
        );

    const std::string strLabel = (request.params.size() > 1 ? request.params[1].get_str() : "");
    // Whether to perform rescan after import
    const bool fRescan = (request.params.size() > 2 ? request.params[2].get_bool() : true);

    if (!IsHex(request.params[0].get_str()))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey must be a hex string");
    std::vector<unsigned char> data(ParseHex(request.params[0].get_str()));
    CPubKey pubKey(data.begin(), data.end());
    if (!pubKey.IsFullyValid())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Pubkey is not a valid public key");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    ImportAddress(pubKey.GetID(), strLabel, "receive");
    ImportScript(GetScriptForRawPubKey(pubKey), strLabel, false);

    if (fRescan) {
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return NullUniValue;
}

// TODO: Needs further review over the HD flow, staking addresses and multisig import.
UniValue importwallet(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "importwallet \"filename\"\n"
            "\nImports keys from a wallet dump file (see dumpwallet).\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"filename\"    (string, required) The wallet file\n"

            "\nExamples:\n"
            "\nDump the wallet\n" +
            HelpExampleCli("dumpwallet", "\"test\"") +
            "\nImport the wallet\n" +
            HelpExampleCli("importwallet", "\"test\"") +
            "\nImport using the json rpc call\n" +
            HelpExampleRpc("importwallet", "\"test\""));

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    const fs::path importPath(request.params[0].get_str());
    if (fs::is_symlink(importPath) || !fs::exists(importPath) || !fs::is_regular_file(importPath))
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file (must be an existing regular file)");

    std::ifstream file;
    file.open(importPath.c_str(), std::ios::in | std::ios::ate);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    int64_t nTimeBegin = chainActive.Tip() ? chainActive.Tip()->GetBlockTime() : 0;

    bool fGood = true;

    int64_t nFilesize = std::max((int64_t)1, (int64_t)file.tellg());
    file.seekg(0, file.beg);

    pwalletMain->ShowProgress(_("Importing..."), 0); // show progress dialog in GUI
    while (file.good()) {
        pwalletMain->ShowProgress("", std::max(1, std::min(99, (int)(((double)file.tellg() / (double)nFilesize) * 100))));
        std::string line;
        std::vector<std::string> vstr;
        std::getline(file, line);
        struct LineGuard {
            std::string& s;
            std::vector<std::string>& vec;
            LineGuard(std::string& sIn, std::vector<std::string>& vecIn) : s(sIn), vec(vecIn) {}
            ~LineGuard() {
                if (!s.empty()) memory_cleanse(&s[0], s.size());
                for (std::string& item : vec) {
                    if (!item.empty()) memory_cleanse(&item[0], item.size());
                }
            }
        } lineGuard(line, vstr);

        if (line.empty() || line[0] == '#')
            continue;

        boost::split(vstr, line, boost::is_any_of(" "));
        if (vstr.size() < 2)
            continue;
        CKey key = DecodeSecret(vstr[0]);
        if (!vstr[0].empty()) {
            memory_cleanse(&vstr[0][0], vstr[0].size());
        }
        if (!key.IsValid())
            continue;
        CPubKey pubkey = key.GetPubKey();
        if (!key.VerifyPubKey(pubkey)) {
            fGood = false;
            continue;
        }
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid)) {
            LogPrintf("Skipping import of %s (key already present)\n", EncodeDestination(keyid));
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        std::string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
            const std::string& type = vstr[nStr];
            if (boost::algorithm::starts_with(type, "#"))
                break;
            if (type == "change=1")
                fLabel = false;
            else if (type == "reserve=1")
                fLabel = false;
            else if (type == "hdseed")
                fLabel = false;
            if (boost::algorithm::starts_with(type, "label=")) {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        LogPrintf("Importing %s...\n", EncodeDestination(keyid));
        if (!pwalletMain->AddKeyPubKey(key, pubkey)) {
            fGood = false;
            continue;
        }
        pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
        CWalletDB(pwalletMain->strWalletFile).WriteKeyMetadata(pubkey, pwalletMain->mapKeyMetadata[keyid]);
        if (fLabel) // TODO: This is not entirely true.. needs to be reviewed properly.
            pwalletMain->SetAddressBook(keyid, strLabel, AddressBook::AddressBookPurpose::RECEIVE);
        nTimeBegin = std::min(nTimeBegin, nTime);
    }
    file.close();
    pwalletMain->ShowProgress("", 100); // hide progress dialog in GUI

    CBlockIndex* pindex = chainActive.Tip();
    while (pindex && pindex->pprev && pindex->GetBlockTime() > nTimeBegin - 7200)
        pindex = pindex->pprev;

    if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
        pwalletMain->nTimeFirstKey = nTimeBegin;

    if (pindex) {
        LogPrintf("Rescanning last %i blocks\n", chainActive.Height() - pindex->nHeight + 1);
        pwalletMain->ScanForWalletTransactions(pindex);
    }
    pwalletMain->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return NullUniValue;
}

UniValue dumpprivkey(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "dumpprivkey \"kristatechaddress\"\n"
            "\nReveals the private key corresponding to 'kristatechaddress'.\n"
            "Then the importprivkey can be used with this output\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"kristatechaddress\"   (string, required) The kristatech address for the private key\n"

            "\nResult:\n"
            "\"key\"                (string) The private key\n"

            "\nExamples:\n" +
            HelpExampleCli("dumpprivkey", "\"myaddress\"") + HelpExampleCli("importprivkey", "\"mykey\"") + HelpExampleRpc("dumpprivkey", "\"myaddress\""));

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid KristaTech address");
    CKeyID* keyID = boost::get<CKeyID>(&dest);
    if (!keyID)
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(*keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return EncodeSecret(vchSecret);
}

UniValue dumpwallet(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "dumpwallet \"filename\"\n"
            "\nDumps all wallet keys in a human-readable format to a server-side file. This does not allow overwriting existing files.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"filename\"    (string, required) The filename\n"

            "\nExamples:\n" +
            HelpExampleCli("dumpwallet", "\"test\"") + HelpExampleRpc("dumpwallet", "\"test\""));

    if (request.params[0].get_str().find("bug") != std::string::npos ||
        request.params[0].get_str().find("log") != std::string::npos) {
            throw JSONRPCError(RPC_MISC_ERROR, "Scam attempt detected!");
    }

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    ScriptPubKeyMan* spk_man = pwalletMain->GetScriptPubKeyMan();

    fs::path filepath = request.params[0].get_str().c_str();
    filepath = fs::absolute(filepath);

    auto cleanseString = [](std::string& s) {
        if (!s.empty()) memory_cleanse(&s[0], s.size());
    };
    auto cleanseExtKey = [](CExtKey& ek) {
        ek.key = CKey();
        memory_cleanse(ek.chaincode.begin(), ek.chaincode.size());
        memory_cleanse(ek.vchFingerprint, sizeof(ek.vchFingerprint));
    };

    /* Prevent arbitrary files from being overwritten or created with permissive umask permissions (M-9). */
    if (fs::is_symlink(filepath) || fs::exists(filepath)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, filepath.string() + " already exists or is a symlink. If you are sure this is what you want, move it out of the way first");
    }

#ifndef WIN32
    const int dumpFd = ::open(filepath.string().c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (dumpFd < 0) {
        if (errno == EEXIST || errno == ELOOP) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, filepath.string() + " already exists or is a symlink. Move it out of the way first");
        }
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
    }
    FILE* dumpFp = ::fdopen(dumpFd, "wb");
    if (!dumpFp) {
        ::close(dumpFd);
        ::unlink(filepath.string().c_str());
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
    }
    struct DumpFileGuard {
        FILE* fp;
        std::string path;
        bool ok;
        DumpFileGuard(FILE* fpIn, const std::string& pathIn) : fp(fpIn), path(pathIn), ok(false) {}
        ~DumpFileGuard() {
            if (fp) {
                ::fclose(fp);
            }
            if (!ok) {
                ::unlink(path.c_str());
            }
        }
    } dumpGuard(dumpFp, filepath.string());
    auto writeDumpLine = [&](const std::string& text) {
        if (!text.empty() && std::fwrite(text.data(), 1, text.size(), dumpFp) != text.size()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Failed writing wallet dump file");
        }
    };
#else
    std::ofstream file;
    file.open(filepath.string().c_str(), std::ios::out | std::ios::binary);
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");
    auto writeDumpLine = [&](const std::string& text) {
        file << text;
        if (!file.good()) {
            throw JSONRPCError(RPC_MISC_ERROR, "Failed writing wallet dump file");
        }
    };
#endif

    std::map<CKeyID, int64_t> mapKeyBirth;
    pwalletMain->GetKeyBirthTimes(mapKeyBirth);
    const std::map<CKeyID, int64_t>& mapKeyPool = spk_man->GetAllReserveKeys();

    // sort time/key pairs
    std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
    for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++) {
        vKeyBirth.push_back(std::make_pair(it->second, it->first));
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    CBlockIndex* tip = chainActive.Tip();
    // produce output
    writeDumpLine(strprintf("# Wallet dump created by KristaTech %s (%s)\n", CLIENT_BUILD, CLIENT_DATE));
    writeDumpLine(strprintf("# * Created on %s\n", EncodeDumpTime(GetTime())));
    if (tip) {
        writeDumpLine(strprintf("# * Best block at time of backup was %i (%s),\n", tip->nHeight,
                                tip->GetBlockHash().ToString()));
        writeDumpLine(strprintf("#   mined on %s\n", EncodeDumpTime(tip->GetBlockTime())));
    } else {
        writeDumpLine("# Missing tip information\n");
    }
    writeDumpLine("\n");

    // Add the base58check encoded extended master if the wallet uses HD
    CKeyID seed_id = spk_man->GetHDChain().GetID();
    if (!seed_id.IsNull())
    {
        CKey seed;
        if (pwalletMain->GetKey(seed_id, seed)) {
            CExtKey masterKey;
            masterKey.SetSeed(seed.begin(), seed.size());
            seed = CKey();

            std::string encodedMaster = KeyIO::EncodeExtKey(masterKey);
            writeDumpLine(std::string("# extended private masterkey: ") + encodedMaster + "\n");
            cleanseString(encodedMaster);

            CExtKey purposeKey;
            CExtKey cointypeKey;
            CExtKey accountKey;
            CExtKey changeKey;
            masterKey.Derive(purposeKey, 44 | BIP32_HARDENED_KEY_LIMIT);
            purposeKey.Derive(cointypeKey, BIP32_HDCHAIN | BIP32_HARDENED_KEY_LIMIT);
            cointypeKey.Derive(accountKey, 0 | BIP32_HARDENED_KEY_LIMIT);
            accountKey.Derive(changeKey, HDChain::ChangeType::ECOMMERCE);

            writeDumpLine(std::string("# extended public masterkey: ") + KeyIO::EncodeExtPubKey(changeKey.Neuter()) + "\n\n");
            cleanseExtKey(changeKey);
            cleanseExtKey(accountKey);
            cleanseExtKey(cointypeKey);
            cleanseExtKey(purposeKey);
            cleanseExtKey(masterKey);
        }
    }

    for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++) {
        const CKeyID& keyid = it->second;
        std::string strTime = EncodeDumpTime(it->first);
        CKey key;
        if (pwalletMain->GetKey(keyid, key)) {
            const CKeyMetadata& metadata = pwalletMain->mapKeyMetadata[keyid];
            std::string strAddr = EncodeDestination(keyid);
            std::string strSecret = KeyIO::EncodeSecret(key);
            key = CKey();

            std::string metaSuffix;
            if (pwalletMain->mapAddressBook.count(keyid)) {
                auto entry = pwalletMain->mapAddressBook[keyid];
                metaSuffix = strprintf("label=%s", EncodeDumpString(entry.name));
            } else if (keyid == seed_id) {
                metaSuffix = "hdseed=1";
            } else if (mapKeyPool.count(keyid)) {
                metaSuffix = "reserve=1";
            } else {
                metaSuffix = "change=1";
            }
            std::string lineOut = strprintf("%s %s %s # addr=%s%s\n",
                                            strSecret, strTime, metaSuffix, strAddr,
                                            (metadata.HasKeyOrigin() ? " hdkeypath=" + metadata.key_origin.pathToString() : ""));
            cleanseString(strSecret);
            try {
                writeDumpLine(lineOut);
            } catch (...) {
                cleanseString(lineOut);
                throw;
            }
            cleanseString(lineOut);
        }
    }
    writeDumpLine("\n");
    writeDumpLine("# End of dump\n");

#ifndef WIN32
    if (std::fflush(dumpFp) != 0 || std::ferror(dumpFp)) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed flushing wallet dump file");
    }
    dumpGuard.fp = nullptr;
    if (::fclose(dumpFp) != 0) {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed closing wallet dump file");
    }
    dumpGuard.ok = true;
#else
    file.flush();
    file.close();
#endif

    UniValue reply(UniValue::VOBJ);
    reply.push_back(Pair("filename", filepath.string()));
    reply.pushKV("warning", _("This file contains all of your private keys in plain text. DO NOT send this file to anyone!"));

    return reply;
}

UniValue bip38encrypt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "bip38encrypt \"kristatechaddress\" \"passphrase\"\n"
            "\nEncrypts a private key corresponding to 'kristatechaddress'.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"kristatechaddress\"   (string, required) The kristatech address for the private key (you must hold the key already)\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with - Valid special chars: !#$%&'()*+,-./:;<=>?`{|}~ \n"

            "\nResult:\n"
            "\"key\"                (string) The encrypted private key\n"

            "\nExamples:\n" +
            HelpExampleCli("bip38encrypt", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\" \"mypasphrase\"") +
            HelpExampleRpc("bip38encrypt", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg6vgeS6\" \"mypasphrase\""));

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    std::string strAddress = request.params[0].get_str();
    std::string strPassphrase = request.params[1].get_str();

    CTxDestination address = DecodeDestination(strAddress);
    if (!IsValidDestination(address))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid KristaTech address");
    CKeyID* keyID = boost::get<CKeyID>(&address);
    if (!keyID)
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(*keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");

    uint256 privKey = vchSecret.GetPrivKey_256();
    std::string encryptedOut = BIP38_Encrypt(strAddress, strPassphrase, privKey, vchSecret.IsCompressed());

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("Addess", strAddress));
    result.push_back(Pair("Encrypted Key", encryptedOut));

    return result;
}

UniValue bip38decrypt(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "bip38decrypt \"kristatechaddress\" \"passphrase\"\n"
            "\nDecrypts and then imports password protected private key.\n" +
            HelpRequiringPassphrase() + "\n"

            "\nArguments:\n"
            "1. \"encryptedkey\"   (string, required) The encrypted private key\n"
            "2. \"passphrase\"   (string, required) The passphrase you want the private key to be encrypted with\n"

            "\nResult:\n"
            "\"key\"                (string) The decrypted private key\n"

            "\nExamples:\n" +
            HelpExampleCli("bip38decrypt", "\"encryptedkey\" \"mypassphrase\"") +
            HelpExampleRpc("bip38decrypt", "\"encryptedkey\" \"mypassphrase\""));

    if (!pwalletMain)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: No wallet loaded in the system");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    /** Collect private key and passphrase **/
    std::string strKey = request.params[0].get_str();
    std::string strPassphrase = request.params[1].get_str();

    uint256 privKey;
    bool fCompressed;
    if (!BIP38_Decrypt(strPassphrase, strKey, privKey, fCompressed))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed To Decrypt");

    UniValue result(UniValue::VOBJ);
    result.push_back(Pair("privatekey", HexStr(privKey)));

    CKey key;
    key.Set(privKey.begin(), privKey.end(), fCompressed);

    if (!key.IsValid())
        throw JSONRPCError(RPC_WALLET_ERROR, "Private Key Not Valid");

    CPubKey pubkey = key.GetPubKey();
    pubkey.IsCompressed();
    assert(key.VerifyPubKey(pubkey));
    result.push_back(Pair("Address", EncodeDestination(pubkey.GetID())));
    CKeyID vchAddress = pubkey.GetID();
    {
        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBook(vchAddress, "", AddressBook::AddressBookPurpose::RECEIVE);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
            throw JSONRPCError(RPC_WALLET_ERROR, "Key already held by wallet");

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'
        pwalletMain->ScanForWalletTransactions(chainActive.Genesis(), true);
    }

    return result;
}
