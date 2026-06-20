// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKENCODINGS_H
#define BITCOIN_BLOCKENCODINGS_H

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

struct PrefilledTransaction {
    uint16_t index; // Differential index (absolute index for first, diff - 1 for others)
    CTransaction tx;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        uint64_t idx = index;
        READWRITE(COMPACTSIZE(idx));
        if (ser_action.ForRead()) {
            index = idx;
        }
        READWRITE(tx);
    }
};

class CBlockHeaderAndShortTxIDs {
private:
    mutable uint64_t shorttxidk0, shorttxidk1;
    void FillKeys() const;

public:
    CBlockHeader header;
    uint64_t nonce;
    std::vector<uint64_t> shorttxids;
    std::vector<PrefilledTransaction> prefilledtxn;
    std::vector<unsigned char> vchBlockSig; // Block signature for Proof of Stake

    CBlockHeaderAndShortTxIDs();
    CBlockHeaderAndShortTxIDs(const CBlock& block);

    uint64_t GetShortID(const uint256& txhash) const;

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(header);
        READWRITE(nonce);
        
        uint64_t shorttxids_size = shorttxids.size();
        READWRITE(COMPACTSIZE(shorttxids_size));
        if (ser_action.ForRead()) {
            shorttxids.resize(shorttxids_size);
        }
        for (size_t i = 0; i < shorttxids_size; i++) {
            unsigned char bytes[6];
            if (!ser_action.ForRead()) {
                bytes[0] = shorttxids[i] & 0xff;
                bytes[1] = (shorttxids[i] >> 8) & 0xff;
                bytes[2] = (shorttxids[i] >> 16) & 0xff;
                bytes[3] = (shorttxids[i] >> 24) & 0xff;
                bytes[4] = (shorttxids[i] >> 32) & 0xff;
                bytes[5] = (shorttxids[i] >> 40) & 0xff;
            }
            READWRITE(FLATDATA(bytes));
            if (ser_action.ForRead()) {
                shorttxids[i] = (uint64_t)bytes[0] |
                                ((uint64_t)bytes[1] << 8) |
                                ((uint64_t)bytes[2] << 16) |
                                ((uint64_t)bytes[3] << 24) |
                                ((uint64_t)bytes[4] << 32) |
                                ((uint64_t)bytes[5] << 40);
            }
        }
        
        READWRITE(prefilledtxn);

        bool is_pos = false;
        if (ser_action.ForRead()) {
            is_pos = (prefilledtxn.size() > 1 && prefilledtxn[1].tx.IsCoinStake());
        } else {
            is_pos = (prefilledtxn.size() > 1 && prefilledtxn[1].tx.IsCoinStake());
        }
        if (is_pos) {
            READWRITE(vchBlockSig);
        }
    }
};

class BlockTransactionsRequest {
public:
    uint256 blockhash;
    std::vector<uint16_t> indexes; // Differential indexes

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockhash);
        uint64_t indexes_size = indexes.size();
        READWRITE(COMPACTSIZE(indexes_size));
        if (ser_action.ForRead()) {
            indexes.resize(indexes_size);
        }
        uint32_t prev = 0;
        for (size_t i = 0; i < indexes_size; i++) {
            if (ser_action.ForRead()) {
                uint64_t diff = 0;
                READWRITE(COMPACTSIZE(diff));
                if (i == 0) {
                    indexes[i] = diff;
                } else {
                    indexes[i] = diff + prev + 1;
                }
                prev = indexes[i];
            } else {
                uint64_t diff = (i == 0) ? indexes[i] : (indexes[i] - prev - 1);
                READWRITE(COMPACTSIZE(diff));
                prev = indexes[i];
            }
        }
    }
};

class BlockTransactions {
public:
    uint256 blockhash;
    std::vector<CTransaction> txn;

    BlockTransactions() {}
    BlockTransactions(const BlockTransactionsRequest& req) : blockhash(req.blockhash) {}

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockhash);
        READWRITE(txn);
    }
};

#endif // BITCOIN_BLOCKENCODINGS_H
