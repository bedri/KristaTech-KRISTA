# MPA (Multi-Proof Algorithm) Consensus Specification

## 1. Introduction and Background

The **Multi-Proof-Algorithm (MPA)** is a hybrid consensus mechanism implemented on top of the ADAM cooperative consensus framework. It combines Proof-of-Stake (PoS) with specialized cryptographic proof mechanisms to incentivize node operators, lock-up liquidity, and ensure secure block production.

MPA introduces four co-existing staking proof types:
1. **Proof of Stake (PoS - Baseline)**: Standard coin age-based block production.
2. **Proof of Lock (PoL)**: Staking using output scripts containing CLTV or CSV timelocks, awarding extra mining weight for longer lock durations.
3. **Proof of Burn (PoB)**: Burning coins to a designated unspendable address, providing a mining weight multiplier that decays linearly over time.
4. **Proof of Masternode (PoM)**: Allocating mining weight based on active Masternode collateral and its continuous uptime.

These weight metrics directly scale the difficulty targets during block validation, creating a multi-tiered consensus topology where resource commitment translates directly to consensus power.

---

## 2. Consensus Architecture & Parameters

MPA is activated at a specific block height `nPoMBLHeight`. Its core configuration resides in `src/consensus/params.h` and is defined per network in `src/chainparams.cpp`.

### Core Parameters
* **`nPoMBLHeight`**: The block height at which MPA consensus rules activate. PoMBL stands for **Proof of Masternode, Burn and Lock**. Below this height, the network operates under legacy or version 11 ADAM rules.
* **`nPoMBLTargetSpacing`**: Spacing target for block production (configured to 30 seconds).
* **`mBurnAddresses`**: A map containing registered unspendable burn addresses and their active starting heights.

### Network Activation Heights
| Network | `nPoMBLHeight` (UPGRADE_POMBL) | Enforced Block Version |
| :--- | :--- | :--- |
| **Mainnet** | 2000 | Version 12 |
| **Testnet** | 400 | Version 12 |
| **Regtest** | 300 | Version 12 |

---

## 3. Mining Kernel Weight Calculations

The probability of mining the next block under the hybrid MPA system is governed by the kernel evaluation equation:

$$\text{KernelHash} < \text{Target} \times \text{Weight}_{\text{Total}}$$

For a given transaction output (UTXO) or Masternode collateral, its mining $\text{Weight}_{\text{Total}}$ is calculated using the following rules implemented in `CalculateMPAWeight()` in `src/kernel.cpp`:

### 3.1. Proof of Stake (PoS - Baseline)

$$W_{\text{PoS}} = \text{Amount}$$

### 3.2. Proof of Lock (PoL)
Applicable to transaction outputs locked using absolute (`OP_CHECKLOCKTIMEVERIFY`) or relative (`OP_CHECKSEQUENCEVERIFY`) timelocks of duration $L$ blocks up to $T_{\text{MAX}}$:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \frac{L}{T_{\text{MAX}}}\right)$$

* $\gamma$ is the lock multiplier parameter (default: `2.0`, giving up to a 3x weight bonus).
* $T_{\text{MAX}}$ is the maximum lock duration evaluated (default: `1,000,000` blocks).

### 3.3. Proof of Burn (PoB)
Coins sent to a registered unspendable burn address (e.g. `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4`) receive a substantial weight multiplier that decays linearly to zero over time $T$ (blocks elapsed since the burn block):

$$W_{\text{PoB}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$

* $\beta$ is the burn incentive multiplier (default: `5.0`).
* $T_{\text{MAX}}$ is the decay threshold (default: `500,000` blocks). Once $T \ge T_{\text{MAX}}$, the mining weight becomes 0.

### 3.4. Proof of Masternode (PoM)
Active, enabled Masternodes with collateral $C$ and active lifetime $t_{\text{active}}$ (blocks elapsed since transitioning to `ENABLED` status):

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

* $\alpha$ is the masternode lifetime multiplier (default: `1.0`, yielding up to 2x weight).
* $T_{\text{MAX}}$ is the maximum lifetime maturity (100,000 blocks on Mainnet, 10,000 blocks on Testnet/Regtest).
* If a Masternode falls out of `ENABLED` status (due to a restart, ping timeout, or config change), $t_{\text{active}}$ immediately resets to 0.

---

## 4. Long-Living Masternode Quorums (LLMQs)

To support secure leader election and signature aggregation, LLMQs are implemented using individual **secp256k1** signatures rather than BLS threshold signatures. Quorum members sign the block hash using their private secp256k1 keys, and these signatures are verified individually. Note that the network natively integrates a real **BLS12-381** library (`blst`) for VRF rolling seeds, PoBLS ephemeral ticket generation, and coordinator signatures.

### Quorum Election & Size
* **Quorum Size**: Exactly **5 members**.
* **DKG Interval**: DKG sessions run every **100 blocks** on Mainnet, and every **10 blocks** on Testnet/Regtest.
* **Active Masternode Filtering**: No hardcoded key ID filtering or local wallet restrictions are applied on any network (Mainnet, Testnet, or Regtest). The election dynamically draws from all active and enabled Masternodes on the network.
* **Deterministic Fallback**: If fewer than 5 active masternodes are available, the DKG session manager falls back to electing quorum members from the registered miner pool (`GetAdamMinerPool(nHeight - 1)`). On Regtest, the registered miner pool is pre-populated with **15 deterministic keys** (derived from the index 0 to 14) to facilitate local testing, while on other networks it consists of registered miners (via Coin-Lock or PoW-Lock) and bootstrap miners.

### Signature and Threshold Validation
* Quorum members sign the block hash using their private secp256k1 keys.
* The quorum signature `vQuorumSig` is populated in block headers once version is `>= 12` (when Standard Mode is active, which is controlled by `SPORK_21_ADAM_STANDARD_MODE` and active once block height $\ge$ `nPoMBLHeight`). Note that Model D (`UPGRADE_MODELD`) is a separate upgrade height that activates later (height 2200 on Mainnet, 500 on Testnet, and 200 on Regtest).
* **Quorum Validation Threshold**:
  - **Mainnet**: The signature verification threshold is **75%** of the quorum size (`quorum.members.size() * 3 / 4`), with a minimum of 2 signatures (capped to the actual quorum size if it is smaller). For a standard quorum of 5 members, the verification threshold is 3 signatures. During block generation (`miner.cpp`), a simple majority + 1 threshold (`quorum.members.size() / 2 + 1`) is used.
  - **Testnet**: When Model D is active (height $\ge 500$), the threshold is exactly **2 signatures** to ensure liveness in small setups. Before Model D activation (heights 400 to 499), the threshold is **0 signatures** (verification is bypassed to allow bootstrapping).
  - **Regtest**: Since LLMQ activates at height 300 (`UPGRADE_POMBL`) and Model D activates at height 200 (`UPGRADE_MODELD`), Model D is already active when quorums start running. Therefore, the threshold on Regtest is always exactly **2 signatures**.

> [!NOTE]
> **Quorum Threshold Distinction:**
> Do not confuse the **ADAM Cooperative Mining Threshold** (7 out of 11 rule for puzzle solutions `vAdamSolutions`) with the **LLMQ Signature Verification Threshold** (3 out of 5 rule on Mainnet for block signature `vQuorumSig`):
> * **ADAM Threshold**: Enforces that at least 7 out of 11 elected miners (`nAdamThreshold = 7` on Mainnet/Regtest, `3` on Testnet) must solve and submit their PoW puzzle solutions (`vAdamSolutions`) for the block to be accepted. This is part of the ADAM cooperative puzzle validation.
> * **LLMQ Threshold**: Enforces that at least 3 out of 5 LLMQ members must sign the proposed block hash using their secp256k1 private keys (`vQuorumSig`). This is part of the decentralized block validation.

---

## 5. Consensus Enforcements & Validation Rules

When a block is received, the validation rules in `CheckBlock()` in `src/main.cpp` enforce the following checks when height $\ge \text{nPoMBLHeight}$:

1. **Version Enforcement**: The block version must be at least `12`. Block headers of version < 12 are strictly rejected with a `bad-version` code.
2. **Burn Output Integrity**: All transaction inputs spending from a registered burn address are strictly rejected at the mempool layer (`AcceptToMemoryPool`) and block connection layer (`ConnectBlock`).
3. **Locktime Script Enforcement**: Locked transaction outputs used for PoL must strictly conform to locked scripts and cannot be spent until their specified lock height/timestamp has passed.
4. **Quorum Vote Validation**: The quorum signature (`vQuorumSig`) included in the block header is validated against the active LLMQ public key.

---

## 6. Testing and Verification

The MPA consensus changes are fully covered by a dual test suite:

### 6.1. Unit Tests (`test_kristatech`)
The C++ unit tests in `src/test/mpa_tests.cpp` verify:
* Weight calculations for standard PoS.
* Correct decay parameters and decay calculations for Proof of Burn (PoB).
* Height and duration calculations for Proof of Lock (PoL) transaction inputs.

### 6.2. Functional Tests (`consensus_pombl.py`)
The Python functional test suite `test/functional/consensus_pombl.py` verifies the rules on a local Regtest network:
* Simulates block generation up to the activation height.
* Confirms block version < 12 rejection at the activation boundary.
* Confirms block version 12 acceptance.
* Verifies that sending funds to a burn address is accepted.
* Asserts that attempts to spend from a burn address are blocked and rejected with `bad-txns-invalid-outputs`.
