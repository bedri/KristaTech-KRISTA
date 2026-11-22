#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test KTDIP3 Single-IP Multi-Masternode & wallet.dat mnmeta persistence.

Verifies that:
1. Under UPGRADE_DIP3=1 on Regtest, Cold Controller (Node 0) operates with ZERO
   masternode.conf file on disk and retired legacy RPCs (listmasternodeconf,
   startmasternode, createmasternodebroadcast) return -32601 Method not found.
2. Node 1 (Hot Multi-MN host on single IP:port 127.0.0.1:27939) loads 3 ECDSA+BLS
   operator keypairs strictly from activemasternode.conf with masternode=1 and
   ZERO masternodeprivkey= entries in kristatech.conf.
3. Cold Controller (Node 0) locks 3 separate 4,200 KRISTA collaterals, mines >=15
   confirmations, registers all 3 on-chain via protx register (no external keys
   injected into wallet, zero mocktime), and stores local aliases inside
   wallet.dat via protx set_alias.
4. Both Cold Controller protx wallet_list / protx list valid and Hot Node 1
   getmasternodestatus (status=4 x 3) reach enabled consensus (enabled=3).
5. Restarting Node 0 preserves all mnmeta aliases and locked collaterals in
   wallet.dat without creating masternode.conf, and protx remove_meta refuses
   to unlock collaterals of active on-chain Masternodes.
"""

from decimal import Decimal
import os

from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
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

        self.log.info("Step 1: Generate 3 ECDSA+BLS operator keypairs via RPC & configure activemasternode.conf on Hot Node (Node 1)")
        raw_mn_keys = []
        for idx in (1, 2, 3):
            mn_wif = node0.createmasternodekey()
            bls_pair = node0.protx("bls_generate")
            raw_mn_keys.append((f"mn{idx}", mn_wif, bls_pair["secret"], bls_pair["public"]))

        self.stop_node(1)
        active_conf_path = os.path.join(node1.datadir, "regtest", "activemasternode.conf")
        with open(active_conf_path, "w", encoding="utf-8") as f:
            for alias, mn_privkey, bls_secret, _ in raw_mn_keys:
                f.write(f"{alias} {mn_privkey} {bls_secret}\n")

        node1_conf_path = os.path.join(node1.datadir, "kristatech.conf")
        with open(node1_conf_path, "r", encoding="utf-8") as f:
            node1_conf_text = f.read()
        assert "masternodeprivkey=" not in node1_conf_text, "Hot Node kristatech.conf must never contain masternodeprivkey="

        self.start_node(1, nu_args + ["-masternode=1", f"-port={mn_port}", "-staking=0", "-gen=0"])
        connect_nodes(node1, 0)

        self.log.info("Step 2: Verify retired Cold Controller RPCs return -32601 and zero masternode.conf exists")
        legacy_mn_conf = os.path.join(node0.datadir, "regtest", "masternode.conf")
        assert not os.path.exists(legacy_mn_conf), f"Unexpected legacy masternode.conf at {legacy_mn_conf}"
        assert_raises_rpc_error(-32601, "Method not found", node0.listmasternodeconf)
        assert_raises_rpc_error(-32601, "Method not found", node0.startmasternode, "all", "0")
        assert_raises_rpc_error(-32601, "Method not found", node0.createmasternodebroadcast, "all")

        self.log.info("Step 3: Mine blocks on Node 0 to accumulate >= 12,650 mature KRISTA")
        node0.generate(355)
        while node0.getbalance() < Decimal("12650"):
            node0.generate(50)
        self.sync_all()

        bal = node0.getbalance()
        self.log.info("Node 0 mature balance: %s KRISTA", bal)
        assert bal >= Decimal("12650"), f"Expected >= 12650 KRISTA mature balance, got {bal}"

        self.log.info("Step 4: Lock 3 separate 4,200 KRISTA collaterals on Cold Controller (Node 0)")
        mn_specs = []
        for alias, _, _, bls_pub in raw_mn_keys:
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
            mn_specs.append((alias, bls_pub, mn_address, txid, vout))

        self.log.info("Step 5: Mine 16 blocks so all 3 collaterals exceed 15 confirmations")
        node0.generate(16)
        self.sync_all()

        for n in (node0, node1):
            wait_until(lambda n=n: n.mnsync("status").get("IsBlockchainSynced", False), timeout=30)

        self.log.info("Step 6: Register all 3 Masternodes on-chain via protx register (single IP:port Rule 8)")
        protx_hashes = {}
        for alias, bls_pub, mn_address, txid, vout in mn_specs:
            pro_tx_hash = node0.protx(
                "register",
                txid,
                vout,
                f"127.0.0.1:{mn_port}",
                mn_address,
                bls_pub,
                mn_address,
                0,
                mn_address,
            )
            protx_hashes[alias] = pro_tx_hash
        node0.generate(1)
        self.sync_all()

        self.log.info("Step 7: Store aliases in wallet.dat via protx set_alias and verify protx wallet_list")
        # Exercise both proTxHash and outpoint lookup forms
        set1 = node0.protx("set_alias", protx_hashes["mn1"], "mn1")
        assert_equal(set1["alias"], "mn1")
        mn2_spec = mn_specs[1]
        set2 = node0.protx("set_alias", f"{mn2_spec[3]}:{mn2_spec[4]}", "mn2")
        assert_equal(set2["alias"], "mn2")
        set3 = node0.protx("set_alias", protx_hashes["mn3"], "mn3")
        assert_equal(set3["alias"], "mn3")

        # Duplicate alias must be rejected cleanly
        assert_raises_rpc_error(-8, "already in use", node0.protx, "set_alias", protx_hashes["mn2"], "mn1")

        wallet_list = node0.protx("wallet_list")
        assert_equal(len(wallet_list), 3)
        assert_equal({item["alias"] for item in wallet_list}, {"mn1", "mn2", "mn3"})
        for item in wallet_list:
            assert_equal(item["onChain"], True)
            assert_equal(item["status"], "ENABLED")
            assert_equal(item["service"], f"127.0.0.1:{mn_port}")
            assert "privateKey" not in item and "blsSecret" not in item

        self.log.info("Step 8: Verify protx list valid, getmasternodecount, and Hot Node getmasternodestatus (status=4 x 3)")
        assert_equal(len(node0.protx("list", "valid")), 3)
        wait_until(lambda: node1.getmasternodecount().get("enabled", 0) == 3, timeout=30)

        def hot_mns_ready():
            st = node1.getmasternodestatus()
            return isinstance(st, list) and len(st) == 3 and all(item.get("status") == 4 for item in st)

        wait_until(hot_mns_ready, timeout=45)
        hot_status = node1.getmasternodestatus()
        assert_equal({item["alias"] for item in hot_status}, {"mn1", "mn2", "mn3"})

        self.log.info("Step 9: Restart Cold Controller (Node 0) and verify wallet.dat persistence with ZERO masternode.conf")
        self.restart_node(0, nu_args + ["-staking=0", "-gen=0"])
        connect_nodes(node0, 1)
        self.sync_all()

        assert not os.path.exists(legacy_mn_conf), "Restart must never create masternode.conf"
        restarted_list = node0.protx("wallet_list")
        assert_equal(len(restarted_list), 3)
        assert_equal({item["alias"] for item in restarted_list}, {"mn1", "mn2", "mn3"})

        locked_outpoints = {(u["txid"], u["vout"]) for u in node0.listlockunspent()}
        for _, _, _, txid, vout in mn_specs:
            assert (txid, vout) in locked_outpoints, f"Collateral {(txid, vout)} must stay locked after restart"

        self.log.info("Step 10: Verify protx remove_meta deletes alias metadata while refusing to unlock active on-chain collateral")
        rem = node0.protx("remove_meta", "mn3", True)
        assert_equal(rem["removed"], True)
        assert_equal(rem["unlocked"], False)

        after_remove = node0.protx("wallet_list")
        assert_equal({item["alias"] for item in after_remove}, {"mn1", "mn2", ""})
        locked_after = {(u["txid"], u["vout"]) for u in node0.listlockunspent()}
        assert (mn_specs[2][3], mn_specs[2][4]) in locked_after

        self.log.info("KTDIP3 Single-IP Multi-Masternode & wallet.dat persistence functional test passed successfully!")


if __name__ == "__main__":
    MasternodeTest().main()
