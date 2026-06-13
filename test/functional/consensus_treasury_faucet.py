#!/usr/bin/env python3
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test consensus-level Developer Treasury & Bootstrap Faucet splits, activation, and quorum-signed spends."""

import hashlib
import struct
import ecdsa

from test_framework.test_framework import KristaTechTestFramework
from test_framework.util import *
from test_framework.messages import CTransaction, COutPoint, CTxIn, CTxOut, ToHex, FromHex, hash256

# Helper to serialize compactSize
def ser_compact_size(l):
    if l < 253:
        return struct.pack("B", l)
    elif l < 0x10000:
        return struct.pack("<BH", 253, l)
    elif l < 0x100000000:
        return struct.pack("<BI", 254, l)
    else:
        return struct.pack("<BQ", 255, l)

ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

def b58decode(v):
    decimal = 0
    for char in v:
        decimal = decimal * 58 + ALPHABET.index(char)
    num_bytes = 0
    temp = decimal
    while temp > 0:
        num_bytes += 1
        temp >>= 8
    num_ones = len(v) - len(v.lstrip('1'))
    return b'\x00' * num_ones + decimal.to_bytes(num_bytes, 'big')

def address_to_scriptPubKey(address):
    decoded = b58decode(address)
    keyid = decoded[1:21]
    return b'\x76\xa9\x14' + keyid + b'\x88\xac'

class ConsensusTreasuryFaucetTest(KristaTechTestFramework):
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
        # Addresses
        dev_address = "kt5KNitasi4bEHbQQCrpy6445nJD7uSVNux"
        faucet_address = "kt6scexsVd5Hgsgwsc9v844YCgzpv7F46wZ"
        spork_privkey = "932HEevBSujW2ud7RfB1YF91AFygbBRQj3de3LyaCRqNzKKgWXi"
        
        # 1. Mine blocks up to block 95 (approaching the governance start height 100)
        self.log.info("Mining 95 blocks to generate treasury/faucet rewards")
        self.nodes[0].generate(95)
        
        # Verify block reward splits
        self.log.info("Verifying reward splits in coinbase outputs")
        block_reward = 100 * 100000000 # 100 COIN on Regtest for heights < 1000
        expected_treasury = int(block_reward * 7 / 100)
        expected_faucet = int(block_reward * 5 / 100)
        
        block10_hash = self.nodes[0].getblockhash(10)
        block10 = self.nodes[0].getblock(block10_hash)
        coinbase_txid = block10['tx'][0]
        coinbase_tx = self.nodes[0].getrawtransaction(coinbase_txid, True)
        
        found_treasury = False
        found_faucet = False
        for out in coinbase_tx['vout']:
            val_sat = int(round(out['value'] * 100000000))
            if 'addresses' in out['scriptPubKey']:
                addr = out['scriptPubKey']['addresses'][0]
                if addr == dev_address:
                    assert_equal(val_sat, expected_treasury)
                    found_treasury = True
                elif addr == faucet_address:
                    assert_equal(val_sat, expected_faucet)
                    found_faucet = True
                    
        assert found_treasury, "Developer treasury output not found in block 10 coinbase"
        assert found_faucet, "Bootstrap faucet output not found in block 10 coinbase"
        self.log.info("Reward splits verified successfully")

        # 2. Try to spend a treasury output early (before block 100)
        self.log.info("Verifying early spend from treasury is rejected")
        # Use a treasury UTXO from block 5
        block5_hash = self.nodes[0].getblockhash(5)
        block5 = self.nodes[0].getblock(block5_hash)
        cb_txid = block5['tx'][0]
        cb_tx = self.nodes[0].getrawtransaction(cb_txid, True)
        
        utxo_vout = -1
        utxo_amount = 0
        for out in cb_tx['vout']:
            if 'addresses' in out['scriptPubKey'] and out['scriptPubKey']['addresses'][0] == dev_address:
                utxo_vout = out['n']
                utxo_amount = out['value']
                break
                
        assert utxo_vout != -1
        
        # Construct raw spend
        dest_addr = self.nodes[0].getnewaddress()
        inputs = [{"txid": cb_txid, "vout": utxo_vout}]
        outputs = {dest_addr: float(utxo_amount) - 0.01}
        raw_tx = self.nodes[0].createrawtransaction(inputs, outputs)
        
        # Sign it in Python using the spork key
        tx = FromHex(CTransaction(), raw_tx)
        spork_script = address_to_scriptPubKey("yCvUVd72w7xpimf981m114FSFbmAmne7j9")
        
        # Helper to convert WIF to private key bytes
        def wif_to_privkey_bytes(wif):
            decoded = b58decode(wif)
            payload = decoded[:-4]
            return payload[1:33]
            
        def sign_hash_with_wif(h, wif):
            priv_bytes = wif_to_privkey_bytes(wif)
            sk = ecdsa.SigningKey.from_string(priv_bytes, curve=ecdsa.SECP256k1)
            sig = sk.sign_digest_deterministic(h, hashfunc=hashlib.sha256, sigencode=ecdsa.util.sigencode_der)
            # Apply BIP62 Low-S normalization
            order = ecdsa.SECP256k1.generator.order()
            r, s = ecdsa.util.sigdecode_der(sig, order)
            if s > order // 2:
                s = order - s
            return ecdsa.util.sigencode_der(r, s, order)

        def get_pubkey_bytes_from_wif(wif):
            decoded = b58decode(wif)
            payload = decoded[:-4]
            priv_bytes = payload[1:33]
            sk = ecdsa.SigningKey.from_string(priv_bytes, curve=ecdsa.SECP256k1)
            vk = sk.verifying_key
            if len(payload) == 34:
                x_bytes = vk.to_string()[:32]
                prefix = b'\x02' if vk.pubkey.point.y() % 2 == 0 else b'\x03'
                return prefix + x_bytes
            else:
                return b'\x04' + vk.to_string()

        # Calculate the sighash for the spending transaction
        from test_framework.script import SignatureHash, SIGHASH_ALL, CScript
        (sighash, err) = SignatureHash(CScript(spork_script), tx, 0, SIGHASH_ALL)
        assert err is None
        
        # Helper to construct a proper script data push
        def script_push(data):
            l = len(data)
            if l < 76:
                return bytes([l]) + data
            elif l < 256:
                return bytes([0x4c, l]) + data
            elif l < 65536:
                return bytes([0x4d]) + struct.pack("<H", l) + data
            else:
                return bytes([0x4f]) + struct.pack("<I", l) + data

        # Sign it
        sig_der = sign_hash_with_wif(sighash, spork_privkey)
        sig_with_type = sig_der + b'\x01'
        spork_pubkey = get_pubkey_bytes_from_wif(spork_privkey)
        
        # 1. Spend without quorum signature
        scriptSig_no_q = script_push(sig_with_type) + script_push(spork_pubkey)
        tx.vin[0].scriptSig = scriptSig_no_q
        signed_hex = ToHex(tx)
        
        # 2. Spend with dummy/invalid quorum signature
        dummy_qsig = b'\x00' * 32 + b'\x00' # serialization of empty CQuorumSignature
        scriptSig_dummy_q = scriptSig_no_q + script_push(dummy_qsig)
        tx.vin[0].scriptSig = scriptSig_dummy_q
        early_spend_hex = ToHex(tx)
        
        try:
            self.nodes[0].sendrawtransaction(early_spend_hex)
            assert False, "Spending from treasury early should have been rejected"
        except JSONRPCException as e:
            assert "bad-txns-spend-devfund-early" in str(e)
            self.log.info("Early spend rejected as expected with: bad-txns-spend-devfund-early")

        # 3. Mine to block 100 to enable spending
        self.nodes[0].generate(5)
        assert_equal(self.nodes[0].getblockcount(), 100)
        self.log.info("Mined to block 100. Treasury governance active.")

        # 4. Try spending without quorum signature (using the spork signature alone)
        self.log.info("Verifying spend without quorum signature is rejected")
        try:
            self.nodes[0].sendrawtransaction(signed_hex)
            assert False, "Spend without quorum signature should have been rejected"
        except JSONRPCException as e:
            assert "bad-txns-missing-quorum-sig" in str(e)
            self.log.info("Spend without quorum signature rejected as expected with: bad-txns-missing-quorum-sig")

        # 5. Try spending with malformed/invalid quorum signature
        self.log.info("Verifying spend with invalid quorum signature is rejected")
        try:
            self.nodes[0].sendrawtransaction(early_spend_hex)
            assert False, "Spend with invalid quorum signature should have been rejected"
        except JSONRPCException as e:
            assert "bad-txns-invalid-quorum-sig" in str(e)
            self.log.info("Spend with invalid quorum signature rejected as expected with: bad-txns-invalid-quorum-sig")

        # 6. Construct a valid quorum signature and spend successfully!
        self.log.info("Constructing a valid LLMQ quorum signature")
        
        # Reconstruct the tx without the quorum signature to get the correct txid hash
        tx_unsigned = FromHex(CTransaction(), signed_hex)
        tx_unsigned.rehash()
        txid_hash = hash256(tx_unsigned.serialize_without_witness())
        self.log.info("TXID hash to sign: %s", bytes_to_hex_str(txid_hash))
        
        WIF_KEYS = [
            '7Q22iqZn4cbxwEi7ormJmTGyVoAnxkW6UGFdiCg9Ub1Vd4GoRDPo',
            '7Pc3quyHTqGnpPnKETJiCuHix9KfUFhusSxHszvpadDPfUdPwsz1',
            '7VNGozNMCA1erGbGfSrfJPvNG5YKNBHah4qW1ng18CQRGQFw7oAX',
            '7TK9dyQVNGj7C3n5FnYry4Xbr9brPyHXEjpiBMV14VgkUsrHT9oZ',
            '7PqSigWg6aUkPYBc6s8yJDhsLFWkhHEf3UquA1TGWSgZms49tgxw',
            '7TeshNscV5J9ehuhm12gewrMa6uSze28Q4tDxztF8819pH8Vo6Kh',
            '7S5uZcH6gRgDeJrKKTgEaibRbAniZBx6Jsy45Ziuz9HaSk9DVwCT',
            '7TcPjyKWCmVQy9W9bnrGwZ8pxFkVvgUEkt9FbZJdMGmozEs5tTFM',
            '7QZBiT16WDZJnV2CcbkHeVxbvuW6tGHPzNtmTWzWa67mqSSVELX4',
            '7VGhiXgoF75VotbL6LmoFitUwW1WqNhzD8MmZhYyhCAaN1v7GD4A',
            '7S7Rux5yvbiWqVvVBD84Nmj9Z77a49avnvrLoquCeg9eoBtcCWug',
            '7UycT39reukXoZDtzr7ESDg4We152XRvMbErB6u5FeS3wh2Ds4cv',
            '7NSJZ7qahx3kU13SuytyYPCiMvr58SobVaUk3bM2fo2gDPR5z3bZ',
            '7P5jNxSxfe6JC6iFmfvBeeZamUtUR2psATBjWFHydnax5oBd4Gbq',
            '7SCkRezdFnRdUUQEqoNy9qqzMDSR62G6BprRniwtCPdCz8HRK7J7'
        ]
        
        # Pair each WIF key with its compressed public key bytes, then sort lexicographically
        paired_keys = []
        for wif in WIF_KEYS:
            pub = get_pubkey_bytes_from_wif(wif)
            paired_keys.append((pub, wif))
        paired_keys.sort(key=lambda x: x[0])

        qsig_sigs = []
        for i, (pub, wif) in enumerate(paired_keys):
            pub_hash = hash256(pub)
            sig_der = sign_hash_with_wif(txid_hash, wif)
            outpoint_bin = pub_hash + struct.pack("<I", i)
            sig_bin = ser_compact_size(len(sig_der)) + sig_der
            qsig_sigs.append(outpoint_bin + sig_bin)

        serialized_qsig = txid_hash + ser_compact_size(len(qsig_sigs)) + b"".join(qsig_sigs)
        
        # Build the final scriptSig
        final_scriptSig = scriptSig_no_q + script_push(serialized_qsig)
        tx_unsigned.vin[0].scriptSig = final_scriptSig
        valid_spend_hex = ToHex(tx_unsigned)
        
        # Submit transaction
        self.log.info("Submitting transaction with valid quorum signature")
        valid_txid = self.nodes[0].sendrawtransaction(valid_spend_hex)
        self.log.info("Transaction accepted to mempool: %s", valid_txid)
        
        # Mine it
        self.nodes[0].generate(1)
        assert_equal(self.nodes[0].getblockcount(), 101)
        
        # Verify it is mined and spent
        block101_hash = self.nodes[0].getblockhash(101)
        block101 = self.nodes[0].getblock(block101_hash)
        assert valid_txid in block101['tx']
        self.log.info("Treasury spend mined successfully in block 101!")

if __name__ == '__main__':
    ConsensusTreasuryFaucetTest().main()
