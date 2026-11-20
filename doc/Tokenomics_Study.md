# KristaTech (KRISTA) Tokenomics Study & Design Proposal

This study represents a comprehensive **Tokenomics (Token Economy)** design proposal prepared to maximize the financial sustainability, security incentives, and market reputation of the KristaTech (KRISTA) blockchain.

Instead of the temporary dummy values currently used, a new emission program is proposed that mathematically models supply, limits inflation, and establishes a healthy balance between masternodes and staking.

---

## 1. Current Situation and Inflation Risk Analysis

In the original (dummy) setup, with a 30-second block time, approximately **1,051,200 blocks** were mined per year.
* **First 5 months (400,000 blocks):** The circulating supply quickly reached 5M (premine) + 50M (mining) = **55,000,000 KRISTA**.
* **Afterward:** Unlimited inflation was produced with a constant emission of **105,120,000 KRISTA** per year.
* **Reward Distribution:** 95% of rewards went to masternodes, and only 5% went to miners/stakers.

### Risks:
1. **Excessive Sell Pressure:** With 95% of rewards going to masternodes, there is constant downward pressure on price due to operators who tend to continuously sell coins on the market.
2. **Insecure Mining/Staking Infrastructure:** Receiving only 5% of rewards, miners (PoW) and stakers (PoS) lack sufficient economic incentives to secure the network. This weakens block production stability and overall security.
3. **Loss of Reputation:** Unlimited or excessively high supply caps destroy the coin's identity as a "store of value."

---

## 2. Global Standards and Comparison (Bitcoin and Others)

| Cryptocurrency | Consensus | Maximum Supply (Hard Cap) | Emission Reduction Model | Masternode / Staking Share |
| :--- | :---: | :---: | :---: | :---: |
| **Bitcoin (BTC)** | PoW | 21,000,000 BTC | 50% reduction every 4 years (Halving) | None |
| **Dash (DASH)** | PoW/Masternode | ~18,900.000 DASH | 7.14% reduction every year (Decay) | 47.5% MN / 47.5% Miner / 5% Treasury |
| **KRISTATECH (KRISTA)** | PoW/PoS Hybrid (ADAM/Model D) | 210,000,000 KRISTA | 2.15% reduction every ~90 days (Decay) | Model D: 35% Passive MN / 10% LLMQ / 15% Producer / 40% Validator (20% 7 MN + 20% 4 Miner) |

---

## 3. Proposed KRISTA Tokenomics Model

In order for KRISTA to protect both infrastructure providers (Masternodes) and network security (Staking) at the maximum level, and to remain a **reputable digital asset**, the following model is proposed:

### 3.1. Limited Maximum Supply (Hard Cap)
* **Proposed Hard Cap:** **210,000,000 (210 Million) KRISTA**
* **Premine:** **0 KRISTA** (No Premine)
* **Circulation to be Distributed via Mining/Staking:** **210,000,000 KRISTA** (100%)

### 3.2. Quarterly Emission Reduction (Decay Model from Block 2)
The emission program launches cleanly at **17 KRISTA** per block from Block 2 (Blocks 0–1 mint `0 KRISTA`) and applies a **2.15% quarterly decay** every ~90 days (`259,200` blocks, `period = (nHeight - 2) / 259200`). Eliminating temporary bootstrap step-downs avoids supply-cliff shocks while providing sufficient early liquidity for `4,200 KRISTA` masternode formation and sustaining block rewards for over 45 years.

### 3.3. Balancing Masternode & Miner-Staker Reward Distribution
Block reward distribution is optimized to incentivize both PoW miners and PoS stakers under the Model D hybrid split:
* **Blocks 2 - 199 (Classic PoW Period 0 Start):** 100% Miner (`15.81 KRISTA` net of 7% Treasury) to bootstrap initial coin distribution and network hash power.
* **Blocks 200 - 4,999 (Pre-Model D Cooperative ADAM):** **60% Block Producer (Coordinator / Staker) + 40% Active ADAM Validators** (`8.00%` for 1 Coin-Lock seat, `4.00%` each for 3 Open PoW seats, `~2.857%` each for 7 MN/spillover seats, with unsolved seat shares split 50% Automatic Protocol Burn + 50% Developer Treasury) while LLMQ/PoMBL Version 12 activates at Block `4,800` (`UPGRADE_POMBL`).
* **Blocks 5,000+ (Model D & KTDIP3 v2 Maturation):** **45% Passive+LLMQ Masternode / 55% Active Producer+Validator** split is fully active and enforced under Model D (`UPGRADE_MODELD = 5,000` on Mainnet, `500` on Testnet, `200` on Regtest): 35% passive MN (`5.5335 KRISTA` in Period 0), 10% active LLMQ (`1.5810 KRISTA`), 40% participants [20% across 7 MN seats + 20% across 4 independent miner seats], and 15% block winner — resulting in 80% total Masternode ecosystem share and 20% dedicated Independent Miner share.

> [!NOTE]
> Since the network has a dual (hybrid) structure, PoW miners and PoS stakers are continuously incentivized by receiving 55% of the net block reward for active block production and validation (15% coordinator/producer + 20% across 7 Masternode validator seats + 20% across 4 independent miner seats [8% Coin-Lock + 3 x 4% Open PoW]; rescuing same-track standby validators at $t \ge 60\text{s}$ earn 100% of their track's seat share, while overflow validators filling vacant Masternode seats earn the Masternode base rate (`~2.857%`)). Transaction fees (`nFees`) go 100% to the Block Producer in PoW blocks and are destroyed (burned) in PoS blocks. The Miner/Staker share never drops to 0%.

### 3.4. Flat Masternode Collateral
To balance network security, validator participation, and hosting ROI, the masternode collateral is locked to a flat **4,200 KRISTA** starting from block 1. This enables rapid bootstrapping of the 20+ active masternodes required for LLMQ quorums since the circulating supply can easily support the collateral requirements.

---

## 4. Mathematical Projection (25-Year Simulation)

Under the **17 KRISTA + 2.15% Quarterly (~90 days) Decay** model (`period = (nHeight - 2) / 259200`), block rewards and supply growth progress as follows:

* **Launch & Early Consensus Milestones (Blocks 0 – 5,000):**
  - **Blocks 0 – 1:** **0 KRISTA** (Fair Launch — Zero Premine on Mainnet)
  - **Blocks 2 – 4,800 (`UPGRADE_POMBL`):** **17 KRISTA** per block (`81,583 KRISTA` gross / `75,872.19 KRISTA` net -> 18.06 MNs max)
  - **Blocks 4,801 – 5,000 (`UPGRADE_MODELD`):** **17 KRISTA** per block (`84,983 KRISTA` cumulative gross / `79,034.19 KRISTA` net -> 18.82 MNs max)
* **Year 1 (Periods 0–3, Blocks 2 – 1,036,801 [4Q] / ~365d Block 1,051,199):** **17 KRISTA** decaying 2.15% every 259,200 blocks
  - Period Rewards (`0..3`): `17.00` -> `16.6345` -> `16.2769` -> `15.9269 KRISTA` (Avg **16.46 KRISTA**)
  - Annual Production (`4Q`): **17,065,278.04 KRISTA** (`365d`: **17,289,663.35 KRISTA**)
  - Cumulative Supply at Year End (`4Q`): **17,065,278.04 KRISTA** (`8.13%` of Hard Cap)
* **Year 2 (Periods 4–7, Blocks 1,036,802 – 2,073,601 [4Q]):**
  - Average Block Reward: **15.09 KRISTA** (2.15% quarterly reduction steps)
  - Annual Production: **15,644,319.92 KRISTA**
  - Cumulative Supply at Year End: **32,709,597.96 KRISTA** (`15.58%` of Hard Cap)
* **Year 3 (Periods 8–11, Blocks 2,073,602 – 3,110,401 [4Q]):**
  - Average Block Reward: **13.83 KRISTA**
  - Annual Production: **14,341,679.35 KRISTA**
  - Cumulative Supply at Year End: **47,051,277.31 KRISTA** (`22.41%` of Hard Cap)
* **Year 5 (Periods 16–19, Blocks 4,147,202 – 5,184,001 [4Q]):**
  - Average Block Reward: **11.63 KRISTA**
  - Annual Production: **12,052,763.88 KRISTA**
  - Cumulative Supply at Year End: **72,251,545.70 KRISTA** (`34.41%` of Hard Cap)
* **Year 10 (Periods 36–39, Blocks 9,331,202 – 10,368,001 [4Q]):**
  - Average Block Reward: **7.53 KRISTA**
  - Annual Production: **7,803,748.21 KRISTA**
  - Cumulative Supply at Year End: **119,031,925.39 KRISTA** (`56.68%` of Hard Cap)
* **Infinite Horizon:** The total gross minted supply asymptotes to **204,948,837 KRISTA** (`97.59%` of Hard Cap), safely under **210,000,000 KRISTA**, leaving a **5.05M KRISTA (`2.41%`) gap reserve** and ensuring smooth emission for over 45 years without halving shocks.

```
Supply Saturation Projection Graph:
[0] (Genesis) -> [0.085M] (Block 5k) -> [17.07M] (Year 1) -> [32.71M] (Year 2) -> [47.05M] (Year 3) -> [72.25M] (Year 5) -> [119.03M] (Year 10) -> [204.95M] (Asymptote)
```

---

## 5. Why This Model Makes KRISTA Reputable

1. **Deflationary Structure:** Having the total supply locked at a reputable limit like 210 Million ensures that the unit value increases in the long term. The actual supply asymptotes around 204.95M, making it even more scarce.
2. **High Lock-up Rate:** Setting the Masternode collateral to a flat 4,200 KRISTA enables a massive number of active masternodes (20+ for quorums, scaling upwards), locking up circulating supply and narrowing exchange liquidity to drive price appreciation.
3. **Secure PoW/PoS:** Directing 55% of net rewards (15% producer + 40% participants, including a dedicated 20% sub-pool [8% for 1 Coin-Lock + 4% each for 3 Open PoW] for the 4 independent miners) to active block producers and validators incentivizes hardware hashpower, wallet uptime, and network participation, decentralizing security.

---

## 6. Code-Level Changes

The finalized implementation in the code is as follows:

1. **Setting the Maximum Supply Limit:**
   Set `consensus.nMaxMoneyOut = 210000000 * COIN;` (210M) in [src/chainparams.cpp](../src/chainparams.cpp).
2. **Updating Block Reward Logic:**
   Update the `GetBlockValue` function in [src/masternode.cpp](../src/masternode.cpp) to enforce the Block 2 `17 KRISTA` start and 2.15% quarterly (~90 days) decay:
   ```cpp
   CAmount CMasternode::GetBlockValue(int nHeight)
   {
       CAmount maxMoneyOut = Params().GetConsensus().nMaxMoneyOut;

       if (nMoneySupply >= maxMoneyOut) {
           return 0;
       }

       if (nHeight == 1) {
           return 0; // Set premine to 0
       }

       if (nHeight == 0) {
           return 17 * COIN;
       }

       if (nHeight < 2) {
           return 0;
       }

       // 2.15% decay every ~90 days (259,200 blocks) from Block 2 — Deterministic 128-bit Fixed-Point Integer Math
       int period = (nHeight - 2) / 259200;
       static const unsigned __int128 FIXED_SCALE = 1000000000000000000ULL; // 10^18
       static const unsigned __int128 DECAY_NUM   = 978500000000000000ULL; // 0.9785 * 10^18
       unsigned __int128 factor = FIXED_SCALE;
       unsigned __int128 base = DECAY_NUM;
       int exp = period;
       while (exp > 0) {
           if (exp & 1) {
               factor = (factor * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
           }
           exp >>= 1;
           if (exp > 0) {
               base = (base * base + (FIXED_SCALE / 2)) / FIXED_SCALE;
           }
       }
       unsigned __int128 rawSubsidy = (unsigned __int128)(17 * COIN) * factor;
       CAmount nSubsidy = (CAmount)((rawSubsidy + (FIXED_SCALE / 2)) / FIXED_SCALE);

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }

       return nSubsidy;
   }
   ```
3. **Updating Split Ratios:**
   Update the `GetMasternodePayment` function in [src/masternode.cpp](../src/masternode.cpp) to return the correct reward distributions:
    ```cpp
    CAmount CMasternode::GetMasternodePayment(int nHeight)
    {
        // Model D activates at block 5,000 on Mainnet, 500 on Testnet, and 200 on Regtest.
        // When active, it overrides the legacy bootstrap rules below.
        if (IsModelDActive(nHeight)) {
            CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
            CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
            CAmount nBlockVal = nBlockValActual - nTreasurySplit;
            return nBlockVal * 35 / 100; // %35 MN passive pay (Model D) after 7% Treasury split
        }

        // Legacy/Fallback Rules (prior to Model D activation):
        // 1. Early Bootstrap (blocks 2 to 5,000): Masternodes receive no payment to allow initial setup.
        if (nHeight <= 5000) {
            return 0;
        }

        // 2. Late Bootstrap (blocks 5,001 to 100,000): Masternodes receive 80% of block value.
        if (nHeight <= 100000) {
            return CMasternode::GetBlockValue(nHeight) * 80 / 100; // %80 MN, %20 Miner-Staker
        }

        // 3. Maturation Phase (blocks 100,001+): Masternodes receive 60% of block value.
        return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Miner-Staker
    }
    ```

---

## 7. Developer Treasury Split

To secure long-term ecosystem funding, a protocol-enforced block reward split mechanism is active since block 2 on Mainnet:

* **Developer Treasury (7%)**:
  - **Deduction**: 7% of the block reward is automatically allocated to the Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`).
  - **Scope**: Applies to all blocks starting from block height 2. Blocks 0 and 1 (zero premine) are exempt.
* **Unclaimed Cooperative / Model D Shares (50% Protocol Burn / 50% Developer Treasury)**:
  - Starting at block 200 (`IsAdamValidatorSplitActive` and Model D `block >= 5,000` on Mainnet), any unclaimed validator or LLMQ shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn** (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, permanently deducted from `nMoneySupply` with zero personal PoB weight) and **50% to the Developer Treasury**.

The 7% Developer Treasury deduction leaves a uniform **93.0%** net reward pool for validators and masternodes across every emission phase (e.g., from **17 KRISTA** to **15.81 KRISTA** during Period 0 [Blocks 2–259,201], and from **16.6345 KRISTA** to **15.470085 KRISTA** during Period 1 [Blocks 259,202–518,401]).

Under **KTDIP3 v2** (`nDip3V2Height = 5,000` on Mainnet; `1` on Testnet/Regtest), masternode block and quorum payouts are routed through `AppendDip3OrDirectPayment` directly to the deterministic `ProRegTx` `scriptPayout` (with an optional `nOperatorReward` basis-points split to `scriptOperatorPayout`), while LLMQ consensus participation scales dynamically (`5 -> 9 -> 11 -> 15` members via `GetTargetQuorumSize`) with the active deterministic pool.
