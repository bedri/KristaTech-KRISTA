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
- **A Decentralized Approach Model (ADAM)**: [English](A%20Decentralized%20Approach%20Model%20(ADAM).md) or [Turkish (TR)](A%20Decentralized%20Approach%20Model%20(ADAM)_TR.md)
- **ADAM Consensus Guide**: [English](ADAM_Consensus.md) or [Turkish (TR)](ADAM_Consensus_TR.md)
- **ADAM Security Analysis**: [English](ADAM_Security_Analysis.md) or [Turkish (TR)](ADAM_Security_Analysis_TR.md)
- **Proof of BLS (PoBLS) Consensus**: [English](PoBLS_Consensus.md) or [Turkish (TR)](PoBLS_Consensus_TR.md)
- **MPA & PoMBL Consensus Guide**: [English](MPA_Consensus.md) or [Turkish (TR)](MPA_Consensus_TR.md)
- **MESCAL Smart Contracts**: [English](MESCAL.md) or [Turkish (TR)](MESCAL_TR.md)
- **MESCAL Usecases**: [English](MESCAL_Usecases.md) or [Turkish (TR)](MESCAL_Usecases_TR.md)
- **Tokenomics Model Study**: [English](Tokenomics_Study.md) or [Turkish (TR)](Tokenomics_Study_TR.md)
- **Hardcap & Halving Analysis**: [English](Hardcap_Analysis.md) or [Turkish (TR)](Hardcap_Analysis_TR.md)
- **Masternode Setup Guide**: [English](Masternode_Setup.md) or [Turkish (TR)](Masternode_Setup_TR.md)
- **Miner Registration Guide**: [English](Miner_Registration.md) or [Turkish (TR)](Miner_Registration_TR.md)
- **Tokenized Assets Study**: [English](Tokenized_Assets_Study.md) or [Turkish (TR)](Tokenized_Assets_Study_TR.md)
- **IDEAS FOR A NEW PROOF ALGORITHM**: [English](IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM.md) or [Turkish (TR)](IDEAS%20FOR%20A%20NEW%20PROOF%20ALGORITHM_TR.md)
- **Security Audit Report**: [English](Security_Audit.md) or [Turkish (TR)](Security_Audit_TR.md)

Mainnet vs Testnet Parameter Comparison
---------------------
To ensure consistency across network configurations, the differences between Mainnet and Testnet are detailed below:

| Parameter | Mainnet Value | Testnet Value |
| --- | --- | --- |
| **P2P Port** | 27999 | 27989 |
| **RPC Port** | 27979 | 27919 |
| **Base58 Address Prefix** | `KT...` (starts with KT) | `kt...` (starts with kt) |
| **Bech32 Address HRP** | `kt...` | `tk...` |
| **Coinbase Maturity** | 100 blocks | 15 blocks |
| **Stake Min Age** | 1 hour (3600 seconds) | 0 seconds |
| **Stake Min Depth** | 100 blocks | 30 blocks |
| **Stake Min Depth V2** | 600 blocks | 30 blocks |
| **Developer Fund Address** | `KTMbi3v9yXtJ4z3QuWG5urXVn5WwxHBEAfm` | `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` |
| **Bootstrap Faucet Address** | `KTP9wyzSbStzXa8xNuZB4pXytzDZkSFsQKh` | `ktNSLsNGPmkpkSZHNM9Pmz4PYjBRZhrVnRz` |
| **Treasury Start Height** | block 2,880 | block 200 |
| **Masternode Payout Enforcement** | block 1,200 | block 1,200 |
| **UPGRADE_POS Activation** | block 200 | block 200 |
| **UPGRADE_ADAM Activation** | block 200 | block 200 |
| **UPGRADE_ADAM_V2 Activation**| block 705 | block 300 |
| **UPGRADE_POMBL Activation** | block 2,000 | block 400 |
| **UPGRADE_MODELD Activation** | block 2,200 | block 500 |


Development
---------------------
The KristaTech repo's [root README](/README.md) contains relevant information on the development process and automated testing.

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
Distributed under the [MIT software license](/COPYING).
This product includes software developed by the OpenSSL Project for use in the [OpenSSL Toolkit](https://www.openssl.org/). This product includes
cryptographic software written by Eric Young ([eay@cryptsoft.com](mailto:eay@cryptsoft.com)), and UPnP software written by Thomas Bernard.
