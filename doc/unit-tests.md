# Testing in KristaTech Core

KristaTech Core includes a robust suite of tests to verify consensus changes, transaction logic, wallet behaviors, and network consensus. The test suite is divided into C++ unit tests and Python functional/integration tests.

---

## 1. C++ Unit Tests (`test_pivx`)

C++ unit tests target individual classes, consensus functions, and low-level algorithms. They are built using the **Boost Test Library**.

### Compilation
Unit tests are compiled automatically during the build process unless explicitly disabled:
```bash
# Compile and run all unit tests
make check
```

If you modify test sources, you can recompile only the test binary:
```bash
# Recompile test suite binary manually
make -C src test/test_pivx
```

### Running C++ Unit Tests Manually
You can run the compiled binary directly from the `src` folder:
```bash
./src/test/test_pivx
```

### Running Specific Tests
You can filter execution to a single test suite or specific test cases using the `--run_test` argument:
```bash
# Run all tests in a specific suite (e.g., mpa_tests)
./src/test/test_pivx --run_test=mpa_tests

# Run a specific test case within a suite
./src/test/test_pivx --run_test=mpa_tests/burn_weight_decay
```
Run `./src/test/test_pivx --help` to see all available Boost.Test command-line flags.

---

## 2. C++ GUI Unit Tests (`test_kristatech-qt`)

If the Qt GUI is compiled, a separate unit test binary for Qt widgets and interface logic is created.

### Running GUI Tests Manually
```bash
./src/qt/test/test_kristatech-qt
```

---

## 3. Python Functional & Integration Tests

Functional tests spin up local regtest nodes, connect them in various topologies, mine blocks, and call RPC methods to assert correct network behavior (such as block propagation, invalid transaction rejection, and activation of upgrades).

They are located in the [test/functional/](file:///home/bedri/Coin-Projects/KristaTech-KRISTA/test/functional/) directory.

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
# Run the consensus miner registration test
python3 test/functional/consensus_miner_registration.py

# Run the MPA/PoMBL consensus test
python3 test/functional/consensus_pombl.py

# Run the treasury and faucet split test
python3 test/functional/consensus_treasury_faucet.py
```

### Debugging Failed Tests
Functional tests support several options for investigating failures:
* `--nocleanup`: Keeps temporary node directories and debug logs intact under `/tmp/pivx_func_test_XXXXXX` for post-run inspection.
* `--loglevel=debug`: Outputs verbose trace messages from the test framework.
* `test_framework.log` and `nodeX/regtest/debug.log` will contain detailed execution logs of each node.
