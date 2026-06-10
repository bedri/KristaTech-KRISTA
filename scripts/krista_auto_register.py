#!/usr/bin/env python3
import json
import urllib.request
import base64
import time
import sys
import logging

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "auto_reg", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=600.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            logging.error(f"RPC Call {method} failed: {e}")
            return None

def parse_locktime_from_asm(asm):
    # Coin-Lock ASM: <pubkey> OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP ...
    # PoW-Lock ASM: <nonce> <challenge> <pubkey> OP_DROP OP_DROP OP_DROP <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP ...
    tokens = asm.split()
    try:
        cltv_idx = tokens.index("OP_CHECKLOCKTIMEVERIFY")
        if cltv_idx > 0:
            locktime_str = tokens[cltv_idx - 1]
            if locktime_str.startswith("0x"):
                return int(locktime_str, 16)
            else:
                return int(locktime_str)
    except (ValueError, IndexError) as e:
        pass
    return None

def check_and_register(rpc, mode, target_addr_or_pubkey):
    # 1. Get current blockchain height
    chain_info = rpc.call("getblockchaininfo")
    if not chain_info:
        logging.error("Could not fetch blockchain info. Skipping this check.")
        return
    current_height = chain_info['blocks']
    logging.info(f"Current blockchain height: {current_height}")

    # 2. Query listunspent to find our active registrations
    unspent = rpc.call("listunspent", [1, 9999999])
    if unspent is None:
        logging.error("Could not fetch unspent outputs. Skipping this check.")
        return

    max_active_locktime = 0
    
    for utxo in unspent:
        script_pub_key = utxo.get('scriptPubKey')
        if not script_pub_key:
            continue
        
        # We decode the script to inspect the ASM
        decoded = rpc.call("decodescript", [script_pub_key])
        if not decoded:
            continue
        
        asm = decoded.get('asm', '')
        if "OP_CHECKLOCKTIMEVERIFY" in asm:
            locktime = parse_locktime_from_asm(asm)
            if locktime and locktime > current_height:
                if target_addr_or_pubkey == "default" or utxo.get('address') == target_addr_or_pubkey:
                    if locktime > max_active_locktime:
                        max_active_locktime = locktime

    if max_active_locktime > current_height:
        remaining_blocks = max_active_locktime - current_height
        logging.info(f"Active registration found! Expires at height {max_active_locktime} ({remaining_blocks} blocks remaining).")
        
        if remaining_blocks > 240:
            logging.info("Registration is still valid. No renewal needed.")
            return
        else:
            logging.warning(f"Registration is expiring soon ({remaining_blocks} blocks remaining). Triggering renewal...")
    else:
        logging.warning("No active miner registration found! Triggering registration...")

    # 3. Perform registration
    logging.info(f"Registering miner using mode: {mode}...")
    
    # Check if we have enough balance before sending transaction
    wallet_info = rpc.call("getwalletinfo")
    if not wallet_info:
        logging.error("Could not fetch wallet info. Cannot proceed with registration.")
        return
        
    balance = wallet_info.get('balance', 0.0)
    required = 1000.0 if mode == "lock" else 0.0001
    
    if balance < required:
        logging.error(f"Insufficient spendable balance to register (Have {balance} KRISTA, Need {required} KRISTA). Will retry when balance matures.")
        return

    txid = rpc.call("registerminer", [mode, None, target_addr_or_pubkey])
    if txid:
        logging.info(f"Successfully registered miner! Transaction ID: {txid}")
    else:
        logging.error("Miner registration failed.")

def main():
    # Load configuration
    RPC_IP = "127.0.0.1"
    RPC_PORT = 27979
    RPC_USER = "kristarpc"
    RPC_PASS = "kristarpcpass"
    MODE = "pow"
    TARGET_KEY = "default"
    CHECK_INTERVAL = 300      # Check every 5 minutes

    # Parse command line overrides
    if len(sys.argv) > 1:
        MODE = sys.argv[1]
    if len(sys.argv) > 2:
        RPC_PORT = int(sys.argv[2])
    if len(sys.argv) > 3:
        TARGET_KEY = sys.argv[3]

    # Setup unique logging per port
    logging.basicConfig(
        level=logging.INFO,
        format='%(asctime)s [%(levelname)s] %(message)s',
        handlers=[
            logging.StreamHandler(sys.stdout),
            logging.FileHandler(f"krista_auto_register_{RPC_PORT}.log")
        ]
    )

    logging.info("==========================================")
    logging.info("KristaTech Miner Auto-Registration Daemon")
    logging.info(f"RPC Port: {RPC_PORT} | Mode: {MODE} | Target Key: {TARGET_KEY}")
    logging.info("==========================================")

    rpc = KristaRPC(RPC_IP, RPC_PORT, RPC_USER, RPC_PASS)

    while True:
        try:
            check_and_register(rpc, MODE, TARGET_KEY)
        except Exception as e:
            logging.error(f"Error in daemon loop: {e}")
        
        logging.info(f"Sleeping for {CHECK_INTERVAL} seconds...")
        time.sleep(CHECK_INTERVAL)

if __name__ == "__main__":
    main()
