# KTIP-0014: Deterministic Masternodes (KRISTA-DIP3 & v2 Hot/Cold BLS)

- **KTIP**: 0014
- **Title**: Deterministic On-Chain Masternodes (KRISTA-DIP3 & v2 Hot/Cold BLS)
- **Author**: KristaTech Core Developers
- **Status**: Active / Implemented
- **Type**: Consensus & Protocol Upgrade (`UPGRADE_DIP3` & `nDip3V2Height`)
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
  - `nVersion` (`uint16_t = 1`), `collateralOutpoint` (`COutPoint`), `addr` (`CService`), `pubKeyOwner` (`CPubKey`), `blsPubKeyOperator` (`CBLSPubKey`, 48-byte BLS public key), `keyIDVoting` (`CKeyID`), `nOperatorReward` (`uint16_t`, `0..10000` bps), `scriptPayout` (`CScript`), `inputsHash` (`uint256`), `vchSig` (`std::vector<unsigned char>`, compact ECDSA signature by `pubKeyOwner`).
  - Collateral must be an unspent `4,200 KRISTA` output with at least `15` confirmations; from `nDip3V2Height`, `ConnectBlock` enforces that the collateral owner key matches `pubKeyOwner` (`bad-protx-collateral-owner`) and rejects same-block collateral spends (`bad-protx-collateral-spent-in-block`).
  - **Rule 8 (Multi-MN per IP)**: Multiple Masternodes MAY share the same IP address; uniqueness is enforced on `proTxHash`, `collateralOutpoint`, and `blsPubKeyOperator`.
- **Type `2` — `ProUpServTx` (`CProUpServTx`, Update Service / PoSe Revive)**:
  - `nVersion`, `proTxHash`, `addr`, `scriptOperatorPayout`, `inputsHash`, `blsSig` (`CBLSSignature`, BLS signature by `blsPubKeyOperator`).
  - Clears `PoSeBanned` status when validly signed by the operator.
- **Type `3` — `ProUpRegTx` (`CProUpRegTx`, Update Registrar)**:
  - `nVersion`, `proTxHash`, `blsPubKeyOperator`, `keyIDVoting`, `scriptPayout`, `inputsHash`, `vchSig` (ECDSA signature by `pubKeyOwner`).
- **Type `4` — `ProUpRevTx` (`CProUpRevTx`, Revoke Operator)**:
  - `nVersion`, `proTxHash`, `nReason`, `inputsHash`, `blsSig` (BLS signature by `blsPubKeyOperator`).

### 2. Deterministic State, KTDIP3 v2 Hot/Cold BLS & Proof-of-Service (PoSe)

- **Collateral Spend Removal**: Whenever a block spends a registered `collateralOutpoint`, the corresponding Masternode is removed from the deterministic list at that block height.
- **PoSe Scoring**: Active Masternodes elected to ADAM committees or PoBLS coordination that fail to submit valid solutions/signatures accrue PoSe penalties; exceeding the ban threshold marks the Masternode `PoSeBanned` until revived via `ProUpServTx`.
- **ADAM & Dynamic BFT LLMQ Integration**: `GetAdamMinerPool` reads active Masternodes directly from `CDeterministicMNManager::GetListForBlock(pindex)` when `UPGRADE_DIP3` is active, and LLMQ membership/verification scales dynamically (`5 -> 9 -> 11 -> 15` via `GetTargetQuorumSize`).
- **KTDIP3 v2 Hot/Cold BLS Separation (`nDip3V2Height = 1` Mainnet, Testnet & Regtest)**:
  - Starting at `nDip3V2Height`, deterministic Masternodes sign coordinator VRF proofs, block headers, and ADAM solutions exclusively via their registered `blsPubKeyOperator` (`SignBLSOperatorOnly`, leaving `ecdsaSig` empty without `PinBLSPubKey`), verified uniformly by `VerifyAdamOrDip3Signature` and resolved via `GetMNByOperator`.
  - Masternode payouts route through `AppendDip3OrDirectPayment`, paying `scriptPayout` and splitting `nOperatorReward` basis points directly to `scriptOperatorPayout` when configured.

### 3. RPC Interface

The `protx` RPC namespace provides:
- `protx bls_generate` — generate a fresh BLS12-381 operator `(secret, public)` keypair
- `protx register`
- `protx update_service`
- `protx update_registrar`
- `protx revoke`
- `protx list`
- `protx info`

Additionally, `listmasternodes` and `getmasternodecount` include KTDIP3 deterministic Masternodes when `UPGRADE_DIP3` is active, and `getmasternodestatus` appends `" (KTDIP3)"` to active deterministic entries.

### 4. Cold Wallet GUI & Mempool Hardening

- **1-Click In-Wallet Registration**: In Step 3 of `MasterNodeWizardDialog`, the operator can leave **Operator BLS Public Key (optional)** blank (deriving `DeriveBLSFromCKey(mnKey)` and outputting `<alias> <privkey> <bls_privkey_hex>`) or supply a dedicated 48-byte hex `blsPubKeyOperator` from an external/cold-separated Hot Node (outputting `operatorPubKey=<hex>`). When the collateral has `>= 15` confirmations, clicking **Start** / **Start All** / **Start Inactive/s** in `MasterNodesWidget` (or completing the wizard with an existing mature collateral) directly builds, signs, and broadcasts the `ProRegTx` via `RegisterDip3MasternodeFromWallet` without requiring the RPC Debug Console or CLI.
- **Mempool & Block Template Conflict Protection**: `AcceptToMemoryPool` rejects duplicate/conflicting `ProRegTx` transactions sharing the same `collateralOutpoint` or `blsPubKeyOperator` (`bad-protx-mempool-conflict`), and `CreateNewBlock` deduplicates pending `ProRegTx` / `ProUp*` payloads within a single block template.
- **Startup Collateral Auto-Lock & Live Status**: `AppInit2` locks all wallet-owned on-chain KTDIP3 collateral outpoints at startup, `WalletModel::getMNCollateralCandidate` skips existing DIP3 registrations, `MNModel` displays `PRE_ENABLED` while a `ProRegTx` is pending in the mempool, and `TopBar` / `StatsWidget` / `SettingsConsoleWidget` / `ClientModel` reflect KTDIP3 counts directly.
