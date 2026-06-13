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
            return None

def double_sha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def main():
    # Query getblockchaininfo from port 28002 (Node 1)
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    
    # We want to find the coordinator for the NEXT block, which uses the seed of block 1559.
    # What is the seed of block 1559?
    # We can get it from the debug logs or check it.
    # Wait, the seed printed in CreateNewBlock was: 188142fd7ef2ced24be5c4939d1ca906a13c170bb38158837cb9f0b2aa952939
    seed_hex = "188142fd7ef2ced24be5c4939d1ca906a13c170bb38158837cb9f0b2aa952939"
    seed_bytes = bytes.fromhex(seed_hex)
    
    miners = rpc.call("getadamminers")
    if not miners:
        print("Failed to get miners")
        return
        
    # Get masternodes list
    mn_list = rpc.call("listmasternodes")
    if mn_list is None:
        mn_list = []
    
    # Extract masternode pubkeys
    mn_keys = []
    for mn in mn_list:
        if mn.get('status') == 'ENABLED':
            mn_keys.append(mn.get('pubkey'))
            
    print(f"Active Masternodes: {len(mn_keys)}")
    
    # Calculate ranks for all miners
    ranked_pool = []
    for m in miners:
        pubkey_hex = m['pubkey']
        pubkey_bytes = bytes.fromhex(pubkey_hex)
        
        # Serialize CPubKey: compact size of length followed by bytes
        compact_len = bytes([len(pubkey_bytes)])
        serialized = seed_bytes + compact_len + pubkey_bytes
        
        h = double_sha256(serialized)
        
        # Convert hash to arith_uint256 equivalent (little endian)
        # So we compare byte strings, but in little endian (or just standard python sorting of bytes but in little-endian byte order)
        # Wait, C++ uint256 comparison compares the bytes from most-significant to least-significant,
        # which means big-endian comparison of the little-endian uint256 array.
        # A uint256 is stored as 32 bytes in little endian. When compared in arith_uint256,
        # the last byte (index 31) is the most significant byte.
        # So we reverse the bytes of the hash to sort them as integers!
        reversed_h = h[::-1]
        
        ranked_pool.append({
            "pubkey": pubkey_hex,
            "address": m['address'],
            "hash": reversed_h,
            "hash_hex": h.hex()
        })
        
    ranked_pool.sort(key=lambda x: x['hash'])
    
    # Rule 2: Active Masternodes count <= 7 (threshold)
    # The active masternode count is 2 (which is <= 7), so Rule 2 applies!
    min_count = 11  # nVersion = 11
    elect_count = min(min_count, len(ranked_pool))
    
    selected_miners = ranked_pool[:elect_count]
    if len(ranked_pool) > elect_count:
        coordinator = ranked_pool[elect_count]
    else:
        coordinator = ranked_pool[0]
        
    print(f"Elected Coordinator:")
    print(f"  Address: {coordinator['address']}")
    print(f"  PubKey:  {coordinator['pubkey']}")
    
    print("\nElected Miners:")
    for idx, m in enumerate(selected_miners):
        print(f"  Miner {idx}: {m['address']}")

if __name__ == "__main__":
    main()
