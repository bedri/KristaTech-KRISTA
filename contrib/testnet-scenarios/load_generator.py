#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Staged transaction load generator and performance sampler for the live test network.

Every node (local Qt + 4 seeds) runs a local bash worker that issues wallet RPCs
against its own daemon (no per-transaction SSH round trip). Traffic mix:

  ~80%  sendtoaddress   small payment to another node
  ~10%  sendmany        50-output payment to another node
  ~10%  MESCAL Taproot  payment to a P2TR address produced by compilemescaltotaproot

Sampled metrics: block interval, tx/block, block size, mempool size per node,
RPC latency per node (measured on the node itself), CreateNewBlock quorum wait,
ConnectBlock bench timings (-debug=bench enabled at runtime) and, when the
binaries were built with -DDEBUG_LOCKCONTENTION, LOCKCONTENTION hot spots.

Usage:
  python3 contrib/testnet-scenarios/load_generator.py --stages 10,50,200,500 --minutes 10
"""

import argparse
import collections
import json
import re
import shlex
import statistics
import threading
import time

from common import ALL_NODES, log

MESCAL_CONTRACT = json.dumps({
    "basic": {
        "Lock": {"role": "lock-time", "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": 100000}]},
        "Num": {"role": "number", "inputs": [{"name": "Number", "type": "number", "value": 7}]},
        "Eq": {"role": "num-equal"},
    },
    "contract": {"C": {"actions": [{"type": "basic", "name": "Lock"},
                                   {"type": "basic", "name": "Num"},
                                   {"type": "basic", "name": "Eq"}]}},
    "active_contract": "C",
}, separators=(",", ":"))

WORKER_LOG = "/tmp/krista_load_worker.log"


def pct(values, p):
    if not values:
        return None
    s = sorted(values)
    return round(s[min(len(s) - 1, int(len(s) * p))], 1)


# --------------------------------------------------------------------------- setup

def prepare(nodes, split_outputs, split_amount):
    """Collect destination addresses, a MESCAL P2TR address per node, and pre-split UTXOs."""
    info = {}
    for n in nodes:
        addrs = [n.cli("getnewaddress", "loadtest") for _ in range(50)]
        ik_addr = n.cli("getnewaddress", "loadtest_ik")
        rc, out = n.sh(f"{n.cli_path} validateaddress {ik_addr} | grep -m1 '\"pubkey\"'")
        m = re.search(r'"0[23]([0-9a-f]{64})"', out)
        p2tr = None
        if m:
            res = n.try_cli("compilemescaltotaproot", MESCAL_CONTRACT, m.group(1))
            if isinstance(res, dict):
                p2tr = res.get("address")
        info[n.name] = {"addrs": addrs, "p2tr": p2tr}
        log(f"  {n.name}: 50 receive addresses, MESCAL P2TR={p2tr}")

    log(f"  pre-splitting {split_outputs} x {split_amount} KRISTA UTXOs on every node")
    for n in nodes:
        outs = {a: split_amount for a in info[n.name]["addrs"][:split_outputs]} if split_outputs <= 50 else None
        if outs is None:
            extra = [n.cli("getnewaddress", "loadtest_split") for _ in range(split_outputs - 50)]
            outs = {a: split_amount for a in info[n.name]["addrs"] + extra}
        txid = n.try_cli("sendmany", "", json.dumps(outs))
        log(f"    {n.name} split tx: {txid}")
    return info


def worker_script(node, peers_info, rate_per_min, minutes):
    """Bash worker executed on the node itself."""
    dests = []
    many_dest = []
    p2trs = []
    for name, inf in peers_info.items():
        if name == node.name:
            continue
        dests += inf["addrs"][:10]
        if not many_dest:
            many_dest = inf["addrs"][:50]
        if inf["p2tr"]:
            p2trs.append(inf["p2tr"])
    sendmany_json = json.dumps({a: 0.01 for a in many_dest}, separators=(",", ":"))
    total = max(1, int(rate_per_min * minutes))
    interval = 60.0 / rate_per_min
    cli = node.cli_path
    return f"""
CLI={cli}
DESTS=({' '.join(dests)})
P2TR=({' '.join(p2trs)})
MANY={shlex.quote(sendmany_json)}
: > {WORKER_LOG}
for i in $(seq 1 {total}); do
  t0=$(date +%s%N)
  r=$((i % 10))
  if [ $r -eq 0 ]; then
    out=$($CLI sendmany "" "$MANY" 2>&1); kind=many
  elif [ $r -eq 5 ] && [ ${{#P2TR[@]}} -gt 0 ]; then
    out=$($CLI sendtoaddress ${{P2TR[$((RANDOM % ${{#P2TR[@]}}))]}} 0.0$((RANDOM % 9 + 1)) 2>&1); kind=mescal
  else
    out=$($CLI sendtoaddress ${{DESTS[$((RANDOM % ${{#DESTS[@]}}))]}} 0.$((RANDOM % 89 + 10)) 2>&1); kind=pay
  fi
  rc=$?
  dt=$(( ($(date +%s%N) - t0) / 1000000 ))
  if [ $rc -eq 0 ]; then echo "ok $kind $dt" >> {WORKER_LOG}; else echo "fail $kind $dt $(echo "$out" | tr '\\n' ' ' | cut -c1-160)" >> {WORKER_LOG}; fi
  sl=$(awk -v a={interval} -v b=$dt 'BEGIN{{s=a-b/1000; if(s<0)s=0; printf "%.3f", s}}')
  sleep $sl
done
echo done >> {WORKER_LOG}
"""


def launch_worker(node, script):
    escaped = shlex.quote(script)
    node.sh(f"nohup bash -c {escaped} >/dev/null 2>&1 </dev/null &", timeout=30)


def worker_results(node):
    rc, out = node.sh(f"cat {WORKER_LOG} 2>/dev/null", timeout=60)
    ok = fail = 0
    lat = []
    errors = collections.Counter()
    kinds = collections.Counter()
    for line in out.splitlines():
        parts = line.split(" ", 3)
        if parts[0] == "ok" and len(parts) >= 3:
            ok += 1
            kinds[parts[1]] += 1
            lat.append(int(parts[2]))
        elif parts[0] == "fail" and len(parts) >= 3:
            fail += 1
            msg = parts[3] if len(parts) > 3 else ""
            m = re.search(r'"message":"([^"]{0,90})', msg)
            errors[m.group(1) if m else msg[:90]] += 1
    return {"ok": ok, "fail": fail, "lat": lat, "errors": errors, "kinds": kinds,
            "done": "done" in out.splitlines()[-1:] if out else False}


# --------------------------------------------------------------------------- sampler

class Sampler(threading.Thread):
    def __init__(self, nodes, period=15):
        super().__init__(daemon=True)
        self.nodes = nodes
        self.period = period
        self.stop_flag = threading.Event()
        self.mempool = collections.defaultdict(list)
        self.rpc_ms = collections.defaultdict(list)
        self.blocks = {}

    def run(self):
        local = self.nodes[0]
        while not self.stop_flag.is_set():
            for n in self.nodes:
                rc, out = n.sh(f"t0=$(date +%s%N); {n.cli_path} getmempoolinfo; echo RPCMS=$(( ($(date +%s%N)-t0)/1000000 ))", timeout=40)
                m = re.search(r'"size":\s*(\d+)', out)
                if m:
                    self.mempool[n.name].append(int(m.group(1)))
                m = re.search(r"RPCMS=(\d+)", out)
                if m:
                    self.rpc_ms[n.name].append(int(m.group(1)))
            h = local.height()
            if h is not None:
                for hh in range(max(h - 3, 1), h + 1):
                    if hh not in self.blocks:
                        bh = local.try_cli("getblockhash", hh)
                        b = local.try_cli("getblock", bh) if isinstance(bh, str) else None
                        if isinstance(b, dict):
                            self.blocks[hh] = {"time": b.get("time"), "ntx": len(b.get("tx", [])), "size": b.get("size")}
            self.stop_flag.wait(self.period)

    def snapshot_and_reset(self):
        snap = {"mempool": dict(self.mempool), "rpc_ms": dict(self.rpc_ms), "blocks": dict(self.blocks)}
        self.mempool.clear()
        self.rpc_ms.clear()
        self.blocks.clear()
        return snap


# --------------------------------------------------------------------------- log metrics

def log_metrics(nodes, offsets):
    quorum_ms, connect_ms = [], []
    contention = collections.Counter()
    for n in nodes:
        off = offsets[n.name]
        for line in n.grep_log_since(off, r"Quorum threshold met", max_lines=100000):
            m = re.search(r"after waiting (\d+) ms", line)
            if m:
                quorum_ms.append(int(m.group(1)))
        for line in n.grep_log_since(off, r"- Connect block: ", max_lines=100000):
            m = re.search(r"Connect block: ([\d.]+)ms", line)
            if m:
                connect_ms.append(float(m.group(1)))
        for line in n.grep_log_since(off, r"^.*Locker: ", max_lines=200000):
            m = re.search(r"Locker: (\S+)", line)
            if m:
                contention[m.group(1)] += 1
    return quorum_ms, connect_ms, contention


# --------------------------------------------------------------------------- main

def run_stage(nodes, info, sampler, rate_total, minutes):
    per_node = rate_total / len(nodes)
    log(f"=== stage {rate_total} tx/min ({per_node:.1f}/node) for {minutes} min ===")
    offsets = {n.name: n.log_offset() for n in nodes}
    sampler.snapshot_and_reset()
    for n in nodes:
        launch_worker(n, worker_script(n, info, per_node, minutes))
    deadline = time.time() + minutes * 60 + 120
    time.sleep(minutes * 60)
    while time.time() < deadline:
        if all(worker_results(n)["done"] for n in nodes):
            break
        time.sleep(10)
    time.sleep(90)  # let the mempool drain into a couple of blocks
    snap = sampler.snapshot_and_reset()
    results = {n.name: worker_results(n) for n in nodes}
    quorum_ms, connect_ms, contention = log_metrics(nodes, offsets)
    return {"rate": rate_total, "minutes": minutes, "workers": results, "snap": snap,
            "quorum_ms": quorum_ms, "connect_ms": connect_ms, "contention": contention}


def stage_markdown(st):
    w = st["workers"]
    ok = sum(r["ok"] for r in w.values())
    fail = sum(r["fail"] for r in w.values())
    blocks = st["snap"]["blocks"]
    hs = sorted(blocks)
    intervals = [blocks[b]["time"] - blocks[a]["time"] for a, b in zip(hs, hs[1:]) if blocks[a]["time"] and blocks[b]["time"]]
    ntx = [blocks[h]["ntx"] for h in hs]
    sizes = [blocks[h]["size"] for h in hs if blocks[h]["size"]]
    lines = [f"## Stage: {st['rate']} tx/min for {st['minutes']} min", "",
             f"- Transactions accepted: **{ok}** ({ok / max(st['minutes'], 1):.1f}/min), rejected: **{fail}**",
             f"- Blocks sampled: {len(hs)}, interval avg/max: {statistics.mean(intervals):.1f}s / {max(intervals)}s" if intervals else "- Blocks sampled: n/a",
             f"- Tx per block avg/max: {statistics.mean(ntx):.1f} / {max(ntx)}, block size max: {max(sizes) if sizes else 'n/a'} bytes" if ntx else "",
             f"- CreateNewBlock quorum wait p50/p95/max: {pct(st['quorum_ms'], .5)} / {pct(st['quorum_ms'], .95)} / {max(st['quorum_ms']) if st['quorum_ms'] else None} ms ({len(st['quorum_ms'])} samples)",
             f"- ConnectBlock p50/p95/max: {pct(st['connect_ms'], .5)} / {pct(st['connect_ms'], .95)} / {max(st['connect_ms']) if st['connect_ms'] else None} ms ({len(st['connect_ms'])} samples)",
             "", "| Node | ok | fail | send RPC p50/p95 ms | mempool max | getmempoolinfo p95 ms | Top error |",
             "| :--- | ---: | ---: | :--- | ---: | ---: | :--- |"]
    for name, r in w.items():
        mp = st["snap"]["mempool"].get(name, [])
        rpc = st["snap"]["rpc_ms"].get(name, [])
        top = r["errors"].most_common(1)
        lines.append(f"| {name} | {r['ok']} | {r['fail']} | {pct(r['lat'], .5)} / {pct(r['lat'], .95)} | "
                     f"{max(mp) if mp else 'n/a'} | {pct(rpc, .95)} | {(top[0][0] + ' x' + str(top[0][1])) if top else ''} |")
    if st["contention"]:
        lines += ["", "Top lock contention sites (`LOCKCONTENTION` Locker lines):", "",
                  "| Site | Count |", "| :--- | ---: |"]
        for site, c in st["contention"].most_common(15):
            lines.append(f"| `{site}` | {c} |")
    return "\n".join(l for l in lines if l is not None) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stages", default="10,50,200,500", help="total tx/min per stage across all nodes")
    ap.add_argument("--minutes", type=float, default=10)
    ap.add_argument("--split-outputs", type=int, default=100)
    ap.add_argument("--split-amount", type=float, default=20)
    ap.add_argument("--skip-prepare-wait", action="store_true")
    ap.add_argument("--report", default=f"load_test_report_{time.strftime('%Y%m%d_%H%M%S')}.md")
    args = ap.parse_args()
    nodes = ALL_NODES

    for n in nodes:
        n.try_cli("logging", '["bench"]')
    try:
        info = prepare(nodes, args.split_outputs, args.split_amount)
        if not args.skip_prepare_wait:
            h0 = nodes[0].height()
            log("  waiting for 2 confirmations of the split transactions")
            while (nodes[0].height() or 0) < (h0 or 0) + 2:
                time.sleep(10)
        sampler = Sampler(nodes)
        sampler.start()
        stages = []
        with open(args.report, "w", encoding="utf-8") as f:
            f.write(f"# KristaTech Testnet Load Test Report\n\nGenerated: {time.strftime('%Y-%m-%d %H:%M:%S %z')}\n\n")
        for rate in [int(x) for x in args.stages.split(",") if x.strip()]:
            st = run_stage(nodes, info, sampler, rate, args.minutes)
            stages.append(st)
            md = stage_markdown(st)
            print(md, flush=True)
            with open(args.report, "a", encoding="utf-8") as f:
                f.write(md + "\n")
        sampler.stop_flag.set()
        log(f"report written to {args.report}")
    finally:
        for n in nodes:
            n.try_cli("logging", "[]", '["bench"]')
            n.sh(f"pkill -f {WORKER_LOG} 2>/dev/null; true")


if __name__ == "__main__":
    main()
