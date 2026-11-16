```
KTIP: 0013
Title: Trustless Shared Masternodes via Taproot, PSBT & Multi-Payee Consensus
Author: KristaTech Core Developers
Status: Draft
Type: Standard Track (Consensus / Masternode / Wallet)
Created: 2026-11-17
Requires: KTIP-0005, KTIP-0006, KTIP-0008, KTIP-0012
```

## Abstract
This proposal defines a **non-custodial (trustless) Shared Masternode** protocol for KristaTech (`KRISTA`). By combining KristaTech's existing **Taproot (`kt1p...` / BIP341)**, **Schnorr (`BIP340`)**, **BIP68/CSV relative timelocks**, **BIP174 PSBT**, and **protocol-level Multi-Payee coinbase splitting**, up to `21` participants can co-fund a `4,200 KRISTA` Masternode collateral without trusting any single operator with custody of the principal or reward distribution.

## Motivation
Under the current Masternode rules (`src/masternode.cpp`), collateral validation enforces two strict single-party assumptions:
1. **P2PKH-Only Collateral (`L260`, `L640`)**: The `4,200 KRISTA` collateral UTXO must pay to a single `CKeyID` (`GetScriptForDestination(pubKeyCollateralAddress.GetID())`). Taproot (`WitnessV1Taproot`) and P2SH multisig outputs are rejected.
2. **Single-Payee Coinbase (`L281`, `L329`)**: All Masternode rewards (`35%` Passive Pool, `20%` Validator Track, `10%` LLMQ Quorum under Model D) are paid to that single collateral address, forcing shared pools to rely on custodial operators who hold the private key and manually distribute payouts.

KTIP-0013 eliminates both custodial bottlenecks at the consensus layer.

## Specification

### 1. Taproot Collateral Support (`UPGRADE_SHARED_MN`)
Upon activation of `UPGRADE_SHARED_MN` at height `H_shared`:
* **Accepted Collateral Scripts**: `CMasternodeBroadcast` (`mnb` v2) accepts `4,200 KRISTA` (`MASTERNODE_COLLATERAL * COIN`) UTXOs locked to either:
  1. Legacy `P2PKH` (`OP_DUP OP_HASH160 <20B> OP_EQUALVERIFY OP_CHECKSIG`), or
  2. **Taproot (`WitnessV1Taproot`, `OP_1 <32B x-only pubkey>`)** (`kt1p...`).
* **Recommended Taproot Construction**:
  * **Key-Path (Cooperative Exit / Rebalance)**: A `BIP340` / `MuSig2` aggregated `N-of-N` x-only public key of all pool participants. Any cooperative spend requires all co-owners' PSBT partial signatures.
  * **Script-Path (Trustless Recovery / Operator Timeout)**: A `BIP342 Tapscript` leaf combining `BIP68` relative timelock (`OP_CHECKSEQUENCEVERIFY`, e.g., `43,200` blocks ≈ 30 days) with an `M-of-N` (`OP_CHECKSIGADD`) threshold or pre-committed refund outputs, guaranteeing participants can recover principal if the operator or a minority co-owner disappears.

### 2. Broadcast Authorization Without Single-Key Custody
In `mnb` v2 (`protocolVersion >= SHARED_MN_PROTO_VERSION`), when the collateral is `WitnessV1Taproot`:
* **Delegation Proof**: The collateral output key (`CXOnlyPubKey`) signs the `CMasternodeBroadcast` payload (including `vin`, `addr`, `pubKeyMasternode`, and the immutable `vPayees` table) using a **BIP340 Schnorr signature** (either via cooperative MuSig2 key-path signing or via a dedicated `OP_RETURN` commitment in the funding transaction).
* **Hot Operator Isolation**: Once authorized, ongoing Masternode liveness (`SendOnChainPing`), ADAM puzzle solving, and LLMQ/PoBLS quorum signing continue to use the operator's `pubKeyMasternode` hot key, which has **zero spending authority** over the `4,200 KRISTA` Taproot collateral.

### 3. Protocol-Level Multi-Payee Coinbase Splitting
`CMasternodeBroadcast` and `CMasternode` add an optional, signature-protected payout table:
```cpp
struct CMasternodePayeeShare {
    CScript scriptPubKey; // P2PKH, P2SH, or WitnessV1Taproot (kt1p...)
    uint16_t nShareBps;   // Basis points (1..10000; 100 bps = 1.00%)
};
```
**Consensus Validation Rules**:
1. **Bounded Fan-out**: `1 <= vPayees.size() <= MAX_SHARED_MN_PAYEES` (`21`).
2. **Exact Sum**: $\sum_i \text{nShareBps}_i = 10{,}000$ (`100.00%`).
3. **Anti-Dust Minimum**: Each entry must satisfy `nShareBps >= 100` (`>= 1.00%`, i.e., `>= 42 KRISTA` equivalent stake).
4. **Standard Script Check**: Every `scriptPubKey` must be a standard `P2PKH`, `P2SH`, or `WitnessV1Taproot` destination (`<= 34` bytes).
5. **Immutability**: `vPayees` is covered by the `mnb` collateral signature and cannot be altered by the hot operator. Updating shares requires spending/re-creating the collateral or broadcasting a new `mnb` signed by the collateral owners.

**Coinbase Construction (`FillBlockPayee` / `IsBlockPayeeValid`)**:
* Given the total Masternode reward $R_{\text{mn}}$ (Passive Pool, Validator Track, or LLMQ share), each payee $i$ receives:
  $$R_i = \left\lfloor \frac{R_{\text{mn}} \times \text{nShareBps}_i}{10{,}000} \right\rfloor$$
* Any integer division remainder $\left(R_{\text{mn}} - \sum_i R_i\right)$ (at most `20` uKRISTA) is added to `vPayees[0]` so total coinbase issuance matches consensus to the exact uKRISTA.
* Optionally, a share may target a **MESCAL** (`kt1p...`) covenant address for programmable vesting or DAO treasury routing.

### 4. Security & DoS Considerations
* **Coinbase Size & SigOp Bounds**: Capping `MAX_SHARED_MN_PAYEES = 21` bounds coinbase bloat to ~`750` bytes per MN payout and introduces zero additional input sigops.
* **No Reward Hijack**: Because `vPayees` is signed by the Taproot collateral owners (`MuSig2`), a malicious VPS operator cannot redirect rewards to their own address.
* **Liveness Accountability**: If the VPS operator goes offline, the `2,880`-block activity window / PoSe checks mark the MN expired, stopping rewards without risking the `4,200 KRISTA` principal.

### 5. Wallet & PSBT Workflow
1. **Pool Creation (`createsharedmnpool`)**: Defines `N` participant pubkeys/payout scripts, optional operator fee share (`<= 1,500` bps recommended), and CSV recovery delay.
2. **Co-Funding via PSBT (BIP174)**: Each participant adds their KRISTA input(s) and signs via `walletprocesspsbt` / `signpsbt`; the combined funding TX creates the `4,200 KRISTA` `kt1p...` Taproot output.
3. **Activation**: After `15` confirmations, participants co-sign the `mnb` v2 delegation message (or commit it during funding), and the hot node starts automatically via `activemasternode.conf`.

## Backward Compatibility
* Requires a coordinated hard fork (`UPGRADE_SHARED_MN` activation height).
* Legacy single-owner `P2PKH` Masternodes remain 100% valid with `vPayees.empty()` (defaulting to `10,000` bps to `pubKeyCollateralAddress`).
