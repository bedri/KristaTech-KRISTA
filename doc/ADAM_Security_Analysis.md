# ADAM Consensus Quorum Resiliency and Placeholder Security Analysis

## 1. Executive Summary

This document evaluates the security posture and cryptographic safety of the quorum-resilient block template generation and validation mechanism in the **ADAM (A Decentralized Approach Model)** consensus framework of the KRISTA network. 

Previously, the cooperative mining loop required a 100% response rate from all elected nodes (11-14 miners in Fallback Mode, 15 miners in Balanced Model D Standard Mode / 11 miners Pre-Model D) to successfully construct and propagate a block. Under this model, if even a single node went offline, delayed its solution, or experienced a network partition, the Coordinator could not build a block template. This resulted in chain freezes and compromised network liveness.

To resolve this, we introduced a **Quorum-Resilient Responding and Placeholder Mechanism**:
1. The block template is finalized and propagated as long as a minimum quorum of valid solutions is met (`GetAdamThreshold(nHeight)`):
   - **Bootstrap Phase (`nHeight < nAdamThresholdBootstrapLimit`, i.e., below block 50,000 on Mainnet / 100,000 on Testnet / 1,000 on Regtest)**: At least **5** valid solutions from the elected miners (out of `15` under Model D, or `11` Pre-Model D).
   - **Standard Phase (`nHeight >= nAdamThresholdBootstrapLimit`, Version 11 & Version 12)**: At least **8** of `15` under Balanced Model D (`nAdamThresholdModelD = 8` on Mainnet/Regtest; **7** of `11` Pre-Model D) or **3** (on Testnet) valid solutions from the elected miners.
2. Missing solutions are represented within the block header's serialization format using **empty vector placeholders** (`std::vector<unsigned char>()`).
3. This analysis demonstrates that the placeholder mechanism preserves the cryptographic security of the consensus model, maintains backward compatibility, and mitigates key attack vectors (including forgery, coordinator censorship, payout theft, tampering, and replay attacks) while significantly improving network liveness.

---

## 2. Technical Mechanism and Validation Workflow

### 2.1. Placeholder Insertion during Block Template Generation
When the Coordinator's staking/mining thread (`CreateNewBlock()`) attempts to construct a block template:
1. It queries the active elected miners list for the block's seed (`SelectAdamNodes()`).
2. It fetches available partial PoW puzzle solutions matching these elected miners' public keys from the network's consensus cache.
3. If the number of collected valid solutions is less than the required quorum threshold ($T$), the block template generation is deferred.
4. If the number of valid solutions meets or exceeds $T$, but some elected miners are missing, the Coordinator substitutes the missing miner solutions with an empty byte vector:

   $$
   \text{vAdamSolutions}[i] = \text{std::vector}{<}\text{unsigned char}{>}\text{()}
   $$

5. This ensures that `vAdamSolutions` preserves a 1:1 positional mapping with the elected miners listed in `vAdamMiners`.

### 2.2. Network Validation Logic
Upon receiving a block, every validating peer executes the consensus verification checks in `CheckBlock()` and `ContextualCheckBlockHeader()`:
1. **Array Boundary Validation**: Check that the size of `vAdamSolutions` matches the expected number of miners (e.g., $N = 15$ under Balanced Model D Standard Mode, or $N = 11$ Pre-Model D).
2. **Quorum Verification**:
   - Loop through `vAdamSolutions`.
   - If a solution is empty (`std::vector<unsigned char>()`), it is treated as a placeholder. The verification function `VerifyAdamSolution()` immediately returns `false` (or is bypassed), and this placeholder is **not** counted toward the valid solutions.
   - If a solution is non-empty, the peer validates the partial PoW target hash difficulty and verifies the miner's cryptographic signature against the elected miner's public key. If both checks pass, the valid solutions count is incremented.
3. **Threshold Enforcement**: The block is rejected with a consensus violation error if the count of cryptographically validated, non-empty solutions is less than the required threshold ($T$).

---

## 3. Threat Modeling & Security Mitigations

### 3.1. Cryptographic Forgery & Bypass Protection
* **Threat**: An attacker attempts to use empty placeholders to bypass Proof-of-Work checks or forge miner signatures, submitting a block with fewer than the required number of physical solutions.
* **Mitigation**:
  - Empty placeholders are mathematically incapable of satisfying signature or difficulty checks. The validation code explicitly treats them as failed solutions.
  - The block verification rules enforce that at least $T = \text{GetAdamThreshold}(nHeight)$ (`5` during bootstrap, `8` of `15` under Model D [`7` of `11` Pre-Model D] on Mainnet/Regtest post-bootstrap, or `3` on Testnet post-bootstrap) solutions must be *fully valid, non-empty, cryptographically signed, and meet the target difficulty*.
  - An attacker cannot bypass the physical work requirement; they must still perform the necessary multi-algorithm hashing computations for at least $T$ seats to build a block that the network will accept.

### 3.2. Coordinator Abuse & Miner Censorship
* **Threat**: A malicious Coordinator deliberately censors honest miners by omitting their solutions and replacing them with empty placeholders, attempting to shut them out of block participation.
* **Mitigation**:
  - The degree of censorship a Coordinator can perform is strictly capped by the quorum threshold $T = \text{GetAdamThreshold}(nHeight)$.
  - In Fallback Mode ($N \in \lbrace 11, \dots, 14 \rbrace$, or up to $18$ if Model D is active pre-POMBL) and Standard Mode ($N = 15$ under Model D; $N = 11$ Pre-Model D), with $T = 5$ during bootstrap, $T = 8$ under Model D ($7$ Pre-Model D) on Mainnet/Regtest post-bootstrap, and $T = 3$ on Testnet post-bootstrap, the Coordinator can censor at most $N - T$ miners (e.g., at most `7` on Mainnet/Regtest post-bootstrap when $N = 15$ and $T = 8$ under Model D; at most `4` Pre-Model D when $N = 11$ and $T = 7$).
  - If a Coordinator attempts to censor more miners, the block will fail validation at all peer nodes and be rejected.
  - In addition, because the Masternode network (`KTDIP3` / `UPGRADE_DIP3`, tracked deterministically in `evodb`) dynamically rotates coordinators and miners every block height using a Verifiable Random Function (VRF) powered by deterministic BLS12-381 signatures (`SignBLSOperatorOnly` / `VerifyAdamOrDip3Signature` for KTDIP3 Masternodes from `UPGRADE_DIP3 = 1` on Mainnet & Testnet, or `SignBLSWithECDSAFallback` for legacy / non-MN validators), a malicious coordinator only has a temporary opportunity to censor. They cannot lock out a miner indefinitely.

### 3.3. Block Reward and Payout Security (No Financial Incentive to Censor)
* **Threat**: A Coordinator excludes a miner's solution to steal their portion of the block reward or redirect it to their own address or remaining validator seats.
* **Mitigation**:
  - The block reward distribution is governed deterministically by consensus rules at the protocol level. Payout addresses are calculated by querying `SelectAdamNodes()` for the elected public keys at that block height.
  - Under Balanced Model D (`IsModelDActive` with `UPGRADE_FAIRNESS` active from Block 1), **Equal Work → Equal Pay** applies strictly across all 15 committee seats (`8 MN + 2 Coin-Lock + 3 Funded PoW-Lock + 2 Free Entry`): every seat $i \in \lbrace 0, \dots, 14 \rbrace$ that provides a valid PoW solution in `vAdamSolutions[i]` (with mandatory 33-byte compressed validator pubkey) — whether filled by the original primary validator `vExpectedMiners[i]` OR by a legitimate same-track standby validator `vAdamMiners[i]` (standby indices `m >= 15` under Model D, `m = 11..16` Pre-Model D) when the primary seat timed out ($t \ge 60\text{s}$) — receives the exact same equal share **`60% / 15 = 4.00%`** (`0.6324 KRISTA` in Period 0, with zero integer truncation remainder when all `15/15` committee seats solve; Pre-Model D `75% / 11 ≈ 6.818%` / `1.07795454 KRISTA` across `11` seats) regardless of MN, Coin-Lock, or Open-PoW track. Unsolved/empty seats receive `0%`, while the Coordinator retains its full **`25%`** producer reward (`3.9525 KRISTA` in Period 0; **`22%` Staker [`3.4782 KRISTA`] + `3%` Signing Coordinator [`0.4743 KRISTA`] + `100%` `nFees`** in PoS; Pre-Model D `20% + 5%`) when rescuing a stalled block with standby validators. Any unclaimed empty-seat or unsigned-LLMQ shares (`nUnclaimedPartSplit`) are split **50% to the Automatic Protocol Burn (`nExpectedBurn = nUnclaimedPartSplit / 2`)** and **50% to the Developer Treasury (`nExpectedTreasury = nTreasurySplit + (nUnclaimedPartSplit - nExpectedBurn)`)** while the Passive Masternode Winner receives its exact **`5%`** (`nMNSplit` = `0.7905 KRISTA` in Period 0) and active LLMQ members who signed `prevBlock.vQuorumSig` share **`10%`** (`10% / N`, `1.5810 KRISTA` in Period 0, 1-block lag, `-5/+5` PoSe).
  - Furthermore, in `qblockprop` (evaluated only when the LLMQ member's tip matches `msg.block.hashPrevBlock`), at $t \ge 60\text{s}$ LLMQ quorum members verify their local `mapAdamSolutionsCache[hashPrevBlock]` and refuse to sign (`QUORUMSIGSHARE`) any block proposal that substitutes a primary seat (`vAdamMiners[i] != vExpectedMiners[i]`) when a valid solution from `vExpectedMiners[i]` is already cached locally. Because neither the Coordinator's net reward (`0` net gain), nor the Passive Masternode Winner's `5%` share, nor the fixed reward of any active seat increases when a seat is omitted or substituted, a Coordinator has **zero financial incentive** to censor honest validators.

### 3.4. Replay and Tampering Attacks
* **Threat**: An attacker replays valid solutions from a previous block or tries to modify transaction data in the block template after the miners have signed their solutions.
* **Mitigation**:
  - **Seed Binding**: Each miner signs a unique puzzle hash derived from the rolling election seed (`Challenge = Seed_H || CompactSize(MinerPubKey_i) || MinerPubKey_i || LE32(Nonce_i)`):
    - In **Version 11** (`UPGRADE_ADAM` to `UPGRADE_POMBL - 1`): single-stage `CalculateAdamPuzzleHash(GetAdamPuzzleAlgo(Seed_H, MinerPubKey_i), Challenge)`.
    - In **Version 12** (`UPGRADE_POMBL`+): 3-stage compound permutation `GetAdam3PermutationAlgos(hashPrevBlock, MinerPubKey_i)` applied sequentially over `Challenge`.

    The rolling seed `Seed_H` is derived from the previous block's VRF proof and is unique to the current block height. A solution signed for height $H$ cannot be replayed at height $H+1$ because the seeds (and `hashPrevBlock`) will not match, causing signature and difficulty validation to fail.
  - **Header Integrity**: The final block header hash (`CBlockHeader::GetHash()`, which checks `fFallbackMode = (nVersion == 11) || (vAdamMiners.size() == vAdamSolutions.size() + 1)` so all `15` Version 12 seats hash sequentially in 3-permutation mode) binds all transactions (via `hashMerkleRoot`), the block time, the previous block hash, the VRF proof, and the list of elected miners.
  - **Multi-Layer Block Signatures**: After the block template is finalized (with or without placeholders), the Coordinator signs the entire block header hash (`vAdamCoordinatorSig`, verified via `VerifyAdamOrDip3Signature` / `VerifyBLSWithECDSAFallback` — pure `blsPubKeyOperator` under **KTDIP3 (`UPGRADE_DIP3 = 1` Mainnet & Testnet)**, or hybrid BLS+ECDSA when inactive/non-MN), and once `UPGRADE_MODELD` activates, the active LLMQ (`5–15` members scaling dynamically with active MN count) co-signs the block via a strict BFT majority (`3-of-5`, `5-of-9`, `6-of-11`, or `8-of-15`) aggregated BLS12-381 quorum certificate (`vQuorumSig`). For **Cooperative PoS blocks (`IsProofOfStake()`)** at heights $\ge 200$, the staker additionally signs the block header using their staking key (`vchBlockSig`), whereas ADAM PoW blocks leave `vchBlockSig` empty. Any alteration of transactions or block metadata invalidates these overarching signatures, preventing any post-hoc tampering by intermediate nodes.

### 3.5. Nothing-at-Stake and Sybil Resiliency
* **Threat**: Attackers sign blocks on multiple forks at zero cost or spin up virtual nodes to dominate the leader election.
* **Mitigation**:
  - The requirement for physical PoW computation on $T$ elected nodes ensures that creating blocks on competing forks remains computationally expensive. Staking on multiple forks is not free, mitigating nothing-at-stake.
  - Under the **Dual-Track Quota Election (`ElectDualTrackValidators`, active at `nAdamDualTrackHeight = UPGRADE_ADAM`)**, **`UPGRADE_FAIRNESS` (Height `1`)**, and **`UPGRADE_MODELD` (`5,000` Mainnet / `500` Testnet / `200` Regtest)**, the **15 validator seats** (`11` Pre-Model D) are partitioned into **Track 1 (`10` Collateralized Seats: `8` Active KTDIP3 Masternodes [`4,200 KRISTA`, `32%` validator share] + `2` strictly isolated Coin-Lock [`250 KRISTA`, `8%`])** and **Track 2 (`5` Open PoW Seats [`20%`]: `3` Funded `10 KRISTA` PoW-Lock / Bootstrap [`12%`] + `2` permanent `0 KRISTA` Open-Entry seats [`8%`] from `nAdamOpenEntryHeight` [`2,500` Mainnet, `1` Testnet/Regtest]**, with unselected Coin-Locks excluded from `Tier 2A`, `AutoRegisterMiner` gating automatic Coin-Lock on `< 2` seats under Model D [`< 1` Pre-Model D], `GetAdamMinersCount(h) + 2 = 17` funded cap prior to `nAdamOpenEntryHeight` [`13` Pre-Model D], expanded to `240` Zero-Coin keys [`MAX_ZEROCOIN_BOOTSTRAP_MINERS_FAIRNESS`] with `HashX11KVS` 15-bit Mainnet PoW, a `10`-block challenge window, and a `240`-block lease TTL [`50` post-Model-D on Testnet/Regtest]; Pre-Model D `7 + 1 + 3`)**, while the Coordinator is elected via **Proportional Locked-Capital VRF (`4,200` per Active MN, `250` per `Coin-Lock`, and `10` per `Funded PoW-Lock`, no per-track caps: `ADAM_FAIR_COORD_V5` enforcing an exact `>= 55%` Active MN security floor when `11 * sumNonMN > 9 * sumMN` under Model D, and `ADAM_FAIR_COORD_V4` enforcing `>= 75%` when `3 * sumNonMN > sumMN` Pre-Model D; `0` Coordinator weight for `0-KRISTA` Open-Entry miners while any funded validator exists; falling back to `Bootstrap -> Zero-Coin PoW` only when no funded validators exist)**. Vacant committee seats spill over in strict priority: **Active Masternode $\to$ Open PoW $\to$ Coin-Lock (last resort)**. Because `ADAM_MODELD_MASTERNODE_TRACK_QUOTA = 8` is locked at compile time (`static_assert`) and verified at runtime in `chainparams.cpp` (`assert(ADAM_MODELD_MASTERNODE_TRACK_QUOTA >= consensus.nAdamThresholdModelD)`), **the `7` non-Masternode seats (`2 Coin-Lock + 5 Open PoW`) are strictly `< 8` post-bootstrap threshold so non-MNs alone CANNOT bypass Active Masternodes, while the `8` Active Masternode seats (`8 >= 8`) guarantee liveness even if all `7` non-Masternode seats go silent or withhold solutions** (Pre-Model D: `7` MN seats `>= 7` even if all `4` non-MNs withhold solutions). Furthermore, if any Masternode is offline while non-Masternode seats withhold solutions, **Standby Validator Seat Handover (`ADAM_COMMITTEE_FALLBACK_TIMEOUT_SECS = 60s`)** hands silent seats only to same-track standby validators in strict sequential rank order (`MN -> MN`, `Coin-Lock -> Coin-Lock`, `Open PoW -> Open PoW`, standby indices `m >= 15` under Model D vs `11..16` Pre-Model D, verified against the block's `CCoinsViewCache* pView`, with equal **`60% / 15 = 4.00%`** [`0.6324 KRISTA` in Period 0, `0` remainder at `15/15`; Pre-Model D `75% / 11`] reward for any solved seat, full **`25%`** rescue Coordinator reward, LLMQ anti-censorship protection for primary solutions in `qblockprop`, and unclaimed empty-seat shares split `50%` Automatic Protocol Burn and `50%` Developer Treasury), and **Time-Relaxed Quorum Threshold (`ADAM_THRESHOLD_RELAXATION_TIMEOUT_SECS = 120s`)** enforces a Model D post-bootstrap (and `>= 8` MN) floor of **`ADAM_MODELD_MIN_RELAXED_THRESHOLD = 8`** (`> 7` non-MN seats and `<= 8` MN seats, ensuring at least `1` Active Masternode is always required post-bootstrap; relaxing bootstrap `< 50,000` down to `3` only when active MNs `< 8`, and `ADAM_MIN_RELAXED_THRESHOLD = 5` `> 4` non-MN seats Pre-Model D), preventing any permanent block stall.

---

## 4. Conclusion

The introduction of the quorum-resilient placeholder mechanism represents a major security and robustness upgrade for the KRISTA network. It successfully:
* **Restores Liveness**: Eliminates the single point of failure where a single offline or lagging miner could freeze the entire blockchain.
* **Preserves Safety**: Enforces the same cryptographic and thermodynamic security guarantees of the ADAM consensus model by ensuring that a substantial majority of elected validators must actively participate and sign.
* **Zero Incentive for Abuse**: Eliminates any financial incentive for coordinator censorship by decoupling the physical solution inclusion from the deterministic reward payout logic.
