```
KTIP: 0012
Title: Taproot/CSV/BIP68 Consensus Enforcement, P2P/DoS Hardening, BIP324 v2 Transport, HeadersSyncState & PSBT
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus / Network / Wallet)
Created: 2026-11-16
```

## Abstract
This proposal specifies a comprehensive security and protocol modernization upgrade for KRISTA Core: (1) **Consensus Script & Relative Locktime Enforcement** (`SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, and BIP68 `CheckSequenceLocks` in `ConnectBlock`), (2) **P2P & RPC DoS Hardening** (bounded `GETDATA` receive queue, per-peer `CMPCTBLOCK`/`BLOCKTXN` isolation, `ADDR` token-bucket rate limiting, BIP133 `FEEFILTER`, `-blocksonly`, and salted HMAC-SHA256 `-rpcauth`), (3) **`HeadersSyncState`** low-work header pre-sync buffering, (4) **BIP324 v2 Encrypted P2P Transport** (`ElligatorSwift` + `FSChaCha20Poly1305` with automatic v1 fallback), and (5) **BIP174 Partially Signed KristaTech Transactions (PSBT)**.

## Motivation
1. **Closing Consensus-vs-Policy Gaps**: Previously, `SCRIPT_VERIFY_TAPROOT` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` were enforced in mempool policy (`STANDARD_SCRIPT_VERIFY_FLAGS`) but omitted from `ConnectBlock()`, creating an anyone-can-spend / timelock-bypass risk inside mined blocks.
2. **P2P Resource Exhaustion & Poisoning**: Unbounded `vRecvGetData` queues (CVE-2018-17145 pattern), global `mapPartiallyDownloadedBlocks` state (CVE-2023-39910 / CVE-2024-35202 area), unthrottled `ADDR` floods (CVE-2021-3401), and unbuffered low-work `HEADERS` floods exposed nodes to memory exhaustion and relay disruption.
3. **Transport Privacy & Cold-Signing Interoperability**: Plaintext P2P traffic reveals transaction origin and topology to passive observers, while offline/hardware signing workflows require standard BIP174 PSBT support.

## Specification

### 1. Consensus Enforcement: Taproot, CSV & BIP68 (`ConnectBlock` / `AcceptToMemoryPoolWorker`)
* `ConnectBlock()` (`src/main.cpp`) now enforces `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_TAPROOT` (plus `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY` when BIP65 is active) on every non-coinbase/non-coinstake transaction.
* **BIP68 Relative Locktimes (`CheckSequenceLocks`)**: Both `AcceptToMemoryPoolWorker` and `ConnectBlock` enforce height-based and median-time-past-based relative locktimes for `tx.nVersion >= 2` inputs when `CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG` is unset.

### 2. P2P & RPC DoS Hardening
* **`GETDATA` Queue Cap**: `ProcessMessage(NetMsgType::GETDATA)` rejects incoming `GETDATA` batches that would push `pfrom->vRecvGetData.size() + vInv.size() > MAX_INV_SZ` (`50,000`), penalizing `Misbehaving(pfrom->GetId(), 20)`.
* **Per-Peer Compact Blocks (`CMPCTBLOCK` / `BLOCKTXN`)**: `mapPartiallyDownloadedBlocks` is keyed by `std::pair<NodeId, uint256>` and cleaned up in `FinalizeNode()`. Only the peer that sent the `CMPCTBLOCK` can complete or clear its `BLOCKTXN` entry.
* **`ADDR` Token-Bucket Rate Limiting**: `CNode` maintains `nAddrTokenBucket` (capacity `1,000.0`, refill `0.1` addr/s) to drop excess `ADDR` floods from non-whitelisted peers.
* **BIP133 `FEEFILTER` & `-blocksonly`**: `NetMsgType::FEEFILTER` is negotiated and periodically broadcast; `SendMessages` skips `MSG_TX` relays below `pto->minFeeFilter` or when `-blocksonly` is enabled.
* **Salted `-rpcauth`**: `src/httprpc.cpp` supports `<USERNAME>:<SALT>$<HASH>` entries verified via `CHMAC_SHA256` and `TimingResistantEqual`.

### 3. `HeadersSyncState` Low-Work Header Pre-Sync
* `mapHeadersSyncState` (`src/main.cpp`, cleaned in `FinalizeNode()`) buffers incoming `HEADERS` up to `MAX_HEADERS_PRESYNC_BUFFER` (`16,384` headers) when the parent chain work is below `chainActive.Tip()->nChainWork / 2`.
* Headers are verified for continuity and cumulative `GetBlockProof()` work before being flushed into `mapBlockIndex` via `AcceptBlockHeader()`. Exceeding the buffer without sufficient work triggers `Misbehaving(20)` and discards the buffer.

### 4. BIP324 v2 Encrypted P2P Transport (`NODE_P2P_V2`)
* Nodes advertise `NODE_P2P_V2 = (1 << 11)` when `-v2transport` is active (default: `1`).
* `BIP324Cipher` (`src/bip324.cpp`, `src/crypto/chacha20poly1305.cpp`) uses `secp256k1_ellswift_create` and `secp256k1_ellswift_xdh` (`secp256k1_ellswift_xdh_hash_function_bip324`) with HKDF-SHA256 (`"bitcoin_v2_shared_secret" || magic`) to derive `FSChaCha20` (length) and `FSChaCha20Poly1305` (AEAD payload, rekeying every `224` packets) ciphers and `16`-byte garbage terminators.
* **Automatic v1 Fallback**: Inbound connections inspect the first `4` bytes (`fV2AutoDetect`); matching `Params().MessageStart()` switches seamlessly to v1 plaintext, while outbound connections initiate v2 when the peer advertises `NODE_P2P_V2`.

### 5. PSBT (BIP174 Partially Signed Transactions)
* Implemented `PartiallySignedTransaction`, `PSBTInput`, `PSBTOutput`, Base64/binary serialization (`psbt\xff`), `Merge`, `FinalizePSBT`, and `SignPSBTInput` in `src/psbt.h` / `src/psbt.cpp`.
* Exposed RPCs: `createpsbt`, `converttopsbt`, `decodepsbt`, `combinepsbt`, `utxoupdatepsbt`, `analyzepsbt`, `finalizepsbt`, and `walletprocesspsbt`.

## Backward Compatibility
* Activating `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, and `BIP68` in `ConnectBlock` is a consensus rule change; coordinated deployment across all Seed nodes and a clean Genesis reset are applied.
* BIP324 v2 transport and BIP133 `FEEFILTER` are backward-compatible at the P2P layer via `NODE_P2P_V2` negotiation and v1 magic auto-detection.

## Reference Implementation
* Consensus, `HeadersSyncState`, `GETDATA`/`ADDR`/`CMPCTBLOCK`/`FEEFILTER`: `src/main.cpp`, `src/protocol.h`, `src/protocol.cpp`, `src/init.cpp`.
* BIP324 v2 Transport: `src/bip324.h`, `src/bip324.cpp`, `src/crypto/chacha20poly1305.h`, `src/crypto/chacha20poly1305.cpp`, `src/net.h`, `src/net.cpp`.
* RPC Auth & PSBT: `src/httprpc.cpp`, `src/psbt.h`, `src/psbt.cpp`, `src/rpc/rawtransaction.cpp`, `src/wallet/rpcwallet.cpp`, `src/rpc/server.cpp`.
