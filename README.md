KristaTech (KRISTA) Core
========================

### Coin Specs

* **Ticker**: KRISTA
* **PoW Algorithm**: X11KVS (with 13-algo dynamic puzzle mining under ADAM consensus)
* **Premine**: 30,000,000 KRISTA
* **PoW Only Blocks**: 1 - 1000
* **Hybrid PoS/PoW Blocks**: Starting from 1001 (ADAM multi-algo PoW alongside PoS)
* **Block Time**: 30 Seconds
* **Maturity**: 100 Confirmations
* **Prefix**: KRISTA addresses start with the capital letters **KT**
* **Mainnet Ports**: 27999 (p2p) / 27979 (rpc)
* **Testnet Ports**: 27989 (p2p) / 27919 (rpc)

* **Explorer**: [explorer.kristalteknoloji.com](https://explorer.kristalteknoloji.com)
* **Website**: [krista.kristalteknoloji.com](https://krista.kristalteknoloji.com)

---

### Advanced Consensus & Features

KristaTech implements next-generation hybrid consensus models, smart contract capabilities, and stable tokenomics:

* **ADAM (A Decentralized Approach Model) Consensus (Block 200+)**: Cooperative VRF-based multi-algorithm puzzle mining. See [ADAM Consensus Guide](doc/ADAM_Consensus.md).
* **MPA (Multi-Proof Algorithm) & PoMBL (Proof of Masternode, Burn and Lock) Consensus (Block 1000+)**: Proof of Lock, Proof of Burn, and Proof of Masternode consensus. See [MPA Consensus Guide](doc/MPA_Consensus.md).
* **Proof of BLS (PoBLS) Consensus**: A lightweight cryptographic lottery model with temporary BLS keys for fair and secure block production, integrated with ADAM and Model D reward splits. See [PoBLS Consensus Guide](doc/PoBLS_Consensus.md) or [Turkish Guide (TR)](doc/PoBLS_Consensus_TR.md).
* **MESCAL Smart Contracts**: Script-based smart contracts with visual design templates, including escrow, recovery, and real-world asset tokenization. See [Tokenized Assets Study](doc/Tokenized_Assets_Study.md).
* **Tokenomics Model**: Strict 100M Hard Cap with 20% annual decay and 60% MN / 40% Miner-Staker split. See [Tokenomics Study](doc/Tokenomics_Study.md) and [Implementation Plan](doc/Tokenomics_Implementation_Plan.md).

---

### Rewards Breakdown

| Block Range | Phase | Collateral | Block Reward | MN % (Passive / Active LLMQ) | Miner-Staker % (BP / Participants) | MN Reward (Total) | Miner-Staker Reward (Total) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Premine | - | 30,000,000 KRISTA | - | - | - | 30,000,000 KRISTA |
| 2 - 5000 | Bootstrap | 20,000 KRISTA | 14.5 KRISTA | 0% | 100% | 0 KRISTA | 14.5 KRISTA + fees |
| 5001 - 100,000 | MN Accumulation | 20,000 KRISTA | 14.5 KRISTA | 80% (80% / 0%) | 20% (20% / 0%) | 11.6 KRISTA | 2.9 KRISTA + fees |
| 100,001 - 1,051,200 | Model D Hybrid | 20,000 KRISTA | 14.5 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.7 KRISTA | 5.8 KRISTA + fees |
| Year 2 | Model D Hybrid | 20,000 KRISTA | 11.6 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 6.96 KRISTA | 4.64 KRISTA + fees |
| Year 3 | Model D Hybrid | 20,000 KRISTA | 9.28 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 5.568 KRISTA | 3.712 KRISTA + fees |
| Year 4 - 10 | Model D Hybrid | 20,000 KRISTA | Decays 20% / yr | 60% (50% / 10%) | 40% (15% / 25%) | 60% of reward | 40% of reward + fees |
| Year 11+ (100M Cap) | Fees Only | 20,000 KRISTA | 0 KRISTA | - | - | 0 KRISTA | 100% of tx fees |

> [!NOTE]
> **Model D Reward Splits (Blocks >= 100,001)**:
> - **MN Passive Payee (%50)**: Paid to the masternode next in the global payment queue.
> - **Active LLMQ Quorum (%10)**: Split equally among the active masternodes verifying and signing PoBLS tickets in the round.
> - **Block Producer / Winner (%15)**: Earned by the validator/staker who wins the lottery/staking to produce the block.
> - **Validator Participants (%25)**: Shared equally among the candidate validator nodes in the elected validator set (shared by 12 miners in PoS blocks, and 13 miners in PoW blocks).
