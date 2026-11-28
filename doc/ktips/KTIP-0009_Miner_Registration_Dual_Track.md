```
KTIP: 0009
Title: Dual-Path Miner Registration & Dual-Track (8 + 2 + 5 Balanced Model D / 7 + 1 + 3 Pre-Model D) Committee Quota Election
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-08-15
```

## Abstract
This proposal specifies the on-chain **Dual-Path Miner Registration** (`Coin-Lock` and `PoW-Lock`) and **Dual-Track Committee Quota Election (`ElectDualTrackValidators`)** mechanisms in the ADAM consensus layer—supporting **15 seats (`8 + 2 + 5`)** under **Balanced Model D (`UPGRADE_MODELD`: block `200` on Mainnet / Testnet / Regtest; replacing legacy `11`-seat `7 + 1 + 3` Pre-Model D layout)**. Under Balanced Model D, partitioning the 15 validator seats into **Track 1 (Collateralized Pool — 10 Seats: 8 Active Masternodes [`4,200 KRISTA`, `32%`] + 2 Coin-Lock [`250 KRISTA`, `8%`])** and **Track 2 (Open PoW Pool — 5 Seats, `20%`: 3 Funded PoW-Lock [`10 KRISTA`, `12%`] + 2 Permanent Free Entry [`0 KRISTA`, `8%`])** guarantees that low-cost PoW registrations can never crowd out collateralized validators or reach the `8`-of-`15` quorum threshold alone, while preserving permissionless mining onboarding.

## Motivation
In a purely flat candidate pool where every registered public key has an equal $1/P$ probability of election regardless of collateral tier, an adversary could flood the pool with low-cost `PoW-Lock` identities and capture a majority of the `15` (`11` Pre-Model D) validator seats—either stalling block production by withholding solutions or dominating participant rewards without locking Masternode collateral.

Conversely, requiring 4,200 KRISTA or 250 KRISTA from every participant would prevent new CPU/GPU miners with zero initial balance from joining the network.

`KTIP-0009` resolves this tension through two mechanisms:
1. **Hardened Dual-Path Miner Registration**: Enforces strict collateral, confirmation depth, and per-block rate limits on registration transactions.
2. **Dual-Track ($8 + 2 + 5$ Balanced Model D / $7 + 1 + 3$ Pre-Model D) Committee Quota Isolation**: Under Model D, allocates `8` seats to Active Masternodes (`4,200 KRISTA`), `2` seats to Coin-Lock validators (`250 KRISTA`), and `5` seats to Open PoW miners (`3` Funded `10 KRISTA` PoW-Lock + `2` Permanent Free Entry `0 KRISTA`)—expanding from `7 + 1 + 3` Pre-Model D—enforced at compile time (`static_assert`) and runtime (`assert`).

## Specification

### 1. Dual-Path Miner Registration (`registerminer`)

#### A. Coin-Lock Registration (`250 KRISTA`)
* **Lock Amount**: Requires `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`) with a 33-byte compressed public key. Under `UPGRADE_HARDENING_V4` (`"Hardening_v4"`, Height `1` across all networks), `MatchCoinLockRegistration` prepends mandatory **`<48-byte blsPubKey> <96-byte blsPoP> OP_2DROP`** verified by `CheckRegistrationBlsBinding` (`bad-txns-minerreg-missing-bls`, `bad-txns-minerreg-bls-pop`, `bad-txns-minerreg-dup-bls`), binding `VerifyAdamOrDip3Signature` to the pinned BLS key. Outputs locking less than 250 KRISTA in a Coin-Lock script format (`MatchCoinLockRegistration`) are rejected by mempool policy (`AcceptToMemoryPoolWorker` with `bad-txns-coinlock-amount-too-small`) and ignored during active pool scanning (`GetAdamMinerPool`).
* **Anti-Sybil Input Maturity, Rate Limits & Per-Wallet Guard**: All transaction inputs (`vin`) must spend UTXOs with at least **`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-txns-coinlock-input-immature` in mempool and `ConnectBlock`); consensus (`CheckBlock`), mempool, `CreateNewBlock`, and `ExtractBlockMinerRegEntries` enforce at most **1 `Coin-Lock` output per transaction** (`bad-txns-too-many-coinlock`) and **`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1` `Coin-Lock` transaction per block** (`bad-blk-too-many-coinlock`); and the wallet/RPC/GUI layer enforces at most **1 active `Coin-Lock` seat per wallet** (renewable in the final `240` blocks on Mainnet or `15` blocks post-Model-D on Testnet/Regtest).
* **Activation**: Consensus and manual `registerminer lock` accept `250 KRISTA` Coin-Lock registrations at **all block heights** across Mainnet, Testnet, and Regtest, whereas background `AutoRegisterMiner` waits until Model D is active (`IsModelDActive`: `nHeight >= 200` on Mainnet, Testnet, and Regtest) so bootstrap rewards remain liquid, and thereafter auto-selects `Coin-Lock` only when renewing the wallet's own expiring `Coin-Lock` seat or when `Sub-Track 1B` holds `< 2` active/pending `Coin-Lock` seats under Model D (`< 1` Pre-Model D) while `Track 2` already holds at least `nFundedPoWTarget = 3` active Funded `PoW-Lock` validators (`nOpenPoWQuota - nZeroMax`).
* **Lock Duration**: `nLockHeight - nTxHeight >= nRegPeriod` (`2,880` blocks on Mainnet; `100` blocks on Testnet/Regtest once Model D is active, `2,880` blocks pre-Model D).

#### B. PoW-Lock Registration (`10 KRISTA` Funded / `0 KRISTA` Zero-Coin Bootstrap)
* **Out-of-Band PoW Challenge & Mandatory BLS+PoP Pinning (`MatchPoWLockRegistration`)**: Under `UPGRADE_HARDENING_V4` (`"Hardening_v4"`, Height `1` across all networks), registration scripts prepend mandatory **`<48-byte blsPubKey> <96-byte blsPoP> OP_2DROP`** (`bad-txns-minerreg-missing-bls`, `bad-txns-minerreg-bls-pop`, `bad-txns-minerreg-dup-bls`) ahead of `<4-byte LE32 nonce> <32-byte challenge blockHash> <33-byte compressed minerPubKey> OP_DROP OP_DROP OP_DROP` bound to an ancestor block:
  - **Funded `PoW-Lock` (`10 KRISTA`, unified on `HashX11KVS` under `UPGRADE_HARDENING_V2` and V4 BLS-bound under `UPGRADE_HARDENING_V4` at Block `1`)**: Challenge within the last `100` blocks (`pindexChallenge->nHeight >= nHeight - 100`); solves 7-level recursive **`HashX11KVS("KRPL" || Hash(challenge || blsPubKey) || 33-byte compressed minerPubKey || "POWLKv4" || LE32(nNonce))`** (`ComputeMinerPoWRegistrationHash` / `BuildMinerPoWRegistrationHeaderPrefix`; pre-V4 used raw `challenge` and `"POWLKv2"`) meeting `GetMinerPoWLimit(..., true)` (**15-bit** zero prefix on Mainnet [`~14.8s` CPU], **13-bit** on Testnet, **8-bit** on Regtest), enforced in both mempool admission (`bad-txns-powlock-invalid` / `bad-txns-powlock-stale-challenge`) and consensus pool scanning (`ExtractBlockMinerRegEntries`).
  - **Zero-Coin `PoW-Lock` (`0 KRISTA`, hardened under `UPGRADE_HARDENING_V2`, `UPGRADE_FAIRNESS`, & `UPGRADE_HARDENING_V4` at Block `1`)**: Challenge within the last **`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` blocks** on-chain (`8` blocks at mempool admission); solves 7-level recursive **`HashX11KVS("KRZC" || Hash(challenge || blsPubKey) || 33-byte compressed minerPubKey || "SYBILV4" || LE32(nNonce))`** (`ComputeZeroCoinRegistrationHash` / `BuildZeroCoinRegistrationHeaderPrefix`; pre-V4 used raw `challenge` and `"SYBILV2"`) meeting `GetZeroCoinPoWLimit` (**15-bit** zero prefix on Mainnet [`0001ffff...`, parity with Funded PoW-Lock] under `UPGRADE_FAIRNESS`, **12-bit** on Testnet, **8-bit** on Regtest).
* **Funded `PoW-Lock` (`10 KRISTA`)**:
  - Must lock `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`, enforced in mempool and `CheckBlock` via `bad-txns-powlock-amount-too-small`).
  - All transaction inputs (`vin`) must spend UTXOs with at least **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-txns-powlock-input-immature` in mempool and `ConnectBlock`), preventing rapid UTXO splitting to farm multiple identities.
  - Rate-limited to at most **1 `PoW-Lock` output per transaction** (`bad-txns-too-many-powlock`) and **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`** funded `PoW-Lock` transaction per block (`bad-blk-too-many-powlock`, tracked independently from the `1` `Zero-Coin PoW` slot per block), with the wallet/RPC/GUI layer enforcing at most **1 active `PoW` seat per wallet** (`0 -> 10 KRISTA` upgrade allowed anytime; funded renewal window in the final `240` / `15` blocks).
* **Zero-Coin / Permanent Open-Entry `PoW-Lock` (`0 KRISTA`)**:
  - Permitted (`nValue == 0` with sentinel `0x01:0` input, capped at `1` per block via `bad-blk-too-many-zerocoin-pow`) ONLY while the **funded** validator pool (`GetFundedAdamMinerPoolCount`: Active Masternodes + `250 KRISTA` Coin-Lock + `10 KRISTA` Funded PoW-Lock) has fewer than **`GetAdamMinersCount(h) + 2` funded validators (`17` under Model D)**, or unconditionally once `nHeight >= consensus.nAdamOpenEntryHeight` (**`1` across Mainnet, Testnet, and Regtest**), keeping `0-KRISTA` **Open-Entry** **permanently open** for **`2` dedicated Track 2 seats under Model D**, preserving capped permissionless participation.
  - Under `UPGRADE_HARDENING_V2` & `UPGRADE_FAIRNESS`, active `Zero-Coin / Open-Entry PoW` seats have a **`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240`-block** (`~2 hours` on Mainnet; `50` blocks post-Model-D on Testnet/Regtest) maximum lease TTL (`nEffectiveExpiry = min(lockTime, nTxHeight + nZeroActivePeriod)`), expand to **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS = 240`** newest distinct keys under Fairness (`16` legacy V2), preserve Block `1..199` Classic PoW bootstrap coinbase miners ahead of Zero-Coin keys, reject duplicate mempool renewals for already-active keys (`bad-txns-zerocoin-key-already-active`), and automatically promote to Funded `10 KRISTA` / `250 KRISTA` locks via `AutoRegisterMiner` once matured wallet balance exists. All registration locks enforce strict block-height CLTV (`< LOCKTIME_THRESHOLD = 500,000,000`, `<= nHeight + 5,760`).

---

### 2. Dual-Track ($8 + 2 + 5$ Balanced Model D / $7 + 1 + 3$ Pre-Model D) Committee Quota Election (`ElectDualTrackValidators`)
When `nHeight >= consensus.nAdamDualTrackHeight` (block `200` on Mainnet/Testnet matching `UPGRADE_ADAM`, block `1` on Regtest), `SelectAdamNodes` (`src/adam.cpp`) partitions the candidate pool (excluding the elected Coordinator) into deterministic sub-pools sorted by $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$—using the **15-seat (`8 + 2 + 5`) Balanced Model D** layout upon `UPGRADE_MODELD` (block `200` across Mainnet, Testnet, and Regtest):

| Track / Sub-Track | Eligible Candidates | Target Quota (Model D / Pre-Model D) | Compile-Time / Runtime Invariant |
| :--- | :--- | :---: | :--- |
| **Sub-Track 1A (Masternodes)** | Active Masternodes (`4,200 KRISTA` + on-chain ping $\le 2,880$ blocks) | **8 Seats (`32%`)** / `7` Pre-Model D | `ADAM_MODELD_MASTERNODE_TRACK_QUOTA = 8` (`7` Pre-Model D) |
| **Sub-Track 1B (Coin-Lock)** | Active `Coin-Lock` Validators (`250 KRISTA` CLTV, strictly isolated to primary seats) | **2 Isolated Seats (`8%`)** / `1` Pre-Model D | `ADAM_MODELD_COIN_LOCK_TRACK_QUOTA = 2` (`1` Pre-Model D) |
| **Track 1 Total (Collateralized)** | Sub-Track 1A + Sub-Track 1B (`< 2` Coin-Lock spills to Active MN; `< 8` MN spills to Open PoW first) | **10 Seats (`66.7%` seats / `40%` reward)** / `8` (`72.7%`) Pre-Model D | `ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA = 10` (`8` Pre-Model D) |
| **Track 2 (Open PoW)** | From `nAdamOpenEntryHeight` (`1` Mainnet, Testnet & Regtest): **3 seats** `Tier 2A` (`10 KRISTA` `PoW-Lock`, `12%`, spilling to Active MN / Bootstrap) + **2 permanent seats** `Tier 2C` (`0 KRISTA` Open-Entry, `8%`, max `240` under Fairness, spilling to Funded PoW-Lock / Bootstrap); Pre-Model D `2` Funded + `1` Free Entry | **5 Seats (`33.3%` seats / `20%` reward)** / `3` (`27.3%`) Pre-Model D | `ADAM_MODELD_OPEN_POW_TRACK_QUOTA = 5`, `ADAM_MODELD_ZERO_COIN_OPEN_SEATS = 2` (`3` / `1` Pre-Model D) |

#### Compile-Time and Runtime Invariants
In `src/adam.h` and `src/chainparams.cpp`:
```cpp
// Pre-Model D (11 seats: 7 + 1 + 3)
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Collateralized sub-track quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD,
              "Masternode track quota must be at least ADAM_MAX_CONSENSUS_THRESHOLD so Masternodes alone can always form a quorum");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);

// Balanced Model D (15 seats: 8 + 2 + 5)
static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA + ADAM_MODELD_COIN_LOCK_TRACK_QUOTA == ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA,
              "Model D collateralized sub-track quotas must sum to ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MODELD_COLLATERALIZED_TRACK_QUOTA + ADAM_MODELD_OPEN_POW_TRACK_QUOTA == ADAM_MODELD_MINERS_COUNT,
              "Model D track quotas must sum to ADAM_MODELD_MINERS_COUNT");
static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MODELD_MAX_CONSENSUS_THRESHOLD,
              "Model D Masternode track quota must be at least ADAM_MODELD_MAX_CONSENSUS_THRESHOLD so Masternodes alone can form a quorum");
static_assert(ADAM_MODELD_MIN_RELAXED_THRESHOLD > ADAM_MODELD_COIN_LOCK_TRACK_QUOTA + ADAM_MODELD_OPEN_POW_TRACK_QUOTA,
              "Model D post-bootstrap relaxed threshold floor must exceed non-Masternode seats so at least 1 Masternode is always required");
assert(Consensus::Params::ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThresholdModelD);
```
Because `8 >= nAdamThresholdModelD (8)` under Model D (and `7 >= nAdamThreshold (7)` Pre-Model D), the `8` Active Masternode seats alone are sufficient to reach the post-bootstrap quorum threshold even if all `7` non-Masternode seats (`2 Coin-Lock + 5 Open PoW`) withhold solutions.

#### Strict Quota Isolation, Ordered Spillover, Equal Seat Pay & Proportional Coordinator VRF (`4,200 : 250 : 10`)
1. **Strict `2`-Seat `Coin-Lock` Isolation (`1` Pre-Model D)**: `Sub-Track 1B` elects at most **`2`** `Coin-Lock` validators under Model D (`1` Pre-Model D). Unselected `Coin-Lock` keys do **not** spill into `Sub-Track 1A` ahead of Open PoW and do **not** enter Track 2 `Tier 2A` (`vFundedPoWRanked`). If fewer than `2` `Coin-Lock` validators exist under Model D (`< 1` Pre-Model D), vacant seats immediately spill to remaining Active Masternodes (`4,200 KRISTA`).
2. **Ordered Empty-Seat Spillover & Standby Priority**: Any remaining vacant seats across the `15`-validator committee (`11` Pre-Model D) are filled in strict order: **remaining Active Masternodes (`4,200 KRISTA`) $\rightarrow$ remaining Open PoW (`10 KRISTA` `PoW-Lock` $\rightarrow$ `Bootstrap` $\rightarrow$ `Zero-Coin PoW`) $\rightarrow$ unselected `Coin-Lock` (`250 KRISTA`, last-resort liveness fallback only when `< 15` total candidates remain under Model D / `< 11` Pre-Model D)**. Standby validators follow the exact same `Active Masternode -> Open PoW -> Coin-Lock` priority order.
3. **Equal Work → Equal Seat Pay, Proportional Capital Coordinator VRF (`4,200 : 250 : 10`) & `ADAM_VRF_KEY_MATURITY = 100`**: Under `UPGRADE_FAIRNESS` (Height `1`), `ComputeModelDValidatorSeatPayments` divides the Active Validator pool **equally across all `15` committee seats under Model D (`60% / 15 = 4.00%` per solved seat; exact `0.6324 KRISTA` in Period 0 with `0` remainder when all `15/15` seats solve)** and across `11` seats Pre-Model D (`75% / 11 ≈ 6.818%`), regardless of MN, Coin-Lock, or Open-PoW track. Meanwhile, `SelectAdamNodes` elects the primary Coordinator via proportional locked-capital VRF across **all funded validators without per-track caps (`4,200` per Active MN, `250` per `Coin-Lock`, and `10` per `Funded PoW-Lock`), enforcing an exact `>= 55%` Active MN floor under `ADAM_FAIR_COORD_V5` (`11 * sumNonMN > 9 * sumMN`) once `UPGRADE_MODELD` is active** (vs. `>= 75%` under `ADAM_FAIR_COORD_V4` when `3 * sumNonMN > sumMN` Pre-Model D; replacing legacy `65:8:2`). Under `UPGRADE_HARDENING_V4` (Height `1`), `SelectAdamNodes` and `RankValidatorsByPoBLSDistance` enforce **`ADAM_VRF_KEY_MATURITY = 100` blocks**, prioritizing validator keys matured for at least `100` blocks ahead of freshly registered keys (falling back to unmatured keys only if no matured keys exist).

## Backward Compatibility
* Activated at `consensus.nAdamDualTrackHeight` (block `200` on Mainnet/Testnet, block `1` on Regtest).
* Existing `Coin-Lock` (`>= 250 KRISTA`) and funded `PoW-Lock` (`>= 10 KRISTA`) scripts remain compatible.

## Reference Implementation
* Quota constants and compile-time assertions: `src/adam.h` and `src/chainparams.cpp`.
* Dual-Track election and pool classification: `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey`, and `GetAdamMinerPool` in `src/adam.cpp`.
* Mempool and block validation of registration transactions: `src/main.cpp` and `src/rpc/mining.cpp`.
