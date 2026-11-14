```
KTIP: 0009
Title: Dual-Path Miner Registration & Dual-Track (7 + 1 + 3) Committee Quota Election
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus)
Created: 2026-08-15
```

## Abstract
This proposal specifies the on-chain **Dual-Path Miner Registration** (`Coin-Lock` and `PoW-Lock`) and **Dual-Track ($7 + 1 + 3$) Committee Quota Election (`ElectDualTrackValidators`)** mechanisms in the ADAM consensus layer. By partitioning the 11 validator seats into **Track 1 (Collateralized Pool — 8 Seats: 7 Active Masternodes [`4,200 KRISTA`] + 1 Coin-Lock [`250 KRISTA`])** and **Track 2 (Open PoW Pool — 3 Seats)**, the protocol guarantees that low-cost PoW registrations can never crowd out collateralized validators or reach the quorum threshold alone, while preserving permissionless mining onboarding.

## Motivation
In a purely flat candidate pool where every registered public key has an equal $1/P$ probability of election regardless of collateral tier, an adversary could flood the pool with low-cost `PoW-Lock` identities and capture a majority of the 11 validator seats—either stalling block production by withholding solutions or dominating participant rewards without locking Masternode collateral.

Conversely, requiring 4,200 KRISTA or 250 KRISTA from every participant would prevent new CPU/GPU miners with zero initial balance from joining the network.

`KTIP-0009` resolves this tension through two mechanisms:
1. **Hardened Dual-Path Miner Registration**: Enforces strict collateral, confirmation depth, and per-block rate limits on registration transactions.
2. **Dual-Track ($7 + 1 + 3$) Committee Quota Isolation**: Allocates 7 seats to Active Masternodes (`4,200 KRISTA`), 1 seat to Coin-Lock validators (`250 KRISTA`), and 3 seats to Open PoW miners (`10 KRISTA` / `0 KRISTA`), enforced at compile time (`static_assert`) and runtime (`assert`).

## Specification

### 1. Dual-Path Miner Registration (`registerminer`)

#### A. Coin-Lock Registration (`250 KRISTA`)
* **Lock Amount**: Requires `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`) with a 33-byte compressed public key. Outputs locking less than 250 KRISTA in a Coin-Lock script format (`MatchCoinLockRegistration`) are rejected by mempool policy (`AcceptToMemoryPoolWorker` with `bad-txns-coinlock-amount-too-small`) and ignored during active pool scanning (`GetAdamMinerPool`).
* **Anti-Sybil Input Maturity, Rate Limits & Per-Wallet Guard**: All transaction inputs (`vin`) must spend UTXOs with at least **`ADAM_COIN_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-txns-coinlock-input-immature` in mempool and `ConnectBlock`); consensus (`CheckBlock`), mempool, `CreateNewBlock`, and `ExtractBlockMinerRegEntries` enforce at most **1 `Coin-Lock` output per transaction** (`bad-txns-too-many-coinlock`) and **`ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1` `Coin-Lock` transaction per block** (`bad-blk-too-many-coinlock`); and the wallet/RPC/GUI layer enforces at most **1 active `Coin-Lock` seat per wallet** (renewable in the final `240` blocks on Mainnet or `15` blocks post-Model-D on Testnet/Regtest).
* **Activation**: Consensus and manual `registerminer lock` accept `250 KRISTA` Coin-Lock registrations at **all block heights** across Mainnet, Testnet, and Regtest, whereas background `AutoRegisterMiner` waits until `nHeight >= 2200` on Mainnet so bootstrap rewards remain liquid.
* **Lock Duration**: `nLockHeight - nTxHeight >= nRegPeriod` (`2,880` blocks on Mainnet; `100` blocks on Testnet/Regtest once Model D is active, `2,880` blocks pre-Model D).

#### B. PoW-Lock Registration (`10 KRISTA` Funded / `0 KRISTA` Zero-Coin Bootstrap)
* **Out-of-Band PoW Challenge (`MatchPoWLockRegistration`)**: Embeds `<4-byte LE32 nonce> <32-byte challenge blockHash> <33-byte compressed minerPubKey> OP_DROP OP_DROP OP_DROP` bound to an ancestor block:
  - **Funded `PoW-Lock` (`10 KRISTA`, unified on `HashX11KVS` under `UPGRADE_HARDENING_V2` at Block `1`)**: Challenge within the last `100` blocks (`pindexChallenge->nHeight >= nHeight - 100`); solves 7-level recursive **`HashX11KVS("KRPL" || challenge || 33-byte compressed minerPubKey || "POWLKv2" || LE32(nNonce))`** (`ComputeMinerPoWRegistrationHash`) meeting `GetMinerPoWLimit(..., true)` (**15-bit** zero prefix on Mainnet [`~14.8s` CPU], **13-bit** on Testnet, **8-bit** on Regtest), enforced in both mempool admission (`bad-txns-powlock-invalid` / `bad-txns-powlock-stale-challenge`) and consensus pool scanning (`ExtractBlockMinerRegEntries`).
  - **Zero-Coin `PoW-Lock` (`0 KRISTA`, hardened under `UPGRADE_HARDENING_V2` at Block `1`)**: Challenge within the last **`ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2 = 10` blocks** on-chain (`8` blocks at mempool admission); solves 7-level recursive **`HashX11KVS("KRZC" || challenge || 33-byte compressed minerPubKey || "SYBILV2" || LE32(nNonce))`** (`ComputeZeroCoinRegistrationHash`) meeting `GetZeroCoinPoWLimit` (**14-bit** zero prefix on Mainnet [`~7.4s` CPU], **12-bit** on Testnet, **8-bit** on Regtest).
* **Funded `PoW-Lock` (`10 KRISTA`)**:
  - Must lock `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`, enforced in mempool and `CheckBlock` via `bad-txns-powlock-amount-too-small`).
  - All transaction inputs (`vin`) must spend UTXOs with at least **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-txns-powlock-input-immature` in mempool and `ConnectBlock`), preventing rapid UTXO splitting to farm multiple identities.
  - Rate-limited to at most **1 `PoW-Lock` output per transaction** (`bad-txns-too-many-powlock`) and **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 1`** funded `PoW-Lock` transaction per block (`bad-blk-too-many-powlock`, tracked independently from the `1` `Zero-Coin PoW` slot per block), with the wallet/RPC/GUI layer enforcing at most **1 active `PoW` seat per wallet** (`0 -> 10 KRISTA` upgrade allowed anytime; funded renewal window in the final `240` / `15` blocks).
* **Zero-Coin Bootstrap `PoW-Lock` (`0 KRISTA`)**:
  - Permitted (`nValue == 0` with sentinel `0x01:0` input, capped at `1` per block via `bad-blk-too-many-zerocoin-pow`) ONLY while the **funded** validator pool (`GetFundedAdamMinerPoolCount`: Active Masternodes + `250 KRISTA` Coin-Lock + `10 KRISTA` Funded PoW-Lock) has fewer than **`consensus.nAdamMinersCount + 2` (`13`) funded validators**.
  - Under `UPGRADE_HARDENING_V2`, active `Zero-Coin PoW` seats have a **`ADAM_ZEROCOIN_POW_REG_PERIOD_V2 = 240`-block** (`~2 hours` on Mainnet; `50` blocks post-Model-D on Testnet/Regtest) maximum lease TTL (`nEffectiveExpiry = min(lockTime, nTxHeight + nZeroActivePeriod)`), are capped at **`MAX_ZEROCOIN_BOOTSTRAP_MINERS_V2 = 16`** newest distinct keys, preserve Block `1..199` Classic PoW bootstrap coinbase miners ahead of Zero-Coin keys, reject duplicate mempool renewals for already-active keys (`bad-txns-zerocoin-key-already-active`), and automatically promote to Funded `10 KRISTA` / `250 KRISTA` locks via `AutoRegisterMiner` once matured wallet balance exists. All registration locks enforce strict block-height CLTV (`< LOCKTIME_THRESHOLD = 500,000,000`, `<= nHeight + 5,760`).

---

### 2. Dual-Track ($7 + 1 + 3$) Committee Quota Election (`ElectDualTrackValidators`)
When `nHeight >= consensus.nAdamDualTrackHeight` (block `200` on Mainnet/Testnet matching `UPGRADE_ADAM`, block `1` on Regtest), `SelectAdamNodes` (`src/adam.cpp`) partitions the candidate pool (excluding the elected Coordinator) into three deterministic sub-pools sorted by $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$:

| Track / Sub-Track | Eligible Candidates | Target Quota | Compile-Time / Runtime Invariant |
| :--- | :--- | :---: | :--- |
| **Sub-Track 1A (Masternodes)** | Active Masternodes (`4,200 KRISTA` + on-chain ping $\le 2,880$ blocks) | **7 Seats** | `ADAM_MASTERNODE_TRACK_QUOTA = 7` |
| **Sub-Track 1B (Coin-Lock)** | Active `Coin-Lock` Validators (`250 KRISTA` CLTV, strictly isolated to 1 primary seat) | **1 Isolated Seat** | `ADAM_COIN_LOCK_TRACK_QUOTA = 1` |
| **Track 1 Total (Collateralized)** | Sub-Track 1A + Sub-Track 1B (`< 1` Coin-Lock spills to Active MN; `< 7` MN spills to Open PoW first) | **8 Seats (72.7%)** | `ADAM_COLLATERALIZED_TRACK_QUOTA = 8` |
| **Track 2 (Open PoW)** | Strict priority (`UPGRADE_HARDENING_V2`): `Tier 2A` (`10 KRISTA` `PoW-Lock` by VRF) $\rightarrow$ `Tier 2B` Bootstrap (`Blocks 1..199`) $\rightarrow$ `Tier 2C` `Zero-Coin PoW` (`0 KRISTA`, max `16`) | **3 Seats (27.3%)** | `ADAM_OPEN_POW_TRACK_QUOTA = 3` |

#### Compile-Time and Runtime Invariants
In `src/adam.h` and `src/chainparams.cpp`:
```cpp
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Track 1 sub-quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= Consensus::Params::ADAM_MAX_CONSENSUS_THRESHOLD,
              "Masternode track quota must be at least ADAM_MAX_CONSENSUS_THRESHOLD so Masternodes alone can always form a quorum");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);
```
Because `7 >= nAdamThreshold (7)`, the 7 Active Masternode seats alone are sufficient to reach the post-bootstrap quorum threshold even if all 4 non-Masternode seats (`1 Coin-Lock + 3 Open PoW`) withhold solutions.

#### Strict Quota Isolation, Ordered Spillover, Payout Protection & Quota-Capped Coordinator VRF
1. **Strict `1`-Seat `Coin-Lock` Isolation**: `Sub-Track 1B` elects at most **1** `Coin-Lock` validator (`8.00%` participant share). Unselected `Coin-Lock` keys do **not** spill into `Sub-Track 1A` ahead of Open PoW and do **not** enter Track 2 `Tier 2A` (`vTier2AFunded`). If no `Coin-Lock` validator exists (`< 1`), the vacant seat immediately spills to remaining Active Masternodes (`4,200 KRISTA`).
2. **Ordered Empty-Seat Spillover & Standby Priority**: Any remaining vacant seats across the 11-validator committee (e.g., when `< 7` Active Masternodes or `< 3` Open PoW miners exist) are filled in strict order: **remaining Active Masternodes (`4,200 KRISTA`) $\rightarrow$ remaining Open PoW (`10 KRISTA` `PoW-Lock` $\rightarrow$ `Bootstrap` $\rightarrow$ `Zero-Coin PoW`) $\rightarrow$ unselected `Coin-Lock` (`250 KRISTA`, last-resort liveness fallback only when `< 11` non-`Coin-Lock` + 1 primary `Coin-Lock` validators exist)**. Standby validators follow the exact same `Active Masternode -> Open PoW -> Coin-Lock` priority order.
3. **Track-Protected Seat Payouts & Quadratic Quota-Capped Coordinator VRF**: In `ComputeModelDValidatorSeatPayments` (active from Block `200` via `IsAdamValidatorSplitActive`), at most `1` `Coin-Lock` validator receives `8.00%`, up to `3` non-Masternode, non-`Coin-Lock` Open PoW validators (`Funded PoW-Lock 10 -> Bootstrap -> Zero-Coin PoW`) receive `4.00%` each, and any overflow validator (Open PoW or last-resort `Coin-Lock`) filling a vacant Masternode seat receives `nMNSeatBase` (`~2.857%`). Meanwhile, `SelectAdamNodes` elects the primary Coordinator via quota-capped quadratic stake-weighted VRF ($\sqrt{\text{Collateral}}$) across **all Active Masternodes (`65` each), at most `1` VRF-top `Coin-Lock` representative (`16`), and at most `3` VRF-top `Funded PoW-Lock` representatives (`4` each, capping Open PoW Coordinator weight at `12` and maintaining a symmetric `~4x` per-tier ladder `4 -> 16 -> 65`)**.

## Backward Compatibility
* Activated at `consensus.nAdamDualTrackHeight` (block `200` on Mainnet/Testnet, block `1` on Regtest).
* Existing `Coin-Lock` (`>= 250 KRISTA`) and funded `PoW-Lock` (`>= 10 KRISTA`) scripts remain compatible.

## Reference Implementation
* Quota constants and compile-time assertions: `src/adam.h` and `src/chainparams.cpp`.
* Dual-Track election and pool classification: `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey`, and `GetAdamMinerPool` in `src/adam.cpp`.
* Mempool and block validation of registration transactions: `src/main.cpp` and `src/rpc/mining.cpp`.
