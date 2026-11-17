# KTIP-0014: Deterministic Masternodes (KRISTA-DIP3)

- **KTIP**: 0014
- **Title**: Deterministic On-Chain Masternodes (KRISTA-DIP3)
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

- **Type `1` — `ProRegTx` (Register Masternode)**:
  - `nVersion` (`uint16_t = 1`), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`), `vchOperatorPubKey` (48-byte BLS public key), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (ECDSA signature by `pubKeyOwner`).
  - Collateral must be an unspent `4,200 KRISTA` output with at least `15` confirmations.
  - **Rule 8 (Multi-MN per IP)**: Multiple Masternodes MAY share the same IP address; uniqueness is enforced on `proTxHash`, `collateralOutpoint`, and `vchOperatorPubKey`.
- **Type `2` — `ProUpServTx` (Update Service / PoSe Revive)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `vchSig` (BLS signature by `vchOperatorPubKey`).
  - Clears `PoSeBanned` status when validly signed by the operator.
- **Type `3` — `ProUpRegTx` (Update Registrar)**:
  - `nVersion`, `proTxHash`, `vchOperatorPubKey`, `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (ECDSA signature by `pubKeyOwner`).
- **Type `4` — `ProUpRevTx` (Revoke Operator)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `vchSig` (BLS signature by `vchOperatorPubKey`).

### 2. Deterministic State & Proof-of-Service (PoSe)

- **Collateral Spend Removal**: Whenever a block spends a registered `collateralOutpoint`, the corresponding Masternode is removed from the deterministic list at that block height.
- **PoSe Scoring**: Active Masternodes elected to ADAM committees or PoBLS coordination that fail to submit valid solutions/signatures accrue PoSe penalties; exceeding the ban threshold marks the Masternode `PoSeBanned` until revived via `ProUpServTx`.
- **ADAM Integration**: `GetAdamMinerPool` reads active Masternodes directly from `CDeterministicMNManager::GetListForBlock(pindex)` when `UPGRADE_DIP3` is active.

### 3. RPC Interface

The `protx` RPC namespace provides:
- `protx register`
- `protx update_service`
- `protx update_registrar`
- `protx revoke`
- `protx list`
- `protx info`

Additionally, `listmasternodes` and `getmasternodecount` include KTDIP3 deterministic Masternodes when `UPGRADE_DIP3` is active.

### 4. Cold Wallet GUI & Mempool Hardening

- **1-Click In-Wallet Registration**: `MasterNodeWizardDialog` derives the operator BLS key deterministically via `DeriveBLSFromCKey(mnKey)` (matching `AppInitActiveMasternode` on hot nodes) and outputs `<alias> <privkey> <bls_privkey_hex>` for `activemasternode.conf`. When the collateral has `>= 15` confirmations, clicking **Start** / **Start All** / **Start Inactive/s** in `MasterNodesWidget` (or completing the wizard with an existing mature collateral) directly builds, signs, and broadcasts the `ProRegTx` via `RegisterDip3MasternodeFromWallet` without requiring the Debug Console or CLI.
- **Mempool & Block Template Conflict Protection**: `AcceptToMemoryPool` rejects duplicate/conflicting `ProRegTx` transactions sharing the same `collateralOutpoint` or `blsPubKeyOperator` (`bad-protx-mempool-conflict`), and `CreateNewBlock` deduplicates pending `ProRegTx` / `ProUp*` payloads within a single block template.
- **Startup Collateral Auto-Lock & Live Status**: `AppInit2` locks all wallet-owned on-chain KTDIP3 collateral outpoints at startup, `WalletModel::getMNCollateralCandidate` skips existing DIP3 registrations, `MNModel` displays `PRE_ENABLED` while a `ProRegTx` is pending in the mempool, and `TopBar` / `StatsWidget` / `SettingsConsoleWidget` / `ClientModel` reflect KTDIP3 counts directly.
