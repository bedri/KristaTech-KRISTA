# Non-Masternode Miner Registration System

To support cooperative block production under the **ADAM (A Decentralized Approach Model)** consensus framework, KristaTech Core implements a **Non-Masternode Miner Registration System**. 

While Masternodes are automatically eligible for election in the validator and coordinator sets if their count is sufficiently high, this registration system allows standard nodes/wallets to lock collateral or perform an initial proof-of-work registration to join the active miner pool.

---

## 1. Registration Methods

Miners can register themselves using one of two cryptographic paths:

### A. Coin-Lock Registration (`lock`)
The **Coin-Lock** path allows a node to compete for the dedicated **`Sub-Track 1B` (`1` seat)** by locking up a collateral amount, earning under `UPGRADE_FAIRNESS` an **equal `1/11` committee validator share (`~3.636%` Model D, `~6.818%` Pre-Model D)** and proportional **`250`** Coordinator VRF capital weight, with strict quota isolation so excess `Coin-Lock` registrations cannot crowd out Open PoW seats (`only serving as last-resort liveness fallback`).
* **Collateral Amount:** **250 KRISTA** (`MINER_REGISTRATION_LOCK_AMOUNT = 250 * COIN`, enforced in mempool via `bad-txns-coinlock-amount-too-small` and in `GetAdamMinerPool`; consensus and manual `registerminer lock` accept `250 KRISTA` Coin-Locks at **all block heights**, while background `AutoRegisterMiner` waits until height `>= 5,000` (`IsModelDActive`) so bootstrap rewards stay liquid).
* **Anti-Sybil Consensus & Wallet Limits:** Every input UTXO funding a `Coin-Lock` transaction must have at least **`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-txns-coinlock-input-immature`); consensus (`CheckBlock` / `ConnectBlock`), mempool (`AcceptToMemoryPoolWorker`), and `ExtractBlockMinerRegEntries` enforce at most **`1` `Coin-Lock` output per transaction** (`bad-txns-too-many-coinlock`) and **`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1` `Coin-Lock` transaction per block** (`bad-blk-too-many-coinlock`); and the wallet/RPC/GUI layer enforces at most **`1` active `Coin-Lock` seat per wallet** (renewable within the last `240` blocks on Mainnet or `15` blocks post-Model-D on Testnet/Regtest).
* **Timelock Duration:** Must be locked for at least **2,880 blocks** (`nRegPeriod`, ~24 hours at 30s block spacing on Mainnet; `100 blocks` on Testnet/Regtest once Model D is active, `2,880 blocks` pre-Model D) from the registration height. The wallet adds a `+20` block confirmation buffer by default (`2,900 blocks` on Mainnet, `120 blocks` on Testnet/Regtest post-Model D).
* **Script Structure (`MatchCoinLockRegistration`):** Uses absolute `OP_CHECKLOCKTIMEVERIFY` (CLTV) script outputs (`<pubkey> OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubkeyHash> OP_EQUALVERIFY OP_CHECKSIG`) with a 33-byte compressed public key. The funds cannot be spent, moved, or unlocked until the block height reaches the specified target.

### B. PoW-Lock Registration (`pow`)
The **PoW-Lock** path allows nodes without the 250 KRISTA collateral to participate by solving a CPU puzzle to prove resource dedication.

* **Funded PoW Registration (`POW_LOCK_REGISTRATION_AMOUNT`):** Under `UPGRADE_HARDENING_V2`, a node with a mature UTXO locks **10 KRISTA** via `OP_CHECKLOCKTIMEVERIFY` and solves a 7-level recursive **`HashX11KVS`** CPU hash puzzle (`ComputeMinerPoWRegistrationHash` over `"KRPL"[4] || challenge[32] || compressedPubKey[33] || "POWLKv2"[7] || le32(nNonce)[4]` against `GetMinerPoWLimit(..., true)`: **15-bit** zero prefix on Mainnet [`~14.8s` CPU], **13-bit** on Testnet, **8-bit** on Regtest) bound to an ancestor block within the last `100` blocks to create a timelocked output. Mempool policy (`AcceptToMemoryPoolWorker`) validates the compressed pubkey, `100`-block challenge freshness, and `HashX11KVS` target up front so malformed transactions never accidentally lock funds. The 10 KRISTA remains in the miner's wallet and becomes spendable again after the lock expires.
* **Anti-Sybil PoW-Lock Consensus Rules (`nAdamDualTrackHeight` = Block 200):**
  - **Input UTXO Maturity (`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100`):** Every input UTXO funding a non-zero `PoW-Lock` registration transaction must have at least **100 confirmations**, preventing rapid UTXO splitting to mass-register keys.
  - **Block, Transaction & Per-Wallet Rate Limits (`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`):** At most **1 `PoW-Lock` output per transaction** and at most **1 funded `PoW-Lock` transaction per block** are accepted by consensus (`CheckBlock`), mempool (`AcceptToMemoryPool`), and the block assembler (`CreateNewBlock`), tracked independently of the `1` `Zero-Coin PoW` slot per block so free `0 KRISTA` registrations can never starve funded `10 KRISTA` registrations. Any funded `PoW-Lock` output locking less than `10 KRISTA` (`POW_LOCK_REGISTRATION_AMOUNT`) is rejected (`bad-txns-powlock-amount-too-small`), and the wallet/RPC/GUI layer enforces at most **`1` active `PoW` seat per wallet** (with immediate `0 -> 10 KRISTA` promotion allowed at any time, and renewal permitted in the final `240` blocks [`15` post-Model-D Testnet/Regtest] for funded seats or `24` blocks [`8` post-Model-D Testnet/Regtest] for Zero-Coin seats).
  - **Dual-Track ($7 + 1 + 3$) Equal-Work Seat Payouts (`UPGRADE_FAIRNESS`):** Within the **8-seat Collateralized Track (`ADAM_COLLATERALIZED_TRACK_QUOTA = 8`)**, **7 seats (`ADAM_MASTERNODE_TRACK_QUOTA = 7`)** are reserved for Active Masternodes (`4,200 KRISTA`) and **1 seat (`ADAM_COIN_LOCK_TRACK_QUOTA = 1`)** is strictly isolated for `250 KRISTA` Coin-Lock validators, while Open PoW miners compete for the **3-seat Open PoW Track (`ADAM_OPEN_POW_TRACK_QUOTA = 3`)**. Any unfilled seats spill over in strict order: **Active Masternodes (`4,200 KRISTA`) $\to$ Open PoW (`10 KRISTA` `PoW-Lock` $\to$ `Bootstrap` $\to$ `Zero-Coin PoW`) $\to$ `Coin-Lock` (`250 KRISTA`, last-resort liveness fallback)**. Under `UPGRADE_FAIRNESS` (Block 1), every solved committee seat earns an **exact equal `1/11` share** (`40% / 11 ≈ 3.636%` Model D, `75% / 11 ≈ 6.818%` Pre-Model D) with zero track disparity.
* **Zero-Coin PoW Registration (`0 KRISTA Balance`, Hardened under `UPGRADE_HARDENING_V2` & `UPGRADE_FAIRNESS` at Block #1):**
  - **Permanent `0 KRISTA` Open-Entry Seat (`nAdamOpenEntryHeight = 2500` Mainnet / `1` Testnet & Regtest):** Prior to `nAdamOpenEntryHeight`, Zero-Coin PoW closed once the funded validator pool reached `13` (`nAdamMinersCount + 2`). From `nAdamOpenEntryHeight` onward, Zero-Coin PoW remains **permanently open** with **1 dedicated Open-Entry seat** inside `Track 2` (alongside `2` dedicated `10 KRISTA` PoW-Lock seats, expanded under `UPGRADE_FAIRNESS` to `MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS = 240` active keys with `0` Coordinator VRF weight), allowing any newcomer with `0 KRISTA` to earn the equal committee validator seat share immediately without a faucet, pool, or exchange and auto-promote to `10 KRISTA` `PoW-Lock`.
  - **Strict Economic Election Priority & Proportional Capital Coordinator VRF (`4,200 : 250 : 10` with `75%` MN Floor):** Under `UPGRADE_HARDENING_V2` & `UPGRADE_FAIRNESS`, `ElectDualTrackValidators` partitions Open PoW (`Track 2`) into **Tier 2A (`10 KRISTA` Funded PoW-Lock competing by VRF)** $\to$ **Tier 2B (`Blocks 1..199` Bootstrap)** $\to$ **Tier 2C (`0 KRISTA` Zero-Coin PoW)**, while `SelectAdamNodes` elects `coordinatorOut` via **`ADAM_FAIR_COORD_V4`** (`4,200` per Active MN, `250` per `Coin-Lock`, and `10` per `Funded PoW-Lock` with an exact `>= 75%` Active MN security floor when `3 * sumNonMN > sumMN`; replacing the legacy `65:8:2` quadratic cap), falling back to `Bootstrap` and finally `Zero-Coin PoW` only when zero funded validators exist.
  - **CPU-Bound `HashX11KVS` Puzzle & `10`-Block Fresh Challenge Window:** Under `UPGRADE_FAIRNESS` (Block 1), `ComputeZeroCoinRegistrationHash` constructs a deterministic 80-byte header (`"KRZC"[4] || challenge[32] || compressedPubKey[33] || "SYBILV2"[7] || le32(nNonce)[4]`, requiring a 33-byte compressed pubkey) and hashes it with 7-level (`127`-branch `X11KV`) **`HashX11KVS`** against `GetZeroCoinPoWLimit(..., true, true)` (**15 leading zero bits** on Mainnet [`0001ffff...`], matching Funded `PoW-Lock` work; **12 bits** on Testnet [`000fffff...`], **8 bits** on Regtest [`00ffffff...`]). The referenced `challenge` block must be within the last **`10` blocks** (`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` in consensus, `8` blocks in mempool acceptance), preventing offline transaction stockpiling.
  - **Consensus & Wallet Safety:** The registration transaction uses a **0 KRISTA** output (`nValue = 0`) and a sentinel input outpoint (`0x01:0`, at most 1 per block and 1 in mempool at a time; keys already active in `GetAdamMinerPool` are rejected from mempool). Consensus rules (`CheckTransaction`) grant an explicit exception for valid Zero-Coin PoW registration scripts, and the wallet treats sentinel `0x01:0` inputs like coinbase inputs (no false wallet double-spend conflicts), commits Zero-Coin PoW transactions to `wallet.dat` only after mempool acceptance, and limits each wallet to at most **1 active PoW seat (`0` or `10 KRISTA`) and 1 active `Coin-Lock` seat (`250 KRISTA`)** alongside unlimited `4,200 KRISTA` Masternodes.
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
Builds, signs, and broadcasts a miner registration transaction (automatically pairing a deterministic BLS12-381 key in `wallet.dat`, selecting $\ge 100$-confirmation UTXOs via `CCoinControl` for both `Coin-Lock` and `PoW-Lock`). Rejects early with a descriptive error if the node is still in Initial Block Download (`IsInitialBlockDownload`), if the wallet already holds an active or pending registration in the requested track outside its renewal window (`1` active `Coin-Lock` seat + `1` active `PoW` seat per wallet; immediate `0 -> 10 KRISTA` upgrade permitted anytime), if the key is already active/pending in the mempool, or if another Zero-Coin PoW transaction is already waiting in the mempool.

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

1. **Active Pool, Renewal Window & Automatic Funded Promotion Check**:
   The engine checks `GetAdamMinerPoolDetailed` (plus pending mempool/unconfirmed wallet registrations) to see if the wallet already holds an active **funded** validator registration (`masternode`, `coin_lock`, or `pow_lock`) with more than `240` blocks (`15` blocks post-Model-D on Testnet/Regtest) remaining; if so, auto-registration exits silently. If the wallet only holds an **unfunded** entry (`zerocoin_pow` or `bootstrap`), `AutoRegisterMiner` stays silently idle while matured funds are below `10.01 KRISTA`, and **automatically promotes** the node to a funded `10 KRISTA` PoW-Lock (or `250 KRISTA` Coin-Lock at height $\ge 5,000$) as soon as matured ($\ge 100$-confirmation) coins are available (while from `nAdamOpenEntryHeight = 2,500` Mainnet / `1` Testnet & Regtest, `0-KRISTA` Open-Entry itself remains permanently open for 1 dedicated Track 2 seat regardless of funded pool size).

2. **Spam Prevention**:
   To prevent registration transaction spam, the engine tracks the height of the last broadcasted registration. If a registration was sent less than **50 blocks** ago, the auto-registration is deferred.

3. **Key Management**:
   The engine automatically attempts to generate or retrieve the corresponding BLS key for the miner public key. If the wallet is locked, it logs a warning:
   `AutoRegisterMiner: Wallet is locked. Cannot auto-register. Please unlock your wallet or run registerminer manually.`

4. **Mode Selection & Collateral Check**:
   The engine evaluates the wallet's matured ($\ge 100$-confirmation) balance and the current block height to select the appropriate registration path:
   - **Coin-Lock (PoL) Mode**: If the current block height is $\ge$ 5,000 (`IsModelDActive`), matured ($\ge 100$-confirmation) UTXOs total at least **250.01 KRISTA** (`250 KRISTA` collateral plus `0.01 KRISTA` fee buffer), and either the wallet is renewing its own expiring `Coin-Lock` seat or `Sub-Track 1B` currently has `< 1` active/pending `Coin-Lock` validator while `Track 2` already holds at least `3` active `PoW-Lock` validators, the engine constructs a Coin-Lock transaction with a lock duration of **2,900 blocks** in the future. Below height 5,000 (pre-Model D phase) or once `Sub-Track 1B` is already occupied, Coin-Lock auto-registration defers to `PoW-Lock` so automated `gen=1` wallets stay balanced across both tracks and rewards remain liquid (manual `registerminer lock` and Qt Miner Hub registration remain unrestricted at any height).
   - **PoW-Lock Mode**: If the block height is < 5,000 or the balance is insufficient for a Coin-Lock, the engine falls back to PoW-Lock mode. If the wallet has at least **10.01 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT + 0.01 * COIN`) in mature ($\ge 100$-confirmation) UTXOs, it solves the **`HashX11KVS` Funded PoW-Lock puzzle** (`GetMinerPoWLimit(..., true)`, 15-bit on Mainnet) and locks **10 KRISTA** (`POW_LOCK_REGISTRATION_AMOUNT`) in a timelocked output for **2,900 blocks** in the future. If the wallet has `< 10.01 KRISTA`, holds no active unfunded seat yet, and either `nHeight >= consensus.nAdamOpenEntryHeight` (`2,500` Mainnet / `1` Testnet & Regtest — permanent `0-KRISTA` Open-Entry) or the **funded** validator pool (`GetFundedAdamMinerPoolCount`) has `< 13` (`nAdamMinersCount + 2`) funded validators, it solves the **`HashX11KVS` Zero-Coin / Open-Entry PoW puzzle** (`GetZeroCoinPoWLimit(..., true)`, 14-bit on Mainnet), creating a 0-value timelocked registration transaction for **260 blocks** (`GetZeroCoinRegPeriod + 20`) without requiring spent UTXOs.

5. **Broadcast**:
   The resulting transaction is validated by `AcceptToMemoryPool`, committed, and broadcast to the network.

---

## 6. Qt Wallet Miner Hub

KristaTech-Qt provides a dedicated **MINER HUB** TopBar navigation tab (`src/qt/kristatech/minerhubwidget.cpp`), cleanly separated from the **HOME** overview (which displays the 4-card KPI strip `Total Balance` / `Spendable` / `Locked Collateral` / `Immature Rewards`, `Recent Transactions`, and reorderable drag-and-drop right-column cards `Earnings Snapshot`, `Node & Consensus`, and `My Masternodes`):
* **1-Click Cooperative CPU Mining Toggle**: Enables/disables internal multi-threaded CPU mining (`GenerateBitcoins`) with configurable thread count.
* **Standalone TopBar Tab & Full Screen (`F11` / `Esc`) Monitoring Mode**: Switching to **MINER HUB** from the top navigation bar opens the full Cooperative Miner & Validator workspace, including a **1-Click Full Screen (`⛶ Full Screen` / `F11` / `Esc`)** toggle that expands the table across 100% of the window while displaying complete 34-character miner addresses.
* **Non-Blocking Contextual 1-Click Track Registration**: Provides 1-click **Track 2 (`PoW-Lock`: `10 KRISTA` / `0 KRISTA` bootstrap)** and **Track 1 (`Coin-Lock`: `250 KRISTA`)** registration buttons with live per-wallet seat state (`Pending...`, `Active (Xb left)` disabled until the renewal window opens, `Upgrade to PoW-Lock`, `Renew ...`, and `Register ...`). PoW puzzle solving runs asynchronously on a background worker thread (`⏳ Solving PoW...`) so the Qt GUI remains responsive.
* **Live Validator & Committee Role Table**: Displays all local wallet registrations (`Coin-Lock`, `PoW-Lock`, `0-Coin PoW`), Block `1..199` coinbase fallback keys (`Bootstrap`), active/controller Masternodes (`activemasternode.conf` & `masternode.conf`), and active unregistered external RPC miners (`cpuminer (CPU)` / `ccminer (GPU)` polling within the last 65s, pinned at the top with an inline `Reg` menu and explicit `Registration Not Possible` feedback if wallet quotas or mature balance requirements are unmet). Each row clearly shows live execution states, next-block committee election role alongside the active hash algorithm (`X11KVS` in Bootstrap Blocks `1..199`, or V11 single-algo / V12 3-permutation puzzle pipeline in ADAM Blocks `200+`), remaining lock blocks with percentage (`Pending` while unconfirmed), cumulative block/seat rewards won (including both Bootstrap-era coinbase hits and ADAM-era validator payouts), solved committee puzzle counts, external miner last-seen age (`Xs ago`), and 1-click address / `cpuminer` command copy, custom label editing (`✎`), and inline renewal (`↻`) buttons.
* **Live `ADAMSOL` Counter, Multi-Dimensional Filters & Sorting**: The Miner Hub header displays a live `ADAMSOL: X / Y (Min T) • Zs / 30s` solution counter and block-age badge, while the roster toolbar provides `All` / `Active` / `Elected` / `Ext RPC` / `VPS MN` filter pills, a Track dropdown (`Masternode`, `Coin-Lock`, `PoW-Lock`, `Zero-Coin`, `Unregistered`), live address/alias search, `X / Y` row count, and clickable column sorting.
