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

MPA is activated via the `Consensus::UPGRADE_POMBL` network upgrade (`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`). Its core configuration resides in `src/consensus/params.h` and is defined per network in `src/chainparams.cpp`.

### Core Parameters
* **`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`**: The block height at which MPA consensus rules activate (`PoMBL` stands for **Proof of Masternode, Burn and Lock**). Below this height, the network operates under legacy or Version 11 ADAM rules.
* **`nTargetSpacing`**: Spacing target for block production (`30` seconds on Mainnet and Testnet, `10` seconds on Regtest).
* **`mBurnAddresses`**: A map containing registered unspendable burn addresses and their active starting heights.

### Network Activation Heights
| Network | `UPGRADE_POMBL` Activation Height | Enforced Block Version |
| :--- | :--- | :--- |
| **Mainnet** | 4800 | Version 12 |
| **Testnet** | 400 | Version 12 |
| **Regtest** | 300 | Version 12 |

---

## 3. Mining Kernel Weight Calculations

The probability of mining the next block under the hybrid MPA system is governed by the kernel evaluation equation (`CStakeKernel::CheckKernelHash` in `src/kernel.cpp`):

$$\text{KernelHash} < \text{Target} \times \lfloor \text{Weight}_{\text{Total}} / 100 \rfloor$$

For a given transaction output (UTXO) or Masternode collateral, its mining $\text{Weight}_{\text{Total}}$ is calculated in `CalculateMPAWeight()` in `src/kernel.cpp` using the exclusive priority order **1. PoM $\rightarrow$ 2. PoL $\rightarrow$ 3. PoB $\rightarrow$ 4. PoS**:

### 3.1. Proof of Stake (PoS - Baseline)

$$W_{\text{PoS}} = \text{Amount}$$

### 3.2. Proof of Lock (PoL) — Live Self-Renewing (`UPGRADE_FAIRNESS`)
Applicable to transaction outputs locked using block-height absolute (`OP_CHECKLOCKTIMEVERIFY`) or relative (`OP_CHECKSEQUENCEVERIFY`) timelocks of remaining/sequence duration $L$ blocks up to $L_{\text{MAX}}$:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

* $\gamma$ is the lock multiplier parameter (default: `2.0`, giving up to a 3x weight bonus).
* $L_{\text{MAX}}$ is the maximum lock duration evaluated (default: `50,000` blocks).
* **Live Lock-Preserving Self-Renewal (`UPGRADE_FAIRNESS`, Height 1)**: Canonical block-height CLTV `TX_CLTV_TIMELOCK` outputs (`<lockTime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <20B pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`, `0 < lockTime < 500,000,000`) are selectable by `CWallet::AvailableCoins(STAKEABLE_COINS)` and can stake live (`IsValidPoLSelfRenewal`) without waiting for unlock as long as the coinstake tx spends strictly `1` input (`vin.size() == 1`), keeps `vout[1].scriptPubKey == prevout.scriptPubKey` (preserving the exact lock height and owner key hash, zero stake-splitting), and keeps `vout[1].nValue >= prevout.nValue` (100% of principal + newly minted staker rewards remain locked until lock expiry). Unexpired `TX_CLTV_TIMELOCK` UTXOs remain unspendable in regular non-coinstake transactions until block height reaches `lockTime`.

### 3.3. Proof of Burn (PoB)
Coins voluntarily sent in standard non-coinbase / non-coinstake transactions (`!tx.IsCoinBase() && !tx.IsCoinStake()`) to a registered unspendable burn address (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` / `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95` on Mainnet, `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4` / `ktBurnKmNRzVXVVVDXxFhhKytkH5x2yqsCG` on Testnet) receive a substantial personal PoB staking weight multiplier in `mapAddressBurns` that decays linearly to zero over time $T$ (blocks elapsed since the burn block). Automatic protocol burns in `CoinBase` or `CoinStake` outputs (unclaimed Model D shares) permanently deduct from `nMoneySupply` as pure supply deflation and do **not** grant personal PoB staking weight:

$$W_{\text{PoB}} = \text{Amount} + \min\left(\text{BurnWeight}_{\text{active}}, \text{Amount} \times \beta\right)$$

where $\text{BurnWeight}_{\text{active}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$.

* $\beta$ is the burn incentive multiplier (default: `5.0`). Capping the per-UTXO burn bonus at $\text{Amount} \times \beta$ ensures 100% UTXO-splitting invariance, preventing an attacker from dividing coins into thousands of tiny UTXOs to artificially multiply address-level burn weight.
* $T_{\text{MAX}}$ (`nBurnDecayBlocks`) is the decay threshold (`500,000` blocks on Mainnet, `10,000` blocks on Testnet/Regtest). Once $T \ge T_{\text{MAX}}$, the burn bonus decays to 0 (leaving the base staking UTXO amount).

### 3.4. Proof of Masternode (PoM) — Disabled Under `UPGRADE_FAIRNESS`
* **Under `UPGRADE_FAIRNESS` (Height 1 on Mainnet, Testnet, and Regtest)**: `PoM` (`MPA_WEIGHT_POM` up to `2x`) is **disabled** in `CalculateMPAWeight` (`src/kernel.cpp`) to prevent Masternode collaterals — which already earn `35%` Passive MN rewards, `10%` Signer-Only LLMQ rewards, and `40%/11` active committee seats — from double-dipping in PoS staking weight. Any legacy PoM-eligible UTXO falls back cleanly to standard `PoS (1x)` (or `PoL`/`PoB` if applicable).
* *(Historical Pre-Fairness formula retained only below `UPGRADE_FAIRNESS`: $W_{\text{PoM}} = C \times (1 + \alpha \cdot \min(t_{\text{active}} / T_{\text{MAX}}, 1.0))$ with $\alpha = 1.0$, $T_{\text{MAX}} = 100{,}000$ Mainnet / $10{,}000$ Testnet.)*

---

## 4. Long-Living Masternode Quorums (LLMQs)

To support secure leader election and signature aggregation, LLMQs use **BLS12-381** (`blst`) aggregated threshold signatures (`CQuorumSignature::AggregateBLSSignatures`). Under unified **KTDIP3 (`UPGRADE_DIP3`)**, quorum members sign `blockHash` directly using their registered operator BLS key (`blsPubKeyOperator` in `evodb`, verified via `VerifyAdamOrDip3Signature` / `VerifyDip3OperatorBLS` without requiring Cold ECDSA keys or `PinBLSPubKey`); pre-DIP3 and non-MN fallback members use deterministic `DeriveBLSFromCKey` keys authorized via `CBLSSignedData` (`secp256k1` + `PinBLSPubKey`). During verification (`CQuorumSignature::Verify`), after verifying member identities, signing keys and aggregated signatures (`CBLSPubKey::Aggregate`, `CBLSSignature::Aggregate`) are validated via a single pairing equation.

### Quorum Election & Size
* **Dynamic Quorum Size (`GetTargetQuorumSize` under `UPGRADE_HARDENING_V1`)**: Committee size $N$ scales dynamically with verified on-chain active Masternodes: **`5`** (`<25` MNs, majority `3-of-5`), **`9`** (`25–49` MNs, `5-of-9`), **`11`** (`50–99` MNs, `6-of-11`), or **`15`** (`>=100` MNs, `8-of-15`).
* **DKG Interval & Per-Block Liveness Rotation**: `GetActiveQuorum` runs DKG sessions every **100 blocks** on Mainnet (until block 1,400) and every **10 blocks** on Testnet (until block 1,000) / Regtest (until block 800); from those heights onward (including all Mainnet `UPGRADE_POMBL` blocks `>= 4,800`), `RunDKG(nHeight)` evaluates active quorum membership at **every block** using the deterministic epoch seed at `((nHeight / dkgInterval) * dkgInterval) - 1`.
* **Active Masternode Filtering**: No hardcoded key ID filtering or local wallet restrictions are applied on any network (Mainnet, Testnet, or Regtest). The election dynamically draws from all active valid Masternodes (`evodb` under KTDIP3 or legacy on-chain pings).
* **Deterministic Fallback**: If fewer than 5 active Masternodes exist on-chain, `ElectQuorumMembers` strictly preserves all verified real Active Masternodes (1..4) in the quorum and deterministically fills only the remaining slots up to 5 from quota-isolated fallback candidates (`GetAdamMinerPool(nHeight - 1)`: at most 1 Coin-Lock + up to 3 Funded PoW-Locks, then overflow -> Bootstrap -> Zero-Coin -> Coin-Lock fallback). On Regtest, when no on-chain Masternodes or Coin-Locks exist, the registered miner pool uses the **15 deterministic keys** to facilitate automated testing.

### Signature and Threshold Validation
* Quorum members sign the block hash using their registered `blsPubKeyOperator` (KTDIP3) or `DeriveBLSFromCKey` keys authorized by `secp256k1`, and signatures are aggregated via `CQuorumSignature::AggregateBLSSignatures`.
* The quorum signature `vQuorumSig` is populated in block headers once version is `>= 12` (when Standard Mode is active via height-gated `Consensus::UPGRADE_POMBL`: block 4,800 on Mainnet, 400 on Testnet, and 300 on Regtest). Note that Model D (`UPGRADE_MODELD`) is a separate upgrade height that activates at height 5,000 on Mainnet, 500 on Testnet, and 200 on Regtest.
* **Quorum Validation Threshold (`CQuorumSignature::Verify` in `src/llmq.cpp`)**:
  - **Prior to Model D Activation (`!NetworkUpgradeActive(nHeight, UPGRADE_MODELD)` on Mainnet / Testnet)**: The verification threshold is **0 signatures** (verification is bypassed to allow bootstrapping before active LLMQ quorums are required).
  - **Once Model D is Active (Mainnet, Testnet, and Regtest)**: Both block generation (`miner.cpp`) and consensus verification (`llmq.cpp`) enforce a strict BFT majority threshold of `quorum.members.size() / 2 + 1` (`3-of-5`, `5-of-9`, `6-of-11`, or `8-of-15` depending on active Masternode count).

> [!NOTE]
> **Quorum Threshold Distinction:**
> Do not confuse the **ADAM Cooperative Mining Threshold** (7 out of 11 rule for puzzle solutions `vAdamSolutions`) with the **LLMQ Signature Verification Threshold** (`quorum.members.size() / 2 + 1` BFT majority for `vQuorumSig`):
> * **ADAM Threshold**: Enforces that at least `GetAdamEffectiveThreshold` valid solutions (baseline `5` during bootstrap below `nAdamThresholdBootstrapLimit`, then `nAdamThreshold = 7` on Mainnet/Regtest and `3` on Testnet, with time-based relaxation at $t \ge 120\text{s}$) must be solved and submitted in `vAdamSolutions` for the block to be accepted. This is part of the ADAM cooperative puzzle validation.
> * **LLMQ Threshold**: Enforces that at least `3-of-5` up to `8-of-15` active LLMQ members must sign the proposed block hash using their BLS12-381 keys (`vQuorumSig`). This is part of the decentralized block validation.

---

## 5. Consensus Enforcements & Validation Rules

When a block is received, the validation rules in `CheckBlock()` in `src/main.cpp` enforce the following checks when `Consensus::UPGRADE_POMBL` is active:

1. **Version Enforcement**: The block version must be at least `12`. Block headers of version < 12 are strictly rejected with a `bad-version` code.
2. **Burn Output Integrity**: All transaction inputs spending from a registered burn address are strictly rejected at the mempool layer (`AcceptToMemoryPool`) and block connection layer (`ConnectBlock`) with `bad-txns-spend-burn-address`.
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
* Asserts that attempts to spend from a burn address are blocked and rejected with `bad-txns-spend-burn-address`.
