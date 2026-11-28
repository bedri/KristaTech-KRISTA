```
KTIP: 0006
Title: Model D Network Upgrade (LLMQ topologies, DKG session timing, and verification thresholds)
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-06-16
```

## Abstract
This proposal specifies the **Model D Network Upgrade**, which marks the transition of the KRISTA network from the initial bootstrap phase to its mature, long-term consensus and economic design. Model D activates Long-Living Masternode Quorums (LLMQs) using Distributed Key Generation (DKG) sessions, enforces hybrid PoBLS quorum signature validation, reduces the active miner registration period to prevent stale/abandoned slots, and enables the final economic block reward split.

## Motivation
During the short PoW bootstrap phase (blocks `1..199` prior to block `200` on Mainnet and Testnet), the KRISTA network initializes coinbase keys and foundational UTXOs without risking network deadlocks due to immature validator pools. 

Model D addresses this by implementing a unified single-phase transition at Block `200` (`1..199` bootstrap $\rightarrow$ Block `200` full Model D + LLMQ + PoMBL):
1. **Bootstrap Phase (`1..199`)**: Passive Masternode (`5%`) and LLMQ (`10%`) payments are set to `0%` prior to Model D (`100%` net Miner on Classic PoW blocks `2..199`, transitioning cleanly at Block `200` into Balanced Model D). Prior to Block `200`, quorum signature verification is not yet active ($T = 0$) to allow Classic PoW bootstrap to complete cleanly.
2. **Maturation Phase (Balanced Model D, Block `200+`)**: Starting at Block `200` on Mainnet, Testnet, and Regtest, Model D activates LLMQ topologies, expands the ADAM committee to **15 seats (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Free Entry 0 KRISTA`)** with a **`55%` Active MN Coordinator VRF floor**, enforces strict signature verification thresholds (`8`-of-`15` ADAM once `nActiveMasternodes >= 8` under `UPGRADE_HARDENING_V4` Height `1`, falling back to `5` only while `< 8` — plus `2/3` BFT LLMQ threshold `floor(2N/3)+1 ∈ {4, 7, 8, 11}`), and sets the final split to **`5%` Passive MN + `10%` Signer-Only LLMQ (1-block lag) + `25%` Producer (`22%` Staker + `3%` Coordinator in PoS) + `60%` Equal Active Validators (`60% / 15 = 4.00%`/seat)**.

## Specification

### 1. Upgrade Activation Timing
The Model D upgrade (`Consensus::UPGRADE_MODELD`) is activated cleanly alongside `UPGRADE_POMBL`, `UPGRADE_ADAM_V2`, and `UPGRADE_STAKE_MIN_DEPTH_V2` at Block `200`:

| Network | Activation Height | Status |
| --- | --- | --- |
| **Mainnet** | Block 200 | Active |
| **Testnet** | Block 200 | Active |
| **Regtest** | Block 200 | Active |

---

### 2. Long-Living Masternode Quorums (LLMQs) & DKG Sessions
Under Model D, the network organizes active, registered masternodes into Long-Living Masternode Quorums (LLMQs) using a simulated Distributed Key Generation (DKG) session.

* **Quorum Size ($N$)**: Dynamic BFT sizing (`UPGRADE_HARDENING_V1` / `GetTargetQuorumSize` in `src/llmq.cpp`) scales with verified active on-chain Masternodes (`<25` $\rightarrow$ `5`, `25–49` $\rightarrow$ `9`, `50–99` $\rightarrow$ `11`, `>=100` $\rightarrow$ `15`). When `< N` Active Masternodes exist, deficit seats are filled with quota-capped standby candidates (`max 1 Coin-Lock + max 3 Funded PoW-Locks`).
* **DKG Rotation Interval (`GetActiveQuorum`)**:
  * **Mainnet & Testnet**: Once activated at Block `200` (`nHeight >= 200`), a fresh quorum is elected **every block** (`H_{\text{DKG}} = H`, so there is no separate delayed rotation boundary).
  * **Regtest**: Prior to block `800`, anchored every **10 blocks** (clamped to `UPGRADE_POMBL = 300`), and **every block** from block `800` onwards:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 10 \rfloor \times 10,\; H_{\text{PoMBL}}\right) & \text{if } H < 800 \text{ (Regtest only)} \\
    H & \text{if } H \ge H_{\text{rot}}
    \end{cases}
    $$
  
  Where $H$ is the current block height (`nHeight`), $H_{\text{PoMBL}}$ is `UPGRADE_POMBL` (`200` on Mainnet/Testnet, `300` on Regtest), $H_{\text{rot}} = 200$ on Mainnet/Testnet ($800$ on Regtest), and $H_{\text{DKG}}$ is the rotation height for which the active quorum is elected.

* **Quorum Election**: Elected members are determined deterministically by sorting all active masternodes using a score derived from the rolling entropy seed of the preceding block and the masternode's collateral outpoint. When fewer than $N$ verified Active Masternodes exist on-chain (`nVerifiedOnChainMNs < N`), all available real Active Masternodes are strictly guaranteed inclusion first, preserving genuine Masternode operator quorum seats. The remaining deficit slots to complete the $N$-member quorum are filled from fallback candidate pools (top Coin-Lock validators, followed by top PoW-Lock validators), with each candidate strictly quota-capped (max 1 Coin-Lock + max 3 Funded PoW-Locks) to maintain Byzantine isolation and prevent any single entity or multi-lock cluster from capturing LLMQ signing.

---

### 3. Quorum Signature Verification Thresholds
Blocks of Version 12 and above contain a quorum signature payload `vQuorumSig` validating the block header hash. The verification threshold ($T$) in `CQuorumSignature::Verify()` via `llmq::GetQuorumThreshold(N, nHeight)` (`src/llmq.cpp`) is calculated as follows:

* **Prior to Model D Activation (`!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)` on Mainnet / Testnet)**:
  Quorum signature verification is bypassed ($T = 0$) to allow bootstrapping before active LLMQ quorums are required.
* **Once Model D is Active (All Networks: Mainnet, Testnet, Regtest)**:
  Under `UPGRADE_HARDENING_V4` (`"Hardening_v4"`, Height `1` across all networks), the threshold is set to a strict dynamic `2/3` BFT supermajority ($\lfloor 2N / 3 \rfloor + 1$):
  
  $$T = \lfloor 2N / 3 \rfloor + 1 \in \{4, 7, 8, 11\}$$
  
  Thus, a strict `2/3` BFT supermajority (`4-of-5`, `7-of-9`, `8-of-11`, or `11-of-15` matching `GetTargetQuorumSize`) of quorum members must sign the block hash across all networks once Model D is active. *(Pre-V4 historical formula: $\lfloor N / 2 \rfloor + 1 \in \{3, 5, 6, 8\}$, i.e., `3-of-5`, `5-of-9`, `6-of-11`, `8-of-15`.)*

---

### 4. Economic Model & Block Reward Splits
Model D enforces the final, sustainable economic split of the block reward. The split structure changes based on the block type:

#### A. Proof-of-Work (PoW) Blocks
For blocks mined via PoW (ADAM cooperative mining), the distributable block reward $R$ (after deducting the 7.0% Developer Treasury) is split as follows under Balanced Model D + `UPGRADE_FAIRNESS`:
* **Passive Masternode Reward (5%)**: Distributed to one active Masternode deterministically selected by VRF seed modulo (`seed.GetUint64(0) % vActiveMns.size()` in `GetModelDPassiveMasternodePayee`).
* **Signer-Only LLMQ Reward (10%, 1-Block Lag)**: Divided across the $N \in \{5, 9, 11, 15\}$ quorum slots (`10% / N`) and paid strictly with a **1-block lag** to the active LLMQ members who signed the **previous block** (`prevBlock.vQuorumSig`; under `UPGRADE_HARDENING_V4` `M-4` PoSe fairness, signing grants `-5` PoSe recovery while LLMQ non-signing no longer accrues `+5` PoSe penalty — pre-V4 `-5/+5` — whereas `+5` PoSe instead penalizes missed primary coordinators and silent primary ADAM committee members displaced by standby substitution); unsigned quorum slots forfeit their share (`50%` Burn + `50%` Treasury).
* **Cooperative Participants (60% — Equal `1/15` = `4.00%` per Solved Seat)**: Divided **equally across all 15 committee seats (`8 MN + 2 Coin-Lock + 3 PoW-Lock + 2 Free Entry`)** (`60% / 15 = 4.00%` per solved seat with mandatory 33-byte compressed pubkey, regardless of MN, Coin-Lock, or Open-PoW track). Unsolved/empty seats receive **0%**, and their unclaimed shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)**.
* **Block Solver / Winner (25% + `nFees` — Full Rescue Reward)**: Paid in full to the winning PoBLS Coordinator (elected via proportional `4,200 : 250 : 10` VRF with **`55%` Active MN floor** under Model D) who proposed the block. When a silent primary validator seat is substituted by a legitimate same-track standby validator at $t \ge 60\text{s}$ (`vAdamMiners[i] != vExpectedMiners[i]`), the standby validator is paid its equal `4.00%` seat share directly out of the 60% Cooperative Participants Pool (`nPartSplitTotal`) without deducting any penalty from the Coordinator's 25% reward (while the displaced primary DIP3 Masternode accrues `+5` PoSe under `M-4`). Primary validators remain protected against opportunistic substitution and omission by: (1) expiration of the primary Coordinator's exclusivity window at $t = 45\text{s}$ (aligned with `llmq::QUORUM_EQUIVOCATION_GUARD_SECS = 45`, before standby rotation opens at $t = 60\text{s}$), (2) deterministic same-track standby rank ordering (`vStandbyMiners`), and (3) LLMQ `qblockprop` anti-substitution vetoes (`t >= 60s`) plus R35-1 `5s` primary omission vetoes (`mapAdamSolutionFirstSeen >= 5s`) when a primary validator's valid solution is already cached locally.

#### B. Proof-of-Stake (PoS) Blocks
For blocks mined via PoS (dual staking + ADAM cooperative validation), the distributable block reward $R$ is split symmetrically as follows under Balanced Model D + `UPGRADE_FAIRNESS`:
* **Passive Masternode Reward (5%)**: Distributed to one active Masternode deterministically selected by VRF seed modulo (`GetModelDPassiveMasternodePayee`).
* **Signer-Only LLMQ Reward (10%, 1-Block Lag)**: Paid strictly to previous block's `prevBlock.vQuorumSig` signers (`10% / N`; missed shares split `50%` Burn + `50%` Treasury).
* **Cooperative Participants (60% — Equal `1/15` = `4.00%` per Solved Seat)**: Paid equally across solved ADAM committee seats (`!vAdamSolutions[i].empty()`, `60% / 15 = 4.00%`), with any unclaimed empty-seat shares split **50% to the Automatic Protocol Burn** (permanently deducted from `nMoneySupply` without crediting any personal PoB staking weight) and **50% to the Developer Treasury**.
* **PoS Staker (`22% + nFees`) & Signing Coordinator (`3%`)**: Splits the 25% producer share symmetrically between the Staker (`22%` + **`100%` of PoS transaction fees `nFees`**, Pre-Model D `20% + nFees`) and the elected Signing Coordinator (`3%`, Pre-Model D `5%`).

---

### 5. Miner Registration Period (`nRegPeriod`) Reduction
Under legacy consensus rules and on Mainnet, a cooperative miner's registration is valid for a ~24-hour duration to minimize transaction overhead:

$$\text{nRegPeriod}_{\text{Mainnet / Legacy}} = 2880 \text{ blocks}$$

On Testnet and Regtest, to prevent stale or abandoned miners from cluttering the active validator seat list during rapid testing and causing block generation delays, Model D reduces this registration validity period:

$$\text{nRegPeriod}_{\text{Model D (Testnet / Regtest)}} = 100 \text{ blocks}$$

Miners must regularly renew their registration on the blockchain by submitting a miner registration transaction.

## Backward Compatibility
* Model D rules are triggered cleanly at Block `200` (`UPGRADE_MODELD` + `UPGRADE_POMBL` + `UPGRADE_ADAM_V2` + `UPGRADE_STAKE_MIN_DEPTH_V2` all activate together at block `200` on Mainnet and Testnet; Regtest activates `UPGRADE_MODELD` at `200` and `UPGRADE_POMBL` at `300`; `UPGRADE_HARDENING_V4` activates at height `1` across all networks — immediately enforcing Model D ADAM threshold `8` once `nActiveMasternodes >= 8`, falling back to `5` only while `< 8`, or `3` on Testnet).
* Dynamic `2/3` BFT LLMQ `vQuorumSig` verification (`4-of-5` up to `11-of-15` via `floor(2N/3)+1` under `UPGRADE_HARDENING_V4`; pre-V4 `3-of-5` up to `8-of-15`) and Model D reward splits are bypassed for blocks below `UPGRADE_MODELD`, preserving bootstrap behavior.

## Reference Implementation
* Upgrade activation flags and heights: `src/chainparams.cpp`.
* Quorum signature threshold logic: `Verify` in `src/llmq.cpp`.
* DKG session scheduling: `GetActiveQuorum` in `src/llmq.cpp`.
* Reward splitting logic: `FillBlockPayee`, `IsBlockPayeeValid`, and `ComputeModelDValidatorSeatPayments` in `src/masternode-payments.cpp`, `GetMasternodePayment` in `src/masternode.cpp`, and `src/miner.cpp`.
* Miner registration expiration check: `GetAdamMinerPool` in `src/adam.cpp`.
