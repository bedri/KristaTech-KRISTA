KristaTech (KRISTA) Core
========================

### Coin Specs

* **Ticker**: KRISTA
* **PoW Algorithm**: X11KVS (with 13-algo dynamic puzzle mining under ADAM consensus)
* **Premine**: 0 KRISTA (No Premine)
* **PoW Only Blocks**: 1 - 199
* **Hybrid PoS/PoW Blocks**: Starting from 200 (ADAM multi-algo PoW alongside PoS)
* **Block Time**: 30 Seconds
* **Coinbase Maturity**: Mainnet: 100 Confirmations, Testnet: 15 Confirmations
* **Stake Min Age**: Mainnet: 1 Hour (3600 seconds), Testnet: 0 seconds
* **Address Prefixes**:
  * **Mainnet**: Base58: **KT** (starts with K), Bech32: **kt**
  * **Testnet**: Base58: **kt** (starts with k), Bech32: **tk**
* **Mainnet Ports**: 27999 (p2p) / 27979 (rpc)
* **Testnet Ports**: 27989 (p2p) / 27919 (rpc)

* **Explorer**: [explorer.kristalteknoloji.com](https://explorer.kristalteknoloji.com)
* **Website**: [krista.kristalteknoloji.com](https://krista.kristalteknoloji.com)

---

### Advanced Consensus & Features

KristaTech implements next-generation hybrid consensus models, smart contract capabilities, and stable tokenomics. Full technical documentation is available in both English (default) and Turkish:

* **Technical Whitepaper**: Comprehensive design document. See [English Whitepaper](doc/Whitepaper.md) ([PDF Version](doc/Whitepaper.pdf)) or [Turkish Whitepaper (TR)](doc/Whitepaper_TR.md) ([PDF Version](doc/Whitepaper_TR.pdf)).
* **A Decentralized Approach Model (ADAM)**: Conceptual & mathematical framework for cooperative puzzle mining. See [English Paper](doc/A%20Decentralized%20Approach%20Model%20(ADAM).md) or [Turkish Paper (TR)](doc/A%20Decentralized%20Approach%20Model%20(ADAM)_TR.md).
* **ADAM Consensus Guide**: Technical implementation details of ADAM block validation. See [English Guide](doc/ADAM_Consensus.md) or [Turkish Guide (TR)](doc/ADAM_Consensus_TR.md).
* **ADAM Security Analysis**: Threat model analysis and mitigations for the ADAM network. See [English Analysis](doc/ADAM_Security_Analysis.md) or [Turkish Analysis (TR)](doc/ADAM_Security_Analysis_TR.md).
* **Proof of BLS (PoBLS) Consensus**: A lightweight cryptographic lottery model with temporary BLS keys for block production. See [English Guide](doc/PoBLS_Consensus.md) or [Turkish Guide (TR)](doc/PoBLS_Consensus_TR.md).
* **MPA & PoMBL Consensus Guide**: Proof of Masternode, Burn and Lock consensus models. See [English Guide](doc/MPA_Consensus.md) or [Turkish Guide (TR)](doc/MPA_Consensus_TR.md).
* **MESCAL Smart Contracts**: Script-based smart contracts specification. See [English Specification](doc/MESCAL.md) or [Turkish Specification (TR)](doc/MESCAL_TR.md).
* **MESCAL Usecases**: Real-world contract scenarios, escrow, and tokenization. See [English Usecases](doc/MESCAL_Usecases.md) or [Turkish Usecases (TR)](doc/MESCAL_Usecases_TR.md).
* **Tokenomics Model Study**: Emission schedules, decaying reward periods, and treasury splits. See [English Study](doc/Tokenomics_Study.md) or [Turkish Study (TR)](doc/Tokenomics_Study_TR.md).
* **Hardcap & Halving Analysis**: Hardcap supply target and block rewards schedule. See [English Analysis](doc/Hardcap_Analysis.md) or [Turkish Analysis (TR)](doc/Hardcap_Analysis_TR.md).
* **Masternode Setup Guide**: Step-by-step instructions for deploying and running a masternode. See [English Guide](doc/Masternode_Setup.md) or [Turkish Guide (TR)](doc/Masternode_Setup_TR.md).
* **Miner Registration Guide**: Specifications for registering pool keys on the blockchain. See [English Guide](doc/Miner_Registration.md) or [Turkish Guide (TR)](doc/Miner_Registration_TR.md).
* **Tokenized Assets Study**: Blockchain applications for real-world assets. See [English Study](doc/Tokenized_Assets_Study.md) or [Turkish Study (TR)](doc/Tokenized_Assets_Study_TR.md).
* **IDEAS FOR A NEW PROOF ALGORITHM**: Brainstorming notes on proof mechanics. See [English Paper](doc/IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM.md) or [Turkish Paper (TR)](doc/IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM_TR.md).
* **Security Audit Report**: Code audit and design vulnerabilities analysis. See [English Report](doc/Security_Audit.md) or [Turkish Report (TR)](doc/Security_Audit_TR.md).

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
> - **Developer Treasury (%7)**: 7% of the block reward is sent to the Developer Fund Address (`KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm` on Mainnet, `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` on Testnet) for ecosystem development (applies to all blocks except block 1).
> - **Bootstrap Faucet (%0.7)**: 0.7% of the block reward is sent to the Bootstrap Faucet Address (`KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh` on Mainnet, `ktNSLsNGPmkpkSZHNM9Pmz4PYjBRZhrVnRz` on Testnet) for blocks 2 through 50,000 (applies to all blocks except block 1).
> - **Genesis Block (Block 1)**: Block 1 has a reward of 0 KRISTA (No Premine) and is completely exempt from these splits.
> 
> [!NOTE]
> **Model D Reward Splits (Active since block 2,200 on Mainnet / block 500 on Testnet)**:
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
