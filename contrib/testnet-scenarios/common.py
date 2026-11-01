#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Shared helpers for driving the live KristaTech test network over RPC/SSH.

This tooling never alters consensus code, never uses mocktime, and never
injects keys into wallets. It only starts/stops daemons, toggles P2P
reachability with iptables (root seeds only) and reads RPC state / logs.
"""

import json
import os
import shlex
import subprocess
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

SSH_OPTS = [
    "-o", "StrictHostKeyChecking=no",
    "-o", "ConnectTimeout=15",
    "-o", "ServerAliveInterval=15",
    "-o", "ControlMaster=auto",
    "-o", "ControlPath=~/.ssh/cm-krista-%r@%h:%p",
    "-o", "ControlPersist=30m",
]

SSH_ENV = dict(os.environ)
if os.path.exists("/usr/bin/ksshaskpass"):
    SSH_ENV.setdefault("SSH_ASKPASS", "/usr/bin/ksshaskpass")
    SSH_ENV.setdefault("SSH_ASKPASS_REQUIRE", "force")

P2P_PORT = 27999
IPTABLES_TAG = "krista-scenario"


class RPCError(Exception):
    pass


def log(msg):
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


class Node:
    def __init__(self, name, ssh=None, ip=None, root=False, hosts_masternodes=False):
        self.name = name
        self.ssh = ssh
        self.ip = ip
        self.root = root
        self.hosts_masternodes = hosts_masternodes

    # ------------------------------------------------------------------ shell
    @property
    def is_local(self):
        return self.ssh is None

    def sh(self, cmd, timeout=90):
        """Run a shell command on the node. Returns (returncode, stdout+stderr)."""
        if self.is_local:
            args = ["bash", "-c", cmd]
        else:
            args = ["ssh", *SSH_OPTS, self.ssh, cmd]
        try:
            p = subprocess.run(args, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=timeout, env=SSH_ENV, text=True)
            return p.returncode, p.stdout.strip()
        except subprocess.TimeoutExpired:
            return 124, "timeout"

    @property
    def cli_path(self):
        return str(REPO / "src" / "kristatech-cli") if self.is_local else "~/kristatech-cli"

    @property
    def daemon_path(self):
        return "~/kristatechd"

    @property
    def debug_log(self):
        return "~/.kristatech/debug.log"

    # -------------------------------------------------------------------- rpc
    def cli(self, *args, timeout=60):
        cmd = self.cli_path + " " + " ".join(shlex.quote(str(a)) for a in args)
        rc, out = self.sh(cmd, timeout=timeout)
        if rc != 0:
            raise RPCError(f"{self.name}: {' '.join(map(str, args))} -> rc={rc}: {out[-300:]}")
        try:
            return json.loads(out)
        except (ValueError, json.JSONDecodeError):
            return out

    def try_cli(self, *args, timeout=60):
        try:
            return self.cli(*args, timeout=timeout)
        except RPCError:
            return None

    def height(self):
        h = self.try_cli("getblockcount", timeout=20)
        return int(h) if isinstance(h, int) or (isinstance(h, str) and h.isdigit()) else None

    def tip(self):
        t = self.try_cli("getbestblockhash", timeout=20)
        return t if isinstance(t, str) and len(t) == 64 else None

    # -------------------------------------------------------------- lifecycle
    def is_running(self):
        rc, _ = self.sh("pgrep -x kristatechd >/dev/null", timeout=20)
        return rc == 0

    def stop(self, wait=120):
        if self.is_local:
            raise RuntimeError("The local Qt cold wallet is never stopped by the scenario runner")
        log(f"  stopping {self.name}")
        self.try_cli("stop", timeout=30)
        deadline = time.time() + wait
        while time.time() < deadline:
            if not self.is_running():
                return True
            time.sleep(2)
        log(f"  {self.name} did not stop gracefully, sending SIGKILL")
        self.sh("pkill -9 -x kristatechd", timeout=20)
        time.sleep(2)
        return not self.is_running()

    def stop_async(self):
        """Send the RPC stop command without waiting for the process to exit (precise timing)."""
        if self.is_local:
            raise RuntimeError("The local Qt cold wallet is never stopped by the scenario runner")
        log(f"  stopping {self.name} (async)")
        self.sh(f"{self.cli_path} stop >/dev/null 2>&1 &", timeout=15)

    def wait_stopped(self, wait=120):
        deadline = time.time() + wait
        while time.time() < deadline:
            if not self.is_running():
                return True
            time.sleep(2)
        self.sh("pkill -9 -x kristatechd", timeout=20)
        time.sleep(2)
        return not self.is_running()

    def start(self, wait=300):
        if self.is_local:
            raise RuntimeError("The local Qt cold wallet is never started by the scenario runner")
        log(f"  starting {self.name}")
        self.sh(f"nohup {self.daemon_path} -daemon >/dev/null 2>&1 </dev/null &", timeout=30)
        deadline = time.time() + wait
        while time.time() < deadline:
            if self.height() is not None:
                self.try_cli("setgenerate", "true", "1")
                return True
            time.sleep(3)
        return False

    # -------------------------------------------------------------- isolation
    def _iptables_rules(self):
        return [
            f"INPUT -p tcp --dport {P2P_PORT} -m comment --comment {IPTABLES_TAG} -j DROP",
            f"INPUT -p tcp --sport {P2P_PORT} -m comment --comment {IPTABLES_TAG} -j DROP",
            f"OUTPUT -p tcp --dport {P2P_PORT} -m comment --comment {IPTABLES_TAG} -j DROP",
            f"OUTPUT -p tcp --sport {P2P_PORT} -m comment --comment {IPTABLES_TAG} -j DROP",
        ]

    def isolate(self):
        if not self.root:
            raise RuntimeError(f"{self.name} has no root access for iptables isolation")
        log(f"  isolating {self.name} from P2P port {P2P_PORT}")
        for rule in self._iptables_rules():
            self.sh(f"iptables -I {rule}")

    def unisolate(self):
        if not self.root:
            return
        log(f"  removing P2P isolation on {self.name}")
        for rule in self._iptables_rules():
            # delete every copy of the rule (idempotent cleanup)
            self.sh(f"while iptables -D {rule} 2>/dev/null; do :; done")

    # ------------------------------------------------------------------- logs
    def log_offset(self):
        rc, out = self.sh(f"stat -c %s {self.debug_log} 2>/dev/null || echo 0", timeout=20)
        try:
            return int(out.splitlines()[-1])
        except (ValueError, IndexError):
            return 0

    def grep_log_since(self, offset, pattern, max_lines=200):
        """Return log lines matching an extended regex since a byte offset."""
        cmd = (f"tail -c +{offset + 1} {self.debug_log} 2>/dev/null | "
               f"grep -E {shlex.quote(pattern)} | tail -n {max_lines}")
        rc, out = self.sh(cmd, timeout=60)
        return [l for l in out.splitlines() if l.strip()] if out and out != "timeout" else []

    def __repr__(self):
        return f"<Node {self.name}>"


LOCAL = Node("local")
SEED1 = Node("seed1", "root@168.119.227.178", "168.119.227.178", root=True)
SEED2 = Node("seed2", "root@95.217.188.96", "95.217.188.96", root=True)
SEED3 = Node("seed3", "bedri@157.90.130.224", "157.90.130.224", root=False, hosts_masternodes=True)
SEED4 = Node("seed4", "root@167.233.98.102", "167.233.98.102", root=True)

ALL_NODES = [LOCAL, SEED1, SEED2, SEED3, SEED4]
SEEDS = [SEED1, SEED2, SEED3, SEED4]
SEED_IPS = {n.ip for n in SEEDS}
