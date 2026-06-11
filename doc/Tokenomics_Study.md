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
| **PIVX (PIVX)** | PoS/Masternode | Unlimited (Dynamic Deflation) | Constant 5 PIVX per block (MN/Staker dynamic) | Variable (Usually 60% MN / 40% Staker) |

---

## 3. Proposed KRISTA Tokenomics Model

In order for KRISTA to protect both infrastructure providers (Masternodes) and network security (Staking) at the maximum level, and to remain a **reputable digital asset**, the following model is proposed:

### 3.1. Limited Maximum Supply (Hard Cap)
* **Proposed Hard Cap:** **100,000,000 (100 Million) KRISTA**
* **Premine:** **0 KRISTA** (No Premine)
* **Circulation to be Distributed via Mining/Staking:** **100,000,000 KRISTA** (100%)

### 3.2. Annual Emission Reduction (Decay Model)
The emission program uses a **20% annual decay** model with a starting reward of **14.5 KRISTA** (following an initial 10,000 block bootstrap phase). This model offers a smoother and more predictable transition instead of Bitcoin's harsh 4-year halving shocks.

### 3.3. Balancing Masternode & Miner-Staker Reward Distribution
Block reward distribution is optimized to incentivize both PoW miners and PoS stakers under the Model D hybrid split:
* **Blocks 2 - 2,199 (Early Stage / PoW-PoS Hybrid):** 100% Miner/Staker (to ensure network security and hash power while masternodes are being set up).
* **Blocks 2,200 - 5,000 (Model D Early Stage):** Model D splits are active (50% passive MN, 10% active LLMQ, 25% participants, 15% block winner). Since `GetMasternodePayment` is 0 for blocks <= 5000, the passive MN payee is not enforced by voting/consensus.
* **Blocks 5,001+ (Maturation Period):** **60% Masternode / 40% Miner-Staker** split is fully active and enforced under Model D (50% passive MN, 10% active LLMQ, 25% participants, 15% block winner).

> [!NOTE]
> Since the network has a dual (hybrid) structure, PoW miners (when a block is produced via PoW) or PoS stakers (when a block is produced via PoS) are continuously incentivized by receiving 40% of the block reward (15% coordinator/producer + 25% participants) and 100% of the transaction fees. The Miner/Staker share never drops to 0%.

### 3.4. Flat Masternode Collateral
To balance network security, validator participation, and hosting ROI, the masternode collateral is locked to a flat **5,000 KRISTA** starting from block 1. This enables rapid bootstrapping of the 20+ active masternodes required for LLMQ quorums.

---

## 4. Mathematical Projection (25-Year Simulation)

Under the Bootstrap + 20% Annual Decay model, block rewards and supply growth progress as follows:

* **Bootstrap Phase (Blocks 2 - 9,999):** **50 KRISTA** per block
  - Total Bootstrap Production: **499,900 KRISTA**
* **Year 1 (Blocks 10,000 - 1,061,199):** **14.5 KRISTA** per block
  - Annual Production: **15,242,400 KRISTA**
  - Cumulative Supply at Year End: **15,742,300 KRISTA**
* **Year 2 (Blocks 1,061,200 - 2,112,399):** **11.6 KRISTA** per block (20% Reduction)
  - Annual Production: **12,193,920 KRISTA**
  - Cumulative Supply at Year End: **27,936,220 KRISTA**
* **Year 3 (Blocks 2,112,400 - 3,163,599):** **9.28 KRISTA** per block
  - Annual Production: **9,755,136 KRISTA**
  - Cumulative Supply at Year End: **37,691,356 KRISTA**
* **Year 4 (Blocks 3,163,600 - 4,214,799):** **7.424 KRISTA** per block
  - Annual Production: **7,804,108.80 KRISTA**
  - Cumulative Supply at Year End: **45,495,464.80 KRISTA**
* **Year 5 (Blocks 4,214,800 - 5,265,999):** **5.9392 KRISTA** per block
  - Annual Production: **6,243,287.04 KRISTA**
  - Cumulative Supply at Year End: **51,738,751.84 KRISTA**
* **Year 10 (Blocks 9,470,800 - 10,521,999):** **1.9462 KRISTA** per block
  - Annual Production: **2,045,800.30 KRISTA**
  - Cumulative Supply at Year End: **68,528,698.81 KRISTA**
* **Infinite Horizon:** The total circulating supply asymptotes to **76,711,900 KRISTA**, well below the absolute hard cap of **100,000,000 KRISTA**.

```
Supply Saturation Projection Graph:
[0] (Genesis) -> [15.74M] (Year 1) -> [27.93M] (Year 2) -> [37.69M] (Year 3) -> [51.73M] (Year 5) -> [76.71M] (Asymptote)
```

---

## 5. Why This Model Makes KRISTA Reputable

1. **Deflationary Structure:** Having the total supply locked at a reputable limit like 100 Million ensures that the unit value increases in the long term. The actual supply asymptotes around 76.71M, making it even more scarce.
2. **High Lock-up Rate:** Setting the Masternode collateral to a flat 5,000 KRISTA enables a massive number of active masternodes (20+ for quorums, scaling upwards), locking up circulating supply and narrowing exchange liquidity to drive price appreciation.
3. **Secure PoS:** Directing 40% of rewards (15% producer + 25% participants) to stakers/validators incentivizes wallet uptime and network participation, decentralizing security.

---

## 6. Code-Level Changes

The finalized implementation in the code is as follows:

1. **Setting the Maximum Supply Limit:**
   Set `consensus.nMaxMoneyOut = 100000000 * COIN;` (100M) in [src/chainparams.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/chainparams.cpp).
2. **Updating Block Reward Logic:**
   Update the `GetBlockValue` function in [src/masternode.cpp](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/src/masternode.cpp) to support the bootstrap phase and 20% annual decay:
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
               return 14.5 * COIN;
           }
           return 50 * COIN; // Bootstrap
       }

       // Yıllık %20 azalma (Decay) - Her 1.051.200 blokta bir
       int year = (nHeight - 10000) / 1051200;
       double subsidy = 14.5 * pow(0.8, year);
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
       if (nHeight <= 5000) return 0;

       if (IsModelDActive(nHeight)) {
           return CMasternode::GetBlockValue(nHeight) * 50 / 100; // %50 MN pasif payı (Model D)
       }

       if (nHeight <= 100000) {
           return CMasternode::GetBlockValue(nHeight) * 80 / 100; // %80 MN, %20 Miner-Staker
       }

       return CMasternode::GetBlockValue(nHeight) * 60 / 100; // %60 MN, %40 Miner-Staker
   }
   ```

---

## 7. Developer Treasury and Bootstrap Faucet Splits

To secure ecosystem funding and facilitate new user onboarding, a block reward split mechanism is active since block 2 on Mainnet:

* **Developer Treasury (7%)**:
  - **Deduction**: 7% of the block reward is automatically allocated to the Developer Fund Address (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`).
  - **Scope**: Applies to all blocks starting from block height 2. Block height 1 (premine) is exempt.
* **Bootstrap Faucet (0.7%)**:
  - **Deduction**: 0.7% of the block reward is allocated to the Bootstrap Faucet Address (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`).
  - **Scope**: Active for blocks 2 through 50,000. Block height 1 (premine) is exempt.

These splits are deducted directly from the block value, reducing the block producer's coinbase reward payout accordingly (e.g., from 50 KRISTA to 46.15 KRISTA for blocks 2–9,999).
