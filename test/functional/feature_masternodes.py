#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Single-IP Multi-Masternode Activation via activemasternode.conf.

Verifies that:
1. Under the v1.5.5+ 40 KRISTA (93% net = 37.2 KRISTA) bootstrap schedule, Node 0
   accumulates mature funds and locks 3 separate 4,200 KRISTA collaterals using
   its own wallet-generated keys (no external key injection, zero mocktime).
2. Node 1 (Hot Multi-MN node on single IP:port 127.0.0.1:27939) loads 3 operator
   keys strictly from activemasternode.conf with masternode=1 and ZERO
   masternodeprivkey= entries in kristatech.conf.
3. Cold Controller (Node 0) activates all 3 Masternodes after 15 confirmations,
   and both getmasternodestatus (status=4 x 3) and listmasternodes /
   getmasternodecount (enabled=3) reach consensus across nodes.
"""

from decimal import Decimal
import os
import time

from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import (
    assert_equal,
    connect_nodes,
    wait_until,
)


class MasternodeTest(KristaTechTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.mocktime = 0
        nu_args = ["-nuparams=time_protocol_v2:2000", "-nuparams=ModelD:2000", "-nuparams=PoS:2000"]
        self.extra_args = [
            nu_args + ["-staking=0", "-gen=0"],
            nu_args + ["-port=27939", "-staking=0", "-gen=0"],
        ]

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]
        mn_port = 27939
        nu_args = ["-nuparams=time_protocol_v2:2000", "-nuparams=ModelD:2000", "-nuparams=PoS:2000"]

        self.log.info("Step 1: Generate 3 operator keys via RPC & configure activemasternode.conf on Hot Node (Node 1) at height 0")
        raw_mn_keys = [(f"mn{idx}", node0.createmasternodekey()) for idx in (1, 2, 3)]

        self.stop_node(1)
        active_conf_path = os.path.join(node1.datadir, "regtest", "activemasternode.conf")
        with open(active_conf_path, "w", encoding="utf-8") as f:
            for alias, mn_privkey in raw_mn_keys:
                f.write(f"{alias} {mn_privkey}\n")

        node1_conf_path = os.path.join(node1.datadir, "kristatech.conf")
        with open(node1_conf_path, "r", encoding="utf-8") as f:
            node1_conf_text = f.read()
        assert "masternodeprivkey=" not in node1_conf_text, "Hot Node kristatech.conf must never contain masternodeprivkey="

        self.start_node(1, nu_args + ["-masternode=1", f"-port={mn_port}", "-staking=0", "-gen=0"])
        connect_nodes(node1, 0)

        self.log.info("Step 2: Mine blocks on Node 0 to accumulate >= 12,650 mature KRISTA")
        node0.generate(355)
        while node0.getbalance() < Decimal("12650"):
            node0.generate(50)
        self.sync_all()

        bal = node0.getbalance()
        self.log.info("Node 0 mature balance: %s KRISTA", bal)
        assert bal >= Decimal("12650"), f"Expected >= 12650 KRISTA mature balance, got {bal}"

        self.log.info("Step 3: Lock 3 separate 4,200 KRISTA collaterals on Cold Controller (Node 0)")
        mn_specs = []
        for alias, mn_privkey in raw_mn_keys:
            mn_address = node0.getnewaddress(f"collateral_{alias}")
            txid = node0.sendtoaddress(mn_address, 4200)
            tx_info = node0.getrawtransaction(txid, True)
            vout = -1
            for out in tx_info["vout"]:
                if Decimal(str(out["value"])) == Decimal("4200"):
                    addrs = out.get("scriptPubKey", {}).get("addresses", [])
                    if mn_address in addrs:
                        vout = out["n"]
                        break
            assert vout != -1, f"4200 KRISTA collateral vout not found for {alias}"
            assert node0.lockunspent(False, [{"txid": txid, "vout": vout}])
            mn_specs.append((alias, mn_privkey, mn_address, txid, vout))

        self.log.info("Step 4: Mine 16 blocks so all 3 collaterals exceed 15 confirmations")
        conf_hashes = node0.generate(16)
        self.sync_all()

        # Ensure real wall-clock time has caught up with the 15th-confirmation block's median+1 timestamp (zero mocktime)
        conf15_time = node0.getblockheader(conf_hashes[14])["time"]
        wait_until(lambda: int(time.time()) >= conf15_time, timeout=120)

        self.log.info("Step 5: Write masternode.conf on Cold Controller (Node 0)")
        masternode_conf_path = os.path.join(node0.datadir, "regtest", "masternode.conf")
        with open(masternode_conf_path, "w", encoding="utf-8") as f:
            for alias, mn_privkey, _, txid, vout in mn_specs:
                f.write(f"{alias} 127.0.0.1:{mn_port} {mn_privkey} {txid} {vout}\n")

        for n in (node0, node1):
            wait_until(lambda n=n: n.mnsync("status").get("IsBlockchainSynced", False), timeout=30)

        self.log.info("Step 6: Activate all 3 Masternodes from Cold Controller (Node 0)")
        start_res = node0.startmasternode("all", "0")
        self.log.info("startmasternode result: %s", start_res)
        assert "Successfully started 3 masternodes" in start_res["overall"]
        for entry in start_res["detail"]:
            assert_equal(entry["result"], "success")

        self.log.info("Step 7: Verify Hot Node getmasternodestatus (status=4 x 3) and network MN count (enabled=3)")

        def hot_mns_ready():
            st = node1.getmasternodestatus()
            return isinstance(st, list) and len(st) == 3 and all(item.get("status") == 4 for item in st)

        wait_until(hot_mns_ready, timeout=30)
        hot_status = node1.getmasternodestatus()
        assert_equal({item["alias"] for item in hot_status}, {"mn1", "mn2", "mn3"})

        wait_until(lambda: node0.getmasternodecount().get("enabled", 0) == 3, timeout=30)
        mn_list = node0.listmasternodes()
        enabled_addrs = {m["addr"] for m in mn_list if m.get("status") in ("ACTIVE", "ENABLED")}
        for _, _, mn_address, _, _ in mn_specs:
            assert mn_address in enabled_addrs, f"Collateral address {mn_address} missing from enabled masternodes"

        self.log.info("Single-IP Multi-Masternode functional test passed successfully!")


if __name__ == "__main__":
    MasternodeTest().main()
