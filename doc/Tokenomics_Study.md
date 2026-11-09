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
| **KRISTATECH (KRISTA)** | PoW/PoS Hybrid (ADAM/Model D) | 210,000,000 KRISTA | 1.9% reduction every ~90 days (Decay) | Model D: 35% Passive MN / 10% LLMQ / 15% Producer / 40% Validator (20% 7 MN + 20% 4 Miner) |

---

## 3. Proposed KRISTA Tokenomics Model

In order for KRISTA to protect both infrastructure providers (Masternodes) and network security (Staking) at the maximum level, and to remain a **reputable digital asset**, the following model is proposed:

### 3.1. Limited Maximum Supply (Hard Cap)
* **Proposed Hard Cap:** **210,000,000 (210 Million) KRISTA**
* **Premine:** **0 KRISTA** (No Premine)
* **Circulation to be Distributed via Mining/Staking:** **210,000,000 KRISTA** (100%)

### 3.2. Quarterly Emission Reduction (Decay Model) & Graduated Bootstrap
The emission program uses a **1.9% quarterly decay** model (applied every ~90 days / 259,200 blocks) with a starting standard reward of **15 KRISTA** following a graduated 10,000-block bootstrap phase (**40 KRISTA** for blocks 2–2,200, **30 KRISTA** for blocks 2,201–5,000, and **20 KRISTA** for blocks 5,001–9,999). This model eliminates 72.8% of early bootstrap inflation while offering a smoother and more predictable transition instead of Bitcoin's harsh 4-year halving shocks, stretching the block reward lifecycle for over 50 years.

### 3.3. Balancing Masternode & Miner-Staker Reward Distribution
Block reward distribution is optimized to incentivize both PoW miners and PoS stakers under the Model D hybrid split:
* **Blocks 2 - 2,199 (Early Stage / PoW-PoS Hybrid):** 100% Miner/Staker (to ensure network security and hash power while masternodes are being set up).
* **Blocks 2,200 - 5,000 (Model D Early Stage):** Model D splits are active (35% passive MN, 10% active LLMQ, 40% participants [20% across 7 MN seats + 20% across 4 independent miner seats], 15% block winner). Once Model D is active (`IsModelDActive(nHeight)`), it overrides the legacy bootstrap rule `nHeight <= 5000` (which paid 0). Thus, the 35% passive masternode reward split is fully paid out and enforced by consensus. (Note: Model D activates at block height 2200 on Mainnet, height 500 on Testnet, and height 200 on Regtest).
* **Blocks 5,001+ (Maturation Period):** **45% Passive+LLMQ Masternode / 55% Active Producer+Validator** split is fully active and enforced under Model D (35% passive MN, 10% active LLMQ, 40% participants [20% 7 MN + 20% 4 independent miners], 15% block winner — resulting in 80% total Masternode ecosystem share and 20% dedicated Independent Miner share).

> [!NOTE]
> Since the network has a dual (hybrid) structure, PoW miners and PoS stakers are continuously incentivized by receiving 55% of the net block reward for active block production and validation (15% coordinator/producer + 20% across 7 Masternode validator seats + 20% across 4 independent miner seats [8% Coin-Lock + 3 x 4% Open PoW]) and 100% of the transaction fees. The Miner/Staker share never drops to 0%.

### 3.4. Flat Masternode Collateral
To balance network security, validator participation, and hosting ROI, the masternode collateral is locked to a flat **4,200 KRISTA** starting from block 1. This enables rapid bootstrapping of the 20+ active masternodes required for LLMQ quorums since the circulating supply can easily support the collateral requirements.

---

## 4. Mathematical Projection (25-Year Simulation)

Under the Bootstrap + 1.9% Quarterly (~90 days) Decay model, block rewards and supply growth progress as follows:

* **Graduated Bootstrap Phase (Blocks 2 - 9,999):**
  - **Blocks 2 – 2,200:** **40 KRISTA** per block (`87,960 KRISTA`)
  - **Blocks 2,201 – 5,000:** **30 KRISTA** per block (`84,000 KRISTA`)
  - **Blocks 5,001 – 9,999:** **20 KRISTA** per block (`99,980 KRISTA`)
  - Total Bootstrap Production: **271,940 KRISTA** *(271,955 KRISTA including 15 KRISTA Genesis)*
* **Year 1 (Periods 0-3, Blocks 10,000 - 1,046,799):** **15 KRISTA** decaying 1.9% every 259,200 blocks
  - Average Block Reward: **14.44 KRISTA**
  - Annual Production: **18,715,182.85 KRISTA**
  - Cumulative Supply at Year End: **18,987,122.85 KRISTA**
* **Year 2 (Periods 4-7, Blocks 1,046,800 - 2,083,599):**
  - Average Block Reward: **13.37 KRISTA** (1.9% quarterly reduction steps)
  - Annual Production: **13,732,027.76 KRISTA**
  - Cumulative Supply at Year End: **32,719,150.61 KRISTA**
* **Year 3 (Periods 8-11, Blocks 2,083,600 - 3,120,399):**
  - Average Block Reward: **12.36 KRISTA**
  - Annual Production: **12,028,806.91 KRISTA**
  - Cumulative Supply at Year End: **44,747,957.52 KRISTA**
* **Year 5 (Periods 16-19, Blocks 4,157,200 - 5,193,999):**
  - Average Block Reward: **10.53 KRISTA**
  - Annual Production: **10,217,998.66 KRISTA**
  - Cumulative Supply at Year End: **68,123,767.48 KRISTA**
* **Year 10 (Periods 36-39, Blocks 9,341,200 - 10,377,999):**
  - Average Block Reward: **7.16 KRISTA**
  - Annual Production: **7,164,158.41 KRISTA**
  - Cumulative Supply at Year End: **111,706,513.54 KRISTA**
* **Infinite Horizon:** The total circulating supply asymptotes to **204,903,519 KRISTA**, safely under the absolute hard cap of **210,000,000 KRISTA**, leaving a healthy ~5.10M buffer and ensuring block rewards can continue for over 50 years without sudden shocks.

```
Supply Saturation Projection Graph:
[0] (Genesis) -> [18.99M] (Year 1) -> [32.72M] (Year 2) -> [44.75M] (Year 3) -> [68.12M] (Year 5) -> [204.90M] (Asymptote)
```

---

## 5. Why This Model Makes KRISTA Reputable

1. **Deflationary Structure:** Having the total supply locked at a reputable limit like 210 Million ensures that the unit value increases in the long term. The actual supply asymptotes around 204.90M, making it even more scarce.
2. **High Lock-up Rate:** Setting the Masternode collateral to a flat 4,200 KRISTA enables a massive number of active masternodes (20+ for quorums, scaling upwards), locking up circulating supply and narrowing exchange liquidity to drive price appreciation.
3. **Secure PoW/PoS:** Directing 55% of net rewards (15% producer + 40% participants, including a dedicated 20% sub-pool [8% for 1 Coin-Lock + 4% each for 3 Open PoW] for the 4 independent miners) to active block producers and validators incentivizes hardware hashpower, wallet uptime, and network participation, decentralizing security.

---

## 6. Code-Level Changes

The finalized implementation in the code is as follows:

1. **Setting the Maximum Supply Limit:**
   Set `consensus.nMaxMoneyOut = 210000000 * COIN;` (210M) in [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp).
2. **Updating Block Reward Logic:**
   Update the `GetBlockValue` function in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) to support the bootstrap phase and 1.9% quarterly (~90 days) decay:
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

       if (nHeight < 10000) {
           if (nHeight == 0) {
               return 15 * COIN;
           }
           if (nHeight <= 2200) {
               return 40 * COIN; // Bootstrap Phase 1 (Blocks 2..2,200)
           }
           if (nHeight <= 5000) {
               return 30 * COIN; // Bootstrap Phase 2 (Blocks 2,201..5,000)
           }
           return 20 * COIN;     // Bootstrap Phase 3 (Blocks 5,001..9,999)
       }

        // ~90 günde bir %1.9 azalma (Decay) - Her 259.200 blokta bir
       int period = (nHeight - 10000) / 259200;
       double subsidy = 15.0 * pow(0.981, period);
       CAmount nSubsidy = (CAmount)(subsidy * COIN + 0.5);

       if (nMoneySupply + nSubsidy > maxMoneyOut) {
           return maxMoneyOut - nMoneySupply;
       }

       return nSubsidy;
   }
   ```
3. **Updating Split Ratios:**
   Update the `GetMasternodePayment` function in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) to return the correct reward distributions:
    ```cpp
    CAmount CMasternode::GetMasternodePayment(int nHeight)
    {
        // Model D activates at block 2,200 on Mainnet and Testnet (200 on Regtest).
        // When active, it overrides the legacy bootstrap rules below.
        if (IsModelDActive(nHeight)) {
            CAmount nBlockValActual = CMasternode::GetBlockValue(nHeight);
            CAmount nTreasurySplit = (nHeight > 1) ? (nBlockValActual * 7 / 100) : 0;
            CAmount nFaucetSplit = (nHeight > 1 && nHeight <= 50000) ? (nBlockValActual * 7 / 1000) : 0;
            CAmount nBlockVal = nBlockValActual - nTreasurySplit - nFaucetSplit;
            return nBlockVal * 35 / 100; // %35 MN passive pay (Model D) after Treasury & Faucet splits
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

## 7. Developer Treasury and Bootstrap Faucet Splits

To secure ecosystem funding and facilitate new user onboarding, a block reward split mechanism is active since block 2 on Mainnet:

* **Developer Treasury (7%)**:
  - **Deduction**: 7% of the block reward is automatically allocated to the Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`).
  - **Scope**: Applies to all blocks starting from block height 2. Block height 1 (premine) is exempt.
* **Bootstrap Faucet (0.7%)**:
  - **Deduction**: 0.7% of the block reward is allocated to the Bootstrap Faucet Address (`KThn5Li5BnfGzbNJ92UDH7FL9CZR1VSRGEh`).
  - **Scope**: Active for blocks 2 through 50,000. Block height 1 (premine) is exempt.

These splits are deducted directly from the block value, reducing the remaining validator/masternode reward pool accordingly (e.g., from 40 KRISTA to 36.92 KRISTA for blocks 2–2,200, from 30 KRISTA to 27.69 KRISTA for blocks 2,201–5,000, from 20 KRISTA to 18.46 KRISTA for blocks 5,001–9,999, and from 15 KRISTA to 13.845 KRISTA for blocks 10,000–50,000).
