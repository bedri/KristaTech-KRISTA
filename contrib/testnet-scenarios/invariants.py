#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Network-wide invariants checked during and after every scenario."""

import time

from common import ALL_NODES, SEED_IPS, log

# Log patterns that indicate a consensus / P2P failure between honest nodes.
CRITICAL_LOG_PATTERNS = (
    r"Signature is empty"
    r"|bad-cb-payee"
    r"|BAN THRESHOLD EXCEEDED"
    r"|ERROR: ConnectBlock"
    r"|bad-adam-coord-sig"
    r"|Invalid coordinator signature or VRF proof"
    r"|Invalid signature verification from outpoint"
)

# Informational patterns that describe which recovery paths were exercised.
OBSERVATION_PATTERNS = {
    "fallback_takeover": r"Taking over as PoBLS XOR-ranked fallback coordinator|Rotating to PoBLS XOR-ranked fallback coordinator",
    "quorum_met": r"Quorum threshold met",
    "quorum_failed": r"Failed to collect enough quorum signatures",
    "quorum_abort": r"Aborting quorum signature wait",
    "not_in_quorum": r"which is not in active quorum",
    "misbehaving": r"Misbehaving: ",
}


def snapshot(nodes=ALL_NODES):
    return {n.name: (n.height(), n.tip()) for n in nodes}


def max_height(nodes=ALL_NODES):
    hs = [h for h in (n.height() for n in nodes) if h is not None]
    return max(hs) if hs else None


def wait_for_new_block(nodes=ALL_NODES, timeout=300):
    """Block until the best height across nodes increases. Returns (height, seconds_waited) or (None, timeout)."""
    start_h = max_height(nodes)
    t0 = time.time()
    while time.time() - t0 < timeout:
        h = max_height(nodes)
        if h is not None and start_h is not None and h > start_h:
            return h, time.time() - t0
        time.sleep(1)
    return None, timeout


def wait_for_progress(start_height, nodes=ALL_NODES, timeout=240):
    t0 = time.time()
    while time.time() - t0 < timeout:
        h = max_height(nodes)
        if h is not None and h > start_height:
            return True, time.time() - t0
        time.sleep(2)
    return False, timeout


def wait_for_convergence(nodes=ALL_NODES, timeout=300):
    """All reachable nodes must report the same best block hash at the same instant."""
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        tips = {n.name: n.tip() for n in nodes}
        last = tips
        values = set(tips.values())
        if None not in values and len(values) == 1:
            return True, time.time() - t0, tips
        time.sleep(3)
    return False, timeout, last


def check_no_honest_bans(nodes=ALL_NODES):
    """No seed may be banned, and no peer connection between seeds may carry a ban score."""
    problems = []
    for n in nodes:
        banned = n.try_cli("listbanned") or []
        for b in banned if isinstance(banned, list) else []:
            addr = b.get("address", "").split("/")[0]
            if addr in SEED_IPS:
                problems.append(f"{n.name} has banned seed {addr}")
        peers = n.try_cli("getpeerinfo") or []
        for p in peers if isinstance(peers, list) else []:
            ip = p.get("addr", "").rsplit(":", 1)[0]
            score = p.get("banscore", 0)
            if ip in SEED_IPS and score:
                problems.append(f"{n.name} -> {ip} banscore={score}")
    return problems


def log_offsets(nodes=ALL_NODES):
    return {n.name: n.log_offset() for n in nodes}


def collect_logs(offsets, nodes=ALL_NODES):
    """Return (critical_lines, observation_counts) since the recorded offsets."""
    critical = {}
    observations = {k: 0 for k in OBSERVATION_PATTERNS}
    for n in nodes:
        off = offsets.get(n.name, 0)
        lines = n.grep_log_since(off, CRITICAL_LOG_PATTERNS, max_lines=50)
        if lines:
            critical[n.name] = lines
        for key, pat in OBSERVATION_PATTERNS.items():
            observations[key] += len(n.grep_log_since(off, pat, max_lines=10000))
    return critical, observations


def summarize_state(nodes=ALL_NODES):
    snap = snapshot(nodes)
    for name, (h, t) in snap.items():
        log(f"    {name:6s} height={h} tip={t[:16] if t else None}")
    return snap
