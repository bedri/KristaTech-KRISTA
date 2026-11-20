# Single IP Multi-Masternode Logic and Configuration

In KristaTech (and other DECENOMY-based core coin projects), it is possible to run multiple masternodes on a single physical server (and a single IPv4 address). This document explains the architecture and configuration guidelines of this multi-masternode setup.

---

## 1. Architectural Logic

In the traditional Dash/PIVX masternode structure, each masternode requires a unique IP address and a separate wallet daemon process. This structure leads to high RAM/CPU usage on the VPS and additional IP leasing costs.

The **DECENOMY** core, which KristaTech is based on, natively supports running **multiple masternodes** simultaneously under a single `kristatechd` daemon process (Multi-Masternode).

### Core Principles:
1. **Single Process, Single Port:** There is only a single `kristatechd` daemon process running on the default port (`27999`) on each server.
2. **Multiple Identities:** This single process loads all masternode private keys defined in `~/.kristatech/activemasternode.conf`. It manages keep-alive consensus activity and on-chain heartbeat pings independently for each configured key.
3. **Network Validation:** The controller wallet (cold wallet) locks and broadcasts a separate 4200 KRISTA collateral transaction for each masternode. Although all nodes share the same IP:27999 address, the network recognizes them as distinct nodes because each masternode has a unique private key and TxID.
4. **Strict Hot/Cold Signing Isolation (KTDIP3 & v2):** Under KTDIP3 (`UPGRADE_DIP3` active from Block #1) and KTDIP3 v2 (`nDip3V2Height = 5,000` on Mainnet, `1` on Testnet/Regtest), the controller wallet (`masternode.conf` + 1-click GUI **Start** or `protx register`) only broadcasts on-chain `ProRegTx` / `ProUpRegTx` / `ProUpRevTx` transactions and never signs LLMQ quorum shares, block proposals, or coordinator messages. From `nDip3V2Height`, the VPS Hot Node (`masternode=1` + `activemasternode.conf`) authenticates strictly via its pure operator BLS key (`blsPubKeyOperator` with empty `ecdsaSig` via `SignBLSOperatorOnly`), keeping operator keys cleanly separated from owner collateral and `scriptPayout` / `scriptOperatorPayout` addresses.

---

## 2. Configuration Files

### A. Cold Wallet (Controller / Host) - `masternode.conf`
In the controller wallet, add a line for each masternode. The IP address and default port (`27999`) are repeated for the respective VPS hosts:

```text
# Format: alias IP:port privkey txid vout
mn1_203 203.0.113.10:27999 <MN1_PRIVKEY_WIF> <MN1_COLLATERAL_TXID> 1
mn2_203 203.0.113.10:27999 <MN2_PRIVKEY_WIF> <MN2_COLLATERAL_TXID> 1
mn3_203 203.0.113.10:27999 <MN3_PRIVKEY_WIF> <MN3_COLLATERAL_TXID> 1
```

*Note that different aliases share the same IP and default port.*

### B. Hot Node (VPS Server) - `activemasternode.conf`
In the VPS data directory (`~/.kristatech/activemasternode.conf`), list all masternode private keys associated with that host:

```text
# Format: alias activemasternodeprivkey [bls_privkey_hex]
# (If bls_privkey_hex is omitted, KristaTech Core deterministically derives the BLS12-381 key from activemasternodeprivkey via BLST_KeyGen.)
mn1_203 <MN1_PRIVKEY_WIF> <MN1_BLS_PRIVKEY_HEX>
mn2_203 <MN2_PRIVKEY_WIF> <MN2_BLS_PRIVKEY_HEX>
mn3_203 <MN3_PRIVKEY_WIF> <MN3_BLS_PRIVKEY_HEX>
```

### C. Hot Node (VPS Server) - `kristatech.conf`
In the VPS data directory (`~/.kristatech/kristatech.conf`), enable multi-masternode mode without embedding any masternode private key:

```ini
masternode=1
listen=1
txindex=1
server=1
daemon=1
```

> [!IMPORTANT]
> **Never** write `masternodeprivkey=...` or `masternodeaddr=...` inside the Hot Node's `kristatech.conf`. All active masternode private keys on the VPS must reside exclusively in `activemasternode.conf` (`0600` permissions), while `kristatech.conf` only sets `masternode=1`.

---

## 3. Advantages

1. **Low Resource Footprint:** Since a single `kristatechd` process runs on the VPS instead of multiple daemons, server memory (RAM) and CPU consumption are kept to a minimum.
2. **IP Address Savings:** Multiple masternodes can be run using a single IPv4 address without needing to lease additional IP addresses.
3. **No Port Conflicts:** All masternodes listen on the network's default `27999` port as required by the consensus protocol.
4. **Simple Network Management:** No extra network configuration (bind settings, virtual interfaces, alias IPs) is required on the server side.
