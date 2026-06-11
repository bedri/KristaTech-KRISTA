KristaTech (KRISTA) Core
========================

### Coin Specs

* **Ticker**: KRISTA
* **PoW Algorithm**: X11KVS (with 13-algo dynamic puzzle mining under ADAM consensus)
* **Premine**: 0 KRISTA (No Premine)
* **PoW Only Blocks**: 1 - 199
* **Hybrid PoS/PoW Blocks**: Starting from 200 (ADAM multi-algo PoW alongside PoS)
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
* **MPA (Multi-Proof Algorithm) & PoMBL (Proof of Masternode, Burn and Lock) Consensus (Block 2000+)**: Proof of Lock, Proof of Burn, and Proof of Masternode consensus. See [MPA Consensus Guide](doc/MPA_Consensus.md).
* **Proof of BLS (PoBLS) Consensus**: A lightweight cryptographic lottery model with temporary BLS keys for fair and secure block production, integrated with ADAM and Model D reward splits. See [PoBLS Consensus Guide](doc/PoBLS_Consensus.md) or [Turkish Guide (TR)](doc/PoBLS_Consensus_TR.md).
* **MESCAL Smart Contracts**: Script-based smart contracts with visual design templates, including escrow, recovery, and real-world asset tokenization. See [Tokenized Assets Study](doc/Tokenized_Assets_Study.md).
* **Tokenomics Model**: Strict 100M Hard Cap with 10k block bootstrap (50 KRISTA) followed by 14.5 KRISTA decaying 20% annually, split 60% MN / 40% Miner-Staker. See [Tokenomics Study](doc/Tokenomics_Study.md) (or [Turkish version](doc/Tokenomics_Study_TR.md)).

---

### Rewards Breakdown

| Block Range | Phase | Collateral | Block Reward | MN % (Passive / Active LLMQ) | Miner-Staker % (BP / Participants) | MN Reward (Total) | Miner-Staker Reward (Total) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Genesis (No Premine) | - | 0 KRISTA | - | - | - | 0 KRISTA |
| 2 - 5,000 | Early Bootstrap | 5,000 KRISTA | 50 KRISTA | 0% | 100% | 0 KRISTA | 50 KRISTA + fees |
| 5,001 - 9,999 | Late Bootstrap (Model D) | 5,000 KRISTA | 50 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 30 KRISTA | 20 KRISTA + fees |
| 10,000 - 100,000 | MN Accumulation (Year 0) | 5,000 KRISTA | 14.5 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.7 KRISTA | 5.8 KRISTA + fees |
| 100,001 - 1,061,200 | Model D Hybrid (Year 1) | 5,000 KRISTA | 14.5 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.7 KRISTA | 5.8 KRISTA + fees |
| 1,061,201 - 2,112,400 | Model D Hybrid (Year 2) | 5,000 KRISTA | 11.6 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 6.96 KRISTA | 4.64 KRISTA + fees |
| 2,112,401 - 3,163,600 | Model D Hybrid (Year 3) | 5,000 KRISTA | 9.28 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 5.57 KRISTA | 3.71 KRISTA + fees |
| 3,163,601 - 10,522,000 | Model D Hybrid (Years 4-10) | 5,000 KRISTA | Decays 20% / yr | 60% (50% / 10%) | 40% (15% / 25%) | 60% of reward | 40% of reward + fees |
| 10,522,001+ (100M Cap) | Fees Only | 5,000 KRISTA | 0 KRISTA | - | - | 0 KRISTA | 100% of tx fees |

> [!NOTE]
> **Developer Treasury & Bootstrap Faucet Splits (Active since block 2)**:
> - **Developer Treasury (%7)**: 7% of the block reward is sent to the Developer Fund Address (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`) for ecosystem development (applies to all blocks except block 1).
> - **Bootstrap Faucet (%0.7)**: 0.7% of the block reward is sent to the Bootstrap Faucet Address (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`) for blocks 2 through 50,000 (applies to all blocks except block 1).
> - **Genesis Block (Block 1)**: Block 1 has a reward of 0 KRISTA (No Premine) and is completely exempt from these splits.

> [!NOTE]
> **Model D Reward Splits (Active since block 1,200 on Mainnet / block 500 on Testnet)**:
> - **MN Passive Payee (%50)**: Paid to the masternode next in the global payment queue.
> - **Active LLMQ Quorum (%10)**: Split equally among the active masternodes verifying and signing PoBLS tickets in the round.
> - **Block Producer / Winner (%15)**: Earned by the validator/staker who wins the lottery/staking to produce the block.
> - **Validator Participants (%25)**: Shared equally among the candidate validator nodes in the elected validator set (shared by 10 miners in PoS blocks, and 11 miners in PoW blocks).

---

### Network & GUI Stability Improvements (June 2026 Updates)

To support robust network operation and accurate UI/UX display under hybrid consensus load, the following stability and protocol-level improvements have been integrated:

* **GUID-Based Duplicate Connection Mitigation**: Peers now generate and exchange a unique `nLocalNodeGUID` during the initial connection handshake. If a duplicate connection from the same GUID is detected, a deterministic tie-breaking logic (larger GUID keeps outbound, terminates inbound) closes the redundant channel, ensuring the active peer count matches the unique physical nodes exactly.
* **Masternode Sync Flags Reset**: Running `mnsync reset` now correctly clears the asked-flags maps (`mWeAskedForMasternodeList` and `mAskedUsForMasternodeList`) in the masternode manager, preventing nodes from getting locked out of syncing due to rate-limiting on private networks.
* **Protocol Inventory Type Mapping Fix**: Corrected the inventory commands mapping (`CInv::GetCommand()`) for `MSG_MASTERNODE_ANNOUNCE` and `MSG_MASTERNODE_PING` to their corresponding network message strings, restoring proper P2P propagation of masternode database updates and active status pings.
* **Masternode Reward UI Display**: Refined transaction decomposition (`decomposeCoinBase()`) to properly identify masternode rewards/splits at output indices >= 1 in coinbase transactions, ensuring zero-value rewards (such as at block heights <= 5000) display as "Masternode Reward" rather than "No information".
* **Safe Datadir Initialization**: Fixed a null-pointer dereference in `GetDataDir()` where base params were requested before configuration parsing completed. A fallback non-cached path is now returned until parameters are configured, preventing pointer-named folder pollution in the repository root.
