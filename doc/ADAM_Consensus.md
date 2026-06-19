# ADAM (A Decentralized Approach Model) Cooperative Consensus

## 1. Introduction and Background

The **ADAM (A Decentralized Approach Model)** consensus mechanism is a cooperative hybrid consensus model designed to harden the blockchain against mining centralization, selfish mining, block grinding, and targeted Denial-of-Service (DoS) attacks on block leaders. 

Traditional Proof-of-Work (PoW) and Proof-of-Stake (PoS) protocols suffer from distinct vulnerabilities:
* In PoW, miners with massive hashing power dominate block production, leading to pools that centralize consensus.
* In PoS, the richest staking wallets have a disproportionate chance of producing blocks.
* In both models, if the identity of the next block producer is known beforehand (pre-elected), they become targets for DoS attacks or bribery. If block contents can be manipulated to alter the next block's randomness, miners can perform *grinding attacks* to skew future leader elections.

ADAM addresses these issues by splitting the block production process into a **Cooperative Hybrid Round**:
1. **Election Phase**: For each block slot, a pseudo-random seed deterministically elects a pool of **$N$ Miners** and **1 Coordinator**.
2. **Solving Phase**: The elected Miners compile and solve lightweight, parallelized PoW puzzles (partial solutions) based on the current block's target.
3. **Aggregation Phase**: The elected Coordinator collects the partial solutions, verifies them, packages them into a block template, signs the final block header using their private key, and broadcasts it to the network.

---

## 2. Consensus Architecture & Parameters

ADAM consensus is activated conditionally based on block height. The core parameters are defined in `src/consensus/params.h` and configured per network in `src/chainparams.cpp`.

To allow the network to bootstrap smoothly when the active masternode count is low, ADAM supports two modes of operation controlled dynamically by a network Spork.

### A. Fallback Mode (Block Version 11)
Fallback Mode is designed for the bootstrap phase of the network. It operates without requiring active Masternode registration and is fully self-contained.
* **Miners Count ($N$)**: Dynamically determined by the size of the key pool $K-1$, where $11 \le K \le 14$ (keys are deterministic and the elected Coordinator is appended as the last element of `vAdamMiners`).
* **Consensus Threshold ($T$)**: Fixed at the consensus parameter `nAdamThreshold` (which is **`7`** on Mainnet/Regtest, but set to **`3`** on Testnet to facilitate local testing/block coordination) in both modes.
* **Self-Contained Header Layout**: `vAdamMiners` stores all $K$ public keys (first $K-1$ elected miners, last key is the coordinator). `vAdamSolutions` stores the $K-1$ partial solutions.

### B. Standard Mode (Block Version 12)
Standard Mode represents the full cooperative consensus state, requiring a fully populated Masternode network.
* **Miners Count ($N$)**: Configured via the consensus parameter `nAdamMinersCount` (defined in `src/consensus/params.h` and initialized in `src/chainparams.cpp` to `11` on Mainnet/Testnet/Regtest).
* **Consensus Threshold ($T$)**: Configured via the consensus parameter `nAdamThreshold` (defined in `src/consensus/params.h` and initialized in `src/chainparams.cpp` to **`7`** on Mainnet/Regtest, and **`3`** on Testnet).
* **Elected Coordinator**: The coordinator is elected dynamically from the active Masternode list and is distinct from the miners list.

### C. Spork-Controlled Activation (`SPORK_21_ADAM_STANDARD_MODE`)
The transition between Fallback Mode (Version 11) and Standard Mode (Version 12) is controlled by `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`).
* **Default Value**: `4070908800ULL` (OFF).
* **Behavior**:
  - If the spork is active: blocks are built as **Version 12** under Standard Mode rules.
  - If the spork is inactive: blocks are built as **Version 11** under Fallback Mode rules.

### Network Configurations
| Network | Activation Height (`Consensus::UPGRADE_ADAM`) | Default Mode | Target Spacing |
| :--- | :--- | :--- | :--- |
| **Mainnet** | 200 | Fallback (Version 11) | 20 seconds |
| **Testnet** | 200 | Fallback (Version 11) | 20 seconds |
| **Regtest** | 200 | Fallback (Version 11) | 10 seconds |

### D. Puzzle Difficulty Bit-Shift Parameters
To prevent a "false-positive flood" of puzzle solutions on the network while maintaining dynamic and configurable difficulty scaling across different block heights, ADAM utilizes three parameters in `Consensus::Params`:
* **V1 Difficulty Shift (`nAdamDifficultyShiftV1`)**: Bit-shift multiplier relaxed target difficulty for block heights below the shift height. Default value: `10` (or `12`).
* **V2 Difficulty Shift (`nAdamDifficultyShiftV2`)**: Bit-shift multiplier relaxed target difficulty for block heights at or above the shift height. Default value: `6`.
* **Shift Height (`nAdamDifficultyShiftHeight`)**: The block height threshold at which the difficulty transition occurs. Default value: `705`.

During puzzle verification, the `scaledTarget` is derived by shifting the consensus target (`Target`) by the current active difficulty shift value:

$$\text{scaledTarget} = \text{Target} \ll \text{activeShift}$$

* $\text{activeShift} = \text{nAdamDifficultyShiftV1}$ if block height $< \text{nAdamDifficultyShiftHeight}$.
* $\text{activeShift} = \text{nAdamDifficultyShiftV2}$ if block height $\ge \text{nAdamDifficultyShiftHeight}$.

### E. Starting Difficulty Limit (powLimit)
To prevent blocks 1–199 from being mined too quickly (which led to split forks and quorum deadlocks), the starting difficulty target `powLimit` is set to:

$$\text{powLimit} = \text{~UINT256\_ZERO} \gg 20$$

On Mainnet and Testnet, this is exactly `1/2^20` (equivalent to the genesis block's `nBits` of `0x1e0ffff0`). It ensures blocks are naturally spaced out at approximately 20 seconds from genesis, allowing nodes to establish stable P2P connections and maintain a unified chain tip.

---

## 3. Verifiable Random Function (VRF) & Rolling Seeds

To prevent **grinding attacks** (where miners alter transactions or nonces to manipulate the hash of block $H$, thereby skewing the leader election for block $H+1$), ADAM implements a **Verifiable Random Function (VRF) Rolling Seed** model.

### Mathematical Formulation
For any block height $H$ where the ADAM network upgrade (`Consensus::UPGRADE_ADAM`) is active:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Where:
* The election seed $\text{Seed}$ at height $H$ is used to determine the leaders for block $H+1$.
* The VRF proof $\text{VRFProof}$ at height $H-1$ is the signature generated by the elected **Coordinator** of block $H-1$ on the previous seed at height $H-2$ using a hybrid BLS12-381 + ECDSA fallback signature mechanism (`SignBLSWithECDSAFallback`).
* Under this scheme, the Coordinator generates a BLS12-381 signature using a BLS secret key derived deterministically from their ECDSA private key. To prove authorization and prevent key manipulation, the Coordinator signs the BLS public key using their ECDSA private key. Verification (`VerifyBLSWithECDSAFallback`) ensures both the ECDSA authorization of the BLS key and the BLS signature of the target seed are valid.
* Because the resulting signature is deterministic, the Coordinator has exactly one valid signature for a given seed, making the rolling seed completely immutable and unpredictable until the Coordinator signs and broadcasts it.

### Node Selection (SSLE)
The election of miners and coordinator is performed by `SelectAdamNodes()` inside `src/adam.cpp`:
1. Compile the active node pool (the registered Masternode list and active registered miners via Coin-Lock or PoW-Lock).
2. The selection pool is network-dependent:
   * **Mainnet & Testnet**: The pool is constructed dynamically from active Masternodes and active registered miners. However, during the early bootstrap phase (when block height is $< 5000$ on Mainnet or $< 600$ on Testnet), the network automatically scans the block producers (coinbase outputs) from blocks 1 to 199 and adds their public keys to the miner pool. This prevents chain stalls before active masternodes or registrations are established.
   * **Regtest**: The pool automatically bypasses external registrations and includes 15 deterministic bootstrap public keys to facilitate automated testing:

     $$\text{Pool}_{\text{bootstrap}} = \{\text{DeterministicPubKey}_0, \dots, \text{DeterministicPubKey}_{14}\}$$

3. Compute a unique hash rank for each node in the selection pool based on the rolling seed:

   $$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

4. Sort the pool in ascending order of their $\text{Rank}_i$.
5. The first $N$ nodes are elected as **Miners**.
6. The next node is elected as the **Coordinator**.

---

## 4. Block Header Extensions & Serialization

When the ADAM network upgrade (`Consensus::UPGRADE_ADAM`) is active, blocks are serialized using **Version 11** (Fallback Mode) or **Version 12** (Standard Mode) block structures. The `CBlockHeader` class in `src/primitives/block.h` is extended with four new fields:

```cpp
class CBlockHeader {
public:
    // Legacy fields...
    int32_t nVersion;
    uint256 hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    // ADAM Extended fields (Version >= 11)
    std::vector<CPubKey> vAdamMiners;                  // Public keys of elected miners (and coordinator in v11)
    std::vector<std::vector<unsigned char>> vAdamSolutions; // Serialized partial solutions (nonce + miner signature)
    std::vector<unsigned char> vAdamVRFProof;          // Coordinator's VRF signature on previous seed
    std::vector<unsigned char> vAdamCoordinatorSig;    // Coordinator's signature on final block header hash
};
```

### 4.1 Hashing and Serialization Order
Unlike `vAdamCoordinatorSig` (which signs the finished block header hash and is excluded from `GetHash()`), `vAdamVRFProof` is generated *before* the block hash is computed. It is included in the block header serialization during `GetHash()`, ensuring the VRF proof is cryptographically locked into the header.

### 4.2 Block Hashing Chain (Stateless)
To calculate the block hash (`CBlockHeader::GetHash()`), ADAM hashes the serialized header (excluding `vAdamCoordinatorSig` and the LLMQ `vQuorumSig` if version 12) through a chain of $M$ sequential hashing rounds corresponding to the elected miners:
* In fallback mode (`nVersion == 11`): $M = \text{vAdamMiners.size()} - 1$.
* In standard mode (`nVersion == 12`): $M = \text{vAdamMiners.size()}$.

For each round $i \in \{0, \dots, M-1\}$:
1. Derive a unique round hash from the previous block hash and round index:

   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$

2. Extract the first byte $v_i = \text{roundHash}_i[0]$.
3. Calculate an odd coprime multiplier $m_i$:

   $$m_i = v_i \mid 1$$

   If $m_i < 3$, set $m_i = 3$. This ensures the multipliers are coprime to $2^{256}$, preserving 100% entropy.
4. Perform the round hashing:
   - **Fallback Mode (Version 11)**:
     - **Round 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$
     - **Round $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{i-1}) \times m_i \pmod{2^{256}}$
   - **Standard Mode (Version 12)**:
     - **Round 0**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ for miner $0$.
       * Compute:

         $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, \text{SerializedHeader})$$

         $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$

         $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$

       * Apply multiplier:

         $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$

     - **Round $i > 0$**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ for miner $i$.
       * Compute:

         $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$

         $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

         $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

       * Apply multiplier:

         $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

The final hash $H_{\text{prev}}$ (or $H_0 \times m_0$ in Version 11) is returned as the block hash.

---

## 5. Consensus Validation Rules

When a block is received, `CheckBlock()` in `src/main.cpp` enforces the following validations if the ADAM network upgrade (`Consensus::UPGRADE_ADAM`) is active:

1. **Version Enforcement & Downgrade Prevention**:
   - The block version must be at least `11`.
   - If `block.nVersion == 11` and `block.GetBlockTime() >= sporkManager.GetSporkValue(SPORK_21_ADAM_STANDARD_MODE)`, the block is rejected with a `bad-version` DoS error to prevent malicious miner downgrades.
   - If the block height is $\ge \text{nPoMBLHeight}$ (300 on Regtest, 400 on Testnet, 2000 on Mainnet), the block version must be `12` if Standard Mode is active.

2. **Miners & Solutions Sizing**:
   - **Version 11 (Fallback Mode)**:
     - `vAdamMiners` size must be between 11 and 14.
     - `vAdamSolutions` size must be exactly `vAdamMiners.size() - 1`.
     - The Coordinator to verify is the last element of `vAdamMiners` (`vAdamMiners.back()`).
   - **Version 12 (Standard Mode)**:
     - `vAdamMiners` size must match exactly `nAdamMinersCount`.
     - `vAdamSolutions` size must match exactly `vAdamMiners.size()`.
     - The Coordinator is derived from `SelectAdamNodes`.

3. **VRF Proof Validation**: The `vAdamVRFProof` must be verified against the previous rolling seed $\text{Seed}_{H-1}$ and must be a valid signature matching the public key of the expected Coordinator using `VerifyBLSWithECDSAFallback`.

4. **Election Path Validation**: In Version 12, the list of public keys in `vAdamMiners` must match the exact output of the deterministic `SelectAdamNodes` algorithm. Version 11 bypasses this check since it operates in self-contained bootstrap mode.

5. **Partial Solutions Validation**:
   - The number of valid partial solutions must meet the required threshold:
      - **Version 11**: at least the threshold defined by the `nAdamThreshold` consensus parameter (7 solutions on Mainnet/Regtest, 3 solutions on Testnet).
      - **Version 12**: at least the threshold defined by the `nAdamThreshold` consensus parameter (7 solutions on Mainnet/Regtest, 3 solutions on Testnet).
   - Each solution is parsed into a `nonce` and a `signature`.
   - The puzzle hash is calculated using the algorithm(s) assigned to the miner:
     - In **Version 11 (Fallback Mode)**:

       $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$

       where $\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$, using one of the **18 supported algorithms** (including `Hamsi`, `Fugue`, `Shabal`, `Whirlpool`, and `Haval-256`).
     - In **Version 12 (Standard Mode)**: Uses a 3-permutation selector scheme $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$ that deterministically selects 3 distinct hashing algorithms ($\text{algo1}$, $\text{algo2}$, and $\text{algo3}$) out of 18 available algorithms based on the previous block's hash and the miner's public key. The solver compounds the three algorithms:

       $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

       where $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
    - The `PuzzleHash` must satisfy the target difficulty defined by `nBits` (relaxed to `scaledTarget = Target \ll \text{activeShift}`, as described in Section 2.D).
   - The signature must be verified against `MinerPubKey_i` signing the `PuzzleHash`.

6. **Coordinator Signature Validation**: The `vAdamCoordinatorSig` must be verified against the expected Coordinator's public key signing the final block header hash (excluding the signature itself) using `VerifyBLSWithECDSAFallback`.

---

## 6. Wallet & RPC API Integration

To support developers and mining pool operators, the RPC API exposes the cooperative consensus fields:

### `getblocktemplate` Response
When `nVersion >= 11`, the JSON response includes:
* `version`: 11
* `adamminers`: Array of hex-encoded public keys of the elected miners.
* `adamvrfproof`: Hex-encoded VRF signature on the previous block's seed.
* `adamsolutions`: Array of hex-encoded partial solutions currently collected.
* `adamcoordinatorsig`: Hex-encoded signature of the Coordinator.

### `generate` RPC Update
The `generate` RPC handles block assembly for ADAM blocks. Instead of performing traditional high-difficulty hash grinding, it:
1. Determines if the node is elected as a Miner or Coordinator based on the rolling seed.
2. If elected as Miner, solves the lightweight partial PoW puzzle.
3. If elected as Coordinator, collects solutions, deterministic-signs the VRF proof and final block hash, and submits the finished block template to the network.

---

## 7. Proof-of-Stake (PoS) Integration & Cooperative PoS

ADAM does not replace Proof-of-Stake (PoS) but integrates with it to form a **Hybrid Cooperative PoS** consensus mechanism where PoS and PoW (ADAM cooperative mining) function together.

In traditional PoS, block production is determined solely by the staking weight (the amount of coins held in a wallet). In ADAM, this is combined with the cooperative miner-coordinator validation loop to prevent block grinding, selfish staking, and targeted leader DoS.

### The Staking and Cooperative Lifecycle (Block height $\ge$ 200)

Once the network upgrade `Consensus::UPGRADE_POS` activates (at block height 200 on Mainnet), block generation transitions from pure Cooperative PoW to Hybrid Cooperative PoS, where PoW cooperative mining and PoS staking run in parallel:

> [!IMPORTANT]
> **No Single-Signature PoS Blocks:** Proof-of-Work (ADAM cooperative mining) never ends or gets disabled in favor of PoS. Every PoS block in KristaTech is hybrid, requiring the validation of PoW puzzles solved by elected ADAM miners, and must be double-signed by the staker's key (`vchBlockSig`) and the elected ADAM coordinator's key (`vAdamCoordinatorSig`). Single-signature blocks are strictly rejected by consensus validation rules.

1. **Staking Entitlement (Kernel Check)**:
   The wallet's staking thread (`ThreadStakeMinter`) periodically evaluates if any UTXOs are eligible to stake a block by verifying the kernel hash check (proportional to coin weight).
   
2. **Cooperative Puzzle Collection**:
   Once a staking thread wins the right to propose a block, it builds a block template (Version 12, as both PoS and `UPGRADE_POMBL` are active at block height $\ge 2000$).
   The staker's wallet retrieves the elected miners for the current block height via `SelectAdamNodes` and collects the lightweight PoW puzzles solved by these elected miners from the P2P network memory cache (`mapAdamSolutionsCache`). If any elected miner's solution is missing from the local cache, the block template is deferred until all required solutions are received.

3. **Coordinator Validation and Signature**:
   The elected Coordinator of the current round validates the block template, signs the previous block's seed to produce the VRF proof (`vAdamVRFProof`), and signs the block header to generate the Coordinator signature (`vAdamCoordinatorSig`).

4. **Staker Block Signature (Dual Locking)**:
   The finalized block is secured using two distinct signature types:
   - **ADAM Signature (`vAdamCoordinatorSig`)**: Generated by the elected Coordinator to validate that the cooperative PoW mining round was completed successfully.
   - **PoS Block Signature (`vchBlockSig`)**: Generated by the staker's wallet using `SignBlock` (signing the final block hash using the private key of the staking UTXO).

### Dual Validation on the Network

When a peer receives a Cooperative PoS block, the validation rules in `CheckBlock` require both consensus checks to pass:
1. **Proof-of-Stake Verification**: The node verifies the `coinstake` transaction, checks the kernel hash target difficulty, and verifies the staker's block signature (`vchBlockSig`).

---

## 8. LLMQ (Long-Living Masternode Quorums) & DKG (Distributed Key Generation)

To validate and sign Version 12 block templates under Standard Mode, the network relies on **Long-Living Masternode Quorums (LLMQs)** which execute **Distributed Key Generation (DKG)** sessions.

### A. Quorum Topology and Parameters
* **Quorum Size**: Every active LLMQ consists of exactly **5 members** (`llmq.cpp:197`).
* **DKG Intervals**:
  - **Mainnet**: A new DKG session is executed every **100 blocks** (`GetActiveQuorum`).
  - **Testnet & Regtest**: DKG sessions are executed every **10 blocks** to accelerate testing.
* **Quorum Signature Verification Threshold (`CQuorumSignature::Verify`)**:
  - **Mainnet**: The verification threshold is set to **75%** of the quorum size (at least 2 signatures must be present).
  - **Testnet & Regtest**:
    - If the block height is below the **Model D** activation height (`UPGRADE_MODELD`), the threshold is **0 signatures** (verification is bypassed to allow bootstrapping).
    - Once Model D is active (height $\ge 500$ on Testnet, $\ge 200$ on Regtest), the threshold is enforced to be exactly **2 signatures**.

#### B. Active Masternode Filtering (Dynamic Pool)
To ensure the network is robust, decentralized, and survives local node resets:
* Quorums are elected dynamically from the pool of active, enabled Masternodes on the network.
* If fewer than 5 active masternodes are registered, the network falls back to electing quorum members from the registered miner pool (mined blocks 1-199 bootstrap or coin-lock/pow-lock registrations) dynamically.
* No hardcoded key ID filtering or local wallet restrictions are applied on Mainnet, Testnet, or Regtest, ensuring a fully decentralized and trustless test environment.
