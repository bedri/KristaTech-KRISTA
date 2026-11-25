KristaTech Core
=============

Setup
---------------------
[KristaTech Core](https://krista.kristalteknoloji.com/) is the original KristaTech client and it builds the backbone of the network. However, it downloads and stores the entire history of KristaTech transactions; depending on the speed of your computer and network connection, the synchronization process can take anywhere from a few hours to a day or more. Thankfully you only have to do this once.

Running
---------------------
The following are some helpful notes on how to run KristaTech Core on your native platform.

### Unix

Unpack the files into a directory and run:

- `bin/kristatech-qt` (GUI) or
- `bin/kristatechd` (headless)

### Windows

Unpack the files into a directory, and then run KristaTech-qt.exe.

### macOS

Drag KristaTech-Qt to your applications folder, and then run KristaTech-Qt.

### Need Help?

* See the documentation at the [KristaTech Wiki](https://github.com/bedri/KristaTech-KRISTA/)
for help and more information.
* Visit the [KristaTech Homepage](https://krista.kristalteknoloji.com/) for the latest updates and community links.

Building
---------------------
The following are developer notes on how to build KristaTech Core on your native platform. They are not complete guides, but include notes on the necessary libraries, compile flags, etc.

- [Dependencies](dependencies.md)
- [macOS Build Notes](build-osx.md)
- [Unix Build Notes](build-unix.md)
- [Windows Build Notes](build-windows.md)
- [Gitian Building Guide](gitian-building.md)

Consensus Architecture & Advanced Features
---------------------
KristaTech Core runs on specialized consensus mechanisms designed for decentralization, speed, and advanced smart contract capabilities. All documentation is available in both English (default) and Turkish:

- **Technical Whitepaper**: [English](Whitepaper.md) ([PDF Version](Whitepaper.pdf)) or [Turkish (TR)](Whitepaper_TR.md) ([PDF Version](Whitepaper_TR.pdf))
- **A Decentralized Approach Model (ADAM)**: [English](A%20Decentralized%20Approach%20Model%20%28ADAM%29.md) or [Turkish (TR)](A%20Decentralized%20Approach%20Model%20%28ADAM%29_TR.md)
- **ADAM Consensus Guide**: [English](ADAM_Consensus.md) or [Turkish (TR)](ADAM_Consensus_TR.md)
- **ADAM Security Analysis**: [English](ADAM_Security_Analysis.md) or [Turkish (TR)](ADAM_Security_Analysis_TR.md)
- **Proof of BLS (PoBLS) Consensus**: [English](PoBLS_Consensus.md) or [Turkish (TR)](PoBLS_Consensus_TR.md)
- **MPA & PoMBL Consensus Guide**: [English](MPA_Consensus.md) or [Turkish (TR)](MPA_Consensus_TR.md)
- **MESCAL Smart Contracts**: [English](MESCAL.md) or [Turkish (TR)](MESCAL_TR.md)
- **MESCAL Usecases**: [English](MESCAL_Usecases.md) or [Turkish (TR)](MESCAL_Usecases_TR.md)
- **Tokenomics Model Study**: [English](Tokenomics_Study.md) or [Turkish (TR)](Tokenomics_Study_TR.md)
- **Hardcap & Halving Analysis**: [English](Hardcap_Analysis.md) or [Turkish (TR)](Hardcap_Analysis_TR.md)
- **Masternode Setup Guide**: [English](Masternode_Setup.md) or [Turkish (TR)](Masternode_Setup_TR.md)
- **Multi-Masternode Single IP Guide**: [English](Multi_Masternode_Single_IP.md) or [Turkish (TR)](Multi_Masternode_Single_IP_TR.md)
- **Sentry Node Setup Guide**: [English](Sentry_Node_Setup.md) or [Turkish (TR)](Sentry_Node_Setup_TR.md)
- **Miner Registration Guide**: [English](Miner_Registration.md) or [Turkish (TR)](Miner_Registration_TR.md)
- **Security Audit Report**: [English](Security_Audit.md) or [Turkish (TR)](Security_Audit_TR.md)
- **Future Blocks Queue**: [English](Future_Blocks_Queue.md) or [Turkish (TR)](Future_Blocks_Queue_TR.md)
- **KRISTA vs DASH Comparison**: [English](KRISTA%20vs%20DASH.md) or [Turkish (TR)](KRISTA%20vs%20DASH_TR.md)
- **Tokenized Assets Study**: [English](Tokenized_Assets_Study.md) or [Turkish (TR)](Tokenized_Assets_Study_TR.md)
- **Ideas for a New Proof Algorithm**: [English](IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM.md) or [Turkish (TR)](IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM_TR.md)

KristaTech Improvement Proposals (KTIPs)
---------------------
- **KTIP-0001 (KTIP Process)**: [English](ktips/KTIP-0001_KTIP_Process.md) or [Turkish (TR)](ktips/KTIP-0001_KTIP_Process_TR.md)
- **KTIP-0002 (ADAM Cooperative Consensus)**: [English](ktips/KTIP-0002_ADAM.md) or [Turkish (TR)](ktips/KTIP-0002_ADAM_TR.md)
- **KTIP-0003 (PoMPA Weight & Dual Mining)**: [English](ktips/KTIP-0003_PoMPA_Dual_Mining.md) or [Turkish (TR)](ktips/KTIP-0003_PoMPA_Dual_Mining_TR.md)
- **KTIP-0004 (PoMBL Dual-Signature Validation)**: [English](ktips/KTIP-0004_PoMBL.md) or [Turkish (TR)](ktips/KTIP-0004_PoMBL_TR.md)
- **KTIP-0005 (PoBLS Hybrid ECDSA/BLS12-381 Signatures)**: [English](ktips/KTIP-0005_PoBLS.md) or [Turkish (TR)](ktips/KTIP-0005_PoBLS_TR.md)
- **KTIP-0006 (Model D Network Upgrade)**: [English](ktips/KTIP-0006_Model_D.md) or [Turkish (TR)](ktips/KTIP-0006_Model_D_TR.md)
- **KTIP-0007 (Protocol Scaling Upgrade & BIP152)**: [English](ktips/KTIP-0007_Scaling_Upgrade.md) or [Turkish (TR)](ktips/KTIP-0007_Scaling_Upgrade_TR.md)
- **KTIP-0008 (MESCAL Smart Contracts & Taproot P2TR)**: [English](ktips/KTIP-0008_MESCAL.md) or [Turkish (TR)](ktips/KTIP-0008_MESCAL_TR.md)
- **KTIP-0009 (Dual-Path Miner Registration & Dual-Track Quotas: 7+1+3 Pre-Model D / 8+2+5 Balanced Model D)**: [English](ktips/KTIP-0009_Miner_Registration_Dual_Track.md) or [Turkish (TR)](ktips/KTIP-0009_Miner_Registration_Dual_Track_TR.md)
- **KTIP-0010 (On-Chain Pings, Standby Handover, Relaxed Quorum & Equal-Work Incentives)**: [English](ktips/KTIP-0010_Consensus_Liveness_Incentives.md) or [Turkish (TR)](ktips/KTIP-0010_Consensus_Liveness_Incentives_TR.md)
- **KTIP-0011 (Sentry Relay Topology, Future Blocks Queue & LLMQ Treasury Governance)**: [English](ktips/KTIP-0011_Sentry_FutureBlocks_Treasury.md) or [Turkish (TR)](ktips/KTIP-0011_Sentry_FutureBlocks_Treasury_TR.md)
- **KTIP-0012 (Taproot/CSV/BIP68 Consensus, P2P/RPC DoS Hardening, BIP324, HeadersSyncState & PSBT)**: [English](ktips/KTIP-0012_Security_BIP324_HeadersSync_PSBT.md) or [Turkish (TR)](ktips/KTIP-0012_Security_BIP324_HeadersSync_PSBT_TR.md)
- **KTIP-0013 (Trustless Shared Masternodes — Draft)**: [English](ktips/KTIP-0013_Trustless_Shared_Masternodes.md) or [Turkish (TR)](ktips/KTIP-0013_Trustless_Shared_Masternodes_TR.md)
- **KTIP-0014 (Deterministic On-Chain Masternodes — KTDIP3)**: [English](ktips/KTIP-0014_Deterministic_Masternodes.md) or [Turkish (TR)](ktips/KTIP-0014_Deterministic_Masternodes_TR.md)

Mainnet vs Testnet Parameter Comparison
---------------------
To ensure consistency across network configurations, the differences between Mainnet and Testnet are detailed below:

| Parameter | Mainnet Value | Testnet Value |
| --- | --- | --- |
| **P2P Port** | 27999 | 27989 |
| **RPC Port** | 27979 | 27919 |
| **Base58 Address Prefix** | `KT...` (starts with KT) | `kt...` (starts with kt) |
| **Bech32 Address HRP** | `kt...` | `tk...` |
| **Target Block Spacing** | 30 seconds | 30 seconds |
| **Time Slot Length** | 15 seconds | 15 seconds |
| **Block Size Limit** | 8 MB | 8 MB |
| **Compact Blocks** | BIP152 Activated (MSG_CMPCT_BLOCK = 20) | BIP152 Activated (MSG_CMPCT_BLOCK = 20) |
| **Bootstrap Limit (nAdamBootstrapLimit)** | 2,500 blocks (under `UPGRADE_FAIRNESS`; legacy 44,850) | 100,000 blocks |
| **Threshold Bootstrap Limit (nAdamThresholdBootstrapLimit)** | 50,000 blocks | 100,000 blocks |
| **Dual-Track Quota Height (nAdamDualTrackHeight)** | block 200 (`UPGRADE_ADAM`) | block 200 (`UPGRADE_ADAM`) |
| **Coinbase Maturity** | 100 blocks | 15 blocks |
| **Stake Min Age** | 1 hour (3600 seconds) | 0 seconds |
| **Stake Min Depth** | 100 blocks | 30 blocks |
| **Stake Min Depth V2 (`UPGRADE_STAKE_MIN_DEPTH_V2`)** | 600 blocks (from block 5,001) | 30 blocks (from block 5,001) |
| **Developer Fund Address** | `KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP` | `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` |
| **Treasury Governance Spend Height (`nTreasuryGovernanceStartHeight`)** | block 5,000 (7% allocation active from block 2) | block 200 (7% allocation active from block 2) |
| **UPGRADE_DIP3 Activation (KTDIP3, Hot/Cold BLS & Payouts)** | block 1 | block 1 |
| **UPGRADE_FAIRNESS Activation (Equal Work → Equal Pay & Fair PoS/PoL)** | block 1 | block 1 (and block 1 Regtest) |
| **Coordinator V4 Capital Weights (`ADAM_FAIR_COORD_V4`, `4200:250:10`, 75% MN Floor Pre-Model D)** | block 1 (until block 4,999) | block 1 (until block 499) |
| **Coordinator V5 Capital Weights (`ADAM_FAIR_COORD_V5`, `4200:250:10`, 55% MN Floor)** | block 5,000 (`UPGRADE_MODELD`) | block 500 (`UPGRADE_MODELD`) |
| **Permanent 0-KRISTA Free Entry (`nAdamOpenEntryHeight`: 1 seat Pre-Model D / 2 seats Model D)** | block 2,500 | block 1 |
| **UPGRADE_POS Activation** | block 200 | block 200 |
| **UPGRADE_ADAM Activation** | block 200 | block 200 |
| **UPGRADE_ADAM_V2 Activation**| block 5,000 | block 500 |
| **UPGRADE_POMBL Activation** | block 4,800 | block 400 |
| **UPGRADE_MODELD Activation (Balanced Model D: 5/10/60/25, 15 Seats 8+2+5, Threshold 8, PoBLS)** | block 5,000 | block 500 |


Development
---------------------
The KristaTech repo's [root README](../README.md) contains relevant information on the development process and automated testing.

- [Developer Notes](developer-notes.md)
- [Multiwallet Qt Development](multiwallet-qt.md)
- [Release Notes](release-notes.md)
- [Release Process](release-process.md)
- [Source Code Documentation (External Link)](https://github.com/bedri/KristaTech-KRISTA/)
- [Translation Process](translation_process.md)
- [Unit Tests](unit-tests.md)
- [Unauthenticated REST Interface](REST-interface.md)
- [Dnsseed Policy](dnsseed-policy.md)

### Resources
* Discuss on the [KristaTech Homepage](https://krista.kristalteknoloji.com/).

### Miscellaneous
- [Assets Attribution](assets-attribution.md)
- [Files](files.md)
- [Tor Support](tor.md)
- [Init Scripts (systemd/upstart/openrc)](init.md)

License
---------------------
Distributed under the [MIT software license](../COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
