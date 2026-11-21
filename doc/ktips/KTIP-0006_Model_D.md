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
During the bootstrap phase (prior to block 5,000 on Mainnet), the KRISTA network must build a robust pool of masternodes and secure validator collaterals without risking network deadlocks due to non-existent or immature quorums. 

Model D addresses this by implementing a phased maturation model:
1. **Bootstrap Phase**: Passive Masternode (`35%`) and LLMQ (`10%`) payments are set to `0%` prior to Model D to incentivize stakers and miners to accumulate the `4,200 KRISTA` collateral and set up masternode daemons (`100%` Miner on Classic PoW blocks `2..199`, transitioning at Block `200` under `IsAdamValidatorSplitActive` + `UPGRADE_FAIRNESS` to **`25%` Block Producer [`20%` Staker + `5%` Signing Coordinator in PoS] + `75%` Equal Active ADAM Validators** [`~6.818%`/seat] with `50/50` unclaimed Burn/Treasury). During this phase, quorum signature verification thresholds are bypassed ($T = 0$) to allow block production to proceed.
2. **Maturation Phase (Model D)**: Once a critical mass of active masternodes is established, Model D activates advanced LLMQ topologies, enforces strict signature verification thresholds, and sets the final split to **`35%` Passive MN + `10%` Signer-Only LLMQ (1-block lag) + `15%` Producer (`12%` Staker + `3%` Coordinator in PoS) + `40%` Equal Active Validators (`40% / 11 ≈ 3.636%`/seat)**.

## Specification

### 1. Upgrade Activation Timing
The Model D upgrade (`Consensus::UPGRADE_MODELD`) is activated at different block heights depending on the network type:

| Network | Activation Height | Status |
| --- | --- | --- |
| **Mainnet** | Block 5,000 | Active |
| **Testnet** | Block 500 | Active |
| **Regtest** | Block 200 | Active |

---

### 2. Long-Living Masternode Quorums (LLMQs) & DKG Sessions
Under Model D, the network organizes active, registered masternodes into Long-Living Masternode Quorums (LLMQs) using a simulated Distributed Key Generation (DKG) session.

* **Quorum Size ($N$)**: Dynamic BFT sizing (`UPGRADE_HARDENING_V1` / `GetTargetQuorumSize` in `src/llmq.cpp`) scales with verified active on-chain Masternodes (`<25` $\rightarrow$ `5`, `25–49` $\rightarrow$ `9`, `50–99` $\rightarrow$ `11`, `>=100` $\rightarrow$ `15`). When `< N` Active Masternodes exist, deficit seats are filled with quota-capped standby candidates (`max 1 Coin-Lock + max 3 Funded PoW-Locks`).
* **DKG Rotation Interval (`GetActiveQuorum`)**:
  * **Mainnet**: Prior to block 1,400, the active quorum rotation height is anchored every **100 blocks** (clamped to at least `UPGRADE_POMBL`), and from block 1,400 onwards a fresh quorum is elected **every block**:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 100 \rfloor \times 100,\; H_{\text{PoMBL}}\right) & \text{if } H < 1400 \\
    H & \text{if } H \ge 1400
    \end{cases}
    $$

  * **Testnet / Regtest**: Prior to block 1,000 (Testnet) / block 800 (Regtest), the active quorum rotation height is anchored every **10 blocks** (clamped to at least `UPGRADE_POMBL`), and **every block** thereafter:

    $$
    H_{\text{DKG}} = \begin{cases}
    \max\left(\lfloor H / 10 \rfloor \times 10,\; H_{\text{PoMBL}}\right) & \text{if } H < H_{\text{rot}} \\
    H & \text{if } H \ge H_{\text{rot}}
    \end{cases}
    $$
  
  Where $H$ is the current block height (`nHeight`), $H_{\text{PoMBL}}$ is `UPGRADE_POMBL` (`4800` on Mainnet, `400` on Testnet, `300` on Regtest), $H_{\text{rot}} = 1000$ on Testnet ($800$ on Regtest), and $H_{\text{DKG}}$ is the rotation height for which the active quorum is elected.

* **Quorum Election**: Elected members are determined deterministically by sorting all active masternodes using a score derived from the rolling entropy seed of the preceding block and the masternode's collateral outpoint. When fewer than $N$ verified Active Masternodes exist on-chain (`nVerifiedOnChainMNs < N`), all available real Active Masternodes are strictly guaranteed inclusion first, preserving genuine Masternode operator quorum seats. The remaining deficit slots to complete the $N$-member quorum are filled from fallback candidate pools (top Coin-Lock validators, followed by top PoW-Lock validators), with each candidate strictly quota-capped (max 1 Coin-Lock + max 3 Funded PoW-Locks) to maintain Byzantine isolation and prevent any single entity or multi-lock cluster from capturing LLMQ signing.

---

### 3. Quorum Signature Verification Thresholds
Blocks of Version 12 and above contain a quorum signature payload `vQuorumSig` validating the block header hash. The verification threshold ($T$) in `CQuorumSignature::Verify()` (`src/llmq.cpp`) is calculated as follows:

* **Prior to Model D Activation (`!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)` on Mainnet / Testnet)**:
  Quorum signature verification is bypassed ($T = 0$) to allow bootstrapping before active LLMQ quorums are required.
* **Once Model D is Active (All Networks: Mainnet, Testnet, Regtest)**:
  The threshold is set to a strict dynamic BFT majority ($\lfloor N / 2 \rfloor + 1$):
  
  $$T = \lfloor N / 2 \rfloor + 1 \in \{3, 5, 6, 8\}$$
  
  Thus, a strict BFT majority (`3-of-5`, `5-of-9`, `6-of-11`, or `8-of-15` matching `GetTargetQuorumSize`) of quorum members must sign the block hash across all networks once Model D is active.

---

### 4. Economic Model & Block Reward Splits
Model D enforces the final, sustainable economic split of the block reward. The split structure changes based on the block type:

#### A. Proof-of-Work (PoW) Blocks
For blocks mined via PoW (ADAM cooperative mining), the distributable block reward $R$ (after deducting the 7.0% Developer Treasury) is split as follows under `UPGRADE_FAIRNESS` (Height `1`):
* **Passive Masternode Reward (35%)**: Distributed to one active Masternode deterministically selected by VRF seed modulo (`seed.GetUint64(0) % vActiveMns.size()` in `GetModelDPassiveMasternodePayee`).
* **Signer-Only LLMQ Reward (10%, 1-Block Lag)**: Divided across the $N \in \{5, 9, 11, 15\}$ quorum slots (`10% / N`) and paid strictly with a **1-block lag** to the active LLMQ members who signed the **previous block** (`prevBlock.vQuorumSig`, with `-5/+5` deterministic PoSe scoring); unsigned quorum slots forfeit their share (`50%` Burn + `50%` Treasury).
* **Cooperative Participants (40% — Equal `1/11` per Solved Seat)**: Divided **equally across all 11 committee seats** (`40% / 11 ≈ 3.636%` per solved seat with mandatory 33-byte compressed pubkey, regardless of MN, Coin-Lock, or Open-PoW track; integer truncation remainder paid only when all `11/11` seats solve). Unsolved/empty seats receive **0%**, and their unclaimed shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)**.
* **Block Solver / Winner (15% + `nFees` — Full Rescue Reward)**: Paid in full to the winning PoBLS Coordinator who proposed the block. When a silent primary validator seat is substituted by a legitimate same-track standby validator at $t \ge 60\text{s}$ (`vAdamMiners[i] != vExpectedMiners[i]`), the standby validator is paid its equal seat share directly out of the 40% Cooperative Participants Pool (`nPartSplitTotal`) without deducting any penalty from the Coordinator's 15% reward. Primary validators remain protected against opportunistic substitution by: (1) expiration of the primary Coordinator's exclusivity window at $t = 45\text{s}$ (before standby rotation opens at $t = 60\text{s}$), (2) deterministic same-track standby rank ordering (`vStandbyMiners`), and (3) LLMQ `qblockprop` anti-substitution vetoes when a primary validator's valid solution is already cached locally.

#### B. Proof-of-Stake (PoS) Blocks
For blocks mined via PoS (dual staking + ADAM cooperative validation), the distributable block reward $R$ is split symmetrically as follows under `UPGRADE_FAIRNESS`:
* **Passive Masternode Reward (35%)**: Distributed to one active Masternode deterministically selected by VRF seed modulo (`GetModelDPassiveMasternodePayee`).
* **Signer-Only LLMQ Reward (10%, 1-Block Lag)**: Paid strictly to previous block's `prevBlock.vQuorumSig` signers (`10% / N`; missed shares split `50%` Burn + `50%` Treasury).
* **Cooperative Participants (40% — Equal `1/11` per Solved Seat)**: Paid equally across solved ADAM committee seats (`!vAdamSolutions[i].empty()`, `40% / 11 ≈ 3.636%`), with any unclaimed empty-seat shares split **50% to the Automatic Protocol Burn** (permanently deducted from `nMoneySupply` without crediting any personal PoB staking weight) and **50% to the Developer Treasury**.
* **PoS Staker (`12% + nFees`) & Signing Coordinator (`3%`)**: Splits the 15% producer share symmetrically between the Staker (`12%` + **`100%` of PoS transaction fees `nFees`**, Pre-Model D `20% + nFees`) and the elected Signing Coordinator (`3%`, Pre-Model D `5%`).

---

### 5. Miner Registration Period (`nRegPeriod`) Reduction
Under legacy consensus rules and on Mainnet, a cooperative miner's registration is valid for a ~24-hour duration to minimize transaction overhead:

$$\text{nRegPeriod}_{\text{Mainnet / Legacy}} = 2880 \text{ blocks}$$

On Testnet and Regtest, to prevent stale or abandoned miners from cluttering the active validator seat list during rapid testing and causing block generation delays, Model D reduces this registration validity period:

$$\text{nRegPeriod}_{\text{Model D (Testnet / Regtest)}} = 100 \text{ blocks}$$

Miners must regularly renew their registration on the blockchain by submitting a miner registration transaction.

## Backward Compatibility
* Model D rules are triggered dynamically when block height reaches `UPGRADE_MODELD` (while Version 12 block header serialization activates at `UPGRADE_POMBL`: block `4800` on Mainnet, `400` on Testnet, `300` on Regtest).
* Dynamic BFT LLMQ `vQuorumSig` verification (`3-of-5` up to `8-of-15`) and Model D reward splits are bypassed for blocks below `UPGRADE_MODELD`, preserving bootstrap behavior.

## Reference Implementation
* Upgrade activation flags and heights: `src/chainparams.cpp`.
* Quorum signature threshold logic: `Verify` in `src/llmq.cpp`.
* DKG session scheduling: `GetActiveQuorum` in `src/llmq.cpp`.
* Reward splitting logic: `FillBlockPayee`, `IsBlockPayeeValid`, and `ComputeModelDValidatorSeatPayments` in `src/masternode-payments.cpp`, `GetMasternodePayment` in `src/masternode.cpp`, and `src/miner.cpp`.
* Miner registration expiration check: `GetAdamMinerPool` in `src/adam.cpp`.
