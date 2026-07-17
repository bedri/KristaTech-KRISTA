```
KTIP: 0003
Title: PoMPA (Proof of Multi-Proof Algorithm) Weight & Dual Mining
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-06-16
```

## Abstract
This proposal specifies the **PoMPA (Proof of Multi-Proof Algorithm)** and **Dual Mining** consensus mechanism. PoMPA enhances standard Proof-of-Stake (PoS) by dynamically scaling staking kernel weights using four distinct contribution proofs: Proof of Stake (PoS), Proof of Lock (PoL), Proof of Burn (PoB), and Proof of Masternode (PoM). Dual Mining allows PoS staking and ADAM cooperative PoW mining to run in parallel, securing the network under a dual-locking validation loop.

## Motivation
Standard PoS networks suffer from several consensus vulnerabilities, including:
1. **Nothing-at-Stake**: Stakers can sign blocks on competing forks simultaneously at zero cost.
2. **Wealth Centralization**: The richest staking wallets dominate block generation.
3. **Low Economic Velocity**: Holding coins passively in a wallet is highly rewarded, discouraging active network participation or lockups.

PoMPA introduces a dynamic staking weight model that rewards network commitment (burns, locks, and active masternode operations) rather than raw passive token holdings, while Dual Mining binds physical PoW to PoS blocks to completely resolve the Nothing-at-Stake problem.

## Specification

### 1. PoMPA Dynamic Weight Model
Staking kernel target evaluations (`CheckStakeKernelHash()` in `src/kernel.cpp`) utilize a dynamically calculated staking weight multiplier computed via `CalculateMPAWeight()`. The total staking weight is the sum of four components:

$$W_{\text{total}} = W_{\text{PoS}} + W_{\text{PoL}} + W_{\text{PoB}} + W_{\text{PoM}}$$

Each component is defined below:

#### A. Proof of Stake (PoS - Baseline Weight)
The baseline weight corresponds to standard, unlocked coins held in a wallet:

$$W_{\text{PoS}} = \text{Amount}$$

---

#### B. Proof of Lock (PoL - Timelock Bonus)
Coins locked in outputs using absolute (`OP_CHECKLOCKTIMEVERIFY`) or relative (`OP_CHECKSEQUENCEVERIFY`) script timelocks of duration $L$ blocks receive a lockup multiplier:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

Where:
* $\gamma = 2.0$ represents a maximum 200% bonus (yielding up to a 3x weight multiplier).
* $L_{\text{MAX}} = 50,000$ blocks represents the maximum rewarded lock duration.

---

#### C. Proof of Burn (PoB - Burn-decay Weight)
Coins sent to the registered unspendable burn address receive a high initial multiplier that decays linearly over time $T$ (blocks elapsed since the burn block):

$$W_{\text{PoB}} = \text{Amount} + \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$

Where:
* $\beta = 5.0$ represents a starting 5x multiplier on the burned amount.
* $T_{\text{MAX}} = 10,000$ blocks represents the burn weight decay period. Once $T \ge T_{\text{MAX}}$, the burn weight multiplier decays to 0, leaving only the base UTXO amount.

---

#### D. Proof of Masternode (PoM - Masternode Age Bonus)
Active Masternodes with collateral $C = 4,200 \text{ KRISTA}$ and active lifetime $t_{\text{active}}$ (blocks elapsed since the node transitioned to `ENABLED` status in `masternode.conf`) receive a Masternode age multiplier:

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

Where:
* $\alpha = 1.0$ represents a maximum 100% age bonus (yielding up to a 2x weight multiplier on the collateral).
* $T_{\text{MAX}} = 10,000$ blocks represents the maximum rewarded active age. Any state transitions out of `ENABLED` reset the active lifetime $t_{\text{active}}$ to 0.

---

### 2. Dual Mining & Staking Integration
Dual Mining couples PoS block validation with the ADAM cooperative PoW solver. A block is only accepted by the network if:
1. The staking kernel check matches the target difficulty:

   $$\text{kernelHash} \le W_{\text{total}} \times \text{Target}$$

2. The block header includes a quorum of valid, signed partial PoW solutions from elected ADAM miners.
3. The block is double-signed by the staker's key (`vchBlockSig`) and the elected ADAM coordinator's key (`vAdamCoordinatorSig`).

## Backward Compatibility
* PoMPA weights and Dual Mining are activated conditionally upon block height:
  - POS staking activates on Mainnet at block height `200` (`Consensus::UPGRADE_POS`).
  - Standard Mode Version 12 block serialization activates at block height `2000` (Mainnet) or `400` (Testnet).
* Legacy PoW-only blocks prior to height 200 do not evaluate PoMPA weights.

## Reference Implementation
* Weight calculations: `CalculateMPAWeight()` and `CheckStakeKernelHash()` in `src/kernel.cpp`.
* Staking mining thread: `ThreadStakeMinter()` in `src/wallet/wallet.cpp`.
* Coinbase and coinstake transaction validation: `CheckBlock()` in `src/main.cpp`.
