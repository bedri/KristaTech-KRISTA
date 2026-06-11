#!/usr/bin/env python3
import json
import urllib.request
import base64
import time
import sys
import logging
import os

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.port = port
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

def check_and_register(rpc, mode, target_addr_or_pubkey):
    # 1. Retrieve or generate a persistent miner address for this port
    address_file = f"miner_address_{rpc.port}.txt"
    try:
        with open(address_file, "r") as f:
            my_address = f.read().strip()
    except FileNotFoundError:
        my_address = rpc.call("getnewaddress")
        if not my_address:
            logging.error("Failed to generate new address. Cannot proceed.")
            return
        with open(address_file, "w") as f:
            f.write(my_address)
        logging.info(f"Generated and saved new persistent address: {my_address}")

    # 2. Get current blockchain height
    chain_info = rpc.call("getblockchaininfo")
    if not chain_info:
        logging.error("Could not fetch blockchain info. Skipping this check.")
        return
    current_height = chain_info['blocks']
    logging.info(f"Current blockchain height: {current_height}")

    # 3. Check if our address is registered in the active ADAM miner pool
    miners = rpc.call("getadamminers")
    is_in_pool = False
    if miners:
        for miner in miners:
            if miner.get('address') == my_address:
                is_in_pool = True
                break

    # 4. Check local registration state file for expiration
    state_file = f"miner_state_{rpc.port}.json"
    expires_at_height = 0
    if os.path.exists(state_file):
        try:
            with open(state_file, "r") as f:
                state = json.load(f)
                if state.get("address") == my_address:
                    expires_at_height = state.get("expires_at_height", 0)
        except Exception as e:
            logging.error(f"Error reading state file: {e}")

    # 5. Determine if registration or renewal is needed
    if is_in_pool and expires_at_height > current_height:
        remaining_blocks = expires_at_height - current_height
        logging.info(f"Active registration found in pool! Expires at height {expires_at_height} ({remaining_blocks} blocks remaining).")
        
        if remaining_blocks > 240:
            logging.info("Registration is still valid. No renewal needed.")
            return
        else:
            logging.warning(f"Registration is expiring soon ({remaining_blocks} blocks remaining). Triggering renewal...")
    else:
        if not is_in_pool:
            logging.warning("Miner address not found in ADAM pool! Triggering registration...")
        else:
            logging.warning("Local state says expired, but found in pool. Triggering renewal to be safe...")

    # 6. Perform registration
    logging.info(f"Registering miner using mode: {mode} for address: {my_address}...")
    
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

    txid = rpc.call("registerminer", [mode, None, my_address])
    if txid:
        logging.info(f"Successfully registered miner! Transaction ID: {txid}")
        # Save new state
        new_state = {
            "address": my_address,
            "txid": txid,
            "registered_at_height": current_height,
            "expires_at_height": current_height + 2900
        }
        try:
            with open(state_file, "w") as f:
                json.dump(new_state, f)
        except Exception as e:
            logging.error(f"Error writing state file: {e}")
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
