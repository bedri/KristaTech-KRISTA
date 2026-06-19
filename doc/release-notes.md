# Release Notes - June 2026 Technical Updates & Tokenomics Revision

This release introduces a fully revised, sustainable tokenomics model with a 210M KRISTA supply cap, flat masternode collateral, quarterly block reward decay, and specific treasury allocations. Additionally, it integrates standard BLS12-381 signatures using the `supranational/blst` library, optimizes CPU mining performance by removing debug log spam, implements the dual-path non-masternode miner registration system, details network upgrade heights, and introduces official technical whitepapers.

---

## 1. Core Tokenomics & Emission Revision

We have refactored the emission schedule in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) and [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp) to implement a mathematically modeled, finite supply curve designed for long-term scarcity and security.

### Supply Cap & Decay Parameters
- **Maximum Supply (Hard Cap):** Defined as **210,000,000 KRISTA** in [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp) via `nMaxMoneyOut`.
- **Premine Block (Height 1):** Configured to yield **0 KRISTA** (Zero Premine) to ensure fair and organic network distribution.
- **Bootstrap Phase (Heights 2 to 9,999):** Emits a temporary boost of **100 KRISTA** per block to seed early circulation and incentivize initial node operators.
- **Decay Phase (Height 10,000+):** Begins at **15 KRISTA** per block and decays by **1.9%** every **90 days (259,200 blocks)** by multiplying the base reward by `0.981` at each period boundaries.
- **Supply Asymptote:** Total circulation converges to **205,631,379 KRISTA**, providing a **4.37M buffer** under the hard cap to prevent sudden supply cutoffs.

### Masternode Collateral
- Updated to a flat, invariant **2,100 KRISTA** in `CMasternode::GetMasternodeNodeCollateral`. This establishes an optimal lock-up ratio while ensuring the network can easily scale to the 20+ active masternodes required for LLMQ quorum formation.

### Ecosystem Treasury & Faucet Splits
Coinbase transaction processing deducts funding allocations directly from block value before calculating reward distributions:
- **Developer Treasury (7%)**: Diverts 7% of block rewards to the Developer Fund Address (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`) starting at block height 2.
- **Bootstrap Faucet (0.7%)**: Diverts 0.7% of block rewards to the Bootstrap Faucet Address (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`) for blocks 2 through 50,000.

### Block Reward Allocations & Model D
The remaining portion of each block reward is divided between masternodes and stakers/miners as follows:
- **Heights <= 5000 (Early Bootstrapping):** **100% Miner-Staker / 0% Masternode** to support security during node setup (Note: this is overridden once Model D is active, where masternodes receive the 50% passive split).
- **Heights 5001 to 100,000 (Early Stage):** **80% Masternode / 20% Miner-Staker** (overridden by Model D splits when active).
- **Heights 100,001+ (Maturation):** **60% Masternode / 40% Miner-Staker** (overridden by Model D splits when active).
- **Model D Consensus**: Splits the remainder according to cooperative roles (active starting at block 2200 on Mainnet, block 500 on Testnet, block 200 on Regtest):
  - **50% Passive Masternode Queue**
  - **10% Active LLMQ Quorum Validators**
  - **15% Block Proposer** (PoW coordinator or PoS staker)
  - **25% Validator Participants**

---

## 2. Consensus & Protocol Enhancements

### Dynamic Puzzle Algorithms & Scaling (ADAM)
- **Dynamic Algorithm Mapping:** In Fallback Mode (Version 11), the puzzle hashing algorithm for each elected miner is determined dynamically per-block using the formula `Hash(Seed_H || MinerPubKey_i) % 18`. In Standard Mode (Version 12), it uses a 3-permutation selector scheme that deterministically selects 3 distinct hashing algorithms (out of 18) and compounds them (`algo3` -> `algo2` -> `algo1`) to secure the puzzle verification and block header hashing.
- **Difficulty Scaling Adjustments:** Solved a false-positive flood issue where miners submitted valid puzzles that did not meet the exact block target. The target difficulty for puzzle verification is scaled by shifting `bnTarget` by 6 bits (for blocks >= 705) instead of the previous 12 bits, stabilizing puzzle submission rate-limits.
- **Quorum Solution Threshold:** In `CreateNewBlock`, template generation is deferred if the available solutions cache fails to meet the consensus threshold (`nAdamThreshold`, initialized to `7` on Mainnet/Regtest, and `3` on Testnet) rather than requiring a 100% submission rate, allowing block production to continue even if a few elected miners are offline.
- **Version 12 Gating:** Gated the transition to block Version 12 and standard consensus rules under `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`). This prevents private or staging networks from freezing during bootstrapping before active Masternode numbers meet LLMQ quorum requirements.

### Integration of Supranational BLS (`blst`)
- Integrated the high-performance `supranational/blst` library for BLS12-381 signature generation and verification.
- Solved build issues related to static linking, assembly optimization flag compatibility, and test suite execution.

---

## 3. Non-Masternode Miner Registration System

- **Dual-Path Registration:** Implemented two distinct registration types for non-masternode nodes to participate in ADAM mining elections via `registerminer`:
  1. **Coin-Lock (`lock`):** Requires locking **1,000 KRISTA** collateral via CLTV/CSV timelocks for a minimum duration of **2,880 blocks**.
  2. **PoW-Lock (`pow`):** Solves an on-the-fly puzzle based on the best block hash challenge for a registration fee of **0.0001 KRISTA**.
- **Expiration Warning System:** Implemented automatic log warnings and GUI notifications when a miner's registration lock approaches expiration (less than **240 blocks** remaining) to prevent sudden consensus dropouts.
- **Democratic Verification & Pool Scanning:** Nodes dynamically scan the UTXO database to construct the active miner pool and verify registration finality during block validations.

---

## 4. Performance Optimizations

### CheckProofOfWork Log Spam Mitigation
- Fixed a major CPU performance bottleneck and storage issue in [src/pow.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/pow.cpp).
- Removed the debug log output `"CheckProofOfWork() : hash doesn't match nBits"` for non-matching candidate hashes. Because candidate block mining checks are executed millions of times per second, removing this log print significantly decreases CPU disk-write overhead and reduces `debug.log` size accumulation from ~50MB per minute to a standard clean baseline.

---

## 5. Technical Whitepapers

To provide a comprehensive mathematical and technical foundation for the KristaTech network, we have added official academic whitepapers to the repository:
- **English Version ([doc/Whitepaper.md](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/doc/Whitepaper.md)):** Detailed architectural paper outlining the hybrid PoW/PoS consensus layer, ADAM rolling seed selections, LLMQ quorum mechanics, empty-vector bypass proofs, MESCAL smart contract declarations, and Model D tokenomics.
- **Turkish Version ([doc/Whitepaper_TR.md](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/doc/Whitepaper_TR.md)):** A fully localized, precise translation of the technical specifications and mathematical proofs for Turkish academic and developer audiences.

---

## 6. Upgrade Heights & Stability Fixes

### Protocol Network Upgrades
The transition heights for key protocol feature gates are defined as follows across Mainnet, Testnet, and Regtest:
* **`UPGRADE_POS`**: Activates Proof-of-Stake block production (Height 200 on Mainnet/Testnet, 251 on Regtest).
* **`UPGRADE_ADAM`**: Activates the ADAM cooperative validator consensus loop (Height 200 on Mainnet/Testnet/Regtest).
* **`UPGRADE_POMBL`**: Activates Multi-Proof-Algorithm (PoMBL) block templates and verification (Height 2000 on Mainnet, 400 on Testnet, 300 on Regtest).
* **`UPGRADE_MODELD`**: Enforces Model D reward splits and PoBLS consensus (Height 2200 on Mainnet, 500 on Testnet, 200 on Regtest).

### Network Connection & Sync Logic
- **GUID-Based Connection Mitigation:** Peers now generate and exchange a unique `nLocalNodeGUID` during connection handshakes. Redundant channels from duplicate GUIDs are resolved deterministically (larger GUID wins), ensuring active peer counts represent unique physical nodes.
- **Masternode Sync Flags Reset:** Resolved an issue where running `mnsync reset` did not clear `mWeAskedForMasternodeList` and `mAskedUsForMasternodeList` flags. The flags are now fully cleared, permitting nodes to sync lists without hitting rate-limiting restrictions.
- **Protocol Inventory Mapping:** Fixed mapping commands for `MSG_MASTERNODE_ANNOUNCE` and `MSG_MASTERNODE_PING` in `CInv::GetCommand()`, restoring proper P2P propagation of masternode database updates.

### GUI & User Experience (MESCAL Integration)
- **Zero-Value Reward Classification:** Corrected transaction serialization mapping (`decomposeCoinBase`) to properly classify output indices >= 1 in coinbase transactions as "Masternode Reward" rather than "No information" when rewards are zero-value (such as heights <= 5000).
- **Zero-Balance Transaction Filtering:** Adjusted the transaction filter proxy (`transactionfilterproxy.cpp`) so that the zero-amount hide rule only filters block rewards (stakes/mined blocks) and masternode rewards. This ensures that legitimate zero-fee `SendToSelf` transfers (such as masternode collateral setups) remain visible in the transaction list.
- **GUI Integration:** Fully integrated miner registration templates and status monitors directly into the MESCAL Smart Contract GUI.

### Memory & System Safety
- **Safe Datadir Initialization:** Fixed a null-pointer dereference inside `GetDataDir()` where network parameter initialization was requested before configuration file parsing finished. This prevents empty folders named after the pointer from being created in the root repository.
- **BDB and Qt GCC 15 Fixes:** Integrated depends-layer fixes to ensure compatibility with Berkeley DB and Qt compilation under GCC 15 environment rules.

---

## 7. Smart Contract Standardness & Enforced Native BLS Keys

This update establishes native BLS key validation across all block-signing and puzzle-signing pathways, fully deprecating the legacy fallback mechanisms. In addition, standardness rules are re-enabled in the mempool for all non-Regtest networks, and a dedicated smart contract fee policy is introduced.

### Smart Contract Standardness & Relay Policy
- **Standard Smart Contract Transactions:** Added four new standard transaction output types to support smart contracts in [src/script/standard.h](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/script/standard.h) and [src/script/standard.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/script/standard.cpp):
  - `TX_CONTRACT_PUBLISH` ("contractpublish") - Matches scripts ending with `OP_PUBLISH`
  - `TX_CONTRACT_RUN` ("contractrun") - Matches scripts ending with `OP_RUN`
  - `TX_CONTRACT_STATUS` ("contractstatus") - Matches scripts ending with `OP_UPDATE_STATUS`
  - `TX_MESCAL_CONTRACT` ("mescalcontract") - Matches decompiled MESCAL contract scripts (using `CMescal::Decompile`)
- **Mempool Acceptance Restored:** Restored strict transaction and input standardness validation (`IsStandardTx` and `AreInputsStandard`) in mempool acceptance flows (`AcceptToMemoryPoolWorker` in [src/main.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/main.cpp)) to shield the network against arbitrary scripts.
- **1000x Relay Fee Multiplier for OP_RETURN:**
  - Enforced a 1000x fee multiplier inside `GetMinRelayFee` in [src/main.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/main.cpp) for any transaction containing an `OP_RETURN` output script.
  - Aligned the wallet transaction builder (`CWallet::CreateTransaction` in [src/wallet/wallet.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/wallet/wallet.cpp)) to calculate fees with the 1000x multiplier.
- **Payload Capacity Increase:** Raised the maximum relayable size for data carrier scripts (`MAX_OP_RETURN_RELAY` in [src/script/standard.h](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/script/standard.h)) to **1024 bytes** (up from 83 bytes) to support detailed smart contract publications.
- **MESCAL Code Safety:** Restrained `CMescal::Decompile` in [src/script/mescal.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/script/mescal.cpp) to explicitly reject and fail when encountering unhandled/unsupported opcodes, permitting only standard signature verification, multisig, timelock, and conditional branch opcodes.

### Native BLS Key Architecture & Enforcements
- **Legacy Fallback Deprecation:** Removed the automatic BIP32 derivation pathway and the fallback derivation from ECDSA private key bytes in `CWallet::GetBLSKey` in [src/wallet/wallet.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/wallet/wallet.cpp). Stakers and miners must configure direct native BLS keys.
- **Direct Block & Solution Signing:**
  - Refactored `SignBlockWithKey` and block signing in [src/blocksignature.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/blocksignature.cpp) to require a valid `CBLSSecretKey`.
  - Block template creation, coordinator block signing, and puzzle solutions in [src/miner.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/miner.cpp) and [src/rpc/mining.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/rpc/mining.cpp) lookup native BLS keys from the wallet mapping or the active masternode cache. If no key is found, signing and submissions fail.
- **Masternode Configuration Update:**
  - Updated `CActiveMasternodeConfig` and `CActiveMasternodeEntry` to store a third column `[bls_privkey_hex]` in [src/activemasternodeconfig.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/activemasternodeconfig.cpp).
  - Newly generated configuration templates explicitly document the new format.
  - Masternode initialization in [src/init.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/init.cpp) loads the BLS key via `-masternodeblsprivkey` command line or `activemasternode.conf`.
- **New RPC Commands:**
  - `createblsprivkey` (registered in [src/rpc/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/rpc/masternode.cpp)): Generates a cryptographically secure BLS private key in hex.
  - `setblsprivkey` (registered in [src/rpc/mining.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/rpc/mining.cpp)): Pairs and stores a native BLS key for a local wallet ECDSA address in `wallet.dat` using the new `blskey` db record.

