# KristaTech Testnet Boundary Scenario Report

Generated: 2026-11-01 13:46:35 +0300

| Scenario | Result | Duration (s) | Key metrics |
| :--- | :---: | ---: | :--- |
| S5 — Masternode host offline for 3 minutes | **WARN** | 203.8 | `blocks_while_mn_host_down=0`, `recovery_after_restart_s=3.9`, `convergence_s=0.8` |
| S2 — Double rotation crossing (+10s and +44s) | **PASS** | 220.0 | `block_intervals_s=[46.5, 63.9, 46.7]`, `convergence_s=0.8` |

## S5 — Masternode host offline for 3 minutes (WARN)

- WARN: chain halted for 180s while all masternodes (whole LLMQ quorum) were offline - expected single point of failure because all 10 MNs share one host
- Observations: `fallback_takeover=8`, `quorum_met=1`, `quorum_failed=25`, `quorum_abort=3`, `not_in_quorum=0`, `misbehaving=0`

## S2 — Double rotation crossing (+10s and +44s) (PASS)

- iteration 1: stopped seed1@10s (block arrived before 44s, second stop skipped) -> block 4904 after 46.5s
- iteration 2: stopped seed2@10s, seed4@44s -> block 4906 after 63.9s
- iteration 3: stopped seed4@10s (block arrived before 44s, second stop skipped) -> block 4909 after 46.7s
- Observations: `fallback_takeover=3`, `quorum_met=7`, `quorum_failed=0`, `quorum_abort=3`, `not_in_quorum=0`, `misbehaving=0`
