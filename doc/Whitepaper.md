# KristaTech (KRISTA) Technical Whitepaper
## A Quorum-Resilient Cooperative Hybrid Consensus Blockchain with Proof-of-BLS (PoBLS) Proposer Selection and JSON-Compiled Declarative Smart Contracts (MESCAL)

**Abstract**  
This paper introduces the KristaTech (KRISTA) blockchain protocol, a decentralized platform designed to address consensus centralization, leader-targeted Denial-of-Service (DoS) attacks, and the architectural complexities of traditional virtual machines. KristaTech implements a two-tier consensus mechanism that decouples validator set selection from block proposal. The selection layer, **ADAM (A Decentralized Approach Model)**, utilizes a Verifiable Random Function (VRF) rolling seed to deterministically elect a set of $N$ validators and 1 coordinator per block height. The proposal layer, **Proof of BLS (PoBLS)**, holds an ephemeral cryptographic lottery via XOR distance calculation to dynamically select the block producer from the elected validator pool, securing the network against pre-computation and proposer-targeted DoS attacks. To maintain chain liveness under network partitions or offline validators, we introduce a quorum-resilient responding mechanism utilizing empty vector placeholders (`std::vector<unsigned char>()`) verified against a dynamic threshold. Smart contracts are executed using **MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)**, a simple, declarative, JSON-based specification that compiles directly into stack-based `CScript` bytecode, eliminating state-based vulnerabilities and gas calculation overhead. Finally, we propose a sustainable **210 Million KRISTA** emission model governed by a **1.9% quarterly (~90 days) decay** and the **Model D Cooperative Split**, which fairly distributes net block rewards among passive masternodes (35%), active LLMQ quorum members (10%), the block producer (15%), and validator participants (40%: 20% across 7 Masternode validator seats and 20% across 4 independent miner seats), alongside institutional developer and faucet funding allocations.

**Keywords**: Consensus Protocols, Blockchain Security, Cryptographic Lottery, Declarative Smart Contracts, Tokenomics.

---

## 1. Introduction and Background

Decentralized consensus protocols are fundamentally tasked with solving the Byzantine Generals Problem in an adversarial, open-network environment. Traditional consensus designs, primarily Proof of Work (PoW) and Proof of Stake (PoS), have successfully demonstrated network coordination but suffer from critical structural weaknesses:

1. **Consensus Centralization**: In PoW, the economies of scale lead to the concentration of hashing power in a few industrial mining pools. In PoS, wealth concentration ("rich-get-richer" dynamics) permits dominant coin holders to govern block production.
2. **Leader Targetability**: In protocols where the next block proposer is pre-elected or predictable, attackers can launch targeted Denial-of-Service (DoS) attacks or bribe the proposer to censor transactions, compromising network liveness.
3. **Block Grinding Attacks**: If block headers or transactions can be manipulated to influence the entropy source of future leader elections, adversarial nodes can grind blocks to artificially increase their selection probability.
4. **Smart Contract Simplicity**: Traditional virtual machines introduce significant complexity in state execution, causing vulnerabilities and unpredictable transaction execution. A simpler, predictable contract language is required to guarantee state transitions.

KristaTech addresses these challenges through a dual-consensus cooperative architecture. By splitting consensus into a **deterministic selection layer** (ADAM) and an **unpredictable proposing layer** (PoBLS), KristaTech achieves high throughput, DDoS resilience, and robust Sybil resistance. Contract execution is restricted to the deterministic and simple **MESCAL** declarative language, maintaining blockchain state security. The ecosystem is supported by a deflationary, long-lifecycle tokenomics model designed for sustainable network maintenance over decades.

---

## 2. Consensus Architecture

The core of KristaTech is a cooperative consensus process. The block lifecycle is split into an **Election Phase**, a **Solving Phase**, and an **Aggregation Phase**.

```
+-----------------------------------------------------------------------+
|                           ADAM Selection Layer                        |
|   1. Roll VRF Seed: Seed_H = Hash(Seed_{H-1} || VRFProof_{H-1})       |
|   2. Rank Nodes: Rank_i = Hash(Seed_H || PubKey_i)                    |
|   3. Elect: Top N Nodes -> Validators (Miners); Next -> Coordinator   |
+-----------------------------------++----------------------------------+
                                    ||
                                    \/
+-----------------------------------------------------------------------+
|                            PoBLS Proposer Layer                       |
|   1. Ephemeral BLS Keypair: (sk_i, pk_i)                              |
|   2. Ticket Generation: T_i = Hash(sk_i || pk_i || H)                 |
|   3. Ticket Distance: D_i = |T_i XOR T_target|                        |
|   4. Winner: Node with min(D_i) receives LLMQ Quorum certification    |
+-----------------------------------++----------------------------------+
                                    ||
                                    \/
+-----------------------------------------------------------------------+
|                         Aggregation & Validation                      |
|   1. Block Producer builds block, attaching validator partial PoW.    |
|   2. Offline validators represented by empty vector placeholders.     |
|   3. Network verifies: (Valid Solutions >= T) && Coordinator Sig      |
+-----------------------------------------------------------------------+
```

### 2.1. A Decentralized Approach Model (ADAM)
ADAM serves as the identity and selection layer. Rather than allowing all nodes to compete freely, which introduces Sybil vulnerabilities, ADAM selects a restricted pool of $N$ Validators (Miners) and 1 Coordinator per block height.

#### 2.1.1. VRF Rolling Seeds
To prevent block grinding, the selection entropy is generated via a Verifiable Random Function (VRF). At block height $H$, the selection seed is defined as:

$$\text{Seed}_H = \text{Hash}\left(\text{Seed}_{H-1} \mathbin{\Vert} \text{VRFProof}_{H-1}\right)$$

Where the VRF proof ($\text{VRFProof}$) at block height $H-1$ is a deterministic signature generated by the Coordinator of block $H-1$ using a hybrid BLS12-381 + ECDSA fallback signature mechanism (`SignBLSWithECDSAFallback`) on the seed of block $H-2$. Because the resulting BLS signature (authorized by an ECDSA signature of the BLS public key) is strictly deterministic, the Coordinator cannot manipulate the signature value to skew the election of block $H+1$. The rolling seed is stored directly in the block header, making the history of selection entropy immutable and publicly verifiable.

#### 2.1.2. Node Selection, Ranking & Dual-Track Quotas
Given the active candidate pool $P$, the system calculates a unique rank for each node $i$:

$$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

On Mainnet and Testnet, the candidate pool is dynamically constructed from active Masternodes (with unspent 4,200 KRISTA collateral and on-chain heartbeat pings within the last 2,880 blocks) and active registered miners (via `250 KRISTA` Coin-Lock, `10 KRISTA` funded PoW-Lock, or `0 KRISTA` Zero-Coin PoW). Once `nHeight >= consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM` = block 200 on Mainnet/Testnet, block 1 on Regtest), the $N = 11$ **Validators (Miners)** are elected using a **Dual-Track Quota Election (`ElectDualTrackValidators`)** to prevent low-cost PoW registrations from crowding out collateral-backed validators while guaranteeing open mining participation:
* **Track 1 (Collateralized Pool — 8 Seats Total)**:
  * **Sub-Track 1A (Active Masternodes — 7 Seats)**: Reserved for active Masternodes (`4,200 KRISTA` collateral), enforced via `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7)` so active Masternodes alone always meet the post-bootstrap threshold (`7`).
  * **Sub-Track 1B (Coin-Lock Validators — 1 Seat)**: Reserved for `250 KRISTA` Coin-Lock miners, with immediate intra-Track-1 spillover between Sub-Track 1A and Sub-Track 1B so all 8 collateralized seats are filled whenever $\ge 8$ total collateralized candidates exist.
* **Track 2 (Open PoW Pool — 3 Seats Total)**: Reserved for Open PoW miners (`10 KRISTA` `PoW-Lock`, `0 KRISTA` `Zero-Coin PoW`, and bootstrap keys below `nAdamBootstrapLimit`), with bidirectional inter-track spillover if either track has fewer candidates than its quota.

To ensure nodes can accumulate the necessary 4,200 KRISTA collateral for masternode setup during the early bootstrap phase, Coin-Lock registration is disabled below block height 2,200 (Model D activation) on Mainnet, directing early miners to use PoW-Lock (`10 KRISTA` locked or `0 KRISTA` Zero-Coin PoW) and keeping block rewards liquid. On Regtest, the pool automatically includes 15 deterministic bootstrap public keys to facilitate automated testing.

#### 2.1.3. Mode Dynamics & Spork-Control
To facilitate bootstrapping, ADAM operates in two modes:
* **Fallback Mode (Version 11)**: Operates during the early bootstrap phase of the network. The validator pool is selected from the registered miner pool (since the active masternode count is below the quorum threshold). The miners count $N \in [11, 14]$ is dynamic, and the consensus threshold $T$ is governed by `GetAdamThreshold(nHeight)` (5 valid solutions during the bootstrap phase for `nHeight < nAdamThresholdBootstrapLimit`, i.e., below block 50,000 on Mainnet / 100,000 on Testnet / 1,000 on Regtest, transitioning thereafter to the consensus parameter `nAdamThreshold` of 7 valid solutions on Mainnet/Regtest and 3 on Testnet).
* **Standard Mode (Version 12)**: Enforces full cooperative consensus once a sufficient number of active Masternodes are online. The miner count $N$ is set to `nAdamMinersCount` (11), and the consensus threshold $T$ is governed by `GetAdamThreshold(nHeight)` (5 solutions during bootstrap below block 50,000 on Mainnet / 100,000 on Testnet / 1,000 on Regtest, and 7 on Mainnet/Regtest or 3 on Testnet thereafter). The Coordinator is elected dynamically from the active Masternode list, while the miners are elected from the registered miner pool.
* **Activation**: The transition is governed by `SPORK_21_ADAM_STANDARD_MODE` (Spork ID `10020`). If active, the protocol enforces Version 12 block validation.

#### 2.1.4. Bootstrap Security and Starting Difficulty
To ensure the network can bootstrap securely from genesis (block 1) when mining is public and multiple nodes mine concurrently, the starting PoW difficulty limit (`powLimit`) is hardened to `~UINT256_ZERO >> 20` (equivalent to the genesis block's `nBits` of `0x1e0ffff0`). This prevents blocks 1–199 from being mined instantly in microseconds, giving nodes sufficient time to establish P2P connections and propagate blocks. This eliminates the risk of fork splits (where nodes disagree on the block 199 hash and fail to form quorums at block 200).

---

### 2.2. Proof of BLS (PoBLS) Consensus
Once the validator set is elected by ADAM, the block proposer is chosen using the **Proof of BLS (PoBLS)** lottery. This prevents proposer targetability, as the winning block producer is only revealed at the moment of block propagation.

#### 2.2.1. Ephemeral Ticket Generation
Each elected validator $i$ generates an ephemeral BLS keypair $(sk_i, pk_i)$ for height $H$. A lottery ticket $T_i$ is computed as:

$$T_i = \text{Hash}(sk_i \parallel pk_i \parallel H)$$

To prove key ownership without exposing the private key, the validator signs the previous block hash ($Hash_{\text{prev}}$) using the ephemeral key:

$$\sigma_i = \text{Sign}_{sk_i}(Hash_{\text{prev}})$$

The validator broadcasts its ticket message $(pk_i, \sigma_i, T_i)$ to the active **Long-Living Masternode Quorum (LLMQ)**. Each LLMQ consists of exactly 5 members. Once Model D is active, the quorum signature verification threshold in `CQuorumSignature::Verify()` (`src/llmq.cpp`) is a strict BFT majority (`quorum.members.size() / 2 + 1`), which requires at least **3 out of 5** signatures across all networks (Mainnet, Testnet, and Regtest); prior to Model D activation on Mainnet and Testnet, quorum signature verification is bypassed (0 signatures required) to allow bootstrapping. Furthermore, no hardcoded key ID filtering or local wallet restrictions are applied on Mainnet, Testnet, or Regtest, ensuring a fully decentralized and trustless test environment.

#### 2.2.2. Winner Selection via XOR Distance
The target hash $T_{\text{target}}$ is derived from the active rolling seed ($\text{Seed}_H$). The LLMQ calculates the XOR distance between each submitted ticket and the target:

$$D_i = |T_i \oplus T_{\text{target}}|$$

The validator with the smallest distance ($D_i$) wins the right to propose the block. Ephemeral BLS keys must be derived deterministically from the node's long-term identity key to prevent grinding:

$$sk_i = \text{DeriveKey}(sk_{\text{node}}, Hash_{\text{prev}})$$

This ensures that each elected validator can generate exactly one valid ticket per height, preventing ticket pre-computation. DKG sessions to elect new active quorums run every 100 blocks on Mainnet (until block 1,400) and every 10 blocks on Testnet (until block 1,000) / Regtest (until block 800), and rotate every block (`nInterval = 1`) thereafter.

#### 2.2.3. Metric Space and XOR Distance Analysis
The XOR operation $\oplus$ defines a metric space $(X, d)$ on the set of binary keys of length $L = 256$, where $d(x, y) = x \oplus y$. This metric satisfies the three basic properties of a metric space:
1. **Identity of Indiscernibles**: $d(x, y) = 0 \iff x \oplus y = 0 \iff x = y$
2. **Symmetry**: $d(x, y) = x \oplus y = y \oplus x = d(y, x)$
3. **Triangle Inequality**: $d(x, z) \le d(x, y) \oplus d(y, z)$ which in XOR space satisfies the stronger ultrametric property:
   
   $$d(x, z) \le \max(d(x, y), d(y, z))$$

Because $T_{\text{target}}$ is pseudorandom and uniformly distributed, and the ephemeral tickets $T_i$ are generated cryptographically, the distance metrics $D_i$ behave as independent, uniformly distributed random variables in $[0, 2^{256}-1]$. The probability $P$ of any validator winning the block proposal behaves as $1/N$, ensuring complete fairness.

#### 2.2.4. Production Implementation (`src/adam.cpp`, `src/llmq.cpp`, `src/crypto/bls.cpp`)
The production C++ consensus engine unifies the conceptual PoBLS + ADAM mathematical model directly with on-chain deterministic execution:
* **Deterministic & Pinned BLS12-381 Key Derivation (`DeriveBLSFromCKey` / `PinBLSPubKey`)**: Every validator, coordinator, and quorum member derives a deterministic BLS12-381 keypair $sk_i = \text{BLST\_KeyGen}(\text{SHA256D}(sk_{\text{node}}))$, $pk_i = g_1^{sk_i}$, bound to its permanent `secp256k1` identity key via `CBLSSignedData` (`SignBLSWithECDSAFallback` / `VerifyBLSWithECDSAFallback`). Upon first valid on-chain participation, the `(secp256k1 pubKey -> blsPubKey)` binding is persistently pinned in LevelDB (`PinBLSPubKey`), making key-grinding and intra-block VRF/ticket manipulation mathematically impossible.
* **PoBLS Ticket ($T_i$), Target ($T_{\text{target}}$), and XOR Metric Distance ($D_i = |T_i \oplus T_{\text{target}}|$)**: Each elected validator's signed partial solution embeds $(pk_i, \sigma_i)$, from which `ComputePoBLSTicket` derives $T_i = \text{SHA256D}(pk_i \parallel \sigma_i \parallel H)$ (`ExtractPoBLSTicketFromSolution`). Against $T_{\text{target}} = \text{SHA256D}(\text{Seed}_H \parallel H)$ (`ComputePoBLSTarget`), `ComputePoBLSXORDistance` computes $D_i = |T_i \oplus T_{\text{target}}|$ on $(\{0,1\}^{256}, \oplus)$, and `RankValidatorsByPoBLSDistance` orders active ticket-holding validators in ascending order of XOR distance $D_i$.
* **5-Member BFT LLMQ & BLS12-381 Signature Aggregation (`ElectQuorumMembers`, `CQuorumSignature::Verify`, `CQuorumSignature::AggregateBLSSignatures`)**: For each height $H$, 5 Masternodes with unspent 4,200 KRISTA collateral (`pcoinsTip->HaveCoin`) and active on-chain status are deterministically elected via $\text{SHA256D}(\text{Seed} \parallel \text{collateralOutpoint})$. Quorum members sign `blockHash` (`PROPOSEBLOCK` / `QUORUMSIGSHARE`) using deterministic BLS12-381 keys (`DeriveBLSFromCKey`). `CQuorumSignature::Verify` enforces a strict **3-of-5** BFT majority and aggregates the signing members' BLS12-381 public keys (`CBLSPubKey::Aggregate`) and signatures (`CBLSSignature::Aggregate`) to verify the pairing equation $e(g_1, \Sigma_{\text{agg}}) = e(PK_{\text{agg}}, H_2(\text{blockHash}))$.
* **Consensus Synergy**:
  * **Sybil Protection (ADAM)**: Participation is strictly bounded to collateral-backed Masternodes and Coin-Lock / PoW-Lock registered miners.
  * **DDoS Resistance (PoBLS XOR Ranking + Time-Based Rotation)**: If the primary Coordinator does not propagate a block within 45 seconds (`ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45`), proposer authority rotates every 30 seconds across the 11 elected validators ranked by smallest PoBLS XOR distance $D_i = |T_i \oplus T_{\text{target}}|$ (`RankValidatorsByPoBLSDistance`).

---

### 2.3. Block Header Extensions & Hashing Chain
When the ADAM upgrade is active, the block header structure is expanded to store consensus proofs:

| Field | Type | Description |
| :--- | :--- | :--- |
| `vAdamMiners` | `std::vector<CPubKey>` | Public keys of elected validators. |
| `vAdamSolutions` | `std::vector<std::vector<char>>` | Serialized partial solutions (nonce + signature). |
| `vAdamVRFProof` | `std::vector<unsigned char>` | Coordinator's VRF signature on the previous seed. |
| `vAdamCoordinatorSig` | `std::vector<unsigned char>` | Coordinator's signature on the final block hash. |

#### 2.3.1. Stateless Hashing Chain
To calculate the block hash, the block header is processed through a sequential hashing chain corresponding to the elected validators. For each validator $i$ in the chain (where $M = \text{size}(vAdamMiners)$):

1. Generate a round-specific hash from the previous block hash and index:
   
   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$
   
2. Extract the first byte $v_i = \text{roundHash}_i[0]$ and compute an odd coprime multiplier:
   
   $$m_i = v_i \mid 1 \quad (\text{if } m_i < 3, m_i = 3)$$
   
3. Execute the hashing round:
   - **Fallback Mode (Version 11)**:
     - **Round 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$
     - **Round $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{i-1}) \times m_i \pmod{2^{256}}$
     where the algorithm index is:
     
     $$\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$$
     
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

The final hash $H_{\text{prev}}$ (or $H_0 \times m_0$ in Version 11) represents the block hash.

#### 2.3.2. Coprime Multiplier Properties and Mathematical Soundness
The multiplication of the intermediate hashes by $m_i$ modulo $2^{256}$ is mathematically sound. In modular arithmetic, an element $m$ has a multiplicative inverse modulo $K$ if and only if $\gcd(m, K) = 1$.
For the group of integers modulo $2^{256}$ ($\mathbb{Z}_{2^{256}}$), the modulus is a power of 2. Therefore, any odd integer $m_i$ is coprime to $2^{256}$:

$$\gcd(m_i, 2^{256}) = 1$$

This coprimality guarantees that the mapping $f(x) = x \cdot m_i \pmod{2^{256}}$ is a bijection (a one-to-one and onto mapping). As a result:
* **No Entropy Loss**: The multiplication preserves the entire entropy of the hash function; no two distinct input values map to the same output value.
* **No Degeneracy**: The intermediate state cannot collapse to a zero or sub-space, maintaining the mathematical integrity of the cryptographic chain.
* **Non-commutativity**: The ordered application of different multipliers prevents order-swapping attacks.

---

### 2.4. Quorum Resiliency and Placeholder Security
A strict cooperative loop requiring 100% participation from all elected validators creates a vulnerability where a single offline node can halt block production. To prevent chain freezes, KristaTech implements a **Quorum-Resilient Responding Mechanism** with **Standby Validator Seat Handover ($t \ge 60\text{s}$)** and **Time-Relaxed Quorum Thresholds ($t \ge 120\text{s}$)**.

#### 2.4.1. Placeholder Mechanism & Standby Handover
The block template is finalized as long as the count of valid validator solutions meets the effective threshold $T$ governed by `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` (starting at `GetAdamThreshold(nHeight)` = 5 during bootstrap `< nAdamThresholdBootstrapLimit`, i.e., below block 50,000 on Mainnet / 100,000 on Testnet / 1,000 on Regtest, and 7 on Mainnet/Regtest or 3 on Testnet thereafter). If an elected validator fails to submit its solution within the normal block slot window ($t < 60\text{s}$), the Coordinator replaces the missing solution in `vAdamSolutions` with an empty byte vector:

$$\text{vAdamSolutions}[i] = \text{std::vector<unsigned char>()}$$

If block assembly is delayed by $t \ge 60\text{s}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`, advancing in `30s` slots), any silent primary seat in `vAdamMiners` may be handed over strictly to a **same-track standby validator** (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) in sequential rank order verified against the block's `CCoinsViewCache* pView` (`ResolveAdamCommitteeForTime`, `ValidateAdamCommitteeForTime`). If the block remains stalled at $t \ge 120\text{s}$ (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = 120`), the required threshold relaxes by `1` every `60s` down to `ADAM_MIN_RELAXED_THRESHOLD = 5` (`> 4` non-Masternode seats, ensuring at least 1 Active Masternode is always required when post-bootstrap or active MNs $\ge 7$, or `3` during bootstrap when active MNs $< 7$). This preserves positional alignment between `vAdamMiners` and `vAdamSolutions` while guaranteeing block recovery under severe outages.

#### 2.4.2. Validation Logic
Validating peers execute the following verification steps in `CheckBlock()`:
1. Verify that `vAdamSolutions` size matches `vAdamMiners` size and validate `vAdamMiners` against `ValidateAdamCommitteeForTime(..., pView)` for the block's elapsed time.
2. Iterate through `vAdamSolutions`. If a solution is empty, it is marked as a placeholder and skipped. If it is non-empty, calculate the puzzle hash and verify the signature and difficulty:
   * **Version 11**: $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Challenge}\right)$$ where $\text{algoIndex} = \text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{18}$, and $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
   * **Version 12**: $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$ where $\text{algo1}$, $\text{algo2}$, and $\text{algo3}$ are derived using $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$, and $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
3. Confirm that the number of cryptographically validated, non-empty solutions is greater than or equal to the effective threshold $T$.

#### 2.4.3. Security Proofs
* **Bypass Resistance**: Empty placeholders cannot satisfy signature or difficulty checks. The validation code explicitly treats placeholders as failed proofs. The thermodynamic work requirement is preserved because at least $T$ nodes must submit valid, high-difficulty proofs.
* **Censorship Non-Profitability & Equal Work Reward**: Under Model D (`IsModelDActive`), **Equal Work, Equal Reward** applies across all 11 committee seats within the 40% Participant Validator pool (split into 20% across the 4 Independent Miner quota seats [`8.00%` for 1 Coin-Lock + `4.00%` each for 3 Open PoW] and 20% across the 7 Masternode Validator quota seats [`~2.857%` per seat]): every seat $i \in \{0,\dots,10\}$ that provides a valid PoW solution in `vAdamSolutions[i]` (whether filled by the primary validator `vExpectedMiners[i]` or by a legitimate same-track standby validator at $t \ge 60\text{s}$) receives its full track-specific seat share, while unsolved/empty seats receive `0%`. Any unclaimed empty-seat shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** (permanently reducing `nMoneySupply` as pure supply deflation without crediting any personal PoB staking weight) and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)**, while the Coordinator retains its full 15% reward when rescuing a stalled block with legitimate standby validators. Furthermore, primary validators remain protected against opportunistic substitution by: (1) expiration of the primary Coordinator's exclusivity window at $t = 45\text{s}$ before standby rotation opens at $t = 60\text{s}$, (2) deterministic same-track standby rank ordering (`m = 11..16`), and (3) LLMQ `qblockprop` vetoes against any proposal at $t \ge 60\text{s}$ that substitutes a primary seat whose valid solution is already cached locally. Because omitting an honest seat yields **zero financial gain** for the Coordinator, the Passive Masternode Winner, or remaining validators, there is **zero financial incentive** to censor miners.

---

## 3. MESCAL Smart Contract Language

**MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)** is a JSON-based declarative language designed to define, assemble, and serialize smart contracts.

### 3.1. Design Philosophy
Designed for simplicity and safety, MESCAL uses a declarative paradigm that compiles directly into stack-based Bitcoin Script (`CScript`) bytecode.
1. **Safety**: Operates on a restricted, deterministic instruction set matching blockchain opcodes, eliminating re-entrancy vulnerabilities.
2. **Gasless Determinism**: Because loop instructions are omitted, contract execution time is linear and predictable, removing the need for complex gas estimation.
3. **Accessibility**: Contracts are defined as structured JSON, making them easy to generate, verify, and parse across client-side applications.

---

### 3.2. Grammar and Formal Syntax
A MESCAL smart contract is defined by a structured grammar. Using Backus-Naur Form (BNF), the contract configuration is represented as:

```bnf
<contract_file>      ::= "{" <declaration_list> "," <contract_def> "," <active_field> "}"
<declaration_list>   ::= <basic_declaration> | <condition_declaration> | <declaration_list> "," <declaration_list>
<basic_declaration>  ::= "\"basic\":" "{" <basic_definitions> "}"
<basic_definitions>  ::= <basic_entry> | <basic_definitions> "," <basic_entry>
<basic_entry>        ::= "\"" <identifier> "\":" "{" <role_def> "," <inputs_def> "}"
<role_def>           ::= "\"role\":" <opcode_string>
<inputs_def>         ::= "\"inputs\":" "[" <input_list> "]"
<input_list>         ::= <input_entry> | <input_list> "," <input_entry>
<input_entry>        ::= "{" "\"type\":" <type_string> "," "\"value\":" <value_string> "}"

<condition_declaration> ::= "\"condition\":" "{" <condition_definitions> "}"
<condition_definitions> ::= <condition_entry> | <condition_definitions> "," <condition_entry>
<condition_entry>       ::= "\"" <identifier> "\":" "{" "\"role\":" "\"if-condition\"" "," <exprs_def> "," <true_path> "," <false_path> "}"

<contract_def>       ::= "\"contract\":" "{" "\"" <identifier> "\":" "{" "\"actions\":" "[" <action_list> "]" "}" "}"
<action_list>        ::= <action_entry> | <action_list> "," <action_entry>
<action_entry>       ::= "{" "\"type\":" <type_string> "," "\"name\":" <value_string> "}"
<active_field>       ::= "\"active_contract\":" "\"" <identifier> "\""
```

---

### 3.3. Opcode Mapping and Specifications
The compiler translates JSON structures into binary operations:

* **`hash160`**: Pushes the input data and computes SHA256 followed by RIPEMD160.  
  * *CScript Equivalent*: `<input> OP_HASH160`
* **`check-signature-verification`**: Pushes a public key and verifies a cryptographic signature.  
  * *CScript Equivalent*: `<pubkey> OP_CHECKSIGVERIFY`
* **`equalverify-checksig`**: Standard P2PKH validation.  
  * *CScript Equivalent*: `OP_DUP OP_HASH160 <pubkeyhash> OP_EQUALVERIFY OP_CHECKSIG`
* **`multi-signature`**: Multi-signature evaluation.  
  * *CScript Equivalent*: `<m> <pubkeys...> <n> OP_CHECKMULTISIG`
* **`lock-time`**: Enforces time or block-height restrictions.  
  * *CScript Equivalent*: `<locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP`
* **`number`, `add`, `sub`, `bool-and`, `bool-or`, `num-equal`, `greater-than`, `drop`**: Stack arithmetic and boolean primitives (`<int64_t>`, `OP_ADD`, `OP_SUB`, `OP_BOOLAND`, `OP_BOOLOR`, `OP_NUMEQUAL`, `OP_GREATERTHAN`, `OP_DROP`).
* **`coin-lock-miner` & `pow-miner`**: Specialized ADAM miner registration scripts embedding miner identity, optional PoW challenge/nonce, CLTV locktime, and P2PKH spend rules.

---

### 3.4. Execution Safety Proof
Let $C$ be a compiled MESCAL contract consisting of a finite sequence of stack instructions $I_1, I_2, \ldots, I_k$. 
1. **Loop-Free Execution**: The instruction grammar contains no loop operations (`OP_LOOP`, `OP_WHILE`, or jumps). Therefore, the control flow graph (CFG) is a directed acyclic graph (DAG).
2. **Linear Time Complexity**: The maximum number of instructions executed is strictly bounded by the number of defined operations:
   
   $$E_{\text{max}} = O(k)$$
   
   Where $k$ is the size of the actions array in JSON.
3. **Termination Guarantee**: Because $E_{\text{max}}$ is finite and linear, every MESCAL contract is guaranteed to terminate in a deterministic number of steps, completely preventing infinite-loop attacks.
4. **Gasless Nature**: Since execution is guaranteed to terminate quickly and linear-time bounds can be verified at compilation, the network does not require gas metering.

---

### 3.5. Template Case: Dead Man's Switch (Inheritance)
Allows an heir to claim funds after a period of inactivity, while the owner can access them at any time:

* *CScript Equivalent*:  
  `<heir-pubkey> OP_CHECKSIGVERIFY OP_IF <expiry> OP_CHECKLOCKTIMEVERIFY OP_DROP OP_ELSE <owner-pubkey> OP_CHECKSIGVERIFY OP_ENDIF`

```json
{
  "basic": {
    "Heir-Sig": {
      "role": "check-signature-verification",
      "inputs": [{ "name": "Pubkey", "type": "pubkey", "value": "02ee1fb8..." }]
    },
    "Lock-Time": {
      "role": "lock-time",
      "inputs": [{ "name": "Lock-Until", "type": "height", "value": 1780718400 }]
    },
    "Owner-Sig": {
      "role": "check-signature-verification",
      "inputs": [{ "name": "Pubkey", "type": "pubkey", "value": "03ee1fb8..." }]
    }
  },
  "condition": {
    "Inheritance-Condition": {
      "role": "if-condition",
      "expressions": [{ "type": "basic", "name": "Heir-Sig" }],
      "true": [{ "type": "basic", "name": "Lock-Time" }],
      "false": [{ "type": "basic", "name": "Owner-Sig" }]
    }
  },
  "contract": {
    "Inheritance-Switch": {
      "actions": [{ "type": "condition", "name": "Inheritance-Condition" }]
    }
  },
  "active_contract": "Inheritance-Switch"
}
```

### 3.6. Taproot (P2TR) Script-Path Integration
MESCAL contracts can be compiled directly into Taproot (P2TR) script-path spending conditions using the built-in `compilemescaltotaproot` RPC command. This allows developers to commit a contract to a Bech32m-encoded P2TR address, preserving privacy and space.
* **Compilation**: The compilation process takes the contract JSON and an `internal_pubkey` to construct a script tree. It generates the P2TR `address`, `scriptPubKey`, `leafScript` hex, and a 33-byte `controlBlock`.
* **Execution**: To spend the output, a transaction witness script must contain the execution arguments, the `leafScript`, and the `controlBlock` (appended in scriptSig). Since only the executed script leaf is revealed on-chain, alternative branches (e.g., recovery or timeout paths) remain hidden.

---

## 4. Ecosystem Tokenomics

KristaTech implements a deflationary emission model designed to preserve scarcity while maintaining security incentives over a long lifecycle.

### 4.1. Core Emission Parameters
* **Maximum Supply (Hard Cap)**: **210,000,000 KRISTA**
* **Premine**: **0 KRISTA** (fair-launch distribution)
* **Block Time Target**: 30 seconds (~1,051,200 blocks per year)
* **Collateral Requirement**: Flat **4,200 KRISTA** from block 1
* **Graduated Bootstrap Phase (Blocks 2 - 9,999)**: **40 KRISTA** for blocks 2–2,200, **30 KRISTA** for blocks 2,201–5,000, and **20 KRISTA** for blocks 5,001–9,999. This phase generates **271,940 KRISTA** (~0.13% of supply), providing ample circulating liquidity to support active masternodes for LLMQ and Model D formation while preventing early inflation overhang.
* **Starting Block Reward (Block 10,000+)**: **15 KRISTA**

---

### 4.2. Emission Decay Model
To avoid the supply shocks of 4-year halvings, block rewards decrease gradually using a **1.9% decay every 259,200 blocks**. With a block target spacing of 30 seconds, this 259,200-block interval translates to approximately every 90 days (quarterly) in calendar time. The reward at period $P$ is calculated as:

$$\text{Reward}(P) = 15.0 \times (0.981)^P$$

Where:

$$P = \left\lfloor \frac{\text{Height} - 10000}{259200} \right\rfloor$$

#### 4.2.1. Mathematical Derivation of Supply Cap and Gap Reserve
To prove that the emission model never exceeds the hard cap of 210M KRISTA, we represent the total supply as the sum of bootstrap emission and the infinite geometric series of decaying periods. Let $S_{\text{max}}$ be the maximum circulating supply.

$$S_{\text{max}} = S_{\text{bootstrap}} + \sum_{P=0}^{\infty} \left( B_{\text{blocks}} \times R_0 \times (1 - d)^P \right)$$

Where:
* $S_{\text{bootstrap}} = 271,940 \text{ KRISTA}$ (graduated emission from blocks 2 to 9,999)
* $B_{\text{blocks}} = 259,200$ (blocks per decay period)
* $R_0 = 15.0 \text{ KRISTA}$ (starting decaying reward)
* $d = 0.019$ (decay rate of 1.9%, so the multiplier is $1 - d = 0.981$)

Since $0 < (1 - d) < 1$, the infinite series converges:

$$\sum_{P=0}^{\infty} (0.981)^P = \frac{1}{1 - 0.981} = \frac{1}{0.019} \approx 52.631579$$

Substituting these constants:

$$S_{\text{max}} = 271,940 + 259,200 \times 15.0 \times \frac{1}{0.019}$$
$$S_{\text{max}} = 271,940 + 3,888,000 \times 52.631579$$
$$S_{\text{max}} = 271,940 + 204,631,579 \approx 204,903,519 \text{ KRISTA}$$

The difference between the Hard Cap ($210,000,000$ KRISTA) and the maximum supply limit $S_{\text{max}}$ represents the **Gap Reserve** ($G_{\text{reserve}}$):

$$G_{\text{reserve}} = 210,000,000 - 204,903,519 = 5,096,481 \text{ KRISTA}$$

This Gap Reserve of **5,096,481 KRISTA (2.43%)** ensures the network can continue reward emissions for decades. This gradual decay prevents security shocks and facilitates a smooth transition to a transaction-fee security model.

```
Supply Saturation Lifecycle:
[0] (Genesis) -> [18.99M] (Year 1) -> [32.72M] (Year 2) -> [68.12M] (Year 5) -> [111.71M] (Year 10) -> [204.90M] (Asymptote)
```

---

### 4.3. Model D Cooperative Reward Split
Block rewards are split to incentivize both validator execution and network infrastructure.

```
+------------------------------------------------------------+
|                  Model D Block Reward (100%)               |
|            (Applicable to blocks 2,200+ on mainnet)        |
+------------------------------+-----------------------------+
|    Masternode Pool (45%)     |     Validator Pool (55%)    |
+--------------+---------------+--------------+--------------+
|  Passive MN  |   LLMQ Active |  Producer/   | Participant  |
|  Queue (35%) |  Quorum (10%) |  Winner (15%)|  Miners (40%)|
+--------------+---------------+--------------+--------------+
```

* **Early Stage (Blocks 2 - 2,199)**: 100% Miner-Staker (supports network security while masternodes accumulate).
* **Quorum Accumulation (Blocks 2,000 - 2,199)**: LLMQs activate, prompting nodes to set up masternodes.
* **Model D Activation (Blocks 2,200+)**: The hybrid split is enforced:
  1. **Masternode Passive Share (35%)**: Paid to the next Masternode in the global deterministic queue (incentivizes collateral locking).
  2. **LLMQ Quorum Active Share (10%)**: Distributed among the active masternode members validating and signing PoBLS tickets for that block.
  3. **Block Producer Share (15%)**: Paid in full to the winning PoBLS validator (PoW block) or coin staker (PoS block), plus transaction fees.
  4. **Validator Participant Share (40%)**: Partitioned into **20% across the 7 Masternode Validator quota seats** (`~2.857%` per seat) and **20% across the 4 Independent Miner quota seats (`8.00%` for the 1 Coin-Lock seat + `12.00%` across the 3 Open PoW seats at `4.00%` per seat)** for all committee seats that submit a valid PoW solution (**Equal Work, Equal Reward**). If a Masternode spills over into an empty independent miner seat, it receives the standard Masternode Validator seat share (`~2.857%`), and the unclaimed difference (`~5.143%` for the `8.00%` Coin-Lock seat or `~1.143%` for a `4.00%` Open PoW seat) alongside any unsolved seat shares (`nUnclaimedPartSplit`) is split **50% to the Automatic Protocol Burn** (permanently deducted from `nMoneySupply` without crediting any personal PoB staking weight) and **50% to the Developer Treasury**.

#### 4.3.1. Hybrid Block Reward Dynamics (PoW vs. PoS)
The split adapts dynamically to the block type to support miners and stakers:
* **PoW Blocks**: The 55% active execution share is distributed among the ADAM validator pool (15% to the coordinator/proposer, 40% split among the 11 validator seats [20% for 7 MN seats + 20% for 4 independent miner seats], with unsolved shares split 50% to the Automatic Protocol Burn and 50% to the Developer Treasury).
* **PoS Blocks**: The 15% producer share goes to the coin staker who won the kernel difficulty check. The 40% validator share is distributed among the solved ADAM committee seats, keeping the mining infrastructure active.

---

### 4.4. Developer & Faucet Treasury
To fund development and user onboarding, the protocol implements direct treasury allocations:
* **Developer Treasury**: **7.0%** of the block reward is allocated to the Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`) from block 2 onward.
* **Bootstrap Faucet**: **0.7%** of the block reward is allocated to the Faucet Fund Address (`KThn5Li5BnfGzbNJ92UDH7FL9CZR1VSRGEh`) for blocks 2 through 50,000.

These allocations are deducted directly from the total block value. For example, during Bootstrap Phase 1 (Blocks 2–2,200), the 40 KRISTA block reward is distributed as: 2.8 KRISTA to the Developer Treasury, 0.28 KRISTA to the Faucet, and the remaining 36.92 KRISTA split according to the active consensus rules.

---

## 5. Security & Cryptographic Analysis

### 5.1. Sybil Attacks & Dual-Track Quota Isolation
An attacker attempting to dominate the validator selection or ticket submission pools faces high economic barriers. Under `ElectDualTrackValidators`, the 11 validator seats are partitioned into **Track 1 (8 Collateralized Seats: 7 Active Masternodes [`4,200 KRISTA`] + 1 Coin-Lock [`250 KRISTA`] with intra-track spillover, locked via `static_assert(ADAM_MASTERNODE_TRACK_QUOTA >= 7)`)** and **Track 2 (3 Open PoW Seats)**. Because Active Masternodes alone fill 7 seats ($\ge$ the post-bootstrap threshold of 7), non-Masternode Sybil registrations can never stall block production, and controlling a consensus quorum requires locking a massive portion of the active Masternode collateral supply.

### 5.2. Pre-computation and Nothing-at-Stake
* **Pre-computation**: Because ephemeral BLS keys are derived from long-term node identity keys and the previous block hash, validators cannot pre-compute or grind tickets to skew the PoBLS lottery.
* **Nothing-at-Stake**: In PoS, nodes can sign blocks on multiple forks at no cost. In KristaTech, validating on competing forks requires solving the physical multi-algorithm PoW puzzles for at least $T$ validator seats, imposing a real computational cost that mitigates the nothing-at-stake vulnerability.

### 5.3. Double Block Signatures
For blocks at heights $\ge 200$ (Cooperative PoS), security is strictly enforced using two cryptographic signatures:

> [!IMPORTANT]
> **No Single-Signature PoS Blocks:** PoS blocks do not transition the network into a single-signature consensus model. To prevent consensus hijack or verification bypass, every PoS block must carry both signatures. Single-signature blocks (temporary or permanent) are strictly rejected by validating nodes under all circumstances.

1. **ADAM Coordinator Signature (`vAdamCoordinatorSig`)**: Validates that the cooperative validator selection and voting rounds were completed successfully.
2. **Staker Block Signature (`vchBlockSig`)**: Generated using the private key of the staking UTXO, locking the transactions to the block.

Validating nodes require both signatures to be valid, securing the chain against both history-rewriting and consensus-hijacking attacks.

---

## 6. Conclusion

The KristaTech blockchain protocol presents a cooperative consensus design that addresses key limitations of traditional networks. By separating validator selection (ADAM) from block proposal (PoBLS), the protocol achieves Sybil resistance, proposer anonymity, and defense against targeted DDoS attacks. The quorum-resilient placeholder mechanism ensures chain liveness, while the MESCAL language provides a secure, gasless, and declarative environment for smart contracts. Supported by a 210 Million emission model with a 1.9% decay rate and the Model D Cooperative Split, KristaTech establishes a balanced incentive structure for miners, stakers, and masternode operators, offering a secure and sustainable framework for decentralized applications.

---

## 7. References

1. Nakamoto, S. (2008). "Bitcoin: A Peer-to-Peer Electronic Cash System."
2. Micali, S., Rabin, M., & Vadhan, S. (1999). "Verifiable Random Functions." *Proceedings of the 40th Annual Symposium on Foundations of Computer Science (FOCS)*.
3. Wood, G. (2014). "Ethereum: A Secure Decentralised Generalised Transaction Ledger."
4. Boneh, D., Gentry, C., Lynn, B., & Shacham, H. (2003). "Aggregate and Verifiable Signatures from Bilinear Maps." *Journal of Cryptology*.
5. Maymounkov, P., & Mazieres, D. (2002). "Kademlia: A Peer-to-Peer Information System Based on the XOR Metric." *International Workshop on Peer-to-Peer Systems*.
