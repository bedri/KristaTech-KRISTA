# Future Blocks Queue

This document describes the **Future Blocks Queue** mechanism developed for the KRISTA blockchain network to prevent block progression deadlocks caused by clock drift between nodes.

---

## 1. Background and Problem Statement

KRISTA utilizes the **Time Protocol V2** standard for block timing. Under this protocol:
- Block generation is aligned to fixed 15-second time slots.
- For a block to be accepted by other nodes, its timestamp must not be further than 14 seconds (`nTimeSlotLength - 1`) in the future compared to the validating node's local adjusted time.
- If a block or block header exceeds this threshold, it is rejected during validation with the error `time-too-new`.

### Deadlock Scenario
When natural clock drifts of more than 15 seconds occur between nodes, a node with a lagging clock will temporarily reject a valid block produced by a node with a leading clock as `time-too-new`.

According to Bitcoin/PIVX P2P protocol rules, once a block header is rejected, the sending peer does not re-broadcast it, and the receiving node does not automatically re-request it. If the node that is deterministically elected as the next block's coordinator is stuck due to this rejection, it cannot produce the next block, causing a permanent deadlock across the network.

---

## 2. Architectural Solution: Future Blocks Queue

To ensure the network can heal itself without relying on strict NTP clock synchronization or changing consensus rules, blocks that fail validation with a `time-too-new` reject reason are stored in a temporary in-memory queue.

### Components

1. **CFutureBlock Structure and mapFutureBlocks:**
   Deferred blocks or block headers are stored in `mapFutureBlocks` along with the sending peer's address:
   ```cpp
   struct CFutureBlock {
       CBlock block;
       std::string peerAddr;
   };
   std::map<uint256, CFutureBlock> mapFutureBlocks;
   ```

2. **HEADERS & BLOCK Structural Pre-Verification (`SEC-04` / `IsFutureBlockPreVerified`):**
   If a block or header fails validation with `"time-too-new"` and its timestamp is within `GetAdjustedTime() + 300` seconds (5 minutes — enforced identically on both `HEADERS` and `BLOCK` messages), the peer is not punished or disconnected. Before inserting into `mapFutureBlocks`, `ProcessMessage` requires a known parent in `mapBlockIndex` (to derive `nCandidateHeight = pindexPrev->nHeight + 1`) and calls `IsFutureBlockPreVerified(header, nCandidateHeight)` (`src/main.cpp`) to reject malformed or unauthenticated spam headers before queueing:
   - For `block.nVersion >= 11`: non-empty `vAdamCoordinatorSig` and `vAdamVRFProof`, each strictly bounded to `<= 512` bytes.
   - For `block.nVersion >= 12`: `vQuorumSig` strictly bounded to `<= 4096` bytes, and required to be non-empty whenever `IsModelDActive(nCandidateHeight)` is active.
   - For full `BLOCK` messages accepted into `mapFutureBlocks`, `pfrom->AddInventoryKnown(inv)` is recorded upon queueing (and skipped if the block fails pre-verification or capacity eviction so an evicted block can still be re-requested later). Full cryptographic BLS/ECDSA/VRF/LLMQ and contextual block validation executes when `ProcessFutureBlocks` invokes `ProcessNewBlock`.

3. **Bounded Capacity & Furthest-Timestamp Eviction (`64` Entries):**
   `mapFutureBlocks` is strictly capped at **64 entries** (`mapFutureBlocks.size() >= 64`). If the queue is full, the entry with the furthest-future timestamp (`max_element`) is evicted only if the incoming block has a closer timestamp; otherwise, the incoming block is dropped.

4. **ProcessFutureBlocks Loop:**
   During periodic message processing (`ProcessFutureBlocks`), the node checks the queue. When the local clock advances such that a deferred block's timestamp becomes valid (`block.nTime <= MaxFutureBlockTime()`), it is removed from the queue and processed (`GETDATA` for header-only entries or `ProcessNewBlock` for full blocks).

5. **Stale Entry Pruning:**
   In addition to the `+300s` admission ceiling and `64`-entry cap, parentless entries (`hashPrevBlock` missing from `mapBlockIndex`) whose block timestamp has aged more than 1 hour (`GetAdjustedTime() - block.nTime > 3600`) are automatically pruned.

---

## 3. Advantages

- **Consensus Compliance:** Block verification is still strictly bound by the same consensus time limits; no rules are modified.
- **Independence:** Prevents deadlock even if NTP services are disabled or misconfigured on some nodes.
- **Network Stability:** Avoids unnecessary DoS banning and peer disconnections.
