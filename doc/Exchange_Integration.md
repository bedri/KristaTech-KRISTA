# KristaTech (KRISTA) — Exchange & Atomic Swap Integration Guide

**Target Node Version:** `v1.5.9.0+` (including `UPGRADE_HARDENING_V3`, active at Block `1`)  
**Scope:** Centralized Exchange (CEX) Custody/Daemon Integration & Non-Custodial Atomic Swap / DEX Integration (`Komodo DeFi Framework` & `BasicSwap DEX`)

---

## 1. Chain & Asset Specifications

| Property | Value | Source Reference |
|---|---|---|
| **Asset Name** | KristaTech | `src/chainparams.cpp` |
| **Ticker Symbol** | `KRISTA` | `src/utilstrencodings` / `CURRENCY_UNIT` |
| **Divisibility / Decimals** | `8` (`1 KRISTA = 100,000,000` satoshis) | `src/amount.h` |
| **Architecture** | Layer-1 UTXO (Bitcoin/PIVX-derived `CTransaction` serialization) | `src/primitives/transaction.h` |
| **Consensus** | Hybrid PoW (`X11KVS` + 17-algo ADAM MPA) + PoS + LLMQ BLS Quorums | `src/chainparams.cpp`, `src/adam.cpp`, `src/llmq.cpp` |
| **Target Block Time** | `30 seconds` (`~2,880` blocks/day) | `nTargetSpacing = 30` |
| **Max Supply (Hard Cap)** | `210,000,000 KRISTA` | `nMaxMoneyOut = 210000000 * COIN` |
| **Premine** | `0 KRISTA` (fair launch; Blocks `0`–`1` emit `0` spendable coinbase rewards; Block `2+` starts at `17 KRISTA`) | `src/chainparams.cpp`, `src/masternode.cpp` |
| **Emission Decay** | `-2.15%` every `259,200` blocks (`~90 days` at 30s spacing; period `(height - 2) / 259200`) | `src/masternode.cpp` (`ComputeDecaySubsidyForPeriod`) |
| **Developer Treasury** | `7%` of block subsidy from Block `2+` (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`) | `src/chainparams.cpp`, `src/masternode.cpp` |
| **Coinbase / Coinstake Maturity** | `100` blocks (`101` confirmations required to spend; `~50 min`) | `consensus.nCoinbaseMaturity = 100` |
| **Minimum Relay Fee** | `10,000` sat/kB (`0.0001 KRISTA/kB`) | `minRelayTxFee` in `src/main.cpp` |
| **P2PKH Dust Threshold** | `5,460` satoshis (`0.00005460 KRISTA` via `3 * minRelayTxFee.GetFee(txout_size + 148)`) | `src/primitives/transaction.h` (`CTxOut::GetDustThreshold`) |
| **Max Block Size** | `8,000,000` bytes (`MAX_BLOCK_SIZE_CURRENT`) | `src/consensus/consensus.h` |
| **Max Standard Tx Size** | `150,000` bytes (`MAX_STANDARD_TX_SIZE`) | `src/main.h` |
| **Max P2SH SigOps** | `15` (`MAX_P2SH_SIGOPS`) | `src/policy/policy.h` |

### Protocol Activation Heights (Mainnet)

| Upgrade / Milestone | Activation Height | Impact on Exchanges / Integrators |
|---|---|---|
| `UPGRADE_HARDENING_V1`, `V2`, `V3`, `UPGRADE_DIP3`, `UPGRADE_FAIRNESS` | `1` | Strict DER ECDSA (`DERSIG`), strict `SIGHASH_DEFAULT = 0` BIP341 Taproot checks, KTDIP3 v2 BLS masternodes |
| `UPGRADE_POS`, `UPGRADE_BIP65`, `UPGRADE_TIME_PROTOCOL_V2`, `UPGRADE_ADAM` | `200` | PoS blocks + `OP_CHECKLOCKTIMEVERIFY` (`BIP65`) + ADAM cooperative PoW committee |
| `UPGRADE_POMBL` | `4,800` | BLS LLMQ quorums active |
| `UPGRADE_MODELD` | `5,000` | Full Model D block reward distribution active |

### Genesis Hashes

- **Mainnet Genesis Hash:** `0000050d4a94e97a9491c2ddde1251ff040772fee943e653fe583d5737e4d5e3`
- **Mainnet Genesis Merkle Root:** `5c30c7b517b927fa601cdde9106de52a3335a5fe28001cb43f540356b6b37a44`
- **Mainnet Genesis Timestamp:** `1780589293`
- **Testnet Genesis Hash:** `00000e861e53873b7022fd0b9acabb804bd9eb14b8efe9ab20fe94ff3d24ca90`

---

## 2. Network Ports, Magic Bytes & Seed Infrastructure

| Network | P2P Port | JSON-RPC Port | Network Magic (`pchMessageStart`) | Data Subdir |
|---|---|---|---|---|
| **Mainnet** | `27999` | `27979` | `0xc8 0x7b 0x86 0x77` | default (`~/.kristatech`) |
| **Testnet** (`-testnet`) | `27989` | `27919` | `0x45 0x76 0x65 0xba` | `testnet1` |
| **Regtest** (`-regtest`) | `27989` | `27929` | inherited from Testnet | `regtest` |

### Official DNS Seeds & Public SPV Endpoints

- **DNS Seeds:** `seeder.kristalteknoloji.com`, `seed1.kristalteknoloji.com`, `seed2.kristalteknoloji.com`, `seed3.kristalteknoloji.com`, `seed4.kristalteknoloji.com`
- **Electrum / SPV Server (`KristaTech-Electrum-Server`):** `electrum.kristalteknoloji.com`
  - WebSocket (`WSS` behind Nginx): port `50003` (upstream) / `443`
  - Standard Electrum TCP: `50001` (local/direct); TLS TCP `50002` recommended to be exposed publicly alongside `WSS` for light-client & atomic-swap compatibility (see Section 6).

---

## 3. Address Formats, Prefixes & Key Derivation

KristaTech supports **Base58Check** addresses (with a **2-byte P2PKH prefix** on Mainnet/Testnet) and **Bech32m Taproot (`kt1p...`)** addresses.

| Format | Mainnet Prefix / HRP | Mainnet Example Prefix | Testnet Prefix / HRP | Notes |
|---|---|---|---|---|
| **P2PKH (`PUBKEY_ADDRESS`)** | **`[10, 100]`** (`0x0A, 0x64` — **2 bytes**) | `KT...` (35 chars) | `[24, 181]` (`0x18, 0xB5`, `kt...`) | **Recommended primary format for CEX deposit & hot wallets** |
| **P2SH (`SCRIPT_ADDRESS`)** | **`[16]`** (`0x10` — **1 byte**) | `7...` (34 chars) | `[19]` (`0x13`, `8...`/`9...`) | Used for multisig custody & HTLC Atomic Swaps (`TX_SCRIPTHASH`) |
| **WIF (`SECRET_KEY`)** | **`[43]`** (`0x2B` — **1 byte**) | `7M..`–`7W..` (comp, 52 chars) / `2S..`–`2U..` (uncomp) | `[239]` (`0xEF`, `c...`) | Wallet Import Format private keys |
| **Taproot (`P2TR`, Witness v1)** | Bech32m HRP **`"kt"`** | `kt1p...` | HRP **`"tk"`** (`tk1p...`; Regtest `"ktrt"`) | Enforces BIP340 Schnorr + BIP341 (`SIGHASH_DEFAULT = 0`) |
| **BIP32 `xpub` (`EXT_PUBLIC_KEY`)** | `0x02, 0x2D, 0x25, 0x73` | `ToEK...` | `0x3a, 0x80, 0x61, 0xa0` (`DRKV...`) | Extended public key prefix |
| **BIP32 `xprv` (`EXT_SECRET_KEY`)** | `0x02, 0x21, 0x31, 0x2B` | `TDt9...` | `0x3a, 0x80, 0x58, 0x37` (`DRKP...`) | Extended private key prefix |
| **BIP44 Coin Type (`EXT_COIN_TYPE`)** | `0x80001DFC` (**`7676'`**) | — | `0x80000001` (`1'`) | Derivation path `m/44'/7676'/account'/change/index` |
| **Signed Message Prefix (`strMessageMagic`)** | `"DarkNet Signed Message:\n"` | — | `"DarkNet Signed Message:\n"` | Used by `signmessage` / `verifymessage` |

### Critical Warning for Exchanges Regarding Protocol Addresses

Never allow exchange users or automated sweepers to send funds to KRISTA's unspendable protocol burn addresses:
- `KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr`
- `KTburnUHdmKMVx4z8BcnZYZCWYKxRgfZp95`

Coins sent to these addresses are permanently removed from spendable supply (`IsUnspendable()` / protocol burn tracking). Exchange validators should validate withdrawal targets via `kristatech-cli validateaddress "<addr>"` and maintain a withdrawal denylist for protocol burn addresses.

---

## 4. Transaction Serialization & Signing Notes

1. **Standard Wire Format (`CTransaction`, `nVersion = 1`):**
   - KRISTA uses classic Bitcoin `CTransaction` serialization (`int32_t nVersion`, `std::vector<CTxIn> vin`, `std::vector<CTxOut> vout`, `uint32_t nLockTime`).
   - There is **no** SegWit marker/flag (`0x00 0x01`) on wire transactions, **no** `nTime` field in transactions, and **no** Dash DIP2 special-tx payload on regular UTXO transfers (special payloads exist only on masternode `ProRegTx` / `ProUp*` / `ProRev` txs).
   - `MAX_STANDARD_TX_SIZE` is `150,000` bytes; exchanges consolidating large numbers of small deposits must keep each raw transaction strictly under `150,000` bytes (and consider `<= 100,000` bytes for conservative margin).

2. **Taproot (`kt1p...`) Withdrawals & Deposits:**
   - Under `UPGRADE_HARDENING_V3` (`Height >= 1`), `TX_WITNESS_V1_TAPROOT` verifies strict BIP340/BIP341 sighashes (`SIGHASH_DEFAULT = 0`), delivered in `scriptSig` (because KRISTA does not serialize a separate SegWit witness field).
   - **CEX Best Practice:** Issue **P2PKH (`KT...`)** deposit addresses and store hot/cold custody in **P2PKH (`KT...`)** or **P2SH multisig (`7...`)**. Support **withdrawals** to both `KT...`, `7...`, and `kt1p...` by validating destination addresses with `validateaddress` (`isvalid == true`) and constructing/signing withdrawals via `kristatechd` RPC or BIP174 PSBT (`createpsbt` / `utxoupdatepsbt` / `combinepsbt` / `finalizepsbt`).

---

## 5. Exchange Daemon Setup & Custody Workflow

### 5.1 Build from Source (Linux)

```bash
git clone https://github.com/bedri/KristaTech-KRISTA.git
cd KristaTech-KRISTA
./autogen.sh
./configure --without-gui --disable-tests --disable-bench
make -j"$(nproc)" kristatechd kristatech-cli kristatech-tx
```

Binary integrity should be verified via SHA-256 against the official release tag before starting production infrastructure.

### 5.2 Recommended Exchange `kristatech.conf`

Exchanges **must NOT** run mining (`gen=0`), staking (`staking=0`), or masternodes (`masternode=0`) on deposit/withdrawal custody nodes.

```ini
# Network & RPC
server=1
daemon=1
txindex=1
listen=1
port=27999
rpcport=27979
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
rpcuser=CHANGE_TO_RANDOM_USERNAME
rpcpassword=CHANGE_TO_LONG_RANDOM_PASSWORD

# Exchange safety: disable block production, staking, and masternode roles
staking=0
gen=0
masternode=0

# Mempool & block policy limits
blockmaxsize=500000
maxmempool=300
maxtxfee=1.0

# Optional ZMQ notifications if integrating real-time block/tx listeners
# zmqpubhashblock=tcp://127.0.0.1:28332
# zmqpubrawtx=tcp://127.0.0.1:28333
```

> **Note:** Legacy Bitcoin `account` RPCs (`getaccount`, `getaccountaddress`, `listaccounts`, `sendfrom`) are deprecated and disabled by default. Use address labels (`getnewaddress "<label>"`, `getaddressesbylabel`, `listsinceblock`, `listunspent`) and `sendtoaddress` / `sendmany` / `createrawtransaction` / PSBT workflows.

### 5.3 Confirmation & Reorg Policy Recommendations

| Flow | Suggested Confirmations | Approx. Wall Time (30s blocks) | Rationale |
|---|---|---|---|
| **Standard Deposit Credit** | `20` confirmations | `~10 minutes` | Balances fast user UX with reorg margin |
| **High-Value Deposit Credit** | `40`–`60` confirmations | `20`–`30 minutes` | Extra finality margin |
| **Coinbase / Coinstake / MN Reward Sweeps** | `101` confirmations | `~50.5 minutes` | Protocol enforces `nCoinbaseMaturity = 100` before spending |

### 5.4 Deposit & Withdrawal RPC Reference

- **Generate Deposit Address:** `getnewaddress "user_deposit_label"`
- **Validate User Withdrawal Address:** `validateaddress "<address>"` (verify `"isvalid": true` and reject burn addresses)
- **Scan Incoming Deposits Safely (Reorg-Safe Cursor):**
  - `getblockcount` / `getbestblockhash`
  - `listsinceblock "<last_processed_blockhash>" 1` (re-check until `confirmations >= 20`, verify `category == "receive"`)
  - `gettransaction "<txid>"` (check `confirmations` and `blockhash`)
  - `getrawtransaction "<txid>" true` (available when `txindex=1`)
- **Fee Estimation:**
  - `estimatesmartfee 5` (floor against `minRelayTxFee = 0.0001 KRISTA/kB`)
- **Batch Withdrawals:**
  - `sendmany "" '{"KT...": 12.5, "7...": 25.0}'` or `createrawtransaction` + `fundrawtransaction` + `signrawtransaction` + `sendrawtransaction`
- **Cold-Storage Multi-Party / Offline Signing (BIP174 PSBT):**
  - Supported natively: `createpsbt`, `utxoupdatepsbt`, `decodepsbt`, `analyzepsbt`, `combinepsbt`, `finalizepsbt`

---

## 6. Atomic Swap & Non-Custodial DEX Technical Compatibility Analysis

Instead of operating a custodial centralized exchange (which triggers heavy capital, licensing, AML/Travel Rule, and hot-wallet custody obligations), KRISTA can participate in **non-custodial peer-to-peer Atomic Swaps** via **Hashed Time-Lock Contracts (HTLCs)**.

### 6.1 Protocol & Script Engine Readiness

KristaTech's consensus script verifier (`STANDARD_SCRIPT_VERIFY_FLAGS` in `src/policy/policy.h`) already enforces the full opcodes required for standard Bitcoin-style HTLC atomic swaps:
- **P2SH (`OP_HASH160 <20B> OP_EQUAL`)** with custom redeem script spending (`AreInputsStandard()` in `src/policy/policy.cpp`, up to `MAX_P2SH_SIGOPS = 15`; standard HTLC uses `2` sigops).
- **Time-locks:** `OP_CHECKLOCKTIMEVERIFY` (`BIP65`, active from height `200`, 5-byte `CScriptNum` under `UPGRADE_HARDENING_V3`) and `OP_CHECKSEQUENCEVERIFY` (`BIP112`).
- **Hash preimage locks:** `OP_HASH160`, `OP_SHA256`, `OP_RIPEMD160`, strict `DERSIG` & `LOW_S`.
- **Native Smart Contract Compiler (MESCAL):** KRISTA also supports HTLC contracts directly via `compilemescal` (`"mode": "htlc-equal"`), documented in `doc/MESCAL_Usecases.md`.

### 6.2 Komodo DeFi Framework (`mm2`) Compatibility

| Requirement | KRISTA Status | Action Needed |
|---|---|---|
| Standard `CTransaction` serialization (`txversion: 1`) | **Ready** | None |
| `P2SH` + `CLTV` + `OP_HASH160` HTLC spendability | **Ready** | Verify with live regtest swap |
| Electrum 1.4 JSON-RPC Server (`KristaTech-Electrum-Server`) | **Ready** (supports `server.version`, `server.banner`, `server.features`, `blockchain.headers.subscribe`, `blockchain.scripthash.*`, `blockchain.transaction.*`, `blockchain.estimatefee`) | Expose public **TLS TCP (`50002`)** in addition to WSS (`50003`), and run **>= 2 redundant Electrum nodes** |
| **Asymmetric Base58 Prefixes (`PUBKEY=[10,100]` vs `SCRIPT=[16]`)** | **Requires 1 small PR in `mm2`** | See engineering note below |

#### CRITICAL Engineering Note for Komodo `mm2`: Asymmetric 2-Byte P2PKH / 1-Byte P2SH Prefixes

KRISTA Mainnet defines:
- `base58Prefixes[PUBKEY_ADDRESS] = [10, 100]` (**2 bytes** -> `KT...`)
- `base58Prefixes[SCRIPT_ADDRESS] = [16]` (**1 byte** -> `7...`)

Standard `mm2` `coins` JSON historically expects single-byte `pubtype` / `p2shtype` integers, OR uses `taddr` (Zcash convention) which prepends the **same** leading byte to *both* `pubtype` and `p2shtype`. If `taddr = 10`, `pubtype = 100`, `p2shtype = 16` were configured naively in standard `mm2`, `mm2` would construct `[10, 16]` for P2SH instead of KRISTA's real `[16]`, breaking HTLC swap script address generation!

**Required Fix before adding KRISTA to `KomodoPlatform/coins`:**
- Submit a small PR to `KomodoPlatform/komodo-defi-framework` allowing `pubtype` / `p2shtype` (or explicit `pubtype_bytes` / `p2shtype_bytes` / asymmetric prefix arrays) so KRISTA can specify `[10, 100]` for P2PKH and `[16]` for P2SH independently.
- **Draft `coins` entry (DO NOT deploy on mainnet until `mm2` PR supports independent `[10, 100]` / `[16]` prefixes):**

```json
{
  "coin": "KRISTA",
  "name": "kristatech",
  "fname": "KristaTech",
  "mm2": 1,
  "protocol": {
    "type": "UTXO"
  },
  "txversion": 1,
  "txfee": 10000,
  "dust": 5460,
  "required_confirmations": 2,
  "avg_blocktime": 0.5,
  "decimals": 8,
  "pubtype": 100,
  "p2shtype": 16,
  "wiftype": 43,
  "taddr_pub_only": 10,
  "p2p": 27999,
  "magic": "77867bc8"
}
```
*(Verify byte-order of `magic` against `mm2` UTXO SPV header parser if SPV mode is used alongside Electrum TLS endpoints on `electrum.kristalteknoloji.com:50002`.)*

### 6.3 BasicSwap DEX Compatibility

| Swap Mode | KRISTA Compatibility | Engineering Notes |
|---|---|---|
| **Secret-Hash HTLC Swaps** (`KRISTA ↔ BTC / LTC / DOGE / BCH / DASH / PIVX / PART`) | **Directly Compatible (Best First Target)** | Python coin adapters in BasicSwap define `pubkey_address` (`bytes([10, 100])`) and `script_address` (`bytes([16])`) **independently**, so KRISTA's asymmetric Base58 prefix works cleanly without Zcash-`taddr` collisions. |
| **Wallet / Watch-Only RPC Interface** | **Legacy Bitcoin Core Pattern** | `kristatechd` exposes `importaddress`, `importpubkey`, `importprivkey`, `listunspent`, `createrawtransaction`, `signrawtransaction`, `sendrawtransaction`. Note that `importmulti` and descriptor wallets are **not** present, so the BasicSwap adapter should subclass/follow the `PIVX` / `DASH` / non-descriptor UTXO adapter pattern rather than Bitcoin Core v24+ descriptors. |
| **Scriptless Adaptor Signatures (OtVES)** (`KRISTA ↔ XMR`) | **Not Supported Currently** | Scriptless adaptor swaps against Monero require SegWit (or equivalent txid non-malleability) on the UTXO funding chain. Without SegWit witness separation, KRISTA should target **Secret-Hash (HTLC)** pairs only. |

### 6.4 Pre-Listing & Pre-Swap Readiness Checklist

Before approaching Tier-2/Tier-3 CEXs or submitting PRs to DEX coin registries:

1. **Zero Chain Resets:** Commit to permanent, uninterrupted Mainnet continuity post-`UPGRADE_HARDENING_V3`.
2. **Reproducible Release Artifacts:** Publish signed GitHub Releases (`v1.5.9.x`+) with Linux x86_64 binaries (`kristatechd`, `kristatech-cli`, `kristatech-tx`), `SHA256SUMS`, and an official `Dockerfile`.
3. **Redundant Public Infrastructure:**
   - Maintain 4 stable DNS Seed full nodes (`seed1`–`seed4`).
   - Run 24/7 public Explorer (`https://explorer.kristalteknoloji.com` or equivalent) with verified API endpoints (`supply`, `height`, `tx`).
   - Expose TLS (`50002`) + WSS (`50003`) on `electrum.kristalteknoloji.com` and add a secondary redundant Electrum instance.
4. **Regtest Atomic Swap Proof:** Execute and document an end-to-end Regtest HTLC atomic swap (`KRISTA <-> LTC` or `KRISTA <-> BTC`) using BasicSwap (Secret-Hash mode) or patched `mm2` prior to announcing DEX support.
