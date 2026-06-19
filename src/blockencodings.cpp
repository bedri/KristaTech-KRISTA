// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockencodings.h"
#include "hash.h"
#include "random.h"

CBlockHeaderAndShortTxIDs::CBlockHeaderAndShortTxIDs() : nonce(0), shorttxidk0(0), shorttxidk1(0) {}

CBlockHeaderAndShortTxIDs::CBlockHeaderAndShortTxIDs(const CBlock& block) {
    header = block.GetBlockHeader();
    
    // Choose a random 64-bit nonce
    GetRandBytes((unsigned char*)&nonce, sizeof(nonce));
    
    FillKeys();
    
    // Prefill coinbase (vtx[0]) and coinstake (vtx[1]) if it exists
    uint32_t prev_absolute = 0;
    for (size_t i = 0; i < block.vtx.size(); i++) {
        bool prefill = false;
        if (i == 0 || (i == 1 && block.vtx[1].IsCoinStake())) {
            prefill = true;
        }
        if (prefill) {
            PrefilledTransaction ptx;
            if (prefilledtxn.empty()) {
                ptx.index = i;
            } else {
                ptx.index = i - prev_absolute - 1;
            }
            ptx.tx = block.vtx[i];
            prefilledtxn.push_back(ptx);
            prev_absolute = i;
        }
    }
    
    // Calculate short txids for the remaining transactions
    for (size_t i = 0; i < block.vtx.size(); i++) {
        bool is_prefilled = false;
        if (i == 0 || (i == 1 && block.vtx[1].IsCoinStake())) {
            is_prefilled = true;
        }
        if (!is_prefilled) {
            shorttxids.push_back(GetShortID(block.vtx[i].GetHash()));
        }
    }

    if (block.vtx.size() > 1 && block.vtx[1].IsCoinStake()) {
        vchBlockSig = block.vchBlockSig;
    }
}

void CBlockHeaderAndShortTxIDs::FillKeys() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << header.GetHash() << nonce;
    uint256 hasher_hash = ss.GetHash();
    shorttxidk0 = hasher_hash.GetUint64(0);
    shorttxidk1 = hasher_hash.GetUint64(1);
}

uint64_t CBlockHeaderAndShortTxIDs::GetShortID(const uint256& txhash) const {
    if (shorttxidk0 == 0 && shorttxidk1 == 0) {
        FillKeys();
    }
    return SipHashUint256(shorttxidk0, shorttxidk1, txhash) & 0x0000ffffffffffffULL;
}
