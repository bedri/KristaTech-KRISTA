KristaTech (KRISTA) Core
========================

### Coin Specs

* **Ticker**: KRISTA
* **PoW Algorithm**: X11KVS (Blocks 1–199) + ADAM Cooperative 18-Algorithm Puzzle Mining (single-algo in V11, 3-permutation compound hashing in V12)
* **Premine**: 0 KRISTA (No Premine — Blocks 0 and 1 mint 0 KRISTA on Mainnet)
* **PoW Only Blocks**: 1 - 199
* **Hybrid PoS/PoW Blocks**: Starting from 200 (ADAM multi-algo PoW alongside PoS)
* **Block Time**: 30 Seconds
* **Coinbase Maturity**: Mainnet: 100 Confirmations, Testnet: 15 Confirmations
* **Stake Min Age / Min Depth**: Mainnet: 1 Hour (3600 seconds) / 100 Blocks (blocks 200–5,000) & 600 Blocks (from block 5,001+, `UPGRADE_STAKE_MIN_DEPTH_V2`), Testnet: 0 Seconds / 30 Blocks
* **Address Prefixes**:
  * **Mainnet**: Base58: **KT** (starts with KT), Bech32: **kt**
  * **Testnet**: Base58: **kt** (starts with kt), Bech32: **tk**
* **Mainnet Ports**: 27999 (p2p) / 27979 (rpc)
* **Testnet Ports**: 27989 (p2p) / 27919 (rpc)

* **Explorer**: [explorer.kristalteknoloji.com](https://explorer.kristalteknoloji.com)
* **Website**: [krista.kristalteknoloji.com](https://krista.kristalteknoloji.com)

---

### Advanced Consensus & Features

KristaTech implements next-generation hybrid consensus models, smart contract capabilities, and stable tokenomics. Full technical documentation is available in both English (default) and Turkish:

* **Technical Whitepaper**: Comprehensive design document. See [English Whitepaper](doc/Whitepaper.md) ([PDF Version](doc/Whitepaper.pdf)) or [Turkish Whitepaper (TR)](doc/Whitepaper_TR.md) ([PDF Version](doc/Whitepaper_TR.pdf)).
* **A Decentralized Approach Model (ADAM)**: Conceptual & mathematical framework for cooperative puzzle mining. See [English Paper](doc/A%20Decentralized%20Approach%20Model%20%28ADAM%29.md) or [Turkish Paper (TR)](doc/A%20Decentralized%20Approach%20Model%20%28ADAM%29_TR.md).
* **ADAM Consensus Guide**: Technical implementation details of ADAM block validation. See [English Guide](doc/ADAM_Consensus.md) or [Turkish Guide (TR)](doc/ADAM_Consensus_TR.md).
* **ADAM Security Analysis**: Threat model analysis and mitigations for the ADAM network. See [English Analysis](doc/ADAM_Security_Analysis.md) or [Turkish Analysis (TR)](doc/ADAM_Security_Analysis_TR.md).
* **Proof of BLS (PoBLS) Consensus**: A lightweight cryptographic lottery model with temporary BLS keys for block production. See [English Guide](doc/PoBLS_Consensus.md) or [Turkish Guide (TR)](doc/PoBLS_Consensus_TR.md).
* **MPA & PoMBL Consensus Guide**: Proof of Masternode, Burn and Lock consensus models. See [English Guide](doc/MPA_Consensus.md) or [Turkish Guide (TR)](doc/MPA_Consensus_TR.md).
* **MESCAL Smart Contracts**: Script-based smart contracts specification. See [English Specification](doc/MESCAL.md) or [Turkish Specification (TR)](doc/MESCAL_TR.md).
* **MESCAL Usecases**: Real-world contract scenarios, escrow, and tokenization. See [English Usecases](doc/MESCAL_Usecases.md) or [Turkish Usecases (TR)](doc/MESCAL_Usecases_TR.md).
* **Tokenomics Model Study**: Emission schedules, decaying reward periods, and treasury splits. See [English Study](doc/Tokenomics_Study.md) or [Turkish Study (TR)](doc/Tokenomics_Study_TR.md).
* **Hardcap & Halving Analysis**: Hardcap supply target and block rewards schedule. See [English Analysis](doc/Hardcap_Analysis.md) or [Turkish Analysis (TR)](doc/Hardcap_Analysis_TR.md).
* **Masternode Setup Guide**: Step-by-step instructions for deploying and running a masternode. See [English Guide](doc/Masternode_Setup.md) or [Turkish Guide (TR)](doc/Masternode_Setup_TR.md).
* **Multi-Masternode Single IP Guide**: Running multiple Masternodes on a single IP/daemon via `activemasternode.conf`. See [English Guide](doc/Multi_Masternode_Single_IP.md) or [Turkish Guide (TR)](doc/Multi_Masternode_Single_IP_TR.md).
* **Sentry Node Setup Guide**: DDoS-shielded Masternode architecture using public relay nodes. See [English Guide](doc/Sentry_Node_Setup.md) or [Turkish Guide (TR)](doc/Sentry_Node_Setup_TR.md).
* **Miner Registration Guide**: Specifications for registering pool keys on the blockchain. See [English Guide](doc/Miner_Registration.md) or [Turkish Guide (TR)](doc/Miner_Registration_TR.md).
* **Security Audit Report**: Cryptographic, consensus, and P2P hardening audit. See [English Report](doc/Security_Audit.md) or [Turkish Report (TR)](doc/Security_Audit_TR.md).
* **Future Blocks Queue**: Clock-drift tolerance and bounded `mapFutureBlocks` queue specification. See [English Guide](doc/Future_Blocks_Queue.md) or [Turkish Guide (TR)](doc/Future_Blocks_Queue_TR.md).
* **KRISTA vs DASH Comparison**: Architectural comparison of consensus, LLMQs, smart contracts, and tokenomics. See [English Comparison](doc/KRISTA%20vs%20DASH.md) or [Turkish Comparison (TR)](doc/KRISTA%20vs%20DASH_TR.md).
* **Tokenized Assets Study**: Blockchain applications for real-world assets. See [English Study](doc/Tokenized_Assets_Study.md) or [Turkish Study (TR)](doc/Tokenized_Assets_Study_TR.md).

---

### KristaTech Improvement Proposals (KTIPs)

To formally document and track consensus changes, upgrade rules, and cryptographic standards, the network utilizes the KTIP process. See the following proposals in both English and Turkish:

* **KTIP-0001 (KTIP Process)**: Formal submission standards and lifecycle transitions. See [English Proposal](doc/ktips/KTIP-0001_KTIP_Process.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0001_KTIP_Process_TR.md).
* **KTIP-0002 (ADAM Consensus)**: A Decentralized Approach Model cooperative consensus rules. See [English Proposal](doc/ktips/KTIP-0002_ADAM.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0002_ADAM_TR.md).
* **KTIP-0003 (PoMPA & Dual Mining)**: Proof of Multi-Proof Algorithm and block difficulty weights. See [English Proposal](doc/ktips/KTIP-0003_PoMPA_Dual_Mining.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0003_PoMPA_Dual_Mining_TR.md).
* **KTIP-0004 (PoMBL Dual-Signature)**: Dual-signature verification rule (staker and coordinator). See [English Proposal](doc/ktips/KTIP-0004_PoMBL.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0004_PoMBL_TR.md).
* **KTIP-0005 (PoBLS Hybrid Signatures)**: Hybrid ECDSA/BLS12-381 key derivation and message validation. See [English Proposal](doc/ktips/KTIP-0005_PoBLS.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0005_PoBLS_TR.md).
* **KTIP-0006 (Model D Network Upgrade)**: Quorum topologies, DKG session timing, and verification thresholds. See [English Proposal](doc/ktips/KTIP-0006_Model_D.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0006_Model_D_TR.md).
* **KTIP-0007 (Protocol Scaling Upgrade)**: 8 MB block limit, 30s target block spacing, 15s PoS time slots, and BIP152 Compact Blocks. See [English Proposal](doc/ktips/KTIP-0007_Scaling_Upgrade.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0007_Scaling_Upgrade_TR.md).
* **KTIP-0008 (MESCAL & Taproot P2TR)**: Stateless MESCAL smart contracts and Schnorr/Taproot script-path execution. See [English Proposal](doc/ktips/KTIP-0008_MESCAL.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0008_MESCAL_TR.md).
* **KTIP-0009 (Dual-Path Miner Registration & 7+1+3 Quotas)**: Coin-Lock (`250 KRISTA`), PoW-Lock (`10 KRISTA` / `0 KRISTA`), and Dual-Track validator quotas. See [English Proposal](doc/ktips/KTIP-0009_Miner_Registration_Dual_Track.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0009_Miner_Registration_Dual_Track_TR.md).
* **KTIP-0010 (Consensus Liveness & Equal-Work Incentives)**: On-chain pings, same-track standby handover, time-relaxed quorum, and 50/50 burn/treasury split. See [English Proposal](doc/ktips/KTIP-0010_Consensus_Liveness_Incentives.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0010_Consensus_Liveness_Incentives_TR.md).
* **KTIP-0011 (Sentry Relay Topology, Future Blocks Queue & LLMQ Treasury Governance)**: DDoS-shielded validator setup, bounded `mapFutureBlocks`, and dynamic BFT LLMQ Developer Fund governance (`3-of-5` up to `8-of-15`). See [English Proposal](doc/ktips/KTIP-0011_Sentry_FutureBlocks_Treasury.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0011_Sentry_FutureBlocks_Treasury_TR.md).
* **KTIP-0012 (Taproot/CSV/BIP68 Consensus, P2P DoS Hardening, BIP324, HeadersSyncState & PSBT)**: Mandatory block-level Taproot/CSV/BIP68 checks, `GETDATA`/`ADDR`/`CMPCTBLOCK` DoS protections, `HeadersSyncState`, BIP324 v2 encrypted P2P transport, `-rpcauth`, and BIP174 PSBT RPCs + Qt Sign/Broadcast UI. See [English Proposal](doc/ktips/KTIP-0012_Security_BIP324_HeadersSync_PSBT.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0012_Security_BIP324_HeadersSync_PSBT_TR.md).
* **KTIP-0013 (Trustless Shared Masternodes — Draft)**: Design specification for non-custodial co-funded `4,200 KRISTA` Taproot (`kt1p...`) Masternode collateral with BIP68 CSV recovery and protocol-level multi-payee coinbase splitting. See [English Proposal](doc/ktips/KTIP-0013_Trustless_Shared_Masternodes.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0013_Trustless_Shared_Masternodes_TR.md).
* **KTIP-0014 (Deterministic On-Chain Masternodes — KTDIP3 & KTDIP3 v2)**: `evodb`-backed deterministic Masternode list (`ProRegTx`, `ProUpServTx`, `ProUpRegTx`, `ProUpRevTx`), Hot/Cold pure operator BLS12-381 separation (`nDip3V2Height`), PoSe scoring, operator reward splits, and 1-click GUI wizard. See [English Proposal](doc/ktips/KTIP-0014_Deterministic_Masternodes.md) or [Turkish Proposal (TR)](doc/ktips/KTIP-0014_Deterministic_Masternodes_TR.md).

---

### Rewards Breakdown

| Block Range | Phase | Collateral | Gross Block Reward | Top-Slice (Developer Fund) | Net Pool Split (Passive MN / LLMQ / Producer / 11 Validators) |
| --- | --- | --- | --- | --- | --- |
| 0 – 1 | Genesis & Initial Block (No Premine) | - | 0 KRISTA | 0% | 0 KRISTA |
| 2 – 199 | Classic PoW (Period 0 Start) | 4,200 KRISTA | 17 KRISTA | 7.0% | 15.81 KRISTA (100% Miner; + tx fees in PoW) |
| 200 – 4,999 | Pre-Model D Cooperative ADAM (LLMQ/PoMBL at 4,800) | 4,200 KRISTA | 17 KRISTA | 7.0% | 15.81 KRISTA: 60% Producer (`9.486 KRISTA`) / 40% Active Validators (`6.324 KRISTA`: 20% 7 MN/Spillover + 8% Coin-Lock + 12% 3 Open PoW) |
| 5,000 – 259,201 | Model D Activation & Period 0 (~90 days) | 4,200 KRISTA | 17 KRISTA | 7.0% | 15.81 KRISTA: 35% MN (`5.5335`) / 10% LLMQ (`1.581`) / 15% Producer (`2.3715`) / 40% Validators (`6.324`: 20% 7 MN + 8% Coin-Lock + 12% 3 Open PoW) |
| 259,202 – 518,401 | Period 1 (~90 days) | 4,200 KRISTA | 16.6345 KRISTA | 7.0% | 15.470085 KRISTA: 35% MN / 10% LLMQ / 15% Producer / 40% Validators |
| 518,402 – 777,601 | Period 2 (~90 days) | 4,200 KRISTA | 16.27685825 KRISTA | 7.0% | 15.13747818 KRISTA: 35% MN / 10% LLMQ / 15% Producer / 40% Validators |
| 777,602 – 1,036,801 | Period 3 (~90 days) | 4,200 KRISTA | 15.92690580 KRISTA | 7.0% | 14.81202240 KRISTA: 35% MN / 10% LLMQ / 15% Producer / 40% Validators |
| 1,036,802+ | Long-Term Quarterly Decay | 4,200 KRISTA | Decays 2.15% every 259,200 blocks | 7.0% | 93.0% Net Pool: 35% MN / 10% LLMQ / 15% Producer / 40% Validators |
| After ~204.95M minted (210M Hard Cap) | Max Supply Asymptote | 4,200 KRISTA | 0 KRISTA | - | Transaction fees in PoW blocks (burned in PoS blocks) |

> [!NOTE]
> **Developer Treasury Split (Active since block 2)**:
> - **Developer Treasury (7.0%)**: 7.0% of the gross block reward is sent to the LLMQ-governed Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP` on Mainnet, `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` on Testnet; spending requires the active BFT LLMQ majority signature — scaling from `3-of-5` up to `8-of-15` by active Masternodes — from height `5,000` on Mainnet / `200` on Testnet).
> - **Blocks 0 & 1 (Zero Premine)**: Blocks 0 and 1 mint 0 KRISTA on Mainnet and are completely exempt from this split.
> 
> [!NOTE]
> **Model D Net Pool Splits (Active from block 5,000 on Mainnet / block 500 on Testnet / block 200 on Regtest)**:
> - **MN Passive Payee (35%)**: Paid to an Active Masternode deterministically selected via VRF seed modulo (`GetModelDPassiveMasternodePayee`).
> - **Active LLMQ Quorum (10%)**: Divided equally (`10% / N`, e.g. `2.0%` each in a `5`-member `3-of-5` quorum) among the $N \in \{5, 9, 11, 15\}$ active LLMQ Masternodes co-signing `vQuorumSig` (scaled dynamically by active Masternode count: `<25` $\rightarrow$ `3-of-5`, `25–49` $\rightarrow$ `5-of-9`, `50–99` $\rightarrow$ `6-of-11`, `>=100` $\rightarrow$ `8-of-15`).
> - **Block Producer / Winner (15%)**: Paid in full to the Coordinator (+ 100% of tx fees) in PoW blocks or to the winning Staker in PoS blocks (where tx fees are burned), with **zero Coordinator penalty** when standby validators rescue silent seats.
> - **Validator Participants (40% — 20% across 7 MN Seats + 20% across 4 Independent Miner Seats)**: Every solved seat (`!vAdamSolutions[i].empty()`, primary or legitimate same-track standby at $t \ge 60\text{s}$) receives **100% of its own validator track's per-seat share** (`~2.857%` per Masternode seat across up to 7 seats, `8.00%` for 1 Coin-Lock seat [`250 KRISTA`], and `4.00%` per Open PoW seat [`10 KRISTA` / `0 KRISTA`]). Unsolved/empty seats receive `0%`, and all unclaimed shares (`nUnclaimedPartSplit`) are split **50% to Automatic Protocol Burn** (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`, pure supply deflation with zero personal PoB weight) and **50% to the Developer Treasury**.

---

### Network & GUI Stability Improvements (June 2026 Updates)

To support robust network operation and accurate UI/UX display under hybrid consensus load, the following stability and protocol-level improvements have been integrated:

* **GUID-Based Duplicate Connection Mitigation**: Peers now generate and exchange a unique `nLocalNodeGUID` during the initial connection handshake. If a duplicate connection from the same GUID is detected, a deterministic tie-breaking logic (larger GUID keeps outbound, terminates inbound) closes the redundant channel, ensuring the active peer count matches the unique physical nodes exactly.
* **Masternode Sync Flags Reset**: Running `mnsync reset` now correctly clears the asked-flags maps (`mWeAskedForMasternodeList` and `mAskedUsForMasternodeList`) in the masternode manager, preventing nodes from getting locked out of syncing due to rate-limiting on private networks.
* **Protocol Inventory Type Mapping Fix**: Corrected the inventory commands mapping (`CInv::GetCommand()`) for `MSG_MASTERNODE_ANNOUNCE` and `MSG_MASTERNODE_PING` to their corresponding network message strings, restoring proper P2P propagation of masternode database updates and active status pings.
* **Masternode Reward UI Display**: Refined transaction decomposition (`decomposeCoinBase()`) to properly identify masternode rewards/splits at output indices >= 1 in coinbase transactions, ensuring zero-value rewards (such as at block heights <= 5000) display as "Masternode Reward" rather than "No information".
* **Safe Datadir Initialization**: Fixed a null-pointer dereference in `GetDataDir()` where base params were requested before configuration parsing completed. A fallback non-cached path is now returned until parameters are configured, preventing pointer-named folder pollution in the repository root.

### Consensus, P2P & Performance Hardening (v1.4.5.9 – v1.5.8.0 Updates)

* **Deterministic On-Chain Masternodes (`KTIP-0014` / `KTDIP3` & `KTDIP3 v2`) & 1-Click In-Wallet Setup (`v1.5.8.0`)**: Replaced legacy P2P ping tracking with an `evodb`-backed deterministic Masternode list (`ProRegTx`, `ProUpServTx`, `ProUpRegTx`, `ProUpRevTx`), `protx` RPCs (including `protx bls_generate`), Rule 8 multi-MN per IP, mempool/block-template duplicate `ProTx` conflict guards, startup auto-locking of wallet-owned DIP3 collaterals, and 1-click GUI registration directly from `Create Masternode Controller` and `Start`. With **KTDIP3 v2 (`nDip3V2Height = 5,000` Mainnet / `1` Testnet & Regtest)**, Hot VPS nodes authenticate purely with their registered `blsPubKeyOperator` (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature`) without needing Cold owner ECDSA keys, while payouts deterministically split operator bps rewards (`scriptPayout` / `scriptOperatorPayout`) and drive LLMQ / `PoM` directly from `evodb`.
* **Core, Script & Qt Stability Hardening (`v1.5.8.0`)**: Added `VerifyDB`/reindex `evodb` state preservation and null `phashBlock` guards, fixed `sendrawtransaction`/mempool lock ordering & `protx` integer parsing, added `mescal` `"mode": "hash-equalverify"` & 32-bit `nLockTime` bounds (`0..4294967295`), eliminated the 25s Qt startup D-Bus splash freeze, fixed HTTP `workQueue` shutdown deadlock & `-zapwallettxes=2` restart datadir lock races, and modernized the 9-tab Qt wallet suite (including the 3-pane Light/Dark Visual Smart Contract Designer, live YAML syntax highlighting, redesigned Stats/Telemetry views, Home `My Masternodes` card with persistent drag-and-drop card reordering, 2-step encryption verification, and clean TopBar focus/lock-dropdown UX).
* **BIP324 Encrypted P2P v2, Headers Pre-Sync, BIP68, PSBT & `-rpcauth` (`KTIP-0012`)**: Added ChaCha20-Poly1305 / ElligatorSwift BIP324 v2 P2P transport, anti-DoS headers pre-sync, BIP68 relative locktimes, BIP174/370 PSBT workflow RPCs, and salted `-rpcauth` RPC credentials.

* **Dual-Track ($7 + 1 + 3$) Validator Quotas, Strict `1`-Seat `Coin-Lock` Isolation, Unified `HashX11KVS` `PoW-Lock` & Quadratic Coordinator VRF (`ElectDualTrackValidators`, `SelectAdamNodes`)**: Partitioned the 11 validator seats into **Track 1 (8 Collateralized Seats: 7 Active Masternodes [`4,200 KRISTA`] + 1 strictly isolated Coin-Lock [`250 KRISTA`], enforced via `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7)`)** and **Track 2 (3 Open PoW Seats: 2 `10 KRISTA` PoW-Lock seats + 1 permanent `0 KRISTA` Open-Entry seat from `nAdamOpenEntryHeight = 2500` on Mainnet [`1` Testnet/Regtest] so newcomers with 0 balance can always start solo mining and auto-promote to `10 KRISTA` PoW-Lock)** so Active Masternodes alone always meet the post-bootstrap quorum threshold (`7`). Both funded (`10 KRISTA`, `15`-bit `"KRPL"`/`"POWLKv2"`) and unfunded (`0 KRISTA`, `14`-bit `"KRZC"`/`"SYBILV2"`, `240`-block lease TTL) `PoW-Lock` paths use domain-separated 7-level recursive **`HashX11KVS`** CPU proofs, while both `Coin-Lock` and `Funded PoW-Lock` enforce 100-confirmation input maturity, a strict `1` tx/block consensus limit (`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1`, `ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`), and a 1-active-seat-per-wallet guard. Unselected `250 KRISTA` Coin-Lock validators are strictly excluded from Open PoW `Tier 2A` (`4.00%` seats) and act only as a last-resort liveness fallback behind Active Masternodes and Open PoW miners (earning `~2.857%` MN seat base if filling an empty MN seat), LLMQ quorum fallback (`< 5` Active MNs) strictly preserves all `1..4` real verified Active Masternodes first, PoB staking weight is UTXO-splitting invariant (`min(nBurnWeight, nAmount * 5)`), and the primary Coordinator is elected via quota-capped quadratic stake-weighted VRF (`65` per Active MN, max `1` Coin-Lock at `8`, max `3` Funded PoW-Locks at `2` each [`6` track cap] under `nAdamCoordWeightV2Height`, `1` Mainnet/Regtest, `1950` Testnet; previously `16` and `4` [`12` cap]) (`PROTOCOL_VERSION = 70983`).
* **Standby Validator Seat Handover ($t \ge 60\text{s}$) & Time-Relaxed Quorum Threshold ($t \ge 120\text{s}$)**: Silent validator seats at $t \ge 60\text{s}$ are handed over strictly to same-track standby validators (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) in sequential rank order verified against the block's `CCoinsViewCache* pView` (`ResolveAdamCommitteeForTime`, `ValidateAdamCommitteeForTime`). If a block remains delayed at $t \ge 120\text{s}$, `GetAdamEffectiveThreshold` relaxes the required threshold by `1` every `60s` down to `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` non-MN seats, ensuring at least 1 Active Masternode is always required when post-bootstrap or active MNs $\ge 7$, or `3` during bootstrap when active MNs $< 7$).
* **Equal Work, Equal Reward (40% Active Validator Pool from Block 200), 50/50 Automatic Protocol Burn & LLMQ Anti-Substitution Veto**: Starting at Block `200` (`IsAdamValidatorSplitActive`, splitting `60% Producer + 40% Active Validators` prior to Model D and unbundling the `60%` Producer share into `15% Producer + 35% Passive MN + 10% LLMQ` at Block `5,000`), every solved seat (primary or legitimate same-track standby at $t \ge 60\text{s}$) receives 100% of its validator track's per-seat share of the 40% Participant Validator pool (`~2.857%` per Masternode/spillover seat, `8.00%` for the Coin-Lock seat, `4.00%` per Open PoW seat), unsolved seats receive `0%`, and the Coordinator retains its full producer reward when rescuing a stalled block, with unclaimed shares split **50% to Automatic Protocol Burn** and **50% to the Developer Treasury**. At $t \ge 60\text{s}$, LLMQ members veto `qblockprop` proposals that substitute a primary seat whose valid solution is already cached locally.
* **Node-Wide Single-Block-Per-Parent Guard (`ActiveProposalGuard`) & Alternating PoW/PoS Slot Priority**: Version-independent guard in `src/miner.cpp` prevents any node from producing both a PoW block and a PoS block on the same parent across all block versions (`v11` and `v12`), while `ADAM_SLOT_PRIORITY_WINDOW_MS = 1500` alternates slot-opening priority between PoW and PoS based on the parent block type (`pindexPrev->IsProofOfStake()`).
* **Deterministic Equal-Work Fork Tie-Breaker (`CBlockIndexWorkComparator`)**: When competing chain tips at the same height have equal `nChainWork`, nodes deterministically prefer the tip with the numerically smaller block hash (`GetBlockHash()`), ensuring instantaneous network-wide fork convergence (`< 1s`) after partition healing without waiting for the next block slot.
* **LLMQ Anti-Equivocation Guard & Multi-Hop Gossip Relay**: Quorum members (`5–15` active Masternodes scaling dynamically with network MN count: `<25` $\rightarrow$ `3-of-5`, `25–49` $\rightarrow$ `5-of-9`, `50–99` $\rightarrow$ `6-of-11`, `>=100` $\rightarrow$ `8-of-15`) enforce parent-keyed anti-equivocation (`llmq::AllowQuorumSignature` / `mapSignedProposalByPrev[prevHash]`, blocking competing coordinators for `30s` with a `12s` same-coordinator retry) so competing blocks in the same slot cannot both obtain an LLMQ quorum certificate (`vQuorumSig`). Block proposals and signature shares are deduplicated and relayed across all connected peers (`setSeenProposedBlocks`, `mapQuorumBlockSigs`).
* **Strict Hot/Cold Masternode Signing Isolation & Decentralized PoS Co-Signing (`POSCOORDREQ` / `POSCOORDSIG`)**: Cold controller wallets (`masternode.conf`) only broadcast `CMasternodeBroadcast` activations; active Hot Nodes (`masternode=1` + `activemasternode.conf`) handle consensus signing, multi-MN single-IP hosting, and remote PoS Coordinator co-signing.
* **Authenticated On-Chain Masternode Heartbeat Pings (`SendOnChainPing`)**: Supports both 33-byte compressed (`36-byte` script) and 65-byte uncompressed (`68-byte` script generated by `createmasternodekey`) public keys with a serialized `CDataStream` `OP_RETURN` proof (`collateralOutpoint` + `vchPingSig` + optional `COnChainMasternodeDelegation`), and exempts authenticated Masternode pings (`0.01 KRISTA` self-spendable UTXO) from the `1000x` `OP_RETURN` relay fee multiplier (paying a flat `0.0001 KRISTA` fee after `>= 1,440` idle blocks while active block participation renews status at zero cost).
* **Lock-Free Chain Tip Access (`CChain::AtomicTip()`)**: `GetChainTip()` reads an atomic pointer (`std::atomic<CBlockIndex*>`) updated inside `CChain::SetTip()`, eliminating `cs_main` lock contention between P2P message handling, RPC requests, and staking/mining threads.
* **Live DNS Seeder & Fixed Seed Fallback**: Active DNS Seeder (`seeder.kristalteknoloji.com`) and hardcoded IPv4-mapped IPv6 fallback seeds (`pnSeed6_main` and `pnSeed6_test` in `src/chainparamsseeds.h`) provide resilient peer discovery across all seed nodes.
