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
* **Lock Amount**: Requires `nValue >= MINER_REGISTRATION_LOCK_AMOUNT` (`250 * COIN`). Outputs locking less than 250 KRISTA in a Coin-Lock script format are rejected by both mempool policy (`AcceptToMemoryPoolWorker`) and block validation (`CheckBlock` with `bad-miner-reg-amount`).
* **Activation**: Enabled on Mainnet once Model D is active (`nHeight >= 2200`), and enabled at all heights on Testnet/Regtest.
* **Lock Duration**: `nLockHeight - nTxHeight >= nRegPeriod` (`2,880` blocks on Mainnet; `100` blocks on Testnet/Regtest under Model D).

#### B. PoW-Lock Registration (`10 KRISTA` Funded / `0 KRISTA` Zero-Coin Bootstrap)
* **Out-of-Band PoW Challenge**: Requires solving a 32-byte payload `[24-byte blockHash prefix || 8-byte nonce]` bound to a recent chain block within the last `nRegPeriod` blocks such that `SHA256D(blockHash || minerPubKey || nonce)` has at least 16 leading zero bits.
* **Funded `PoW-Lock` (`10 KRISTA`)**:
  - Must lock `nValue >= POW_LOCK_REGISTRATION_AMOUNT` (`10 * COIN`).
  - All transaction inputs (`vin`) must spend UTXOs with at least **`ADAM_POW_LOCK_MIN_INPUT_DEPTH = 100` confirmations** (`bad-pow-lock-input-depth`), preventing rapid UTXO splitting to farm multiple identities.
  - Rate-limited to at most **`ADAM_MAX_POW_LOCK_TX_PER_BLOCK = 3`** funded `PoW-Lock` transactions per block (`bad-pow-lock-block-quota`).
* **Zero-Coin Bootstrap `PoW-Lock` (`0 KRISTA`)**:
  - Permitted (`nValue == 0`) only when the active validator pool (`GetAdamMinerPool`) has fewer than **13 active keys** (`ADAM_ZERO_COIN_POW_MAX_POOL = 13`) to guarantee bootstrap liveness.

---

### 2. Dual-Track ($7 + 1 + 3$) Committee Quota Election (`ElectDualTrackValidators`)
When `nHeight >= consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM` = block 200 on Mainnet/Testnet, block 1 on Regtest), `SelectAdamNodes` (`src/adam.cpp`) partitions the candidate pool (excluding the elected Coordinator) into three deterministic sub-pools sorted by $\text{SHA256D}(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i)$:

| Track / Sub-Track | Eligible Candidates | Target Quota | Compile-Time / Runtime Invariant |
| :--- | :--- | :---: | :--- |
| **Sub-Track 1A (Masternodes)** | Active Masternodes (`4,200 KRISTA` + on-chain ping $\le 2,880$ blocks) | **7 Seats** | `ADAM_MASTERNODE_TRACK_QUOTA = 7` |
| **Sub-Track 1B (Coin-Lock)** | Active `Coin-Lock` Validators (`250 KRISTA` CLTV) | **1 Seat** | `ADAM_COIN_LOCK_TRACK_QUOTA = 1` |
| **Track 1 Total (Collateralized)** | Sub-Track 1A + Sub-Track 1B (with immediate intra-Track-1 spillover) | **8 Seats (72.7%)** | `ADAM_COLLATERALIZED_TRACK_QUOTA = 8` |
| **Track 2 (Open PoW)** | `PoW-Lock` (`10 KRISTA`), `Zero-Coin PoW` (`0 KRISTA`), Bootstrap keys | **3 Seats (27.3%)** | `ADAM_OPEN_POW_TRACK_QUOTA = 3` |

#### Compile-Time and Runtime Invariants
In `src/adam.h` and `src/chainparams.cpp`:
```cpp
static_assert(ADAM_MASTERNODE_TRACK_QUOTA + ADAM_COIN_LOCK_TRACK_QUOTA == ADAM_COLLATERALIZED_TRACK_QUOTA,
              "Track 1 sub-quotas must sum to ADAM_COLLATERALIZED_TRACK_QUOTA");
static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7,
              "Masternode track quota must be >= post-bootstrap quorum threshold (7)");
assert(ADAM_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThreshold);
```
Because `7 >= nAdamThreshold (7)`, the 7 Active Masternode seats alone are sufficient to reach the post-bootstrap quorum threshold even if all 4 non-Masternode seats (`1 Coin-Lock + 3 Open PoW`) withhold solutions.

#### Two-Stage Bidirectional Spillover
1. **Intra-Track-1 Spillover**: Any unfilled seats in Sub-Track 1A (`< 7` Active Masternodes) are immediately filled by remaining ranked candidates in Sub-Track 1B (`Coin-Lock`), and vice versa (`< 1` Coin-Lock validator spills over to remaining Active Masternodes).
2. **Inter-Track Spillover**: If Track 1 as a whole has fewer than 8 candidates, unfilled seats spill over to remaining ranked Track 2 (`Open PoW`) candidates; conversely, if Track 2 has fewer than 3 candidates, unfilled seats spill over to remaining ranked Track 1 candidates (Masternodes first, then Coin-Lock).

## Backward Compatibility
* Activated at `consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM`).
* Existing `Coin-Lock` (`>= 250 KRISTA`) and funded `PoW-Lock` (`>= 10 KRISTA`) scripts remain compatible.

## Reference Implementation
* Quota constants and compile-time assertions: `src/adam.h` and `src/chainparams.cpp`.
* Dual-Track election and pool classification: `ElectDualTrackValidators`, `SelectAdamNodes`, `IsCoinLockValidatorKey`, and `GetAdamMinerPool` in `src/adam.cpp`.
* Mempool and block validation of registration transactions: `src/main.cpp` and `src/rpc/mining.cpp`.
