```
KTIP: 0003
Title: PoMPA (Proof of Multi-Proof Algorithm) Weight & Dual Mining
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-06-16
```

## Abstract
This proposal specifies the **PoMPA (Proof of Multi-Proof Algorithm)** and **Dual Mining** consensus mechanism. PoMPA enhances standard Proof-of-Stake (PoS) by dynamically scaling staking kernel weights using active contribution proofs: under `UPGRADE_FAIRNESS` (Height `1`), Proof of Stake (PoS, `1x`), reduced-coefficient Proof of Masternode (`PoM`, up to `1.25x` with $\alpha = 0.25$ vs legacy `2x`), live lock-preserving Proof of Lock (PoL, up to `3x`), and Proof of Burn (PoB, up to `6x`) work together in balanced progression. Dual Mining allows PoS staking and ADAM cooperative PoW mining to run in parallel, securing the network under a dual-locking validation loop.

## Motivation
Standard PoS networks suffer from several consensus vulnerabilities, including:
1. **Nothing-at-Stake**: Stakers can sign blocks on competing forks simultaneously at zero cost.
2. **Wealth Centralization**: The richest staking wallets dominate block generation.
3. **Low Economic Velocity**: Holding coins passively in a wallet is highly rewarded, discouraging active network participation or lockups.

PoMPA introduces a dynamic staking weight model that rewards network commitment (burns, locks, and active masternode operations) rather than raw passive token holdings, while Dual Mining binds physical PoW to PoS blocks to completely resolve the Nothing-at-Stake problem.

## Specification

### 1. PoMPA Dynamic Weight Model
Staking kernel target evaluations (`CStakeKernel::CheckKernelHash()` in `src/kernel.cpp`) utilize a dynamically calculated staking weight multiplier computed via `CalculateMPAWeight()` once `Consensus::UPGRADE_POMBL` is active (`4800` on Mainnet, `400` on Testnet, `300` on Regtest). Under `UPGRADE_FAIRNESS` (Height `1`), `CalculateMPAWeight()` evaluates **`1. PoM (up to 1.25x, alpha = 0.25) -> 2. PoL (up to 3x, live self-renewing CLTV) -> 3. PoB (up to 6x) -> 4. PoS (1x)`**, with 256-bit saturation protection at `nHeight >= 2280`:

$$
W_{\text{effective}} \in \left\lbrace W_{\text{PoM}}, W_{\text{PoL}}, W_{\text{PoB}}, W_{\text{PoS}} \right\rbrace
$$

Each tier is defined below:

#### A. Proof of Stake (PoS - Baseline Weight)
The baseline weight corresponds to standard, unlocked coins held in a wallet:

$$W_{\text{PoS}} = \text{Amount}$$

---

#### B. Proof of Lock (PoL - Live Self-Renewing Timelock Bonus)
Coins locked in outputs using absolute (`OP_CHECKLOCKTIMEVERIFY`) or relative (`OP_CHECKSEQUENCEVERIFY`) script timelocks of duration $L$ blocks receive a lockup multiplier:

$$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$

Where:
* $\gamma = 2.0$ represents a maximum 200% bonus (yielding up to a 3x weight multiplier).
* $L_{\text{MAX}} = 50,000$ blocks represents the maximum rewarded lock duration.
* Under `UPGRADE_FAIRNESS` (Height `1`), **live `TX_CLTV_TIMELOCK` self-renewal** enables any matured CLTV UTXO (even while its `OP_CLTV` lock height has not yet expired) to stake as a single-input coinstake provided `vout[1].scriptPubKey == prevout.scriptPubKey` and `vout[1].nValue >= prevout.nValue`, preserving both principal and lock script without breaking time-lock custody.

---

#### C. Proof of Burn (PoB - Burn-decay Weight)
Coins voluntarily sent in standard non-coinbase / non-coinstake transactions (`!tx.IsCoinBase() && !tx.IsCoinStake()`) to a registered unspendable burn address (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` / `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95` on Mainnet, `ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4` / `ktBurnKmNRzVXVVVDXxFhhKytkH5x2yqsCG` on Testnet) receive a high initial personal PoB staking weight multiplier in `mapAddressBurns` that decays linearly over time $T$ (blocks elapsed since the burn block). Automatic protocol burns in `CoinBase` or `CoinStake` outputs permanently deduct from `nMoneySupply` only and do **not** grant personal PoB staking weight:

$$W_{\text{PoB}} = \text{Amount} + \min\left(\text{BurnWeight}_{\text{active}}, \text{Amount} \times \beta\right)$$

Where:
* $\text{BurnWeight}_{\text{active}} = \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$.
* $\beta = 5.0$ represents a starting 5x multiplier on the burned amount. Capping the per-UTXO burn bonus to $\text{Amount} \times \beta$ ensures 100% UTXO-splitting invariance, preventing an attacker from dividing coins into thousands of tiny UTXOs to artificially multiply address-level burn weight.
* $T_{\text{MAX}}$ (`nBurnDecayBlocks`) is **500,000 blocks** on Mainnet (**10,000 blocks** on Testnet and Regtest), representing the burn weight decay period. Once $T \ge T_{\text{MAX}}$, the burn weight multiplier decays to 0, leaving only the base UTXO amount.

---

#### D. Proof of Masternode (PoM - Reduced Fairness Uptime Bonus, `1.25x`)
Active Masternodes verified 100% deterministically on-chain (via `evodb` KTDIP3 registration/coin height or legacy `IsMasternodeActiveAtHeight` within 2,880 blocks and `IsValidMasternodeCollateralAndAuth`) receive an uptime loyalty multiplier on their `4,200 KRISTA` collateral (`W_PoM`):

$$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$

Where under **`UPGRADE_FAIRNESS` (Block `1` across all networks)**, **$\alpha = 0.25$ (max `1.25x` / `+25%`, exact integer formula `C + floor(C * capped / (4 * T_MAX))`)** over $T_{\text{MAX}} = 100,000$ blocks on Mainnet ($10,000$ on Testnet/Regtest), preserving a modest uptime reward alongside Passive MN (`35%`) and LLMQ (`10%`) payouts while keeping `PoM (1.25x) << PoL (3x) < PoB (6x)`. *(Pre-Fairness historical parameter: $\alpha = 1.0$, max `2.0x`.)*

---

### 2. Dual Mining & Staking Integration
Dual Mining couples PoS block validation with the ADAM cooperative PoW solver (running alongside cooperative ADAM PoW blocks, with alternating `1,500ms` `ADAM_SLOT_PRIORITY_WINDOW_MS` slot-opening priority once `UPGRADE_POMBL` is active). A cooperative PoS block (`IsProofOfStake()`) is only accepted by the network if:
1. The staking kernel check matches the target difficulty:

   $$\text{kernelHash} < \left\lfloor \frac{W_{\text{total}}}{100} \right\rfloor \times \text{Target}$$

2. The block header includes a quorum of valid, signed partial PoW solutions from elected ADAM miners.
3. The PoS block is double-signed by the staker's key (`vchBlockSig`) and the elected ADAM coordinator's key (`vAdamCoordinatorSig`, plus the dynamic BFT LLMQ `vQuorumSig` — `3-of-5` baseline scaling `5 -> 9 -> 11 -> 15` via `GetTargetQuorumSize` — once `UPGRADE_MODELD` is active).

## Backward Compatibility
* PoMPA weights and Dual Mining are activated conditionally upon block height:
  - PoS staking activates at block height `200` on Mainnet/Testnet and `251` on Regtest (`Consensus::UPGRADE_POS`).
  - Standard Mode Version 12 block serialization (`Consensus::UPGRADE_POMBL`) activates at block height `4800` (Mainnet), `400` (Testnet), or `300` (Regtest).
* Legacy PoW-only blocks prior to `UPGRADE_POS` do not evaluate PoMPA weights.

## Reference Implementation
* Weight calculations and kernel validation: `CalculateMPAWeight()`, `CStakeKernel::CheckKernelHash()`, and `CheckProofOfStake()` in `src/kernel.cpp`.
* Staking mining thread: `ThreadStakeMinter()` in `src/miner.cpp`.
* Coinbase and coinstake transaction validation: `CheckBlock()` in `src/main.cpp`.
