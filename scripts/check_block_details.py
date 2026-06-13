#!/usr/bin/env python3
import json
import urllib.request
import base64
import hashlib

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "check_seed", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            print(f"RPC error: {e}")
            return None

def main():
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    
    # Get seed from tip
    chain_info = rpc.call("getblockchaininfo")
    if not chain_info:
        print("Failed to get blockchain info")
        return
        
    tip_hash = chain_info['bestblockhash']
    print(f"Tip hash: {tip_hash}")
    
    # Let's get the block details to see the seed
    block = rpc.call("getblock", [tip_hash])
    if not block:
        print("Failed to get block")
        return
        
    # Wait, getblock might have 'adamseed' or we can calculate it
    # Let's print block keys to see
    print("Block keys:", block.keys())
    
    # Let's query getadamminers
    miners = rpc.call("getadamminers")
    if not miners:
        print("Failed to get adam miners")
        return
        
    print(f"Pool size: {len(miners)}")
    
    # Let's print them
    for m in miners:
        print(f"  Index {m['index']}: {m['pubkey']} ({m['address']})")

if __name__ == "__main__":
    main()
