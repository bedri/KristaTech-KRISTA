# KristaTech Masternode Setup Guide (VPS / Cold Node)

This guide provides the necessary steps to securely set up a Masternode (Cold Node) on the KristaTech (KRISTA) network. Thanks to the **Multinode** support and dynamic network mapping used in the modern wallet version, specifying the external IP address (`masternodeaddr`) in the VPS configuration is no longer required.

---

## 1. Requirements

To run a Masternode, you will need:
* **Collateral:** Exactly **4200 KRISTA** coins.
* **Cold Wallet (Controller Wallet):** KRISTA-QT (Desktop GUI Wallet). Holds your coins securely and manages the masternode activation.
* **Hot Wallet (VPS Node / Server):** A Virtual Private Server (VPS) that runs 7/24.
  * **Recommended VPS Specifications:**
    * Operating System: Ubuntu 24.04 or 26.04 LTS (x64)
    * Hardware: At least 1 vCPU, 2 GB RAM, 20 GB SSD
    * Network: 1 Static IPv4 address
    * Default Port: `27999` (Masternodes are required to run on this port)

---

## 2. Step 1: Cold Wallet (Controller) Configuration

### Option A: GUI 1-Click Wizard (`Create Masternode Controller`) — Recommended

You can set up the entire Cold Controller side directly from the desktop GUI without opening Debug Console or editing `masternode.conf` manually:

1. Open your **KRISTA-QT** wallet, wait for sync, and ensure you have at least `4200 KRISTA` (+ small network fee) available.
2. Go to the **MASTERNODES** tab and click **Create Masternode Controller** at the bottom-right.
3. **Step 1 (Intro)**: The wizard checks your wallet for an existing unlocked `4200 KRISTA` UTXO (or prepares to create one automatically). Click **NEXT**.
4. **Step 2 (Name)**: Enter a unique alias without spaces (e.g., `mn1`) and click **NEXT**.
5. **Step 3 (Address & BLS Key)**:
   - Enter your VPS public IPv4 address (e.g., `157.90.130.224`) and P2P port (`27999` default; Rule 8 supports multiple nodes on the same IP).
   - Leave **Operator BLS Public Key (optional)** blank to have the wallet automatically generate and derive the deterministic BLS12-381 operator keypair (`DeriveBLSFromCKey`) and output `<alias> <privkey> <bls_privkey_hex>`, **or** paste an external 48-byte hex `blsPubKeyOperator` (from `protx bls_generate` on an independent Hot Operator node) to separate operator signing (`operatorPubKey=<hex>`).
   - Click **CREATE**.
6. The wallet automatically creates/locks the `4200 KRISTA` collateral UTXO, saves the entry to `masternode.conf`, and displays a **Masternode Created** popup with the `activemasternode.conf` line (`<alias> <privkey> <bls_privkey_hex>`, or `operatorPubKey=<hex>` when using an external operator BLS key). Click **COPY LINE** and apply it to `~/.kristatech/activemasternode.conf` on your VPS (Step 2 below).
7. Once the collateral reaches **15 confirmations**, simply click **Start** on that row in the **MASTERNODES** tab — the wallet automatically signs and broadcasts the KTDIP3 `ProRegTx` on-chain registration (`PRE_ENABLED` while in mempool, `ENABLED` after 1 block). You can monitor aggregate node status from the reorderable **My Masternodes** card on the **HOME** overview.

---

### Option B: Manual Console Setup (Advanced)

Perform the following steps sequentially on your desktop GUI wallet where you hold your KRISTA coins:

### 2.1. Generate a Masternode Address and Send Collateral
1. Open your desktop wallet and wait for it to fully sync.
2. Go to the **Receive** tab, give your masternode a label (e.g., `mn1`), and generate a new address.
3. Send **exactly 4200 KRISTA** to this address.
4. Wait for the transaction to receive at least **15 confirmations** on the blockchain.

> [!IMPORTANT]
> Ensure that the transaction fee is not deducted from the 4200 KRISTA amount when sending in Step 3. After the transaction is sent, you must have a single UTXO containing exactly `4200 KRISTA`.

### 2.2. Generate Masternode Keys (ECDSA & BLS)
1. From the wallet menu, go to **Settings -> RPC Debug Console** (or click the **Debug Console** icon in the TopBar).
2. Type the following command to generate a unique ECDSA keypair (plus derive/generate an operator BLS key via `protx bls_generate` for separated Hot/Cold KTDIP3 setups):
   ```bash
   createmasternodekey
   ```
   ```bash
   protx bls_generate
   ```
   *The console will display a long private key (e.g., `93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg`). Copy and save this key securely. This is your **Masternode Private Key (ECDSA)**.*
3. **(Optional / Legacy)** BLS12-381 signing keys are deterministically derived from each Masternode or validator `secp256k1` ECDSA private key via `DeriveBLSFromCKey` (`BLST_KeyGen(sk_ecdsa)` / HKDF-SHA256) and automatically persisted in `wallet.dat` (`AddBLSKey`, or on demand via the 1-argument `setblsprivkey <address>` RPC). Manual BLS key generation is therefore **not required**; `createblsprivkey` is retained strictly as an optional legacy override:
   ```bash
   createblsprivkey
   ```
   *If invoked manually, the console outputs a 64-character hex BLS secret key. In standard deployments, leave the optional 3rd column of `activemasternode.conf` blank so the daemon deterministically derives and pins the canonical `DeriveBLSFromCKey` keypair.*

### 2.3. Get the Collateral Transaction Output (UTXO Information)
1. In the debug console, run:
   ```bash
   getmasternodeoutputs
   ```
2. The output will look like this:
   ```json
   [
     {
       "txhash": "b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234",
       "outputidx": 0
     }
   ]
   ```
   Note down the `"txhash"` (Transaction ID) and `"outputidx"` (Output Index).

### 2.4. Edit `masternode.conf` on the Cold Wallet
1. In your desktop wallet, click the **Config Files** icon in the TopBar and choose **Open Masternode Configuration** (`masternode.conf`).
2. Add a line at the bottom of the file in the following format:
   ```text
   # Format: [alias] [IP:27999] [masternodeprivkey] [collateral_output_txid] [collateral_output_index]
   mn1 YOUR_VPS_IP:27999 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234 0
   ```
   *Replace `YOUR_VPS_IP` with your server's public IPv4 address.*
3. Save the file, close the KRISTA-QT wallet, and **restart** it.

---

## 3. Step 2: Hot Wallet (VPS Server) Configuration

Follow these steps on your VPS to set up and configure the KRISTA daemon (`kristatechd`).

### 3.1. Install Required Dependencies
Update system packages and install prerequisites on your Ubuntu server (`libgmp-dev` is required; build `blst` 0.3.13 + Berkeley DB 4.8 via `depends/`, or use `./contrib/install_db4.sh` / `--with-incompatible-bdb` — see [build-unix.md](build-unix.md)):
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install build-essential libtool autotools-dev autoconf pkg-config libssl-dev libgmp-dev libevent-dev bsdmainutils python3 libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev libminiupnpc-dev libzmq3-dev -y
```
Compile the KRISTA wallet or transfer the compiled binaries to your server.

### 3.2. Server `kristatech.conf` Configuration
Create the data directory (default: `~/.kristatech/`) and edit `kristatech.conf`:
```bash
mkdir -p ~/.kristatech
nano ~/.kristatech/kristatech.conf
```
Paste the following configuration:
```ini
# VPS / Masternode Settings
masternode=1
listen=1
txindex=1
server=1
daemon=1

# RPC Settings (It is recommended to only allow local connections)
rpcuser=kristarpc
rpcpassword=ChooseAVeryStrongPasswordForYourServer
rpcallowip=127.0.0.1

# Network Settings
maxconnections=256
```
> [!IMPORTANT]
> Do NOT add `masternodeaddr` or `masternodeprivkey` parameters to `kristatech.conf`. These settings are either deprecated or kept in a separate file.

### 3.3. Server `activemasternode.conf` Configuration (Multinode Mode)
Under the modern **Multinode** architecture, masternode private key(s) are stored in `activemasternode.conf`:
```bash
nano ~/.kristatech/activemasternode.conf
```
Add your **Masternode Private Key** generated in step 2.2:
```text
# Format: [alias] [activemasternodeprivkey] (optional: bls_privkey_hex)
mn1 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg
```
*If you want to run multiple masternodes under a single `kristatechd` daemon on the **same single IPv4 address** (or multiple IP addresses), you can append each masternode alias and private key on separate lines (see [Multi_Masternode_Single_IP.md](Multi_Masternode_Single_IP.md)).*

> [!NOTE]
> Setting the `bls_privkey_hex` parameter is optional. If left blank, the daemon will automatically derive the deterministic BLS12-381 key (`DeriveBLSFromCKey`) from the Masternode ECDSA private key or load the configured native BLS key from the wallet database.

> [!TIP]
> **Cooperative Mining, Strict Hot/Cold Isolation, and KTDIP3 Deterministic State (Hot Wallet):**
> * **Strict Hot/Cold Signing Isolation**: Under unified KTDIP3 (`UPGRADE_DIP3` active from Block #1 on Mainnet & Testnet), the Cold Wallet (`masternode.conf`) acts strictly as a controller to broadcast on-chain `ProRegTx` / `ProUpServTx` / `ProUpRegTx` / `ProUpRevTx` transactions and never signs LLMQ quorum shares, block proposals, or coordinator messages. The Hot Node (`masternode=1` + `activemasternode.conf`) signs strictly with its pure operator BLS key (`blsPubKeyOperator` + `SignBLSOperatorOnly`, empty `ecdsaSig`), cleanly separating operator credentials from owner collateral and `scriptPayout` / `scriptOperatorPayout` payout scripts (`AppendDip3OrDirectPayment`).
> * **Deterministic On-Chain State & PoSe (`CDeterministicMNManager`)**: Every active Masternode's state is tracked deterministically in `evodb` (`ProRegTx` with `>= 15` collateral confirmations for `4,200 KRISTA`, `blsPubKeyOperator`, and `ip:port`), eliminating legacy P2P `mnb`/`mnp` drift. Service updates or PoSe revivals are broadcast via `ProUpServTx` (`protx update_service`).


### 3.4. Start the VPS Daemon
Run the daemon:
```bash
kristatechd
```
> [!NOTE]
> If this is the first time starting the wallet with `txindex=1` or if the wallet was previously run without transaction indexing, run it with the `-reindex` flag:
> `kristatechd -reindex`

Monitor the synchronization progress using:
```bash
kristatech-cli getblockchaininfo
kristatech-cli mnsync status
```
*Wait until `"IsBlockchainSynced": true` and `"IsMasternodeListSynced": true` in `mnsync status` output.*

---

## 4. Step 3: Registering and Starting the Masternode

1. Ensure the VPS daemon is fully synchronized and the collateral transaction has at least 15 confirmations.
2. On the Cold Wallet (Controller):
   * Unlock your wallet if it is encrypted (**TopBar -> Wallet Locked -> Unlock Wallet**).
   * Navigate to the **MASTERNODES** tab. You should see `mn1` with status "MISSING" or "PRE_ENABLED".
   * **KTDIP3 (Recommended, GUI 1-Click)**: Select the masternode and click **Start** (or **Start All** / **Start Inactive/s**). When `UPGRADE_DIP3` is active, the wallet automatically uses the configured/derived `blsPubKeyOperator`, signs with the owner key, and broadcasts the on-chain `ProRegTx` (`PRE_ENABLED` while in mempool, `ENABLED` once mined into the next block). No RPC Debug Console or CLI command is required.
   * **Manual CLI / RPC (Optional)**: You may also generate an external operator keypair with `protx bls_generate` and register via `protx register <colHash> <colIdx> <ip:port> <ownerAddr> <opBlsPub> <voteAddr> <opRewardBps> <payoutAddr>`, and inspect registered nodes with `protx list detailed` or `listmasternodes`.

---

## 5. Verification and Monitoring

To verify the status of your masternode, run the following command **on the VPS server**:
```bash
kristatech-cli getmasternodestatus
```

If the masternode is successfully active, the output will look like this:
```json
[
  {
    "alias": "mn1",
    "txhash": "b2c0199e7152063fb55848c9497e28a47895e6cd4fb5d7c34b69c45a7b6b1234",
    "outputidx": 0,
    "netaddr": "YOUR_VPS_IP:27999",
    "addr": "kristacollateraladdress...",
    "status": 4,
    "message": "Masternode successfully started (KTDIP3)"
  }
]
```

If you see `"status": 4` and `"message": "Masternode successfully started (KTDIP3)"` (or without `(KTDIP3)` on pre-DIP3 entries), your masternode is successfully running and active on-chain.

### Common Status Codes:
* `0 (ACTIVE_MASTERNODE_INITIAL)`: Node just started, not yet announced from the cold wallet.
* `1 (ACTIVE_MASTERNODE_SYNC_IN_PROCESS)`: Node sync is in progress. Wait for it to complete.
* `3 (ACTIVE_MASTERNODE_NOT_CAPABLE)`: Node is not capable of running (IP address mismatch, wallet locked, etc.). Refer to `message` for details.
* `4 (ACTIVE_MASTERNODE_STARTED)`: Node is running successfully.

---

## 6. Staking, Rewards, and Developer Fund Notes
* **Developer Fund Staking Exclusion**: To ensure maximum decentralization and prevent the developer treasury from centralizing consensus weight, the protocol strictly filters and excludes Developer Fund outputs from participating in Proof-of-Stake staking.
* **Masternode Rewards and Block Phases**: Masternodes can be set up and activated **at any time**, regardless of the current block height. From Block `200` to `4,999` (`IsAdamValidatorSplitActive`), Active Masternodes earn rewards through the **40% Active Validator Pool** (`~2.857%` per solved MN committee seat) and **60% Coordinator** share when elected. Starting at **Block `5,000` (`UPGRADE_MODELD`)**, the full Model D payout split activates, adding the **35% Passive Masternode Queue** and **10% Active LLMQ Quorum** (shared equally across the active dynamic BFT LLMQ quorum members: `3-of-5` baseline scaling `5 -> 9 -> 11 -> 15` via `GetTargetQuorumSize`) alongside the **15% Producer** and **40% Active Validator** shares (routed via `AppendDip3OrDirectPayment` to `scriptPayout` and optional `scriptOperatorPayout`).

---

## 7. Security and DoS/DDoS Mitigation

Since Masternode IP addresses are announced over the public P2P network, active LLMQ Quorum members are subject to targeted Denial of Service (DoS/DDoS) attacks. To protect your node and secure the stability of the consensus network, applying the following security standards is **highly recommended**.

### A. UFW Firewall Configuration
Keep only the P2P connection port (`27999`) open to the public. Restrict RPC (`27979`) and SSH (`22`) ports to prevent unauthorized access.

```bash
# Clear UFW and set default rules
sudo ufw default deny incoming
sudo ufw default allow outgoing

# Open only the P2P port to everyone
sudo ufw allow 27999/tcp comment 'KristaTech P2P'

# Open the SSH port only to your trusted management IP (Replace 1.2.3.4 with your own IP address)
# WARNING: If you enable UFW without allowing your IP first, you will lose SSH access to your server!
sudo ufw allow from 1.2.3.4 to any port 22 proto tcp comment 'Safe SSH'

# Allow RPC only from localhost or your authorized treasury portal server
sudo ufw allow from 127.0.0.1 to any port 27979 proto tcp comment 'Local RPC'

# Enable the firewall
sudo ufw enable
```

### B. Fail2Ban for DDoS and Resource Exhaustion Protection
Install `fail2ban` to automatically drop IP addresses that spam invalid connection requests or junk traffic:

```bash
sudo apt-get install fail2ban -y
```

Add the following configuration to `/etc/fail2ban/jail.local`:

```ini
[kristatech-p2p]
enabled = true
port = 27999
filter = nosmtp
logpath = /root/.kristatech/debug.log
maxretry = 5
findtime = 600
bantime = 86400
action = iptables-multiport[name=kristatech, port="27999", protocol=tcp]
```

### C. kristatech.conf Optimizations
Limit system memory (RAM) and CPU resource consumption of your daemon by adding the following limits to your `kristatech.conf` file:

```ini
# Limit the maximum active P2P connections (default: 256)
maxconnections=64

# Keep server active but restrict RPC to localhost
server=1
rpcallowip=127.0.0.1
```

> [!TIP]
> **Sentry Node Architecture:** If you want to hide your masternode behind DDoS-protected relay servers (Sentry Nodes) without exposing your real node IP, refer to [Sentry_Node_Setup.md](Sentry_Node_Setup.md).

