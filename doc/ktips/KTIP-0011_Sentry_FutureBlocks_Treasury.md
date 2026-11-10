```
KTIP: 0011
Title: Sentry Node DDoS Shielding, Bounded Future Blocks Queue & LLMQ Governance Treasury
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Network / Governance)
Created: 2026-11-09
```

## Abstract
This proposal specifies three infrastructure and governance hardening mechanisms in KRISTA Core: (1) **Sentry Node Architecture (`-sentrynode=1`)** for isolating Masternodes and Validators behind high-availability DDoS-shielded relay nodes, (2) **Cryptographically Pre-Verified Bounded Future Blocks Queue (`mapFutureBlocks`, `SEC-04`)** for handling slight clock drift without memory exhaustion, and (3) **LLMQ 3-of-5 Threshold Governance (`SEC-09`)** for on-chain Developer Fund (`DeveloperFundAddress()`) expenditures.

## Motivation
1. **Validator IP Protection**: Active Masternodes and Validators hold consensus signing keys (`activemasternode.conf`). Exposing their public IP addresses directly to untrusted peers makes them vulnerable to volumetric DDoS attacks.
2. **Future-Block Queue Stuffing DoS (`SEC-04`)**: Nodes must tolerate minor NTP clock drift by queuing blocks whose timestamp is slightly ahead of local adjusted time (`time-too-new`). Without cryptographic pre-verification and bounded eviction, an attacker could fill `mapFutureBlocks` with fake far-future blocks and prevent legitimate blocks from being queued.
3. **Trustless Developer Treasury Spending (`SEC-09`)**: The 7.0% Developer Treasury accumulates protocol development funds on-chain. Spending from `DeveloperFundAddress()` must require cryptographic authorization from the active LLMQ quorum rather than a single unilateral key, including across multi-input transactions.

## Specification

### 1. Sentry Node Mode (`-sentrynode=1`)
When started with `-sentrynode=1` (`fSentryNode` in `src/init.cpp` and `src/net.cpp`):
* **High Inbound Connection Capacity**: Default `-maxconnections` is automatically raised to `256` (unless overridden) to act as a high-capacity P2P relay hub.
* **No Wallet / No Signing Isolation**: Sentry nodes operate without staking, mining, or Masternode private keys and relay `CMPCTBLOCK`, `ADAMSOL`, `PROPOSEBLOCK`, and `QUORUMSIGSHARE` messages between public peers and private validator nodes connected via `-addnode` / `-connect`.

---

### 2. Bounded & Pre-Verified Future Blocks Queue (`mapFutureBlocks` — `SEC-04`)
When `ProcessMessage` (`"headers"` or `"block"` in `src/main.cpp`) encounters a header or block failing `CheckBlockHeader` with `"time-too-new"` whose timestamp is within `GetAdjustedTime() + 300` seconds (enforced identically on both `HEADERS` and `BLOCK`, with `pfrom->AddInventoryKnown` deferred until `ProcessFutureBlocks`):
1. **Cryptographic Pre-Verification (`IsFutureBlockPreVerified`)**: Before inserting into `mapFutureBlocks`, the node enforces:
   - Known `pindexPrev` in `mapBlockIndex` and `block.nTime > pindexPrev->GetMedianTimePast()`.
   - For `block.nVersion >= 11`: `VerifySelfContainedBLSSignedData` signature verification on both `vAdamVRFProof` (over `GetAdamSeed(pindexPrev)`) and `vAdamCoordinatorSig` (over `block.GetHash()`), matching `coordPubKey` and `vrfBlsPk == coordBlsPk`, and membership of `coordPubKey` in `GetAdamMinerPool(nHeight - 1, pcoinsTip)` when `pindexPrev == chainActive.Tip()`.
   - For `block.nVersion >= 12` (when `UPGRADE_MODELD` is active): `CQuorumSignature::Verify` against `llmq::GetActiveQuorum(nHeight, pindexPrev)` when `pindexPrev == chainActive.Tip()` (or bounded deserializable `CQuorumSignature` matching `blockHash` on side branches).
2. **Bounded Eviction (`MAX_FUTURE_BLOCKS = 64`)**: If `mapFutureBlocks.size() >= 64`, the entry with the furthest-future timestamp (`max_element`) is evicted if the incoming block has an earlier timestamp; otherwise the incoming block is dropped.
3. **Periodic Processing**: The scheduler (`ProcessFutureBlocks`) re-evaluates queued blocks once `GetAdjustedTime()` reaches their block timestamp.

---

### 3. LLMQ 3-of-5 Threshold Developer Treasury Governance (`SEC-09`)
Starting at `consensus.nTreasuryGovernanceStartHeight` (`2,880` on `CMainParams` / Mainnet, `200` on `CTestNetParams` / Testnet, `100` on Regtest), any transaction spending one or more UTXOs from `Params().GetConsensus().DeveloperFundAddress()` (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP` on Mainnet, `ktEooARF3HdV8w59LUc5jaHrS7GBL7974Uy` on Testnet) is validated by `CheckInputs` and `ConnectBlock` (`src/main.cpp`) as follows:

1. **Multi-Input `txTmp.GetHash()` Normalization (`SEC-09`)**:
   - Each input spending from `DeveloperFundAddress()` carries a `scriptSig` with three stack pushes: `<ecdsaSig> <pubKey> <qsig>`.
   - To compute the canonical transaction hash (`txHash`) that the LLMQ quorum signed, the verifier constructs a normalized copy `txTmp` in which the 3rd push (`<qsig>`) is stripped from **every** input spending `DeveloperFundAddress()`, leaving only `<ecdsaSig> <pubKey>`, and computes `txHash = txTmp.GetHash()`.
2. **LLMQ 3-of-5 Quorum Signature Verification**:
   - The `<qsig>` byte vector is deserialized into `CQuorumSignature` (`qsig.blockHash == txHash`).
   - `qsig.Verify(quorum)` verifies that at least **3 out of 5** active LLMQ members at the current block height signed `txHash` using their deterministic BLS12-381 keys (`CBLSSignature::Aggregate` pairing check).
   - If `qsig.Verify(quorum)` fails on any treasury input, the transaction is rejected with `bad-dev-fund-quorum-sig` (`Misbehaving(100)`).

## Backward Compatibility
* Sentry Node mode (`-sentrynode=1`) is an opt-in daemon configuration flag compatible with all P2P peers.
* `SEC-04` and `SEC-09` are active across Mainnet, Testnet, and Regtest in `v1.4.5.9`.

## Reference Implementation
* Sentry Node configuration: `src/init.cpp` and `src/net.cpp`.
* Future Blocks Queue (`mapFutureBlocks`): `src/main.cpp`.
* Developer Treasury LLMQ validation & multi-input normalization: `CheckInputs` and `ConnectBlock` in `src/main.cpp`, and `CQuorumSignature::Verify` in `src/llmq.cpp`.
