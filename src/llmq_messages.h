#ifndef KRISTA_LLMQ_MESSAGES_H
#define KRISTA_LLMQ_MESSAGES_H

#include "primitives/block.h"
#include "primitives/transaction.h"

class CBlockProposeMsg {
public:
    CBlock block;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(block);
    }
};

class CQuorumSigShareMsg {
public:
    uint256 blockHash;
    COutPoint collateralOutpoint;
    std::vector<unsigned char> vchSig;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(blockHash);
        READWRITE(collateralOutpoint);
        READWRITE(vchSig);
    }
};

#endif // KRISTA_LLMQ_MESSAGES_H
