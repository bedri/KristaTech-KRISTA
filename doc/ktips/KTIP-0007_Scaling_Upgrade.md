```
KTIP: 0007
Title: Protocol Scaling Upgrade (8 MB Block Limit, 30s Spacing, 15s Time Slots, and BIP152 Compact Blocks)
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-06-19
```

## Abstract
This proposal specifies the **Protocol Scaling Upgrade**, which scales the transaction throughput and block propagation efficiency of the KRISTA network. The upgrade increases the maximum block size limit to 8 MB, sets the target block spacing to 30 seconds, reduces the PoS time slot length to 15 seconds, and integrates the BIP152 Compact Blocks protocol (with custom mapped inventory type `MSG_CMPCT_BLOCK = 20`) to minimize network bandwidth usage and block propagation latency.

## Motivation
As the KRISTA network matures, Layer 1 throughput must be scaled to support corporate execution, high smart contract (MESCAL) density, and asset tokenization use cases.
However, simply increasing block sizes can result in:
1. **Network Propagation Latency**: Larger block payloads propagate slowly over the P2P network, leading to high block orphan rates and fork splits.
2. **Bandwidth Waste**: Peers repeatedly download transactions that they already have in their local mempools.
3. **Database Write Stalls**: Excessively fast block times (e.g. under 10 seconds) on large blocks do not allow enough time for LevelDB disk compactions and state flushes, causing hardware-level write stalls.

To resolve these issues, we implement a balanced approach:
- Scale the block size limit to **8 MB** to support larger transaction batches.
- Set target spacing to **30 seconds** and PoS slots to **15 seconds** to give nodes ample time for validation, network routing, and SSD merges/compactions.
- Integrate **BIP152 Compact Blocks** (propagating block headers and 6-byte short transaction IDs instead of full block payloads), utilizing local mempools to reconstruct blocks instantly, saving up to 99% of block propagation bandwidth.

## Specification

### 1. Block Size & Message Limits
* **Maximum Block Size (`MAX_BLOCK_SIZE_CURRENT`)**: Increased from 2 MB (`2,000,000` bytes) to **8 MB** (`8,000,000` bytes) in `src/consensus/consensus.h`.
* **Maximum Protocol Message Length (`MAX_PROTOCOL_MESSAGE_LENGTH`)**: Increased from 2 MB to **10 MB** (`10 * 1024 * 1024` bytes) in `src/net.h` to permit transmission of full blocks (when reconstruction fails) without triggering peer size limit disconnects.
* **Default Maximum Block Creation Size (`DEFAULT_BLOCK_MAX_SIZE`)**: Increased from 750 KB to **6 MB** (`6,000,000` bytes) in `src/policy/policy.h`.

### 2. Spacing & Time Slots
* **Target Block Spacing (`nTargetSpacing`)**: Configured to **30 seconds** in `src/chainparams.cpp` for both Mainnet and Testnet.
* **Time Slot Length (`nTimeSlotLength`)**: Configured to **15 seconds** in `src/chainparams.cpp` for both Mainnet and Testnet.
* **Decay Speed Shift**: The decay interval remains at `259,200` blocks. With 30-second block spacing, the decay period remains at **~90 days** (quarterly) in calendar time, preserving the intended tokenomics structure.

### 3. BIP152 Compact Blocks Protocol Integration
To optimize bandwidth, the network implements BIP152 "Compact Blocks" (Short ID reconstruction).

#### A. Protocol Command Constants
* **Inventory Message Type**: Map `MSG_CMPCT_BLOCK` to **`20`** in `src/protocol.h` to avoid collision with legacy Dash-specific commands (such as InstantSend `MSG_TXLOCK_REQUEST = 4`).
* **Message Commands**:
  - `sendcmpct`: Negotiates compact block capability and high/low-bandwidth modes between peers.
  - `cmpctblock`: Transmits a serialized `CBlockHeaderAndShortTxIDs` containing the block header, a 32-bit nonce, differential indexes for prefilled transactions, short transaction IDs, and the PoS block signature (if PoS).
  - `getblocktxn`: Requests missing transactions by their block index.
  - `blocktxn`: Serves requested transactions.

#### B. Short Transaction ID Derivation
Short IDs are 6-byte (48-bit) hashes derived using SipHash-2-4.
The SipHash keys ($k_0, k_1$) are derived by hashing the serialized block header combined with a connection-specific 64-bit nonce:
1. Serialize the block header and connection nonce.
2. Calculate the SHA256 hash.
3. Use the first 8 bytes of the hash as $k_0$, and the next 8 bytes as $k_1$.
4. For each transaction $TX$, compute `SipHash-2-4(txhash, k0, k1)` and mask with `0x0000ffffffffffff`.

#### C. Reconstruction and Fallback
Upon receiving a `cmpctblock` message:
1. Prefilled transactions (such as Coinbase/Coinstake) are decoded directly.
2. The node matches the remaining short IDs against transaction hashes present in its local memory pool (`mempool`).
3. If all transactions are successfully matched, the block is reconstructed instantly and passed to `ProcessNewBlock`.
4. If any transactions are missing, the node stores the partially reconstructed block in `mapPartiallyDownloadedBlocks`, sends a `getblocktxn` request to the peer for the missing indexes, and reconstructs the block upon receiving `blocktxn`.

## Backward Compatibility
* Compact block propagation is negotiated dynamically via `sendcmpct`. Nodes that do not support BIP152 will continue to receive blocks via traditional `block` messages.
* The 8 MB block size limit, 30s target spacing, and 15s slot parameters are consensus-critical and require all network daemons to run updated software.

## Reference Implementation
* Consensus parameter adjustments: `src/consensus/consensus.h`, `src/net.h`, `src/policy/policy.h`, `src/chainparams.cpp`.
* Compact Block structures: `src/blockencodings.h`, `src/blockencodings.cpp`.
* P2P processing logic: `ProcessMessage` and `SendMessages` in `src/main.cpp`.
* Unit tests: `src/test/blockencodings_tests.cpp`.
