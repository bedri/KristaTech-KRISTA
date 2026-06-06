// Copyright (c) 2026 The KristaTech developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef KRISTA_LLMQ_H
#define KRISTA_LLMQ_H

#include "main.h"
#include "masternode.h"
#include "uint256.h"
#include "key.h"
#include "serialize.h"

#include <vector>
#include <map>

namespace llmq {

class CQuorumMember {
public:
    COutPoint collateralOutpoint;
    CPubKey pubKeyMasternode;

    CQuorumMember() = default;
    CQuorumMember(const COutPoint& out, const CPubKey& pub) : collateralOutpoint(out), pubKeyMasternode(pub) {}

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(collateralOutpoint);
        READWRITE(pubKeyMasternode);
    }
};

class CQuorum {
public:
    int nHeight{0};
    uint256 quorumHash;
    std::vector<CQuorumMember> members;
    CPubKey quorumPubKey;

    CQuorum() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(nHeight);
        READWRITE(quorumHash);
        READWRITE(members);
        READWRITE(quorumPubKey);
    }
};

class CQuorumSignature {
public:
    uint256 blockHash;
    std::vector<std::pair<COutPoint, std::vector<unsigned char>>> signatures; // member outpoint -> signature bytes

    CQuorumSignature() = default;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(signatures);
    }

    bool Verify(const CQuorum& quorum) const;
};

// Select M members deterministically based on seed
std::vector<CQuorumMember> ElectQuorumMembers(int nHeight, int nQuorumSize);

// DKG session simulation: create a new quorum at height
CQuorum RunDKG(int nHeight);

// Get the active quorum for a given block height
CQuorum GetActiveQuorum(int nHeight);

// Retrieve private key for a masternode (local or deterministic regtest)
bool GetMasternodePrivKey(const CPubKey& pubKey, CKey& key);

} // namespace llmq

#endif // KRISTA_LLMQ_H
