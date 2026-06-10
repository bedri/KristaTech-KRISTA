# Multi-Wallet Support in KristaTech Core

KristaTech Core utilizes a **single-active-wallet architecture** to ensure maximum consensus predictability, stability, and secure integration with the Masternode subsystem. 

While legacy versions of the upstream codebase contained experimental multi-wallet loading capabilities, KristaTech Core disables simultaneous multi-wallet loading in both the GUI and the headless daemon to prevent potential race conditions during hybrid Proof-of-Stake (PoS) and ADAM consensus round signing.

---

## Specifying a Custom Wallet

Although you cannot load multiple wallets simultaneously within a single running instance, you can run KristaTech Core with a custom wallet file using the `-wallet` command-line option.

### Running with a custom wallet file:
```bash
# Start the headless daemon with a custom wallet filename
./src/kristatechd -wallet=my_custom_wallet.dat

# Start the Qt GUI client with a custom wallet filename
./src/qt/kristatech-qt -wallet=my_other_wallet.dat
```

By default, if the `-wallet` parameter is not provided, the node will search for and load `wallet.dat` inside the node's data directory. If the specified file does not exist, a new, empty wallet will be generated with that name.

---

## Working with Multiple Node Directories (Recommended)

If your workflow requires managing multiple distinct wallets concurrently (for example, to run several local staking nodes or test validator sets), the recommended and safest approach is to run separate, isolated daemon/cüzdan instances with distinct data directories:

```bash
# Run Node 1 using data directory /home/user/.kristatech1
./src/kristatechd -datadir=/home/user/.kristatech1 -port=27999 -rpcport=27979

# Run Node 2 using data directory /home/user/.kristatech2
./src/kristatechd -datadir=/home/user/.kristatech2 -port=28001 -rpcport=28002
```
This keeps database states, block indexes, and cryptographic keys completely separated.
