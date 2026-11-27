```
KTIP: 0010
Title: On-Chain Masternode Heartbeat Pings, Standby Handover, Time-Relaxed Quorum & Equal-Work Incentive Alignment
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus / Economics)
Created: 2026-11-09
```

## Abstract
This proposal specifies the **100% On-Chain Masternode Activity (`SendOnChainPing` / `ExtractOnChainMasternodePing`)**, **Same-Track Standby Validator Seat Handover ($t \ge 60\text{s}$)**, **Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$)**, **Equal Work → Equal Pay (`60% / 15 = 4.00%` Balanced Model D, `75% / 11 ≈ 6.818%` Pre-Model D) and 50% Automatic Protocol Burn / 50% Treasury Unclaimed Split**, **LLMQ Anti-Substitution Veto**, and **Node-Wide Single-Block-Per-Parent Guard (`ActiveProposalGuard`)** mechanisms. Together, these rules guarantee deterministic sync-independent consensus validation, deadlock recovery when `8+` primary validators under Model D (`5+` Pre-Model D) are simultaneously offline, and strict incentive compatibility where neither censoring honest validators nor substituting standby keys yields any financial gain.

## Motivation
Prior to `KTIP-0010`, four structural edge cases existed:
1. **P2P State Divergence During Sync/Reorg (`SEC-12`)**: Relying on local in-memory `mnodeman` P2P state for committee or quorum elections could cause newly syncing or partitioned nodes to compute different active Masternode sets.
2. **Stalled Block Deadlock (`R31-1`)**: Because `SelectAdamNodes` depends only on the parent block's rolling seed, if `8` out of `15` elected primary validators under Model D (`5` out of `11` Pre-Model D) went offline simultaneously, the block could never reach the quorum threshold of `8` (`7` Pre-Model D)—and because chain height stopped advancing, the 2,880-block activity window could never expire offline nodes.
3. **Standby Substitution Gaming (`R32-2` / `R33-4` / `R35-2`)**: If a Coordinator could freely replace primary seats after 60 seconds and keep the substituted seat's reward (or redistribute dropped seats' shares among remaining seats), a rational Coordinator would wait 60 seconds and substitute honest validators with its own keys.
4. **Same-Parent Dual PoW+PoS Proposals (`R32-1`)**: A node running both PoW and PoS threads could propose both a PoW block and a PoS block on the same parent, causing orphan forks and splitting LLMQ votes under Model D.

## Specification

### 1. 100% On-Chain Masternode Activity & Heartbeat Pings (`SendOnChainPing`)
A Masternode is eligible for consensus election (`SelectAdamNodes`, `ElectQuorumMembers`, and `FillBlockPayee` / `IsBlockPayeeValid`) at block height $H$ if and only if:
1. Its `4,200 KRISTA` collateral UTXO (`collateralOutpoint`) is unspent in the block's `CCoinsViewCache` (`pView->HaveCoin(outpoint)`) with confirmation depth (`IsValidMasternodeCollateralAndAuth`).
2. Its operator public key (`pubKeyMasternode`) has recorded on-chain activity within the preceding **2,880 blocks** (`IsMasternodeActiveAtHeight(pubKey, H - 1, 2880)`):
   - Participating as a block Coordinator, LLMQ signer, or ADAM validator (renewing activity at zero fee), OR
   - Broadcasting an authenticated on-chain Heartbeat Ping transaction (`0.01 KRISTA` self-spendable `<33/65-byte pubkey> OP_CHECKSIGVERIFY OP_1` UTXO + `OP_RETURN` proof `collateralOutpoint || vchPingSig` [+ optional `COnChainMasternodeDelegation`], paying a flat `0.0001 KRISTA` fee exempt from the `1000x` `OP_RETURN` multiplier), automatically issued by `SendOnChainPing` (`src/activemasternode.cpp`) whenever an active Hot Masternode has had no on-chain block activity for **$\ge 1,440$ blocks (~12 hours)** within the 2,880-block (~24-hour) window.

---

### 2. Same-Track Standby Validator Seat Handover ($t \ge 60\text{s}$)
Let $\Delta t = \text{block.nTime} - \text{pindexPrev->GetBlockTime()}$ be the elapsed block time.
* For $\Delta t < 60\text{s}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`), `block.vAdamMiners` must exactly equal the `15` primary validators `vExpectedMiners` under Balanced Model D (`11` Pre-Model D).
* For $\Delta t \ge 60\text{s}$ (advancing in `ADAM_COMMITTEE_FALLBACK_SLOT_SECS = 30` slots), up to $n_{\text{slot}}(\Delta t)$ silent primary seats **per track** (up to all $M = 15$ Model D / $11$ Pre-Model D committee seats) may be replaced sequentially by same-track standby candidates from `vStandbyMiners` (`ResolveAdamCommitteeForTime` / `ValidateAdamCommitteeForTime` in `src/adam.cpp`):

  $$
  n_{\text{slot}}(\Delta t) = \left\lfloor \frac{\Delta t - 60}{30} \right\rfloor + 1, \quad \text{trackStandbyIdx} < \text{nReplacedInTrack}[t] + n_{\text{slot}}(\Delta t)
  $$

* **Strict Same-Track Replacement (`GetCandidateTrack` with `pView`)**:
  - A **Masternode** primary seat (`Track 0`) can only be replaced by a standby **Masternode** (`Track 0`).
  - A **Coin-Lock** primary seat (`Track 1`) can only be replaced by a standby **Coin-Lock** validator (`Track 1`).
  - An **Open PoW** primary seat (`Track 2`) can only be replaced by a standby **Open PoW** miner (`Track 2`).
* **Sequential Rank Enforcement & Non-Empty Solution Requirement**: Within each track, standby validators must be consumed in strict ascending rank order (`trackStandbyIdx <= prevStandbyIdxInTrack[expectedTrack]` is rejected), and every substituted seat `i` must carry a valid non-empty PoW solution (`!vAdamSolutions[i].empty()`).

---

### 3. Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$)
If the committee still cannot reach the baseline threshold $T_0 = \text{GetAdamThreshold}(H, nActiveMasternodes)$ (which under Balanced Model D + `UPGRADE_HARDENING_V4` Height `1` already enforces `8` during bootstrap on Mainnet / Regtest once `nActiveMasternodes >= 8`, falling back to `5` only while active MNs `< 8`, or `3` on Testnet) after standby handover begins, `GetAdamEffectiveThreshold(nHeight, nElapsedSecs, nActiveMasternodes)` (`src/consensus/params.h`) relaxes the threshold by `1` every `60 seconds` starting at $\Delta t = 120\text{s}$:

$$
T_{\text{eff}}(H, \Delta t) = \max\left(\min(T_0, T_{\min}), T_0 - \left(\left\lfloor \frac{\Delta t - 120}{60} \right\rfloor + 1\right)\right) \quad (\Delta t \ge 120\text{s})
$$

Where the floor $T_{\min}$ is:
* **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** when `UPGRADE_MODELD` is active AND (`H >= nAdamThresholdBootstrapLimit` OR `nActiveMasternodes >= 8`). Because non-Masternode seats under Model D are capped at `2 Coin-Lock + 5 Open PoW = 7 seats`, a minimum threshold of **`8 > 7`** guarantees that **at least 1 Active Masternode (`4,200 KRISTA`)** must always contribute a valid solution (clamped to $T_0 = 3$ on Testnet where `nAdamThresholdModelD = 3`).
* **`ADAM_MIN_RELAXED_THRESHOLD = 5`** Pre-Model D whenever `H >= nAdamThresholdBootstrapLimit` OR `nActiveMasternodes >= 7` (`5 > 4` non-MN seats: `1 Coin-Lock + 3 Open PoW`).
* **`ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = 3`** only during the early bootstrap phase when `H < nAdamThresholdBootstrapLimit` AND `nActiveMasternodes < 8` under Model D (`< 7` Pre-Model D).
* **Sync-Safe Header Validation (`R34-2`)**: `ContextualCheckBlockHeader` passes `nActiveMasternodes = 0` so syncing nodes never falsely ban peers (`Misbehaving(100)`) over tip-dependent MN counts, whereas full block validation (`CheckFormsOfWork` in `CheckBlock`) enforces the exact parent view count `GetActiveMasternodePoolCount(H - 1, pView)`.

---

### 4. Equal Work → Equal Pay & Automatic Protocol Burn of Unclaimed Shares
Under `UPGRADE_FAIRNESS` (Height `1`) & Balanced Model D (`FillBlockPayee` and `IsBlockPayeeValid` in `src/masternode-payments.cpp`), the **60%** Participant Validator pool (`nParticipantSplit`; **75%** Pre-Model D via `ComputeModelDValidatorSeatPayments`) and **25%** Coordinator/Producer pool (`nProducerSplit`; **25%** Pre-Model D) are governed by three rules:
1. **Equal Work → Equal Pay per Solved Seat** (with $M = \text{vCommitteeMiners.size()} = 15$ under Model D, $11$ Pre-Model D):

   $$
   \text{nPartPaymentPerSeat}[i] = \begin{cases}
   \lfloor \text{nPartSplitTotal} / M \rfloor & \text{if } < M \text{ seats solved (} 60\% / 15 = 4.00\% \text{ Model D, } 75\% / 11 \approx 6.818\% \text{ Pre-Model D)} \\
   \lfloor \text{nPartSplitTotal} / M \rfloor + (\text{nPartSplitTotal} \bmod M \text{ on last solved seat}) & \text{if all } M/M \text{ seats solved (} 15/15 \text{ Model D, } 11/11 \text{ Pre-Model D)}
   \end{cases}
   $$

   Every seat $i \in \lbrace 0, \dots, M - 1 \rbrace$ (`0..14` under Model D, `0..10` Pre-Model D) with a non-empty valid solution (`!block.vAdamSolutions[i].empty()`, mandatory 33-byte compressed pubkey, whether primary or legitimate same-track standby at $\Delta t \ge 60\text{s}$) receives `nPartPaymentPerSeat[i]` (`0.6324 KRISTA` in Period 0 under Model D with `0` satoshi remainder when `15/15` solve) regardless of MN, Coin-Lock, or Open-PoW track, while the rescue Coordinator retains its full **25%** share (**22%** Staker + **3%** Coordinator in PoS under Model D; **20%** Staker + **5%** Coordinator Pre-Model D) since primary Coordinator exclusivity expires at $\Delta t = 45\text{s}$ and LLMQ enforces the `qblockprop` anti-substitution and `5s` primary omission vetoes.
2. **Unsolved Seats & Unclaimed Shares**:
   Every empty placeholder seat (`block.vAdamSolutions[i].empty()`) receives **0%**, and its unclaimed seat share is added to `nUnclaimedPartSplit`.
3. **50/50 Automatic Protocol Burn & Developer Treasury Split**:
   Whenever `nUnclaimedPartSplit > 0`, 50% (`nProtocolBurnSplit = nUnclaimedPartSplit / 2`) is automatically burned on-chain to the primary consensus burn address (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, permanently reducing `nMoneySupply` as pure supply deflation without granting personal PoB weight in `mapAddressBurns`, preventing any censorship incentive for block producers), and the remaining 50% (`nUnclaimedPartSplit - nProtocolBurnSplit`) is added to `nExpectedTreasury`:

   $$
   \text{nExpectedBurn} = \lfloor \text{nUnclaimedPartSplit} / 2 \rfloor, \quad \text{nExpectedTreasury} = \text{nTreasurySplit} + (\text{nUnclaimedPartSplit} - \text{nExpectedBurn})
   $$

---

### 5. LLMQ `qblockprop` Anti-Substitution / `5s` Omission Veto, `M-4` PoSe Fairness & `ActiveProposalGuard`
1. **LLMQ `qblockprop` Anti-Substitution & R35-1 `5s` Primary Omission Veto (`src/net_processing.cpp`)**:
   - Evaluated only when the LLMQ member's active chain tip matches `msg.block.hashPrevBlock` (`pProposalView != nullptr`).
   - At $\Delta t \ge 60\text{s}$, if a proposal substitutes seat $i$ (`msg.block.vAdamMiners[i] != vExpectedMiners[i]`) and the LLMQ member already holds a valid cached solution from `vExpectedMiners[i]` in `mapAdamSolutionsCache`, the LLMQ member vetoes the proposal.
   - **R35-1 `5s` Primary Omission Veto (`mapAdamSolutionFirstSeen`)**: If a proposal leaves an expected primary seat empty (`msg.block.vAdamMiners[i] == vExpectedMiners[i] && msg.block.vAdamSolutions[i].empty()`), LLMQ members veto the proposal whenever they hold a valid cached solution for `vExpectedMiners[i]` in `mapAdamSolutionsCache` whose first-seen timestamp in `mapAdamSolutionFirstSeen` is **`>= 5s`** old (`nowVeto - itFirstSeen->second >= 5`; solutions seen `< 5s` ago are tolerated so in-flight P2P propagation never stalls fast blocks). *(Pre-V4 historical behavior: empty primary seats were previously never vetoed.)*
2. **`M-4` PoSe Fairness (`UPGRADE_HARDENING_V4`, Height `1` in `src/evo/deterministicmns.cpp`)**:
   - Silent primary ADAM committee members displaced by standby substitution (`block.vAdamMiners[i] != vExpectedMiners[i]`) accrue **`+5` PoSe** (alongside `+5` PoSe when a primary Coordinator misses its slot).
   - LLMQ quorum signing grants `-5` PoSe recovery, while LLMQ non-signing no longer accrues `+5` PoSe punishment (replacing pre-V4 `-5/+5` LLMQ omission penalties).
3. **Node-Wide Single-Block-Per-Parent Guard (`ActiveProposalGuard` in `src/miner.cpp`)**:
   - Tracks in-flight (`mapInFlightParentCoord`) and completed (`mapProducedParentCoord`) block proposals per `(parentHash, coordKey)` across all block versions (`v11` and `v12`) with non-destructive FIFO/age eviction (`PruneParentCoordMap`), preventing any node from producing both a PoW and PoS block on the same parent.

## Backward Compatibility
* Fully active in `v1.4.5.9+` (current `v1.5.9.0`) across Mainnet, Testnet, and Regtest.

## Reference Implementation
* Standby handover & track validation: `ResolveAdamCommitteeForTime` and `ValidateAdamCommitteeForTime` in `src/adam.cpp`.
* Time-relaxed threshold: `GetAdamEffectiveThreshold` in `src/consensus/params.h`.
* Equal-work payouts & 50% automatic protocol burn / 50% treasury split: `FillBlockPayee` and `IsBlockPayeeValid` in `src/masternode-payments.cpp`.
* LLMQ anti-substitution & `5s` omission veto, `M-4` PoSe fairness, & single-proposal guard: `src/net_processing.cpp`, `src/evo/deterministicmns.cpp`, `src/main.cpp`, and `src/miner.cpp`.
