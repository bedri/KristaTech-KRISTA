# Single IP Multi-Masternode Logic and Configuration

In KristaTech (and other DECENOMY-based core coin projects), it is possible to run multiple masternodes on a single physical server (and a single IPv4 address). This document explains the architecture and configuration guidelines of this multi-masternode setup.

---

## 1. Architectural Logic

In the traditional Dash/PIVX masternode structure, each masternode requires a unique IP address and a separate wallet daemon process. This structure leads to high RAM/CPU usage on the VPS and additional IP leasing costs.

The **DECENOMY** core, which KristaTech is based on, natively supports running **multiple masternodes** simultaneously under a single `kristatechd` daemon process (Multi-Masternode).

### Core Principles:
1. **Single Process, Single Port:** There is only a single `kristatechd` daemon process running on the default port (`27999`) on each server.
2. **Multiple Identities:** This single process loads all masternode private keys defined in the `/root/.kristatech/activemasternode.conf` file. It sends a separate keep-alive ping to the network for each private key.
3. **Network Validation:** The controller wallet (cold wallet) locks and broadcasts a separate 4200 KRISTA collateral transaction for each masternode. Although all nodes share the same IP:27999 address, the network recognizes them as distinct nodes because each masternode has a unique private key and TxID.

---

## 2. Configuration Files

### A. Cold Wallet (Controller / Host) - `masternode.conf`
In the controller wallet, add a line for each masternode. The IP address and default port (`27999`) are repeated for the respective VPS hosts:

```text
# Format: alias IP:port privkey txid vout
mn1_168 168.119.227.178:27999 2UNUY2rGp3gDL... 7eea932a0ae27634... 1
mn5_168 168.119.227.178:27999 2TMQxYrc4f2p2... 7d18aed3bcc90211... 1
mn6_168 168.119.227.178:27999 2TGtMdUx4FMZJ... d09403ccb83a4f0f... 1
```

*Note that different aliases share the same IP and default port.*

### B. Hot Node (VPS Server) - `activemasternode.conf`
In the VPS data directory (`~/.kristatech/activemasternode.conf`), list all masternode private keys associated with that host:

```text
# Format: alias activemasternodeprivkey
mn1_168 2UNUY2rGp3gDL9wCyjcXoaBqfL4qCpe5oJbEpYaTGyovSh1fa8F
mn5_168 2TMQxYrc4f2p2Vva3eEFV3t5YxUweibALxV8fnC2KYSVfuNsjP3
mn6_168 2TGtMdUx4FMZJc9TCHWoaddh5MB34jsLmjwJ1xSipNFEosimayt
```

---

## 3. Advantages

1. **Low Resource Footprint:** Since a single `kristatechd` process runs on the VPS instead of multiple daemons, server memory (RAM) and CPU consumption are kept to a minimum.
2. **IP Address Savings:** Multiple masternodes can be run using a single IPv4 address without needing to lease additional IP addresses.
3. **No Port Conflicts:** All masternodes listen on the network's default `27999` port as required by the consensus protocol.
4. **Simple Network Management:** No extra network configuration (bind settings, virtual interfaces, alias IPs) is required on the server side.
