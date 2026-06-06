KristaTech (KRISTA) Core
========================

KristaTech (KRISTA) is a decentralized cryptocurrency utilizing a hybrid consensus model that combines Proof of Work (PoW), Proof of Stake (PoS), and the advanced **ADAM (A Decentralized Approach Model)** and **MPA (Multi-Proof-Algorithm)** cooperative consensus systems.

### Coin Specs

* **Ticker**: KRISTA
* **Primary PoW Algorithm**: X11KVS
* **Premine**: 30,000,000 KRISTA
* **PoW Blocks**: 1 - 1000
* **ADAM Consensus Activation**: Block 200+ (cooperative puzzle verification and coordinator signing)
* **MPA Consensus Activation**: Block 1000+ (version 12 blocks, Proof of Lock/Burn/Masternode)
* **PoS Blocks**: Starting from 1001
* **Block Time**: 30 Seconds
* **Maturity**: 100 Confirmations
* **Prefix**: KRISTA addresses start with the capital letter **K**
* **Mainnet Ports**: 27999 (p2p) / 27979 (rpc)
* **Testnet Ports**: 27989 (p2p) / 27919 (rpc)

* **Explorer**: [explorer.kristalteknoloji.com](https://explorer.kristalteknoloji.com)
* **Website**: [krista.kristalteknoloji.com](https://krista.kristalteknoloji.com)

---

### Consensus Protocols

#### 1. ADAM Consensus (A Decentralized Approach Model)
Starting at block height **200**, the network transition to ADAM consensus. Under this model:
* **Elected Miners**: For each block, 13 miners are deterministically selected from the Masternode pool based on a Verifiable Random Function (VRF) rolling seed.
* **Multi-Algorithm Hashing**: Each miner solves a partial puzzle using a different cryptographic algorithm determined by its election index (`minerIndex % 13`):
  0. `blake` | 1. `bmw` | 2. `groestl` | 3. `jh` | 4. `keccak` | 5. `skein` | 6. `luffa` | 7. `cubehash` | 8. `shavite` | 9. `simd` | 10. `echo` | 11. `X11KVS` | 12. `DoubleSHA256`
* **Elected Coordinator**: A single coordinator is elected per block round to compile transactions, gather miner solutions, verify that the quorum threshold of at least 10 valid signatures is met, sign the final header, and publish the block.
* **Difficulty Scaling**: The main block target is scaled down by 12 bits (`<< 12`) on Mainnet for partial puzzles to prevent CPU mining starvation and allow instant coordinator verification.

#### 2. MPA (Multi-Proof-Algorithm) & PoMBL
Starting at block height **1000** (or height 300 on Regtest), the network enforces Version 12 block structures supporting Multi-Proof Consensus:
* **Proof of Lock (PoL)**: Rewards are calculated based on locked coins via CLTV/CSV timelocks.
* **Proof of Burn (PoB)**: Tracks coin-burn proof with mathematical decay.
* **Proof of Masternode (PoM)**: Evaluates active lifetime weights of nodes.
* **Compliance Restrictions**: Addresses designated for burning are prohibited from spending, ensuring locked token mechanics.

---

### MESCAL Smart Contracts

The KristaTech Core wallet features the **MESCAL (KristaTech Smart Contract)** engine with a visual designer widget to draft, compile, and publish contract scripts. Standard templates include:
* **Time-Locked Deposit**: Locks funds until a specific block height or timestamp.
* **Escrow Multi-Signature (2-of-3)**: Requires agreement between buyer, seller, and a mediator.
* **Hash-Locked Claim**: Locks funds requiring a preimage secret key to spend.
* **Dead Man's Switch (Inheritance)**: Delays heir spends unless the owner's signature refresh is missed.
* **2-Factor Authentication (2FA) Wallet**: Multi-sig security for daily operations.
* **Hash Time-Locked Swap (HTLC)**: Secure cross-chain atomic swaps.
* **Multi-Path Security Recovery**: Multi-sig recovery with backup keys.
* **Tokenized Asset (Escrow & Compliance)**: Multi-path escrow designed for RWA tokenization (2-of-3 escrow with a mediator, with dispute/compliance recovery timeout for the seller).

---

### Rewards Breakdown

| Block | Phase | Collateral | Block Reward | MN Reward % | Staking Reward % | MN Reward | Staker/Miner Reward |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Premine | - | 30,000,000 KRISTA | - | - | - | 30,000,000 KRISTA |
| 2 - 1000 | PoW | - | 100 KRISTA | 0% | 100% | 0 KRISTA | 100 KRISTA |
| 1001 - 5000 | PoS | 15,000 KRISTA | 100 KRISTA | 0% | 100% | 0 KRISTA | 100 KRISTA |
| 5001 - 100000 | PoS | 15,000 KRISTA | 100 KRISTA | 95% | 5% | 95 KRISTA | 5 KRISTA |
| 100001 - 200000 | PoS | 17,500 KRISTA | 125 KRISTA | 95% | 5% | 118.75 KRISTA | 6.25 KRISTA |
| 200001 - 300000 | PoS | 20,000 KRISTA | 150 KRISTA | 95% | 5% | 142.5 KRISTA | 7.5 KRISTA |
| 300001 - 400000 | PoS | 20,000 KRISTA | 125 KRISTA | 95% | 5% | 118.75 KRISTA | 6.25 KRISTA |
| 400001+ | PoS | 20,000 KRISTA | 100 KRISTA | 95% | 5% | 95 KRISTA | 5 KRISTA |
