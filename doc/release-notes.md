# Release Notes - June 2026 Technical Updates & Tokenomics Revision

This release introduces a fully revised, sustainable tokenomics model with a 210M KRISTA supply cap, flat masternode collateral, quarterly (~90 days) block reward decay, and specific treasury allocations. Additionally, it integrates standard BLS12-381 signatures using the `supranational/blst` library, optimizes CPU mining performance by removing debug log spam, implements the dual-path non-masternode miner registration system, details network upgrade heights, and introduces official technical whitepapers.

---

## 1. Core Tokenomics & Emission Revision

We have refactored the emission schedule in [src/masternode.cpp](../src/masternode.cpp) and [src/chainparams.cpp](../src/chainparams.cpp) to implement a mathematically modeled, finite supply curve designed for long-term scarcity and security.

### Supply Cap & Decay Parameters
- **Maximum Supply (Hard Cap):** Defined as **210,000,000 KRISTA** in [src/chainparams.cpp](../src/chainparams.cpp) via `nMaxMoneyOut`.
- **Premine Block (Height 1):** Configured to yield **0 KRISTA** (Zero Premine) to ensure fair and organic network distribution.
- **Graduated Bootstrap Phase (Heights 2 to 9,999):** Emits **40 KRISTA** for blocks 2–2,200, **30 KRISTA** for blocks 2,201–5,000, and **20 KRISTA** for blocks 5,001–9,999 (total `271,940 KRISTA`) to seed early Masternode collateral without creating excess liquid inflation.
- **Decay Phase (Height 10,000+):** Begins at **15 KRISTA** per block and decays by **1.9%** every **~90 days (259,200 blocks)** by multiplying the base reward by `0.981` at each period boundaries.
- **Supply Asymptote:** Total circulation converges to **204,903,519 KRISTA**, providing a **5.10M buffer** under the hard cap to prevent sudden supply cutoffs.

### Masternode Collateral
- Updated to a flat, invariant **4,200 KRISTA** in `CMasternode::GetMasternodeNodeCollateral`. This establishes an optimal lock-up ratio while ensuring the network can easily scale to the 20+ active masternodes required for LLMQ quorum formation.

### Ecosystem Treasury & Faucet Splits
Coinbase transaction processing deducts funding allocations directly from block value before calculating reward distributions:
- **Developer Treasury (7%)**: Diverts 7% of block rewards to the Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`) starting at block height 2.
- **Bootstrap Faucet (0.7%)**: Diverts 0.7% of block rewards to the Bootstrap Faucet Address (`KThn5Li5BnfGzbNJ92UDH7FL9CZR1VSRGEh`) for blocks 2 through 50,000.

### Block Reward Allocations & Model D
The remaining portion of each block reward is divided between masternodes and stakers/miners as follows:
- **Heights <= 5000 (Early Bootstrapping):** **100% Miner-Staker / 0% Masternode** to support security during node setup (Note: this is overridden once Model D is active, where masternodes receive the 35% passive split).
- **Heights 5001 to 100,000 (Early Stage):** **80% Masternode / 20% Miner-Staker** (overridden by Model D splits when active).
- **Heights 100,001+ (Maturation):** **60% Masternode / 40% Miner-Staker** (overridden by Model D splits when active).
- **Model D Consensus**: Splits the remainder according to cooperative roles (active starting at block 2200 on Mainnet, block 500 on Testnet, block 200 on Regtest):
  - **35% Passive Masternode Queue**
  - **10% Active LLMQ Quorum Validators**
  - **15% Block Proposer** (PoW coordinator or PoS staker)
  - **40% Validator Participants** (**20% across 7 Masternode Validator seats** at `~2.857%` per seat + **20% across 4 Independent Miner seats**: `8.00%` for 1 Coin-Lock seat and `4.00%` per seat across 3 Open PoW seats)

---

## 2. Consensus & Protocol Enhancements

### Dynamic Puzzle Algorithms & Scaling (ADAM)
- **Dynamic Algorithm Mapping:** In Fallback Mode (Version 11), the puzzle hashing algorithm for each elected miner is determined dynamically per-block using the formula `Hash(Seed_H || MinerPubKey_i) % 18`. In Standard Mode (Version 12), it uses a 3-permutation selector scheme that deterministically selects 3 distinct hashing algorithms (out of 18) and compounds them (`algo3` -> `algo2` -> `algo1`) to secure the puzzle verification and block header hashing.
- **Difficulty Scaling Adjustments:** Solved a false-positive flood issue where miners submitted valid puzzles that did not meet the exact block target. The target difficulty for puzzle verification is scaled by shifting `bnTarget` by 6 bits (`nAdamDifficultyShiftV2 = 6` for blocks >= 705 on Mainnet/Regtest, >= 300 on Testnet) instead of the previous 10 bits (`nAdamDifficultyShiftV1 = 10`), stabilizing puzzle submission rate-limits.
- **Quorum Solution Threshold:** In `CreateNewBlock`, template generation is deferred if the available solutions cache fails to meet the effective consensus threshold (`GetAdamThreshold` / `GetAdamEffectiveThreshold`: `5` during bootstrap `< 50,000` on Mainnet, `< 100,000` on Testnet, `< 1,000` on Regtest; transitioning to `7` on Mainnet/Regtest and `3` on Testnet, with time-relaxed recovery down to `5` [`3` during sparse bootstrap] at $t \ge 120\text{s}$) rather than requiring a 100% submission rate, allowing block production to continue even if several elected miners are offline.
- **Version 12 Gating (`Consensus::UPGRADE_POMBL`):** Gated the transition from Fallback Mode (Version 11) to Standard PoMBL Mode (Version 12) deterministically by block height via `Consensus::UPGRADE_POMBL` (height `2,000` on Mainnet, `400` on Testnet, and `300` on Regtest), rejecting `nVersion < 12` blocks with `bad-version` once active while deferring 3-of-5 LLMQ `vQuorumSig` enforcement on Mainnet/Testnet until `Consensus::UPGRADE_MODELD` (`2,200` on Mainnet, `500` on Testnet).

### Integration of Supranational BLS (`blst`)
- Integrated the high-performance `supranational/blst` library for BLS12-381 signature generation and verification.
- Solved build issues related to static linking, assembly optimization flag compatibility, and test suite execution.

---

## 3. Non-Masternode Miner Registration System

- **Dual-Path Registration:** Implemented two distinct registration types for non-masternode nodes to participate in ADAM mining elections via `registerminer`:
  1. **Coin-Lock (`lock`):** Requires locking **250 KRISTA** (`MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN`) collateral via CLTV timelocks for a minimum duration of **2,880 blocks** on Mainnet (`100 blocks` on Testnet/Regtest once Model D is active). Accepted at all block heights by consensus and manual `registerminer lock`, while background `AutoRegisterMiner` waits until height `>= 2,200` on Mainnet so bootstrap rewards remain liquid; outputs below 250 KRISTA are rejected in mempool (`bad-txns-coinlock-amount-too-small`) and ignored by `GetAdamMinerPool`.
  2. **PoW-Lock (`pow`):** Solves an on-the-fly CPU puzzle bound to an ancestor block within the last `100` blocks and locks **10 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT = 10 * COIN`, funded by inputs with $\ge 100$ confirmations) via CLTV timelock, or **0 KRISTA** (`Zero-Coin PoW`) when the explicit registered pool (`GetExplicitAdamMinerPoolCount`) has fewer than 13 validators. Any funded `PoW-Lock` output below 10 KRISTA is rejected by both mempool policy and `CheckBlock` (`bad-txns-powlock-amount-too-small`).
- **Dual-Track ($8 + 3$) Committee Quotas (`nAdamDualTrackHeight`, block 200 on Mainnet/Testnet, block 1 on Regtest):** Partitioned the 11 validator seats into **Track 1 (8 Collateralized Seats: 7 Active Masternodes [`4,200 KRISTA`] + 1 Coin-Lock [`250 KRISTA`] with immediate intra-Track-1 spillover, enforced via `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD, ...)`)** and **Track 2 (3 Open PoW Seats)** with bidirectional inter-track spillover.
- **Expiration Warning System:** Implemented automatic log warnings and GUI notifications when a miner's registration lock approaches expiration (less than **240 blocks** remaining) to prevent sudden consensus dropouts.
- **Democratic Verification & Pool Scanning:** Nodes dynamically scan the UTXO database to construct the active miner pool and verify registration finality during block validations.

---

## 4. Performance Optimizations

### CheckProofOfWork Log Spam Mitigation
- Fixed a major CPU performance bottleneck and storage issue in [src/pow.cpp](../src/pow.cpp).
- Removed the debug log output `"CheckProofOfWork() : hash doesn't match nBits"` for non-matching candidate hashes. Because candidate block mining checks are executed millions of times per second, removing this log print significantly decreases CPU disk-write overhead and reduces `debug.log` size accumulation from ~50MB per minute to a standard clean baseline.

---

## 5. Technical Whitepapers

To provide a comprehensive mathematical and technical foundation for the KristaTech network, we have added official academic whitepapers to the repository:
- **English Version ([Whitepaper.md](Whitepaper.md)):** Detailed architectural paper outlining the hybrid PoW/PoS consensus layer, ADAM rolling seed selections, LLMQ quorum mechanics, empty-vector bypass proofs, MESCAL smart contract declarations, and Model D tokenomics.
- **Turkish Version ([Whitepaper_TR.md](Whitepaper_TR.md)):** A fully localized, precise translation of the technical specifications and mathematical proofs for Turkish academic and developer audiences.

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
- **Standard Smart Contract Transactions:** Added four transaction output classifications in [src/script/standard.h](../src/script/standard.h) and [src/script/standard.cpp](../src/script/standard.cpp) *(Note: in `v1.4.5.9` `SEC-10`, the legacy stateful opcodes `OP_PUBLISH`, `OP_RUN`, `OP_UPDATE_STATUS`, `OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, and `OP_SSTORE` were permanently disabled at the interpreter layer with `SCRIPT_ERR_DISABLED_OPCODE`; stateless `TX_MESCAL_CONTRACT` and Taproot `TX_WITNESS_V1_TAPROOT` are used instead)*:
  - `TX_CONTRACT_PUBLISH` ("contractpublish") - Matches scripts ending with `OP_PUBLISH` *(disabled in `SEC-10`)*
  - `TX_CONTRACT_RUN` ("contractrun") - Matches scripts ending with `OP_RUN` *(disabled in `SEC-10`)*
  - `TX_CONTRACT_STATUS` ("contractstatus") - Matches scripts ending with `OP_UPDATE_STATUS` *(disabled in `SEC-10`)*
  - `TX_MESCAL_CONTRACT` ("mescalcontract") - Matches decompiled MESCAL contract scripts (using `CMescal::Decompile`)
- **Mempool Acceptance Restored:** Restored strict transaction and input standardness validation (`IsStandardTx` and `AreInputsStandard`) in mempool acceptance flows (`AcceptToMemoryPoolWorker` in [src/main.cpp](../src/main.cpp)) to shield the network against arbitrary scripts.
- **1000x Relay Fee Multiplier for OP_RETURN:**
  - Enforced a 1000x fee multiplier inside `GetMinRelayFee` in [src/main.cpp](../src/main.cpp) for any transaction containing an `OP_RETURN` output script.
  - Aligned the wallet transaction builder (`CWallet::CreateTransaction` in [src/wallet/wallet.cpp](../src/wallet/wallet.cpp)) to calculate fees with the 1000x multiplier.
- **Payload Capacity Increase:** Raised the maximum relayable size for data carrier scripts (`MAX_OP_RETURN_RELAY` in [src/script/standard.h](../src/script/standard.h)) to **1024 bytes** (up from 83 bytes) to support detailed smart contract publications.
- **MESCAL Code Safety:** Restrained `CMescal::Decompile` in [src/script/mescal.cpp](../src/script/mescal.cpp) to explicitly reject and fail when encountering unhandled/unsupported opcodes, permitting only standard signature verification, multisig, timelock, and conditional branch opcodes.

### Native BLS Key Architecture & Enforcements
- **Legacy Fallback Deprecation:** Removed legacy unpinned BIP32 fallback derivation in `CWallet::GetBLSKey` in [src/wallet/wallet.cpp](../src/wallet/wallet.cpp) *(Note: in `v1.4.5.9` `O3`, deterministic `DeriveBLSFromCKey` pairing is automatically persisted to `wallet.dat` during `AutoRegisterMiner`, `registerminer`, `generate`, and 1-argument `setblsprivkey`)*.
- **Direct Block & Solution Signing:**
  - Refactored `SignBlockWithKey` and block signing in [src/blocksignature.cpp](../src/blocksignature.cpp) to require a valid `CBLSSecretKey` (with self-contained BLS+ECDSA proof fallback `VerifyBLSWithECDSAFallback` where applicable).
  - Block template creation, coordinator block signing, and puzzle solutions in [src/miner.cpp](../src/miner.cpp) and [src/rpc/mining.cpp](../src/rpc/mining.cpp) lookup native BLS keys from the wallet mapping or the active masternode cache.
- **Masternode Configuration Update:**
  - Updated `CActiveMasternodeConfig` and `CActiveMasternodeEntry` to store an optional third column `[bls_privkey_hex]` in [src/activemasternodeconfig.cpp](../src/activemasternodeconfig.cpp) (derived deterministically via `DeriveBLSFromCKey` if omitted).
  - Newly generated configuration templates explicitly document the new format.
  - Masternode initialization in [src/init.cpp](../src/init.cpp) loads the BLS key via `-masternodeblsprivkey` command line or `activemasternode.conf`.
- **New RPC Commands:**
  - `createblsprivkey` (registered in [src/rpc/masternode.cpp](../src/rpc/masternode.cpp)): Generates a cryptographically secure BLS private key in hex.
  - `setblsprivkey` (registered in [src/rpc/mining.cpp](../src/rpc/mining.cpp)): Pairs and stores a native BLS key for a local wallet ECDSA address in `wallet.dat` using the new `blskey` db record.

---

## 8. Consensus Finality, Masternode Isolation & Performance Hardening (`v1.4.5.9`)

### Deterministic Equal-Work Fork Resolution
- **Lowest-Hash Tie-Breaker (`CBlockIndexWorkComparator` in [src/chain.h](../src/chain.h)):** When competing chain tips at the same height have equal cumulative chain work (`nChainWork`), nodes deterministically prefer the tip with the numerically smaller block hash (`pa->GetBlockHash() < pb->GetBlockHash()`) before checking local receipt sequence ID (`nSequenceId`), ensuring instantaneous (`< 1s`) network-wide convergence after partition recovery.

### LLMQ Anti-Equivocation & Multi-Hop Gossip Relay
- **Per-Parent Anti-Equivocation Guard (`llmq::AllowQuorumSignature` / `mapSignedProposalByPrev` in [src/llmq.cpp](../src/llmq.cpp)):** Quorum members track signed `(prevHash, coordinator, blockHash)` entries and enforce a `12s` same-coordinator cooldown and `QUORUM_EQUIVOCATION_GUARD_SECS = 30` cross-coordinator guard, preventing competing blocks on the same parent from both acquiring a 3-of-5 quorum certificate (`vQuorumSig`).
- **Multi-Hop Gossip Relay (`setSeenProposedBlocks`, `mapQuorumBlockSigs` in [src/main.cpp](../src/main.cpp)):** Block proposals (`PROPOSEBLOCK` / `qblockprop`) and quorum signature shares (`QUORUMSIGSHARE` / `qsigshare`) are deduplicated and relayed across all connected peers.
- **100% On-Chain Fallback in `ElectQuorumMembers` ([src/llmq.cpp](../src/llmq.cpp)):** When on-chain active Masternode candidates (with unspent 4,200 KRISTA collateral and on-chain heartbeat pings within the last 2,880 blocks) are fewer than `nQuorumSize` (5), `ElectQuorumMembers` deterministically falls back to the on-chain `GetAdamMinerPool(nHeight - 1, pView)` pool without relying on in-memory `mnodeman`.

### Masternode Hot/Cold Isolation & Authenticated On-Chain Pings
- **Strict Hot/Cold Signing Isolation ([src/miner.cpp](../src/miner.cpp), [src/llmq.cpp](../src/llmq.cpp), [src/adam.cpp](../src/adam.cpp)):** Cold controller wallets (`masternode.conf`) only broadcast `CMasternodeBroadcast` activations and never sign LLMQ quorum shares, block proposals, or coordinator messages; only active Hot Nodes (`masternode=1` + `activemasternode.conf`) sign consensus messages.
- **Compressed & Uncompressed PubKey Support in `ExtractOnChainMasternodePing` ([src/adam.cpp](../src/adam.cpp)):** Supports both 33-byte compressed (`36-byte` `<pubkey> OP_CHECKSIGVERIFY OP_1` script) and 65-byte uncompressed (`68-byte` script generated by `createmasternodekey`) Masternode public keys alongside the `CDataStream` `OP_RETURN` proof (`collateralOutpoint` + `vchPingSig` + optional `COnChainMasternodeDelegation`).
- **On-Chain Ping Fee Exemption (`0.0001 KRISTA` in [src/main.cpp](../src/main.cpp) & [src/masternode.cpp](../src/masternode.cpp)):** Authenticated on-chain Masternode ping transactions (`0.01 KRISTA` self-spendable UTXO broadcast after $\ge 1,440$ idle blocks) are exempt from the `1000x` `OP_RETURN` relay fee multiplier in `GetMinRelayFee`, paying a flat `0.0001 KRISTA` fee while regular block participation renews status at zero fee.

### Lock-Free Chain Tip Access & Peer Discovery Infrastructure
- **Lock-Free `CChain::AtomicTip()` ([src/chain.h](../src/chain.h), [src/chain.cpp](../src/chain.cpp), [src/main.cpp](../src/main.cpp)):** Added an atomic tip pointer (`std::atomic<CBlockIndex*> pAtomicTip`) updated in `CChain::SetTip()`, enabling `GetChainTip()` to read the active tip without acquiring `cs_main`.
- **Fixed Seeds & Live DNS Seeder ([src/chainparamsseeds.h](../src/chainparamsseeds.h)):** Populated `pnSeed6_main` and `pnSeed6_test` with the 4 seed node IPv4-mapped IPv6 addresses and deployed the live DNS Seeder at `seeder.kristalteknoloji.com`.

### Standby Validator Handover, Time-Relaxed Quorum & Incentive Alignment (`Rev 30–35`)
- **Dual-Track ($7 + 1 + 3$) Masternode Quorum Guarantee ([src/adam.h](../src/adam.h), [src/adam.cpp](../src/adam.cpp), [src/chainparams.cpp](../src/chainparams.cpp)):** Set `ADAM_MASTERNODE_TRACK_QUOTA = 7` and `ADAM_COIN_LOCK_TRACK_QUOTA = 1` within `ADAM_COLLATERALIZED_TRACK_QUOTA = 8` alongside `ADAM_OPEN_POW_TRACK_QUOTA = 3`, enforced via compile-time `static_assert` and runtime `assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)` so Active Masternodes alone always meet the post-bootstrap quorum threshold (`7`).
- **Standby Validator Seat Handover ($t \ge 60\text{s}$) & Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$) ([src/adam.cpp](../src/adam.cpp), [src/main.cpp](../src/main.cpp), [src/miner.cpp](../src/miner.cpp)):** At $t \ge 60\text{s}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS`), silent primary validator seats may be replaced strictly by same-track standby validators (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) in sequential rank order verified against the block's `CCoinsViewCache* pView` (`ResolveAdamCommitteeForTime`, `ValidateAdamCommitteeForTime`). At $t \ge 120\text{s}$ (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS`), `GetAdamEffectiveThreshold` relaxes the quorum threshold by `1` per `60s` slot down to `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` non-MN seats, ensuring at least 1 Active Masternode is always required when post-bootstrap or active MNs $\ge 7$, or `3` during bootstrap when active MNs $< 7$).
- **Equal Work, Equal Reward (`ComputeModelDValidatorSeatPayments`), 50% Automatic Protocol Burn & LLMQ Anti-Censorship Veto ([src/masternode-payments.cpp](../src/masternode-payments.cpp), [src/main.cpp](../src/main.cpp)):** Every solved committee seat (primary or legitimate same-track standby at $t \ge 60\text{s}$) receives its full track-specific share of the 40% Participant Validator pool (`20%` across the 4 Independent Miner quota seats (`8.00%` for 1 Coin-Lock + `4.00%` per seat for 3 Open PoW), and `20%` across the 7 Masternode Validator quota seats at `~2.857%` per seat), unsolved seats receive `0%`, and the Coordinator retains its full 15% reward when rescuing a stalled block with standby validators, with unclaimed shares (`nUnclaimedPartSplit`) split 50% to the Automatic Protocol Burn (permanently reducing `nMoneySupply` as pure supply deflation without crediting any personal PoB staking weight) and 50% to the Developer Treasury. In `qblockprop` (when the node's tip matches `msg.block.hashPrevBlock` and $t \ge 60\text{s}$), LLMQ members veto proposals that substitute a primary seat (`vAdamMiners[i] != vExpectedMiners[i]`) whose valid solution is already cached locally.
- **Version-Independent `ActiveProposalGuard` with Non-Destructive Eviction ([src/miner.cpp](../src/miner.cpp)):** Enforces a node-wide single-block-per-parent guard across all block versions (`v11` and `v12`) with non-destructive FIFO/age pruning (`PruneParentCoordMap`), preventing any node from producing both a PoW block and a PoS block for the same `(parentHash, coordinatorKey)`.
- **Qt Dashboard Miner Hub, Seat-Targeted `getblocktemplate` & Zero-Coin PoW Bootstrap Fixes (`Rev 38–39`):** Added the integrated **Cooperative CPU Miner & Validator Hub** in [src/qt/kristatech/dashboardwidget.cpp](../src/qt/kristatech/dashboardwidget.cpp) (1-click CPU mining toggle, async background 1-click `PoW-Lock` / `Coin-Lock` registration, bounded pre-Model-D header scan, live `UPGRADE_POMBL`-aligned committee role badges, real wallet payout accounting, and `<RPC_PASSWORD>`-masked operator `cpuminer` command copy); added optional 1-argument `registerminer` defaults and `getblocktemplate '{"mineraddress":"..."}'` seat targeting in [src/rpc/mining.cpp](../src/rpc/mining.cpp); and resolved Genesis Block-0 IBD deadlock ([src/main.cpp](../src/main.cpp)), sentinel `0x01:0` wallet conflict tracking ([src/wallet/wallet.cpp](../src/wallet/wallet.cpp)), and `GetExplicitAdamMinerPoolCount` `< 13` 24-bit Zero-Coin PoW bootstrap eligibility ([src/adam.cpp](../src/adam.cpp)).
- **Decentralized PoS Co-Signing (`POSCOORDREQ`/`POSCOORDSIG`), Alternating PoW/PoS Slot Priority, Cryptographically Pre-Verified Future Queue & Single-Dispatcher Solver (`Rev 40–43`):** Fixed Qt shutdown `powRegUnlockGuard` / `pwalletMain` destruction race (`DashboardWidget::shutdownMinerWorkers()`); enforced cryptographic `IsFutureBlockPreVerified(header, pindexPrev)` (`VerifySelfContainedBLSSignedData` + `GetAdamMinerPool` ownership on `v11+`, 3-of-5 `CQuorumSignature::Verify` against `llmq::GetActiveQuorum` when `IsModelDActive` on `v12+`, `+300s` drift ceiling, `64`-entry cap, full-block preservation over `HEADERS`, and deferred `AddInventoryKnown`) on `mapFutureBlocks` ([src/main.cpp](../src/main.cpp)); enforced the `45s` primary Coordinator window across Pre-Model-D V12 heights (`2,000..2,199`) ([src/main.cpp](../src/main.cpp), [src/miner.cpp](../src/miner.cpp)); implemented the `1,500ms` alternating PoW/PoS slot-opening priority window (`ADAM_SLOT_PRIORITY_WINDOW_MS = 1500` in [src/miner.cpp](../src/miner.cpp)); exempted Zero-Coin PoW `0x01:0` transactions from `CTxMemPool::check` UTXO assertion ([src/txmempool.cpp](../src/txmempool.cpp)); implemented decentralized P2P `POSCOORDREQ` / `POSCOORDSIG` Coordinator co-signing with full reconstructed-header `VerifyAdamCoordinatorSig` verification via `mapSeenPoSCoordReqs` ([src/protocol.h](../src/protocol.h), [src/adam.cpp](../src/adam.cpp), [src/main.cpp](../src/main.cpp), [src/miner.cpp](../src/miner.cpp)); eliminated the `CreateNewBlock` / `TestBlockValidity` / `PROPOSEBLOCK` `pindexPrev`-vs-`pcoinsTip` tip-advance `ConnectBlock` assertion race and unlocked `pcoinsTip` / `mapMasternodeCollateralOutpoints` reads ([src/miner.cpp](../src/miner.cpp), [src/main.cpp](../src/main.cpp)); replaced duplicate multi-thread `BitcoinMiner` puzzle loops with a strided single-dispatcher worker pool, unified deterministic `DeriveBLSFromCKey` and shared `CMinerRegistrationSlotGuard` across `AutoRegisterMiner` / `registerminer` / `generate` / 1-arg `setblsprivkey`, hardened `TryAssembleCoordinatorBlockAsync` lifecycle joins across `GenerateBitcoins` and post-`g_connman.reset()` shutdown, and added V12 3-permutation compound puzzle support to `ccminer`.

