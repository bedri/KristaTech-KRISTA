# KristaTech Testnet Boundary Scenario Report

Generated: 2026-11-01 00:42:28 +0300

| Scenario | Result | Duration (s) | Key metrics |
| :--- | :---: | ---: | :--- |
| S1 — Fallback coordinator takeover (seed stopped at +10s) | **PASS** | 470.7 | `block_intervals_s=[68.0, 31.0, 44.4, 13.9, 47.5, 73.6]`, `max_interval_s=73.6`, `convergence_s=0.9` |
| S2 — Double rotation crossing (+10s and +44s) | **PASS** | 244.6 | `block_intervals_s=[46.7, 54.1, 46.7]`, `convergence_s=0.9` |
| S3 — 5 minute P2P partition of one seed | **PASS** | 346.5 | `blocks_during_isolation=8`, `isolated_node_height=4498`, `convergence_s=28.3` |
| S4 — Seed restart and mnsync transition | **PASS** | 77.8 | `mnsync_seconds=15.9`, `convergence_s=0.8` |
| S5 — Masternode host offline for 3 minutes | **PASS** | 229.0 | `blocks_while_mn_host_down=5`, `recovery_after_restart_s=27.1`, `convergence_s=0.8` |
| S7 — Simultaneous cold restart of all seeds | **PASS** | 65.0 | `first_block_after_cold_start_s=20.8`, `masternodes_enabled=10`, `convergence_s=0.8` |
| S6 — Seed restart across DKG epoch boundary | **PASS** | 2452.0 | `boundary_height=4600`, `convergence_s=0.8` |

## S1 — Fallback coordinator takeover (seed stopped at +10s) (PASS)

- iteration 1: stopped seed1, next block 4480 after 68.0s
- iteration 2: stopped seed2, next block 4482 after 31.0s
- iteration 3: stopped seed4, next block 4484 after 44.4s
- iteration 4: stopped seed1, next block 4486 after 13.9s
- iteration 5: stopped seed2, next block 4488 after 47.5s
- iteration 6: stopped seed4, next block 4490 after 73.6s
- Observations: `fallback_takeover=12`, `quorum_met=6`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`

## S2 — Double rotation crossing (+10s and +44s) (PASS)

- iteration 1: stopped seed1@10s, seed2@44s -> block 4493 after 46.7s
- iteration 2: stopped seed2@10s, seed4@44s -> block 4495 after 54.1s
- iteration 3: stopped seed4@10s, seed1@44s -> block 4497 after 46.7s
- Observations: `fallback_takeover=4`, `quorum_met=5`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`

## S3 — 5 minute P2P partition of one seed (PASS)

- seed1 reconnected; waiting for resync
- Observations: `fallback_takeover=7`, `quorum_met=3`, `quorum_failed=0`, `quorum_abort=1`, `not_in_quorum=0`, `misbehaving=0`

## S4 — Seed restart and mnsync transition (PASS)

- Observations: `fallback_takeover=2`, `quorum_met=1`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`

## S5 — Masternode host offline for 3 minutes (PASS)

- Observations: `fallback_takeover=4`, `quorum_met=3`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`

## S7 — Simultaneous cold restart of all seeds (PASS)

- Observations: `fallback_takeover=2`, `quorum_met=0`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`

## S6 — Seed restart across DKG epoch boundary (PASS)

- current height 4519, waiting for DKG boundary height 4599 (80 blocks)
- Observations: `fallback_takeover=35`, `quorum_met=44`, `quorum_failed=0`, `quorum_abort=0`, `not_in_quorum=0`, `misbehaving=0`
