# KRISTA vs DASH: Architecture and Protocol Comparison

This document provides a detailed technical comparison between the **KristaTech (KRISTA)** blockchain protocol and the **Dash Core** protocol. While KRISTA inherits core infrastructure elements from Dash, it introduces major paradigm shifts in consensus, mining model, smart contracts, and treasury governance.

---

## Technical Comparison Table

| Feature | Dash Core (v23.1.x) | KRISTA (Current Implementation) |
| :--- | :--- | :--- |
| **Consensus Engine** | Proof of Work (X11) + LLMQ ChainLocks | ADAM (Cooperative Lightweight PoW) + Proof of Stake (MPA) |
| **Block Production Model** | Competitive Mining Race (ASIC-dominated) | Cooperative Pool & Selection (SSLE) |
| **Block Time** | ~2.5 minutes | 30 seconds |
| **ASIC Resistance** | None (X11 ASICs dominate the network) | Comprehensive Architecture-level Resistance (Permuted Hash Chain) |
| **Masternode Collateral** | **1000 DASH** (4000 DASH for Evonodes) | **4200 KRISTA** (Unified across network) |
| **Quorum Signature Type** | BLS Threshold Signature | Individual ECDSA Signature List + BLS12-381/ECDSA Fallback |
| **Treasury Allocation** | 20% of block reward (Superblock Bounties) | System-level locked `DeveloperFund` (Dual-Authorization) |
| **Smart Contracts** | None (Data schemas only on Platform layer) | **MESCAL** (Secure stack-based JSON templates) |
| **Governance Locks** | None (Voted proposal payees spend directly) | Enforce start height + Spork and LLMQ Dual-Signature authorization |
| **Network Upgrades** | Sporks + BIP9/BIP135 Hard Forks | Version upgrades + Spork 21 (`SPORK_21_ADAM_STANDARD_MODE`) |
| **Energy Consumption** | High (Continuous competitive global PoW) | Ultra-Low (Only elected block slot miners perform work) |

---

## Detailed Architectural Divergences

### 1. Consensus & Block Validation (ADAM vs. X11 + ChainLocks)
* **Dash Core:** Employs the X11 hashing algorithm (a chain of 11 different hashing algorithms). Mining is fully competitive: whoever computes a block header hash below the current difficulty target wins the block reward. Active Masternode Quorums (LLMQs) monitor the network and sign blocks using BLS threshold signatures to lock them (ChainLocks), which prevents 51% reorganization attacks but doesn't change the competitive, energy-heavy base mining layer.
* **KRISTA:** Replaces competitive mining with the **ADAM (A Decentralized Approach Model)** algorithm combined with **Proof-of-Stake (PoS)**. At each block height, a deterministic Single Secret Leader Election (SSLE) selects a pool of 11-14 miners and a coordinator from active masternodes/registered miners. 
  * Selected miners solve lightweight parallel puzzles.
  * The final block header hash is calculated by sequentially chaining the hashing operations of all elected miners (Stateless Hashing Chain).
  * The block is locked via dual signatures: the Staker's PoS signature and the Coordinator's signature.

### 2. Mining Decentralization & ASIC Resistance
* **Dash Core:** The X11 hashing sequence is static. Once Dash's valuation made it profitable, ASIC manufacturers developed dedicated chips, leading to full hash power centralization in industrial mining farms.
* **KRISTA:** The hashing algorithms required for mining are dynamic. Under **ADAM Standard Mode (Version 12)**, a 3-permutation selection scheme is applied per miner per block:
  * Out of 18 available energy-efficient algorithms, 3 are selected based on the previous block hash and the miner's public key.
  * The puzzle calculation compounds these algorithms recursively.
  * Since the required hashing pipeline changes dynamically for every miner in every block slot, static ASIC pipeline architectures cannot achieve any speed or efficiency advantage.

### 3. Masternode Collateral & Quorums
* **Dash Core:** Standard masternodes require **1000 DASH** as collateral. High-performance "Evonodes" (which run the Dash Platform second-layer data network) require **4000 DASH**.
* **KRISTA:** Masternode collateral is set to **4200 KRISTA**. Instead of separate node tiers, all masternodes run the unified second layer, participating in block consensus validation, leader election pools, and transaction routing.

### 4. Developer Treasury Governance
* **Dash Core:** Superblock payments are determined by proposal voting. Masternodes vote on proposals, and once a proposal is passed, the funds are paid directly to the payee address during the Superblock.
* **KRISTA:** Implements a system-level secure **Developer Fund** address. Spending from the Developer Fund address is strictly controlled at the consensus layer (enforced in `ConnectBlock` and `CheckInputs`):
  * **Time-Lock:** Spends are blocked before block height 2880 on Mainnet (200 on Testnet).
  * **Dual-Signature Authorization:** The transaction must contain a valid LLMQ Quorum Signature (verifying that the active masternode quorum approved the spend) and must be signed by the private key matching the network's administrative Spork Public Key.

### 5. Smart Contracts (MESCAL)
* **Dash Core:** Dash Core does not support smart contracts on the base layer. Dash Platform provides a decentralized document and data store, but it does not execute smart contract logic.
* **KRISTA:** Features the **MESCAL** (Machine-parsable JSON representations of CScript) smart contract engine. Contracts are written as JSON templates representing Bitcoin-style script commands. This allows developers to create secure, re-entrancy-free, and stateless contracts for corporate governance, escrow, DeFi, and tokenization directly on the base layer.
