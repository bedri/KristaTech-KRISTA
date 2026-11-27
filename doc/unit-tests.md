# Testing in KristaTech Core

KristaTech Core includes a robust suite of tests to verify consensus changes, transaction logic, wallet behaviors, and network consensus. The test suite is divided into C++ unit tests and Python functional/integration tests.

---

## 1. C++ Unit Tests (`test_kristatech`)

C++ unit tests target individual classes, consensus functions, and low-level algorithms. They are built using the **Boost Test Library**. The compiled `src/test/test_kristatech` binary contains **61 test suites / 326 test cases**.

### Compilation
Unit tests are compiled automatically during the build process unless explicitly disabled:
```bash
# Compile and run all unit tests
make check
```

If you modify test sources, you can recompile only the test binary:
```bash
# Recompile test suite binary manually
make -C src test/test_kristatech
```

### Running C++ Unit Tests Manually
You can run the compiled binary directly from the `src` folder:
```bash
./src/test/test_kristatech
```

### Key Consensus, Security & Hardening Suites
In addition to core consensus suites (`adam_tests`, `mpa_tests`, `dip3_tests`, `mescal_tests`, `main_tests` including `bad-cs-in-pow`), the compiled binary includes expanded hardening and protocol coverage across:
* `network_upgrades_tests` (`src/test/upgrades_tests.cpp`): Height-gated `UPGRADE_HARDENING_V1`–`V4` (`"Hardening_v4"` at Height `1`), `UPGRADE_FAIRNESS`, `UPGRADE_DIP3`, epoch progression, and `StakeMinDepthV2` consensus enforcement.
* `DoS_tests` (`src/test/DoS_tests.cpp`): Peer banning, `CBlockHeader` ADAM deserialization size bounds, duplicate `VERACK` / pre-sync `ADAMSOL` guard, `HEADERS` pre-sync validation, and compact block `shorttxids` checks.
* `bip324_tests` (`src/test/bip324_tests.cpp`): BIP324 v2 encrypted P2P AEAD roundtrip, AAD tamper rejection, rekeying, `ChaCha20Poly1305` error handling, and decoy flood disconnection.
* `blockencodings_tests` (`src/test/blockencodings_tests.cpp`): Compact block serialization, prefill bounds/overflow validation, and `CBlockHeader` ADAM deserialization bounds.
* `psbt_tests` (`src/test/psbt_tests.cpp`): BIP174 PSBT `non_witness_utxo` / `witness_utxo` enforcement, `PSBTInput::Merge` conflict rejection, `4096` input/output cap, `PSBT_IN_TAP_KEY_SIG` length enforcement, and Taproot / P2SH multisig signing and finalization.
* `script_tests` (`src/test/script_tests.cpp`): `SCRIPT_VERIFY_TAPROOT_BIP341`, base `OP_CHECKSIG` `SCRIPT_VERIFY_NULLFAIL`, MESCAL destination extraction, and CLTV timelock solvability.
* `rpc_tests` & `rpc_protx_tests` (`src/test/rpc_tests.cpp`): `IsAllowedHTTPRequestHost()` DNS-rebinding allowlist validation (`403 Forbidden`), `protx` integer argument parsing, RPC batch limit & range checks, and help port consistency.
* `wallet_tests` (`src/wallet/test/wallet_tests.cpp`): Coin selection (`>600`-input largest-first fallback), locked miner registration coins, expired CLTV spendability, encrypted BLS storage & legacy `masternode.conf` migration (`0600` + `***REDACTED***` WIF redaction), and backup `0600` permissions.

### Running Specific Tests
You can filter execution to a single test suite or specific test cases using the `--run_test` argument:
```bash
# Run core consensus, KTDIP3, upgrade, and MESCAL suites together
./src/test/test_kristatech --run_test=adam_tests,mescal_tests,mpa_tests,dip3_tests,network_upgrades_tests,psbt_tests,rpc_protx_tests

# Run all tests in a specific suite (e.g., mpa_tests)
./src/test/test_kristatech --run_test=mpa_tests

# Run a specific test case within a suite
./src/test/test_kristatech --run_test=mpa_tests/burn_weight_decay
```
Run `./src/test/test_kristatech --help` to see all available Boost.Test command-line flags.

---

## 2. C++ GUI Unit Tests (`test_kristatech-qt`)

If the Qt GUI is compiled, a separate unit test binary (`src/qt/test/test_kristatech-qt`) for Qt widgets and interface logic is created, comprising **2 test classes / 9 test slots** (`URITests` with 8 slots + `PaymentServerTests` with 1 slot):
* `URITests` (`src/qt/test/uritests.cpp`):
  * `uriTests` & `transactionRecordTests`: `kristatech:` URI parsing and transaction record classification.
  * `taprootAndPsbtUiTests`: Taproot (`kt1p...`) destination handling and PSBT UI flows.
  * `wizardAndConsolidationTests`: Oversized UTXO handling (`TransactionTooLarge`), `>600`-input largest-first fallback, targeted UTXO consolidation preview, and `MNRow` `"Confirming (7/15)"` collateral confirmation progress display.
  * `visualDesignerLifecycleTests`: `VisualDesignerDialog` block flow lifecycle, theme styling, and IF-condition validation.
  * `securityUiHardeningTests`: Plain-text escaping in `DefaultDialog` and auto-expiring sensitive clipboard (`GUIUtil::setSensitiveClipboard`).
  * `faqAndAboutUiTests`: `SettingsFaqWidget` (**Help & FAQ**, 14-card search/category filters/deep-link) and `SettingsAboutWidget` (**About KristaTech**, version & clipboard copy).
  * `topBarMoreMenuAndPinOptionTests`: `TopBar` **`MORE ▾`** dropdown (`Masternodes`, `Smart Contracts`, `Network Stats` / `Stats`) and **Settings -> Display & Units -> `Pin advanced tabs to top bar`** preference toggle.
* `PaymentServerTests` (`src/qt/test/paymentservertests.cpp`): Payment request handling (`paymentServerTests`).

### Running GUI Tests Manually
```bash
# Run using X11 / XCB (depends Qt bundles the xcb platform plugin)
./src/qt/test/test_kristatech-qt

# Run headless on servers / CI without an active X server
xvfb-run -a ./src/qt/test/test_kristatech-qt
```

---

## 3. Python Functional & Integration Tests

Functional tests spin up local regtest nodes, connect them in various topologies, mine blocks, and call RPC methods to assert correct network behavior (such as block propagation, invalid transaction rejection, and activation of upgrades).

They are located in the [test/functional/](../test/functional/) directory.

### Requirements
- Python 3.x
- `kristatechd` and `kristatech-cli` binaries must be successfully built and present in `src/`.

### Running All Functional Tests
You can run the test runner script to execute the entire functional test suite:
```bash
python3 test/functional/test_runner.py
```

### Running a Specific Functional Test
You can run any functional test script individually:
```bash
# Run the ADAM Dual-Track & PoBLS consensus test
python3 test/functional/consensus_adam_dualtrack_pobls.py

# Run the consensus miner registration test
python3 test/functional/consensus_miner_registration.py

# Run the MPA/PoMBL consensus test
python3 test/functional/consensus_pombl.py

# Run the developer treasury split and governance spend test
python3 test/functional/consensus_treasury_faucet.py

# Run KTDIP3 Masternode & MESCAL Taproot feature tests
python3 test/functional/feature_masternodes.py
python3 test/functional/feature_mescal_taproot.py
```

### Debugging Failed Tests
Functional tests support several options for investigating failures:
* `--nocleanup`: Keeps temporary node directories and debug logs intact under `/tmp/kristatech_func_test_XXXXXX` for post-run inspection.
* `--loglevel=debug`: Outputs verbose trace messages from the test framework.
* `test_framework.log` and `nodeX/regtest/debug.log` will contain detailed execution logs of each node.
