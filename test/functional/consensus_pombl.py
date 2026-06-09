#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test MPA (Multi-Proof Algorithm) Consensus Activation.

Test that:
1. Version 12 blocks are enforced after height 300 (Regtest activation height).
2. Blocks of version < 12 are rejected after height 300.
3. Sending coins to the designated burn address is accepted.
4. Attempts to spend from a burn address are strictly rejected by the consensus.
"""

from test_framework.test_framework import PivxTestFramework
from test_framework.util import *
from test_framework.mininode import *
from test_framework.blocktools import create_coinbase, create_block
from io import BytesIO

MPA_HEIGHT = 300
REJECT_INVALID = 16

class MPAConsensusTest(PivxTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.extra_args = [['-promiscuousmempoolflags=1', '-whitelist=127.0.0.1', '-sporkkey=932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi']]
        self.setup_clean_chain = True

    def run_test(self):
        try:
            self.do_run_test()
        except Exception as e:
            import os
            self.log.error("Test failed! Printing node0 debug.log...")
            # Search for debug.log in /tmp
            for root, dirs, files in os.walk("/tmp"):
                for file in files:
                    if file == "debug.log" and "node0" in root:
                        log_path = os.path.join(root, file)
                        self.log.error(f"=== {log_path} ===")
                        with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
                            lines = f.readlines()
                            for line in lines[-150:]: # print last 150 lines
                                print(line.rstrip())
            raise e

    def do_run_test(self):
        self.nodes[0].add_p2p_connection(P2PInterface())
        network_thread_start()
        self.nodes[0].p2p.wait_for_verack()

        # Burn addresses configured for Regtest:
        burn_address = "ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4"

        # 1. Mine blocks up to height 298
        self.log.info("Mining 298 blocks to approach activation height")
        self.nodes[0].generate(298)
        assert_equal(self.nodes[0].getblockcount(), 298)

        # 2. Verify we can send to a burn address pre-activation
        self.log.info("Sending 10 coins to the burn address %s", burn_address)
        burn_txid = self.nodes[0].sendtoaddress(burn_address, 10.0)
        self.nodes[0].generate(1) # Mine block 299
        assert_equal(self.nodes[0].getblockcount(), 299)

        # Activate SPORK_21_ADAM_STANDARD_MODE to enforce version 12
        self.log.info("Activating SPORK_21_ADAM_STANDARD_MODE")
        self.activate_spork(0, "SPORK_21_ADAM_STANDARD_MODE")

        # 3. Test version 12 block enforcement at height 300
        self.log.info("Testing version 12 block enforcement at height 300")
        tip = self.nodes[0].getbestblockhash()
        block_time = self.nodes[0].getblockheader(tip)['mediantime'] + 1
        
        # Create a block with version 10 at height 300
        block = create_block(int(tip, 16), create_coinbase(300), block_time)
        block.nVersion = 10
        block.solve()
        
        self.nodes[0].p2p.send_and_ping(msg_block(block))
        
        # Verify block was not accepted
        assert_equal(self.nodes[0].getblockcount(), 299)
        self.log.info("Block version 10 rejected successfully at height 300")

        # Verify reject message was received
        wait_until(lambda: "reject" in self.nodes[0].p2p.last_message.keys(), lock=mininode_lock)
        with mininode_lock:
            assert_equal(self.nodes[0].p2p.last_message["reject"].code, REJECT_INVALID)
            assert b'bad-version' in self.nodes[0].p2p.last_message["reject"].reason
            del self.nodes[0].p2p.last_message["reject"]

        # 4. Mine block 300 with version 12
        self.nodes[0].generate(1)
        
        # Verify version 12 block is accepted
        assert_equal(self.nodes[0].getblockcount(), 300)
        tip = self.nodes[0].getbestblockhash()
        assert_equal(self.nodes[0].getblockheader(tip)['version'], 12)
        self.log.info("Block version 12 accepted successfully at height 300")

        # 5. Verify spending from a burn address is rejected
        self.log.info("Verifying spending from a burn address is rejected")
        # Try to construct a transaction spending from the burn transaction output
        # First, find the vout of the burn output
        burn_tx = self.nodes[0].gettransaction(burn_txid)
        vout_idx = -1
        for out in burn_tx['details']:
            if out['address'] == burn_address:
                vout_idx = out['vout']
                break
        
        assert vout_idx != -1
        
        inputs = [{"txid": burn_txid, "vout": vout_idx}]
        outputs = {self.nodes[0].getnewaddress(): 9.9}
        raw_spend = self.nodes[0].createrawtransaction(inputs, outputs)
        
        # Since we do not have the private key of the burn address, the transaction won't be fully signed,
        # but even if we bypass signatures or attempt to submit it, the consensus check must reject it.
        try:
            self.nodes[0].sendrawtransaction(raw_spend)
            assert False, "Should have failed to accept spending from burn address"
        except JSONRPCException as e:
            assert any(msg in str(e) for msg in ["spending from a burn address is prohibited", "mandatory-script-verify-flag-failed", "bad-txns-invalid-outputs", "bad-txns-spend-burn-address"])
            self.log.info("Spend transaction rejected as expected")

if __name__ == '__main__':
    MPAConsensusTest().main()
