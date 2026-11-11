# KRISTA Blockchain: Hard Cap & Economic Security Analysis Report (Revised Final Version)

This report has been revised in light of the newly decided **210 Million KRISTA** maximum supply limit (Hard Cap), **1.9% quarterly (~90 days) decay** rate, **4,200 KRISTA** flat masternode collateral, and the updated bootstrap model. This new structure aims to maximize block reward longevity while maintaining the network's scarcity perception at the most secure scale.

---

## 1. Executive Summary

The new tokenomics parameters chosen for the KRISTA blockchain follow global standards like Litecoin (84M) and Bitcoin (21M), establishing high investor confidence while spreading the reward lifecycle over many decades:
1. **Selected Hard Cap:** **210,000,000 (210 Million) KRISTA** (exactly 10 times that of Bitcoin).
2. **Extended Reward Lifecycle (1.9% Decay):** The quarterly (~90 days) decay rate has been reduced from 5% to **1.9%**. As a result, block rewards decline much slower, and **block rewards remain active for 50+ years**.
3. **Masternode Collateral:** Updated to a flat **4,200 KRISTA** in proportion to the supply.
4. **Graduated Bootstrap Reinforcement:** To both prevent quorum deadlocks and eliminate 72.8% of early bootstrap inflation overhang, the 10,000-block bootstrap reward follows a graduated **40 -> 30 -> 20 KRISTA** schedule aligned with protocol activation heights.

---

## 2. Tokenomics Model Comparison Matrix

The details of the newly designed 210M model are summarized below:

| Parameter / Metric | Old Model (100M Baseline) | Newly Selected Model (210M - Long Lifecycle) |
| :--- | :---: | :---: |
| **Maximum Supply (Hard Cap)** | 100,000,000 KRISTA | **210,000,000 KRISTA** |
| **Bootstrap Block Reward** | 50 KRISTA | **40 -> 30 -> 20 KRISTA** (10k blocks graduated) |
| **Bootstrap Total Emission** | 499,900 KRISTA | **271,940 KRISTA** (~0.13% of the network) |
| **Starting Block Reward (Post-Bootstrap)** | 19.0 KRISTA | **15 KRISTA** |
| **Period Decay Rate** | 5.00% | **1.90% (Every ~90 days)** |
| **Asymptotic Limit (Actual Supply)** | 98,995,900 KRISTA | **204,903,519 KRISTA** |
| **Ratio to Hard Cap (%)** | 99.00% | **97.57%** |
| **Buffer / Gap Reserve** | 1,004,100 KRISTA | **5,096,481 KRISTA** |
| **Masternode Collateral** | 250 KRISTA | **4,200 KRISTA** |
| **Time to 90% Saturation** | 11.5 Years | **33.3 Years** *(30.0 yrs to 90% of asymptote)* |
| **Time to 95% Saturation** | 15.5 Years | **47.4 Years** *(39.0 yrs to 95% of asymptote)* |

> [!TIP]
> **Gap Reserve Advantage:** The **5.10 Million KRISTA** difference between the 204.90M asymptote limit and the 210M hard cap guarantees that block rewards will never hit a strict cutoff shock, allowing a smooth transition for the network to rely on transaction fees.

---

## 3. Long-Term Time Saturation and Supply Simulation

The supply and reward distribution of the network over a 100-year period based on the new 210M parameters (verified against `CMasternode::GetBlockValue` 128-bit integer decay):

* **Year 0 (First 10,000 Blocks / Graduated Bootstrap):**
  - Block Reward: **Blocks 0–1: 0 KRISTA | Blocks 2–2,200: 40 KRISTA | Blocks 2,201–5,000: 30 KRISTA | Blocks 5,001–9,999: 20 KRISTA**
  - Circulating Supply: **271,940 KRISTA** *(Blocks 0 and 1 mint 0 KRISTA on Mainnet for fair launch)*
  - *Masternode Impact:* At block 2,200 (Model D activation), there are **87,960 KRISTA** (`81,187.08 KRISTA` net after 7.0% Treasury and 0.7% Faucet) in circulation, supporting up to **19 active masternodes** under the 4,200 KRISTA collateral requirement (~3x the 7 MN committee and 5 MN LLMQ quorum thresholds), scaling to **59 masternodes** (`250,999.92 KRISTA` net) by block 10,000. Both quorum deadlocks and early inflation overhang are prevented.
* **End of Year 1 (Block 1,046,799):**
  - Block Reward (Period 3): **14.16 KRISTA** (slow decay)
  - Circulating Supply: **15,386,296 KRISTA** (7.33% of Hard Cap)
* **End of Year 2 (Block 2,083,599):**
  - Block Reward (Period 7): **13.12 KRISTA**
  - Circulating Supply: **29,384,285 KRISTA** (13.99% of Hard Cap)
* **End of Year 5 (Block 5,193,999):**
  - Block Reward (Period 19): **10.42 KRISTA**
  - Circulating Supply: **65,474,618 KRISTA** (31.18% of Hard Cap)
* **End of Year 10 (Block 10,377,999):**
  - Block Reward (Period 39): **7.10 KRISTA** (`7.09876087 KRISTA`)
  - Circulating Supply: **109,901,475 KRISTA** (52.33% of Hard Cap)
* **End of Year 20 (Block 20,745,999):**
  - Block Reward (Period 79): **3.30 KRISTA** (`3.29566335 KRISTA`)
  - Circulating Supply: **160,797,969 KRISTA** (76.57% of Hard Cap)
* **Year 33.3 (Block 34,529,303):**
  - **90% Hard Cap Milestone:** Circulating supply reaches **189,000,000 KRISTA** (90.00% of Hard Cap), and the block reward declines to **1.17 KRISTA** *(90% of the 204.90M asymptote, `184,413,167 KRISTA`, is reached earlier at Year 30.0 / Block 31,104,879 with a `1.53 KRISTA` block reward)*.
* **Year 47.4 (Block 49,115,726):**
  - **95% Hard Cap Milestone:** Circulating supply reaches **199,500,000 KRISTA** (95.00% of Hard Cap), and the block reward declines to **0.40 KRISTA** *(95% of the 204.90M asymptote, `194,658,343 KRISTA`, is reached at Year 39.0 / Block 40,470,882 with a `0.75 KRISTA` block reward)*.
* **End of Year 100 (Block 103,689,999):**
  - Block Reward (Period 399): **~0.01 KRISTA** (`0.00711904 KRISTA`)
  - Circulating Supply: **204,808,332 KRISTA** (97.53% of Hard Cap, 99.95% of Asymptote)

---

## 4. Economic Security and Security Budget Rationales

1. **Ultra-Long-Term Incentives (Sustainability):** The reward decay timeline is extended 3x compared to the old model, guaranteeing miner and staker participation for decades.
2. **Scarcity and Reputation Protection:** The 210M supply limit is in line with reputable projects like Bitcoin and Litecoin. It avoids dilution of unit value.
3. **Quorum Stability & Dump Prevention:** The flat 4,200 KRISTA collateral requirement, paired with the graduated bootstrap reward (`40 -> 30 -> 20 KRISTA`), provides 19 masternode capacity at block 2,200 and 59 masternode capacity by block 10,000, stabilizing LLMQ and Model D structures while preventing excess liquid supply overhang.
