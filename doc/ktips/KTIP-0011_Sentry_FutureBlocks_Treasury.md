```
KTIP: 0011
Title: Sentry Node DDoS Shielding, Bounded Future Blocks Queue & LLMQ Governance Treasury
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Network / Governance)
Created: 2026-11-09
```

## Abstract
This proposal specifies three infrastructure and governance hardening mechanisms in KRISTA Core: (1) **Sentry Node Relay Architecture** for isolating Masternodes and Validators behind high-availability DDoS-shielded relay nodes, (2) **Structurally Pre-Verified Bounded Future Blocks Queue (`mapFutureBlocks`, `SEC-04`)** for handling slight clock drift without memory exhaustion, and (3) **LLMQ 3-of-5 Threshold Governance (`SEC-09`)** for on-chain Developer Fund (`Params().DeveloperFundAddress()`) expenditures.

## Motivation
1. **Validator IP Protection**: Active Masternodes and Validators hold consensus signing keys (`activemasternode.conf`). Exposing their public IP addresses directly to untrusted peers makes them vulnerable to volumetric DDoS attacks.
2. **Future-Block Queue Stuffing DoS (`SEC-04`)**: Nodes must tolerate minor NTP clock drift by queuing blocks whose timestamp is slightly ahead of local adjusted time (`time-too-new`). Without structural header pre-verification, a `+300s` drift ceiling, and bounded eviction, an attacker could fill `mapFutureBlocks` with fake far-future blocks and prevent legitimate blocks from being queued.
3. **Trustless Developer Treasury Spending (`SEC-09`)**: The 7.0% Developer Treasury accumulates protocol development funds on-chain. Spending from `Params().DeveloperFundAddress()` must require both Spork ECDSA authorization and cryptographic 3-of-5 threshold authorization from the active LLMQ quorum rather than a single unilateral key, including across multi-input transactions.

## Specification

### 1. Sentry Node Relay Architecture
Sentry Nodes are public-facing KRISTA Core relay daemons configured via standard P2P and isolation parameters (`listen=1`, `maxconnections=256`, `masternode=0`, `staking=0`, `gen=0`, `disablewallet=1`; see `doc/Sentry_Node_Setup.md`) while private validator nodes bind exclusively to their Sentry peers (`listen=0`, `connect=<sentry_ip>:27999`):
* **High Inbound Connection Capacity**: Raising `maxconnections=256` allows each Sentry Node to act as a high-capacity public P2P relay hub.
* **No Wallet / No Signing Isolation**: Sentry nodes operate without staking, mining, or Masternode private keys and relay `CMPCTBLOCK`, `ADAMSOL`, `PROPOSEBLOCK`, `QUORUMSIGSHARE`, `POSCOORDREQ`, and `POSCOORDSIG` messages between public peers and private validator nodes.

---

### 2. Bounded & Pre-Verified Future Blocks Queue (`mapFutureBlocks` — `SEC-04`)
When `ProcessMessage` (`"headers"` or `"block"` in `src/main.cpp`) encounters a header or block failing `CheckBlockHeader` with `"time-too-new"` whose timestamp is within `GetAdjustedTime() + 300` seconds (enforced on both `HEADERS` and `BLOCK`):
1. **Structural Pre-Verification (`IsFutureBlockPreVerified`)**: Before inserting into `mapFutureBlocks`, the node derives `nCandidateHeight` from `mapBlockIndex[hashPrevBlock]` (or coinbase BIP34 `scriptSig` on `BLOCK`) and enforces:
   - For `header.nVersion >= 11`: non-empty and bounded byte sizes (`1..512` bytes) on both `vAdamCoordinatorSig` and `vAdamVRFProof`.
   - For `header.nVersion >= 12`: non-empty and bounded byte size (`1..4096` bytes) on `vQuorumSig` when `IsModelDActive(nCandidateHeight)` (or `<= 4096` bytes pre-Model D).
2. **Bounded Eviction (`64` Entries)**: `mapFutureBlocks` is capped at `64` entries. If `mapFutureBlocks.size() >= 64`, the entry with the furthest-future timestamp is evicted if the incoming header/block has an earlier timestamp; otherwise the incoming entry is dropped (on `BLOCK` insertion, `pfrom->AddInventoryKnown(inv)` is recorded to prevent duplicate pushes).
3. **Periodic Processing & Full Consensus Verification (`ProcessFutureBlocks`)**: The scheduler prunes parentless entries older than `3,600` seconds (`GetAdjustedTime() - block.nTime > 3600`) and passes matured entries (`block.nTime <= pindexPrev->MaxFutureBlockTime()`) through full cryptographic consensus validation (`ProcessNewBlockHeaders` / `ProcessNewBlock`).

---

### 3. LLMQ 3-of-5 Threshold Developer Treasury Governance (`SEC-09`)
Starting at `consensus.nTreasuryGovernanceStartHeight` (`2,880` on `CMainParams` / Mainnet, `200` on `CTestNetParams` / Testnet, `100` on Regtest; spends below this height fail with `bad-txns-spend-devfund-early`), any transaction spending one or more UTXOs from `Params().DeveloperFundAddress()` (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP` on Mainnet, `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` on Testnet) is validated by `CheckInputs` and `ConnectBlock` (`src/main.cpp`) as follows:

1. **Multi-Input `txTmp.GetHash()` Normalization (`SEC-09`)**:
   - Each input spending from `Params().DeveloperFundAddress()` carries a `scriptSig` with three stack pushes: `<ecdsaSig> <pubKey> <qsig>` (fewer than 3 pushes fails with `bad-txns-missing-quorum-sig`; invalid serialization fails with `bad-txns-malformed-quorum-sig`).
   - To compute the canonical transaction hash (`txHash`) that the LLMQ quorum signed, the verifier constructs a normalized copy `txTmp` in which the 3rd push (`<qsig>`) is stripped from **every** input spending `Params().DeveloperFundAddress()`, leaving only `<ecdsaSig> <pubKey>`, and computes `txHash = txTmp.GetHash()`.
2. **LLMQ 3-of-5 Quorum Signature & Spork Key Verification**:
   - The `<qsig>` byte vector is deserialized into `CQuorumSignature` with `qsig.blockHash = txTmp.GetHash()`.
   - `qsig.Verify(quorum)` verifies that at least **3 out of 5** active LLMQ members at the spend height signed `txHash` using their BLS12-381 keys (`CBLSSignature::Aggregate` pairing check); failure rejects the transaction with `bad-txns-invalid-quorum-sig` (`Misbehaving(100)`).
   - Finally, `<ecdsaSig> <pubKey>` (`scriptSigOverride`) is verified by `CScriptCheck` against `Params().GetConsensus().strSporkPubKey`.

## Backward Compatibility
* Sentry Node deployment uses standard KRISTA Core configuration flags compatible with all P2P peers.
* `SEC-04` and `SEC-09` are active across Mainnet, Testnet, and Regtest in `v1.4.5.9`.

## Reference Implementation
* Sentry Node operational profile: `doc/Sentry_Node_Setup.md`, `src/init.cpp`, and `src/net.cpp`.
* Future Blocks Queue (`IsFutureBlockPreVerified`, `mapFutureBlocks`, `ProcessFutureBlocks`): `src/main.cpp`.
* Developer Treasury LLMQ validation & multi-input normalization: `CheckInputs` and `ConnectBlock` in `src/main.cpp`, and `CQuorumSignature::Verify` in `src/llmq.cpp`.
