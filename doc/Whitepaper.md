# KristaTech (KRISTA) Technical Whitepaper
## A Quorum-Resilient Cooperative Hybrid Consensus Blockchain with Proof-of-BLS (PoBLS) Proposer Selection and JSON-Compiled Declarative Smart Contracts (MESCAL)

**Abstract**  
This paper introduces the KristaTech (KRISTA) blockchain protocol, a decentralized platform designed to address consensus centralization, leader-targeted Denial-of-Service (DoS) attacks, and the architectural complexities of traditional virtual machines. KristaTech implements a two-tier consensus mechanism that decouples validator set selection from block proposal. The selection layer, **ADAM (A Decentralized Approach Model)**, utilizes a Verifiable Random Function (VRF) rolling seed to deterministically elect a set of $N$ validators and 1 coordinator per block height. The proposal layer, **Proof of BLS (PoBLS)**, holds an ephemeral cryptographic lottery via XOR distance calculation to dynamically select the block producer from the elected validator pool, securing the network against pre-computation and proposer-targeted DoS attacks. To maintain chain liveness under network partitions or offline validators, we introduce a quorum-resilient responding mechanism utilizing empty vector placeholders (`std::vector<unsigned char>()`) verified against a dynamic threshold. Smart contracts are executed using **MESCAL (Minimalistically Envisioned Smart Contract Assembling Language)**, a simple, declarative, JSON-based specification that compiles directly into stack-based `CScript` bytecode, eliminating state-based vulnerabilities and gas calculation overhead. Finally, we propose a sustainable **210 Million KRISTA** emission model governed by a **2.15% quarterly (~90 days) decay** from Block 2 and the **Balanced Model D Cooperative Split** (`UPGRADE_MODELD` at Block 5,000 on Mainnet, with `UPGRADE_FAIRNESS` active from Block 1), which fairly distributes net block rewards among passive masternodes (5%), signer-only active LLMQ quorum members (10%, 1-block lag), the block producer (25%; splitting symmetrically into 22% Staker + 3% Coordinator + 100% Staker tx fees on PoS blocks), and validator participants (60% divided **equally** across all 15 solved seats—`8 MN + 2 Coin-Lock + 3 Funded PoW-Lock + 2 Free Entry`—at an exact `4.00%` [`0.6324 KRISTA` in Period 0] each regardless of track), alongside an LLMQ-governed institutional developer treasury allocation.

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

Where the VRF proof ($\text{VRFProof}$) at block height $H-1$ is a deterministic signature generated by the Coordinator of block $H-1$: registered **KTDIP3** Masternodes (`UPGRADE_DIP3 = 1` on Mainnet & Testnet) authenticate deterministically via their pure on-chain `blsPubKeyOperator` (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature` with empty `ecdsaSig`), while pre-DIP3/non-MN validators use hybrid BLS12-381 + ECDSA fallback (`SignBLSWithECDSAFallback`) on the seed of block $H-2$. Because both paths yield a unique deterministic BLS signature per seed, the Coordinator cannot manipulate the signature value to skew the election of block $H+1$. The rolling seed is stored directly in the block header, making the history of selection entropy immutable and publicly verifiable.

#### 2.1.2. Node Selection, Ranking & Dual-Track Quotas
Given the active candidate pool $P$, the system calculates a unique rank for each node $i$:

$$\text{Rank}_i = \text{Hash}\left(\text{Seed}_H \mathbin{\Vert} \text{PubKey}_i\right)$$

On Mainnet and Testnet, the candidate pool is deterministically constructed from active **KTDIP3** Masternodes (`UPGRADE_DIP3 = block 1`, tracked in `evodb` via `CDeterministicMNManager` with `ProRegTx` / `ProUpServTx` / `ProUpRegTx` / `ProUpRevTx`, Hot/Cold `blsPubKeyOperator` separation, and `4,200 KRISTA` unspent collateral) and active registered miners (via `250 KRISTA` Coin-Lock, `10 KRISTA` funded PoW-Lock, or `0 KRISTA` Zero-Coin PoW). Once `nHeight >= consensus.nAdamDualTrackHeight` (`UPGRADE_ADAM` = block 200 on Mainnet/Testnet, block 1 on Regtest), the committee **Validators (Miners)** ($N = 11$ Pre-Model D; expanding to **$N = 15$** under `UPGRADE_MODELD` at block `5,000` on Mainnet, `500` on Testnet, and `200` on Regtest via `GetAdamMinersCount`) are elected using **Dual-Track Quota Election (`ElectDualTrackValidators`)** to prevent low-cost PoW registrations from crowding out collateral-backed validators while guaranteeing open mining participation:
* **Track 1 (Collateralized Pool — 10 Seats under Model D / 8 Seats Pre-Model D)**:
  * **Sub-Track 1A (Active Masternodes — 8 Seats under Model D [`32%` validator share] / 7 Seats Pre-Model D)**: Reserved for active Masternodes (`4,200 KRISTA` collateral), enforced via `static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= ADAM_MODELD_MAX_CONSENSUS_THRESHOLD)` (`8 >= 8`; Pre-Model D `ADAM_MASTERNODE_TRACK_QUOTA >= 7`) so active Masternodes alone always satisfy the post-bootstrap consensus threshold (`8`-of-`15` Model D; `7`-of-`11` Pre-Model D).
  * **Sub-Track 1B (Coin-Lock Validators — 2 Isolated Seats under Model D [`8%` validator share] / 1 Seat Pre-Model D)**: Strictly isolated to `250 KRISTA` Coin-Lock miners (`2` seats under Model D; `1` seat Pre-Model D); if Coin-Lock candidates are absent, vacant seats spill over to remaining Active Masternodes first, then Open PoW.
* **Track 2 (Open PoW Pool — 5 Seats under Model D [`20%` validator share] / 3 Seats Pre-Model D)**: From `nAdamOpenEntryHeight` (`2,500` Mainnet, `1` Testnet/Regtest), Balanced Model D allocates **3 seats (`12%`)** to `10 KRISTA` Funded `PoW-Lock` / Bootstrap (`nFundedPoWTarget = 3`) and **2 permanent Free Entry (Open-Entry) seats (`8%`)** to `0 KRISTA` `Zero-Coin PoW` newcomers (`0` Coordinator weight while any funded validator exists, falling back to Funded `PoW-Lock` / Bootstrap if vacant; Pre-Model D: `2` Funded `PoW-Lock` + `1` Free Entry). Prior to `nAdamOpenEntryHeight`, tiers fill in strict order (`Tier 2A`: `10 KRISTA` Funded `PoW-Lock` $\rightarrow$ `Tier 2B`: Bootstrap $\rightarrow$ `Tier 2C`: `0 KRISTA` `Zero-Coin PoW`), with total bootstrap/`Zero-Coin` pool size capped at `GetAdamMinersCount(h) + 2` (`17` under Model D, `13` Pre-Model D). Empty seats spill over in strict priority: **Active Masternode $\to$ Open PoW $\to$ Coin-Lock (last resort)**. Under **`UPGRADE_FAIRNESS` (Block 1)**, every solved validator seat receives an exact **equal share** of the Active Validator pool (`60% / 15 = 4.00%` = `0.6324 KRISTA` in Period 0 with **zero remainder** under Balanced Model D; `75% / 11 ≈ 6.818%` / `1.07795454 KRISTA` Pre-Model D) with **mandatory `33`-byte compressed pubkeys**, and the primary **Coordinator** is elected across funded validators via proportional locked-capital VRF (`4,200` per Active MN, `250` per `Coin-Lock`, and `10` per `Funded PoW-Lock`, `0` for Zero-Coin while any funded validator exists) with **`ADAM_FAIR_COORD_V5` (`>= 55%` Active MN floor when `11 * sumNonMN > 9 * sumMN`) under Model D** and **`ADAM_FAIR_COORD_V4` (`>= 75%` Active MN floor when `3 * sumNonMN > sumMN`) Pre-Model D** (replacing the legacy `65:8:2` quadratic scheme). Also under `UPGRADE_FAIRNESS`, `nAdamBootstrapLimit` on Mainnet is `2,500`, `Zero-Coin PoW` / Free Entry capacity expands to `240` distinct keys (`MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS`) at `15`-bit Mainnet `HashX11KVS` parity difficulty (`10`-block challenge window `ADAM_ZEROCOIN_CHALLENGE_WINDOW_V2`, `240`-block lease TTL `ADAM_ZEROCOIN_POW_REG_PERIOD_V2`, or `50` blocks post-Model-D on Testnet/Regtest), block puzzles sample uniformly from **`17` single-pass hashing algorithms** (`4,080` V12 permutations, excluding asymmetric `X11KVS`), and nodes apply a **`3,000 ms` ADAM solution grace window** + **`1,000 ms` LLMQ signature grace window**.

While consensus validation (`GetAdamMinerPool`, `CheckBlock`) and manual `registerminer lock` accept `250 KRISTA` Coin-Lock registrations at any height, automatic wallet miner registration (`AutoRegisterMiner` via `setgenerate`) defers `Coin-Lock` below block height 5,000 (`IsModelDActive` on Mainnet) and thereafter auto-selects `Coin-Lock` only when renewing the wallet's own `Coin-Lock` seat or when the 2-seat Model D `Sub-Track 1B` quota is currently below capacity (`< 2` active/pending `Coin-Lock` seats) and `Track 2` holds at least `nFundedPoWTarget = 3` active Funded `PoW-Lock` validators, directing other automated miners to `PoW-Lock` (`10 KRISTA` locked or `0 KRISTA` Free Entry / Zero-Coin PoW) so both tracks remain balanced and block rewards stay liquid for accumulating `4,200 KRISTA` Masternode collateral. On Regtest, the pool automatically includes 20 deterministic bootstrap public keys (`0..19`) to facilitate automated testing.

#### 2.1.3. Mode Dynamics & Deterministic Height-Gated Activation
To facilitate bootstrapping, ADAM operates in two modes:
* **Fallback Mode (Version 11)**: Operates during the early bootstrap phase of the network (`UPGRADE_ADAM` $\le H <$ `UPGRADE_POMBL`). The validator pool is selected from the registered miner pool (since the active masternode count is below the quorum threshold). The miners count $N \in [11, 14]$ is dynamic Pre-Model D (expanding to $15$ if `UPGRADE_MODELD` precedes `UPGRADE_POMBL`, e.g., Regtest block `200`), and the consensus threshold $T$ is governed by `GetAdamThreshold(nHeight)` (`5` valid solutions during the bootstrap phase for `nHeight < nAdamThresholdBootstrapLimit`, i.e., below block `50,000` on Mainnet / `100,000` on Testnet / `1,000` on Regtest, transitioning thereafter to `7` Pre-Model D or `nAdamThresholdModelD = 8` under Model D on Mainnet/Regtest and `3` on Testnet).
* **Standard Mode (Version 12)**: Enforces full cooperative Multi-Proof-Algorithm (PoMBL) consensus with 3-permutation puzzle hashing. The committee size $N$ is governed by `GetAdamMinersCount(nHeight)` (`11` Pre-Model D during blocks `4,800..4,999` on Mainnet, expanding to **`15`** [`ADAM_MODELD_MINERS_COUNT = 15`] upon `UPGRADE_MODELD` at Mainnet block `5,000` / Testnet `500` / Regtest `200`), and the consensus threshold $T$ is governed by `GetAdamThreshold(nHeight)` (`5` solutions during bootstrap below block `50,000` on Mainnet / `100,000` on Testnet / `1,000` on Regtest, and **`8`-of-`15`** [`nAdamThresholdModelD = 8`, `ADAM_MODELD_MAX_CONSENSUS_THRESHOLD = 8`] under Model D on Mainnet/Regtest [`7`-of-`11` Pre-Model D] or `3` on Testnet thereafter). The primary Coordinator is elected dynamically via proportional VRF (`ADAM_FAIR_COORD_V5` `55%` MN floor under Model D; `ADAM_FAIR_COORD_V4` `75%` floor Pre-Model D) while the `15` (Pre-Model D `11`) committee validators are elected via `ElectDualTrackValidators`.
* **Activation (`Consensus::UPGRADE_POMBL` & `Consensus::UPGRADE_MODELD`)**: The transition from Fallback Mode (Version 11) to Standard Mode (Version 12) is strictly height-gated via `vUpgrades[Consensus::UPGRADE_POMBL].nActivationHeight` (block **4,800** on Mainnet, **400** on Testnet, and **300** on Regtest), rejecting any block with `nVersion < 12` (`bad-version`) at or above that height without relying on administrative Sporks, while Balanced Model D (`UPGRADE_MODELD`) activates deterministically at block **5,000** on Mainnet, **500** on Testnet, and **200** on Regtest.

#### 2.1.4. Bootstrap Security and Starting Difficulty
To ensure the network can bootstrap securely from genesis (block 1) when mining is public and multiple nodes mine concurrently, the starting PoW difficulty limit (`powLimit`) is hardened to `~UINT256_ZERO >> 20` (equivalent to the genesis block's `nBits` of `0x1e0ffff0`). This prevents blocks 1–199 from being mined instantly in microseconds, giving nodes sufficient time to establish P2P connections and propagate blocks. This eliminates the risk of fork splits (where nodes disagree on the block 199 hash and fail to form quorums at block 200).

---

### 2.2. Proof of BLS (PoBLS) Consensus
Once the validator set is elected by ADAM, the block proposer is chosen using the **Proof of BLS (PoBLS)** lottery. This prevents proposer targetability, as the winning block producer is only revealed at the moment of block propagation.

#### 2.2.1. Conceptual Ticket Generation Model
In the conceptual PoBLS lottery model (see Section 2.2.4 for the deterministic production formulation), each elected validator $i$ holds a BLS keypair $(sk_i, pk_i)$ for height $H$ and derives a lottery ticket $T_i$:

$$T_i = \text{Hash}(sk_i \parallel pk_i \parallel H)$$

To prove key ownership without exposing the private key, the validator signs a height-bound challenge (or partial puzzle digest) using its BLS secret key:

$$\sigma_i = \text{Sign}_{sk_i}(Hash_{\text{prev}})$$

The validator broadcasts its ticket message $(pk_i, \sigma_i, T_i)$ to the active **Long-Living Masternode Quorum (LLMQ)**. Under `UPGRADE_HARDENING_V1` (`GetTargetQuorumSize` in `src/llmq.cpp`), each LLMQ scales dynamically with the verified active Masternode count (`<25` $\rightarrow$ `5`, `25–49` $\rightarrow$ `9`, `50–99` $\rightarrow$ `11`, `>=100` $\rightarrow$ `15` members). Once Model D is active, the quorum signature verification threshold in `CQuorumSignature::Verify()` (`src/llmq.cpp`) is a strict BFT majority (`quorum.members.size() / 2 + 1`: `3-of-5`, `5-of-9`, `6-of-11`, or `8-of-15`) across all networks (Mainnet, Testnet, and Regtest); prior to Model D activation on Mainnet and Testnet, quorum signature verification is bypassed (0 signatures required) to allow bootstrapping. Furthermore, no hardcoded key ID filtering or local wallet restrictions are applied on Mainnet, Testnet, or Regtest, ensuring a fully decentralized and trustless test environment.

#### 2.2.2. Winner Selection via XOR Distance
The target hash $T_{\text{target}}$ is derived from the active rolling seed ($\text{Seed}_H$). The LLMQ calculates the XOR distance between each submitted ticket and the target:

$$D_i = |T_i \oplus T_{\text{target}}|$$

The validator with the smallest distance ($D_i$) wins the right to propose the block. BLS keys are derived deterministically from the node's long-term identity key to prevent grinding:

$$sk_i = \text{DeriveKey}(sk_{\text{node}})$$

This ensures that each elected validator can generate at most one canonical BLS identity, preventing ticket pre-computation and key grinding. Prior to dynamic liveness rotation (`nHeight < 1,400` on Mainnet, `< 1,000` on Testnet, `< 800` on Regtest), `llmq::GetActiveQuorum` runs DKG quorum elections at `dkgInterval` boundaries (`100` blocks on Mainnet, `10` blocks on Testnet/Regtest); thereafter (`fRotationActive`), on-chain Masternode liveness is evaluated at every block (`RunDKG(nHeight)`) while anchoring the deterministic election seed to the epoch boundary `(nHeight / dkgInterval) * dkgInterval - 1`.

#### 2.2.3. Metric Space and XOR Distance Analysis
The XOR operation $\oplus$ defines a metric space $(X, d)$ on the set of binary keys of length $L = 256$, where $d(x, y) = x \oplus y$. This metric satisfies the three basic properties of a metric space:
1. **Identity of Indiscernibles**: $d(x, y) = 0 \iff x \oplus y = 0 \iff x = y$
2. **Symmetry**: $d(x, y) = x \oplus y = y \oplus x = d(y, x)$
3. **Triangle Inequality**: $d(x, z) \le d(x, y) \oplus d(y, z)$ which in XOR space satisfies the stronger ultrametric property:
   
   $$d(x, z) \le \max(d(x, y), d(y, z))$$

Because $T_{\text{target}}$ is pseudorandom and uniformly distributed, and the ephemeral tickets $T_i$ are generated cryptographically, the distance metrics $D_i$ behave as independent, uniformly distributed random variables in $[0, 2^{256}-1]$. The probability $P$ of any validator winning the block proposal behaves as $1/N$, ensuring complete fairness.

#### 2.2.4. Production Implementation (`src/adam.cpp`, `src/llmq.cpp`, `src/crypto/bls.cpp`)
The production C++ consensus engine unifies the conceptual PoBLS + ADAM mathematical model directly with on-chain deterministic execution:
* **Deterministic BLS12-381 Key Separation & Pinning (`DeriveBLSFromCKey` / `PinBLSPubKey` & KTDIP3)**: Every pre-DIP3/non-MN validator, coordinator, and quorum member derives a deterministic BLS12-381 keypair $sk_i = \text{BLST}\underline{\hspace{0.4em}}\text{KeyGen}(sk_{\text{node}})$, $pk_i = g_1^{sk_i}$, bound to its `secp256k1` identity key via `CBLSSignedData` (`SignBLSWithECDSAFallback` / `PinBLSPubKey`). Once **KTDIP3 (`UPGRADE_DIP3 = 1` Mainnet & Testnet)** is active, registered Masternodes cleanly separate cold owner keys (`pubKeyOwner`) from hot operator keys (`blsPubKeyOperator` in `evodb`) and authenticate purely via operator BLS signatures (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature`, empty `ecdsaSig`), eliminating Hot-node ECDSA dependency and key grinding.
* **PoBLS Ticket ($T_i$), Target ($T_{\text{target}}$), and XOR Metric Distance ($D_i = |T_i \oplus T_{\text{target}}|$)**: Each elected validator's signed partial solution embeds $(pk_i, \sigma_i)$, from which `ComputePoBLSTicket` derives $T_i = \text{SHA256D}(pk_i \parallel \sigma_i \parallel H)$ (`ExtractPoBLSTicketFromSolution`). Against $T_{\text{target}} = \text{SHA256D}(\text{Seed}_H \parallel H)$ (`ComputePoBLSTarget`), `ComputePoBLSXORDistance` computes $D_i = |T_i \oplus T_{\text{target}}|$ on $(\lbrace 0, 1 \rbrace^{256}, \oplus)$, and `RankValidatorsByPoBLSDistance` orders active ticket-holding validators in ascending order of XOR distance $D_i$.
* **Dynamic BFT LLMQ & BLS12-381 Signature Aggregation (`ElectQuorumMembers`, `CQuorumSignature::Verify`, `CQuorumSignature::AggregateBLSSignatures`)**: For each height $H$, $N \in \{5, 9, 11, 15\}$ active Masternodes (`GetTargetQuorumSize` scaled by active MN count from `evodb` / unspent 4,200 KRISTA collateral) are deterministically elected via $\text{SHA256D}(\text{Seed} \parallel \text{collateralOutpoint})$. Quorum members sign `blockHash` (`PROPOSEBLOCK` / `QUORUMSIGSHARE`) using their registered operator BLS12-381 keys (`blsPubKeyOperator` under `UPGRADE_DIP3` or `DeriveBLSFromCKey`). `CQuorumSignature::Verify` enforces strict BFT majority (`quorum.members.size() / 2 + 1`: `3-of-5`, `5-of-9`, `6-of-11`, or `8-of-15`) and aggregates the signing members' BLS12-381 public keys (`CBLSPubKey::Aggregate`) and signatures (`CBLSSignature::Aggregate`) to verify the pairing equation $e(g_1, \Sigma_{\text{agg}}) = e(PK_{\text{agg}}, H_2(\text{blockHash}))$.
* **Consensus Synergy**:
  * **Sybil Protection (ADAM)**: Participation is strictly bounded to collateral-backed Masternodes and Coin-Lock / PoW-Lock registered miners.
  * **DDoS Resistance (PoBLS XOR Ranking + Time-Based Rotation)**: If the primary Coordinator does not propagate a block within 45 seconds (`ADAM_FALLBACK_COORDINATOR_TIMEOUT_SECS = 45`), proposer authority rotates every 30 seconds across the 15 elected committee validators (11 Pre-Model D) ranked by smallest PoBLS XOR distance $D_i = |T_i \oplus T_{\text{target}}|$ (`RankValidatorsByPoBLSDistance`).

---

### 2.3. Block Header Extensions & Hashing Chain
When the ADAM upgrade is active, the block header structure is expanded to store consensus proofs:

| Field | Type | Description |
| :--- | :--- | :--- |
| `vAdamMiners` | `std::vector<CPubKey>` | Public keys of elected validators. |
| `vAdamSolutions` | `std::vector<std::vector<unsigned char>>` | Serialized partial solutions (nonce + signature). |
| `vAdamVRFProof` | `std::vector<unsigned char>` | Coordinator's VRF signature on the previous seed. |
| `vAdamCoordinatorSig` | `std::vector<unsigned char>` | Coordinator's signature on the final block hash. |

#### 2.3.1. Stateless Hashing Chain
To calculate `CBlockHeader::GetHash()` without external chain state, the block header is processed through a sequential hashing chain of length $M$, selecting Fallback Mode ($M = \text{size}(\text{vAdamMiners}) - 1$) when `(nVersion == 11) || (vAdamMiners.size() == vAdamSolutions.size() + 1)` and Version 12 Standard 3-permutation Mode ($M = \text{size}(\text{vAdamMiners})$, hashing all `15` Model D seats or `11` Pre-Model D seats) otherwise. For each round $i \in \lbrace 0, \dots, M - 1 \rbrace$:

1. Generate a round-specific hash from the previous block hash and index:
   
   $$\text{roundHash}_i = \text{Hash}\left(\text{hashPrevBlock} \mathbin{\Vert} i\right)$$
   
2. Extract the first byte $v_i = \text{roundHash}_i[0]$ and compute an odd coprime multiplier:
   
   $$m_i = v_i \mid 1 \quad (\text{if } m_i < 3, m_i = 3)$$
   
3. Execute the hashing round:
   - **Fallback Mode (Version 11)**:
     - **Round 0**: $H_0 = \text{CalculateAdamPuzzleHash}(\text{algo}_0, \text{SerializedHeader}) \times m_0 \pmod{2^{256}}$
     - **Round $i > 0$**: $H_i = \text{CalculateAdamPuzzleHash}(\text{algo}_i, H_{i-1}) \times m_i \pmod{2^{256}}$
     where the stateless header algorithm index is:
     
     $$\text{algo}_i = \text{kUniformAlgos}\left[\text{Hash}(\text{hashPrevBlock} \mathbin{\Vert} \text{vAdamMiners}[i]) \pmod{17}\right]$$
     
   - **Standard Mode (Version 12)**:
     - **Round 0**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ via $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{vAdamMiners}[0])$.
       * Compute:

         $$H_0^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, \text{SerializedHeader})$$

         $$H_0^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_0^{(3)} \times 1 \right) \pmod{2^{256}}\right)$$

         $$H_0 = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_0^{(2)} \times 1 \right) \pmod{2^{256}}\right)$$

       * Apply multiplier:

         $$H_{\text{prev}} = (H_0 \times m_0) \pmod{2^{256}}$$

     - **Round $i > 0$**:
       * Derive 3-permutation algorithms $\text{algo1}$, $\text{algo2}$, $\text{algo3}$ via $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{vAdamMiners}[i])$.
       * Compute:

         $$H_i^{(3)} = \text{CalculateAdamPuzzleHash}(\text{algo3}, H_{\text{prev}})$$

         $$H_i^{(2)} = \text{CalculateAdamPuzzleHash}\left(\text{algo2}, \left( H_i^{(3)} \times (i + 1) \right) \pmod{2^{256}}\right)$$

         $$H_i = \text{CalculateAdamPuzzleHash}\left(\text{algo1}, \left( H_i^{(2)} \times (i + 1) \right) \pmod{2^{256}}\right)$$
         
       * Apply multiplier:
         
         $$H_{\text{prev}} = (H_i \times m_i) \pmod{2^{256}}$$

The final output $H_{\text{prev}}$ after round $M - 1$ represents the canonical block hash.

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
The block template is finalized as long as the count of valid validator solutions meets the effective threshold $T$ governed by `GetAdamEffectiveThreshold(nHeight, nTimeElapsed, nActiveMasternodes)` (starting at `GetAdamThreshold(nHeight)` = `5` during bootstrap `< nAdamThresholdBootstrapLimit`, i.e., below block `50,000` on Mainnet / `100,000` on Testnet / `1,000` on Regtest, transitioning thereafter to **`8`-of-`15`** under Balanced Model D [`nAdamThresholdModelD = 8`, `ADAM_MODELD_MAX_CONSENSUS_THRESHOLD = 8`; `7`-of-`11` Pre-Model D] on Mainnet/Regtest or `3` on Testnet). If an elected validator fails to submit its solution within the normal block slot window ($t < 60\text{s}$), the Coordinator replaces the missing solution in `vAdamSolutions` with an empty byte vector:

$$
\text{vAdamSolutions}[i] = \text{std::vector}{<}\text{unsigned char}{>}\text{()}
$$

If block assembly is delayed by $t \ge 60\text{s}$ (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60`, advancing in `30s` slots), any silent primary seat in `vAdamMiners` may be handed over strictly to a **same-track standby validator** (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`) in sequential rank order verified against the block's `CCoinsViewCache* pView` (`ResolveAdamCommitteeForTime`, `ValidateAdamCommitteeForTime`). If the block remains stalled at $t \ge 120\text{s}$ (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = 120`), the required threshold relaxes by `1` every `60s` down to **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** under Model D (`> 7` non-Masternode seats [`2 Coin-Lock + 5 Open PoW`], ensuring at least 1 Active Masternode is always required when post-bootstrap or active MNs $\ge 8$; `3` during bootstrap when active MNs $< 8$; Testnet post-bootstrap floor remains `3`), or `ADAM_MIN_RELAXED_THRESHOLD = 5` Pre-Model D (`> 4` non-Masternode seats when post-bootstrap or active MNs $\ge 7$, or `3` when active MNs $< 7$). This preserves positional alignment between `vAdamMiners` and `vAdamSolutions` while guaranteeing block recovery under severe outages.

#### 2.4.2. Validation Logic
Validating peers execute the following verification steps in `CheckBlock()`:
1. Verify that `vAdamSolutions` size matches `vAdamMiners` size and validate `vAdamMiners` against `ValidateAdamCommitteeForTime(..., pView)` for the block's elapsed time.
2. Iterate through `vAdamSolutions`. If a solution is empty, it is marked as a placeholder and skipped. If it is non-empty, calculate the puzzle hash and verify the signature and difficulty:
   * **Version 11**:

     $$\text{PuzzleHash} = \text{CalculateAdamPuzzleHash}\left(\text{algoIndex}, \text{Challenge}\right)$$

     where $\text{algoIndex} = \text{kUniformAlgos}[\text{Hash}(\text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i) \pmod{17}]$, and $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
   * **Version 12**:

     $$\text{PuzzleHash} = \text{algo1}\left( (\text{minerIdx} + 1) \times \text{algo2}\left( (\text{minerIdx} + 1) \times \text{algo3}(\text{Challenge}) \right) \right) \pmod{2^{256}}$$

     where $\text{algo1}$, $\text{algo2}$, and $\text{algo3}$ are derived using $\text{GetAdam3PermutationAlgos}(\text{hashPrevBlock}, \text{MinerPubKey}_i)$, and $\text{Challenge} = \text{Seed}_H \mathbin{\Vert} \text{MinerPubKey}_i \mathbin{\Vert} \text{Nonce}_i$.
3. Confirm that the number of cryptographically validated, non-empty solutions is greater than or equal to the effective threshold $T$.

#### 2.4.3. Security Proofs
* **Bypass Resistance**: Empty placeholders cannot satisfy signature or difficulty checks. The validation code explicitly treats placeholders as failed proofs. The thermodynamic work requirement is preserved because at least $T$ nodes must submit valid, high-difficulty proofs.
* **Censorship Non-Profitability & Equal Work → Equal Pay (`UPGRADE_FAIRNESS`)**: Under `UPGRADE_FAIRNESS` Balanced Model D (`IsModelDActive`), **Equal Work → Equal Pay** applies uniformly across all **15** committee seats within the **60%** Participant Validator pool (`60% / 15 = 4.00%` = `0.6324 KRISTA` in Period 0 per solved seat regardless of MN, Coin-Lock, Funded PoW-Lock, or Free Entry track, with **zero remainder** when 15/15 solve): every seat $i \in \lbrace 0, \dots, 14 \rbrace$ that provides a valid 33-byte-compressed PoW solution in `vAdamSolutions[i]` (whether filled by the primary validator `vExpectedMiners[i]` or by a legitimate same-track standby validator at $t \ge 60\text{s}$) receives an identical `nPartSplitTotal / 15` (`4.00%`) seat share (`75% / 11 ≈ 6.818%` across $i \in \lbrace 0, \dots, 10 \rbrace$ Pre-Model D), while unsolved/empty seats receive `0%`. Similarly, the `10%` LLMQ pool is paid with a **1-block lag strictly to members who signed `prevBlock.vQuorumSig`** (accompanied by deterministic `-5/+5` LLMQ PoSe scoring), while any unclaimed validator or unsigned LLMQ shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)**, and the Coordinator retains its full `25%` producer share (`25% + nFees` PoW; `3%` Coordinator + `22%` Staker + `100%` `nFees` PoS) when rescuing a stalled block with legitimate standby validators. Furthermore, primary validators remain protected against opportunistic substitution by: (1) a **`3,000 ms` ADAM grace window** (and **`1,000 ms` LLMQ grace window**), (2) expiration of the primary Coordinator's exclusivity window at $t = 45\text{s}$ before standby rotation opens at $t = 60\text{s}$, (3) deterministic same-track standby rank ordering (`m >= 15` under Model D; `m = 11..16` Pre-Model D), and (4) LLMQ `qblockprop` vetoes against any proposal at $t \ge 60\text{s}$ that substitutes a primary seat whose valid solution is already cached locally. Because omitting an honest seat yields **zero financial gain** for the Coordinator, the Passive Masternode Winner, or remaining validators, there is **zero financial incentive** to censor miners.

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

* **`hash160`**: Pushes the input data and computes SHA256 followed by RIPEMD160, or verifies a hash/HTLC preimage when `"mode": "htlc-equal"` or `"mode": "hash-equalverify"` (or `"type": "hash160"`).  
  * *CScript Equivalent*: `<input> OP_HASH160` (`OP_HASH160 <20-byte-hash> OP_EQUAL` in `"htlc-equal"`, or `OP_HASH160 <20-byte-hash> OP_EQUALVERIFY` in `"hash-equalverify"`)
* **`check-signature-verification`**: Pushes a public key and verifies a cryptographic signature.  
  * *CScript Equivalent*: `<pubkey> OP_CHECKSIGVERIFY`
* **`equalverify-checksig`**: Standard P2PKH validation.  
  * *CScript Equivalent*: `OP_DUP OP_HASH160 <pubkeyhash> OP_EQUALVERIFY OP_CHECKSIG`
* **`multi-signature`**: Multi-signature evaluation.  
  * *CScript Equivalent*: `<m> <pubkeys...> <n> OP_CHECKMULTISIG`
* **`lock-time`**: Enforces 32-bit unsigned time or block-height restrictions (`0..4294967295` via `ParseValidatedLockTime`).  
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
* **Starting Block Reward (Block 2+)**: **17 KRISTA** (Blocks 0 and 1 mint `0 KRISTA` on Mainnet for fair launch; cumulative emission reaches **84,983 KRISTA** / `79,034.19 KRISTA` net by Block 5,000, supporting up to **18.82 masternodes** for LLMQ and Model D formation without artificial bootstrap step-downs).

---

### 4.2. Emission Decay Model
To avoid the supply shocks of 4-year halvings, block rewards decrease gradually using a **2.15% decay every 259,200 blocks** starting from Block 2. With a block target spacing of 30 seconds, this 259,200-block interval translates to approximately every 90 days (quarterly) in calendar time. The reward at period $P$ is calculated as:

$$\text{Reward}(P) = 17.0 \times (0.9785)^P$$

Where:

$$P = \left\lfloor \frac{\text{Height} - 2}{259200} \right\rfloor$$

#### 4.2.1. Mathematical Derivation of Supply Cap and Gap Reserve
To prove that the emission model never exceeds the hard cap of 210M KRISTA, we represent the total supply as the infinite geometric series of decaying periods starting at Block 2. Let $S_{\text{max}}$ be the maximum circulating supply.

$$S_{\text{max}} = \sum_{P=0}^{\infty} \left( B_{\text{blocks}} \times R_0 \times (1 - d)^P \right)$$

Where:
* $B_{\text{blocks}} = 259,200$ (blocks per decay period)
* $R_0 = 17.0 \text{ KRISTA}$ (starting decaying reward from Block 2)
* $d = 0.0215$ (decay rate of 2.15%, so the multiplier is $1 - d = 0.9785$)

Since $0 < (1 - d) < 1$, the infinite series converges:

$$\sum_{P=0}^{\infty} (0.9785)^P = \frac{1}{1 - 0.9785} = \frac{1}{0.0215} \approx 46.511628$$

Substituting these constants (matching 128-bit integer truncation in `CMasternode::GetBlockValue`):

$$S_{\text{max}} = 259,200 \times 17.0 \times \frac{1}{0.0215}$$

$$S_{\text{max}} = 4,406,400 \times 46.511628 \approx 204,948,837 \text{ KRISTA}$$

The difference between the Hard Cap ($210,000,000$ KRISTA) and the maximum supply limit $S_{\text{max}}$ represents the **Gap Reserve** ($G_{\text{reserve}}$):

$$G_{\text{reserve}} = 210,000,000 - 204,948,837 = 5,051,163 \text{ KRISTA}$$

This Gap Reserve of **5,051,163 KRISTA (2.41%)** ensures the network can continue reward emissions for decades. This gradual decay prevents security shocks and facilitates a smooth transition to a transaction-fee security model.

```
Supply Saturation Lifecycle:
[0] (Genesis) -> [17.07M] (Year 1) -> [32.71M] (Year 2) -> [72.25M] (Year 5) -> [119.03M] (Year 10) -> [204.95M] (Asymptote)
```

---

### 4.3. Balanced Model D Cooperative Reward Split
Block rewards are split to incentivize active consensus execution alongside Masternode and quorum infrastructure.

```
+-----------------------------------------------------------------------+
|             Balanced Model D Net Block Reward Pool (100%)             |
|        (93% of Gross Reward after 7% Treasury; Mainnet 5,000+)        |
+-----------------------------------+-----------------------------------+
|    Passive + Quorum Pool (15%)    |   Active Cooperative Pool (85%)   |
+-----------------+-----------------+-----------------+-----------------+
|   Passive MN    |  LLMQ Signers   | Block Producer  |Equal Validators |
|   Queue (5%)    | 1-Blk Lag (10%) |  Winner (25%)*  | 15 Seats (60%)  |
+-----------------+-----------------+-----------------+-----------------+
* PoW: 25% Coordinator + 100% nFees | PoS: 22% Staker (+nFees) + 3% Coord
  15 Equal Validator Seats (4.00% each): 8 MN + 2 Coin-Lock + 5 Open PoW
```

* **Classic PoW Bootstrap (Blocks 2 - 199)**: 100% Miner (`15.8100 KRISTA` net in Period 0; supports initial distribution and hash power).
* **Pre-Model D Cooperative ADAM (Blocks 200 - 4,999)**: Under `UPGRADE_FAIRNESS`, **25% Block Producer (`20%` Staker + `5%` Signing Coordinator in PoS) + 75% Active ADAM Validators** (`IsAdamValidatorSplitActive`: equal `~6.818%` / `1.07795454 KRISTA` per solved seat across `11` seats [`7 MN + 1 Coin-Lock + 3 Open PoW`], `ADAM_FAIR_COORD_V4` `75%` Active MN floor), with unsolved validator shares split 50% Automatic Protocol Burn + 50% Developer Treasury while LLMQs/V12 activate at Block 4,800 (`UPGRADE_POMBL`).
* **Balanced Model D Activation (Blocks 5,000+ on Mainnet, 500 on Testnet, 200 on Regtest)**: The `15% Passive+LLMQ / 85% Active` split is enforced on the `93%` net block pool (`15.8100 KRISTA` in Period 0):
  1. **Masternode Passive Share (5% — `0.7905 KRISTA`)**: Paid to an Active Masternode deterministically selected via VRF seed modulo (`seed.GetUint64(0) % vActiveMns.size()` in `GetModelDPassiveMasternodePayee`, routed via `AppendDip3OrDirectPayment` to `dmn->state.scriptPayout` with optional `nOperatorReward` bps split to `scriptOperatorPayout` under `UPGRADE_DIP3`).
  2. **LLMQ Quorum Signer-Only Share (10% — `1.5810 KRISTA`)**: Under `UPGRADE_FAIRNESS`, divided across quorum slots (`10% / N`, e.g. `2.0%` = `0.3162 KRISTA` each for `N=5`) and paid with a **1-block lag strictly to members who signed `prevBlock.vQuorumSig`** (with `-5/+5` deterministic PoSe scoring); missed slots are forfeited (`50%` Burn + `50%` Treasury).
  3. **Block Producer / Coordinator Symmetry (25% — `3.9525 KRISTA`)**: Paid in full (`25% + nFees`) to the VRF-elected Coordinator (`ADAM_FAIR_COORD_V5` with `55%` Active MN floor) in PoW blocks; under `UPGRADE_FAIRNESS` in PoS blocks, split symmetrically into **`22%` Staker (`3.4782 KRISTA`) + `100%` of PoS tx fees (`nFees`, no longer burned) + `3%` Signing Coordinator (`0.4743 KRISTA`)**.
  4. **Equal-Work Validator Participant Share (60% — `9.4860 KRISTA`)**: Divided **equally** (`nPartSplitTotal / 15 = 4.00%` = `0.6324 KRISTA` per solved seat in Period 0, **zero remainder** when all `15/15` seats solve) across all **15 committee seats** (`8 MN + 2 Coin-Lock + 3 Funded PoW-Lock + 2 Permanent Free Entry 0 KRISTA`) regardless of track (**Equal Work → Equal Pay** — whether solved by a primary validator or a legitimate same-track standby validator at $t \ge 60\text{s}$). Active Masternodes compete for `8` of the `15` active seats (`32%`) in addition to the `5%` passive queue, `10%` LLMQ pool, and `>= 55%` Coordinator VRF floor. All unsolved empty-seat shares and unsigned LLMQ shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn** (`KTburnCYcDjF1GsXa5k3tc3eZrVVbqefyWr` on Mainnet, permanently deducted from `nMoneySupply` without crediting any personal PoB staking weight) and **50% to the Developer Treasury**.

#### 4.3.1. Hybrid Block Reward Dynamics (PoW vs. PoS)
The split adapts dynamically to the block type to support miners and stakers:
* **PoW Blocks**: The coordinator/proposer earns `25%` + `nFees` (`3.9525 KRISTA` + `nFees` in Period 0; `25%` + `nFees` Pre-Model D) and the `60%` (`9.4860 KRISTA`; `75%` Pre-Model D) active validator share is divided equally among the solved ADAM committee seats (`4.00%` = `0.6324 KRISTA`/seat across `15` seats).
* **PoS Blocks**: Under `UPGRADE_FAIRNESS`, the signing Coordinator earns `3%` (`0.4743 KRISTA` in Period 0; `5%` Pre-Model D) and the winning Staker retains `22%` (`3.4782 KRISTA` in Period 0; `20%` Pre-Model D) **plus `100%` of PoS transaction fees (`nFees`)**, while the `60%` (`75%` Pre-Model D) validator share rewards the solved ADAM committee seats (`4.00%`/seat across `15` seats).

---

### 4.4. Developer Treasury
To fund protocol development and long-term ecosystem growth, the protocol implements a direct treasury allocation:
* **Developer Treasury**: **7.0%** of the block reward is allocated to the LLMQ-governed Developer Fund Address (`KTeRC2Kj8hXcuzRohxPp2LtYFRJm2BceRoP`) from block 2 onward.

This allocation is deducted directly from the total block value. For example, during Period 0 (Blocks 2–259,201), a 17 KRISTA block reward allocates **1.19 KRISTA** (`7.0%`) to the Developer Treasury, leaving **15.8100 KRISTA** (`93.0%` net pool) distributed by active consensus rules (`100%` Miner in Blocks 2–199; `25%` Producer / `75%` Equal 11-Seat Validators in Blocks 200–4,999 under `UPGRADE_FAIRNESS`; and Balanced Model D from Block 5,000 onward: **`0.7905 KRISTA` [`5%`] Passive MN + `1.5810 KRISTA` [`10%`] Signer-Only LLMQ + `9.4860 KRISTA` [`60%`] Equal Validators [`0.6324 KRISTA × 15` seats, zero remainder] + `3.9525 KRISTA` [`25%`] Block Producer [`3.4782 KRISTA` (`22%`) Staker + `0.4743 KRISTA` (`3%`) Coordinator in PoS, plus `100%` `nFees`]**, with any empty-seat or unsigned-LLMQ shares split `50%` Burn / `50%` Treasury).

---

## 5. Security & Cryptographic Analysis

### 5.1. Sybil Attacks & Dual-Track Quota Isolation
An attacker attempting to dominate the validator selection or ticket submission pools faces strict economic and structural barriers. Under `ElectDualTrackValidators` with Balanced Model D (`UPGRADE_MODELD`), the **15 validator seats** are partitioned into **Track 1 (10 Collateralized Seats: 8 Active Masternodes [`4,200 KRISTA`] + 2 Coin-Lock [`250 KRISTA`] with intra-track spillover, locked via `static_assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= ADAM_MODELD_MAX_CONSENSUS_THRESHOLD)`)** and **Track 2 (5 Open PoW Seats: 3 Funded `PoW-Lock` [`10 KRISTA`] + 2 Permanent Free Entry [`0 KRISTA` Zero-Coin PoW, `0` Coordinator VRF weight])**. Even if an adversary captures all **7 non-Masternode seats** (`2 Coin-Lock + 5 Open PoW`), `7` remains strictly below both the post-bootstrap Model D threshold **`8` (`nAdamThresholdModelD = 8`)** and the relaxed minimum threshold **`8` (`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8 > 7` non-MN seats)**, whereas Active Masternodes alone fill **8 seats** ($\ge 8$) and maintain an exact **`>= 55%` Coordinator VRF floor (`ADAM_FAIR_COORD_V5`)** alongside proportional `4,200 : 250 : 10` locked-capital weights. (Pre-Model D `Blocks 200..4,999` enforces analogous `11`-seat isolation [`7 MN + 1 Coin-Lock + 3 Open PoW`] where `4` non-MN seats `< 5 = ADAM_MIN_RELAXED_THRESHOLD <= 7` and `ADAM_FAIR_COORD_V4` enforces a `>= 75%` MN floor.) Consequently, non-Masternode Sybil registrations can never stall or hijack block production, and controlling a consensus quorum requires locking a massive portion of the active Masternode collateral supply.

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

The KristaTech blockchain protocol presents a cooperative consensus design that addresses key limitations of traditional networks. By separating validator selection (ADAM) from block proposal (PoBLS), the protocol achieves Sybil resistance, proposer anonymity, and defense against targeted DDoS attacks. The quorum-resilient placeholder mechanism ensures chain liveness, while the MESCAL language provides a secure, gasless, and declarative environment for smart contracts. Supported by a 210 Million emission model with a 2.15% decay rate and the Model D Cooperative Split, KristaTech establishes a balanced incentive structure for miners, stakers, and masternode operators, offering a secure and sustainable framework for decentralized applications.

---

## 7. References

1. Nakamoto, S. (2008). "Bitcoin: A Peer-to-Peer Electronic Cash System."
2. Micali, S., Rabin, M., & Vadhan, S. (1999). "Verifiable Random Functions." *Proceedings of the 40th Annual Symposium on Foundations of Computer Science (FOCS)*.
3. Wood, G. (2014). "Ethereum: A Secure Decentralised Generalised Transaction Ledger."
4. Boneh, D., Gentry, C., Lynn, B., & Shacham, H. (2003). "Aggregate and Verifiable Signatures from Bilinear Maps." *Journal of Cryptology*.
5. Maymounkov, P., & Mazieres, D. (2002). "Kademlia: A Peer-to-Peer Information System Based on the XOR Metric." *International Workshop on Peer-to-Peer Systems*.
