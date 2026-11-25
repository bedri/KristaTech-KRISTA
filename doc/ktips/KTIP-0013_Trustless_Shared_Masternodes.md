```
KTIP: 0013
Title: Shared Masternodes via Taproot, PSBT & KTDIP3 Multi-Payee Consensus (Rev 2)
Author: KristaTech Core Developers
Status: Draft (Deferred — Post-Maturity Roadmap)
Type: Standard Track (Consensus / KTDIP3 / Wallet)
Created: 2026-11-17
Updated: 2026-11-24
Requires: KTIP-0005, KTIP-0006, KTIP-0008, KTIP-0012, KTIP-0014
```

## Abstract
This revised specification defines a **non-custodial (zero-operator-custody) Shared Masternode** protocol built directly on **KTDIP3 (`KTIP-0014`)** deterministic masternodes, **Taproot (`kt1p...` / BIP341)**, **Schnorr (`BIP340`)**, **BIP68/CSV relative timelocks**, **BIP174 PSBT**, and **consensus Multi-Payee reward splitting** for up to **`5` co-owners** (`>= 20%` share each) sharing a `4,200 KRISTA` collateral.

> **Roadmap & Scope Note:** Because KristaTech already provides accessible, low-capital participation natively via **PoS Staking**, **Track 1 Coin-Lock (`250 KRISTA`)**, **Track 2A PoW-Lock (`10 KRISTA`)**, and **Track 2C Zero-Coin / Free Entry (`0 KRISTA`)**, KTIP-0013 is **intentionally deferred** to the post-maturity roadmap (to be reconsidered once network value makes the `4,200 KRISTA` single-owner collateral a practical barrier). Furthermore, Section 4 explicitly distinguishes **Stage A (Consortium / DAO / Trusted-Majority Pools)** from **Stage B (Fully Trustless Anonymous Pools)**, which requires additional protocol primitives (`txid` non-malleability or output-constraining covenants + audited `BIP327 MuSig2`).

## Motivation
Under the active **KTDIP3 (`KTIP-0014` + `UPGRADE_HARDENING_V3`)** rules (`src/evo/deterministicmns.cpp`, `src/masternode-payments.cpp`), every deterministic masternode enforces two single-owner assumptions:
1. **Single-Key Collateral Ownership**: The `4,200 KRISTA` collateral outpoint must be held by a single `pubKeyOwner` / `P2PKH` collateral destination, and ownership changes (`CProUpRegTx` / `CProUpRevTx`) are authorized by one compressed `pubKeyOwner` key (`CPubKey::VerifyStrictDER`).
2. **Single Owner Payout Script (`scriptPayout`)**: After optional operator splitting (`nOperatorReward` in `0..10000` bps paid to `scriptOperatorPayout`), `100%` of the remaining Masternode owner rewards (`5%` Passive Pool, `32%` Validator Track [`8` MN seats × `4.00%` out of `60% / 15`], `10%` Signer-Only LLMQ Quorum — up to `47%` total excl. Coordinator VRF under Balanced Model D) go to a single `scriptPayout`. Any shared pool today must either trust a custodian holding the collateral key or trust one co-owner to manually forward rewards.

KTIP-0013 Rev 2 eliminates operator custody and manual reward distribution at the **KTDIP3 consensus layer** while strictly bounding UTXO bloat and remaining transparent about multi-party principal recovery assumptions.

---

## Specification

### 1. Taproot Collateral & Co-Ownership (`UPGRADE_SHARED_MN`)
Upon activation of `UPGRADE_SHARED_MN` at height `H_shared`:
* **Accepted Collateral Scripts in `CProRegTx` v3**: A `4,200 KRISTA` (`MASTERNODE_COLLATERAL * COIN`) unspent collateral output may lock to either:
  1. Single-owner `P2PKH` (`KT...`), or
  2. **Taproot (`WitnessV1Taproot`, `OP_1 <32B x-only pubkey>`)** (`kt1p...`).
* **Taproot Internal Construction**:
  * **Key-Path (Cooperative Exit / Rebalance)**: Either a single Tapscript-backed control key or (in Stage B) an aggregated `N-of-N` `BIP340` / `MuSig2` x-only output key across all pool participants.
  * **Script-Path (Recovery / Timeout)**: A `BIP342 Tapscript` leaf combining `OP_CHECKSEQUENCEVERIFY` (`BIP68`, e.g., `43,200` blocks ≈ 30 days) with `M-of-N` (`OP_CHECKSIGADD`, already implemented in KristaTech's script engine) or covenant-enforced refund paths (see Stage A vs. Stage B in Section 4).

### 2. KTDIP3 `CProRegTx` v3 & `CProUpRegTx` v3 Integration
*(Supersedes all legacy `CMasternodeBroadcast` / `mnb` v2 / `SendOnChainPing` concepts.)*

Shared Masternodes integrate natively into **KTDIP3 (`evodb`)** via `nVersion = 3` on `CProRegTx` and `CProUpRegTx`:
```cpp
struct CMasternodePayeeShare {
    CScript scriptPubKey; // Standard spendable P2PKH (KT...), P2SH (7...), or WitnessV1Taproot (kt1p...)
    uint16_t nShareBps;   // Basis points (2000..10000; 100 bps = 1.00%)
};
```
* **Collateral Authorization**:
  * `CProRegTx` v3 retains KTDIP3 `blsPubKeyOperator` + mandatory `96`-byte **`blsPoP`** (Proof-of-Possession, required under `UPGRADE_HARDENING_V3`), `addr`, `keyIDVoting`, and `nOperatorReward`.
  * When the collateral UTXO is `WitnessV1Taproot` and/or `vPayees` is non-empty:
    * `CProRegTx` v3 carries `vPayees` alongside a **BIP340 Schnorr signature** (or Tapscript witness proof) over `CProRegTx::GetSignHash()` under the Taproot collateral key.
    * `CProUpRegTx` v3 (for changing `blsPubKeyOperator`, `keyIDVoting`, or `vPayees`) requires valid authorization under the same collateral key/script, while keeping `scriptOperatorPayout` cleared when `nOperatorReward == 0` (preserving `M-5` hardening).
* **Hot Operator Isolation (KTIP-0014 Rule 8 Preserved)**:
  * Hot nodes continue running via **`activemasternode.conf`** (`Single-IP Multi-MN`) using only the `blsPrivKey` operator key (`kristatech.conf` never contains `masternodeprivkey=`).
  * The VPS operator participates in LLMQ quorum signing, PoBLS, and ADAM puzzle solving with **zero spending authority** over the `4,200 KRISTA` collateral and **zero ability** to modify `vPayees`.

### 3. Bounded Multi-Payee Reward Splitting & Anti-Dust Rules
To prevent `chainstate` UTXO bloat and micro-dust outputs in Model D blocks:
1. **Strict Participant Cap**: `1 <= vPayees.size() <= MAX_SHARED_MN_PAYEES` (**`5`** — down from `21`).
2. **Minimum Meaningful Share**: Every entry must satisfy `nShareBps >= MIN_SHARED_MN_SHARE_BPS` (**`2,000` bps = `20.00%`**, i.e., `>= 840 KRISTA` equivalent collateral stake).
3. **Exact Sum**: $\sum_{i=0}^{k-1} \text{nShareBps}_i = 10{,}000$ (`100.00%`).
4. **Standard & Spendable Scripts Only**: Every `scriptPubKey` must be standard `P2PKH`, `P2SH`, or `WitnessV1Taproot` (`<= 34` bytes), `!IsUnspendable()`, and not in `mBurnAddresses`.
5. **Model D Fan-Out Bound**: Under Balanced Model D (`1` Passive MN + up to `8` MN Validator seats + dynamic LLMQ signers [`5` baseline $\rightarrow$ `14` MN rewards $\rightarrow$ **`70` outputs/block**; scaling up to `15` max LLMQ signers at `>= 100` active MNs $\rightarrow$ `24` MN rewards $\rightarrow$ **`120` outputs/block**]), `MAX_SHARED_MN_PAYEES = 5` keeps total MN payout outputs bounded (vs. `294` / `504` under a `21`-payee cap).

**Deterministic 128-Bit Settlement (`FillBlockPayee` / `IsBlockPayeeValid`):**
1. First compute optional KTDIP3 operator fee $R_{\text{op}}$ (`ProUpServTx` `scriptOperatorPayout`, only when `nOperatorReward > 0` and valid payout script is set):
   $$R_{\text{op}} = \left\lfloor \frac{\text{unsigned \_\_int128}(R_{\text{mn}}) \times n_{\text{OperatorReward}}}{10{,}000} \right\rfloor, \qquad R_{\text{owner}} = R_{\text{mn}} - R_{\text{op}}$$
2. Split $R_{\text{owner}}$ across `vPayees` using deterministic `unsigned __int128` arithmetic (matching `L-1` consensus integer math):
   $$R_i = \left\lfloor \frac{\text{unsigned \_\_int128}(R_{\text{owner}}) \times \text{nShareBps}_i}{10{,}000} \right\rfloor$$
3. **Dust & Remainder Guard**: Any division remainder $\left(R_{\text{owner}} - \sum_i R_i\right)$ (at most `4` satoshis), as well as any individual share falling below `CTxOut::GetDustThreshold(minRelayTxFee)` late in the emission tail, is deterministically folded into `vPayees[0]` so coinbase issuance matches consensus to the exact satoshi and never emits unspendable dust outputs.
4. *(Optional)* A `vPayees[i].scriptPubKey` may target a `kt1p...` **MESCAL** Taproot script address for programmable DAO treasury or vesting rules.

---

## 4. Security Threat Model & Phased Prerequisites (Critical Engineering Disclosure)

KristaTech's current script engine already supports **Taproot (`BIP340`/`BIP341`)**, **`OP_CHECKSIGADD`**, **`CSV (BIP68/112)`**, and **PSBT (`BIP174`)**, **but does NOT currently include `BIP327 MuSig2`, SegWit witness separation (`txid` non-malleability), or output-constraining covenants**.

Because of that distinction, KTIP-0013 defines two strict deployment stages:

### Stage A — Consortium / DAO / Known-Partner Pools *(Possible with Current Script Primitives)*
* **What it solves**: Eliminates **single-operator custody** and **manual reward forwarding**. A team, family office, or DAO of up to `5` known partners uses a `Taproot M-of-N` (`OP_CHECKSIGADD` + optional `CSV` timeout leaf) to lock `4,200 KRISTA` while delegating uptime exclusively to a hot VPS operator.
* **Explicit Limitation (Why Stage A is NOT anonymous-trustless)**: Without `txid` non-malleability or output-constraining covenants, an `M-of-N` recovery leaf does not restrict destination outputs; a colluding `M`-of-`N` majority could redirect the `4,200 KRISTA` principal during fallback recovery, while pure `N-of-N` without timeout faces a hostage/key-loss liveness deadlock. Stage A must therefore be clearly presented in wallets/UI as **Consortium / Trusted-Majority Co-Ownership** — not anonymous pool investing.

### Stage B — Fully Trustless Anonymous Pools *(Mandatory Protocol Prerequisites)*
Anonymous strangers co-funding a Masternode without trusting a majority require **both**:
1. **Unilateral Principal Exit Guarantee**: Either
   * **(a) SegWit / Non-Malleable `txid`**, allowing all co-owners to pre-sign proportional refund transactions (`CSV`-locked) *before* signing and broadcasting the funding transaction without third-party `txid` malleability invalidation, **OR**
   * **(b) Output-Constraining Tapscript / MESCAL Covenants**, enforcing on-chain that fallback recovery splits outputs proportionally back to each co-owner's pre-committed `scriptPubKey`.
2. **Audited `BIP327 MuSig2` & Multi-Party PSBT Coordination**: A deterministic, nonce-safe `MuSig2` implementation and coordinator/P2P session layer for `N`-party funding and exit signing.

### Governance Decentralization ("Lido Effect") Mitigation
* Shared Masternode UIs and registries should encourage co-owners to retain `keyIDVoting` under a co-owner key (or rotated consortium key) rather than defaulting governance voting power to commercial VPS pool providers.
* Existing KTDIP3 **PoSe penalty & `nPoseReviveCooldownBlocks`** (`120` blocks Mainnet) apply transparently to `CProRegTx` v3 nodes: if an operator goes offline, rewards halt without endangering the `4,200 KRISTA` Taproot collateral.

---

## Backward Compatibility
* Requires a coordinated network upgrade (`UPGRADE_SHARED_MN` activation height).
* Existing single-owner **KTDIP3 v1/v2** masternodes (`vPayees.empty()`, `P2PKH` collateral) remain `100%` valid and unaffected.
