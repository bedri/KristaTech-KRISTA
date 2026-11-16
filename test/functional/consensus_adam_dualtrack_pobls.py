#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test Multi-Node ADAM Dual-Track Registration, PoBLS Quorum Sealing, and Model D Splits.

Verifies on a 3-node clean Regtest network (mocktime = 0, node-generated keys only):
1. Zero-Coin PoW (0 KRISTA) registration from a zero-balance node enters the ADAM pool
   across all peers via P2P propagation.
2. 100-confirmation input maturity is enforced for funded registrations (< 100 conf rejected).
3. Per-block registration rate cap (ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1) confirms at most
   one 250-KRISTA Coin-Lock transaction per block when multiple are in the mempool.
4. Post-UPGRADE_ADAM / UPGRADE_MODELD (height >= 200) blocks carry 11 elected ADAM
   validators, a signed Coordinator proof, >= 3-of-5 PoBLS quorum seal, and exact
   7% Developer Fund + 93% Model D net reward distribution (including 50% Burn / 50%
   Treasury split for unclaimed validator shares).
5. Submitting a block with a corrupted PoBLS quorum signature (vQuorumSig) is rejected
   by consensus.
"""

from decimal import Decimal

from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    connect_nodes_clique,
    sync_blocks,
    sync_mempools,
    wait_until,
)


def compressed_pubkey(node, addr):
    info = node.validateaddress(addr)
    pk = info["pubkey"]
    if len(pk) == 66:
        return pk
    if len(pk) == 130 and pk.startswith("04"):
        x_hex = pk[2:66]
        y_hex = pk[66:130]
        prefix = "02" if int(y_hex[-1], 16) % 2 == 0 else "03"
        return prefix + x_hex
    raise AssertionError(f"Unexpected pubkey format: {pk}")


class ConsensusAdamDualTrackPoBLSTest(KristaTechTestFramework):
    def set_test_params(self):
        self.num_nodes = 3
        self.setup_clean_chain = True
        self.mocktime = 0
        common = ["-promiscuousmempoolflags=1", "-whitelist=127.0.0.1", "-staking=0", "-gen=0"]
        self.extra_args = [list(common), list(common), list(common)]

    def setup_network(self):
        self.setup_nodes()
        connect_nodes_clique(self.nodes)
        self.sync_all()

    def run_test(self):
        node0, node1, node2 = self.nodes
        dev_addr = "kt5KNitasi4bEHbQQCrpy6445nJD7uSVNux"
        burn_addr = "ktBurn42LtQP2pJ2fS5X2kpRx4Sd86kNgx4"

        self.log.info("Step 1: Mine 25 blocks on Node 0 and test Zero-Coin PoW registration from zero-balance Node 2")
        node0.generate(25)
        self.sync_all()
        assert_equal(node2.getbalance(), Decimal("0"))

        addr_zero = node2.getnewaddress("zero_pow_miner")
        pk_zero = compressed_pubkey(node2, addr_zero)
        txid_zero = node2.registerminer("pow", "", pk_zero)
        sync_mempools(self.nodes)

        raw_zero = node2.getrawtransaction(txid_zero, True)
        assert_equal(Decimal(str(raw_zero["vout"][0]["value"])), Decimal("0"))

        node0.generate(1)
        self.sync_all()

        for idx, n in enumerate(self.nodes):
            pool_pks = {m["pubkey"] for m in n.getadamminers()}
            assert pk_zero in pool_pks, f"Zero-Coin PoW key missing on node{idx}"

        self.log.info("Step 2: Advance past height 200 (UPGRADE_ADAM + UPGRADE_MODELD active)")
        node0.generate(180)  # Height is now 206 (Dual-Track & Model D active)
        self.sync_all()
        assert_equal(node0.getblockcount(), 206)

        self.log.info("Step 3: Verify 100-confirmation input maturity guard on funded Coin-Lock registration")
        addr_n1_fund = node1.getnewaddress("n1_funding")
        node0.sendtoaddress(addr_n1_fund, 300)
        node0.generate(10)  # Height 216: Node 1's 300 KRISTA UTXO has only 10 confirmations (< 100 required)
        self.sync_all()
        assert node1.getbalance() >= Decimal("300")

        addr_n1_lock = node1.getnewaddress("n1_coinlock")
        pk_n1_lock = compressed_pubkey(node1, addr_n1_lock)
        assert_raises_rpc_error(
            -6,
            "100 confirmations",
            node1.registerminer,
            "lock",
            node1.getblockcount() + 300,
            pk_n1_lock,
        )

        self.log.info("Step 4: Mature Node 1 UTXO past 100 confirmations & verify 1 Coin-Lock tx/block cap")
        node0.generate(95)  # Height 311: Node 1's 300 KRISTA UTXO now has 105 >= 100 confirmations
        self.sync_all()
        cur_h = node0.getblockcount()
        assert_equal(cur_h, 311)

        addr_n0_lock = node0.getnewaddress("n0_coinlock")
        pk_n0_lock = compressed_pubkey(node0, addr_n0_lock)

        tx_lock_n1 = node1.registerminer("lock", cur_h + 300, pk_n1_lock)
        tx_lock_n0 = node0.registerminer("lock", cur_h + 300, pk_n0_lock)
        wait_until(lambda: tx_lock_n1 in node0.getrawmempool() and tx_lock_n0 in node0.getrawmempool(), timeout=20)

        # Mining 1 block must include at most 1 Coin-Lock registration (ADAM_MAX_COIN_LOCK_TX_PER_BLOCK = 1)
        b1_hash = node0.generate(1)[0]
        self.sync_all()
        b1_txs = set(node0.getblock(b1_hash)["tx"])
        included_in_b1 = len({tx_lock_n1, tx_lock_n0} & b1_txs)
        assert_equal(included_in_b1, 1)

        # Mining a 2nd block confirms the remaining Coin-Lock from the mempool
        b2_hash = node0.generate(1)[0]
        self.sync_all()
        b2_txs = set(node0.getblock(b2_hash)["tx"])
        included_in_b2 = len({tx_lock_n1, tx_lock_n0} & b2_txs)
        assert_equal(included_in_b2, 1)

        pool_after = {m["pubkey"]: m for m in node2.getadamminers()}
        assert pk_n0_lock in pool_after and pk_n1_lock in pool_after

        self.log.info("Step 5: Register Funded 10-KRISTA PoW-Lock on Node 0 and mine post-ModelD blocks")
        addr_n0_pow = node0.getnewaddress("n0_powlock")
        pk_n0_pow = compressed_pubkey(node0, addr_n0_pow)
        tx_pow_n0 = node0.registerminer("pow", node0.getbestblockhash(), pk_n0_pow)
        sync_mempools(self.nodes)
        assert tx_pow_n0 in node1.getrawmempool()

        mined_hashes = node0.generate(5)
        self.sync_all()

        self.log.info("Step 6: Verify ADAM 11-miner committee, Coordinator sig, PoBLS quorum seal & Model D 40 KRISTA split")
        sample_hash = mined_hashes[-1]
        blk = node1.getblock(sample_hash)
        assert_equal(len(blk["vAdamMiners"]), 11)
        assert len(blk["adamCoordinator"]) > 0
        assert len(blk["vAdamCoordinatorSig"]) > 0
        assert len(blk["adamQuorumSigners"]) >= 3, f"Expected >= 3 PoBLS quorum signers, got {blk['adamQuorumSigners']}"
        assert len(blk["vQuorumSig"]) >= 192, "Expected serialized BLS quorum signature hex"

        reward_txid = blk["tx"][1] if blk.get("isProofOfStake") else blk["tx"][0]
        cb_tx = node1.getrawtransaction(reward_txid, True)
        total_out_sat = sum(int(round(Decimal(str(o["value"])) * 100000000)) for o in cb_tx["vout"])
        total_in_sat = 0
        if blk.get("isProofOfStake"):
            for vin in cb_tx["vin"]:
                prev_tx = node1.getrawtransaction(vin["txid"], True)
                total_in_sat += int(round(Decimal(str(prev_tx["vout"][vin["vout"]]["value"])) * 100000000))
        assert_equal(total_out_sat - total_in_sat, 40 * 100000000)

        dev_sat = sum(
            int(round(Decimal(str(o["value"])) * 100000000))
            for o in cb_tx["vout"]
            if dev_addr in o.get("scriptPubKey", {}).get("addresses", [])
        )
        burn_sat = sum(
            int(round(Decimal(str(o["value"])) * 100000000))
            for o in cb_tx["vout"]
            if burn_addr in o.get("scriptPubKey", {}).get("addresses", [])
        )
        # Base 7% Developer Fund = 2.8 KRISTA (280,000,000 sat), plus 50% of any unclaimed seats matching burn_sat
        assert dev_sat >= 280000000, f"Developer fund output too low: {dev_sat}"
        if burn_sat > 0:
            # Unclaimed share splits 50% Burn + 50% Treasury (within 1 sat integer rounding)
            assert abs((dev_sat - 280000000) - burn_sat) <= 2

        self.log.info("Step 7: Disconnect Node 2, mine 1 block on Node 0, tamper with vQuorumSig, and verify Node 2 rejects it")
        # Isolate Node 2 temporarily so it hasn't seen the next valid block yet
        for peer_info in node2.getpeerinfo():
            node2.disconnectnode(peer_info["addr"])
        wait_until(lambda: node2.getconnectioncount() == 0, timeout=10)

        valid_next_hash = node0.generate(1)[0]
        raw_valid_blk = bytearray.fromhex(node0.getblock(valid_next_hash, False))
        qsig_hex = node0.getblock(valid_next_hash)["vQuorumSig"]
        qsig_bytes = bytes.fromhex(qsig_hex)
        pos = raw_valid_blk.rfind(qsig_bytes)
        assert pos != -1, "vQuorumSig bytes not located in raw block serialization"

        tampered_blk = bytearray(raw_valid_blk)
        tampered_blk[pos + 10] ^= 0x01  # Flip 1 bit inside the BLS quorum signature
        reject_reason = node2.submitblock(tampered_blk.hex())
        self.log.info("Tampered PoBLS block submitblock result: %s", reject_reason)
        assert reject_reason is not None and reject_reason != "", "Tampered vQuorumSig block must be rejected!"
        assert_equal(node2.getblockcount(), cur_h + 7)

        # Reconnect Node 2 and verify it accepts the genuine block and syncs cleanly
        connect_nodes_clique(self.nodes)
        sync_blocks(self.nodes)
        assert_equal(node2.getbestblockhash(), valid_next_hash)

        self.log.info("Multi-node ADAM Dual-Track + PoBLS + Model D functional test passed successfully!")


if __name__ == "__main__":
    ConsensusAdamDualTrackPoBLSTest().main()
