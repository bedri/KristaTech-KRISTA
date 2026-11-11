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

To allow the network to bootstrap smoothly when the active masternode count is low, ADAM transitions deterministically across two block-version modes governed by consensus upgrade heights.

### A. Fallback Mode (Block Version 11)
Fallback Mode is designed for the bootstrap phase of the network (`UPGRADE_ADAM` $\le H <$ `UPGRADE_POMBL`). It operates without requiring active Masternode registration and is fully self-contained.
* **Miners Count ($N$)**: Dynamically determined by the size of the key pool $K-1$, where $11 \le K \le 14$ (keys are deterministic and the elected Coordinator is appended as the last element of `vAdamMiners`).
* **Consensus Threshold ($T$)**: Governed by `GetAdamThreshold(nHeight)` (`5` below `nAdamThresholdBootstrapLimit`, transitioning to `nAdamThreshold` = **`7`** on Mainnet/Regtest and **`3`** on Testnet).
* **Self-Contained Header Layout**: `vAdamMiners` stores all $K$ public keys (first $K-1$ elected miners, last key is the coordinator). `vAdamSolutions` stores the $K-1$ partial solutions.

### B. Standard Mode (Block Version 12)
Standard Mode represents the full cooperative Multi-Proof-Algorithm (PoMBL) consensus state with 3-permutation puzzle hashing.
* **Miners Count ($N$)**: Configured via the consensus parameter `nAdamMinersCount` (defined in `src/consensus/params.h` and initialized in `src/chainparams.cpp` to `11` on Mainnet/Testnet/Regtest).
* **Consensus Threshold ($T$)**: Governed by `GetAdamThreshold(nHeight)` (`5` below `nAdamThresholdBootstrapLimit`, and `nAdamThreshold` = **`7`** on Mainnet/Regtest or **`3`** on Testnet thereafter).
* **Elected Coordinator**: When active Masternodes exceed the threshold, the Coordinator is elected dynamically from the active Masternode list (`SelectAdamNodes` Rule 1) and is distinct from the 11 `vAdamMiners` entries; otherwise `SelectAdamNodes` Rule 2 elects the highest-ranked remaining pool candidate as Coordinator.

### C. Deterministic Height-Gated Activation (`Consensus::UPGRADE_POMBL`)
The transition between Fallback Mode (Version 11) and Standard Mode (Version 12) is strictly height-gated by `Consensus::UPGRADE_POMBL` (`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight` in `src/chainparams.cpp`), with no administrative Spork dependency:
* **Activation Heights**: Block **`2,000`** on Mainnet, Block **`400`** on Testnet, and Block **`300`** on Regtest.
* **Behavior**:
  - For `UPGRADE_ADAM <= nHeight < UPGRADE_POMBL`: blocks are built and validated as **Version 11** under Fallback Mode rules.
  - For `nHeight >= UPGRADE_POMBL`: `ComputeBlockVersion` produces **Version 12** blocks, and both `ContextualCheckBlockHeader` and `CheckBlock` strictly reject any block with `nVersion < 12` (`bad-version`).

### Network Configurations & Activation Heights
The transition of the cooperative consensus framework consists of three activation thresholds:
1. **`Consensus::UPGRADE_ADAM`**: Activates ADAM Fallback Mode (Block Version 11).
2. **`Consensus::UPGRADE_POMBL`**: Activates ADAM Standard Mode / Multi-Proof-Algorithm (Block Version 12).
3. **`Consensus::UPGRADE_MODELD`**: Enforces Model D reward splits and PoBLS consensus.

| Network | `UPGRADE_ADAM` (ADAM & Dual-Track Start) | `UPGRADE_ADAM_V2` (Shift V2) | `UPGRADE_POMBL` (Standard Mode / V12) | `UPGRADE_MODELD` (Model D / PoBLS) | Target Spacing |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Mainnet** | Block 200 (`nAdamDualTrackHeight = 200`) | Block 705 | Block 2000 | Block 2200 | 30 seconds |
| **Testnet** | Block 200 (`nAdamDualTrackHeight = 200`) | Block 300 | Block 400 | Block 500 | 30 seconds |
| **Regtest** | Block 200 (`nAdamDualTrackHeight = 1`) | Block 705 | Block 300 | Block 200 | 10 seconds |

### D. Puzzle Difficulty Bit-Shift Parameters
To prevent a "false-positive flood" of puzzle solutions on the network while maintaining dynamic and configurable difficulty scaling across different block heights, ADAM applies difficulty bit-shifts defined in `Consensus::Params` (`src/adam.cpp` and `src/rpc/mining.cpp`):
* **V1 Difficulty Shift (`nAdamDifficultyShiftV1`)**: Bit-shift multiplier relaxing target difficulty in Fallback Mode (`nVersion == 11` or prior to `Consensus::UPGRADE_MODELD`). Value: `10` (on Mainnet, Testnet, and Regtest).
* **V2 Difficulty Shift (`nAdamDifficultyShiftV2`)**: Bit-shift multiplier relaxing target difficulty in Standard Mode (`nVersion >= 12`) once both `Consensus::UPGRADE_ADAM_V2` and `Consensus::UPGRADE_MODELD` are active. Value: `6` (on Mainnet, Testnet, and Regtest).
* **Effective Transition Height**: Because `fFallbackMode` remains active while `nVersion == 11` or `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MODELD)`, `activeShift` transitions from `10` (`V1`) to `6` (`V2`) in production at **Block 2,200** on Mainnet and **Block 500** on Testnet (while Regtest skips puzzle target bounds in `VerifyAdamSolution` and sets `scaledTarget = ~UINT256_ZERO` for deterministic automated test solving).

During puzzle verification on Mainnet and Testnet, `scaledTarget` is derived by shifting the consensus target (`Target`) by `activeShift` and clamping to `powLimit`:

$$\text{scaledTarget} = \min\left(\text{Target} \ll \text{activeShift}, \text{powLimit}\right)$$

* $\text{activeShift} = \text{nAdamDifficultyShiftV1}$ (`10`) when `nVersion == 11`, `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_MODELD)`, or `!NetworkUpgradeActive(nHeight, Consensus::UPGRADE_ADAM_V2)`.
* $\text{activeShift} = \text{nAdamDifficultyShiftV2}$ (`6`) when `nVersion >= 12` and both `Consensus::UPGRADE_MODELD` and `Consensus::UPGRADE_ADAM_V2` are active.

### E. Starting Difficulty Limit (powLimit)
To prevent blocks 1–199 from being mined too quickly (which led to split forks and quorum deadlocks), the starting difficulty target `powLimit` is set to:

$$
\text{powLimit} = ({\sim}\text{UINT256}\underline{\hspace{0.4em}}\text{ZERO}) \gg 20
$$

On Mainnet and Testnet, this is exactly `1/2^20` (equivalent to the genesis block's `nBits` of `0x1e0ffff0`). It ensures blocks are naturally spaced out at approximately 30 seconds from genesis, allowing nodes to establish stable P2P connections and maintain a unified chain tip.

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
   * **Mainnet & Testnet**: The pool is constructed dynamically from active Masternodes and active registered miners. However, to ensure stable block production before active masternodes or registrations are established, as long as the block height is below the bootstrap limit (`nAdamBootstrapLimit` = 44,850 on Mainnet or 100,000 on Testnet), the network automatically scans the block producers (coinbase outputs) from blocks 1 to 199 and adds their public keys to the selection pool. This prevents chain stalls before active masternodes or registrations are established.
   * **Regtest**: The pool automatically bypasses external registrations and includes 15 deterministic bootstrap public keys to facilitate automated testing:

     $$
     \text{Pool}_{\text{bootstrap}} = \lbrace \text{DeterministicPubKey}_0, \dots, \text{DeterministicPubKey}_{14} \rbrace
     $$

3. **Compute a unique hash rank for each node in the selection pool based on the rolling seed**:

   $$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

4. **Sort the pool in ascending order of their $\text{Rank}_i$**.
5. **Dual-Track ($8 + 3$) Committee Election (`nAdamDualTrackHeight` = 200 on Mainnet/Testnet, 1 on Regtest; must always be set to a future block height $\ge$ `UPGRADE_ADAM` when activated on an existing chain)**:
   When `IsAdamDualTrackActive(H)` is true (`H >= consensus.nAdamDualTrackHeight`), the 11 validator seats are partitioned into two tracks via `ElectDualTrackValidators()` with bidirectional dynamic spillover:
   * **Track 1 — Collateralized Track (`ADAM_COLLATERALIZED_TRACK_QUOTA = 8` seats)**:
     - **Sub-Track 1A (`ADAM_MASTERNODE_TRACK_QUOTA = 7` seats)**: Reserved for **Active Masternodes (`4,200 KRISTA` collateral)**. Enforced at compile time via `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7)` so Active Masternodes alone can always form a post-bootstrap quorum (`GetAdamThreshold = 7`).
     - **Sub-Track 1B (`ADAM_COIN_LOCK_TRACK_QUOTA = 1` seat)**: Reserved for **`250 KRISTA` Coin-Lock** validators.
     - Immediate intra-Track-1 spillover between Sub-Track 1A and Sub-Track 1B ensures that when no Coin-Locks exist, Active Masternodes fill all 8 seats of Track 1 (`7 + 1 = 8`), and when Active Masternodes `< 7`, Coin-Locks fill the remainder of Track 1 before spilling over to Track 2.
   * **Track 2 — Open PoW Track (`ADAM_OPEN_POW_TRACK_QUOTA = 3` seats)**: Reserved for Open PoW miners (`PoW-Lock` with `10 KRISTA` CLTV lock, `Zero-Coin PoW`, and `Block 1..199` bootstrap keys). If fewer than 3 Open PoW candidates exist, unfilled seats in Track 2 spill over first to remaining Active Masternodes and then to remaining Coin-Locks.
   * This guarantees that all 11 validator seats are always filled while protecting `4,200 KRISTA` Active Masternodes (`7` validator seats + `1` Coordinator), giving `250 KRISTA` Coin-Locks a dedicated `1`-seat collateralized sub-track isolated from `10 KRISTA` `PoW-Lock` dilution, and capping Open PoW at `3` seats (`27.27%`) when the collateralized tracks are populated.
   * **Standby Validator Seat Handover & Time-Relaxed Quorum Threshold (`ResolveAdamCommitteeForTime`, `ValidateAdamCommitteeForTime`, `GetAdamEffectiveThreshold`)**:
     - If a block is delayed by $\ge 60\text{s}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`, advancing in `ADAM_COMMITTEE_FALLBACK_SLOT_SECS = 30` slots), any silent seat in the 11-validator committee that has not submitted a solution may be filled ONLY by a **same-track** standby validator (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) from `vStandbyMiners` in strict sequential rank order within that track (`k_t > prevStandbyIdxInTrack[t]` and `k_t < nReplacedInTrack[t] + nSlot`), without displacing any honest primary validator that already solved its puzzle. Both `ResolveAdamCommitteeForTime` and `ValidateAdamCommitteeForTime` resolve validator tracks against the block's `CCoinsViewCache* pView` so reorg and initial-sync validation match `SelectAdamNodes` identically.
     - Under Model D (`IsModelDActive`), **Equal Work, Equal Reward** applies across all 11 committee seats (`ComputeModelDValidatorSeatPayments` in `src/masternode-payments.cpp`): every seat $i \in \lbrace 0, \dots, 10 \rbrace$ that provides a valid PoW solution in `vAdamSolutions[i]` — whether filled by the original primary validator `vExpectedMiners[i]` OR by a legitimate same-track standby validator `vAdamMiners[i]` when the primary seat timed out ($t \ge 60\text{s}$) — receives **100% of its own validator track's per-seat reward** out of the 40% Participant Validator pool (20% across the 7 Masternode Validator quota seats at `~2.857%` each + 20% across the 4 Independent Miner quota seats [`8.00%` for 1 Coin-Lock + `4.00%` each for 3 Open PoW]). If dynamic spillover places an Open PoW miner into an unfilled Coin-Lock seat, that miner receives its own Open PoW rate (`4.00%`, leaving the `4.00%` differential in `nUnclaimedPartSplit`); similarly, an Active Masternode spilling over into a Coin-Lock or Open PoW seat receives its Masternode Validator rate (`~2.857%`, leaving `~5.143%` or `~1.143%` unclaimed). Unsolved/empty seats (`vAdamSolutions[i].empty()`) receive `0%`, while the Coordinator retains its full `15%` reward (`nProducerSplit`) when rescuing a stalled block with standby validators. Any unclaimed validator shares (`nUnclaimedPartSplit = (nPartSplitTotal - nPaidPartTotal)`, plus the `10%` `nLLMQSplit` if `vLlmqPayees` is ever empty) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** while the Passive Masternode Winner receives its exact `35%` (`nMNSplit`). Furthermore, during LLMQ block proposal validation (`qblockprop`, evaluated only when the node's current tip matches `msg.block.hashPrevBlock`), if $t \ge 60\text{s}$ and a primary seat $i$ is **substituted** by a standby key (`vAdamMiners[i] != vExpectedMiners[i]`) while the LLMQ member already holds a valid cached solution from `vExpectedMiners[i]`, the LLMQ member rejects the proposal and refuses to sign `QUORUMSIGSHARE` (empty seats `vAdamMiners[i] == vExpectedMiners[i]` are not vetoed so normal fast blocks are never delayed by in-flight P2P race conditions).
     - If a block remains stalled for $\ge 120\text{s}$ (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = 120`), `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` relaxes the required quorum threshold by `1` every `60s` (`ADAM_THRESHOLD_RELAXATION_SLOT_SECS = 60`) down to `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 1 Coin-Lock + 3 Open PoW = 4`, enforced via `static_assert` and runtime `assert` so at least 1 Active Masternode is always required) whenever the block is post-bootstrap (`nHeight >= nAdamThresholdBootstrapLimit`) OR active Masternodes in the pool are $\ge 7$. Only during the bootstrap period (`nHeight < nAdamThresholdBootstrapLimit`) when fewer than 7 Active Masternodes exist does the floor relax to `ADAM_MIN_RELAXED_THRESHOLD_BOOTSTRAP = 3`. Header-only validation (`ContextualCheckBlockHeader`) passes `0` for `nActiveMasternodes` (since the block's UTXO view is not yet available at header check time, avoiding false bans during sync/reorg), while full block validation (`CheckFormsOfWork`) strictly enforces `GetActiveMasternodePoolCount(nHeight - 1, pView)` against the block's actual UTXO view.
     - Node-wide version-independent single-block-per-parent guard (`ActiveProposalGuard` in `src/miner.cpp`) prevents any node from concurrently or sequentially producing both a PoW block and a PoS block for the same `(parentHash, coordinatorKey)` across all block versions (`v11` and `v12`).
6. **Coordinator Election**:
   * **Rule 1 (`Active Masternodes > Threshold`)**: The highest-ranked Active Masternode is elected as the **Coordinator**, and the 11 **Miners** are elected from the remaining pool via `ElectDualTrackValidators`.
   * **Rule 2 (`Active Masternodes <= Threshold`)**: The 11 **Miners** are elected via `ElectDualTrackValidators`, and the highest-ranked non-elected key in the pool is elected as the **Coordinator**.

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

For each round $i \in \lbrace 0, \dots, M-1 \rbrace$:
1. Derive a unique round hash from the previous block hash and round index:

   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$

2. Extract the first byte $v_i = \text{roundHash}_i[0]$.
3. Calculate an odd coprime multiplier $m_i$:

   $$m_i = v_i \mid 1$$

   If $m_i < 3$, set $m_i = 3$. This ensures the multipliers are coprime to $2^{256}$, preserving 100% entropy.
4. Perform the round hashing:
   - **Fallback Mode (Version 11)** (where $\text{algo}_i = \text{Hash}(\text{hashPrevBlock} \mathbin{\Vert} \text{vAdamMiners}[i]) \pmod{18}$):
     - **Round 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$, setting $H_{\text{prev}} = H_0$.
     - **Round $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{\text{prev}}) \times m_i \pmod{2^{256}}$, updating $H_{\text{prev}} = H_i$.
   - **Standard Mode (Version 12)**:
     - **Round 0**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ via `GetAdam3PermutationAlgos(hashPrevBlock, vAdamMiners[0])`.
       * Compute:

         $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, \text{SerializedHeader})$$

         $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$

         $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$

       * Apply multiplier:

         $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$

     - **Round $i > 0$**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ via `GetAdam3PermutationAlgos(hashPrevBlock, vAdamMiners[i])`.
       * Compute:

         $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$

         $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

         $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

       * Apply multiplier:

         $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

The final hash $H_{\text{prev}}$ after round $M - 1$ is returned as the block hash.

---

## 5. Consensus Validation Rules

When a block is received, `CheckBlock()` and `CheckFormsOfWork()` in `src/main.cpp` enforce the following validations if the ADAM network upgrade (`Consensus::UPGRADE_ADAM`) is active:

1. **Version Enforcement & Downgrade Prevention**:
   - When `Consensus::UPGRADE_ADAM` is active (`nHeight >= 200`), the block version must be at least `11`.
   - When `Consensus::UPGRADE_POMBL` is active (`vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight`: `2,000` on Mainnet, `400` on Testnet, `300` on Regtest), both `ContextualCheckBlockHeader` and `CheckBlock` strictly require `block.nVersion >= 12` and reject any `block.nVersion < 12` block with a `bad-version` DoS error to prevent downgrade attacks.

2. **Miners & Solutions Sizing**:
   - **Version 11 (Fallback Mode)**:
     - `vAdamMiners` size must be between 11 and 14 (`nAdamThreshold + 1` to `14`).
     - `vAdamSolutions` size must be `vAdamMiners.size() - 1` (or `vAdamMiners.size()`, with only the first `vAdamMiners.size() - 1` committee slots evaluated).
     - The Coordinator to verify is the last element of `vAdamMiners` (`vAdamMiners.back()`).
   - **Version 12 (Standard Mode)**:
     - `vAdamMiners` size must match exactly `nAdamMinersCount` (`11`).
     - `vAdamSolutions` size must match exactly `vAdamMiners.size()` (`11`).
     - The Coordinator is derived from `SelectAdamNodes` (or `GetAdamFallbackCoordinatorForSlot` when $t \ge 45\text{s}$).

3. **VRF Proof Validation**: The `vAdamVRFProof` must be verified against the previous rolling seed $\text{Seed}_{H-1}$ and must be a valid signature matching the public key of the expected Coordinator using `VerifyBLSWithECDSAFallback`.

4. **Election Path Validation**: On Mainnet and Testnet, in both Version 11 (`vAdamMiners[0..M-1]`) and Version 12 (`vAdamMiners[0..10]`), the committee public keys must match `SelectAdamNodes` (or a valid same-track standby handover verified via `ValidateAdamCommitteeForTime` when $t \ge 60\text{s}$); only Regtest skips the Version 11 election path check for self-contained unit tests.

5. **Partial Solutions Validation**:
   - The number of valid partial solutions must meet the effective quorum threshold governed by `GetAdamEffectiveThreshold(nHeight, nBlockTimeDiff, nActiveMasternodes)` (whose baseline `GetAdamThreshold(nHeight)` is `5` below `nAdamThresholdBootstrapLimit` [block 50,000 on Mainnet, 100,000 on Testnet, 1,000 on Regtest] and `nAdamThreshold` [`7` on Mainnet/Regtest, `3` on Testnet] thereafter, relaxing at $t \ge 120\text{s}$ as detailed in Section 3).
   - Each solution is parsed into a `nonce` and a `signature`.
   - The puzzle hash is calculated using the algorithm(s) assigned to the miner:
     - In **Version 11 (Fallback Mode)**:

       $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i\right)$$

       where $\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$, using one of the **18 supported algorithms** (including `Hamsi`, `Fugue`, `Shabal`, `Whirlpool`, and `Haval-256`).
     - In **Version 12 (Standard Mode)**: Uses a 3-permutation selector scheme $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$ that deterministically selects 3 distinct hashing algorithms ($\text{algo1}$, $\text{algo2}$, and $\text{algo3}$) out of 18 available algorithms based on the previous block's hash and the miner's public key. The solver compounds the three algorithms:

       $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

       where $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
    - The `PuzzleHash` must satisfy the target difficulty defined by `nBits` (relaxed to $\text{scaledTarget} = \text{Target} \ll \text{activeShift}$, as described in Section 2.D).
   - The signature must be verified against `MinerPubKey_i` signing the `PuzzleHash`.

6. **Coordinator Signature Validation**: The `vAdamCoordinatorSig` must be verified against the expected Coordinator's public key signing the final block header hash (excluding the signature itself) using `VerifyBLSWithECDSAFallback`.

---

## 6. Wallet & RPC API Integration

To support developers and mining pool operators, the RPC API exposes the cooperative consensus fields:

### `getblocktemplate` Request & Response
External miners (`cpuminer-opt -a adam`, `ccminer -a adam`) may pass a JSON template request object containing `"mineraddress"` (`{"mineraddress": "<KRISTA_address>"}`) to target a specific registered validator or Masternode operator seat held by the wallet.
When `nVersion >= 11`, `getblocktemplate` operates in two response modes depending on the caller's role in the current round:
* **External Committee Puzzle Work Response** (returned when a locally held validator key or the requested `"mineraddress"` is elected as a committee validator and has not yet submitted a puzzle solution for the current tip):
  * `capabilities`, `version` (`11` in Fallback Mode or `12` in Standard PoMBL Mode), `previousblockhash`, `transactions` (`[]`), `bits`, `height`, `curtime`
  * `target`: Hex-encoded 256-bit relaxed puzzle target (`scaledTarget = Target << activeShift`).
  * `puzzleheader`: 80-byte hex-encoded puzzle input prefix (`32-byte adamSeed || 0x21 || 33-byte compressed myMinerKey || 10-byte zero padding || 4-byte nonce slot`).
  * `powalgo`: Assigned puzzle algorithm descriptor — `"<algo>"` in Version 11 (e.g., `"hamsi"`) or `"<algo1>+<algo2>+<algo3>+<minerIdx>"` in Version 12 (e.g., `"whirlpool+fugue+haval+0"`).
* **Waiting Response** (returned when no local/specified validator key is elected in the current round, its puzzle is already solved for the current tip, or the active pool is empty — preventing wasted CPU/GPU cycles without falling back to classic BIP22):
  * `capabilities`, `version`, `previousblockhash`, `transactions` (`[]`), `height`, `curtime`, and `"status": "waiting_for_election"`.
* **Coordinator Full Block Template vs. `getblock` JSON Fields**:
  * When `getblocktemplate` assembles a full Coordinator block template (`src/rpc/mining.cpp`), it exposes lowercase `adamminers` (array of hex-encoded compressed public keys), `adamsolutions`, `adamvrfproof`, and `adamcoordinatorsig`.
  * When `getblock` serializes a confirmed or candidate block via `blockToJSON` (`src/rpc/blockchain.cpp`), it exposes camelCase `vAdamMiners` (array of Base58Check KRISTA addresses via `EncodeDestination(miner.GetID())`), `vAdamSolutions`, `vAdamVRFProof`, and `vAdamCoordinatorSig`.

### `generate` RPC & Qt Dashboard Miner Hub
The `generate` / `setgenerate` RPCs and the **KristaTech-Qt Dashboard Miner Hub** handle cooperative block assembly and puzzle solving for ADAM blocks. Instead of performing traditional high-difficulty hash grinding, the node:
1. Determines which local wallet/Masternode keys are elected as Committee Validators or Coordinator based on the rolling seed.
2. Solves lightweight partial PoW puzzles across all locally held elected validator seats and broadcasts `ADAMSOL` messages.
3. If elected as Coordinator, collects committee solutions, pays the PoW Block Producer coinbase share (`vout[0]`) directly to the elected Coordinator's address (or its Masternode cold collateral payout address when backed by `4,200 KRISTA` collateral), deterministically signs the VRF proof and final block hash (requesting a 3-of-5 LLMQ `vQuorumSig` once Model D is active), and submits the finished block to the network. The Qt Dashboard Miner Hub exposes 1-click CPU mining, non-blocking 1-click `PoW-Lock` / `Coin-Lock` registration, live committee role badges, separate paid reward (`🏆`) vs. solved committee puzzle (`🧩`) counters, and 1-click masked `cpuminer` command copy.

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
   
2. **Cooperative Puzzle Collection & Alternating Slot-Opening Priority**:
   At each 15-second `TimeProtocolV2` slot opening (`ADAM_SLOT_PRIORITY_WINDOW_MS = 1500` ms in `src/miner.cpp`), PoW and PoS alternate priority based on the parent block type (`pindexPrev->IsProofOfStake()`): after a PoW block, PoS stakers proceed immediately (`0ms`) while PoW coordinators (`BitcoinMiner` and `TryAssembleCoordinatorBlockAsync`) wait `1500ms`; after a PoS block, PoW coordinators proceed immediately (`0ms`) while PoS stakers (`ThreadStakeMinter`) wait `1500ms` (with seamless fallback to the other side inside the same 15-second slot if no block is produced during the priority window). Once a staking thread proceeds and wins a valid kernel, it builds a candidate block template (`Version 11` below `UPGRADE_POMBL`, `Version 12` at or above `UPGRADE_POMBL`).
   The staker retrieves the elected miners via `SelectAdamNodes` (or `ResolveAdamCommitteeForTime` at $t \ge 60\text{s}$) and collects partial PoW puzzle solutions from `mapAdamSolutionsCache`, deferring template finalization until the effective quorum threshold (`GetAdamEffectiveThreshold`) is met.

3. **Decentralized Coordinator Validation & Co-Signing (`POSCOORDREQ` / `POSCOORDSIG`)**:
   - **Local Coordinator**: If the active Coordinator key (`GetAdamFallbackCoordinatorForSlot` at $t \ge 45\text{s}$ or primary `expectedCoordinator` at $t < 45\text{s}$) is held locally on the staker's node, the node reserves `AcquireCoordinatorProposalSlot` and directly generates `vAdamVRFProof` and `vAdamCoordinatorSig`.
   - **Remote Coordinator (P2P Co-Signing)**: If the active Coordinator resides on a separate Masternode or validator node, the staker signs `ComputePoSCoordRequestHash` with the coinstake input's ECDSA key (`SignPoSCoordRequest`) and broadcasts a `POSCOORDREQ` P2P message (relayed multi-hop across peers). Nodes verifies `CheckBlockTime`, `CheckBlock(..., fCheckSig=false)`, `ContextualCheckBlock`, `CheckProofOfStake`, and `ConnectBlock(..., fJustCheck=true)` before relaying; the active Hot Coordinator additionally verifies `VerifyPoSCoordRequestAuth` and `TestBlockValidity`, reserves its `AcquireCoordinatorProposalSlot` (preventing competing PoW+PoS proposals for the same `(parentHash, coordinatorKey)`), signs `vAdamVRFProof` + `vAdamCoordinatorSig`, and broadcasts `POSCOORDSIG` back across P2P. Once Model D is active, the staker then requests the 3-of-5 LLMQ `vQuorumSig` (`RequestBlockQuorumSig`).

4. **Staker Block Signature (Dual Locking)**:
   The finalized block is secured using two distinct signature types:
   - **ADAM Signature (`vAdamCoordinatorSig`)**: Generated by the active Coordinator (locally or via `POSCOORDSIG`) to certify the cooperative PoW round and VRF seed transition.
   - **PoS Block Signature (`vchBlockSig`)**: Generated by the staker's wallet using `SignBlock` (signing the final block hash with the private key of the staking UTXO).

### Dual Validation on the Network

When a peer receives a Cooperative PoS block, the validation rules in `CheckBlock` require both consensus checks to pass:
1. **Proof-of-Stake Verification**: The node verifies the `coinstake` transaction, checks the kernel hash target difficulty, and verifies the staker's block signature (`vchBlockSig`).
2. **ADAM Cooperative PoW Verification**: The node verifies the elected miners' partial PoW puzzle solutions (`vAdamSolutions` meeting the quorum threshold $T$), the Coordinator's VRF proof (`vAdamVRFProof`), and the Coordinator's signature (`vAdamCoordinatorSig`), as well as the LLMQ quorum signature (`vQuorumSig`) in Version 12 blocks once Model D is active.

---

## 8. LLMQ (Long-Living Masternode Quorums) & DKG (Distributed Key Generation)

To validate and sign Version 12 block templates under Standard Mode, the network relies on **Long-Living Masternode Quorums (LLMQs)** which execute **Distributed Key Generation (DKG)** sessions.

> [!NOTE]
> **Quorum Threshold Distinction:**
> Do not confuse the **ADAM Cooperative Mining Threshold** (7 out of 11 rule for puzzle solutions `vAdamSolutions`) with the **LLMQ Signature Verification Threshold** (3 out of 5 rule for block signature `vQuorumSig`):
> * **ADAM Threshold**: Enforces that at least 7 out of 11 elected miners (`nAdamThreshold = 7` on Mainnet/Regtest, `3` on Testnet; `5` during the initial bootstrap period below `nAdamThresholdBootstrapLimit`) must solve and submit their PoW puzzle solutions (`vAdamSolutions`) for the block to be accepted. This is part of the ADAM cooperative puzzle validation.
> * **LLMQ Threshold**: Enforces that at least 3 out of 5 LLMQ members (`quorum.members.size() / 2 + 1`) must sign the proposed block hash using their deterministic BLS12-381 keys (`DeriveBLSFromCKey`, authorized by `secp256k1` identity keys and aggregated into `vQuorumSig` via `CQuorumSignature`). This is part of the decentralized block validation.

### A. Quorum Topology and Parameters
* **Quorum Size**: Every active LLMQ consists of exactly **5 members** (`llmq.cpp:218`).
* **DKG Intervals & Per-Block Rotation**:
  - **Initial Epoch Mode**: A new DKG session is executed every **100 blocks** on Mainnet and every **10 blocks** on Testnet/Regtest (`GetActiveQuorum`).
  - **Per-Block Quorum Rotation**: Once quorum rotation activates (at block height **1,400** on Mainnet, **1,000** on Testnet, and **800** on Regtest), `GetActiveQuorum` runs `RunDKG(nHeight)` at each block height using the epoch seed from `((nHeight / dkgInterval) * dkgInterval) - 1`.
* **Quorum Signature Verification Threshold (`CQuorumSignature::Verify`)**:
  - **All Networks (Mainnet, Testnet, Regtest)**: Once Model D (`UPGRADE_MODELD`) is active, `CQuorumSignature::Verify` enforces a BFT majority threshold of `quorum.members.size() / 2 + 1` (minimum 2, capped at `quorum.members.size()`), which requires at least **3 out of 5** quorum signatures.
  - **Prior to Model D Activation**: On Mainnet (blocks 2,000–2,199) and Testnet (blocks 400–499), quorum signature verification is bypassed in `CheckBlock()` (`fFallbackMode`) to allow initial masternode accumulation. On Regtest, Model D activates at height 200 (before `UPGRADE_POMBL` at height 300), so the 3-of-5 majority threshold is active from block 300 onward.

### B. Active Masternode Filtering (100% On-Chain Deterministic Pool)
To ensure the network is robust, decentralized, and immune to P2P memory-state divergence:
* Quorums and ADAM validator elections are resolved 100% deterministically from verified on-chain Masternode state (`mapMasternodeCollateralOutpoints`, `mapOnChainCollateralDelegations`, and 4,200 KRISTA collateral verification in `pcoinsTip`), with zero fallback to in-memory P2P `mnodeman`.
* If fewer than 5 on-chain active masternodes are registered, the network deterministically falls back to electing quorum members from the registered miner pool (`GetAdamMinerPool(nHeight - 1)` — mined blocks 1-199 bootstrap or coin-lock/pow-lock registrations).
* No hardcoded key ID filtering or local wallet restrictions are applied on Mainnet, Testnet, or Regtest, ensuring a fully decentralized and trustless consensus environment.

### C. Deterministic Epoch-Seed Hash Ranking & Per-Block Liveness Evaluation
To guarantee 100% consensus agreement on quorum membership across all nodes while protecting quorum members against targeted DoS/DDoS attacks (`ElectQuorumMembers` in `src/llmq.cpp`):
- Each on-chain active Masternode (verified via `IsMasternodeActiveAtHeight(..., 2880)` and `IsValidMasternodeCollateralAndAuth`) is scored deterministically using the rolling ADAM seed and its collateral outpoint:

  $$\text{Score}_i = \text{SHA256D}\left(\text{Seed}_{\text{seedHeight}} \mathbin{\Vert} \text{collateralTxid}_i \mathbin{\Vert} \text{collateralVout}_i\right)$$

- Candidates are sorted in ascending order of $\text{Score}_i$, and the **top 5** members form the active quorum (`nQuorumSize = 5`). If fewer than 5 on-chain active Masternodes exist, `ElectQuorumMembers` deterministically populates the candidate list from `GetAdamMinerPool(nHeight - 1, pView)`.
- **Epoch Seed & Per-Block Liveness**: Prior to rotation activation, `GetActiveQuorum` runs `RunDKG(nDkgHeight)` at `nDkgHeight = max(vUpgrades[UPGRADE_POMBL].nActivationHeight, (nHeight / dkgInterval) * dkgInterval)` using `seedHeight = nDkgHeight - 1`. Once epoch-seed rotation activates, `GetActiveQuorum` evaluates on-chain Masternode liveness at **every block height** (`RunDKG(nHeight)`) using the deterministic epoch seed at `seedHeight = ((nHeight / dkgInterval) * dkgInterval) - 1` (`dkgInterval = 100` on Mainnet, `10` on Testnet/Regtest), preserving stable epoch DKG identity while immediately rotating out any Masternode whose collateral is spent or whose 2,880-block heartbeat expires:
  - **Regtest**: Block height $\ge 800$
  - **Testnet**: Block height $\ge 1,000$
  - **Mainnet**: Block height $\ge 1,400$

---

## 9. Offline Coordinator Fallback Rotation (`GetAdamFallbackCoordinatorForSlot`)

To prevent chain stalls across **both** Fallback Mode (Block Version 11) and Standard Mode (Block Version 12, including Pre-Model-D Blocks `2,000..2,199` and Model D Blocks $\ge 2,200$) whenever the primary elected Coordinator goes offline:

### A. Primary Coordinator Exclusivity Window ($t < 45\text{s}$)
For the first **45 seconds** elapsed since the parent block's timestamp (`nBlockTimeDiff < ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45`):
* **Version 12 (`nHeight >= UPGRADE_POMBL`)**: Both `CheckBlock` (`src/main.cpp`) and block assembly (`src/miner.cpp`) strictly enforce that **only** the primary `expectedCoordinator` elected by `SelectAdamNodes` may sign `vAdamVRFProof` and `vAdamCoordinatorSig`. Early fallback rotation at $t < 45\text{s}$ fails Coordinator signature verification and is rejected (`bad-adam-coord-sig`).
* **Version 11 (`UPGRADE_ADAM <= nHeight < UPGRADE_POMBL`)**: Miners and stakers wait out the 45-second primary window before rotating `vAdamMiners.back()`, while `CheckBlock` validates the self-contained Coordinator key at `vAdamMiners.back()`.

### B. PoBLS XOR-Ranked Fallback Rotation ($t \ge 45\text{s}$)
Once `nBlockTimeDiff >= 45` seconds, coordinator authority rotates in **30-second slots** across the elected committee validators that have submitted valid puzzle solutions (`GetAdamFallbackCoordinatorForSlot` in `src/adam.cpp`):
1. Solved committee validators are ranked deterministically in ascending order of their PoBLS XOR distance $D_i = |T_i \oplus T_{\text{target}}|$ (`RankValidatorsByPoBLSDistance`), falling back to committee order if tickets are unavailable.
2. For slot `nRawSlot = (nBlockTimeDiff - 45) / 30` (with `nSlotOffset = (nBlockTimeDiff - 45) % 30`), the active fallback Coordinator is selected modulo the ranked validator list (`activeFallback`), and `CheckBlock` accepts `expectedCoordinator`, `activeFallback`, or—during the 5-second slot-boundary grace window (`nRawSlot > 0 && nSlotOffset < 5`)—the immediately preceding slot's `prevFallback` to tolerate minor P2P propagation jitter across 30-second boundaries without risking chain stalls.
3. In Version 12, `vAdamMiners` always retains its exact `nAdamMinersCount = 11` committee validator layout (`vAdamSolutions.size() == 11`); the fallback Coordinator signs `vAdamVRFProof` and `vAdamCoordinatorSig` directly without altering `vAdamMiners` sizing.

---

## 10. Consensus Finality, Fork Resolution & Performance Hardening (v1.4.5.9)

### A. Deterministic Equal-Work Fork Tie-Breaker (`CBlockIndexWorkComparator`)
In a 30-second cooperative PoW/PoS hybrid chain, network partitions or simultaneous fallback rotations can occasionally produce competing blocks at the same height with identical cumulative chain work (`nChainWork`). Previously, `CBlockIndexWorkComparator` in `src/chain.h` fell back to sequence ID (`nSequenceId` — order of local receipt) before comparing block hashes, causing partitioned subsets of nodes to remain stuck on different equal-work tips until the next block was mined.
* **Rule**: When `pa->nChainWork == pb->nChainWork`, the comparator deterministically prefers the block index with the numerically smaller block hash (`pa->GetBlockHash() < pb->GetBlockHash()`) before checking `nSequenceId`.
* **Effect**: All peers immediately converge on the exact same canonical chain tip (`< 1s` convergence time) as soon as competing blocks propagate across a healed partition.

### B. LLMQ Anti-Equivocation Guard & Multi-Hop Gossip Relay
* **Per-Parent Anti-Equivocation (`llmq::AllowQuorumSignature` / `mapSignedProposalByPrev`)**: In `src/llmq.cpp`, each LLMQ quorum member tracks `mapSignedProposalByPrev[prevHash]`. Once a quorum member signs a valid `PROPOSEBLOCK` for a given `prevHash`, it blocks competing coordinators from obtaining a quorum signature on a rival block for `QUORUM_EQUIVOCATION_GUARD_SECS = 30` seconds (while permitting a retry by the same coordinator after `12` seconds if block propagation failed). This guarantees that two competing blocks in the same 30-second slot cannot both collect a 3-of-5 quorum certificate (`vQuorumSig`).
* **Multi-Hop Gossip Relay (`setSeenProposedBlocks`, `mapQuorumBlockSigs`)**: `PROPOSEBLOCK` (`qblockprop`) and `QUORUMSIGSHARE` (`qsigshare`) P2P messages are deduplicated and relayed across all connected peers, ensuring quorum members and block proposers receive proposals and signature shares even when not directly connected in a single hop.
* **100% On-Chain Fallback in `ElectQuorumMembers`**: When the number of Masternodes with recent on-chain heartbeat pings (within the 2,880-block activity window) and unspent 4,200 KRISTA collateral is less than `nQuorumSize` (5), `ElectQuorumMembers` deterministically falls back to the on-chain `GetAdamMinerPool(nHeight - 1, pView)` candidate pool without touching in-memory P2P `mnodeman` state.

### C. Authenticated On-Chain Masternode Heartbeat Pings (`ExtractOnChainMasternodePing` & `SendOnChainPing`)
* **Compressed (33-byte) and Uncompressed (65-byte) Public Key Support**: `ExtractOnChainMasternodePing` in `src/adam.cpp` validates both 36-byte (`0x21 <33-byte compressed pubkey> OP_CHECKSIGVERIFY OP_1`) and 68-byte (`0x41 <65-byte uncompressed pubkey> OP_CHECKSIGVERIFY OP_1`) MESCAL `check-signature-verification` heartbeat scripts alongside the serialized `CDataStream` `OP_RETURN` proof (`collateralOutpoint` + `vchPingSig` over `Hash(collateralOutpoint || tx.vin[0].prevout)` + optional `bool hasDelegation` + `COnChainMasternodeDelegation`).
* **Relay Fee Exemption (`0.0001 KRISTA`)**: Authenticated on-chain Masternode ping transactions are exempt from the `1000x` `OP_RETURN` relay fee multiplier in `GetMinRelayFee` (`src/main.cpp`), allowing Hot Nodes to broadcast periodic on-chain heartbeat pings for a flat `0.0001 KRISTA` fee.

### D. Lock-Free Chain Tip Access (`CChain::AtomicTip()`)
* `CChain` (`src/chain.h` / `src/chain.cpp`) maintains an atomic pointer `std::atomic<CBlockIndex*> pAtomicTip` updated inside `CChain::SetTip()`.
* `GetChainTip()` (`src/main.cpp`) reads `chainActive.AtomicTip()` without acquiring `cs_main`, eliminating lock contention between P2P message processing, RPC queries, and staking/mining threads.

### E. Decentralized Cooperative PoS Co-Signing, Cryptographic Future Queue & Single-Dispatcher Solver (`Rev 40–42`)
* **Decentralized `POSCOORDREQ` / `POSCOORDSIG` P2P Co-Signing (`K4` / `R42-F2`)**: Enables PoS stakers to obtain `vAdamVRFProof` and `vAdamCoordinatorSig` over P2P when the active Coordinator resides on a remote node, verified via `VerifyPoSCoordRequestAuth` and `TestBlockValidity`, guarded against dual PoW+PoS proposals via `AcquireCoordinatorProposalSlot`, and verified on `POSCOORDSIG` receipt against `mapSeenPoSCoordReqs` (`targetCoordinator`, `draftHeader`, and parent `adamSeed` via `VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`) prior to caching or P2P relay.
* **Bounded `mapFutureBlocks` Cryptographic Pre-Verification (`K1` / `R42-F1`)**: `IsFutureBlockPreVerified(header, pindexPrev)` requires a known parent in `mapBlockIndex` (`header.GetBlockTime() > pindexPrev->MinPastBlockTime()`), verifies `VerifySelfContainedBLSSignedData` (`vrfBlsPk == coordBlsPk`) and `GetAdamMinerPool` validator ownership (`VerifyAdamVRFProof` + `VerifyAdamCoordinatorSig`) on `nVersion >= 11`, and verifies 3-of-5 `CQuorumSignature::Verify` against `llmq::GetActiveQuorum` on `nVersion >= 12` when `IsModelDActive`, while preserving queued full `CBlock` entries against `HEADERS` overwrites and deferring `AddInventoryKnown` until `ProcessFutureBlocks`.
* **Single-Dispatcher Strided Solver, Deterministic `DeriveBLSFromCKey` & Lifecycle-Safe Coordinator Assembly (`O2`–`O5` / `R42-F3`)**: Coordinates multi-core `BitcoinMiner` puzzle hashing without duplicate nonce work, unifies deterministic `DeriveBLSFromCKey` and shared `CMinerRegistrationSlotGuard` across `AutoRegisterMiner` and `registerminer`, joins `TryAssembleCoordinatorBlockAsync` cleanly across `GenerateBitcoins` transitions and post-`g_connman.reset()` shutdown, and adds Version 12 3-permutation compound puzzle support (`<a1>+<a2>+<a3>+<minerIdx>`) to `ccminer`.

