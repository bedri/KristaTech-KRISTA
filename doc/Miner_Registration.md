# Non-Masternode Miner Registration System

To support cooperative block production under the **ADAM (A Decentralized Approach Model)** consensus framework, KristaTech Core implements a **Non-Masternode Miner Registration System**. 

While Masternodes are automatically eligible for election in the validator and coordinator sets if their count is sufficiently high, this registration system allows standard nodes/wallets to lock collateral or perform an initial proof-of-work registration to join the active miner pool.

---

## 1. Registration Methods

Miners can register themselves using one of two cryptographic paths:

### A. Coin-Lock Registration (`lock`)
The **Coin-Lock** path allows a node to secure a place in the miner pool (`Sub-Track 1B`, 1 priority seat in Track 1 with spillover to all 8 collateralized seats plus VRF overflow into Track 2 `Tier 2A`) by locking up a collateral amount.
* **Collateral Amount:** **250 KRISTA** (`MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN`, enforced in mempool via `bad-txns-coinlock-amount-too-small` and in `GetAdamMinerPool`; consensus and manual `registerminer lock` accept `250 KRISTA` Coin-Locks at **all block heights**, while background `AutoRegisterMiner` waits until height `>= 2,200` so bootstrap rewards stay liquid).
* **Timelock Duration:** Must be locked for at least **2,880 blocks** (`nRegPeriod`, ~24 hours at 30s block spacing on Mainnet; `100 blocks` on Testnet/Regtest once Model D is active, `2,880 blocks` pre-Model D) from the registration height. The wallet adds a `+20` block confirmation buffer by default (`2,900 blocks` on Mainnet, `120 blocks` on Testnet/Regtest post-Model D).
* **Script Structure (`MatchCoinLockRegistration`):** Uses absolute `OP_CHECKLOCKTIMEVERIFY` (CLTV) script outputs (`<pubkey> OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`). The funds cannot be spent, moved, or unlocked until the block height reaches the specified target.

### B. PoW-Lock Registration (`pow`)
The **PoW-Lock** path allows nodes without the 250 KRISTA collateral to participate by solving a CPU puzzle to prove resource dedication.

* **Funded PoW Registration (`POW_LOCK_REGISTRATION_AMOUNT`):** A node with a mature UTXO locks **10 KRISTA** via `OP_CHECKLOCKTIMEVERIFY` and solves a CPU hash puzzle (`GetMinerPoWLimit`: 28-bit zero prefix on Mainnet, 20-bit on Testnet, 8-bit on Regtest) bound to an ancestor block within the last `100` blocks to create a timelocked output. The 10 KRISTA remains in the miner's wallet and becomes spendable again after the lock expires.
* **Anti-Sybil PoW-Lock Consensus Rules (`nAdamDualTrackHeight` = Block 200):**
  - **Input UTXO Maturity (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`):** Every input UTXO funding a non-zero `PoW-Lock` registration transaction must have at least **100 confirmations**, preventing rapid UTXO splitting to mass-register keys.
  - **Block & Transaction Rate Limits (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`):** At most **1 `PoW-Lock` output per transaction** and at most **3 `PoW-Lock` transactions per block** are accepted by consensus (`CheckBlock`) and mempool (`AcceptToMemoryPool`), and at most **1 `PoW-Lock` transaction per block template** is included by the block assembler. Any funded `PoW-Lock` output locking less than `10 KRISTA` (`POW_LOCK_REGISTRATION_AMOUNT`) is rejected by both mempool policy and block validation (`bad-txns-powlock-amount-too-small`).
  - **Dual-Track ($8 + 3$) Committee Quota:** Within the **8-seat Collateralized Track (`ADAM_COLLATERALIZED_TRACK_QUOTA = 8`)**, **7 seats (`ADAM_MASTERNODE_TRACK_QUOTA = 7`)** are reserved for Active Masternodes (`4,200 KRISTA`) and **1 seat (`ADAM_COIN_LOCK_TRACK_QUOTA = 1`)** is reserved for `250 KRISTA` Coin-Lock validators (with immediate intra-Track-1 spillover between the two), while Open PoW miners compete for the **3-seat Open PoW Track (`ADAM_OPEN_POW_TRACK_QUOTA = 3`)**.
* **Zero-Coin PoW Registration (`0 KRISTA Balance`, Hardened under `UPGRADE_HARDENING_V2` at Block #1):**
  - **Bootstrap Support (`GetFundedAdamMinerPoolCount < 13`):** When the **funded** validator pool (`GetFundedAdamMinerPoolCount`, counting `4,200 KRISTA` Active Masternodes, `250 KRISTA` Coin-Locks, and `10 KRISTA` Funded PoW-Locks while excluding `0 KRISTA` Zero-Coin PoW and Block 1..199 coinbase bootstrap fallback keys) has fewer than `nAdamMinersCount + 2` (`13`) funded validators, nodes/wallets with **0 KRISTA balance** can register automatically without requiring any coins or previous UTXOs. Under `UPGRADE_HARDENING_V2`, real `Block 1..199` X11KVS coinbase winners (`cachedBootstrapKeys`) are admitted into the unfunded pool **before** `Zero-Coin PoW` keys so Sybil registrations can never starve real early block miners, and active `Zero-Coin PoW` keys are capped at **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16`** distinct keys (newest registrations first). Once 13 or more funded validators are active on-chain, Zero-Coin PoW registration closes cleanly across consensus, mempool, RPC, and wallet layers (with immediate stale `0x01:0` mempool eviction on tip updates).
  - **Strict Economic Election Priority (`Funded > Bootstrap > Zero-Coin`):** Under `UPGRADE_HARDENING_V2`, `ElectDualTrackValidators` partitions Open PoW (`Track 2`) and standby queues into three strict sub-tiers — **Tier 2A (unselected `250 KRISTA` Coin-Lock overflow + `10 KRISTA` Funded PoW-Lock competing by VRF)** $\to$ **Tier 2B (`Blocks 1..199` Bootstrap)** $\to$ **Tier 2C (`0 KRISTA` Zero-Coin PoW)** — and `SelectAdamNodes` elects `coordinatorOut` across all **Funded Validators first** via splitting-invariant `4,200 / 250 / 10` stake-weighted VRF (`MN ∪ Coin-Lock ∪ 10 KRISTA PoW-Lock`), falling back to `Bootstrap` and finally `Zero-Coin PoW` only when zero funded validators exist. Consequently, `0 KRISTA` keys can **only fill empty leftover seats** and can **never** displace a single `4,200 KRISTA` Masternode, `250 KRISTA` Coin-Lock, or `10 KRISTA` PoW-Lock validator from either Coordinator or Committee seats.
  - **CPU-Bound `HashX11KVS` Puzzle & `10`-Block Fresh Challenge Window:** Under `UPGRADE_HARDENING_V2`, `ComputeZeroCoinRegistrationHash` constructs a deterministic 80-byte header (`"KRZC"[4] || challenge[32] || compressedPubKey[33] || "SYBILV2"[7] || le32(nNonce)[4]`, requiring a 33-byte compressed pubkey) and hashes it with 7-level (`127`-branch `X11KV`) **`HashX11KVS`** against `GetZeroCoinPoWLimit(..., true)` (**14 leading zero bits** on Mainnet [`0003ffff...`], **12 bits** on Testnet [`000fffff...`], **8 bits** on Regtest [`00ffffff...`]). The referenced `challenge` block must be within the last **`10` blocks** (`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` in consensus, `8` blocks in mempool acceptance), preventing offline transaction stockpiling.
  - **Consensus & Wallet Safety:** The registration transaction uses a **0 KRISTA** output (`nValue = 0`) and a sentinel input outpoint (`0x01:0`, at most 1 per block and 1 in mempool at a time; keys already active in `GetAdamMinerPool` are rejected from mempool). Consensus rules (`CheckTransaction`) grant an explicit exception for valid Zero-Coin PoW registration scripts, and the wallet treats sentinel `0x01:0` inputs like coinbase inputs (no false wallet double-spend conflicts), commits Zero-Coin PoW transactions to `wallet.dat` only after mempool acceptance, and limits each wallet to at most **one unfunded `0 KRISTA` seat** (additional seats require a `10 KRISTA` Funded PoW-Lock or `250 KRISTA` Coin-Lock).
  - **Short Lease Duration (`240` Blocks):** Under `UPGRADE_HARDENING_V2`, Zero-Coin PoW registrations have a **`240`-block** (`~2 hours` on Mainnet; **`50` blocks** on Testnet/Regtest post-Model D) active lease (`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240`, wallet locktime `currentHeight + 260`).

---

## 2. Command-Line & Configuration Options (Regtest Only)

On **Regtest (`-regtest`) only**, deterministic public/private keys are derived at startup to support local unit and functional test suites (never active on Mainnet or Testnet).

### `-adamminerseed` *(Regtest Only)*
- Configures the base seed string from which the 15 Regtest deterministic keys are derived.
- **Default:** `"adam_miner_seed_"`
- **Usage:**
  ```bash
  ./src/kristatechd -regtest -adamminerseed=my_secret_miner_seed_string
  ```

---

## 3. RPC Commands

Miners and operators interact with the registration system using these JSON-RPC commands:

### `registerminer`
Builds, signs, and broadcasts a miner registration transaction (automatically pairing a deterministic BLS12-381 key in `wallet.dat`). Rejects early with a descriptive error if the node is still in Initial Block Download (`IsInitialBlockDownload`), if the key is already active/pending in the mempool, or if another Zero-Coin PoW transaction is already waiting in the mempool.

#### Arguments:
1. `mode` (string, required): The registration mode, must be either `"lock"` or `"pow"`.
2. `target` (string/numeric, optional):
   - For `"lock"`: Target unlock block height (must be a block height `< 500,000,000` [`LOCKTIME_THRESHOLD`], $\ge \text{current height} + \text{nRegPeriod}$, and $\le \text{current height} + 5,760$; defaults to `current height + nRegPeriod + 20` if omitted or `0`).
   - For `"pow"`: Current tip block hash used as the puzzle challenge (defaults to `chainActive.Tip()->GetBlockHash()` if omitted or `""`).
3. `address_or_pubkey` (string, optional):
   - A KRISTA address or hex-encoded compressed public key belonging to the local wallet. If omitted, a fresh key is automatically allocated from the wallet keypool (`"miner-reg"` label).

#### Example (1-Argument Automatic Mode):
```bash
# Automatically picks best block hash + fresh wallet key (10 KRISTA Funded PoW or 0 KRISTA Bootstrap PoW)
./src/kristatech-cli registerminer pow

# Automatically picks height + nRegPeriod + 20 + fresh wallet address (250 KRISTA Coin-Lock, manual RPC valid at any height)
./src/kristatech-cli registerminer lock
```

#### Example (Explicit Coin-Lock):
```bash
# Get current height and add 3000 blocks for the lock target
./src/kristatech-cli registerminer lock 3800 KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J
```

#### Example (Explicit PoW-Lock):
```bash
# Fetch best block hash
best_hash=$(./src/kristatech-cli getbestblockhash)
# Get a pubkey to register
pubkey=$(./src/kristatech-cli validateaddress KTiwjsc71G7gyd3yhk6Ycrg3DFLy1wq7G8J | jq -r .pubkey)
# Submit PoW registration
./src/kristatech-cli registerminer pow "$best_hash" "$pubkey"
```

### `getadamminers`
Returns the list of all currently active and registered miners (`index`, `address`, `pubkey`, `track` [`"masternode"`, `"coin_lock"`, `"pow_lock"`, `"zerocoin_pow"`, `"bootstrap"`], and `unlock_height`), backed by the persistent LevelDB `adamregcache_v2` block registration cache so cold-start queries complete in milliseconds without rescanning historical blocks from disk.

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

1. **Active Pool & Automatic Funded Promotion Check**:
   The engine checks if the wallet already has a **funded** validator registration (`masternode`, `coin_lock`, or `pow_lock`) in the active pool at the current height; if so, auto-registration exits silently. If the wallet only holds an **unfunded** entry (`zerocoin_pow` or `bootstrap`), `AutoRegisterMiner` stays silently idle while matured funds are below `10.01 KRISTA`, and **automatically promotes** the node to a funded `10 KRISTA` PoW-Lock (or `250 KRISTA` Coin-Lock at height $\ge 2,200$) as soon as matured ($\ge 100$-confirmation) coins are available — naturally closing the `< 13` bootstrap gate without operator intervention.

2. **Spam Prevention**:
   To prevent registration transaction spam, the engine tracks the height of the last broadcasted registration. If a registration was sent less than **50 blocks** ago, the auto-registration is deferred.

3. **Key Management**:
   The engine automatically attempts to generate or retrieve the corresponding BLS key for the miner public key. If the wallet is locked, it logs a warning:
   `AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.`

4. **Mode Selection & Collateral Check**:
   The engine evaluates the wallet's available balance and the current block height to select the appropriate registration path:
   - **Coin-Lock (PoL) Mode**: If the current block height is $\ge$ 2,200 and the available balance is at least **250 KRISTA** (collateral) plus fees (0.01 KRISTA buffer), the engine constructs a Coin-Lock transaction with a lock duration of **2,900 blocks** in the future. Below height 2,200 (bootstrap phase), Coin-Lock auto-registration is disabled on Mainnet to keep staker/miner rewards liquid.
   - **PoW-Lock Mode**: If the block height is < 2,200 or the balance is insufficient for a Coin-Lock, the engine falls back to PoW-Lock mode. If the wallet has at least **10.01 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT + 0.01 * COIN`) in mature ($\ge 100$-confirmation) UTXOs, it solves the standard PoW puzzle (`GetMinerPoWLimit`) and locks **10 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT`) in a timelocked output for **2,900 blocks** in the future. If the wallet has `< 10.01 KRISTA`, holds no active unfunded seat yet, and the **funded** validator pool (`GetFundedAdamMinerPoolCount`) has `< 13` (`nAdamMinersCount + 2`) funded validators, it solves the **`HashX11KVS` Zero-Coin PoW puzzle** (`GetZeroCoinPoWLimit(..., true)`, 14-bit on Mainnet), creating a 0-value timelocked registration transaction for **260 blocks** (`GetZeroCoinRegPeriod + 20`) without requiring spent UTXOs.

5. **Broadcast**:
   The resulting transaction is validated by `AcceptToMemoryPool`, committed, and broadcast to the network.

---

## 6. Qt Wallet Dashboard Miner Hub

KristaTech-Qt provides an integrated **Cooperative CPU Miner & Validator Hub** directly on the Dashboard (`src/qt/kristatech/dashboardwidget.cpp`):
* **1-Click Cooperative CPU Mining Toggle**: Enables/disables internal multi-threaded CPU mining (`GenerateBitcoins`) with configurable thread count.
* **Show / Hide Toggle & Full Screen (`Tam Ekran`) Monitoring Mode**: Includes a persistent **Show / Hide Miner Hub** button (`QSettings("fMinerHubHidden")`) with live mining activity indicator (`●` / `○`) in the Transactions header bar plus a `✕` quick-hide button on the card header (expanding Recent Transactions to 100% full width when hidden), and a **1-Click Full Screen (`⛶ Full Screen` / `F11` / `Esc`)** button that expands the Miner Hub across 100% of the screen while displaying complete 34-character miner addresses.
* **Non-Blocking 1-Click Track Registration**: Provides 1-click **Track 2 (`PoW-Lock`: `10 KRISTA` / `0 KRISTA` bootstrap)** and **Track 1 (`Coin-Lock`: `250 KRISTA`)** registration buttons. PoW puzzle solving runs asynchronously on a background worker thread (`⏳ Solving PoW...`) so the Qt GUI remains responsive.
* **Live Validator & Committee Role Table**: Displays all local wallet registrations (`Coin-Lock`, `PoW-Lock`, `0-Coin PoW`), Block `1..199` coinbase fallback keys (`🏁 Bootstrap`), and active/controller Masternodes (`activemasternode.conf` & `masternode.conf`), clearly distinguishing live execution states (`● ACTIVE` local CPU threads, `🔌 Ext` external `cpuminer`/`ccminer` RPC polling, `🌐 VPS` 24/7 remote Masternodes, and `⏸ Stopped` idle local seats), next-block committee election role (`Coordinator` / `Seat #i` + V11 single-algo or V12 3-permutation puzzle pipeline), remaining lock blocks (`Pending` while unconfirmed in mempool), ADAM-era paid reward hits (`🏆`), real KRISTA earned, solved committee puzzle counts (`🧩`), and a 1-click `cpuminer` command copy button using the operator address (`--coinbase-addr=`) with `<RPC_PASSWORD>` masked.
