```
KTIP: 0008
Title: MESCAL Declarative Smart Contracts & Taproot (P2TR / BIP-350 Bech32m) Integration
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus / Script)
Created: 2026-07-10
```

## Abstract
This proposal specifies **MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)**, a declarative JSON-based smart contract language that compiles deterministically into stateless, loop-free stack-based `CScript` bytecode and Taproot (`P2TR`, Witness v1, BIP-350 Bech32m) script-path spending leaves. To eliminate global state corruption and unauthenticated storage vulnerabilities, legacy stateful opcodes (`OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN`, `OP_UPDATE_STATUS`) are permanently disabled at the script interpreter layer (`SCRIPT_ERR_DISABLED_OPCODE`).

## Motivation
Traditional virtual-machine smart contract platforms introduce unbounded execution loops, re-entrancy vectors, complex gas metering, and mutable global storage trees that cannot be safely rolled back during chain reorganizations without heavy state snapshots.

MESCAL solves these problems through four architectural guarantees:
1. **Loop-Free Linear Termination ($O(k)$)**: MESCAL grammars disallow backward jumps and loops; the control flow graph is a strict Directed Acyclic Graph (DAG) whose worst-case execution cost is bounded linearly by the number of declared actions $k$.
2. **Gasless UTXO Determinism**: Contract state transitions are encoded directly into UTXO locking scripts (`P2SH` or `P2TR`) validated by standard transaction fees rather than runtime gas meters.
3. **Taproot Script-Path Privacy (`P2TR` / BIP-350 Bech32m)**: Compiling MESCAL contracts into Taproot script trees reveals only the executed branch (`leafScript` + 33-byte `controlBlock`) on-chain while keeping unused branches private.
4. **Hard Disabling of Legacy Stateful Opcodes (`SEC-10`)**: Legacy DECENOMY/SWMP opcodes that wrote directly to `pScriptDB` without authentication or reorg undo logs are permanently disabled.

## Specification

### 1. JSON Structure & Declarative Roles
A MESCAL contract document consists of four top-level keys: `basic`, `condition`, `contract`, and `active_contract`.

Supported `basic` roles compiled by `CompileMESCAL` (`src/script/mescal.cpp`):
* **`hash160`**: `<input> OP_HASH160`
* **`check-signature` / `check-signature-verification`**: `<pubkey> OP_CHECKSIG` / `<pubkey> OP_CHECKSIGVERIFY`
* **`equalverify-checksig`**: `OP_DUP OP_HASH160 <pubkeyhash> OP_EQUALVERIFY OP_CHECKSIG`
* **`multi-signature`**: `<m> <pubkeys...> <n> OP_CHECKMULTISIG`
* **`lock-time`**: `<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP`
* **`relative-lock-time`**: `<sequence> OP_CHECKSEQUENCEVERIFY OP_DROP`
* **`number`, `add`, `sub`, `bool-and`, `bool-or`, `num-equal`, `greater-than`, `drop`**: Stack arithmetic and boolean primitives (`<int64_t>`, `OP_ADD`, `OP_SUB`, `OP_BOOLAND`, `OP_BOOLOR`, `OP_NUMEQUAL`, `OP_GREATERTHAN`, `OP_DROP`).
* **`coin-lock-miner`**: `<minerPubKey> OP_DROP <lockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG`
* **`pow-miner`**: `<nonce> <powChallenge> <minerPubKey> OP_DROP OP_DROP OP_DROP <lockHeight> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_DUP OP_HASH160 <pubKeyHash> OP_EQUALVERIFY OP_CHECKSIG`

Supported `condition` role:
* **`if-condition`**: Compiles `expressions` followed by `OP_IF <true_actions> OP_ELSE <false_actions> OP_ENDIF`.

---

### 2. Taproot (`P2TR`) Compilation & BIP-350 Bech32m Encoding
The `compilemescaltotaproot` RPC and `CompileMESCALToTaproot` function compile a MESCAL JSON definition together with a 32-byte x-only `internal_pubkey` $P$ into a Witness v1 Taproot output:

1. **Tagged Leaf Hash**:

   $$
   \text{leafHash} = \text{TaggedHash}_{\text{TapLeaf}}\left(0\text{xc0} \mathbin{\Vert} \text{CompactSize}(|\text{leafScript}|) \mathbin{\Vert} \text{leafScript}\right)
   $$

2. **Tweak & Output Key**:

   $$
   t = \text{TaggedHash}_{\text{TapTweak}}\left(P \mathbin{\Vert} \text{leafHash}\right)
   $$

   $$
   Q = P + t \cdot G
   $$

3. **Locking Script (`TX_WITNESS_V1_TAPROOT`)**:

   $$
   \text{scriptPubKey} = \text{OP}\underline{\hspace{0.4em}}\text{1}\hspace{0.3em}\langle 32\text{-byte } x(Q) \rangle
   $$

4. **BIP-350 Bech32m Address Encoding (`SEC-11`)**:
   Witness v1 addresses (`kt1p...` on Mainnet, `tk1p...` on Testnet, `ert1p...` on Regtest) use the standard BIP-350 Bech32m polymod checksum constant **`0x2bc830a3`** (`src/bech32.cpp`).

---

### 3. Wallet Watch-Only Tracking & Coin-Selection Isolation (`SEC-13`)
In `IsMine` (`src/script/ismine.cpp`), `TX_WITNESS_V1_TAPROOT` and general `TX_MESCAL_CONTRACT` scripts registered in the local keystore (`HaveCScript`) return `ISMINE_WATCH_ONLY` rather than `ISMINE_SPENDABLE`. This ensures:
* The core wallet tracks balances and transaction history for MESCAL/Taproot contract outputs.
* Automatic UTXO coin selection (`AvailableCoins` / `SelectCoinsMinConf`) never accidentally selects custom contract outputs that the standard P2PKH/P2SH `SignStep` signer cannot satisfy without contract-specific witness arguments.

---

### 4. Permanent Disabling of Legacy Stateful Opcodes (`SEC-10`)
In `EvalScript` (`src/script/interpreter.cpp`), the opcodes `OP_MLOAD`, `OP_MSTORE`, `OP_SLOAD`, `OP_SSTORE`, `OP_PUBLISH`, `OP_RUN`, and `OP_UPDATE_STATUS` immediately fail script execution with `SCRIPT_ERR_DISABLED_OPCODE`. All `CScriptContract` helper methods in `src/script/script.cpp` return `false` without accessing `pScriptDB`.

## Backward Compatibility
* MESCAL contracts compile to standard `CScript` (`P2SH` or `P2TR`) compatible with existing script validation rules.
* Disabling legacy stateful SWMP opcodes (`SEC-10`) is a consensus-hardening measure; no valid KRISTA chain blocks use legacy stateful opcodes.

## Reference Implementation
* Compiler & Taproot synthesis: `src/script/mescal.h`, `src/script/mescal.cpp`, and `src/rpc/SmartContracts.cpp`.
* BIP-350 Bech32m encoding: `src/bech32.cpp`.
* Interpreter hardening & wallet isolation: `src/script/interpreter.cpp`, `src/script/script.cpp`, and `src/script/ismine.cpp`.
