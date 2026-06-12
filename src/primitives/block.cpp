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

        uint256 H_0;
        if (fFallbackMode) {
            int algo0 = 12;
            CHashWriter ssAlgo(SER_GETHASH, 0);
            ssAlgo << hashPrevBlock;
            ssAlgo << vAdamMiners[0];
            uint256 h = ssAlgo.GetHash();
            arith_uint256 tmp = UintToArith256(h);
            algo0 = (tmp - (tmp / 18) * 18).GetLow64();
            H_0 = CalculateAdamPuzzleHash(algo0, (const unsigned char*)&ssHeader[0], (const unsigned char*)&ssHeader[0] + ssHeader.size());
        } else {
            int algo1 = 0 / 17;
            int algo2 = 0 % 17;
            if (algo2 >= algo1) {
                algo2++;
            }
            uint256 hash2 = CalculateAdamPuzzleHash(algo2, (const unsigned char*)&ssHeader[0], (const unsigned char*)&ssHeader[0] + ssHeader.size());
            int i_factor = 1;
            arith_uint256 val = UintToArith256(hash2) * i_factor;
            uint256 multiplied = ArithToUint256(val);
            H_0 = CalculateAdamPuzzleHash(algo1, multiplied.begin(), multiplied.begin() + 32);
        }

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
            uint256 H_i;
            if (fFallbackMode) {
                int algo_i = 12;
                CHashWriter ssAlgo(SER_GETHASH, 0);
                ssAlgo << hashPrevBlock;
                ssAlgo << vAdamMiners[i];
                uint256 h = ssAlgo.GetHash();
                arith_uint256 tmp = UintToArith256(h);
                algo_i = (tmp - (tmp / 18) * 18).GetLow64();
                H_i = CalculateAdamPuzzleHash(algo_i, H_prev.begin(), H_prev.begin() + H_prev.size());
            } else {
                int algo1 = i / 17;
                int algo2 = i % 17;
                if (algo2 >= algo1) {
                    algo2++;
                }
                uint256 hash2 = CalculateAdamPuzzleHash(algo2, H_prev.begin(), H_prev.begin() + H_prev.size());
                int i_factor = i + 1;
                arith_uint256 val = UintToArith256(hash2) * i_factor;
                uint256 multiplied = ArithToUint256(val);
                H_i = CalculateAdamPuzzleHash(algo1, multiplied.begin(), multiplied.begin() + 32);
            }

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

uint256 CalculateAdamPuzzleHash(int algoIndex, const unsigned char* pbegin, const unsigned char* pend) {
    size_t len = pend - pbegin;
    static const unsigned char pblank[1] = {};
    const void* data = (pbegin == pend ? static_cast<const void*>(pblank) : static_cast<const void*>(pbegin));
    
    switch (algoIndex) {
        case 0: { // blake
            sph_blake512_context ctx;
            uint512 hash;
            sph_blake512_init(&ctx);
            sph_blake512(&ctx, data, len);
            sph_blake512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 1: { // bmw
            sph_bmw512_context ctx;
            uint512 hash;
            sph_bmw512_init(&ctx);
            sph_bmw512(&ctx, data, len);
            sph_bmw512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 2: { // groestl
            sph_groestl512_context ctx;
            uint512 hash;
            sph_groestl512_init(&ctx);
            sph_groestl512(&ctx, data, len);
            sph_groestl512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 3: { // jh
            sph_jh512_context ctx;
            uint512 hash;
            sph_jh512_init(&ctx);
            sph_jh512(&ctx, data, len);
            sph_jh512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 4: { // keccak
            sph_keccak512_context ctx;
            uint512 hash;
            sph_keccak512_init(&ctx);
            sph_keccak512(&ctx, data, len);
            sph_keccak512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 5: { // skein
            sph_skein512_context ctx;
            uint512 hash;
            sph_skein512_init(&ctx);
            sph_skein512(&ctx, data, len);
            sph_skein512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 6: { // luffa
            sph_luffa512_context ctx;
            uint512 hash;
            sph_luffa512_init(&ctx);
            sph_luffa512(&ctx, data, len);
            sph_luffa512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 7: { // cubehash
            sph_cubehash512_context ctx;
            uint512 hash;
            sph_cubehash512_init(&ctx);
            sph_cubehash512(&ctx, data, len);
            sph_cubehash512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 8: { // shavite
            sph_shavite512_context ctx;
            uint512 hash;
            sph_shavite512_init(&ctx);
            sph_shavite512(&ctx, data, len);
            sph_shavite512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 9: { // simd
            sph_simd512_context ctx;
            uint512 hash;
            sph_simd512_init(&ctx);
            sph_simd512(&ctx, data, len);
            sph_simd512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 10: { // echo
            sph_echo512_context ctx;
            uint512 hash;
            sph_echo512_init(&ctx);
            sph_echo512(&ctx, data, len);
            sph_echo512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 11: { // X11KVS
            return HashX11KVS(pbegin, pend);
        }
        case 12: { // DoubleSHA256
            return Hash(pbegin, pend);
        }
        case 13: { // hamsi
            sph_hamsi512_context ctx;
            uint512 hash;
            sph_hamsi512_init(&ctx);
            sph_hamsi512(&ctx, data, len);
            sph_hamsi512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 14: { // fugue
            sph_fugue512_context ctx;
            uint512 hash;
            sph_fugue512_init(&ctx);
            sph_fugue512(&ctx, data, len);
            sph_fugue512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 15: { // shabal
            sph_shabal512_context ctx;
            uint512 hash;
            sph_shabal512_init(&ctx);
            sph_shabal512(&ctx, data, len);
            sph_shabal512_close(&ctx, &hash);
            return hash.trim256();
        }
        case 16: { // whirlpool
            sph_whirlpool_context ctx;
            uint512 hash;
            sph_whirlpool_init(&ctx);
            sph_whirlpool(&ctx, data, len);
            sph_whirlpool_close(&ctx, &hash);
            return hash.trim256();
        }
        case 17: { // haval256_5
            sph_haval256_5_context ctx;
            uint256 hash;
            sph_haval256_5_init(&ctx);
            sph_haval256_5(&ctx, data, len);
            sph_haval256_5_close(&ctx, &hash);
            return hash;
        }
        default:
            return Hash(pbegin, pend);
    }
}

