# KRISTA Blockchain: Hard Cap & Economic Security Analysis Report (Revised Final Version)

This report has been revised in light of the newly decided **210 Million KRISTA** maximum supply limit (Hard Cap), **2.15% quarterly (~90 days) decay** rate, **4,200 KRISTA** flat masternode collateral, and the unified **17 KRISTA** fixed-start emission model (eliminating temporary bootstrap reward tiers). This new structure maximizes early liquidity for quorum and masternode formation while preserving the 210M scarcity cap over a 40+ year active tail.

---

## 1. Executive Summary

The new tokenomics parameters chosen for the KRISTA blockchain follow global standards like Litecoin (84M) and Bitcoin (21M), establishing high investor confidence while spreading the reward lifecycle over many decades:
1. **Selected Hard Cap:** **210,000,000 (210 Million) KRISTA** (exactly 10 times that of Bitcoin).
2. **Extended Reward Lifecycle (2.15% Decay):** Starting cleanly from **17 KRISTA** at Block 2, the quarterly (~90 days / 259,200 blocks) decay rate is **2.15%**. As a result, block rewards decline smoothly without arbitrary step-downs, and **block rewards remain active for 45+ years**.
3. **Masternode Collateral:** Updated to a flat **4,200 KRISTA** in proportion to the supply.
4. **Uniform 17 KRISTA Launch & Aligned Model D Activation (Block 5,000):** Eliminates any `40 -> 30 -> 20` step-down shock while minting **84,983 KRISTA** (`79,034.19 KRISTA` net, supporting **18.82 masternodes** at `4,200 KRISTA` each) by Block 5,000 (`UPGRADE_POMBL = 4,800`, `UPGRADE_MODELD = 5,000`).

---

## 2. Tokenomics Model Comparison Matrix

The details of the newly designed 210M model are summarized below:

| Parameter / Metric | Old Model (100M Baseline) | Newly Selected Model (210M - 17 KRISTA Unified) |
| :--- | :---: | :---: |
| **Maximum Supply (Hard Cap)** | 100,000,000 KRISTA | **210,000,000 KRISTA** |
| **Starting Block Reward (Block 2+)** | 50 KRISTA | **17 KRISTA** (No Premine — Blocks 0–1 = 0 KRISTA) |
| **Emission Through Block 5,000** | 250,000 KRISTA | **84,983 KRISTA** (`79,034.19` net = 18.82 MNs) |
| **Period Decay Rate** | 5.00% | **2.15% (Every ~90 days / 259,200 blocks)** |
| **Asymptotic Limit (Actual Supply)** | 98,995,900 KRISTA | **204,948,837 KRISTA** |
| **Ratio to Hard Cap (%)** | 99.00% | **97.59%** |
| **Buffer / Gap Reserve** | 1,004,100 KRISTA | **5,051,163 KRISTA** (`2.41%`) |
| **Masternode Collateral** | 250 KRISTA | **4,200 KRISTA** |
| **Time to 90% Saturation** | 11.5 Years | **29.0 Years** (`365d`) / **29.4 Years** (`4Q`) *(26.1 yrs to 90% asymptote)* |
| **Time to 95% Saturation** | 15.5 Years | **41.2 Years** (`365d`) / **41.7 Years** (`4Q`) *(34.0 yrs to 95% asymptote)* |

> [!TIP]
> **Gap Reserve Advantage:** The **5.05 Million KRISTA** (`5,051,163 KRISTA`) difference between the 204.95M asymptote limit and the 210M hard cap guarantees that block rewards will never hit a strict cutoff shock, allowing a smooth transition for the network to rely on transaction fees.

---

## 3. Long-Term Time Saturation and Supply Simulation

The supply and reward distribution of the network over a 100-year period based on the new 210M parameters (verified against `CMasternode::GetBlockValue` 128-bit integer decay from Block 2, where `period = (nHeight - 2) / 259200`):

* **Launch & Early Consensus Activation (Blocks 0 – 5,000 / Period 0):**
  - Block Reward: **Blocks 0–1: 0 KRISTA | Blocks 2–259,201 (Period 0): 17 KRISTA**
  - Circulating Supply at Block 4,800 (`UPGRADE_POMBL`): **81,583 KRISTA** (`75,872.19 KRISTA` net after the 7.0% Developer Treasury, supporting up to **18 active masternodes** under the `4,200 KRISTA` collateral requirement).
  - Circulating Supply at Block 5,000 (`UPGRADE_MODELD` / `nTreasuryGovernanceStartHeight`): **84,983 KRISTA** (`79,034.19 KRISTA` net, supporting **18.82 masternodes** — ~2.35x the 8 MN Model D committee and 3.8x the 5 MN LLMQ quorum thresholds).
* **End of Year 1 (Block 1,036,801 [4Q] / Block 1,051,199 [365d]):**
  - Block Reward: **15.93 KRISTA** (`Period 3`, 4Q) / **15.58 KRISTA** (`Period 4`, 365d)
  - Circulating Supply: **17,065,278 KRISTA** (`4Q`, 8.13% of Hard Cap) / **17,289,663 KRISTA** (`365d`, 8.23% of Hard Cap)
* **End of Year 2 (Block 2,073,601 [4Q] / Block 2,102,399 [365d]):**
  - Block Reward: **14.60 KRISTA** (`Period 7`, 4Q) / **14.29 KRISTA** (`Period 8`, 365d)
  - Circulating Supply: **32,709,598 KRISTA** (`4Q`, 15.58% of Hard Cap) / **33,121,030 KRISTA** (`365d`, 15.77% of Hard Cap)
* **End of Year 5 (Block 5,184,001 [4Q] / Block 5,255,999 [365d]):**
  - Block Reward: **11.25 KRISTA** (`Period 19`, 4Q) / **11.01 KRISTA** (`Period 20`, 365d)
  - Circulating Supply: **72,251,546 KRISTA** (`4Q`, 34.41% of Hard Cap) / **73,044,021 KRISTA** (`365d`, 34.78% of Hard Cap)
* **End of Year 10 (Block 10,368,001 [4Q] / Block 10,511,999 [365d]):**
  - Block Reward: **7.28 KRISTA** (`Period 39`, 4Q) / **7.13 KRISTA** (`Period 40`, 365d)
  - Circulating Supply: **119,031,925 KRISTA** (`4Q`, 56.68% of Hard Cap) / **120,058,141 KRISTA** (`365d`, 57.17% of Hard Cap)
* **End of Year 20 (Block 20,736,001 [4Q] / Block 21,023,999 [365d]):**
  - Block Reward: **3.05 KRISTA** (`Period 79`, 4Q) / **2.92 KRISTA** (`Period 81`, 365d)
  - Circulating Supply: **168,931,479 KRISTA** (`4Q`, 80.44% of Hard Cap) / **169,790,038 KRISTA** (`365d`, 80.85% of Hard Cap)
* **Year 29.0 (`365d`) / 29.4 (`4Q`) (Block 30,451,596):**
  - **90% Hard Cap Milestone:** Circulating supply reaches **189,000,000 KRISTA** (90.00% of Hard Cap), and the block reward declines to **1.34 KRISTA** *(90% of the 204.95M asymptote, `184,453,953 KRISTA`, is reached at Year 26.1 / Block 27,460,198 with a `1.74 KRISTA` block reward)*.
* **Year 41.2 (`365d`) / 41.7 (`4Q`) (Block 43,259,214):**
  - **95% Hard Cap Milestone:** Circulating supply reaches **199,500,000 KRISTA** (95.00% of Hard Cap), and the block reward declines to **0.46 KRISTA** *(95% of the 204.95M asymptote, `194,701,395 KRISTA`, is reached at Year 34.0 / Block 35,726,730 with a `0.87 KRISTA` block reward)*.
* **End of Year 100 (Block 103,680,001 [4Q] / Block 105,119,999 [365d]):**
  - Block Reward: **0.0029 KRISTA** (`Period 399`, 4Q) / **0.0026 KRISTA** (`Period 405`, 365d)
  - Circulating Supply: **204,914,483 KRISTA** (`4Q`, 97.58% of Hard Cap, 99.98% of Asymptote)

---

## 4. Economic Security and Security Budget Rationales

1. **Ultra-Long-Term Incentives (Sustainability):** Smooth quarterly decay (`2.15%` from Period 0 at `17 KRISTA`) eliminates abrupt bootstrap step-downs while sustaining meaningful block incentives for decades.
2. **Scarcity and Reputation Protection:** The 210M supply limit is in line with reputable projects like Bitcoin and Litecoin and permanently protects scarcity with a `5.05M` gap reserve.
3. **Quorum Stability & Predictable Launch:** The flat `4,200 KRISTA` collateral requirement paired with **17 KRISTA** fixed Block 2+ emission provides **18 masternode capacity** at Block `4,800` (`UPGRADE_POMBL`) and **18.82 masternode capacity** by Block `5,000` (`UPGRADE_MODELD`), stabilizing LLMQ and Model D structures without artificial bootstrap cliffs.
