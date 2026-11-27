// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blocksignature.h"
#include "adam.h"
#include "main.h"
#include "chainparams.h"
#include "crypto/bls.h"
#include "wallet/wallet.h"
#include "masternodeman.h"

bool SignBlockWithKey(CBlock& block, const CKey& key, const CBLSSecretKey& blsKey, int nHeight)
{
    if (!blsKey.IsValid())
        return error("%s: invalid BLS key passed for signing", __func__);
    const std::string dst = GetBLSDomainTag(BLSDomain::POS_BLOCK, IsHardeningV3Active(nHeight));
    if (!SignBLSWithECDSAFallback(block.GetHash(), key, blsKey, block.vchBlockSig, dst))
        return error("%s: failed to sign block hash with key", __func__);

    return true;
}

bool SignBlock(CBlock& block, const CKeyStore& keystore, int nHeight)
{
    CKeyID keyID;
    if (block.IsProofOfWork()) {
        bool fFoundID = false;
        for (const CTxOut& txout :block.vtx[0].vout) {
            if (!txout.GetKeyIDFromUTXO(keyID))
                continue;
            fFoundID = true;
            break;
        }
        if (!fFoundID)
            return error("%s: failed to find key for PoW", __func__);
    } else {
        if (!block.vtx[1].vout[1].GetKeyIDFromUTXO(keyID))
            return error("%s: failed to find key for PoS", __func__);
    }

    CKey key;
    if (!keystore.GetKey(keyID, key))
        return error("%s: failed to get key from keystore", __func__);

    CBLSSecretKey blsKey = DeriveBLSFromCKey(key);
    if (!blsKey.IsValid()) {
        const CWallet* pwallet = dynamic_cast<const CWallet*>(&keystore);
        if (pwallet) {
            LOCK(pwallet->cs_wallet);
            pwallet->GetBLSKey(keyID, blsKey);
        }
    }
    if (!blsKey.IsValid()) {
        for (const auto& activeMasternode : amnodeman.GetActiveMasternodesCopy()) {
            if (activeMasternode.pubKeyMasternode.GetID() == keyID && activeMasternode.blsKeyMasternode.IsValid()) {
                blsKey = activeMasternode.blsKeyMasternode;
                break;
            }
        }
    }

    if (!blsKey.IsValid())
        return error("%s: no direct BLS key associated with staking/mining key %s", __func__, keyID.ToString());

    return SignBlockWithKey(block, key, blsKey, nHeight);
}

bool CheckBlockSignature(const CBlock& block, const bool enableP2PKH, int nHeight)
{
    // if we have already a checkpoint newer than this block 
    // then bypass the signature check
    if (block.nTime <= Params().Checkpoints().nTimeLastCheckpoint)
        return true;

    if (block.IsProofOfWork())
        return block.vchBlockSig.empty();

    if (block.vchBlockSig.empty())
        return error("%s: vchBlockSig is empty!", __func__);

    /** Each block is signed by the private key of the input that is staked.
     * UTXO: The public key that signs must match the public key associated with the first utxo of the coinstake tx.
     */
    if (block.vtx.size() < 2 || block.vtx[1].vout.size() < 2)
        return false;

    CPubKey pubkey;

    txnouttype whichType;
    std::vector<valtype> vSolutions;
    const CTxOut& txout = block.vtx[1].vout[1];
    if (!Solver(txout.scriptPubKey, whichType, vSolutions))
        return false;

    if (!enableP2PKH) {
        // Before v5 activation, P2PKH was always failing.
        if (whichType == TX_PUBKEYHASH) {
            return false;
        }
    }

    if (whichType == TX_PUBKEY) {
        if (vSolutions.empty())
            return false;
        valtype& vchPubKey = vSolutions[0];
        pubkey = CPubKey(vchPubKey);
    } else if (whichType == TX_PUBKEYHASH || whichType == TX_CLTV_TIMELOCK) {
        if (block.vtx[1].vin.empty())
            return false;
        const CTxIn& txin = block.vtx[1].vin[0];
        // Check if the scriptSig is for a p2pk or a p2pkh
        if (txin.scriptSig.size() == 73 || txin.scriptSig.empty()) { // Sig size + DER signature size, or empty scriptSig.
            // If the input is for a p2pk and the output is a p2pkh.
            // We don't have the pubkey to verify the block sig anywhere in this block.
            // p2pk scriptsig only contains the signature and p2pkh scriptpubkey only contain the hash.
            return false;
        } else {
            size_t start = 1 + static_cast<size_t>(txin.scriptSig[0]); // skip sig
            if (start + 1 >= txin.scriptSig.size())
                return false;
            pubkey = CPubKey(txin.scriptSig.begin() + start + 1, txin.scriptSig.end());
            if (whichType == TX_CLTV_TIMELOCK && (vSolutions.empty() || pubkey.GetID() != CKeyID(uint160(vSolutions[0]))))
                return false;
        }
    }

    if (!pubkey.IsValid())
        return error("%s: invalid pubkey %s", __func__, HexStr(pubkey));

    const bool fHardeningV3 = IsHardeningV3Active(nHeight);
    return VerifyBLSWithECDSAFallback(block.GetHash(), pubkey, block.vchBlockSig,
                                      GetBLSDomainTag(BLSDomain::POS_BLOCK, fHardeningV3),
                                      fHardeningV3,
                                      MakeActiveChainPinPredicate(block.hashPrevBlock));
}
