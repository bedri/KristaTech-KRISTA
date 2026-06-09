# KristaTech (KRISTA) Tokenomics Study & Design Proposal

This study represents a comprehensive **Tokenomics (Token Economy)** design proposal prepared to maximize the financial sustainability, security incentives, and market reputation of the KristaTech (KRISTA) blockchain.

Instead of the temporary dummy values currently used, a new emission program is proposed that mathematically models supply, limits inflation, and establishes a healthy balance between masternodes and staking.

---

## 1. Current Situation and Inflation Risk Analysis

In the current (dummy) setup, with a 30-second block time, approximately **1,051,200 blocks** are mined per year.
* **First 5 months (400,000 blocks):** The circulating supply quickly reaches 5M (premine) + 50M (mining) = **55,000,000 KRISTA**.
* **Afterward:** Unlimited inflation is produced with a constant emission of **105,120,000 KRISTA** per year.
* **Reward Distribution:** 95% of rewards go to masternodes, and only 5% go to miners/stakers.

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
| **PIVX (PIVX)** | PoS/Masternode | Unlimited (Dynamic Deflation) | Constant 5 PIVX per block (MN/Staker dynamic) | Variable (Usually 60% MN / 40% Staker) |

---

## 3. Proposed KRISTA Tokenomics Model

In order for KRISTA to protect both infrastructure providers (Masternodes) and network security (Staking) at the maximum level, and to remain a **reputable digital asset**, the following model is proposed:

### 3.1. Limited Maximum Supply (Hard Cap)
* **Proposed Hard Cap:** **100,000,000 (100 Million) KRISTA**
* **Premine (Founder / Ecosystem / Funding):** **5,000,000 KRISTA** (5%)
* **Circulation to be Distributed via Mining/Staking:** **95,000,000 KRISTA** (95%)

### 3.2. Annual Emission Reduction (Decay Model)
It is proposed to **reduce block rewards by 15%** every **1,051,200 blocks** (approximately 1 year). This model offers a smoother and more predictable transition instead of Bitcoin's harsh 4-year halving shocks.
*(Note: The final implementation plan adopted a 20% annual decay with a starting reward of 14.5 KRISTA to meet the 100M hard cap target).*

### 3.3. Balancing Masternode & Miner-Staker Reward Distribution
Block reward distribution should be step-by-step optimized to incentivize both PoW miners and PoS stakers:
* **Blocks 2 - 5000 (L1 Transition Period):** 100% Miner/Staker (to ensure network security and hash power while masternodes are being set up).
* **Blocks 5001 - 100,000:** 80% Masternode / 20% Miner-Staker.
* **Blocks 100,001+ (Maturation Period):** **60% Masternode / 40% Miner-Staker** (The most balanced ratio in the industry).

> [!NOTE]
> Since the network has a dual (hybrid) structure, PoW miners (when a block is produced via PoW) or PoS stakers (when a block is produced via PoS) are continuously incentivized by receiving 40% of the block reward and 100% of the transaction fees. The Miner/Staker share never drops to 0%.

### 3.4. Collateral Scaling
The collateral amount required to set up a Masternode is gradually increased to encourage locking up circulating supply:
* **Blocks 1 - 100,000:** 15,000 KRISTA
* **Blocks 100,001 - 200,000:** 17,500 KRISTA
* **Blocks 200,001+:** 20,000 KRISTA
*(Note: To simplify local deployment and exchange integrations, the final implementation locks the masternode collateral to a flat 20,000 KRISTA from block 1).*

---

## 4. Mathematical Projection (10-Year Simulation)

According to the proposed annual 15% emission reduction (Decay) model, block rewards and annual supply growth:

* **Year 1 (Blocks 2 - 1,051,200):** **25 KRISTA** per block
  - Annual Production: ~26,280,000 KRISTA
  - Total Supply at Year End: **31,280,000 KRISTA** (Including Premine)
* **Year 2 (Blocks 1,051,201 - 2,102,400):** **21.25 KRISTA** per block (15% Reduction)
  - Annual Production: ~22,338,000 KRISTA
  - Total Supply at Year End: **78,618,000 KRISTA**
* **Year 3 (Blocks 2,102,401 - 3,153,600):** **18.06 KRISTA** per block
  - Annual Production: ~18,984,672 KRISTA
  - Total Supply at Year End: **97,602,672 KRISTA**
* **Year 4 and Beyond:** As the Hard Cap of **100,000,000 KRISTA** is reached, emission stops (block reward becomes 0, and the network is sustained solely by transaction fees).

```
Supply Saturation Projection Graph:
[5M] (Genesis) -> [31.28M] (Year 1) -> [53.61M] (Year 2) -> [72.6M] (Year 3) -> [100M Max Cap] (Year 4.5)
```

---

## 5. Why This Model Makes KRISTA Reputable

1. **Deflationary Structure:** Having the total supply locked at a reputable limit like 100 Million ensures that the unit value increases in the long term.
2. **High Lock-up Rate:** Increasing Masternode collateral to 20,000 KRISTA ensures that 60%-70% of the circulating supply is locked in masternodes. This narrows exchange liquidity, driving the price upward.
3. **Secure PoS:** Directing 40% of rewards to staking wallets incentivizes retail investors to lock their coins and keep their wallets online (staking), decentralizing network security.

---

## 6. Code-Level Changes

If this plan is approved, the simple and effective changes to be made in the code are as follows:

1. **Setting the Maximum Supply Limit:**
   Set `consensus.nMaxMoneyOut = 100000000 * COIN;` (100M) in [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp).
2. **Updating Block Reward Logic:**
   Update the `GetBlockValue` function in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) to calculate annual decay:
   ```cpp
   CAmount CMasternode::GetBlockValue(int nHeight) {
       CAmount maxMoneyOut = Params().GetConsensus().nMaxMoneyOut;
       if (nMoneySupply >= maxMoneyOut) return 0;

       if (nHeight == 1) return 5000000 * COIN; // Premine

       // 15% reduction every 1,051,200 blocks (yearly)
       int year = (nHeight - 2) / 1051200;
       double subsidy = 25.0 * pow(0.85, year);
       CAmount nSubsidy = (CAmount)(subsidy * COIN);

       if (nSubsidy <= 0) nSubsidy = 1 * COIN; // Minimum emission limit (optional)

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }
       return nSubsidy;
   }
   ```
3. **Updating Split Ratios:**
   Update the `GetMasternodePayment` function in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) to give a 60% masternode share:
   ```cpp
   CAmount CMasternode::GetMasternodePayment(int nHeight) {
       if (nHeight <= 5000) return 0;
       return CMasternode::GetBlockValue(nHeight) * 60 / 100; // 60% MN, 40% Staker/Miner
   }
   ```
