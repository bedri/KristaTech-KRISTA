```
KTIP: 0001
Title: KTIP Process
Author: KristaTech Core Developers
Status: Active
Type: Process
Created: 2026-06-16
```

## Abstract
This document defines the KristaTech Improvement Proposal (KTIP) process. A KTIP is a design document providing information to the KristaTech community, or describing a new feature for KristaTech or its processes or environment.

## Motivation
KristaTech requires a standardized, transparent, and structured process for introducing, discussing, and documenting network improvements, protocol modifications, and consensus upgrades. By establishing the KTIP process, we ensure that technical specifications are detailed, backward compatibility is evaluated, and implementation details are archived for developers and network participants.

## KTIP Types
We classify KTIPs into three distinct categories:
1. **Standard Track**: Any change that affects the core protocol, block validation rules, consensus parameters, cryptographic primitives, P2P network serialization, or RPC interface.
2. **Informational**: Guidelines, design patterns, or general information relevant to the ecosystem, developers, and mining pools.
3. **Process**: Proposals to alter the development workflow, code compilation guidelines, continuous integration pipeline, or documentation structures.

## KTIP Lifecycle
A KTIP transitions through the following states during its lifetime:

```mermaid
stateDiagram-v2
    [*] --> Draft : Author submits draft
    Draft --> Proposed : Peer review requested
    Proposed --> Active : Deployed on Testnet/Regtest
    Proposed --> Rejected : Abandoned or rejected
    Active --> Final : Activated on Mainnet via consensus/spork
    Active --> Rejected : Reverted/replaced
```

* **Draft**: The proposal is in progress and actively edited by the authors.
* **Proposed**: The proposal is complete, formally submitted, and undergoing review by core developers and the community.
* **Active**: The proposal has been approved and its implementation is deployed and testing on the Testnet and Regtest environments.
* **Final**: The proposal has been successfully activated on Mainnet, either through a specific block height consensus trigger or a Spork activation.
* **Rejected**: The proposal is closed without activation, either due to security flaws, lack of consensus, or replacement by a superior proposal.

## Document Template
Every KTIP must begin with a metadata header block (enclosed in a text block) and contain the following structured sections:
1. **Abstract**: A concise technical summary of the proposed change (under 200 words).
2. **Motivation**: The rationale explaining why the current protocol is insufficient and why the upgrade is necessary.
3. **Specification**: The exact technical details of the proposal, including mathematical formulas, data structures, state machines, and consensus parameters.
4. **Backward Compatibility**: An evaluation of the upgrade's impact on legacy nodes (e.g., Soft Fork, Hard Fork, or Spork-controlled).
5. **Reference Implementation**: Links to the pull requests, commits, or source code files implementing the specification.

## Index of Active KTIPs

| KTIP | Title | Type | Status | Documents |
| :--- | :--- | :--- | :--- | :--- |
| **KTIP-0001** | KTIP Process | Process | Active | [EN](KTIP-0001_KTIP_Process.md) / [TR](KTIP-0001_KTIP_Process_TR.md) |
| **KTIP-0002** | ADAM Cooperative Consensus | Standard Track (Consensus) | Active | [EN](KTIP-0002_ADAM.md) / [TR](KTIP-0002_ADAM_TR.md) |
| **KTIP-0003** | PoMPA Weight & Dual Mining | Standard Track (Consensus) | Active | [EN](KTIP-0003_PoMPA_Dual_Mining.md) / [TR](KTIP-0003_PoMPA_Dual_Mining_TR.md) |
| **KTIP-0004** | PoMBL Dual-Signature Validation | Standard Track (Consensus) | Active | [EN](KTIP-0004_PoMBL.md) / [TR](KTIP-0004_PoMBL_TR.md) |
| **KTIP-0005** | PoBLS Hybrid ECDSA/BLS12-381 Signature Consensus | Standard Track (Consensus) | Active | [EN](KTIP-0005_PoBLS.md) / [TR](KTIP-0005_PoBLS_TR.md) |
| **KTIP-0006** | Model D Network Upgrade (LLMQ, DKG & Reward Splits) | Standard Track (Consensus) | Active | [EN](KTIP-0006_Model_D.md) / [TR](KTIP-0006_Model_D_TR.md) |
| **KTIP-0007** | Protocol Scaling Upgrade (8 MB Blocks, 30s Spacing, BIP152) | Standard Track (Consensus) | Active | [EN](KTIP-0007_Scaling_Upgrade.md) / [TR](KTIP-0007_Scaling_Upgrade_TR.md) |
| **KTIP-0008** | MESCAL Declarative Smart Contracts & Taproot (P2TR / BIP-350) | Standard Track (Consensus / Script) | Active | [EN](KTIP-0008_MESCAL.md) / [TR](KTIP-0008_MESCAL_TR.md) |
| **KTIP-0009** | Dual-Path Miner Registration & Dual-Track ($7 + 1 + 3$) Quotas | Standard Track (Consensus) | Active | [EN](KTIP-0009_Miner_Registration_Dual_Track.md) / [TR](KTIP-0009_Miner_Registration_Dual_Track_TR.md) |
| **KTIP-0010** | On-Chain Heartbeat Pings, Standby Handover, Relaxed Quorum & Equal-Work Incentives | Standard Track (Consensus / Economics) | Active | [EN](KTIP-0010_Consensus_Liveness_Incentives.md) / [TR](KTIP-0010_Consensus_Liveness_Incentives_TR.md) |
| **KTIP-0011** | Sentry Node DDoS Shielding, Bounded Future Blocks Queue & LLMQ Treasury Governance | Standard Track (Network / Governance) | Active | [EN](KTIP-0011_Sentry_FutureBlocks_Treasury.md) / [TR](KTIP-0011_Sentry_FutureBlocks_Treasury_TR.md) |
