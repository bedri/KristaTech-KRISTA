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

To support secure leader election and signature aggregation without adding a heavy external BLS12-381 library dependency, MPA simulates **Long-Living Masternode Quorums (LLMQs)** using the existing **secp256k1** elliptic curve cryptography.

### Quorum Election & Size
* **Quorum Size**: Exactly **5 members**.
* **DKG Interval**: DKG sessions run every **100 blocks** on Mainnet, and every **10 blocks** on Testnet/Regtest.
* **Active Masternode Filtering**: On Testnet and Regtest, the candidates are filtered to **12 local key IDs** (`node1` through `node12`) to isolate local network sandboxes.
* **Deterministic Fallback**: If fewer than 5 active masternodes are available, the DKG session manager falls back to electing members from the registered miner pool (similarly filtered to the 12 local key IDs on Testnet/Regtest, with a final fallback to the unfiltered miner pool if still fewer than 5).

### Signature and Threshold Validation
* Quorum members sign the block hash using their private secp256k1 keys.
* The quorum signature `vQuorumSig` is populated in block headers once version is `>= 12` (when Standard Mode / Model D is active).
* **Quorum Validation Threshold**:
  - **Mainnet**: Threshold is **75%** of the quorum size (at least 3 signatures out of 5 must be valid).
  - **Testnet/Regtest**: When Model D is active (height $\ge 500$ on Testnet, $\ge 200$ on Regtest), the threshold is exactly **2 signatures** to ensure liveness in small setups. Before Model D activation, the threshold is **0 signatures** (verification is bypassed).

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
