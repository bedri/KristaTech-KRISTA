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

2. **HEADERS & BLOCK Cryptographic Pre-Verification (`SEC-04` / `IsFutureBlockPreVerified`):**
   If a block or header fails validation with `"time-too-new"` and its timestamp is within `GetAdjustedTime() + 300` seconds (5 minutes — enforced identically on both `HEADERS` and `BLOCK` messages), the peer is not punished or disconnected, and `pfrom->AddInventoryKnown` is deferred until `ProcessFutureBlocks` so a rejected or evicted block can still be re-requested later. Before inserting into `mapFutureBlocks`, `IsFutureBlockPreVerified` enforces real cryptographic signature and candidate-pool authorization checks (`SEC-04`) to prevent queue stuffing with garbage byte payloads:
   - Known parent `pindexPrev` (`mapBlockIndex`) with `block.nTime > pindexPrev->GetMedianTimePast()`.
   - For `block.nVersion >= 11`: bounded `vAdamMiners` / `vAdamSolutions` sizes, valid self-contained BLS+ECDSA signatures (`VerifySelfContainedBLSSignedData`) on both `vAdamVRFProof` (over `GetAdamSeed(pindexPrev)`) and `vAdamCoordinatorSig` (over `block.GetHash()`), identical recovered `coordPubKey` and pinned/consistent `vrfBlsPk == coordBlsPk`, and membership of `coordPubKey` in the on-chain candidate pool (`GetAdamMinerPool(nHeight - 1, pcoinsTip)` when parent matches active tip).
   - For `block.nVersion >= 12` (when `UPGRADE_MODELD` is active): `CQuorumSignature::Verify` against `llmq::GetActiveQuorum(nHeight, pindexPrev)` when parent matches active tip (or bounded non-empty deserializable `CQuorumSignature` with `qsig.blockHash == blockHash` on side branches).

3. **Bounded Capacity & Furthest-Timestamp Eviction (`MAX_FUTURE_BLOCKS = 64`):**
   `mapFutureBlocks` is strictly capped at **64 entries** (`MAX_FUTURE_BLOCKS = 64`). If the queue is full (`mapFutureBlocks.size() >= 64`), the entry with the furthest-future timestamp (`max_element`) is evicted only if the incoming block has a closer timestamp; otherwise, the incoming block is dropped.

4. **ProcessFutureBlocks Loop:**
   During periodic message processing (`ProcessFutureBlocks`), the node checks the queue. When the local clock advances such that a deferred block's timestamp becomes valid (`block.nTime <= MaxFutureBlockTime()`), it is removed from the queue and processed (`GETDATA` for header-only entries or `ProcessNewBlock` for full blocks).

5. **Stale Entry Pruning:**
   In addition to the `+300s` admission ceiling and `MAX_FUTURE_BLOCKS = 64` cap, stale orphan entries remaining in the queue for more than 1 hour are automatically pruned.

---

## 3. Advantages

- **Consensus Compliance:** Block verification is still strictly bound by the same consensus time limits; no rules are modified.
- **Independence:** Prevents deadlock even if NTP services are disabled or misconfigured on some nodes.
- **Network Stability:** Avoids unnecessary DoS banning and peer disconnections.
