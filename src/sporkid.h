// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2020 The PIVX developers
// Copyright (c) 2021-2022 The DECENOMY Core Developers
// Copyright (c) 2026 The KristaTech Core Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SPORKID_H
#define SPORKID_H

/*
    Don't ever reuse these IDs for other sporks
    - This would result in old clients getting confused about which spork is for what
*/

enum SporkId : int32_t {
    SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT      = 10007,
    SPORK_14_MIN_PROTOCOL_ACCEPTED              = 10013,

    SPORK_101_SERVICES_ENFORCEMENT              = 10100,
    SPORK_102_FORCE_ENABLED_MASTERNODE          = 10101,
    SPORK_103_PING_MESSAGE_SALT                 = 10102,
    SPORK_104_MAX_BLOCK_TIME                    = 10103,
    SPORK_105_MAX_BLOCK_SIZE                    = 10104,
    SPORK_106_STAKING_SKIP_MN_SYNC              = 10105,
    SPORK_107_ALLOW_DUPLICATE_MN_IPS            = 10106,

    SPORK_INVALID                               = -1
};

// Default values
struct CSporkDef
{
    CSporkDef(): sporkId(SPORK_INVALID), defaultValue(0) {}
    CSporkDef(SporkId id, int64_t val, std::string n): sporkId(id), defaultValue(val), name(n) {}
    SporkId sporkId;
    int64_t defaultValue;
    std::string name;
};

#endif
