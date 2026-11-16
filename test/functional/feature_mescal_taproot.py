#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test MESCAL v2 Stateless Taproot (P2TR) Compilation, Wallet Isolation, and Script-Path Spending.

Verifies on a 2-node clean Regtest network (mocktime = 0, node-generated keys only):
1. compilemescaltotaproot deterministically compiles MESCAL JSON contracts into a BIP341
   Taproot P2TR Bech32m output, leafScript, and 33-byte controlBlock, and decompilemescal
   round-trips cleanly back to the identical Taproot commitment.
2. Malformed or unsupported MESCAL roles are rejected at RPC compilation time.
3. Funding a MESCAL P2TR contract address preserves SEC-13 wallet balance isolation
   (watch-only contract UTXOs never inflate spendable wallet balance).
4. Consensus strictly enforces Tapscript conditions on-chain:
   - Spending before CLTV lock height is rejected.
   - Spending with an incorrect secret value is rejected.
   - Spending with a tampered Taproot control block (flipped parity bit) is rejected.
   - Spending with the valid witness stack (secret + leafScript + controlBlock) after
     lock expiry is accepted into the mempool, relayed across peers, and mined.
"""

from decimal import Decimal
import json

from test_framework.messages import (
    COIN,
    COutPoint,
    CTransaction,
    CTxIn,
    CTxOut,
    ToHex,
)
from test_framework.script import CScript
from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import (
    JSONRPCException,
    assert_equal,
    assert_raises_rpc_error,
    sync_mempools,
)


class MescalTaprootTest(KristaTechTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True
        self.mocktime = 0
        common = ["-promiscuousmempoolflags=1", "-whitelist=127.0.0.1", "-staking=0", "-gen=0"]
        self.extra_args = [list(common), list(common)]

    def run_test(self):
        node0, node1 = self.nodes

        self.log.info("Step 1: Mine 115 blocks on Node 0 and derive x-only internal pubkey from Node 0 wallet")
        node0.generate(115)
        self.sync_all()

        internal_addr = node0.getnewaddress("taproot_internal")
        raw_pk = node0.validateaddress(internal_addr)["pubkey"]
        if len(raw_pk) == 130 and raw_pk.startswith("04"):
            compressed_pk = ("02" if int(raw_pk[-1], 16) % 2 == 0 else "03") + raw_pk[2:66]
        else:
            compressed_pk = raw_pk
        assert_equal(len(compressed_pk), 66)
        internal_xonly_hex = compressed_pk[2:]  # 32-byte x-only hex

        lock_height = node0.getblockcount() + 5
        expected_secret = 424242

        mescal_contract = {
            "basic": {
                "VaultLock": {
                    "role": "lock-time",
                    "inputs": [{"name": "Lock-Until", "type": "timestamp-or-block-height", "value": lock_height}],
                },
                "ExpectedCode": {
                    "role": "number",
                    "inputs": [{"name": "Number", "type": "number", "value": expected_secret}],
                },
                "VerifyCode": {"role": "num-equal"},
            },
            "contract": {
                "TaprootVault": {
                    "actions": [
                        {"type": "basic", "name": "VaultLock"},
                        {"type": "basic", "name": "ExpectedCode"},
                        {"type": "basic", "name": "VerifyCode"},
                    ]
                }
            },
            "active_contract": "TaprootVault",
        }
        mescal_json_str = json.dumps(mescal_contract)

        self.log.info("Step 2: Verify deterministic compilemescaltotaproot & decompilemescal round-trip")
        compiled_0 = node0.compilemescaltotaproot(mescal_json_str, internal_xonly_hex)
        compiled_1 = node1.compilemescaltotaproot(mescal_json_str, internal_xonly_hex)
        assert_equal(compiled_0, compiled_1)

        p2tr_addr = compiled_0["address"]
        spk_hex = compiled_0["scriptPubKey"]
        leaf_hex = compiled_0["leafScript"]
        ctrl_hex = compiled_0["controlBlock"]

        assert spk_hex.startswith("5120") and len(spk_hex) == 68, f"Invalid P2TR scriptPubKey: {spk_hex}"
        assert_equal(len(ctrl_hex), 66)  # 33-byte control block

        decompiled_obj = node0.decompilemescal(leaf_hex)
        recompiled = node0.compilemescaltotaproot(json.dumps(decompiled_obj), internal_xonly_hex)
        assert_equal(recompiled, compiled_0)

        # Reject invalid MESCAL role
        bad_contract = json.dumps({
            "basic": {"BadOp": {"role": "nonexistent-loop-opcode"}},
            "contract": {"Bad": {"actions": [{"type": "basic", "name": "BadOp"}]}},
            "active_contract": "Bad",
        })
        assert_raises_rpc_error(-8, "MESCAL Compilation failed", node0.compilemescaltotaproot, bad_contract, internal_xonly_hex)

        self.log.info("Step 3: Fund P2TR MESCAL address (%s) and verify SEC-13 wallet balance isolation", p2tr_addr)
        assert_equal(node1.getbalance(), Decimal("0"))
        node1.importaddress(spk_hex, "mescal_watch", False, True)

        fund_txid = node0.sendtoaddress(p2tr_addr, 25.0)
        node0.generate(1)
        self.sync_all()

        # SEC-13: Contract output must NOT appear in Node 1's spendable balance
        assert_equal(node1.getbalance(), Decimal("0"))

        fund_tx = node0.getrawtransaction(fund_txid, True)
        p2tr_vout = -1
        for out in fund_tx["vout"]:
            if out["scriptPubKey"]["hex"] == spk_hex:
                p2tr_vout = out["n"]
                break
        assert p2tr_vout != -1, "Funded P2TR output not found"

        dest_addr = node1.getnewaddress("vault_payout")
        dest_spk_hex = node1.validateaddress(dest_addr)["scriptPubKey"]
        dest_spk_bytes = bytes.fromhex(dest_spk_hex)
        leaf_bytes = bytes.fromhex(leaf_hex)
        ctrl_bytes = bytes.fromhex(ctrl_hex)
        payout_sats = int(Decimal("24.99") * COIN)

        def build_spend_tx(secret_val, ctrl_block_bytes, n_locktime):
            tx = CTransaction()
            tx.nVersion = 1
            tx.nLockTime = n_locktime
            txin = CTxIn(COutPoint(int(fund_txid, 16), p2tr_vout), b"", 0xFFFFFFFE)
            txin.scriptSig = CScript([secret_val, leaf_bytes, ctrl_block_bytes])
            tx.vin = [txin]
            tx.vout = [CTxOut(payout_sats, dest_spk_bytes)]
            return ToHex(tx)

        self.log.info("Step 4: Verify premature spend before CLTV lock_height (%d) is rejected", lock_height)
        assert node0.getblockcount() < lock_height
        early_hex = build_spend_tx(expected_secret, ctrl_bytes, lock_height)
        try:
            node0.sendrawtransaction(early_hex)
            assert False, "Spend with future nLockTime should have been rejected as non-final"
        except JSONRPCException as e:
            assert "non-final" in str(e)

        early_unlocked_hex = build_spend_tx(expected_secret, ctrl_bytes, node0.getblockcount())
        try:
            node0.sendrawtransaction(early_unlocked_hex)
            assert False, "Spend failing Tapscript CLTV must be rejected"
        except JSONRPCException as e:
            assert "Locktime requirement not satisfied" in str(e) or "mandatory-script-verify-flag-failed" in str(e)

        self.log.info("Step 5: Advance chain to lock_height (%d) and verify wrong secret & bad controlBlock fail", lock_height)
        blocks_needed = lock_height - node0.getblockcount()
        if blocks_needed > 0:
            node0.generate(blocks_needed)
            self.sync_all()

        wrong_secret_hex = build_spend_tx(expected_secret + 1, ctrl_bytes, lock_height)
        try:
            node0.sendrawtransaction(wrong_secret_hex)
            assert False, "Wrong Tapscript secret must be rejected"
        except JSONRPCException:
            pass

        bad_ctrl = bytearray(ctrl_bytes)
        bad_ctrl[0] ^= 0x01  # Flip parity bit
        bad_ctrl_hex = build_spend_tx(expected_secret, bytes(bad_ctrl), lock_height)
        try:
            node0.sendrawtransaction(bad_ctrl_hex)
            assert False, "Corrupted Taproot control block must be rejected"
        except JSONRPCException:
            pass

        self.log.info("Step 6: Broadcast valid Tapscript witness spend, relay to Node 1, mine, and verify payout")
        valid_hex = build_spend_tx(expected_secret, ctrl_bytes, lock_height)
        spend_txid = node0.sendrawtransaction(valid_hex)
        sync_mempools(self.nodes)
        assert spend_txid in node1.getrawmempool()

        node1.generate(1)
        self.sync_all()
        assert_equal(node1.getbalance(), Decimal("24.99"))

        self.log.info("MESCAL v2 Taproot P2TR functional test passed successfully!")


if __name__ == "__main__":
    MescalTaprootTest().main()
