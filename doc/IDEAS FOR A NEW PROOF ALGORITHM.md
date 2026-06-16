**IDEAS FOR A NEW PROOF ALGORITHM**

**Existing proof concepts:**

1) Proof of Work (PoW)  
2) Proof of Stake (PoS)  
3) Proof of Content (PoC)  
4) Proof of Storage (PoSt or something like this)  
5) Proof of Burn (PoB)  
6) Proof of Service (PoSE)

**New proof concepts that can be used:**

1) Proof of Full Node (PoFN): This is actually provided and required by PoSE but still can be used for a base of a new proof / multi-proof concept.  
     
2) Proof of Many Transactions (PoMT): This concept looks for a proof that the user had made a certain number of transactions or maybe a certain amount of coins in a certain number of transactions. Other criteria can be set based on the transaction properties.  
     
**Multi-Proof-Algorithm (MPA) Mining Systems:**

1) PoS + PoM + PoB + PoL (MPA / PoMBL): This system uses Proof-of-Stake (PoS) as the baseline and layers on PoM, PoB, and PoL to determine the consensus weight of a block staking kernel. The total mining weight is dynamically computed via `CalculateMPAWeight()` in `src/kernel.cpp`:
   
   - **Proof of Stake (PoS - Baseline):**
     
     $$W_{\text{PoS}} = \text{Amount}$$
     
     It is the base weight multiplier of standard staking UTXOs.
     
   - **Proof of Lock (PoL):**
     Applicable to UTXOs locked using absolute (`OP_CHECKLOCKTIMEVERIFY`) or relative (`OP_CHECKSEQUENCEVERIFY`) timelocks of duration $L$ blocks up to $L_{\text{MAX}}$:
     
     $$W_{\text{PoL}} = \text{Amount} \times \left(1 + \gamma \cdot \min\left(\frac{L}{L_{\text{MAX}}}, 1.0\right)\right)$$
     
     Where $\gamma = 2.0$ (giving up to a 3x weight bonus) and $L_{\text{MAX}} = 50,000$ blocks.
     
   - **Proof of Burn (PoB):**
     Coins sent to a registered unspendable burn address receive a high starting multiplier that decays linearly over time $T$ (blocks elapsed since the burn block):
     
     $$W_{\text{PoB}} = \text{Amount} + \text{BurnAmount} \times \beta \times \left(1 - \frac{T}{T_{\text{MAX}}}\right)$$
     
     Where $\beta = 5.0$ and $T_{\text{MAX}} = 10,000$ blocks. Once $T \ge T_{\text{MAX}}$, the burn weight decays to 0 (leaving the base staking UTXO amount).
     
   - **Proof of Masternode (PoM):**
     Active Masternodes with collateral $C$ and active lifetime $t_{\text{active}}$ (blocks since transitioning to `ENABLED` status):
     
     $$W_{\text{PoM}} = C \times \left(1 + \alpha \cdot \min\left(\frac{t_{\text{active}}}{T_{\text{MAX}}}, 1.0\right)\right)$$
     
     Where $\alpha = 1.0$ (yielding up to 2x weight) and $T_{\text{MAX}} = 10,000$ blocks. Any state changes out of `ENABLED` reset the active lifetime to 0.

2) LLMQ Quorum Signatures:
   Rather than using BLS keys directly, the consensus leverages a deterministic Long-Living Masternode Quorum (LLMQ) built on secp256k1. Members are selected deterministically using the rolling VRF seed and coordinate DKG to sign block headers (populating `vQuorumSig` in version 12 blocks).

**General Procedure/Approach (ADAM + MPA)**

1) Active Masternode pool acts as the election basis (`GetAdamMinerPool`).
2) A rolling VRF seed is computed deterministically for each block height.
3) Elected miners execute lightweight PoW puzzles.
4) The Coordinator aggregates the solutions meeting the quorum threshold (defined by the `nAdamThreshold` consensus parameter).
5) Stakers build block templates, collect the verified miner solutions from the cache, obtain the Coordinator's VRF proof and block signature, and sign the block with the staking UTXO key (`vchBlockSig`), achieving dual-signature consensus.

REFERENCES:

[1] https://en.bitcoin.it/wiki/Timelock
