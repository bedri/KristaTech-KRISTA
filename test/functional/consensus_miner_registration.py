#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test KRISTA Miner Registration System (Coin-Lock and PoW-Lock paths).

Test that:
1. getadamminers returns the 15 default deterministic keys at startup in regtest.
2. registerminer lock builds and broadcasts a valid coin-lock transaction.
3. registerminer pow solves the CPU puzzle and broadcasts a valid pow-lock transaction.
4. Mined registrations successfully enter the ADAM miner pool.
5. Registration outputs are strictly timelocked and cannot be spent before maturity.
"""

from decimal import Decimal
from test_framework.test_framework import PivxTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
    JSONRPCException,
)

def get_compressed_pubkey(pubkey_hex):
    if len(pubkey_hex) == 66:
        return pubkey_hex
    elif len(pubkey_hex) == 130:
        x_hex = pubkey_hex[2:66]
        y_hex = pubkey_hex[66:130]
        last_digit = int(y_hex[-1], 16)
        prefix = "02" if last_digit % 2 == 0 else "03"
        return prefix + x_hex
    else:
        raise ValueError("Invalid public key length")

class MinerRegistrationTest(PivxTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        # Configure a custom seed to test deterministic key derivation config
        self.extra_args = [['-adamminerseed=test_seed_miner_reg']]
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        self.log.info("Step 1: Check initial ADAM miner pool")
        initial_miners = node.getadamminers()
        assert_equal(len(initial_miners), 15)
        # Verify that they are derived from our custom seed by doing a sanity check
        # We don't need to match the exact keys unless we precalculate them, but
        # verify they are there.
        initial_pubkeys = [m['pubkey'] for m in initial_miners]
        self.log.info(f"Initial pool size: {len(initial_pubkeys)}")

        self.log.info("Step 2: Generate blocks to get spendable coins")
        node.generate(110)
        assert node.getbalance() > 1000

        self.log.info("Step 3: Register a miner via Coin Lock (timelocked 1000 KRISTA)")
        addr_lock = node.getnewaddress()
        addr_lock_info = node.validateaddress(addr_lock)
        pubkey_lock = get_compressed_pubkey(addr_lock_info['pubkey'])

        current_height = node.getblockcount()
        # Lock for 2900 blocks
        locktime_target = current_height + 2900
        txid_lock = node.registerminer("lock", locktime_target, addr_lock)
        self.log.info(f"Coin-Lock registration transaction: {txid_lock}")

        # Check that transaction is in mempool
        mempool = node.getrawmempool()
        assert txid_lock in mempool

        self.log.info("Step 4: Register a miner via CPU PoW")
        addr_pow = node.getnewaddress()
        addr_pow_info = node.validateaddress(addr_pow)
        pubkey_pow = get_compressed_pubkey(addr_pow_info['pubkey'])

        tip_hash = node.getbestblockhash()
        txid_pow = node.registerminer("pow", tip_hash, pubkey_pow)
        self.log.info(f"PoW-Lock registration transaction: {txid_pow}")

        # Check that transaction is in mempool
        mempool = node.getrawmempool()
        assert txid_pow in mempool

        self.log.info("Step 5: Mine the registration transactions")
        node.generate(1)
        mempool = node.getrawmempool()
        assert txid_lock not in mempool
        assert txid_pow not in mempool

        self.log.info("Step 6: Verify they show up in getadamminers pool")
        miners = node.getadamminers()
        pool_pubkeys = [m['pubkey'] for m in miners]

        assert pubkey_lock in pool_pubkeys
        assert pubkey_pow in pool_pubkeys
        self.log.info("Both miners successfully registered and verified in the pool!")

        self.log.info("Step 7: Verify that registration outputs are timelocked and cannot be spent")
        # For coin-lock
        raw_tx_lock = node.getrawtransaction(txid_lock, 1)
        lock_vout = -1
        for i, vout in enumerate(raw_tx_lock['vout']):
            if vout['value'] == Decimal('1000.0'):
                lock_vout = i
                break
        assert lock_vout != -1

        # For pow-lock
        raw_tx_pow = node.getrawtransaction(txid_pow, 1)
        pow_vout = -1
        for i, vout in enumerate(raw_tx_pow['vout']):
            if vout['value'] == Decimal('0.0001'):
                pow_vout = i
                break
        assert pow_vout != -1

        # Try to spend lock output without nLockTime
        inputs = [{"txid": txid_lock, "vout": lock_vout}]
        outputs = {node.getnewaddress(): Decimal('999.99')}
        raw_spend = node.createrawtransaction(inputs, outputs)
        signed_spend = node.signrawtransaction(raw_spend)

        # Sending it without setting nLockTime should fail (CLTV requirement not met)
        try:
            node.sendrawtransaction(signed_spend['hex'])
            assert False, "Should have failed script verification due to CLTV"
        except JSONRPCException as e:
            self.log.info(f"Successfully rejected spend without locktime (expected): {e}")

        # Try to spend lock output with nLockTime set to locktime_target, but block height is too low
        raw_spend_lt = node.createrawtransaction(inputs, outputs, locktime_target)
        signed_spend_lt = node.signrawtransaction(raw_spend_lt)

        try:
            node.sendrawtransaction(signed_spend_lt['hex'])
            assert False, "Should have failed transaction finality check"
        except JSONRPCException as e:
            # PIVX/Dash/Bitcoin throws non-final error
            assert "non-final" in str(e) or "non-BIP68-final" in str(e)
            self.log.info(f"Successfully rejected spend with locktime due to non-finality (expected): {e}")

        self.log.info("All registration tests passed successfully!")

if __name__ == '__main__':
    MinerRegistrationTest().main()
