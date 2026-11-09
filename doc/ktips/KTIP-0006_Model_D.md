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
During the bootstrap phase (prior to block 2,200 on Mainnet), the KRISTA network must build a robust pool of masternodes and secure validator collaterals without risking network deadlocks due to non-existent or immature quorums. 

Model D addresses this by implementing a phased maturation model:
1. **Bootstrap Phase**: The masternode payment is set to 0% to incentivize stakers to compile the 4,200 KRISTA collateral and set up masternode daemons. During this phase, quorum signature verification thresholds are bypassed ($T = 0$) to allow block production to proceed.
2. **Maturation Phase (Model D)**: Once a critical mass of active masternodes is established, Model D is activated via a hard fork. This introduces advanced LLMQ topologies, enforces strict signature verification thresholds, and begins the final sustainable block reward splits.

## Specification

### 1. Upgrade Activation Timing
The Model D upgrade (`Consensus::UPGRADE_MODELD`) is activated at different block heights depending on the network type:

| Network | Activation Height | Status |
| --- | --- | --- |
| **Mainnet** | Block 2,200 | Active |
| **Testnet** | Block 500 | Active |
| **Regtest** | Block 200 | Active |

---

### 2. Long-Living Masternode Quorums (LLMQs) & DKG Sessions
Under Model D, the network organizes active, registered masternodes into Long-Living Masternode Quorums (LLMQs) using a simulated Distributed Key Generation (DKG) session.

* **Quorum Size ($N$)**: The quorum size is fixed to exactly **5 members** to keep resource requirements minimal.
* **DKG Rotation Interval**:
  * **Mainnet**: A new DKG session is run every **100 blocks** prior to block 1,400, and **every block (`nInterval = 1`)** from block 1,400 onwards:
  
    $$H_{\text{DKG}} = \begin{cases} \lfloor H / 100 \rfloor \times 100 & \text{if } H < 1400 \\ H & \text{if } H \ge 1400 \end{cases}$$
  
  * **Testnet / Regtest**: A new DKG session is run every **10 blocks** prior to block 1,000 (Testnet) / block 800 (Regtest), and **every block (`nInterval = 1`)** thereafter:
  
    $$H_{\text{DKG}} = \begin{cases} \lfloor H / 10 \rfloor \times 10 & \text{if } H < H_{\text{rot}} \\ H & \text{if } H \ge H_{\text{rot}} \end{cases}$$
  
  Where $H$ is the preceding block height (`pindexPrev->nHeight`), $H_{\text{rot}} = 1000$ on Testnet ($800$ on Regtest), and $H_{\text{DKG}}$ is the height of the block at which the current active quorum was elected.

* **Quorum Election**: Elected members are determined deterministically by sorting all active masternodes using a score derived from the rolling entropy seed of the preceding block and the masternode's collateral outpoint.

---

### 3. Quorum Signature Verification Thresholds
Blocks of Version 12 and above contain a quorum signature payload `vQuorumSig` validating the block header hash. The verification threshold ($T$) in `CQuorumSignature::Verify()` (`src/llmq.cpp`) is calculated as follows:

* **Prior to Model D Activation (`!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)` on Mainnet / Testnet)**:
  Quorum signature verification is bypassed ($T = 0$) to allow bootstrapping before active LLMQ quorums are required.
* **Once Model D is Active (All Networks: Mainnet, Testnet, Regtest)**:
  The threshold is set to a strict BFT majority ($\lfloor N / 2 \rfloor + 1$):
  
  $$T = \lfloor N / 2 \rfloor + 1 = 3$$
  
  Thus, at least **3 out of 5** quorum members must sign the block hash across all networks once Model D is active.

---

### 4. Economic Model & Block Reward Splits
Model D enforces the final, sustainable economic split of the block reward. The split structure changes based on the block type:

#### A. Proof-of-Work (PoW) Blocks
For blocks mined via PoW (ADAM cooperative mining), the distributable block reward $R$ (after deducting the 7.0% Developer Treasury and, for blocks 2–50,000, the 0.7% Bootstrap Faucet) is split as follows:
* **Passive Masternode Reward (35%)**: Distributed to the masternode at the top of the deterministic payment queue.
* **Active LLMQ Reward (10%)**: Distributed equally to the 5 active members of the current LLMQ quorum who participated in block validation.
* **Cooperative Participants (40% — 20% across 7 MN Validator Seats + 20% across 4 Independent Miner Seats)**: Partitioned into **20% across the 7 Masternode Validator quota seats** (`~2.857%` per seat) and **20% across the 4 Independent Miner quota seats (`1 Coin-Lock + 3 Open PoW`, `5.00%` per seat)** for all committee seats that submit a valid PoW solution (`!vAdamSolutions[i].empty()`). Unsolved/empty seats receive **0%**, and their unclaimed shares (including any unclaimed independent-miner bonus when a Masternode spills over into an empty miner seat) are routed to the **Developer Treasury** (`nExpectedTreasury = nTreasurySplit + nUnclaimedPartSplit`).
* **Block Solver / Winner (15% — Minus Standby Substitution Penalty)**: Paid to the winning PoBLS Coordinator who proposed the block. For each primary seat substituted by a standby validator at $t \ge 60\text{s}$ (`vAdamMiners[i] != vExpectedMiners[i]`), an equal penalty (`nCoordSubstitutionPenalty`, matching the reward of the substituted seat) is deducted from the Coordinator's 15% reward and routed to the **Developer Treasury**, ensuring **zero** net financial gain for substituting an honest primary validator with the coordinator's own standby key. In addition, during `qblockprop` validation at $t \ge 60\text{s}$ (when the LLMQ member's tip matches `hashPrevBlock`), LLMQ members veto proposals that substitute a primary seat whose valid solution is already cached locally.

#### B. Proof-of-Stake (PoS) Blocks
For blocks mined via PoS (dual staking + ADAM cooperative validation), the distributable block reward $R$ is split as follows:
* **Passive Masternode Reward (35%)**: Distributed to the masternode at the top of the payment queue.
* **Active LLMQ Reward (10%)**: Distributed equally to the 5 active members of the current LLMQ quorum.
* **Cooperative Participants (40% — 20% across 7 MN Validator Seats + 20% across 4 Independent Miner Seats)**: Paid per solved non-staker ADAM committee seat (`!vAdamSolutions[i].empty()`), with any unclaimed empty-seat shares routed to the **Developer Treasury**.
* **Staking Wallet / Winner (15%)**: Paid directly to the staker who generated the coinstake transaction.

---

### 5. Miner Registration Period (`nRegPeriod`) Reduction
Under legacy consensus rules and on Mainnet, a cooperative miner's registration is valid for a ~24-hour duration to minimize transaction overhead:

$$\text{nRegPeriod}_{\text{Mainnet / Legacy}} = 2880 \text{ blocks}$$

On Testnet and Regtest, to prevent stale or abandoned miners from cluttering the active validator seat list during rapid testing and causing block generation delays, Model D reduces this registration validity period:

$$\text{nRegPeriod}_{\text{Model D (Testnet / Regtest)}} = 100 \text{ blocks}$$

Miners must regularly renew their registration on the blockchain by submitting a miner registration transaction.

## Backward Compatibility
* Model D rules are triggered dynamically when block height reaches `UPGRADE_MODELD`.
* Version 12 block structures and quorum signature checks are ignored for blocks below the activation height, preserving the original bootstrap behavior.

## Reference Implementation
* Upgrade activation flags and heights: `src/chainparams.cpp`.
* Quorum signature threshold logic: `Verify` in `src/llmq.cpp`.
* DKG session scheduling: `GetActiveQuorum` in `src/llmq.cpp`.
* Reward splitting logic: `GetMasternodePayment` in `src/masternode.cpp` and miner/staker reward functions in `src/miner.cpp`.
* Miner registration expiration check: `GetAdamMinerPool` in `src/adam.cpp`.
