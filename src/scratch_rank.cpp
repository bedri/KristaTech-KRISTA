#define _GNU_SOURCE
#define HAVE_CONFIG_H
#include <config/kristatech-config.h>
#include <endian.h>

#include "compat/endian.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include "pubkey.h"
#include "uint256.h"
#include "hash.h"
#include "key.h"
#include "base58.h"
#include "utilstrencodings.h"
#include "chainparams.h"

int main() {
    SelectParams(CBaseChainParams::MAIN);

    std::string seedBase = "adam_miner_seed_";
    for (int i = 0; i <= 50; ++i) {
        std::string seed = seedBase + std::to_string(i);
        uint256 secret = Hash(seed.begin(), seed.end());
        
        CKey key;
        key.Set(secret.begin(), secret.end(), true);
        
        if (key.IsValid()) {
            std::string wif = EncodeSecret(key);
            CPubKey pubkey = key.GetPubKey();
            std::string address = EncodeDestination(pubkey.GetID());
            std::string keyid = pubkey.GetID().GetHex();
            std::cout << "Index: " << i << " | WIF: " << wif << " | Address: " << address << " | KeyID: " << keyid << std::endl;
        } else {
            std::cout << "Index: " << i << " | Failed to generate key" << std::endl;
        }
    }
    return 0;
}
