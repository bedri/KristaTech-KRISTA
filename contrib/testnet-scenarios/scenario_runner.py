#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Boundary-condition scenario runner for the live KristaTech test network.

Unlike regtest functional tests (which take ~40 IsRegTestNet() shortcuts in
consensus/mining code and rely on mocktime), these scenarios exercise the real
mainnet code paths on the real 5-node network in real time:

  S1  stop a seed 10s after a block            -> 45s fallback coordinator takeover
  S2  stop two seeds at 10s and 44s             -> rotationIndex 0->1 / 15s slot rounding
  S3  isolate a seed via iptables for 5 min     -> partition, resync, ban scores
  S4  restart a seed                            -> mnsync -> IsSynced transition
  S5  stop the masternode host for 3 min        -> whole LLMQ quorum unreachable
  S6  restart a seed across a DKG epoch boundary-> quorum lookup at epoch change
  S7  restart all seeds simultaneously          -> cold start / network re-assembly

Usage:
  python3 contrib/testnet-scenarios/scenario_runner.py --scenarios S1,S3 --report report.md
"""

import argparse
import time
import traceback

from common import ALL_NODES, LOCAL, SEED1, SEED2, SEED3, SEED4, SEEDS, log
from invariants import (check_no_honest_bans, collect_logs, log_offsets, max_height,
                        summarize_state, wait_for_convergence, wait_for_new_block,
                        wait_for_progress)

DKG_INTERVAL = 100  # Mainnet-parameter network (see llmq.cpp GetActiveQuorum)


class Result:
    def __init__(self, sid, title):
        self.sid = sid
        self.title = title
        self.status = "PASS"
        self.metrics = {}
        self.notes = []
        self.critical_logs = {}
        self.observations = {}
        self.started = time.time()
        self.duration = 0

    def fail(self, msg):
        self.status = "FAIL"
        self.notes.append("FAIL: " + msg)
        log("  FAIL: " + msg)

    def warn(self, msg):
        if self.status == "PASS":
            self.status = "WARN"
        self.notes.append("WARN: " + msg)
        log("  WARN: " + msg)

    def note(self, msg):
        self.notes.append(msg)
        log("  " + msg)


# --------------------------------------------------------------------------- helpers

def safety_restore():
    """Bring every seed back to a healthy, connected, running state."""
    for s in SEEDS:
        try:
            s.unisolate()
            if not s.is_running():
                s.start()
        except Exception as e:  # never let cleanup abort the run
            log(f"  cleanup error on {s.name}: {e}")


def sleep_until(t_target):
    while time.time() < t_target:
        time.sleep(0.2)


def post_checks(res, offsets, converge_timeout=360):
    ok, secs, tips = wait_for_convergence(ALL_NODES, timeout=converge_timeout)
    res.metrics["convergence_s"] = round(secs, 1)
    if not ok:
        res.fail(f"nodes did not converge within {converge_timeout}s: "
                 + ", ".join(f"{k}={(v or 'None')[:12]}" for k, v in (tips or {}).items()))
    bans = check_no_honest_bans(ALL_NODES)
    if bans:
        res.fail("honest nodes penalised each other: " + "; ".join(bans))
    res.critical_logs, res.observations = collect_logs(offsets, ALL_NODES)
    if res.critical_logs:
        res.fail("critical log lines found on " + ", ".join(res.critical_logs.keys()))


# --------------------------------------------------------------------------- scenarios

def s1_fallback_takeover(res, iterations=6):
    """Stop a non-masternode seed 10s after each block until a fallback takeover is observed."""
    rotation = [SEED1, SEED2, SEED4]
    intervals = []
    for i in range(iterations):
        target = rotation[i % len(rotation)]
        h, _ = wait_for_new_block(timeout=300)
        if h is None:
            res.fail("chain did not produce a block before the iteration started")
            return
        t_block = time.time()
        sleep_until(t_block + 10)
        target.stop()
        nh, waited = wait_for_new_block(timeout=300)
        interval = round(time.time() - t_block, 1)
        if nh is None:
            res.fail(f"iteration {i + 1}: chain stalled for >300s with {target.name} stopped")
            target.start()
            return
        intervals.append(interval)
        res.note(f"iteration {i + 1}: stopped {target.name}, next block {nh} after {interval}s")
        target.start()
        wait_for_convergence(timeout=240)
    res.metrics["block_intervals_s"] = intervals
    res.metrics["max_interval_s"] = max(intervals) if intervals else None


def s2_double_rotation(res, iterations=3):
    """Stop seed A at +10s and seed B at +44s to force rotationIndex 0 -> 1 crossing."""
    pairs = [(SEED1, SEED2), (SEED2, SEED4), (SEED4, SEED1)]
    intervals = []
    for i in range(iterations):
        a, b = pairs[i % len(pairs)]
        h, _ = wait_for_new_block(timeout=300)
        if h is None:
            res.fail("chain did not produce a block before the iteration started")
            return
        t_block = time.time()
        sleep_until(t_block + 10)
        a.stop_async()
        stopped_b = False
        sleep_until(t_block + 44)
        if max_height() == h:
            b.stop_async()
            stopped_b = True
        nh, _ = wait_for_new_block(timeout=360) if max_height() == h else (max_height(), 0)
        interval = round(time.time() - t_block, 1)
        for s in ((a, b) if stopped_b else (a,)):
            s.wait_stopped()
            s.start()
        if nh is None:
            res.fail(f"iteration {i + 1}: chain stalled for >360s with {a.name}+{b.name} stopped")
            return
        intervals.append(interval)
        res.note(f"iteration {i + 1}: stopped {a.name}@10s" + (f", {b.name}@44s" if stopped_b else " (block arrived before 44s, second stop skipped)")
                 + f" -> block {nh} after {interval}s")
        wait_for_convergence(timeout=240)
    res.metrics["block_intervals_s"] = intervals


def s3_partition(res, isolate_secs=300):
    target = SEED1
    h0 = max_height()
    target.isolate()
    try:
        t0 = time.time()
        produced_while_isolated = 0
        while time.time() - t0 < isolate_secs:
            time.sleep(15)
        others = [n for n in ALL_NODES if n is not target]
        h_others = max_height(others)
        produced_while_isolated = (h_others or h0) - h0
        res.metrics["blocks_during_isolation"] = produced_while_isolated
        res.metrics["isolated_node_height"] = target.height()
        if produced_while_isolated <= 0:
            res.fail("majority partition did not produce blocks while one seed was isolated")
    finally:
        target.unisolate()
    res.note(f"{target.name} reconnected; waiting for resync")


def s4_restart_mnsync(res):
    target = SEED2
    target.stop()
    time.sleep(20)
    target.start()
    t0 = time.time()
    synced = False
    while time.time() - t0 < 420:
        st = target.try_cli("mnsync", "status")
        if isinstance(st, dict) and st.get("IsBlockchainSynced") and st.get("RequestedMasternodeAssets") == 999:
            synced = True
            break
        time.sleep(5)
    res.metrics["mnsync_seconds"] = round(time.time() - t0, 1)
    if not synced:
        res.fail(f"{target.name} did not reach mnsync FINISHED (999) within 420s")
    ok, secs = wait_for_progress(max_height(), timeout=300)
    if not ok:
        res.fail("chain did not progress after restarted seed rejoined")


def s5_masternode_host_down(res, down_secs=180):
    h0 = max_height()
    t0 = time.time()
    SEED3.stop()
    time.sleep(down_secs)
    h1 = max_height([n for n in ALL_NODES if n is not SEED3])
    res.metrics["blocks_while_mn_host_down"] = (h1 or h0) - h0
    SEED3.start()
    ok, secs = wait_for_progress(max(h1 or h0, h0), timeout=420)
    res.metrics["recovery_after_restart_s"] = round(secs, 1)
    if (h1 or h0) == h0:
        res.warn(f"chain halted for {down_secs}s while all masternodes (whole LLMQ quorum) were offline "
                 "- expected single point of failure because all 10 MNs share one host")
    if not ok:
        res.fail("chain did not recover within 420s after masternode host restart")


def s6_dkg_boundary(res, max_wait_minutes=150):
    h = max_height()
    target_h = (h // DKG_INTERVAL + 1) * DKG_INTERVAL - 1
    if h % DKG_INTERVAL == DKG_INTERVAL - 1:
        target_h = h
    blocks_to_go = target_h - h
    res.note(f"current height {h}, waiting for DKG boundary height {target_h} ({blocks_to_go} blocks)")
    deadline = time.time() + max_wait_minutes * 60
    while (max_height() or 0) < target_h - 1 and time.time() < deadline:
        time.sleep(10)
    if (max_height() or 0) < target_h - 1:
        res.warn(f"DKG boundary not reached within {max_wait_minutes} min; scenario skipped")
        return
    SEED2.stop()
    while (max_height() or 0) < target_h + 1 and time.time() < deadline + 900:
        time.sleep(5)
    SEED2.start()
    ok, secs = wait_for_progress(target_h + 1, timeout=300)
    res.metrics["boundary_height"] = target_h + 1
    if not ok:
        res.fail("chain did not progress past the DKG epoch boundary")


def s7_cold_restart(res):
    for s in SEEDS:
        s.stop()
    time.sleep(10)
    for s in SEEDS:
        s.start()
    ok, secs = wait_for_progress(max_height(), timeout=600)
    res.metrics["first_block_after_cold_start_s"] = round(secs, 1)
    if not ok:
        res.fail("network did not produce a block within 600s after cold restart of all seeds")
    mncount = LOCAL.try_cli("getmasternodecount")
    if isinstance(mncount, dict):
        res.metrics["masternodes_enabled"] = mncount.get("enabled")


SCENARIOS = {
    "S1": ("Fallback coordinator takeover (seed stopped at +10s)", s1_fallback_takeover),
    "S2": ("Double rotation crossing (+10s and +44s)", s2_double_rotation),
    "S3": ("5 minute P2P partition of one seed", s3_partition),
    "S4": ("Seed restart and mnsync transition", s4_restart_mnsync),
    "S5": ("Masternode host offline for 3 minutes", s5_masternode_host_down),
    "S6": ("Seed restart across DKG epoch boundary", s6_dkg_boundary),
    "S7": ("Simultaneous cold restart of all seeds", s7_cold_restart),
}


def run(sid):
    title, fn = SCENARIOS[sid]
    res = Result(sid, title)
    log(f"=== {sid}: {title} ===")
    summarize_state()
    offsets = log_offsets()
    try:
        fn(res)
    except Exception as e:
        res.fail(f"exception: {e}")
        traceback.print_exc()
    finally:
        safety_restore()
    post_checks(res, offsets)
    res.duration = round(time.time() - res.started, 1)
    log(f"=== {sid} result: {res.status} ({res.duration}s) ===")
    return res


def write_report(results, path):
    lines = ["# KristaTech Testnet Boundary Scenario Report", "",
             f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S %z')}", "",
             "| Scenario | Result | Duration (s) | Key metrics |", "| :--- | :---: | ---: | :--- |"]
    for r in results:
        metrics = ", ".join(f"`{k}={v}`" for k, v in r.metrics.items())
        lines.append(f"| {r.sid} — {r.title} | **{r.status}** | {r.duration} | {metrics} |")
    for r in results:
        lines += ["", f"## {r.sid} — {r.title} ({r.status})", ""]
        for n in r.notes:
            lines.append(f"- {n}")
        if r.observations:
            lines.append("- Observations: " + ", ".join(f"`{k}={v}`" for k, v in r.observations.items()))
        for node, cl in r.critical_logs.items():
            lines += ["", f"Critical log lines on `{node}`:", "```"] + cl[-20:] + ["```"]
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    log(f"report written to {path}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenarios", default="S1,S2,S3,S4,S5,S7,S6",
                    help="comma separated scenario ids (default: all, S6 last because it waits for a DKG boundary)")
    ap.add_argument("--report", default=f"scenario_report_{time.strftime('%Y%m%d_%H%M%S')}.md")
    args = ap.parse_args()
    results = []
    for sid in [s.strip().upper() for s in args.scenarios.split(",") if s.strip()]:
        if sid not in SCENARIOS:
            raise SystemExit(f"unknown scenario {sid}")
        results.append(run(sid))
        write_report(results, args.report)
    failed = [r.sid for r in results if r.status == "FAIL"]
    log("SUMMARY: " + ", ".join(f"{r.sid}={r.status}" for r in results))
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()
