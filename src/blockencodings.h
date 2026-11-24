// Copyright (c) 2016 The Bitcoin Core developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_BLOCKENCODINGS_H
#define BITCOIN_BLOCKENCODINGS_H

#include "primitives/block.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <algorithm>
#include <ios>
#include <limits>
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
            if (idx > std::numeric_limits<uint16_t>::max()) {
                throw std::ios_base::failure("PrefilledTransaction index overflow");
            }
            index = static_cast<uint16_t>(idx);
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
            if (shorttxids_size > std::numeric_limits<uint16_t>::max()) {
                throw std::ios_base::failure("CBlockHeaderAndShortTxIDs shorttxids size too large");
            }
            shorttxids.clear();
            shorttxids.reserve(std::min<size_t>(shorttxids_size, 4096));
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
                shorttxids.push_back((uint64_t)bytes[0] |
                                     ((uint64_t)bytes[1] << 8) |
                                     ((uint64_t)bytes[2] << 16) |
                                     ((uint64_t)bytes[3] << 24) |
                                     ((uint64_t)bytes[4] << 32) |
                                     ((uint64_t)bytes[5] << 40));
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
            if (indexes_size > std::numeric_limits<uint16_t>::max()) {
                throw std::ios_base::failure("BlockTransactionsRequest indexes size too large");
            }
            indexes.clear();
            indexes.reserve(std::min<size_t>(indexes_size, 4096));
        }
        uint32_t prev = 0;
        for (size_t i = 0; i < indexes_size; i++) {
            if (ser_action.ForRead()) {
                uint64_t diff = 0;
                READWRITE(COMPACTSIZE(diff));
                if (diff > std::numeric_limits<uint16_t>::max()) {
                    throw std::ios_base::failure("BlockTransactionsRequest index overflow");
                }
                uint64_t next = (i == 0) ? diff : (static_cast<uint64_t>(prev) + diff + 1);
                if (next > std::numeric_limits<uint16_t>::max()) {
                    throw std::ios_base::failure("BlockTransactionsRequest index overflow");
                }
                indexes.push_back(static_cast<uint16_t>(next));
                prev = static_cast<uint32_t>(next);
            } else {
                if (i > 0 && indexes[i] <= prev) {
                    throw std::ios_base::failure("BlockTransactionsRequest indexes not strictly increasing");
                }
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
