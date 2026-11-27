# KTIP-0014: Deterministic Masternodes (KRISTA-DIP3 & Hot/Cold BLS)

- **KTIP**: 0014
- **Title**: Deterministic On-Chain Masternodes (KRISTA-DIP3 & Hot/Cold BLS)
- **Author**: KristaTech Core Developers
- **Status**: Active / Implemented
- **Type**: Consensus & Protocol Upgrade (`UPGRADE_DIP3`)
- **Created**: 2026-11-17

---

## Abstract

KTIP-0014 replaces KristaTech's legacy hybrid Masternode tracking (`mnodeman` P2P announcements + `MasternodePingState` in-memory/LevelDB ping window + `fBlockOfflineSync` historical bypass) with a **100% deterministic, on-chain Masternode list (`CDeterministicMNManager` / `evodb`)** modeled after Dash DIP3 and tailored to KristaTech's Rule 8 (multi-MN per IP) and ADAM Dual-Track consensus.

## Motivation

In the legacy architecture, nodes could diverge on the active Masternode set across restarts, cache flushes, or Initial Block Download (IBD), causing nodes to reject valid blocks with `bad-cb-payee` / `Invalid standby validator substitution`. By deriving the Masternode list strictly from on-chain special transactions (`ProRegTx`, `ProUpServTx`, `ProUpRegTx`, `ProUpRevTx`) and UTXO spends with block-level undo diffs:

1. Every node at height `H` computes the **exact bit-identical** Masternode set.
2. Chain reorganizations (`DisconnectBlock`) atomically revert Masternode state diffs.
3. Historical blocks are verified deterministically during IBD without `fBlockOfflineSync` bypasses.

## Specification

### 1. Special Transaction Encoding (`KTDIP3`)

Transactions carry deterministic Masternode payloads inside a standard `OP_RETURN` output prefixed with the 6-byte magic `KTDIP3`, followed by `uint8_t nType` and the serialized payload:

- **Type `1` — `ProRegTx` (`CProRegTx`, Register Masternode)**:
  - `nVersion` (`uint16_t = 2` required under `UPGRADE_HARDENING_V3` Height `1`; legacy `nVersion = 1` pre-V3 only), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`, 33-byte compressed pubkey enforced under V3), `blsPubKeyOperator` (`CBLSPubKey`, 48-byte BLS public key), **`blsPoP`** (`CBLSSignature`, 96-byte Proof-of-Possession over `blsPubKeyOperator` under `BLSDomain::PROTX_POP` DST included in `nVersion = 2` payload and `GetSignHash()`), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (`std::vector<unsigned char>`, compact ECDSA signature verified against `pubKeyOwner`, with strict DER/low-S enforced across ECDSA fallbacks). On registration, `nOperatorKeyHeight` is initialized alongside `nRegisteredHeight`.
  - Collateral must be an unspent `4,200 KRISTA` output with at least `15` confirmations; `ConnectBlock` prepares DIP3 validation (`fJustCheck=true`) strictly before `UpdateCoins`, commits atomically after `IsBlockValueValid()`, enforces that the collateral owner key matches `pubKeyOwner` (`bad-protx-collateral-owner`), and rejects same-block collateral spends (`bad-protx-collateral-spent-in-block`). Under `UPGRADE_HARDENING_V3`, `KTDIP3` payloads in coinbase, coinstake, or ZeroCoinPoW transactions are strictly rejected (`bad-protx-container`).
  - **Rule 8 (Multi-MN per IP)**: Multiple Masternodes MAY share the same IP address; uniqueness is enforced on `proTxHash`, `collateralOutpoint`, and `blsPubKeyOperator`.
- **Type `2` — `ProUpServTx` (`CProUpServTx`, Update Service / PoSe Revive)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `blsSig` (`CBLSSignature`, BLS signature by `blsPubKeyOperator` under `BLSDomain::PROTX_UPSERV` DST).
  - Rejects non-empty `scriptOperatorPayout` when `nOperatorReward == 0` under V3 (`bad-protx-op-payout-not-allowed`).
  - Clears `PoSeBanned` status when validly signed by the operator **after `nPoseReviveCooldownBlocks` blocks** (`120` blocks on Mainnet, `12` blocks on Testnet/Regtest; earlier attempts fail with `bad-protx-pose-cooldown`) and records `nPoSeRevivedHeight`. Under `UPGRADE_HARDENING_V4` (Height `1`), reviving resets KTDIP3 `PoM` staking uptime to start strictly from `std::max(nRegisteredHeight, nPoSeRevivedHeight)`.
- **Type `3` — `ProUpRegTx` (`CProUpRegTx`, Update Registrar)**:
  - `nVersion` (`uint16_t = 2` required under `UPGRADE_HARDENING_V3` Height `1`), `proTxHash`, `blsPubKeyOperator`, **`blsPoP`** (96-byte PoP over new `blsPubKeyOperator`), `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (ECDSA signature by `pubKeyOwner`). Atomically clears `scriptOperatorPayout` so a former operator cannot inherit payouts after key rotation.
  - **V4 Operator Key Rotation Cooldown (`nProUpRegCooldownBlocks = 100`)**: Under `UPGRADE_HARDENING_V4` (Height `1`), changing `blsPubKeyOperator` to a different operator key requires at least **`consensus.nProUpRegCooldownBlocks = 100` blocks** since the last operator key activation (`nOperatorKeyHeight >= 0 ? nOperatorKeyHeight : nRegisteredHeight`, rejected otherwise with `bad-proupreg-cooldown`) and updates `nOperatorKeyHeight = nHeight` upon rotation.
- **Type `4` — `ProUpRevTx` (`CProUpRevTx`, Revoke Operator)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `blsSig` (BLS signature by `blsPubKeyOperator` under `BLSDomain::PROTX_UPREV` DST). Atomically clears `scriptOperatorPayout` upon revocation.

### 2. Deterministic State, Hot/Cold BLS & Proof-of-Service (PoSe)

- **Collateral Spend Removal**: Whenever a block spends a registered `collateralOutpoint`, the corresponding Masternode is removed from the deterministic list at that block height.
- **PoSe Scoring & `M-4` PoSe Fairness**: Active Masternodes elected to ADAM committees (missed primary coordinator slots at `+5` penalty, plus under `UPGRADE_HARDENING_V4` `M-4`, silent primary ADAM committee members displaced by standby substitution at `+5` penalty) or fallback coordination that fail to submit valid solutions/signatures accrue PoSe penalties; LLMQ quorum signing grants `-5` PoSe recovery, while under `UPGRADE_HARDENING_V4` (Height `1`) LLMQ non-signing no longer accrues `+5` PoSe punishment (replacing pre-V4 `-5/+5` LLMQ omission penalties). Exceeding the ban threshold marks the Masternode `PoSeBanned` until revived via `ProUpServTx` after `nPoseReviveCooldownBlocks` (`120` Mainnet, `12` Testnet/Regtest). Under `UPGRADE_HARDENING_V3`, banned or revoked DIP3 Masternodes fail closed inside `VerifyAdamOrDip3Signature`.
- **V4 `PoM` Uptime Reset (`src/kernel.cpp`)**: Under `UPGRADE_HARDENING_V4` (Height `1`), KTDIP3 Proof of Masternode (`PoM`) staking uptime in `CalculateMPAWeight()` starts strictly from **`std::max(nRegisteredHeight, nPoSeRevivedHeight)`** (replacing pre-V4 `std::min(nRegisteredHeight, nCoinHeight)`).
- **ADAM & Dynamic `2/3` BFT LLMQ Integration**: `GetAdamMinerPool` reads active Masternodes directly from `CDeterministicMNManager::GetListForBlock(pindex)` (cutting off legacy ping-only MNs once `UPGRADE_DIP3` + `UPGRADE_HARDENING_V3` are active). Under `UPGRADE_HARDENING_V4` (Height `1`), **`ADAM_VRF_KEY_MATURITY = 100` blocks** is tracked via `(nOperatorKeyHeight >= 0 ? nOperatorKeyHeight : nRegisteredHeight)`, so `SelectAdamNodes` and `RankValidatorsByPoBLSDistance` prioritize matured keys ahead of newly registered or rotated keys. In `ElectQuorumMembers`, `UPGRADE_HARDENING_V3` intersects `pindexPrev` with the `seedHeight - 1` `evodb` pre-seed snapshot sorted by `proTxHash`, preventing post-seed grinding while preserving `<5` active-MN fallback; quorum size scales dynamically (`5 -> 9 -> 11 -> 15` via `GetTargetQuorumSize`), and under `UPGRADE_HARDENING_V4` `llmq::GetQuorumThreshold(N, h) = floor(2N/3) + 1` enforces `2/3` BFT supermajority (`4-of-5`, `7-of-9`, `8-of-11`, `11-of-15`; pre-V4 `3-of-5`..`8-of-15`) alongside `llmq::QUORUM_EQUIVOCATION_GUARD_SECS = 45` (pre-V4 `30s`).
- **KTDIP3 Hot/Cold BLS Separation (`UPGRADE_DIP3 = 1` Mainnet & Testnet, `UPGRADE_HARDENING_V3 = 1` & `UPGRADE_HARDENING_V4 = 1` Across All Networks)**:
  - When `UPGRADE_DIP3` is active, deterministic Masternodes sign coordinator VRF proofs, block headers, and ADAM solutions exclusively via their registered `blsPubKeyOperator` (`SignBLSOperatorOnly`, requiring strictly empty `ecdsaSig` under V3 without `PinBLSPubKey`), verified uniformly across domain-separated BLS DSTs by `VerifyAdamOrDip3Signature` and resolved via `GetMNByOperator`.
  - Cold/Hot separation works seamlessly with `blsPoP`: the Hot operator generates `(opPub, opPoP)` via `protx bls_generate` / `protx bls_pop` (`<opPubHex(96)>:<opPoPHex(192)>` or 288-hex) and hands those public values to the Cold owner wallet (`MasterNodeWizardDialog` / `protx register`) without ever sharing the operator BLS secret key or owner ECDSA private key.
  - Masternode payouts route through `AppendDip3OrDirectPayment`, paying `scriptPayout` and splitting `nOperatorReward` basis points directly to `scriptOperatorPayout` when configured.

### 3. RPC Interface

The `protx` RPC namespace provides:
- `protx bls_generate` — generate a fresh BLS12-381 operator `(secret, public, pop)` keypair
- `protx bls_pop <op_pubkey_or_secret>` — compute a 96-byte `PROTX_POP` signature from an operator BLS secret key or active Hot MN (`masterNodeBLS` redacted from GUI consoles)
- `protx register` (v2 `blsPoP` supported via optional arg or `<opPub>:<opPoP>` / 288-hex input)
- `protx update_service`
- `protx update_registrar` (v2 `blsPoP`, enforcing `nProUpRegCooldownBlocks = 100` on operator key changes under V4)
- `protx revoke`
- `protx list`
- `protx info`
- `protx wallet_list` — list Cold Controller aliases, 15-confirmation drafts, and owned on-chain Masternodes stored in `wallet.dat` (`mnmeta`)
- `protx set_alias <outpoint_or_protxhash> <alias>` — assign or rename a local Cold Controller alias inside `wallet.dat`
- `protx remove_meta <outpoint_or_protxhash_or_alias> (unlock_collateral)` — remove local metadata/draft from `wallet.dat` (refuses to unlock active on-chain collaterals)

Additionally, `listmasternodes` and `getmasternodecount` include KTDIP3 deterministic Masternodes when `UPGRADE_DIP3` is active, and `getmasternodestatus` appends `" (KTDIP3)"` to active deterministic entries. Legacy `masternode.conf` management (`startmasternode`, `listmasternodeconf`, `createmasternodebroadcast`, `-mnconf`) is retired and automatically migrated once to `wallet.dat` (`mnmeta`), archiving `masternode.conf` to `masternode.conf.migrated` (`chmod 0600`) with plaintext WIF secret keys redacted (`***REDACTED***`, refusing symlinks and non-regular files).

### 4. Cold Wallet GUI & Mempool Hardening

- **Navigation Placement & 1-Click In-Wallet Registration (`wallet.dat` Persistence)**: By default, `MasterNodesWidget` is grouped under **`MORE ▾ -> Masternodes`** (`Masternodes (4,200 KRISTA)`) to keep the top navigation bar uncluttered, with an immediate **`Pin advanced tabs to top bar`** toggle in **Settings -> Display & Units** to pin `MASTERNODES` directly on the top bar. In Step 3 of `MasterNodeWizardDialog`, the operator can leave **Operator BLS Public Key (optional)** blank (generating an encrypted operator ECDSA+BLS keypair inside `wallet.dat` and outputting `<alias> <privkey> <bls_privkey_hex>`) or supply a dedicated 48-byte hex `blsPubKeyOperator` from an external/cold-separated Hot Node (outputting `operatorPubKey=<hex>`). Drafts and local aliases persist in `wallet.dat` (`mnmeta`), so no `masternode.conf` file is required.
- **Collateral Progress Badge (`"Confirming (X/15)"`) & Direct UTXO Consolidation**: `MNRow` / `MNModel` displays `"Confirming (X/15)"` until the `4,200 KRISTA` collateral reaches `15` confirmations; once mature, clicking **Start** / **Start All** / **Start Inactive** in `MasterNodesWidget` (or completing the wizard with an existing mature collateral) directly builds, signs, and broadcasts the `ProRegTx` via `RegisterDip3MasternodeFromWallet` without requiring the RPC Debug Console or CLI. If fragmented wallet UTXOs cause an oversized transaction during Send or Masternode operations, `src/qt/kristatech/guitransactionsutils.cpp` offers direct in-dialog **Consolidate Fragmented UTXOs** (auto-continuing immediately when `<= 90 KB` with `-spendzeroconfchange=1`, or guiding the user to wait 1 confirmation when larger).
- **Mempool & Block Template Conflict Protection**: `AcceptToMemoryPool` rejects duplicate/conflicting `ProRegTx` transactions sharing the same `collateralOutpoint` or `blsPubKeyOperator` (`bad-protx-mempool-conflict`), and `CreateNewBlock` deduplicates pending `ProRegTx` / `ProUp*` payloads within a single block template.
- **Startup Collateral Auto-Lock & Live Status**: `AppInit2` locks all wallet-owned on-chain KTDIP3 collateral outpoints at startup, `WalletModel::getMNCollateralCandidate` skips existing DIP3 registrations, `MNModel` displays `PRE_ENABLED` while a `ProRegTx` is pending in the mempool, and `TopBar` / `StatsWidget` / `SettingsConsoleWidget` / `ClientModel` reflect KTDIP3 counts directly.
