# ADAM & MPA Consensus Security Audit Report

This report evaluates the security posture, logic, and potential runtime vulnerabilities of the implemented **ADAM (A Decentralized Approach Model)** and **MPA (Multi-Proof Algorithm)** cooperative consensus mechanisms inside the KRISTA codebase, incorporating the newly integrated multi-algorithm puzzle mining and fallback quorum scaling.

---

## 1. Threat Modeling: Security Against Common Attack Vectors

This section analyzes how the hybrid ADAM/MPA consensus model mitigates the most common and critical attack vectors in blockchain networks.

### 1.1. %51 Hashpower Attack / Cartel Monopolization
* **Traditional Vulnerability**: In standard PoW, an actor controlling 51% of the network's hashpower can double-spend, reorganize blocks, and censor transactions.
* **ADAM/MPA Mitigation**: 
  - Hashpower alone is insufficient. To participate in block production, a node must be elected as one of the 11 Dual-Track committee validators (`7 Active Masternodes + 1 Coin-Lock + 3 Open PoW`) or the PoBLS Coordinator.
  - The primary election pool is anchored by the Masternode network (`4,200 KRISTA` collateral, holding 7 of 11 seats and Coordinator priority) alongside `250 KRISTA` Coin-Lock and rate-limited `10 KRISTA` PoW-Lock validators.
  - To unilaterally compromise a post-bootstrap block, a cartel must control at least 7 of the 11 elected seats (`nAdamThreshold = 7`) plus the Coordinator and active BFT LLMQ quorum majority (scaling dynamically with active Masternodes: `3-of-5` `<25`, `5-of-9` `25–49`, `6-of-11` `50–99`, `8-of-15` `>=100`) — a massive financial barrier that makes the attack economically irrational.

### 1.2. Selfish Mining & Block Withholding
* **Traditional Vulnerability**: A miner hides mined blocks and selectively releases them to orphan honest miners' blocks, gaining an unfair share of block rewards.
* **ADAM/MPA Mitigation**: 
  - Block production is collaborative. An elected miner only solves a lightweight puzzle (`vAdamSolutions`) and sends it to the coordinator.
  - A miner cannot "selfish mine" a private fork because they cannot generate the Coordinator's signature (`vAdamCoordinatorSig`) or the signatures of the other 10 miners.
  - If a miner holds their solution, the coordinator can still publish the block as long as at least 7 other elected miners submit their solutions. Selfish mining is completely neutralized.

### 1.3. Block Grinding / Seed Manipulation
* **Traditional Vulnerability**: In PoS chains, validators alter block content (nonces, transactions) to manipulate the next block's hash, attempting to bias the pseudo-random seed to elect themselves in future slots.
* **ADAM/MPA Mitigation**: 
  - The rolling seed for the next election slot is derived from the Coordinator's deterministic VRF signature of the previous seed:

    $$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

  - For registered **KTDIP3 (`UPGRADE_DIP3 = 1` Mainnet & Testnet)** Masternodes, operators authenticate deterministically using their on-chain registered BLS12-381 operator key (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature` with `blsPubKeyOperator` in `evodb`, empty `ecdsaSig`), while pre-DIP3/non-MN validators use hybrid BLS12-381 + ECDSA fallback (`SignBLSWithECDSAFallback`) where the BLS public key is pinned via ECDSA. Both paths guarantee unique deterministic signatures per seed, making the next block's election seed 100% tamper-proof.

### 1.4. Nothing-at-Stake Attack
* **Traditional Vulnerability**: In PoS, validators can sign block headers on multiple competing forks simultaneously at zero cost, preventing fork resolution.
* **ADAM/MPA Mitigation**: 
  - To validate a block on any fork, the block must contain at least 7 valid PoW puzzle solutions matching the elected miners' keys for that fork's seed.
  - Solving these puzzles requires executing real C++ hashing loops (using a 3-stage permutation of the 18 supported algorithms). 
  - Because miners must spend actual physical processing power (CPU/GPU cycles) to solve the puzzles for each competing fork, the cost of staking on multiple forks is non-zero. Staking on multiple forks is computationally expensive, resolving the nothing-at-stake vulnerability.

### 1.5. Sybil Attacks on Node Election & Dual-Track Quota Isolation
* **Traditional Vulnerability**: An attacker spins up thousands of virtual nodes to dominate the peer list and win all leader elections.
* **ADAM/MPA Mitigation**: 
  - Under the **Dual-Track Quota Election (`ElectDualTrackValidators`, active at `nAdamDualTrackHeight = UPGRADE_ADAM`)**, the 11 validator seats are partitioned into **Track 1 (Collateralized Pool — 8 Seats: 7 Active Masternodes [`4,200 KRISTA`] + 1 strictly isolated Coin-Lock [`250 KRISTA`])** and **Track 2 (Open PoW Pool — 3 Seats)**. Because `ADAM_MASTERNODE_TRACK_QUOTA = 7` is enforced via compile-time `static_assert` and runtime `assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)` in `chainparams.cpp`, Active Masternodes alone always meet the post-bootstrap quorum threshold (`7`) even if all 4 non-Masternode seats withhold solutions; and if 5+ seats are silent simultaneously, **Standby Validator Seat Handover (`60s`)** and **Time-Relaxed Quorum Threshold (`120s`)** guarantee block recovery.
  - Even if an adversary registers hundreds of Open PoW keys, their representation is strictly capped at **3 out of 11 seats (27.3%)** whenever the Collateralized Track is populated — well below the consensus threshold (`GetAdamThreshold`: `5` during bootstrap, `7` post-bootstrap).
  - Furthermore, funded `PoW-Lock` registrations require locking at least **10 KRISTA (`POW_LOCK_REGISTRATION_AMOUNT = 10 * COIN`)** funded by UTXOs with $\ge 100$ confirmations (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`) and are rate-limited to at most 1 funded `PoW-Lock` transaction per block (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`). `Coin-Lock` registrations require locking at least **250 KRISTA** funded by UTXOs with $\ge 100$ confirmations (`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100`) and are rate-limited to at most 1 `Coin-Lock` transaction per block (`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1`). Both tracks enforce at most 1 active seat per wallet, eliminating identity multiplication spam. Any `PoW-Lock` output below 10 KRISTA is rejected by both mempool policy and `CheckBlock` (`bad-txns-powlock-amount-too-small`), while any `Coin-Lock` output below 250 KRISTA is rejected by mempool policy (`bad-txns-coinlock-amount-too-small`) and ignored by `GetAdamMinerPool`.

### 1.6. Signature Malleability & Relay DoS
* **Traditional Vulnerability**: An attacker mutates ECDSA signature bytes in relayed blocks to alter the block payload without changing the validity, causing cache pollution and P2P ban triggers.
* **ADAM/MPA Mitigation**: 
  - The coordinator signature is excluded from the block hash (`GetHash()`), so mutating it does not affect the block's hash identity.
  - We enforce strict DER formatting checks. Any block with a mutated signature is rejected immediately at the P2P layer before entering the block index.

---

## 2. Technical Findings & Code Safety

### 2.1. Multi-Algorithm Array Bound Safety
The puzzle hashing algorithms are selected via:

$$\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$$

which deterministically maps the input to 3 distinct algorithm indices in the `kUniformAlgos` pool of `17` single-pass hashing algorithms (`UPGRADE_FAIRNESS` from Block `1`, excluding asymmetric `X11KVS` `algo 11`, yielding `4,080` V12 permutations; legacy pre-Fairness used `18`).

* **Safety Check**: The `CalculateAdamPuzzleHash()` switch statement handles cases `0` to `17`, representing all hashing primitives, while `GetAdamPuzzleAlgo()` and `GetAdam3PermutationAlgos()` map exclusively onto the uniform `17`-algorithm table under `UPGRADE_FAIRNESS` (`VerifyAdamSolution` also requires 33-byte compressed validator pubkeys). The default branch falls back to Double-SHA256, protecting against any potential index out-of-bounds or undefined behaviors.

### 2.2. Masternode Fallback and Local Key Pools
* **Safety Check**: Deterministic `-adamminerseed` fallback keys (`GetAdamDeterministicKey`) are strictly confined to **Regtest (`fMineBlocksOnDemand`)** for automated unit and functional tests and are **never active on Mainnet or Testnet**. On Mainnet and Testnet, if the explicit on-chain pool (`GetAdamMinerPool`) has fewer than `11` validators prior to `nAdamBootstrapLimit` (`2,500` on Mainnet under `UPGRADE_FAIRNESS`, legacy `44,850`; `100,000` on Testnet), candidate slots are padded deterministically using real Block `1..199` coinbase public keys (`GetBootstrapCoinbaseFallbackKeys`), with zero hardcoded private keys or local wallet filtering.

### 2.3. CPU Denial of Service (DoS on Verification)
* **DoS Risk**: Relaying nodes must verify 11 partial puzzle signatures and one coordinator signature per block, which is CPU-intensive.
* **Mitigation**: Relaying nodes verify the block's difficulty target, transaction structure, and elected miner list *before* performing expensive signature verifications, rejecting invalid spam blocks early.

### 2.4. Equal-Work Fork Convergence & LLMQ Anti-Equivocation (v1.4.5.9)
* **Equal-Work Fork Resolution (`CBlockIndexWorkComparator`)**: When competing chain tips at the same height have identical `nChainWork`, nodes deterministically prefer the tip with the numerically smaller block hash (`pa->GetBlockHash() < pb->GetBlockHash()`), preventing persistent equal-work split-brain states across network partitions.
* **LLMQ Parent-Bound Anti-Equivocation (`llmq::AllowQuorumSignature`)**: Quorum members track signed proposals per parent block (`mapSignedProposalByPrev[prevHash]`), enforcing a 12-second cooldown for same-coordinator re-proposals and `QUORUM_EQUIVOCATION_GUARD_SECS = 30` seconds before signing a fallback coordinator's competing proposal on the same parent.
* **Strict Hot/Cold Masternode Signing Isolation**: Cold controller wallets (`masternode.conf`) are strictly isolated from consensus signing; only active Hot Nodes (`masternode=1` + `activemasternode.conf`) sign LLMQ shares, block proposals, and on-chain heartbeat pings.

---

## 3. Comprehensive 3-Layer Security Audit & Hardening (`v1.4.5.9` — `SEC-01` to `SEC-13`)

1. **`SEC-01` (Server File Permissions & Multi-Tenant Isolation)**: Enforced `0700` on `~/.kristatech` / `~/.kristatech-explorer` and `0600` on `kristatech.conf`, `activemasternode.conf`, `masternode.conf`, `wallet.dat`, and `.env` across all 5 nodes (`Seed 1..4` + Local).
2. **`SEC-02` (`MNBROADCAST` / `MNPING` Cache Poisoning & OOM DoS Prevention)**: Deferred insertion into `mapSeenMasternodeBroadcast` and `mapSeenMasternodePing` in `src/masternodeman.cpp` and `src/masternode.cpp` until *after* cryptographic signature (`CheckSignature`) and UTXO collateral checks succeed.
3. **`SEC-03` (`ADAMSOL` & `QUORUMSIGSHARE` Payload Size Bounds)**: Enforced strict upper bounds (`!empty() && size() <= 512`, `Misbehaving(20)`) on `ADAMSOL` (`msg.vchSolution`) before orphan caching (`mapOrphanAdamSolutions`) and on `QUORUMSIGSHARE` (`msg.vchSig`) before deserialization in `src/main.cpp`.
4. **`SEC-04` (`mapFutureBlocks` Cryptographic Pre-Verification & Queue Stuffing Protection)**: Enforced `IsFutureBlockPreVerified(header, pindexPrev)` requiring known `pindexPrev` (`header.GetBlockTime() > pindexPrev->MinPastBlockTime()`), cryptographic `VerifySelfContainedBLSSignedData` (`vrfBlsPk == coordBlsPk`) / DIP3 operator BLS (`VerifyAdamOrDip3Signature`) and `GetAdamMinerPool` ownership (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`) for `v11+`, active BFT threshold `CQuorumSignature::Verify` against `llmq::GetActiveQuorum` (`3-of-5` up to `8-of-15`) when `IsModelDActive` for `v12+`, unified `+300s` ceiling and `64`-entry cap across `HEADERS` and `BLOCK`, preservation of queued full `CBlock` entries against `HEADERS` overwrites, and deferred `AddInventoryKnown` until `ProcessFutureBlocks`.
5. **`SEC-05` (`CQuorumSignature::Verify` Vector & Element Bounds)**: Enforced `signatures.size() <= 32` and `sig.size() <= 512` in `CQuorumSignature::Verify` and `CQuorumSignature::AggregateBLSSignatures` (`src/llmq.cpp`), and tightened `CheckBlockHeader` (`block.vQuorumSig.size() <= 4096`) in `src/main.cpp`.
6. **`SEC-06` (Web Service Localhost Binding)**: Restricted `krista-explorer`, `krista-website`, and `kristalteknoloji.com` Nitro servers to `--host 127.0.0.1` behind Nginx TLS termination.
7. **`SEC-07` (Explorer Address Search Regex)**: Updated `Explorer/server/api/search.ts` regex from `{34}` to `/^[a-zA-Z0-9]{33,36}$/` to support 35-character KRISTA addresses (`KT...` / `kt...`).
8. **`SEC-08` (Treasury Portal Proposal Validation)**: Aligned `KristaTech-Treasury-Portal` proposal validation (`rawTxHex <= 200000` hex chars, max 50 pending proposals, and dynamic LLMQ BFT majority threshold matching `src/llmq.cpp`).
9. **`SEC-09` (Multi-Input Developer Fund `txTmp.GetHash()` Normalization)**: Normalized all inputs spending from `DeveloperFundAddress()` in `CheckInputs` and `ConnectBlock` (`src/main.cpp`) by stripping the 3rd push (`qsig`) across all matching inputs before computing `txTmp.GetHash()`.
10. **`SEC-10` (Disabling Legacy DECENOMY/SWMP Stateful Opcodes & `CScriptContract` Helpers)**: Disabled legacy `OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN`, and `OP_UPDATE_STATUS` opcodes in `EvalScript` (`src/script/interpreter.cpp`) with `SCRIPT_ERR_DISABLED_OPCODE`, and hard-disabled `CScriptContract::RunContractScript`, `SaveContract`, `LoadContract`, and `UpdateContractStatus` (`src/script/script.cpp`) so they always return `false` without accessing `pScriptDB`.
11. **`SEC-11` (BIP-350 Standard Bech32m Checksum Constant)**: Updated `Encoding::BECH32M` checksum constant in `src/bech32.cpp` and `KristaTech-MESCAL-Web/server/utils/mescal.ts` from `0x3fffffff` to the standard BIP-350 constant `0x2bc830a3`.
12. **`SEC-12` (100% On-Chain Deterministic Consensus Elections & Sync-Independent Validation)**: Removed in-memory P2P `mnodeman` and `masternodeSync.IsSynced()` consensus dependencies across `IsValidMasternodeCollateralAndAuth`, `RegisterBlockMasternodeActivity`, `InitializeOnChainMasternodePings`, `GetAdamMinerPool`, `SelectAdamNodes` (`src/adam.cpp`), `ElectQuorumMembers` (`src/llmq.cpp`), `IsBlockPayeeValid` / `FillBlockPayee` (`src/masternode-payments.cpp`), and `CheckBlock` (`src/main.cpp`), ensuring 100% deterministic on-chain consensus state across all nodes.
13. **`SEC-13` (Core Wallet `TX_WITNESS_V1_TAPROOT` Tracking & Coin-Selection Isolation)**: Added `TX_WITNESS_V1_TAPROOT` (`OP_1 <32-byte XOnlyPubKey>`) and general `TX_MESCAL_CONTRACT` (`keystore.HaveCScript`) evaluation to `IsMine` (`src/script/ismine.cpp`) returning `ISMINE_WATCH_ONLY` so `wallet.dat` tracks Taproot/MESCAL contract UTXOs without polluting automatic `AvailableCoins` / `SelectCoinsMinConf` selection (`ISMINE_SPENDABLE`).

---

## 4. Consensus Liveness, Standby Handover & Incentive Alignment Hardening (`v1.4.5.9` — `Rev 30–35`)

1. **`R30-1` / `R31-2` (7 + 1 + 3 Dual-Track Quotas & Runtime Assert)**: Enforced `ADAM_MASTERNODE_TRACK_QUOTA = 7`, `ADAM_COIN_LOCK_TRACK_QUOTA = 1`, and `ADAM_OPEN_POW_TRACK_QUOTA = 3` (`static_assert` in `src/adam.h` and runtime `assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold)` in `src/chainparams.cpp`), ensuring Active Masternodes (`4,200 KRISTA`) alone satisfy the post-bootstrap quorum threshold (`7`).
2. **`R31-1` / `R32-2` / `R32-3` / `R33-3` / `R34-2` (Same-Track Standby Handover at $t \ge 60\text{s}$ & Time-Relaxed Threshold at $t \ge 120\text{s}$)**:
   - Added `ResolveAdamCommitteeForTime` and `ValidateAdamCommitteeForTime` (`src/adam.cpp`) allowing silent primary committee seats at `nTimeElapsed >= 60s` (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`) to be replaced strictly by same-track standby candidates (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) in sequential rank order verified against the block's `CCoinsViewCache* pView`.
   - Added `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` relaxing the quorum threshold by `1` every `60s` starting at $t = 120\text{s}$ down to `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` non-MN seats, ensuring at least 1 Active Masternode is always required when post-bootstrap OR active MNs $\ge 7$, or `3` during bootstrap when active MNs $< 7$). Header-only validation (`ContextualCheckBlockHeader`) passes `0` for `nActiveMasternodes` to prevent false sync/reorg bans, while full block validation (`CheckFormsOfWork`) passes `GetActiveMasternodePoolCount(nHeight - 1, pView)`.
3. **`R33-4` / `R34-3` / `R35-2` (Equal Work Equal Seat Pay, 50% Automatic Protocol Burn & Developer Treasury Split)**:
   - Under `UPGRADE_FAIRNESS` (Height `1`) & Model D (`FillBlockPayee` and `IsBlockPayeeValid` in `src/masternode-payments.cpp`), every solved committee seat (`!vAdamSolutions[i].empty()`, mandatory 33B compressed pubkey, primary or legitimate same-track standby at $t \ge 60\text{s}$) receives the exact equal `40% / 11 ≈ 3.636%` share (`75% / 11 ≈ 6.818%` Pre-Model D) regardless of track (`nPartPaymentPerSeat[i]`, integer truncation remainder paid only when all `11/11` seats solve).
   - Unsolved/empty seats and unsigned LLMQ slots (`prevBlock.vQuorumSig`, 1-block lag) receive **0%**, while the Coordinator retains its full `15%` (`12%` Staker + `3%` Coordinator in PoS) reward when rescuing a stalled block with standby validators. Any unclaimed empty-seat or unsigned-LLMQ shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)**.
4. **`R35-1` / `R35-5` (LLMQ `qblockprop` Anti-Censorship Substitution Veto)**: Evaluated only when the LLMQ member's tip matches `msg.block.hashPrevBlock` (`pProposalView != nullptr`), at $t \ge 60\text{s}$ LLMQ members veto proposals that substitute a primary seat (`vAdamMiners[i] != vExpectedMiners[i]`) whose valid solution is already cached locally (`mapAdamSolutionsCache`), while never vetoing empty primary seats (`vAdamMiners[i] == vExpectedMiners[i]`) so normal fast blocks are not delayed by in-flight P2P race conditions.
5. **`R32-1` / `R35-4` (Node-Wide Single-Block-Per-Parent Guard)**: `ActiveProposalGuard` in `src/miner.cpp` with non-destructive FIFO/age eviction (`PruneParentCoordMap`) prevents any node from concurrently or sequentially producing both a PoW and PoS block on the same parent across all block versions (`v11` and `v12`).
6. **`R36` Security Audit Hardening (`BULGU-1..13`)**:
   - **Fallback LLMQ Payee Resolution & Empty-Quorum Unclaimed Split (`BULGU-1`)**: Fallback LLMQ members without a `4,200 KRISTA` collateral UTXO are paid to their P2PKH validator destination, and if `vLlmqPayees.empty()`, `nLLMQSplitTotal` (10%) is added to `nUnclaimedPartSplit` and split **50% Automatic Protocol Burn + 50% Developer Treasury** instead of remaining with the Coordinator.
   - **Full-Committee Solver Liveness (`BULGU-2`)**: `BitcoinMiner` solver threads continue hashing across all local elected validator seats instead of breaking early at `cachedCount >= 5` on non-coordinator nodes or prior to the 30s proposal slot, eliminating premature solver starvation under `Rev 34.1` Equal-Work-Equal-Pay.
   - **Fork-Aware `pindexPrev` Ancestry & Treasury Quorum (`BULGU-3`)**: `GetAdamMinerPool` (`src/adam.cpp`) walks `pindexTip->GetAncestor(h)` from `pView->GetBestBlock()` rather than `chainActive[h]`, and `CheckInputs` (`src/main.cpp`) passes `pindexPrevSpend` and `&inputs` to `llmq::GetActiveQuorum`.
   - **Intra-Block BLS Key Consistency & VRF Pinning (`BULGU-4`)**: `CheckBlock` enforces a block-wide `mapBlockBlsKeys` consistency check across `vQuorumSig`, `vAdamSolutions`, `vAdamVRFProof`, and `vAdamCoordinatorSig`, and `RegisterBlockMasternodeActivity` pins `vAdamVRFProof`.
   - **P2P DoS Protections (`BULGU-5, 6, 7, 11, 12`)**: `ADAMSOL` rejects historical/far-future heights outside `chainActive.Height() +/- 2`; `mapQuorumBlockSigs` eviction preserves seen proposed blocks; `PROPOSEBLOCK` evaluates the `R35-1` anti-substitution veto after `setSeenProposedBlocks` insertion and P2P relay; and config/key file permissions are locked to `0600` (`BULGU-10`).

---

## 5. Consensus Security, Decentralized PoS Co-Signing & Solver Hardening (`v1.4.5.9` — `Rev 40–42`)

1. **`R40-F1` (Qt Shutdown `powRegUnlockGuard` / `pwalletMain` Use-After-Free Fix)**: Added `DashboardWidget::shutdownMinerWorkers()` -> `MinerHubWidget::shutdownMinerWorkers()` invoked inside `KRISTATECHGUI::removeAllWallets()` prior to `delete walletModel` and `Shutdown()`, joining `powRegWorkerThread` via `CancelMinerPoWRegistration()` and releasing `powRegUnlockGuard` while both `walletModel` and `pwalletMain` are alive, alongside a `ShutdownRequested() || !pwalletMain || !pcoinsTip` guard in `src/rpc/mining.cpp`.
2. **`K1` / `R42-F1` (`IsFutureBlockPreVerified` Cryptographic Admission, Full-Block Preservation & Deferred `AddInventoryKnown`)**: Hardened `SEC-04` in `src/main.cpp` by requiring `pindexPrev != nullptr` in `mapBlockIndex`, verifying `VerifySelfContainedBLSSignedData` (`vrfBlsPk == coordBlsPk`) and `GetAdamMinerPool` validator ownership (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`) in `v11+`, verifying active BFT majority `CQuorumSignature::Verify` against `llmq::GetActiveQuorum` (`3-of-5` up to `8-of-15`) when `IsModelDActive` in `v12+`, preventing `HEADERS` from overwriting queued full `CBlock` entries, checking full future blocks in `AlreadyHave(MSG_BLOCK)`, and deferring `pfrom->AddInventoryKnown(inv)` until `ProcessFutureBlocks`.
3. **`K2` (Pre-Model-D Version 12 Primary Coordinator Window Enforcement)**: Aligned `CheckBlock` (`src/main.cpp`) and `BitcoinMiner` / `CreateNewBlock` (`src/miner.cpp`) so Version 12 blocks during Pre-Model-D heights (e.g., Mainnet Blocks `4,800..4,999`) strictly enforce primary `expectedCoordinator` authority for `nBlockTimeDiff < 45s` (`ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS`) and allow PoBLS fallback rotation only once `nBlockTimeDiff >= 45s`.
4. **`K3` (`CTxMemPool::check` Sentinel `0x01:0` Assertion Bypass)**: Exempted `IsZeroCoinPoWTransaction(tx)` from `assert(pcoins->HaveCoin(txin.prevout))` inside `CTxMemPool::check` (`src/txmempool.cpp`), preventing assertion aborts on Regtest or under `-checkmempool`.
5. **`K4` / `R42-F2` (Decentralized Cooperative PoS Coordinator Co-Signing via `POSCOORDREQ` / `POSCOORDSIG` & Cache-Poisoning Prevention)**: Added P2P messages `POSCOORDREQ` and `POSCOORDSIG` (`src/protocol.h`, `src/llmq_messages.h`, `src/adam.cpp`, `src/main.cpp`, `src/miner.cpp`) so PoS stakers whose elected Coordinator resides on a remote node obtain `vAdamVRFProof` and `vAdamCoordinatorSig` trustlessly over P2P, tracking `mapSeenPoSCoordReqs` (`targetCoordinator`, `draftHeader`, and parent `adamSeed`) so incoming `POSCOORDSIG` responses must pass both `VerifyAdamVRFProof` and `VerifyAdamCoordinatorSig` over the reconstructed draft header before caching in `mapPoSCoordSigs` or P2P relay (`Misbehaving(20)` on forgery).
6. **`O2`–`O5` / `R42-F3` (Single-Dispatcher Strided Solver, Shared `CMinerRegistrationSlotGuard`, Deterministic `DeriveBLSFromCKey` & Lifecycle-Safe Coordinator Assembly)**: Replaced duplicate per-thread `BitcoinMiner` puzzle grinding with a coordinated strided worker dispatcher (`cs_adam_solver_dispatch`), unified `AutoRegisterMiner` and `registerminer` mutual exclusion via `CMinerRegistrationSlotGuard`, unified deterministic `DeriveBLSFromCKey` across all mining/registration/RPC paths, added interruptible sleep + unconditional `GenerateBitcoins` / post-`g_connman.reset()` shutdown join for `TryAssembleCoordinatorBlockAsync`, and added `ccminer` V12 3-permutation compound puzzle support (`<a1>+<a2>+<a3>+<minerIdx>`).

---

## 6. Bitcoin Core Security Alignment, BIP324, `HeadersSyncState` & PSBT (`KTIP-0012`)

1. **Consensus Taproot, CSV & BIP68 Enforcement (`src/main.cpp`)**: Enforced `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, and BIP68 `CheckSequenceLocks()` inside `ConnectBlock()` as well as `AcceptToMemoryPoolWorker()`, closing the mempool-vs-consensus script verification gap.
2. **P2P & RPC DoS Hardening (`src/main.cpp`, `src/net.h`, `src/httprpc.cpp`)**: Added `MAX_INV_SZ` (`50,000`) queue bounds on `vRecvGetData` (`GETDATA`), isolated `mapPartiallyDownloadedBlocks` per peer (`std::pair<NodeId, uint256>`) with `FinalizeNode()` cleanup (`CMPCTBLOCK` / `BLOCKTXN`), added token-bucket rate limiting on `ADDR` messages, implemented BIP133 `FEEFILTER` and `-blocksonly`, and added salted HMAC-SHA256 `-rpcauth` verification.
3. **`HeadersSyncState` Low-Work Header Pre-Sync (`src/main.cpp`)**: Buffered incoming `HEADERS` up to `MAX_HEADERS_PRESYNC_BUFFER` (`16,384`) to verify continuity and cumulative `GetBlockProof()` work before inserting headers into `mapBlockIndex`.
4. **BIP324 v2 Encrypted P2P Transport (`src/bip324.cpp`, `src/crypto/chacha20poly1305.cpp`, `src/net.cpp`)**: Implemented `NODE_P2P_V2`, `ElligatorSwift` key exchange, and forward-secure `FSChaCha20` / `FSChaCha20Poly1305` packet encryption with automatic v1 magic fallback.
5. **BIP174 PSBT & Legacy MultiSend Removal (`src/psbt.cpp`, `src/rpc/rawtransaction.cpp`, `src/wallet/rpcwallet.cpp`)**: Added BIP174 PSBT serialization and RPCs (`createpsbt`, `converttopsbt`, `decodepsbt`, `combinepsbt`, `utxoupdatepsbt`, `analyzepsbt`, `finalizepsbt`, `walletprocesspsbt`), and removed all disabled legacy MultiSend code across Core, RPC, Wallet, and Qt.

---

## 7. `v1.5.8.0` Hardening: KTDIP3 (`KTIP-0014`), IBD/Core Fixes & Sybil-Hardened Coordinator V2

1. **Deterministic On-Chain Masternodes & Hot/Cold Separation (`src/evo/deterministicmns.cpp`, `src/evo/providertx.cpp`, `src/masternode-payments.cpp`, `src/kernel.cpp`)**: Stored Masternode state in `evodb` (LevelDB) with consensus validation of `ProRegTx` / `ProUpServTx` / `ProUpRegTx` / `ProUpRevTx`, `>= 15` collateral confirmation checks (`4,200 KRISTA`), deterministic PoSe scoring, and null `phashBlock` safety guards. Under unified **KTDIP3 (`UPGRADE_DIP3 = 1` Mainnet & Testnet)**, Hot VPS nodes sign strictly with `blsPubKeyOperator` (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature`, empty `ecdsaSig`, bypassing `PinBLSPubKey`), collateral hijack/same-block spends are rejected (`bad-protx-collateral-owner`, `bad-protx-collateral-spent-in-block`), `AppendDip3OrDirectPayment` enforces `scriptPayout` + `nOperatorReward` (bps) splits, and `GetAdamMinerPool` / `ElectQuorumMembers` / `CalculateMPAWeight` (`PoM`) read deterministically from `evodb`.
2. **Mempool, RPC & Script Hardening (`src/main.cpp`, `src/miner.cpp`, `src/rpc/*.cpp`, `src/script/mescal.cpp`)**: Added `bad-protx-mempool-conflict` checks and block-assembly `ProTx` conflict tracking, fixed `sendrawtransaction`/mempool lock ordering (`cs_main` + single `LOCK(pool.cs)`) and `ParseProTxIntArg` integer argument validation, and hardened MESCAL compilation/decompilation with strict 32-bit `nLockTime` bounds (`0..4294967295`) and `"mode": "hash-equalverify"` (`OP_HASH160 <20B> OP_EQUALVERIFY`).
3. **Startup/Shutdown/IBD Integrity (`src/init.cpp`, `src/main.cpp`, `src/httpserver.cpp`, `src/qt/`)**: Automatically locked wallet-owned KTDIP3 collaterals on startup, preserved `evodb`/MN/ping state during `VerifyDB` (`!fVerifyingBlocks`) and released `evodb` prior to `-reindex`, fixed `HeadersSyncState` PoS-tip `maxPerHeaderWork`, anchored Mainnet Genesis (`#0`) checkpoint (`src/chainparams.cpp`), added `fTrustedReplay` ping replay fast-path and atomic `90s` `CStakerStatus`, removed blocking D-Bus splash introspection (`Notificator`), fixed HTTP `workQueue` UAF shutdown deadlock, and deferred Qt restart spawning until `g_datadir_lock` release.
4. **Historical V2 Coordinator Weights (`65 : 8 : 2`, superseded at Block 1 by `UPGRADE_FAIRNESS`) & Permanent Open-Entry (`src/adam.cpp`)**: Prior to `UPGRADE_FAIRNESS`, V2 lowered non-MN Coordinator VRF weights (`65 : 8 : 2`), and `nAdamOpenEntryHeight` enabled permanent 1-seat `0 KRISTA` Open-Entry admission at Height `2,500` (Mainnet) / `1` (Testnet & Regtest).

---

## 8. `UPGRADE_FAIRNESS` Economic & Protocol Fairness Hardening (Height `1`)

1. **Equal Work → Equal Pay & Rebalanced Pre-Model D**: `40% / 11` equal validator payout under Model D regardless of MN/Coin-Lock/Open-PoW track (remainder awarded only on `11/11` solved seats), and `25% Producer / 75% Active Validators` (`~6.818%`/seat) Pre-Model D split (`src/masternode-payments.cpp`).
2. **Fair Propagation Grace Windows**: `ADAM_SOLUTION_GRACE_WINDOW_MS = 3,000` (on non-Regtest when `< 11/11` solutions cached) and `LLMQ_SIG_GRACE_WINDOW_MS = 1,000` (`src/miner.cpp`).
3. **Uniform 17-Algo Single-Pass Puzzles & Compressed Validator Pubkeys**: Excludes asymmetric `X11KVS` (`algo 11`) from block-level `kUniformAlgos` (`4,080` V12 permutations) and enforces mandatory 33-byte compressed validator keys in `VerifyAdamSolution` (`src/primitives/block.cpp`, `src/adam.cpp`).
4. **1-Block Lag Signer-Only LLMQ Payout & Deterministic PoSe**: `10%` LLMQ payout (`GetPrevBlockLlmqSigners` + `ComputeModelDLlmqPayments`) pays exclusively members who signed `prevBlock.vQuorumSig` (forfeited slots split `50%` Burn + `50%` Treasury) with `-5/+5` deterministic PoSe scoring (`src/masternode-payments.cpp`, `src/evo/deterministicmns.cpp`).
5. **PoS Staker / Signing Coordinator Symmetry & Symmetric Fees**: PoS splits Producer share into **`12%` Staker + `3%` Coordinator** (Pre-Model D: `20%` / `5%`) and routes `100%` of PoS transaction fees (`nFees`) to the Staker (`src/masternode-payments.cpp`, `src/main.cpp`, `src/miner.cpp`).
6. **Live Lock-Preserving `PoL (3x)` Renewal & Reduced `PoM (1.25x)`**: Enables single-input `TX_CLTV_TIMELOCK` coinstake self-renewal (`vout[1]` preserving `scriptPubKey` and `nValue >= prevout.nValue`) while lowering `PoM` from `2.0x` ($\alpha = 1.0$) to `1.25x` ($\alpha = 0.25$) so Masternodes retain a modest uptime loyalty bonus without eclipsing `PoL (3x)` or `PoB (6x)` (`src/script/`, `src/kernel.cpp`, `src/main.cpp`, `src/wallet/wallet.cpp`).
7. **Proportional Capital Coordinator VRF (`4,200 : 250 : 10` with `75%` MN Floor) & 240-Key / 15-Bit Zero-Coin Parity**: `ADAM_FAIR_COORD_V4` removes per-track caps while enforcing `>= 75%` MN floor when `3 * sumNonMN > sumMN`; expands Zero-Coin capacity to `240` keys at Mainnet `15`-bit `HashX11KVS` parity (`src/adam.cpp`).
8. **Fair Bootstrap Limit**: Sets Mainnet `nAdamBootstrapLimit = 2,500` (`src/chainparams.cpp`).
