#!/usr/bin/env python3
import json
import urllib.request
import base64
import hashlib
import sys

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "check", "method": method, "params": params}).encode('utf-8')
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
    # Connect to Node 1 (port 28002) to get blockchain state and miners
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    
    info = rpc.call("getblockchaininfo")
    if not info:
        print("Failed to get blockchain info")
        return
        
    height = info['blocks']
    best_hash = info['bestblockhash']
    print(f"Current Height: {height}, Best Hash: {best_hash}")
    
    # We want to find the coordinator for height+1 (876)
    # The selection for height 876 uses GetAdamSeed(pindexPrev), which is GetAdamSeed for height 875.
    # In the logs we saw: GetAdamSeed(875) seed is 292650086c055dab1790645c24f6ed26a2ef63593945e61eacfca96b77f8eddf
    # Let's get it by calling a debug seed computation or using the hardcoded log value.
    # Actually, we can calculate it recursively if we query block index, but we can also just use the known value.
    # Let's run with the known value 292650086c055dab1790645c24f6ed26a2ef63593945e61eacfca96b77f8eddf.
    seed_hex = "292650086c055dab1790645c24f6ed26a2ef63593945e61eacfca96b77f8eddf"
    seed_bytes = bytes.fromhex(seed_hex)[::-1]
    
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
        reversed_h = h[::-1]
        
        ranked_pool.append({
            "pubkey": pubkey_hex,
            "address": m['address'],
            "hash": h,
            "hash_hex": h.hex()
        })
        
    ranked_pool.sort(key=lambda x: x['hash'])
    
    target_pubkey = "04b18fc97c4bc3a00d0510e2f68d97bd3ff657a5460504f4282863dbcb958b665b92be0bb241c1339f6b0c3033485fbe71c429cfa16c73829abf006a3d1ee0dc9e"
    target_idx = -1
    for idx, r in enumerate(ranked_pool):
        if r['pubkey'] == target_pubkey:
            target_idx = idx
            break
    print(f"Target PubKey Index in Ranked Pool (Raw sorting): {target_idx}")
    
    # Sort with reversed hash to match C++ uint256 operator<
    ranked_pool.sort(key=lambda x: x['hash'][::-1])
    
    target_idx_reversed = -1
    for idx, r in enumerate(ranked_pool):
        if r['pubkey'] == target_pubkey:
            target_idx_reversed = idx
            break
    print(f"Target PubKey Index in Ranked Pool (Reversed sorting): {target_idx_reversed}")

    # Check Rule 2 vs Rule 1
    # Threshold is 3
    threshold = 3
    min_count = 11
    elect_count = min(min_count, len(ranked_pool))
    
    selected_miners = ranked_pool[:elect_count]
    if len(mn_keys) > threshold:
        # Rule 1
        print("Rule 1 applies (Active Masternodes > 3)")
        # Rank active masternodes separately
        ranked_mns = []
        for key in mn_keys:
            pubkey_bytes = bytes.fromhex(key)
            compact_len = bytes([len(pubkey_bytes)])
            serialized = seed_bytes + compact_len + pubkey_bytes
            h = double_sha256(serialized)
            reversed_h = h[::-1]
            ranked_mns.append({
                "pubkey": key,
                "hash": reversed_h
            })
        ranked_mns.sort(key=lambda x: x['hash'])
        coordinator_pubkey = ranked_mns[0]['pubkey']
    else:
        # Rule 2
        print("Rule 2 applies (Active Masternodes <= 3)")
        if len(ranked_pool) > elect_count:
            coordinator = ranked_pool[elect_count]
        else:
            coordinator = ranked_pool[0]
        coordinator_pubkey = coordinator['pubkey']
        
    # Get coordinator Key ID
    coordinator_keyid = "5709ea704242c7e586024ec962d887ca7066b8e4"
    
    print(f"Elected Coordinator Key ID: {coordinator_keyid}")
    
    # Query all ports to see which node owns this coordinator key
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    found = False
    for port in ports:
        node_rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        # Get addresses
        addresses = []
        labels = node_rpc.call("listlabels")
        if labels is not None:
            if "" not in labels:
                labels.append("")
            for label in labels:
                addr_dict = node_rpc.call("getaddressesbylabel", [label])
                if addr_dict:
                    addresses.extend(addr_dict.keys())
        
        groupings = node_rpc.call("listaddressgroupings")
        if groupings:
            for group in groupings:
                for entry in group:
                    if entry[0] not in addresses:
                        addresses.append(entry[0])
                        
        received = node_rpc.call("listreceivedbyaddress", [0, True])
        if received:
            for entry in received:
                if entry.get('address') not in addresses:
                    addresses.append(entry['address'])
                    
        for addr in addresses:
            info = node_rpc.call("validateaddress", [addr])
            if info and info.get('isvalid'):
                script_pub_key = info.get('scriptPubKey', '')
                if script_pub_key.startswith("76a914") and script_pub_key.endswith("88ac"):
                    keyid = script_pub_key[6:-4]
                    if keyid.lower() == coordinator_keyid:
                        print(f"--> FOUND! Port {port} owns coordinator address {addr}")
                        found = True
                        break
        if found:
            break
            
    if not found:
        print("--> WARNING: No node wallet owns the elected coordinator key!")

if __name__ == "__main__":
    main()
