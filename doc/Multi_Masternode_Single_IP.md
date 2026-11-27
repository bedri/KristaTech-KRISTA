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
4. **Strict Hot/Cold Signing Isolation (KTDIP3):** Under KTDIP3 (`UPGRADE_DIP3` active from Block #1 on Mainnet, Testnet & Regtest), the controller wallet stores local drafts, aliases, and encrypted operator keys inside `wallet.dat` (`mnmeta`, replacing the retired `masternode.conf`), and via 1-click GUI **Start** or `protx register` only broadcasts on-chain `ProRegTx` / `ProUpRegTx` / `ProUpRevTx` transactions — never signing LLMQ quorum shares, block proposals, or coordinator messages. The VPS Hot Node (`masternode=1` + `activemasternode.conf`) authenticates strictly via its pure operator BLS key (`blsPubKeyOperator` with empty `ecdsaSig` via `SignBLSOperatorOnly`), keeping operator keys cleanly separated from owner collateral and `scriptPayout` / `scriptOperatorPayout` addresses.

---

## 2. Controller Storage (`wallet.dat`) & Hot Configuration Files

### A. Cold Wallet (Controller / Host) — Inside `wallet.dat` (`mnmeta` + `protx`)
Cold controller metadata (aliases, `IP:port`, 15-confirmation pending drafts, and encrypted operator keys) is stored natively inside encrypted `wallet.dat` (`mnmeta` records). No manual `masternode.conf` editing is needed (existing `masternode.conf` files are automatically migrated once into `wallet.dat` and archived as `masternode.conf.migrated` with `0600` permissions and plaintext WIF keys redacted as `***REDACTED***`):

```bash
# Generate Hot operator BLS keypair & Proof-of-Possession (blsPoP required for ProRegTx v2 from Height 1):
# protx bls_generate  -> {"secret": "...", "public": "<MN1_BLS_PUB>", "pop": "<MN1_BLS_POP>"}
# Register multiple nodes sharing one VPS IP directly on-chain:
# - GUI Wizard Step 3: paste <MN1_BLS_PUB>:<MN1_BLS_POP> into Operator BLS Public Key
# - CLI/RPC: pass <MN1_BLS_PUB> as arg 5 and <MN1_BLS_POP> as arg 9 (optional if operator key is local in wallet.dat)
protx register <MN1_TXID> 1 203.0.113.10:27999 <MN1_OWNER_ADDR> <MN1_BLS_PUB> <MN1_VOTE_ADDR> 0 <MN1_PAYOUT_ADDR> <MN1_BLS_POP>
protx set_alias <MN1_TXID>:1 mn1_203
protx wallet_list
```

*Note that different aliases may share the same IP and default port (`Rule 8`).*

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
