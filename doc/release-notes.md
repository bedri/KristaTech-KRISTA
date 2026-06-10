# Release Notes - June 2026 Technical Updates

This release introduces critical enhancements to the ADAM consensus system, integrates standard BLS12-381 signatures using the `supranational/blst` library, optimizes the non-masternode miner registration system, and fixes several network sync, GUI, and memory safety issues.

---

## 1. Consensus & Protocol Enhancements

### Dynamic Puzzle Algorithms & Scaling (ADAM)
- **Dynamic Algorithm Mapping:** In Fallback Mode (Version 11), the puzzle hashing algorithm for each elected miner is determined dynamically per-block using an index mapping formula: `Hash(hashPrevBlock || MinerPubKey_i) % 18`. In Standard Mode (Version 12), the index simplifies to `minerIndex % 13` to balance hash algorithm usage across the active validator set.
- **Difficulty Scaling Adjustments:** Solved a false-positive flood issue where `cpuminer-opt` submitted valid puzzles that did not meet the exact block target. The target difficulty for puzzle verification is scaled by shifting `bnTarget` by 6 bits (for blocks >= 705) instead of the previous 12 bits, stabilizing puzzle submission rate-limits.
- **Quorum Solution Threshold:** In `CreateNewBlock`, template generation is deferred if the available solutions cache fails to meet the consensus threshold (`nAdamThreshold`, initialized to `7` on Mainnet) rather than requiring a 100% submission rate, allowing block production to continue even if a few elected miners are offline.
- **Version 12 Gating:** Gated the transition to block Version 12 and standard consensus rules under `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`). This prevents private or staging networks from freezing during bootstrapping before active Masternode numbers meet LLMQ quorum requirements.

### Integration of Supranational BLS (`blst`)
- Integrated the high-performance `supranational/blst` library for BLS12-381 signature generation and verification.
- Solved build issues related to static linking, assembly optimization flag compatibility, and test suite execution.

---

## 2. Non-Masternode Miner Registration System

- **Dual-Path Registration:** Implemented two distinct registration types for non-masternode nodes to participate in ADAM mining elections via `registerminer`:
  1. **Coin-Lock (`lock`):** Requires locking **1,000 KRISTA** collateral via CLTV/CSV timelocks for a minimum duration of **2,880 blocks**.
  2. **PoW-Lock (`pow`):** Solves an on-the-fly puzzle based on the best block hash challenge for a registration fee of **0.0001 KRISTA**.
- **Expiration Warning System:** Implemented automatic log warnings and GUI notifications when a miner's registration lock approaches expiration (less than **240 blocks** remaining) to prevent sudden consensus dropouts.
- **Democratic Verification & Pool Scanning:** Nodes dynamically scan the UTXO database to construct the active miner pool and verify registration finality during block validations.

---

## 3. Stability & Bug Fixes

### Network Connection & Sync Logic
- **GUID-Based Connection Mitigation:** Peers now generate and exchange a unique `nLocalNodeGUID` during connection handshakes. Redundant channels from duplicate GUIDs are resolved deterministically (larger GUID wins), ensuring active peer counts represent unique physical nodes.
- **Masternode Sync Flags Reset:** Resolved an issue where running `mnsync reset` did not clear `mWeAskedForMasternodeList` and `mAskedUsForMasternodeList` flags. The flags are now fully cleared, permitting nodes to sync lists without hitting rate-limiting restrictions.
- **Protocol Inventory Mapping:** Fixed mapping commands for `MSG_MASTERNODE_ANNOUNCE` and `MSG_MASTERNODE_PING` in `CInv::GetCommand()`, restoring proper P2P propagation of masternode database updates.

### GUI & User Experience (MESCAL Integration)
- **Zero-Value Reward Classification:** Corrected transaction serialization mapping (`decomposeCoinBase`) to properly classify output indices >= 1 in coinbase transactions as "Masternode Reward" rather than "No information" when rewards are zero-value (such as heights <= 5000).
- **GUI Integration:** Fully integrated miner registration templates and status monitors directly into the MESCAL Smart Contract GUI.

### Memory & System Safety
- **Safe Datadir Initialization:** Fixed a null-pointer dereference inside `GetDataDir()` where network parameter initialization was requested before configuration file parsing finished. This prevents empty folders named after the pointer from being created in the root repository.
- **BDB and Qt GCC 15 Fixes:** Integrated depends-layer fixes to ensure compatibility with Berkeley DB and Qt compilation under GCC 15 environment rules.
