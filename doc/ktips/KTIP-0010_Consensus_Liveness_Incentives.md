```
KTIP: 0010
Title: On-Chain Masternode Heartbeat Pings, Standby Handover, Time-Relaxed Quorum & Equal-Work Incentive Alignment
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus / Economics)
Created: 2026-11-09
```

## Abstract
This proposal specifies the **100% On-Chain Masternode Activity (`SendOnChainPing` / `ExtractOnChainMasternodePing`)**, **Same-Track Standby Validator Seat Handover ($t \ge 60\text{s}$)**, **Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$)**, **Equal Work, Equal Reward (`8.00%` / `4.00%` / `~2.857%` per-seat track share) and 50% Automatic Protocol Burn / 50% Treasury Unclaimed Split**, **LLMQ Anti-Substitution Veto**, and **Node-Wide Single-Block-Per-Parent Guard (`ActiveProposalGuard`)** mechanisms. Together, these rules guarantee deterministic sync-independent consensus validation, deadlock recovery when 5+ primary validators are simultaneously offline, and strict incentive compatibility where neither censoring honest validators nor substituting standby keys yields any financial gain.

## Motivation
Prior to `KTIP-0010`, four structural edge cases existed:
1. **P2P State Divergence During Sync/Reorg (`SEC-12`)**: Relying on local in-memory `mnodeman` P2P state for committee or quorum elections could cause newly syncing or partitioned nodes to compute different active Masternode sets.
2. **Stalled Block Deadlock (`R31-1`)**: Because `SelectAdamNodes` depends only on the parent block's rolling seed, if 5 out of 11 elected primary validators went offline simultaneously, the block could never reach the quorum threshold of 7—and because chain height stopped advancing, the 2,880-block activity window could never expire offline nodes.
3. **Standby Substitution Gaming (`R32-2` / `R33-4` / `R35-2`)**: If a Coordinator could freely replace primary seats after 60 seconds and keep the substituted seat's reward (or redistribute dropped seats' shares among remaining seats), a rational Coordinator would wait 60 seconds and substitute honest validators with its own keys.
4. **Same-Parent Dual PoW+PoS Proposals (`R32-1`)**: A node running both PoW and PoS threads could propose both a PoW block and a PoS block on the same parent, causing orphan forks and splitting LLMQ votes under Model D.

## Specification

### 1. 100% On-Chain Masternode Activity & Heartbeat Pings (`MNPING1`)
A Masternode is eligible for consensus election (`SelectAdamNodes`, `ElectQuorumMembers`, and `FillBlockPayee` / `IsBlockPayeeValid`) at block height $H$ if and only if:
1. Its `4,200 KRISTA` collateral UTXO (`collateralOutpoint`) is unspent in the block's `CCoinsViewCache` (`pView->HaveCoin(outpoint)`) with confirmation depth (`IsValidMasternodeCollateralAndAuth`).
2. Its operator public key (`pubKeyMasternode`) has recorded on-chain activity within the preceding **2,880 blocks** (`IsMasternodeActiveAtHeight(pubKey, H - 1, 2880)`):
   - Participating as a block Coordinator, LLMQ signer, or ADAM validator, OR
   - Broadcasting an authenticated on-chain Heartbeat Ping transaction (`<33/65-byte pubkey> OP_CHECKSIGVERIFY OP_1` + `OP_RETURN` proof `collateralOutpoint || vchPingSig`), automatically issued by `SendOnChainPing` (`src/activemasternode.cpp`) whenever an active Hot Masternode has had no on-chain block activity for **$\ge 1,440$ blocks (~12 hours)** within the 2,880-block (~24-hour) window.

---

### 2. Same-Track Standby Validator Seat Handover ($t \ge 60\text{s}$)
Let $\Delta t = \text{block.nTime} - \text{pindexPrev->GetBlockTime()}$ be the elapsed block time.
* For $\Delta t < 60\text{s}$ (`ADAM_STANDBY_ROTATION_START_SECS = 60`), `block.vAdamMiners` must exactly equal the 11 primary validators `vExpectedMiners`.
* For $\Delta t \ge 60\text{s}$, up to $K(\Delta t)$ silent primary seats may be replaced by standby candidates from `vStandbyMiners` (`ResolveAdamCommitteeForTime` / `ValidateAdamCommitteeForTime` in `src/adam.cpp`):

  $$
  K(\Delta t) = \min\left(11, 4 \times \left(\left\lfloor \frac{\Delta t - 60}{30} \right\rfloor + 1\right)\right)
  $$

* **Strict Same-Track Replacement (`GetCandidateTrack` with `pView`)**:
  - A **Masternode** primary seat (`Track 0`) can only be replaced by a standby **Masternode** (`Track 0`).
  - A **Coin-Lock** primary seat (`Track 1`) can only be replaced by a standby **Coin-Lock** validator (`Track 1`).
  - An **Open PoW** primary seat (`Track 2`) can only be replaced by a standby **Open PoW** miner (`Track 2`).
* **Sequential Rank Enforcement & Non-Empty Solution Requirement**: Within each track, standby validators must be consumed in strict ascending rank order (`k > nextExpectedStandbyRankInTrack[seatTrack]` is rejected), and every substituted seat `i` must carry a valid non-empty PoW solution (`!vAdamSolutions[i].empty()`).

---

### 3. Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$)
If the committee still cannot reach the baseline threshold $T_0 = \text{GetAdamThreshold}(H)$ after standby handover begins, `GetAdamEffectiveThreshold(nHeight, nElapsedSecs, nActiveMasternodes)` (`src/consensus/params.h`) relaxes the threshold by `1` every `60 seconds` starting at $\Delta t = 120\text{s}$:

$$
T_{\text{eff}}(H, \Delta t) = \max\left(T_{\min}, T_0 - \left(\left\lfloor \frac{\Delta t - 120}{60} \right\rfloor + 1\right)\right) \quad (\Delta t \ge 120\text{s})
$$

Where the floor $T_{\min}$ is:
* **`ADAM_MIN_RELAXED_THRESHOLD = 5`** whenever `H >= nAdamThresholdBootstrapLimit` OR `nActiveMasternodes >= 7`. Because non-Masternode seats are capped at `1 Coin-Lock + 3 Open PoW = 4 seats`, a minimum threshold of `5 > 4` guarantees that **at least 1 Active Masternode (`4,200 KRISTA`)** must always contribute a valid solution.
* **`ADAM_BOOTSTRAP_SPARSE_MIN_THRESHOLD = 3`** only during the early bootstrap phase when `H < nAdamThresholdBootstrapLimit` AND `nActiveMasternodes < 7`.
* **Sync-Safe Header Validation (`R34-2`)**: `ContextualCheckBlockHeader` passes `nActiveMasternodes = 0` so syncing nodes never falsely ban peers (`Misbehaving(100)`) over tip-dependent MN counts, whereas full block validation (`CheckFormsOfWork` in `CheckBlock`) enforces the exact parent view count `GetActiveMasternodePoolCount(H - 1, pView)`.

---

### 4. Equal Work, Equal Reward & Automatic Protocol Burn of Unclaimed Shares
Under Model D (`FillBlockPayee` and `IsBlockPayeeValid` in `src/masternode-payments.cpp`), the 40% Participant Validator pool (`nParticipantSplit`, split into 20% across the 4 Independent Miner quota seats and 20% across the 7 Masternode Validator quota seats via `ComputeModelDValidatorSeatPayments`) and 15% Coordinator pool (`nProducerSplit`) are governed by three rules:
1. **Equal Work, Equal Reward per Solved Seat**:

   $$
   \text{nPartPaymentPerSeat}[i] = \begin{cases}
   \lfloor 2 \times \text{nMinerValidatorPool} / 5 \rfloor & \text{if seat } i \text{ is the Coin-Lock validator quota seat (8.00\%)} \\
   \lfloor \text{nMinerValidatorPool} / 5 \rfloor & \text{if seat } i \text{ is an Open PoW validator quota seat (4.00\% each)} \\
   \lfloor \text{nMNValidatorPool} / 7 \rfloor & \text{if seat } i \text{ is a Masternode Validator quota seat } ({\sim}2.857\% \text{ each})
   \end{cases}
   $$

   Every seat $i \in \lbrace 0, \dots, 10 \rbrace$ with a non-empty valid solution (`!block.vAdamSolutions[i].empty()`, whether primary or legitimate same-track standby at $\Delta t \ge 60\text{s}$) receives `nPartPaymentPerSeat[i]`, while the rescue Coordinator retains its full 15% share (since primary Coordinator exclusivity already expires at $\Delta t = 45\text{s}$ and LLMQ enforces the `qblockprop` anti-substitution veto).
2. **Unsolved Seats & Unclaimed Shares**:
   Every empty placeholder seat (`block.vAdamSolutions[i].empty()`) receives **0%**, and its unclaimed seat share is added to `nUnclaimedPartSplit`.
3. **50/50 Automatic Protocol Burn & Developer Treasury Split**:
   Whenever `nUnclaimedPartSplit > 0`, 50% (`nProtocolBurnSplit = nUnclaimedPartSplit / 2`) is automatically burned on-chain to the primary consensus burn address (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, permanently reducing `nMoneySupply` as pure supply deflation without granting personal PoB weight in `mapAddressBurns`, preventing any censorship incentive for block producers), and the remaining 50% (`nUnclaimedPartSplit - nProtocolBurnSplit`) is added to `nExpectedTreasury`:

   $$
   \text{nExpectedBurn} = \lfloor \text{nUnclaimedPartSplit} / 2 \rfloor, \quad \text{nExpectedTreasury} = \text{nTreasurySplit} + (\text{nUnclaimedPartSplit} - \text{nExpectedBurn})
   $$

---

### 5. LLMQ `qblockprop` Anti-Substitution Veto & `ActiveProposalGuard`
1. **LLMQ `qblockprop` Anti-Substitution Veto (`src/main.cpp`)**:
   - Evaluated only when the LLMQ member's active chain tip matches `msg.block.hashPrevBlock` (`pProposalView != nullptr`).
   - At $\Delta t \ge 60\text{s}$, if a proposal substitutes seat $i$ (`msg.block.vAdamMiners[i] != vExpectedMiners[i]`) and the LLMQ member already holds a valid cached solution from `vExpectedMiners[i]` in `mapAdamSolutionsCache`, the LLMQ member vetoes the proposal.
   - Empty primary seats (`msg.block.vAdamMiners[i] == vExpectedMiners[i]`) are never vetoed so in-flight P2P solutions never delay fast blocks.
2. **Node-Wide Single-Block-Per-Parent Guard (`ActiveProposalGuard` in `src/miner.cpp`)**:
   - Tracks in-flight (`mapInFlightParentCoord`) and completed (`mapProducedParentCoord`) block proposals per `(parentHash, coordKey)` across all block versions (`v11` and `v12`) with non-destructive FIFO/age eviction (`PruneParentCoordMap`), preventing any node from producing both a PoW and PoS block on the same parent.

## Backward Compatibility
* Fully active in `v1.4.5.9` across Mainnet, Testnet, and Regtest.

## Reference Implementation
* Standby handover & track validation: `ResolveAdamCommitteeForTime` and `ValidateAdamCommitteeForTime` in `src/adam.cpp`.
* Time-relaxed threshold: `GetAdamEffectiveThreshold` in `src/consensus/params.h`.
* Equal-work payouts & 50% automatic protocol burn / 50% treasury split: `FillBlockPayee` and `IsBlockPayeeValid` in `src/masternode-payments.cpp`.
* LLMQ substitution veto & single-proposal guard: `src/main.cpp` and `src/miner.cpp`.
