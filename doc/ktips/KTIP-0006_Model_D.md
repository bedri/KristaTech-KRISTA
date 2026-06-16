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
1. **Bootstrap Phase**: The masternode payment is set to 0% to incentivize stakers to compile the 2,100 KRISTA collateral and set up masternode daemons. During this phase, quorum signature verification thresholds are bypassed ($T = 0$) to allow block production to proceed.
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
  * **Mainnet**: A new DKG session is run every **100 blocks**.
  
    $$H_{\text{DKG}} = \lfloor H / 100 \rfloor \times 100$$
  
  * **Testnet / Regtest**: A new DKG session is run every **10 blocks**.
  
    $$H_{\text{DKG}} = \lfloor H / 10 \rfloor \times 10$$
  
  Where $H$ is the current block height, and $H_{\text{DKG}}$ is the height of the block at which the current active quorum was elected.

* **Quorum Election**: Elected members are determined deterministically by sorting all active masternodes using a score derived from the rolling entropy seed of the preceding block and the masternode's collateral outpoint.

---

### 3. Quorum Signature Verification Thresholds
Blocks of Version 12 and above contain a quorum signature payload `vQuorumSig` validating the block header hash. The verification threshold ($T$) for the quorum signature verification is calculated as follows:

* **Mainnet**:
  The threshold is set to 75% of the quorum size (rounded down), with a minimum of 2:
  
  $$T_{\text{Mainnet}} = \max\left(2, \lfloor N \times 3 / 4 \rfloor\right) = 3$$
  
  Thus, at least **3 out of 5** quorum members must sign the block hash.

* **Testnet / Regtest**:
  * Prior to Model D activation:
    
    $$T_{\text{Testnet}} = 0$$
    
    Quorum signature verification is bypassed to allow bootstrapping.
  * Once Model D is active:
    
    $$T_{\text{Testnet}} = 2$$
    
    At least **2 out of 5** quorum members must sign the block hash to maintain network liveness in small development and testing environments.

---

### 4. Economic Model & Block Reward Splits
Model D enforces the final, sustainable economic split of the block reward. The split structure changes based on the block type:

#### A. Proof-of-Work (PoW) Blocks
For blocks mined via PoW (ADAM cooperative mining), the block reward $R$ is split as follows:
* **Passive Masternode Reward (50%)**: Distributed to the masternode at the top of the payment queue.
* **Active LLMQ Reward (10%)**: Distributed equally to the 5 active members of the current LLMQ quorum who participated in block validation.
* **Cooperative Participants (25%)**: Distributed to the miners who submitted valid partial PoW solutions for the block under the ADAM model.
* **Block Solver / Winner (15%)**: Paid directly to the miner who discovered the winning block solution.

#### B. Proof-of-Stake (PoS) Blocks
For blocks mined via PoS (standard staking), the block reward $R$ is split as follows:
* **Passive Masternode Reward (50%)**: Distributed to the masternode at the top of the payment queue.
* **Active LLMQ Reward (10%)**: Distributed equally to the 5 active members of the current LLMQ quorum.
* **Staking Wallet / Winner (40%)**: Paid directly to the staker who generated the coinstake transaction.

---

### 5. Miner Registration Period (`nRegPeriod`) Reduction
Under legacy consensus rules, a cooperative miner's registration was valid for a long duration to minimize transaction overhead during the early boot phase:

$$\text{nRegPeriod}_{\text{Legacy}} = 2880 \text{ blocks}$$

To prevent stale or abandoned miners from cluttering the active validator seat list and causing block generation delays or failures, Model D reduces this registration validity period:

$$\text{nRegPeriod}_{\text{Model D}} = 100 \text{ blocks}$$

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
