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
* Join our Discord server [Discord Server](__decenomy_discord_link__)

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
KristaTech Core runs on specialized consensus mechanisms designed for decentralization, speed, and advanced smart contract capabilities:

- [ADAM Consensus Guide](ADAM_Consensus.md)
- [MPA Consensus Guide](MPA_Consensus.md)
- [Tokenized Assets Study](Tokenized_Assets_Study.md)
- [Security Audit Report](Security_Audit.md)

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
* Join the [KristaTech Discord](__decenomy_discord_link__).

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
