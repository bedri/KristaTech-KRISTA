# KristaTech Masternode Setup Guide (VPS / Cold Node)

This guide provides the necessary steps to securely set up a Masternode (Cold Node) on the KristaTech (KRISTA) network. Thanks to the **Multinode** support and dynamic network mapping used in the modern wallet version, specifying the external IP address (`masternodeaddr`) in the VPS configuration is no longer required.

---

## 1. Requirements

To run a Masternode, you will need:
* **Collateral:** Exactly **2100 KRISTA** coins.
* **Cold Wallet (Controller Wallet):** KRISTA-QT (Desktop GUI Wallet). Holds your coins securely and manages the masternode activation.
* **Hot Wallet (VPS Node / Server):** A Virtual Private Server (VPS) that runs 7/24.
  * **Recommended VPS Specifications:**
    * Operating System: Ubuntu 24.04 or 26.04 LTS (x64)
    * Hardware: At least 1 vCPU, 2 GB RAM, 20 GB SSD
    * Network: 1 Static IPv4 address
    * Default Port: `27999` (Masternodes are required to run on this port)

---

## 2. Step 1: Cold Wallet (Controller) Configuration

Perform the following steps sequentially on your desktop GUI wallet where you hold your KRISTA coins:

### 2.1. Generate a Masternode Address and Send Collateral
1. Open your desktop wallet and wait for it to fully sync.
2. Go to the **Receive** tab, give your masternode a label (e.g., `mn1`), and generate a new address.
3. Send **exactly 2100 KRISTA** to this address.
   > [!IMPORTANT]
   > Ensure that the transaction fee is not deducted from the 2100 KRISTA amount. After the transaction is sent, you must have a single UTXO containing exactly `2100 KRISTA`.
4. Wait for the transaction to receive at least **15 confirmations** on the blockchain.

### 2.2. Generate Masternode Keys (ECDSA & BLS)
1. From the wallet menu, go to **Tools -> Debug Console**.
2. Type the following command to generate a unique ECDSA private key for the masternode:
   ```bash
   createmasternodekey
   ```
   *The console will display a long private key (e.g., `93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg`). Copy and save this key securely. This is your **Masternode Private Key (ECDSA)**.*
3. Type the following command to generate a native BLS private key in hex format (required for block/VRF signing):
   ```bash
   createblsprivkey
   ```
   *The console will display a 64-character hex private key. Copy and save this key securely. This is your **Masternode BLS Private Key**.*

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
1. In your desktop wallet, go to **Tools -> Open Masternode Configuration File**.
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
Update system packages and install prerequisites on your Ubuntu server:
```bash
sudo apt update && sudo apt upgrade -y
sudo apt install build-essential libtool autotools-dev autoconf pkg-config libssl-dev libevent-dev bsdmainutils python3 libboost-system-dev libboost-filesystem-dev libboost-chrono-dev libboost-program-options-dev libboost-test-dev libboost-thread-dev libminiupnpc-dev libzmq3-dev libdb++-dev -y
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
maxconnections=125
```
> [!IMPORTANT]
> Do NOT add `masternodeaddr` or `masternodeprivkey` parameters to `kristatech.conf`. These settings are either deprecated or kept in a separate file.

### 3.3. Server `activemasternode.conf` Configuration (Multinode Mode)
Due to the new **Multinode** structure, active masternode private keys are stored in `activemasternode.conf`.
```bash
nano ~/.kristatech/activemasternode.conf
```
Add your **Masternode Private Key** generated in step 2.2:
```text
# Format: [alias] [activemasternodeprivkey] [bls_privkey_hex]
mn1 93HaYBVUCYjEMeeH1Y4sBGLALQZE1Yc1K64xiqgX37tGBDQL8Xg 0000000000000000000000000000000000000000000000000000000000000001
```
*If your server has multiple IP addresses assigned and you want to run multiple masternodes under a single wallet daemon, you can append them on separate lines.*

> [!TIP]
> **Cooperative Mining and Coordinator Role (Hot Wallet):**
> In the ADAM consensus framework, when this hot-node masternode is elected as the round coordinator or an active miner, the daemon will automatically scan `activemasternode.conf` to retrieve the private keys needed to sign block templates, VRF proofs, or puzzle solutions. This allows your VPS node to fully participate in block production and secure masternode rewards without needing any mature balance in `wallet.dat`.


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
   * Unlock your wallet if it is encrypted (**Settings -> Unlock Wallet**).
   * Navigate to the **Masternodes** tab. You should see `mn1` with status "MISSING" or "PRE_ENABLED".
   * Select the masternode and click **Start Alias** (or run `startmasternode alias false mn1` in the Debug Console).
   * The command should output `success` if successful.

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
    "message": "Masternode successfully started"
  }
]
```

If you see `"status": 4` and `"message": "Masternode successfully started"`, your masternode is successfully running and pinging the network.

### Common Status Codes:
* `0 (ACTIVE_MASTERNODE_INITIAL)`: Node just started, not yet announced from the cold wallet.
* `1 (ACTIVE_MASTERNODE_SYNC_IN_PROCESS)`: Node sync is in progress. Wait for it to complete.
* `3 (ACTIVE_MASTERNODE_NOT_CAPABLE)`: Node is not capable of running (IP address mismatch, wallet locked, etc.). Refer to `message` for details.
* `4 (ACTIVE_MASTERNODE_STARTED)`: Node is running successfully.
