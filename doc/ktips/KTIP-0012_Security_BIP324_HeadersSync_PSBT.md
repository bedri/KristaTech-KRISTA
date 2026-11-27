```
KTIP: 0012
Title: Taproot/CSV/BIP68 Consensus Enforcement, P2P/DoS Hardening, BIP324 v2 Transport, HeadersSyncState & PSBT
Author: KristaTech Core Developers
Status: Active
Type: Standard Track (Consensus / Network / Wallet)
Created: 2026-11-16
```

## Abstract
This proposal specifies a comprehensive security and protocol modernization upgrade for KRISTA Core (`v1.5.9.0`, `PROTOCOL_VERSION = 70983`, `MIN_PEER_PROTO_VERSION = 70983`): (1) **Consensus Script & Relative Locktime Enforcement** (`SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_TAPROOT_BIP341`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, base `SCRIPT_VERIFY_NULLFAIL`, BIP-341 `ComputeTapBranchHash` `memcmp`, ECDSA-in-Tapscript BIP-341 sighash, Schnorr sigcache, and BIP68 `CheckSequenceLocks`), (2) **P2P & RPC/HTTP DoS Hardening** (bounded `GETDATA` queue, per-peer `CMPCTBLOCK`/`BLOCKTXN` isolation & empty-`shorttxids` guard, duplicate `VERACK` guard, `CBlockHeader` ADAM deserialization bounds, `ADDR` token-bucket rate limiting, BIP133 `FEEFILTER`, `-blocksonly`, salted `-rpcauth`, and HTTP `Host` DNS-rebinding `403` protection), (3) **`HeadersSyncState`** low-work header pre-sync buffering (`8 MiB` byte cap & non-zero `txcount` rejection), (4) **BIP324 v2 Encrypted P2P Transport** (`ElligatorSwift` + `FSChaCha20Poly1305` with automatic v1 fallback and decoy-flood disconnect), and (5) **Hardened BIP174 PSBT** (`4,096` I/O cap, UTXO/metadata merge conflict rejection, Taproot & `P2SH` multisig signing/finalization).

## Motivation
1. **Closing Consensus-vs-Policy Gaps**: Previously, `SCRIPT_VERIFY_TAPROOT` and `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY` were enforced in mempool policy (`STANDARD_SCRIPT_VERIFY_FLAGS`) but omitted from `ConnectBlock()`, creating an anyone-can-spend / timelock-bypass risk inside mined blocks.
2. **P2P Resource Exhaustion & Poisoning**: Unbounded `vRecvGetData` queues (CVE-2018-17145 pattern), global `mapPartiallyDownloadedBlocks` state (CVE-2023-39910 / CVE-2024-35202 area), unthrottled `ADDR` floods (CVE-2021-3401), and unbuffered low-work `HEADERS` floods exposed nodes to memory exhaustion and relay disruption.
3. **Transport Privacy & Cold-Signing Interoperability**: Plaintext P2P traffic reveals transaction origin and topology to passive observers, while offline/hardware signing workflows require standard BIP174 PSBT support.

## Specification

### 1. Consensus Enforcement: Taproot (`BIP-341`), CSV, `NULLFAIL` & BIP68 (`ConnectBlock` / `AcceptToMemoryPoolWorker`)
* `ConnectBlock()` and `AcceptToMemoryPoolWorker()` (`src/main.cpp`) enforce `SCRIPT_VERIFY_P2SH | SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_CHECKSEQUENCEVERIFY | SCRIPT_VERIFY_TAPROOT | SCRIPT_VERIFY_TAPROOT_BIP341` (plus `SCRIPT_VERIFY_CHECKLOCKTIMEVERIFY` when BIP65 is active) on every non-coinbase/non-coinstake transaction.
* **V4 BIP-341 Script & Signature Hardening (`UPGRADE_HARDENING_V4`, Height `1`)**:
  - **Lexicographical `ComputeTapBranchHash` (`src/script/interpreter.cpp`)**: Sorts Taproot branch child hashes using byte-wise `std::memcmp(left.begin(), right.begin(), 32) < 0` per BIP-341 (replacing pre-V4 little-endian `uint256::operator<`).
  - **ECDSA-in-Tapscript BIP-341 Sighash**: Under `SCRIPT_VERIFY_TAPROOT_BIP341`, 33/65-byte ECDSA keys inside `SIGVERSION_TAPSCRIPT` compute canonical `SignatureHashTaproot(..., SIGVERSION_TAPSCRIPT, ...)` committing `tapleaf_hash` and `codesep_pos` (replacing pre-V4 `SIGVERSION_BASE` DER fallback).
  - **Schnorr SigCache & Base `SCRIPT_VERIFY_NULLFAIL`**: `CachingTransactionSignatureChecker::VerifySchnorrSignature` (`src/script/sigcache.cpp`) caches BIP-340 Schnorr signatures under `'S'` domain separation (forwarded via `TapscriptSignatureChecker`), and `EvalScript` rejects failed non-empty signatures with `SCRIPT_ERR_SIG_NULLFAIL` both in `SIGVERSION_TAPSCRIPT` and whenever `SCRIPT_VERIFY_NULLFAIL` is active on base `OP_CHECKSIG` scripts.
* **BIP68 Relative Locktimes (`CheckSequenceLocks`)**: Both `AcceptToMemoryPoolWorker` and `ConnectBlock` enforce height-based and median-time-past-based relative locktimes for `tx.nVersion >= 2` inputs when `CTxIn::SEQUENCE_LOCKTIME_DISABLE_FLAG` is unset.

### 2. Phase 1 P2P, HTTP & RPC DoS Hardening
* **`GETDATA` Queue Cap**: `ProcessMessage(NetMsgType::GETDATA)` rejects incoming `GETDATA` batches that would push `pfrom->vRecvGetData.size() + vInv.size() > MAX_INV_SZ` (`50,000`), penalizing `Misbehaving(pfrom->GetId(), 20)`.
* **Per-Peer Compact Blocks (`CMPCTBLOCK` / `BLOCKTXN`) & Empty-ShortTxIDs Guard**: `mapPartiallyDownloadedBlocks` is keyed by `std::pair<NodeId, uint256>` and cleaned up in `FinalizeNode()`. Only the peer that sent the `CMPCTBLOCK` can complete or clear its `BLOCKTXN` entry. Additionally (`src/net_processing.cpp`), `CMPCTBLOCK` applies per-peer sliding rate limiting and skips scanning `mempool.mapTx` entirely when `cmpct.shorttxids.empty()` (scanning only requested short IDs when non-empty).
* **`CBlockHeader` ADAM Deserialization Bounds (`src/primitives/block.h`)**: `SerializeAdamMpaFields` enforces strict `CompactSize` caps during unserialization (`<= 32` `vAdamMiners`, `<= 32` `vAdamSolutions`, `<= 512` bytes per solution / `vAdamVRFProof` / `vAdamCoordinatorSig`, `<= 4096` bytes `vQuorumSig`).
* **Duplicate `VERACK` Guard (`src/net_processing.cpp`)**: Ignores duplicate `NetMsgType::VERACK` messages once `pfrom->fSuccessfullyConnected` is already `true` and guards initial `ADAMSOL` tip sync with atomic `fSentInitialAdamSolSync`.
* **`ADDR` Token-Bucket Rate Limiting**: `CNode` maintains `nAddrTokenBucket` (capacity `1,000.0`, refill `0.1` addr/s) to drop excess `ADDR` floods from non-whitelisted peers.
* **BIP133 `FEEFILTER` & `-blocksonly`**: `NetMsgType::FEEFILTER` is negotiated and periodically broadcast; `SendMessages` skips `MSG_TX` relays below `pto->minFeeFilter` or when `-blocksonly` is enabled.
* **Salted `-rpcauth` & HTTP `Host` DNS-Rebinding `403` Guard**: `src/httprpc.cpp` verifies `<USERNAME>:<SALT>$<HASH>` entries via `CHMAC_SHA256` and `TimingResistantEqual`. `http_request_cb` (`src/httpserver.cpp`) validates incoming HTTP `Host` headers via `IsAllowedHTTPRequestHost`, returning `403 Forbidden` (`HTTP_FORBIDDEN`) for untrusted hostnames outside `localhost`, numeric IP literals (`fAllowLookup=false`), and configured `-rpcbind` hosts to prevent browser DNS-rebinding attacks.

### 3. `HeadersSyncState` Low-Work Header Pre-Sync
* `mapHeadersSyncState` (`src/net_processing.cpp`, cleaned in `FinalizeNode()`) buffers incoming `HEADERS` up to `max(16,384, chainActive.Height() + 2000)` headers and an **`8 MiB` (`MAX_PRESYNC_BYTES = 8 * 1024 * 1024`)** serialized byte ceiling when the parent chain work is below `chainActive.Tip()->nChainWork / 2`.
* `ProcessHeadersMessage` immediately rejects any header entry with non-zero `nTxCount != 0` (`Misbehaving(20)`). Buffered headers are verified for continuity and cumulative `GetBlockProof()` work before being flushed into `mapBlockIndex` via `AcceptBlockHeader()`. Exceeding the header count or `8 MiB` byte cap without sufficient work triggers `Misbehaving(20)` and discards the buffer.

### 4. BIP324 v2 Encrypted P2P Transport (`NODE_P2P_V2`)
* Nodes advertise `NODE_P2P_V2 = (1 << 11)` when `-v2transport` is active (default: `1`).
* `BIP324Cipher` (`src/bip324.cpp`, `src/crypto/chacha20poly1305.cpp`) uses `secp256k1_ellswift_create` and `secp256k1_ellswift_xdh` (`secp256k1_ellswift_xdh_hash_function_bip324`) with HKDF-SHA256 (`"bitcoin_v2_shared_secret" || magic`) to derive `FSChaCha20` (length) and `FSChaCha20Poly1305` (AEAD payload, rekeying every `224` packets) ciphers and `16`-byte garbage terminators.
* **Automatic v1 Fallback & Decoy-Flood Disconnect (`src/net.cpp`)**: Inbound connections inspect the first `4` bytes (`fV2AutoDetect`); matching `Params().MessageStart()` switches seamlessly to v1 plaintext, while outbound connections initiate v2 when the peer advertises `NODE_P2P_V2`. During v2 framing, `ReceiveMsgBytes` disconnects peers (`fDisconnect = true`) that send `> 64` decoy packets during handshake (`VERSION_PACKET`), `> 64` consecutive application decoy packets (`v2ConsecutiveDecoys`), or `> 256` decoy packets / `> 4 MiB` of decoys within any `60s` window (`APPLICATION`).

### 5. Hardened PSBT (BIP174 Partially Signed Transactions)
* Implemented `PartiallySignedTransaction`, `PSBTInput`, `PSBTOutput`, Base64/binary serialization (`psbt\xff`), `Merge`, `FinalizePSBT`, and `SignPSBTInput` in `src/psbt.h` / `src/psbt.cpp`:
  - **Resource & Field Bounds**: Rejects PSBTs exceeding **`4,096` inputs or `4,096` outputs** during deserialization and `IsSane()`, and bounds `PSBT_IN_PARTIAL_SIG` (`<= 73` bytes), `PSBT_IN_REDEEMSCRIPT` / `SCRIPTSIG` (`<= MAX_SCRIPT_SIZE`), and `PSBT_IN_TAP_KEY_SIG` (`64` or `65` bytes).
  - **UTXO & Metadata Merge Conflict Rejection**: `PSBTInput::Merge` returns `bool` and fails closed on mixing `non_witness_utxo` with `witness_utxo` or conflicting UTXO hashes/outputs, `redeem_script`, `sighash_type`, or `tap_internal_key`.
  - **Taproot & P2SH Multisig Signing/Finalization**: `SignPSBTInput` and `BuildScriptSigFromPSBTInput` support BIP-341 `TX_WITNESS_V1_TAPROOT` key-path Schnorr signing/finalization (via `BuildSpentOutputsPrecom` + `ComputeTaprootSighash`) as well as `TX_SCRIPTHASH` (`P2SH`) and `TX_MULTISIG` signing and final `scriptSig` assembly.
* Exposed RPCs: `createpsbt`, `converttopsbt`, `decodepsbt`, `combinepsbt`, `utxoupdatepsbt`, `analyzepsbt`, `finalizepsbt`, and `walletprocesspsbt`.

## Backward Compatibility
* Activating `SCRIPT_VERIFY_TAPROOT`, `SCRIPT_VERIFY_TAPROOT_BIP341`, `SCRIPT_VERIFY_CHECKSEQUENCEVERIFY`, and `BIP68` in `ConnectBlock` (alongside `UPGRADE_HARDENING_V4` at height `1` across all networks in `v1.5.9.0`, `PROTOCOL_VERSION = 70983`, `MIN_PEER_PROTO_VERSION = 70983`) is enforced across all synchronized Seed nodes.
* BIP324 v2 transport and BIP133 `FEEFILTER` are backward-compatible at the P2P layer via `NODE_P2P_V2` negotiation and v1 magic auto-detection.

## Reference Implementation
* Consensus, `HeadersSyncState`, `GETDATA`/`ADDR`/`CMPCTBLOCK`/`VERACK`/`FEEFILTER`: `src/main.cpp`, `src/net_processing.cpp`, `src/primitives/block.h`, `src/protocol.h`, `src/protocol.cpp`, `src/init.cpp`.
* BIP341 / Tapscript / SigCache / Signing: `src/script/interpreter.h`, `src/script/interpreter.cpp`, `src/script/sigcache.cpp`, `src/script/sign.cpp`.
* BIP324 v2 Transport & Decoy Protection: `src/bip324.h`, `src/bip324.cpp`, `src/crypto/chacha20poly1305.h`, `src/crypto/chacha20poly1305.cpp`, `src/net.h`, `src/net.cpp`.
* HTTP Host Guard, RPC Auth & PSBT: `src/httpserver.cpp`, `src/httprpc.cpp`, `src/psbt.h`, `src/psbt.cpp`, `src/rpc/rawtransaction.cpp`, `src/wallet/rpcwallet.cpp`, `src/rpc/server.cpp`.
