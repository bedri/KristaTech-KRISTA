// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2019 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"

#include "hash.h"
#include "script/standard.h"
#include "script/sign.h"
#include "tinyformat.h"
#include "utilstrencodings.h"
#include "util.h"
#include "arith_uint256.h"
#include "adam.h"
#include "streams.h"

// TODO: Change X11KVS algorithm call to whatever the coin being adapted is used.
uint256 CBlockHeader::GetHash() const
{
    if (nVersion == 11 || nVersion == 12) {
        bool fFallbackMode = (nVersion == 11);
        int M = 0;
        if (fFallbackMode) {
            M = (int)vAdamMiners.size() - 1;
        } else {
            M = (int)vAdamMiners.size();
        }

        if (M <= 0) {
            return SerializeHash(*this);
        }

        uint256 H_prev;

        // Round 0: Serialize block header (excluding coordinator signature and quorum signature via SER_GETHASH type)
        // Hash it using miner 0's algorithm
        CDataStream ssHeader(SER_GETHASH, PROTOCOL_VERSION);
        ssHeader << *this;

        int algo0 = 12;
        if (fFallbackMode) {
            CHashWriter ssAlgo(SER_GETHASH, 0);
            ssAlgo << hashPrevBlock;
            ssAlgo << vAdamMiners[0];
            uint256 h = ssAlgo.GetHash();
            algo0 = (UintToArith256(h) % 18).GetLow64();
        } else {
            algo0 = 0 % 13;
        }

        uint256 H_0 = CalculateAdamPuzzleHash(algo0, (const unsigned char*)&ssHeader[0], (const unsigned char*)&ssHeader[0] + ssHeader.size());

        // Derive multiplier m_0
        CHashWriter ssRound0(SER_GETHASH, PROTOCOL_VERSION);
        ssRound0 << hashPrevBlock;
        ssRound0 << (uint32_t)0;
        uint256 roundHash0 = ssRound0.GetHash();
        uint32_t v_0 = roundHash0.begin()[0];
        uint32_t m_0 = v_0 | 1;
        if (m_0 < 3) m_0 = 3;

        arith_uint256 bnHash = UintToArith256(H_0);
        bnHash *= m_0;
        H_prev = ArithToUint256(bnHash);

        // Rounds 1 to M-1
        for (int i = 1; i < M; ++i) {
            int algo_i = 12;
            if (fFallbackMode) {
                CHashWriter ssAlgo(SER_GETHASH, 0);
                ssAlgo << hashPrevBlock;
                ssAlgo << vAdamMiners[i];
                uint256 h = ssAlgo.GetHash();
                algo_i = (UintToArith256(h) % 18).GetLow64();
            } else {
                algo_i = i % 13;
            }

            uint256 H_i = CalculateAdamPuzzleHash(algo_i, H_prev.begin(), H_prev.begin() + H_prev.size());

            // Derive multiplier m_i
            CHashWriter ssRound_i(SER_GETHASH, PROTOCOL_VERSION);
            ssRound_i << hashPrevBlock;
            ssRound_i << (uint32_t)i;
            uint256 roundHash_i = ssRound_i.GetHash();
            uint32_t v_i = roundHash_i.begin()[0];
            uint32_t m_i = v_i | 1;
            if (m_i < 3) m_i = 3;

            arith_uint256 bnHash_i = UintToArith256(H_i);
            bnHash_i *= m_i;
            H_prev = ArithToUint256(bnHash_i);
        }

        return H_prev;
    }

    if (nVersion < 4)  { // nVersion = 1, 2, 3
#if defined(WORDS_BIGENDIAN)
        uint8_t data[80];
        WriteLE32(&data[0], nVersion);
        memcpy(&data[4], hashPrevBlock.begin(), hashPrevBlock.size());
        memcpy(&data[36], hashMerkleRoot.begin(), hashMerkleRoot.size());
        WriteLE32(&data[68], nTime);
        WriteLE32(&data[72], nBits);
        WriteLE32(&data[76], nNonce);

        return HashX11KVS(data, data + 80);
#else // Can take shortcut for little endian
        return HashX11KVS(BEGIN(nVersion), END(nNonce));
#endif
    }
	
    return SerializeHash(*this); // nVersion >= 4
}

std::string CBlock::ToString() const
{
    std::stringstream s;
    s << strprintf("CBlock(hash=%s, ver=%d, hashPrevBlock=%s, hashMerkleRoot=%s, nTime=%u, nBits=%08x, nNonce=%u, vtx=%u)\n",
        GetHash().ToString(),
        nVersion,
        hashPrevBlock.ToString(),
        hashMerkleRoot.ToString(),
        nTime, nBits, nNonce,
        vtx.size());
    for (unsigned int i = 0; i < vtx.size(); i++)
    {
        s << "  " << vtx[i].ToString() << "\n";
    }
    return s.str();
}

void CBlock::print() const
{
    LogPrintf("%s", ToString());
}
