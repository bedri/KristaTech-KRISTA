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
* **Registration Fee:** A minimal fee of **0.0001 KRISTA**.
* **Puzzle Mechanism:** The node solves a CPU hash puzzle target based on the current block tip and public key.
* **Script Structure:** The registration transaction records the solved nonce and parent block hash challenge. The output is timelocked for at least **2,880 blocks** to prevent registration churn.

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
