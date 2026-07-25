# Non-Masternode Miner Registration System

To support cooperative block production under the **ADAM (A Decentralized Approach Model)** consensus framework, KristaTech Core implements a **Non-Masternode Miner Registration System**. 

While Masternodes are automatically eligible for election in the validator and coordinator sets if their count is sufficiently high, this registration system allows standard nodes/wallets to lock collateral or perform an initial proof-of-work registration to join the active miner pool.

---

## 1. Registration Methods

Miners can register themselves using one of two cryptographic paths:

### A. Coin-Lock Registration (`lock`)
The **Coin-Lock** path allows a node to secure a place in the miner pool by locking up a collateral amount.
* **Collateral Amount:** **1,000 KRISTA** (enforced on Mainnet).
* **Timelock Duration:** Must be locked for at least **2,880 blocks** (~24 hours at 30s block spacing) from the registration height.
* **Script Structure:** Uses absolute `OP_CHECKLOCKTIMEVERIFY` (CLTV) or relative `OP_CHECKSEQUENCEVERIFY` (CSV) script outputs. The funds cannot be spent, moved, or unlocked until the block height reaches the specified target.

### B. PoW-Lock Registration (`pow`)
The **PoW-Lock** path allows nodes without the 1,000 KRISTA collateral to participate by solving a CPU puzzle to prove resource dedication.

* **Funded PoW Registration:** A node with a non-zero balance pays a minimal fee of **0.0001 KRISTA** and solves a CPU hash puzzle to create a timelocked output.
* **Zero-Coin 15-Minute PoW Registration (`0 KRISTA Balance`):**
  - **Zero-Balance Support:** Nodes/wallets with **0 KRISTA balance** can register automatically without requiring any coins or previous UTXOs.
  - **15-Minute CPU Puzzle Target:** The node solves a CPU hash target (`GetZeroCoinPoWLimit`, 21-bit zero prefix on Mainnet), requiring ~15 minutes of single-core CPU computation.
  - **Consensus Exception:** The registration transaction uses a **0 KRISTA** output (`nValue = 0`) and a sentinel input outpoint. Consensus rules (`CheckTransaction`) grant an explicit exception for valid Zero-Coin PoW registration scripts.
  - **Timelock Duration:** Timelocked for **2,900 blocks** (~24 hours / 1 day at 30s block spacing) from the registration height.

---

## 2. Command-Line & Configuration Options

At node startup, deterministic public/private keys are automatically derived to support the consensus validation engine.

### `-adamminerseed`
- Configures the base seed string from which the 15 deterministic keys are derived.
- **Default:** `"adam_miner_seed_"`
- **Usage:**
  ```bash
  ./src/kristatechd -adamminerseed=my_secret_miner_seed_string
  ```

---

## 3. RPC Commands

Miners and operators interact with the registration system using these JSON-RPC commands:

### `registerminer`
Builds, signs, and broadcasts a miner registration transaction.

#### Arguments:
1. `mode` (string, required): The registration mode, must be either `"lock"` or `"pow"`.
2. `target` (string/numeric, required):
   - For `"lock"`: The target block height at which the collateral will be unlocked (must be at least current height + 2880).
   - For `"pow"`: The block hash of the current tip (used as the puzzle challenge).
3. `address_or_pubkey` (string, required):
   - For `"lock"`: The KRISTA address that owns the collateral.
   - For `"pow"`: The hex-encoded compressed public key of the miner.

#### Example (Coin-Lock):
```bash
# Get current height and add 3000 blocks for the lock target
./src/kristatech-cli registerminer lock 3800 KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J
```

#### Example (PoW-Lock):
```bash
# Fetch best block hash
best_hash=$(./src/kristatech-cli getbestblockhash)
# Get a pubkey to register
pubkey=$(./src/kristatech-cli validateaddress KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J | jq -r .pubkey)
# Submit PoW registration
./src/kristatech-cli registerminer pow "$best_hash" "$pubkey"
```

### `getadamminers`
Returns the list of all currently active and registered miners. 

* On a fresh Regtest chain, this returns the 15 default deterministic keys.
* On Mainnet/Testnet, it scans the UTXO database for unspent Coin-Locks and PoW-Locks that satisfy the minimum duration rules and lists them along with their pool indices.

#### Example:
```bash
./src/kristatech-cli getadamminers
```

---

## 4. Consensus & Democratic Verification

When a block is constructed, the node scans the active miner pool. For the elected miner set:
1. The blockchain validates that each registration transaction output remains **unspent**.
2. The consensus engine verifies that the registration has not expired (the timelock is still active and has remaining blocks).
3. The node outputs warnings to the log if a registered miner's timelock is expiring soon (less than **240 blocks** remaining) to alert the operator to renew their registration.

---

## 5. Automatic Miner Registration (`setgenerate`)

When a node enables block generation (via `setgenerate true` or `gen=1` in config), the mining engine automatically manages miner registration in the background:

1. **Active Pool Check**:
   The engine checks if the configured miner public key is already registered in the active pool at the current height. If it is already registered, the auto-registration process exits silently.

2. **Spam Prevention**:
   To prevent registration transaction spam, the engine tracks the height of the last broadcasted registration. If a registration was sent less than **50 blocks** ago, the auto-registration is deferred.

3. **Key Management**:
   The engine automatically attempts to generate or retrieve the corresponding BLS key for the miner public key. If the wallet is locked, it logs a warning:
   `AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.`

4. **Mode Selection & Collateral Check**:
   The engine evaluates the wallet's available balance and the current block height to select the appropriate registration path:
   - **Coin-Lock (PoL) Mode**: If the current block height is $\ge$ 2,200 and the available balance is at least **1,000 KRISTA** (collateral) plus fees (0.01 KRISTA buffer), the engine constructs a Coin-Lock transaction with a lock duration of **2,900 blocks** in the future. Below height 2,200 (bootstrap phase), Coin-Lock auto-registration is disabled on Mainnet to keep staker/miner rewards liquid.
   - **PoW-Lock Mode**: If the block height is < 2,200 or the balance is insufficient for a Coin-Lock, the engine falls back to PoW-Lock mode. If the wallet balance is **0 KRISTA**, it solves the **15-minute Zero-Coin PoW puzzle** (`GetZeroCoinPoWLimit`), creating a 0-value timelocked transaction without requiring spent UTXOs. If the wallet has a non-zero balance, it solves the standard PoW puzzle with a 0.0001 KRISTA fee. Once solved, it constructs a PoW-Lock transaction with a lock duration of **2,900 blocks** in the future.

5. **Broadcast**:
   The resulting transaction is committed and broadcast to the network.
