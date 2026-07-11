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

2. **HEADERS Message Handler Modification:**
   If a header fails validation because of `"time-too-new"`, the peer is not punished or disconnected; instead, the header is added to the queue.

3. **BLOCK Message Handler Modification:**
   If a full block fails validation because of `"time-too-new"`, the block data is added to the queue.

4. **ProcessFutureBlocks Loop:**
   During every execution of the message processing loop (`ProcessMessages`), the node checks the queue. When the local clock advances such that a deferred block's timestamp becomes valid (`block.nTime <= MaxFutureBlockTime()`), it is removed from the queue and processed.
   - If it was a header, once validated, the node requests the full block data (`GETDATA`) from the peer.
   - If it was a full block, it is added directly to the chain via `ProcessNewBlock`.

5. **Memory Leak Prevention:**
   To prevent memory leaks from orphan chains or invalid blocks, any block that remains in the queue for more than 1 hour is automatically pruned.

---

## 3. Advantages

- **Consensus Compliance:** Block verification is still strictly bound by the same consensus time limits; no rules are modified.
- **Independence:** Prevents deadlock even if NTP services are disabled or misconfigured on some nodes.
- **Network Stability:** Avoids unnecessary DoS banning and peer disconnections.
