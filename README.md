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

* **Technical Whitepaper**: Comprehensive design document. See [English Whitepaper](doc/Whitepaper.md) ([PDF Version](doc/Whitepaper.pdf)) or [Turkish Whitepaper](doc/Whitepaper_TR.md) ([PDF Version](doc/Whitepaper_TR.pdf)).
* **ADAM (A Decentralized Approach Model) Consensus (Block 200+)**: Cooperative VRF-based multi-algorithm puzzle mining. See [ADAM Consensus Guide](doc/ADAM_Consensus.md).
* **MPA (Multi-Proof Algorithm) & PoMBL (Proof of Masternode, Burn and Lock) Consensus (Block 2000+)**: Proof of Lock, Proof of Burn, and Proof of Masternode consensus. See [MPA Consensus Guide](doc/MPA_Consensus.md).
* **Proof of BLS (PoBLS) Consensus**: A lightweight cryptographic lottery model with temporary BLS keys for fair and secure block production, integrated with ADAM and Model D reward splits. See [PoBLS Consensus Guide](doc/PoBLS_Consensus.md) or [Turkish Guide (TR)](doc/PoBLS_Consensus_TR.md).
* **MESCAL Smart Contracts**: Script-based smart contracts with visual design templates, including escrow, recovery, and real-world asset tokenization. See [Tokenized Assets Study](doc/Tokenized_Assets_Study.md).
* **Tokenomics Model**: Strict 210M Hard Cap with 10k block bootstrap (100 KRISTA) followed by 15 KRISTA decaying 1.9% quarterly (every 90 days / 259,200 blocks), split 60% MN / 40% Miner-Staker. See [Tokenomics Study](doc/Tokenomics_Study.md) (or [Turkish version](doc/Tokenomics_Study_TR.md)).

---

### Rewards Breakdown

| Block Range | Phase | Collateral | Block Reward | MN % (Passive / Active LLMQ) | Miner-Staker % (BP / Participants) | MN Reward (Total) | Miner-Staker Reward (Total) |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Genesis (No Premine) | - | 0 KRISTA | - | - | - | 0 KRISTA |
| 2 - 2,199 | Early Bootstrap (Model D Inactive) | 2,100 KRISTA | 100 KRISTA | 0% | 100% | 0 KRISTA | 100 KRISTA + fees |
| 2,200 - 9,999 | Late Bootstrap (Model D Active) | 2,100 KRISTA | 100 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 60 KRISTA | 40 KRISTA + fees |
| 10,000 - 269,199 | Quarter 1 (Period 0) | 2,100 KRISTA | 15 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 9.00 KRISTA | 6.00 KRISTA + fees |
| 269,200 - 528,399 | Quarter 2 (Period 1) | 2,100 KRISTA | 14.715 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.829 KRISTA | 5.886 KRISTA + fees |
| 528,400 - 787,599 | Quarter 3 (Period 2) | 2,100 KRISTA | 14.44 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.664 KRISTA | 5.776 KRISTA + fees |
| 787,600 - 1,046,799 | Quarter 4 (Period 3) | 2,100 KRISTA | 14.16 KRISTA | 60% (50% / 10%) | 40% (15% / 25%) | 8.496 KRISTA | 5.664 KRISTA + fees |
| 1,046,800+ | Long-term Decay | 2,100 KRISTA | Decays 1.9% quarterly (every 259.2k blocks) | 60% (50% / 10%) | 40% (15% / 25%) | 60% of reward | 40% of reward + fees |
| After ~205.6M minted | Max Supply Cap | 2,100 KRISTA | 0 KRISTA | - | - | 0 KRISTA | 100% of tx fees |

> [!NOTE]
> **Developer Treasury & Bootstrap Faucet Splits (Active since block 2)**:
> - **Developer Treasury (%7)**: 7% of the block reward is sent to the Developer Fund Address (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm`) for ecosystem development (applies to all blocks except block 1).
> - **Bootstrap Faucet (%0.7)**: 0.7% of the block reward is sent to the Bootstrap Faucet Address (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh`) for blocks 2 through 50,000 (applies to all blocks except block 1).
> - **Genesis Block (Block 1)**: Block 1 has a reward of 0 KRISTA (No Premine) and is completely exempt from these splits.
> 
> [!NOTE]
> **Model D Reward Splits (Active since block 2,200 on Mainnet / Testnet)**:
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
