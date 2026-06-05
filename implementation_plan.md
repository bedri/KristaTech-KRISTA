# Implementation Plan - A Decentralized Approach Model (ADAM) Consensus

This implementation plan details the technical steps to code and test the **ADAM (A Decentralized Approach Model)** cooperative consensus mechanism inside the newly created `/home/bedri/Coin-Projects/ADAM` repository. 

Our goal is to build a Proof-of-Concept (PoC) where the block creation is divided into `n` partial problems solved by randomly selected miners, and aggregated by a randomly selected Coordinator (Decider), replacing the hash power race with distributed co-operative mining.

---

## 1. Cryptographic Design (Simulating VRF & Thresholds)

To keep the compilation stable and avoid introducing massive external C++ libraries that could break the build, we will utilize the blockchain's existing cryptographic primitives built on **secp256k1**:
1. **Verifiable Random Function (VRF)**: A secp256k1 signature generated deterministically (RFC 6979) on the hash of the previous block acts as a verifiable random value. The hash of this signature is the lottery ticket.
2. **Threshold Proofs**: We will represent the combination of partial problem solutions by hashing the concatenated solutions and verify that a quorum (e.g. `m` out of `n`) of the selected miners submitted valid signatures.

---

## 2. Proposed Changes

We will implement the ADAM changes in stages, using git micro-commits after each logical step.

### Stage 1: Consensus Parameters & Activation Height
Define the activation height and constants for the ADAM model. For the local Regtest testing network, we will set `n = 4` partial miners and `m = 3` threshold quorum to make local simulation easy.

#### [MODIFY] [params.h](file:///home/bedri/Coin-Projects/ADAM/src/consensus/params.h)
- Add consensus variables:
  - `nAdamHeight`: Height where ADAM activates.
  - `nAdamMinersCount`: Number of partial miners (e.g. `4` on regtest).
  - `nAdamThreshold`: Quorum size (e.g. `3` on regtest).

#### [MODIFY] [chainparams.cpp](file:///home/bedri/Coin-Projects/ADAM/src/chainparams.cpp)
- Configure `nAdamHeight` for Regtest (e.g., block 200), Testnet, and Mainnet.

---

### Stage 2: Block Header & Block Structure Updates
We need to modify the block structure to hold the list of selected miners, their signatures, and the partial solutions.

#### [MODIFY] [block.h](file:///home/bedri/Coin-Projects/ADAM/src/primitives/block.h)
- Add fields to `CBlockHeader` / `CBlock`:
  - `std::vector<CPubKey> vAdamMiners`: Public keys of the selected miners.
  - `std::vector<std::vector<unsigned char>> vAdamSolutions`: Array of partial PoW solutions (nonces/hashes).
  - `std::vector<unsigned char> vAdamCoordinatorSig`: Signature of the Coordinator.
- Update serialization functions (`SerializationOp`) to correctly serialize/deserialize these fields when `nHeight >= nAdamHeight`.

---

### Stage 3: VRF Election and Selection Logic
We will implement the logic to deterministically select the `n` miners and `1` Coordinator from the pool of active nodes using the previous block's hash.

#### [NEW] [adam.h](file:///home/bedri/Coin-Projects/ADAM/src/adam.h) / [adam.cpp](file:///home/bedri/Coin-Projects/ADAM/src/adam.cpp)
- Create a helper module to:
  - Keep track of the active miner registration pool (e.g. read from masternode list or transaction data).
  - Implement election logic: select `n` miners and `1` Coordinator deterministically based on seed `hashPrevBlock`.

---

### Stage 4: Consensus Validation rules
Update validation logic to accept both PoW and PoS blocks, and verify ADAM proofs when active.

#### [MODIFY] [main.cpp](file:///home/bedri/Coin-Projects/ADAM/src/main.cpp)
- In `ConnectBlock()`, modify/remove the hard switchover check that rejects PoW blocks when PoS is active.
- Add validations inside `CheckBlock()` for blocks at height `>= nAdamHeight`:
  - Confirm the block has the correct number of partial solutions.
  - Validate that the solutions verify against the elected miner list.
  - Validate coordinator signature.

#### [MODIFY] [pow.cpp](file:///home/bedri/Coin-Projects/ADAM/src/pow.cpp)
- Update `GetNextWorkRequired` to separately calculate targets for PoW and PoS blocks to prevent difficulty calculation mismatches.

---

### Stage 5: Miner and Staker Threads
Modify cüzdan (wallet) block generation to support cooperative mining.

#### [MODIFY] [miner.cpp](file:///home/bedri/Coin-Projects/ADAM/src/miner.cpp)
- Update `GenerateBitcoins()` and staker loop to:
  - Check if local node is elected as a partial miner or Coordinator for the next block height.
  - If elected as partial miner, solve the sub-difficulty puzzle and broadcast the solution.
  - If elected as Coordinator, collect sub-solutions from the P2P network, build the full block, and sign it.

---

## 3. Verification Plan

### Automated Tests
- Run `make` inside the Podman builder container to verify successful compilation after each commit.
- Write a functional regtest script under `test/functional/` to run a local cluster of nodes and verify blockchain progress beyond the `nAdamHeight` transition block.

### Manual Verification
- Deploy a 4-node local regtest network.
- Force blocks to mine under the ADAM rules and check that:
  - Both PoW/PoS blocks are accepted.
  - Rewards are split equally among the coordinator and miners.
