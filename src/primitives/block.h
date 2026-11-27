// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2015-2020 The PIVX developers
// Copyright (c) 2020-2026 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_BLOCK_H
#define BITCOIN_PRIMITIVES_BLOCK_H

#include "primitives/transaction.h"
#include "keystore.h"
#include "serialize.h"
#include "uint256.h"
#include "pubkey.h"

#include <atomic>
#include <ios>

/** Nodes collect new transactions into a block, hash them into a hash tree,
 * and scan through nonce values to make the block's hash satisfy proof-of-work
 * requirements.  When they solve the proof-of-work, they broadcast the block
 * to everyone and the block is added to the block chain.  The first transaction
 * in the block is a special one that creates a new coin owned by the creator
 * of the block.
 */
class CBlockHeader
{
public:
    // header
    static const int32_t CURRENT_VERSION=12;     //!> Version 12 includes POMBL upgrades
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;
    uint256 nAccumulatorCheckpoint;             // only for version 4, 5 and 6.

    // ADAM consensus fields
    std::vector<CPubKey> vAdamMiners;
    std::vector<std::vector<unsigned char>> vAdamSolutions;
    std::vector<unsigned char> vAdamVRFProof;
    std::vector<unsigned char> vAdamCoordinatorSig;

    // MPA consensus fields
    std::vector<unsigned char> vQuorumSig;

    CBlockHeader()
    {
        SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    static inline void ReadBoundedByteVector(Stream& s, std::vector<unsigned char>& vch, size_t nMaxSize, const char* pszField)
    {
        const uint64_t nSize = ::ReadCompactSize(s);
        if (nSize > nMaxSize) {
            throw std::ios_base::failure(std::string(pszField) + " size too large");
        }
        vch.resize(nSize);
        if (nSize > 0) {
            s.read((char*)vch.data(), nSize);
        }
    }

    template <typename Stream, typename Operation>
    inline void SerializeAdamMpaFields(Stream& s, Operation ser_action)
    {
        if (nVersion >= 11) {
            READWRITE(vAdamMiners);
            READWRITE(vAdamSolutions);
            READWRITE(vAdamVRFProof);
            if (!(s.GetType() & SER_GETHASH)) {
                READWRITE(vAdamCoordinatorSig);
            }
        }
        if (nVersion >= 12) {
            if (!(s.GetType() & SER_GETHASH)) {
                READWRITE(vQuorumSig);
            }
        }
    }

    template <typename Stream>
    inline void SerializeAdamMpaFields(Stream& s, CSerActionUnserialize)
    {
        if (nVersion >= 11) {
            const uint64_t nMiners = ::ReadCompactSize(s);
            if (nMiners > 32) {
                throw std::ios_base::failure("CBlockHeader vAdamMiners size too large");
            }
            vAdamMiners.resize(nMiners);
            for (uint64_t i = 0; i < nMiners; ++i) {
                std::vector<unsigned char> vchPk;
                ReadBoundedByteVector(s, vchPk, CPubKey::PUBLIC_KEY_SIZE, "CBlockHeader pubkey");
                vAdamMiners[i].Set(vchPk.begin(), vchPk.end());
            }

            const uint64_t nSolutions = ::ReadCompactSize(s);
            if (nSolutions > 32) {
                throw std::ios_base::failure("CBlockHeader vAdamSolutions size too large");
            }
            vAdamSolutions.resize(nSolutions);
            for (uint64_t i = 0; i < nSolutions; ++i) {
                ReadBoundedByteVector(s, vAdamSolutions[i], 512, "CBlockHeader solution");
            }

            ReadBoundedByteVector(s, vAdamVRFProof, 512, "CBlockHeader vAdamVRFProof");
            if (!(s.GetType() & SER_GETHASH)) {
                ReadBoundedByteVector(s, vAdamCoordinatorSig, 512, "CBlockHeader vAdamCoordinatorSig");
            }
        }
        if (nVersion >= 12) {
            if (!(s.GetType() & SER_GETHASH)) {
                ReadBoundedByteVector(s, vQuorumSig, 4096, "CBlockHeader vQuorumSig");
            }
        }
    }

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nVersion);
        READWRITE(hashPrevBlock);
        READWRITE(hashMerkleRoot);
        READWRITE(nTime);
        READWRITE(nBits);
        READWRITE(nNonce);

        // Header changes to include accumulator checksum
        if(nVersion > 3 && nVersion < 7)
            READWRITE(nAccumulatorCheckpoint);

        SerializeAdamMpaFields(s, ser_action);
    }

    void SetNull()
    {
        nVersion = CBlockHeader::CURRENT_VERSION;
        hashPrevBlock.SetNull();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
        nAccumulatorCheckpoint.SetNull();
        vAdamMiners.clear();
        vAdamSolutions.clear();
        vAdamVRFProof.clear();
        vAdamCoordinatorSig.clear();
        vQuorumSig.clear();
    }

    bool IsNull() const
    {
        return (nBits == 0);
    }

    uint256 GetHash() const;

    int64_t GetBlockTime() const
    {
        return (int64_t)nTime;
    }
};


class CBlock : public CBlockHeader
{
public:
    // network and disk
    std::vector<CTransaction> vtx;

    // ppcoin: block signature - signed by one of the coin base txout[N]'s owner
    std::vector<unsigned char> vchBlockSig;

    // memory only
    struct CopyableAtomicBool {
        std::atomic<bool> val{false};
        CopyableAtomicBool() noexcept = default;
        CopyableAtomicBool(bool v) noexcept : val(v) {}
        CopyableAtomicBool(const CopyableAtomicBool& o) noexcept : val(o.val.load(std::memory_order_relaxed)) {}
        CopyableAtomicBool& operator=(const CopyableAtomicBool& o) noexcept {
            val.store(o.val.load(std::memory_order_relaxed), std::memory_order_relaxed);
            return *this;
        }
        CopyableAtomicBool& operator=(bool v) noexcept {
            val.store(v, std::memory_order_relaxed);
            return *this;
        }
        operator bool() const noexcept { return val.load(std::memory_order_relaxed); }
    };
    mutable CopyableAtomicBool fChecked;

    CBlock()
    {
        SetNull();
    }

    CBlock(const CBlockHeader &header)
    {
        SetNull();
        *((CBlockHeader*)this) = header;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(*(CBlockHeader*)this);
        READWRITE(vtx);
	if(vtx.size() > 1 && vtx[1].IsCoinStake())
		READWRITE(vchBlockSig);
    }

    void SetNull()
    {
        CBlockHeader::SetNull();
        vtx.clear();
        fChecked = false;
        vchBlockSig.clear();
    }

    CBlockHeader GetBlockHeader() const
    {
        CBlockHeader block;
        block.nVersion       = nVersion;
        block.hashPrevBlock  = hashPrevBlock;
        block.hashMerkleRoot = hashMerkleRoot;
        block.nTime          = nTime;
        block.nBits          = nBits;
        block.nNonce         = nNonce;
        if(nVersion > 3 && nVersion < 7)
            block.nAccumulatorCheckpoint = nAccumulatorCheckpoint;
        if (nVersion >= 11) {
            block.vAdamMiners = vAdamMiners;
            block.vAdamSolutions = vAdamSolutions;
            block.vAdamVRFProof = vAdamVRFProof;
            block.vAdamCoordinatorSig = vAdamCoordinatorSig;
        }
        if (nVersion >= 12) {
            block.vQuorumSig = vQuorumSig;
        }
        return block;
    }

    bool IsProofOfStake() const
    {
        return (vtx.size() > 1 && vtx[1].IsCoinStake());
    }

    bool IsProofOfWork() const
    {
        return !IsProofOfStake();
    }

    std::string ToString() const;
    void print() const;
};


/** Describes a place in the block chain to another node such that if the
 * other node doesn't have the same branch, it can find a recent common trunk.
 * The further back it is, the further before the fork it may be.
 */
struct CBlockLocator
{
    std::vector<uint256> vHave;

    CBlockLocator() {}

    CBlockLocator(const std::vector<uint256>& vHaveIn)
    {
        vHave = vHaveIn;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        int nVersion = s.GetVersion();
        if (!(s.GetType() & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vHave);
    }

    void SetNull()
    {
        vHave.clear();
    }

    bool IsNull()
    {
        return vHave.empty();
    }
};

#endif // BITCOIN_PRIMITIVES_BLOCK_H
