// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"

#include "pubkey.h"
#include "script/script.h"
#include "script/mescal.h"
#include <univalue.h>
#include "util.h"
#include "utilstrencodings.h"

typedef std::vector<unsigned char> valtype;

unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t)
{
    switch (t)
    {
    case TX_NONSTANDARD: return "nonstandard";
    case TX_PUBKEY: return "pubkey";
    case TX_PUBKEYHASH: return "pubkeyhash";
    case TX_SCRIPTHASH: return "scripthash";
    case TX_MULTISIG: return "multisig";
    case TX_NULL_DATA: return "nulldata";
    case TX_WITNESS_V1_TAPROOT: return "witness_v1_taproot";
    case TX_CONTRACT_PUBLISH: return "contractpublish";
    case TX_CONTRACT_RUN: return "contractrun";
    case TX_CONTRACT_STATUS: return "contractstatus";
    case TX_MESCAL_CONTRACT: return "mescalcontract";
    case TX_ADAM_COINLOCK: return "adam_coinlock";
    case TX_ADAM_POWLOCK: return "adam_powlock";
    }
    return NULL;
}

static bool MatchPayToPubkey(const CScript& script, valtype& pubkey)
{
    if (script.size() == CPubKey::PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    if (script.size() == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 2 && script[0] == CPubKey::COMPRESSED_PUBLIC_KEY_SIZE && script.back() == OP_CHECKSIG) {
        pubkey = valtype(script.begin() + 1, script.begin() + CPubKey::COMPRESSED_PUBLIC_KEY_SIZE + 1);
        return CPubKey::ValidSize(pubkey);
    }
    return false;
}

static bool MatchPayToPubkeyHash(const CScript& script, valtype& pubkeyhash)
{
    if (script.size() == 25 && script[0] == OP_DUP && script[1] == OP_HASH160 && script[2] == 20 && script[23] == OP_EQUALVERIFY && script[24] == OP_CHECKSIG) {
        pubkeyhash = valtype(script.begin () + 3, script.begin() + 23);
        return true;
    }
    return false;
}

/** Test for "small positive integer" script opcodes - OP_1 through OP_16. */
static constexpr bool IsSmallInteger(opcodetype opcode)
{
    return opcode >= OP_1 && opcode <= OP_16;
}

static bool MatchMultisig(const CScript& script, unsigned int& required, std::vector<valtype>& pubkeys)
{
    opcodetype opcode;
    valtype data;
    CScript::const_iterator it = script.begin();
    if (script.size() < 1 || script.back() != OP_CHECKMULTISIG) return false;

    if (!script.GetOp(it, opcode, data) || !IsSmallInteger(opcode)) return false;
    required = CScript::DecodeOP_N(opcode);
    while (script.GetOp(it, opcode, data) && CPubKey::ValidSize(data)) {
        pubkeys.emplace_back(std::move(data));
    }
    if (!IsSmallInteger(opcode)) return false;
    unsigned int keys = CScript::DecodeOP_N(opcode);
    if (pubkeys.size() != keys || keys < required) return false;
    return (it + 1 == script.end());
}

static bool MatchContractPublish(const CScript& script) {
    if (script.size() < 2 || script.back() != OP_PUBLISH) return false;
    CScript::const_iterator pc = script.begin();
    CScript::const_iterator end_minus_one = script.end() - 1;
    while (pc < end_minus_one) {
        opcodetype opcode;
        std::vector<unsigned char> vch;
        if (!script.GetOp(pc, opcode, vch)) return false;
        if (opcode > OP_16) return false;
    }
    return true;
}

static bool MatchContractRun(const CScript& script) {
    if (script.size() < 2 || script.back() != OP_RUN) return false;
    CScript::const_iterator pc = script.begin();
    CScript::const_iterator end_minus_one = script.end() - 1;
    while (pc < end_minus_one) {
        opcodetype opcode;
        std::vector<unsigned char> vch;
        if (!script.GetOp(pc, opcode, vch)) return false;
        if (opcode > OP_16) return false;
    }
    return true;
}

static bool MatchContractStatus(const CScript& script) {
    if (script.size() < 2 || script.back() != OP_UPDATE_STATUS) return false;
    CScript::const_iterator pc = script.begin();
    CScript::const_iterator end_minus_one = script.end() - 1;
    while (pc < end_minus_one) {
        opcodetype opcode;
        std::vector<unsigned char> vch;
        if (!script.GetOp(pc, opcode, vch)) return false;
        if (opcode > OP_16) return false;
    }
    return true;
}

bool MatchCoinLockRegistration(const CScript& script, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut) {
    CScript::const_iterator pc = script.begin();
    opcodetype op;
    std::vector<unsigned char> vchPubKey;
    std::vector<unsigned char> vchLockTime;
    std::vector<unsigned char> vchHash;

    // 1. <pubkey>
    if (!script.GetOp(pc, op, vchPubKey) || vchPubKey.size() != 33) return false;
    // 2. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 3. <locktime>
    if (!script.GetOp(pc, op, vchLockTime)) return false;
    // 4. OP_CHECKLOCKTIMEVERIFY
    if (!script.GetOp(pc, op) || op != OP_CHECKLOCKTIMEVERIFY) return false;
    // 5. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 6. OP_DUP
    if (!script.GetOp(pc, op) || op != OP_DUP) return false;
    // 7. OP_HASH160
    if (!script.GetOp(pc, op) || op != OP_HASH160) return false;
    // 8. <pubkeyhash>
    if (!script.GetOp(pc, op, vchHash) || vchHash.size() != 20) return false;
    // 9. OP_EQUALVERIFY
    if (!script.GetOp(pc, op) || op != OP_EQUALVERIFY) return false;
    // 10. OP_CHECKSIG
    if (!script.GetOp(pc, op) || op != OP_CHECKSIG) return false;
    // Ensure we reached the end of the script
    if (pc != script.end()) return false;

    pubkeyOut = CPubKey(vchPubKey);
    if (!pubkeyOut.IsValid()) return false;

    try {
        lockTimeOut = CScriptNum(vchLockTime, true).getint64();
    } catch (...) {
        return false;
    }

    pubkeyHashOut = CKeyID(uint160(vchHash));
    return true;
}

bool MatchPoWLockRegistration(const CScript& script, std::vector<unsigned char>& nonceOut, uint256& challengeOut, CPubKey& pubkeyOut, int64_t& lockTimeOut, CKeyID& pubkeyHashOut) {
    CScript::const_iterator pc = script.begin();
    opcodetype op;
    std::vector<unsigned char> vchNonce;
    std::vector<unsigned char> vchChallenge;
    std::vector<unsigned char> vchPubKey;
    std::vector<unsigned char> vchLockTime;
    std::vector<unsigned char> vchHash;

    // 1. <nonce>
    if (!script.GetOp(pc, op, vchNonce) || vchNonce.empty()) return false;
    // 2. <challenge>
    if (!script.GetOp(pc, op, vchChallenge) || vchChallenge.size() != 32) return false;
    // 3. <pubkey>
    if (!script.GetOp(pc, op, vchPubKey) || vchPubKey.size() != 33) return false;
    // 4. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 5. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 6. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 7. <locktime>
    if (!script.GetOp(pc, op, vchLockTime)) return false;
    // 8. OP_CHECKLOCKTIMEVERIFY
    if (!script.GetOp(pc, op) || op != OP_CHECKLOCKTIMEVERIFY) return false;
    // 9. OP_DROP
    if (!script.GetOp(pc, op) || op != OP_DROP) return false;
    // 10. OP_DUP
    if (!script.GetOp(pc, op) || op != OP_DUP) return false;
    // 11. OP_HASH160
    if (!script.GetOp(pc, op) || op != OP_HASH160) return false;
    // 12. <pubkeyhash>
    if (!script.GetOp(pc, op, vchHash) || vchHash.size() != 20) return false;
    // 13. OP_EQUALVERIFY
    if (!script.GetOp(pc, op) || op != OP_EQUALVERIFY) return false;
    // 14. OP_CHECKSIG
    if (!script.GetOp(pc, op) || op != OP_CHECKSIG) return false;
    // Ensure end of script
    if (pc != script.end()) return false;

    nonceOut = vchNonce;
    challengeOut = uint256(vchChallenge);
    pubkeyOut = CPubKey(vchPubKey);
    if (!pubkeyOut.IsValid()) return false;

    try {
        lockTimeOut = CScriptNum(vchLockTime, true).getint64();
    } catch (...) {
        return false;
    }

    pubkeyHashOut = CKeyID(uint160(vchHash));
    return true;
}

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<std::vector<unsigned char> >& vSolutionsRet)
{
    vSolutionsRet.clear();

    // Shortcut for pay-to-script-hash, which are more constrained than the other types:
    // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
    if (scriptPubKey.IsPayToScriptHash())
    {
        typeRet = TX_SCRIPTHASH;
        std::vector<unsigned char> hashBytes(scriptPubKey.begin()+2, scriptPubKey.begin()+22);
        vSolutionsRet.push_back(hashBytes);
        return true;
    }

    // Check for Pay-to-Taproot (P2TR)
    if (scriptPubKey.size() == 34 && scriptPubKey[0] == OP_1 && scriptPubKey[1] == 32) {
        typeRet = TX_WITNESS_V1_TAPROOT;
        std::vector<unsigned char> xonlyBytes(scriptPubKey.begin() + 2, scriptPubKey.end());
        vSolutionsRet.push_back(std::move(xonlyBytes));
        return true;
    }

    // Provably prunable, data-carrying output
    //
    // So long as script passes the IsUnspendable() test and all but the first
    // byte passes the IsPushOnly() test we don't care what exactly is in the
    // script.
    if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin()+1)) {
        typeRet = TX_NULL_DATA;
        return true;
    }

    std::vector<unsigned char> data;
    if (MatchPayToPubkey(scriptPubKey, data)) {
        typeRet = TX_PUBKEY;
        vSolutionsRet.push_back(std::move(data));
        return true;
    }

    if (MatchPayToPubkeyHash(scriptPubKey, data)) {
        typeRet = TX_PUBKEYHASH;
        vSolutionsRet.push_back(std::move(data));
        return true;
    }

    unsigned int required;
    std::vector<std::vector<unsigned char>> keys;
    if (MatchMultisig(scriptPubKey, required, keys)) {
        typeRet = TX_MULTISIG;
        vSolutionsRet.push_back({static_cast<unsigned char>(required)}); // safe as required is in range 1..16
        vSolutionsRet.insert(vSolutionsRet.end(), keys.begin(), keys.end());
        vSolutionsRet.push_back({static_cast<unsigned char>(keys.size())}); // safe as size is in range 1..16
        return true;
    }

    if (MatchContractPublish(scriptPubKey)) {
        typeRet = TX_CONTRACT_PUBLISH;
        return true;
    }
    if (MatchContractRun(scriptPubKey)) {
        typeRet = TX_CONTRACT_RUN;
        return true;
    }
    if (MatchContractStatus(scriptPubKey)) {
        typeRet = TX_CONTRACT_STATUS;
        return true;
    }

    std::string mescalErr;
    UniValue decompiled = CMescal::Decompile(scriptPubKey, mescalErr);
    if (mescalErr.empty() && !decompiled.isNull() && decompiled.exists("actions")) {
        typeRet = TX_MESCAL_CONTRACT;
        return true;
    }

    CPubKey pubkey;
    int64_t lockTime = 0;
    CKeyID pubkeyHash;
    if (MatchCoinLockRegistration(scriptPubKey, pubkey, lockTime, pubkeyHash)) {
        typeRet = TX_ADAM_COINLOCK;
        vSolutionsRet.push_back(ToByteVector(pubkeyHash));
        vSolutionsRet.push_back(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
        return true;
    }

    std::vector<unsigned char> nonce;
    uint256 challenge;
    if (MatchPoWLockRegistration(scriptPubKey, nonce, challenge, pubkey, lockTime, pubkeyHash)) {
        typeRet = TX_ADAM_POWLOCK;
        vSolutionsRet.push_back(ToByteVector(pubkeyHash));
        vSolutionsRet.push_back(std::vector<unsigned char>(pubkey.begin(), pubkey.end()));
        return true;
    }

    vSolutionsRet.clear();
    typeRet = TX_NONSTANDARD;
    return false;
}

int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<unsigned char> >& vSolutions)
{
    switch (t)
    {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
    case TX_CONTRACT_PUBLISH:
    case TX_CONTRACT_RUN:
    case TX_CONTRACT_STATUS:
    case TX_MESCAL_CONTRACT:
        return -1;
    case TX_PUBKEY:
        return 1;
    case TX_PUBKEYHASH:
    case TX_ADAM_COINLOCK:
    case TX_ADAM_POWLOCK:
        return 2;
    case TX_MULTISIG:
        if (vSolutions.size() < 1 || vSolutions[0].size() < 1)
            return -1;
        return vSolutions[0][0] + 1;
    case TX_SCRIPTHASH:
        return 1; // doesn't include args needed by the script
    case TX_WITNESS_V1_TAPROOT:
        return 1;
    }
    return -1;
}

bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet)
{
    std::vector<valtype> vSolutions;
    txnouttype whichType;
    if (!Solver(scriptPubKey, whichType, vSolutions))
        return false;

    if (whichType == TX_PUBKEY) {
        CPubKey pubKey(vSolutions[0]);
        if (!pubKey.IsValid())
            return false;

        addressRet = pubKey.GetID();
        return true;

    } else if (whichType == TX_PUBKEYHASH || whichType == TX_ADAM_COINLOCK || whichType == TX_ADAM_POWLOCK) {
        addressRet = CKeyID(uint160(vSolutions[0]));
        return true;

    } else if (whichType == TX_SCRIPTHASH) {
        addressRet = CScriptID(uint160(vSolutions[0]));
        return true;

    } else if (whichType == TX_WITNESS_V1_TAPROOT) {
        addressRet = WitnessV1Taproot(CXOnlyPubKey(vSolutions[0].data(), vSolutions[0].size()));
        return true;
    }
    // Multisig txns have more than one address...
    return false;
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, std::vector<CTxDestination>& addressRet, int& nRequiredRet)
{
    addressRet.clear();
    typeRet = TX_NONSTANDARD;
    std::vector<valtype> vSolutions;
    if (!Solver(scriptPubKey, typeRet, vSolutions))
        return false;
    if (typeRet == TX_NULL_DATA){
        // This is data, not addresses
        return false;
    }

    if (typeRet == TX_MULTISIG)
    {
        nRequiredRet = vSolutions.front()[0];
        for (unsigned int i = 1; i < vSolutions.size()-1; i++)
        {
            CPubKey pubKey(vSolutions[i]);
            if (!pubKey.IsValid())
                continue;

            CTxDestination address = pubKey.GetID();
            addressRet.push_back(address);
        }

        if (addressRet.empty())
            return false;

    } else
    {
        nRequiredRet = 1;
        CTxDestination address;
        if (!ExtractDestination(scriptPubKey, address))
           return false;
        addressRet.push_back(address);
    }

    return true;
}

namespace
{
class CScriptVisitor : public boost::static_visitor<bool>
{
private:
    CScript *script;
public:
    CScriptVisitor(CScript *scriptin) { script = scriptin; }

    bool operator()(const CNoDestination &dest) const {
        script->clear();
        return false;
    }

    bool operator()(const CKeyID &keyID) const {
        script->clear();
        *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
        return true;
    }

    bool operator()(const CScriptID &scriptID) const {
        script->clear();
        *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
        return true;
    }

    bool operator()(const WitnessV1Taproot &dest) const {
        script->clear();
        *script << OP_1 << ToByteVector(dest);
        return true;
    }
};
}

CScript GetScriptForDestination(const CTxDestination& dest)
{
    CScript script;

    boost::apply_visitor(CScriptVisitor(&script), dest);
    return script;
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey)
{
    return CScript() << std::vector<unsigned char>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys)
{
    CScript script;

    script << CScript::EncodeOP_N(nRequired);
    for (const CPubKey& key : keys)
        script << ToByteVector(key);
    script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
    return script;
}

bool IsValidDestination(const CTxDestination& dest) {
    return dest.which() != 0;
}
