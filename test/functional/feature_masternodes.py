#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Masternode Activation and Status.

Test that:
1. We can configure and start a masternode in regtest network.
2. The masternode enters ACTIVE status.
"""

import os
import time
from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import (
    assert_equal,
    p2p_port,
    connect_nodes,
)

class MasternodeTest(KristaTechTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.extra_args = [[], ["-port=27939"]]

    def run_test(self):
        node0 = self.nodes[0]
        node1 = self.nodes[1]

        self.log.info("Step 1: Mine blocks on node0 to get mature coins")
        node0.generate(110)
        self.sync_all()

        assert node0.getbalance() >= 4200

        self.log.info("Step 2: Generate masternode key and collateral address")
        mn_privkey = node0.createmasternodekey()
        mn_address = node0.getnewaddress("mn_collateral")

        self.log.info("Step 3: Send exactly 4200 KRISTA collateral")
        txid = node0.sendtoaddress(mn_address, 4200)
        self.log.info(f"Collateral TXID: {txid}")
        node0.generate(1)
        self.sync_all()

        # Find the vout of the 4200 KRISTA output
        tx_info = node0.getrawtransaction(txid, 1)
        vout = -1
        for i, out in enumerate(tx_info['vout']):
            if out['value'] == 4200.0:
                vout = i
                break
        assert vout != -1
        self.log.info(f"Collateral vout index: {vout}")

        # Stop nodes to write configurations
        self.log.info("Stopping nodes to write config files")
        self.stop_nodes()

        # Write masternode.conf for Node 0 (controller)
        # Masternodes on Regtest must run on default port 27939
        mn_port = 27939
        masternode_conf_path = os.path.join(node0.datadir, 'regtest', 'masternode.conf')
        with open(masternode_conf_path, 'w', encoding='utf-8') as f:
            f.write(f"mn1 127.0.0.1:{mn_port} {mn_privkey} {txid} {vout}\n")

        # Write activemasternode.conf for Node 1 (masternode)
        active_conf_path = os.path.join(node1.datadir, 'regtest', 'activemasternode.conf')
        with open(active_conf_path, 'w', encoding='utf-8') as f:
            f.write(f"mn1 {mn_privkey}\n")

        # Also write gen=0 and staking=0 for node0 config to make it a pure cold wallet
        node0_conf_path = os.path.join(node0.datadir, 'kristatech.conf')
        with open(node0_conf_path, 'a', encoding='utf-8') as f:
            f.write("staking=0\ngen=0\n")

        # Start nodes again (Node 1 with -masternode=1 and -port=27939)
        self.log.info("Starting nodes again with new config parameters")
        self.start_nodes([[], ["-masternode=1", "-port=27939"]])

        # Connect them again since they were stopped
        connect_nodes(node1, 0)

        # Mine blocks on Node 0 until the collateral transaction has 15 confirmations
        self.log.info("Mining 15 blocks to satisfy collateral confirmation requirement")
        node0.generate(15)
        self.sync_all()

        # Sync mnsync status on both nodes
        self.log.info("Waiting for mnsync to be synced on both nodes")
        for n in [node0, node1]:
            while True:
                status = n.mnsync("status")
                if status["IsBlockchainSynced"]:
                    break
                time.sleep(1)

        # Start the masternode!
        self.log.info("Activating masternode 'mn1' from Node 0")
        start_res = node0.startmasternode("alias", "0", "mn1")
        self.log.info(f"Activation result: {start_res}")
        assert start_res['result'] == 'success'

        # Wait a few seconds for the status to propagate
        time.sleep(5)

        # Verify that listmasternodes shows mn1 as ACTIVE
        mn_list = node0.listmasternodes()
        self.log.info(f"Masternode list: {mn_list}")
        assert any(mn['addr'] == mn_address and mn['status'] == 'ACTIVE' for mn in mn_list)

        self.log.info("Masternode functional test passed successfully!")

if __name__ == '__main__':
    MasternodeTest().main()
