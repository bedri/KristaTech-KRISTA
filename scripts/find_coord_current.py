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
        data = json.dumps({"jsonrpc": "2.0", "id": "find_coord", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=10.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            print(f"RPC {method} failed on port {self.url.split(':')[-1]}: {e}")
            return None

def double_sha256(data):
    return hashlib.sha256(hashlib.sha256(data).digest()).digest()

def write_compact_size(size):
    if size < 253:
        return bytes([size])
    elif size <= 0xffff:
        return b'\xfd' + size.to_bytes(2, 'little')
    elif size <= 0xffffffff:
        return b'\xfe' + size.to_bytes(4, 'little')
    else:
        return b'\xff' + size.to_bytes(8, 'little')

def get_adam_seed_at(rpc, target_height):
    # Determine activation height
    activation_height = 200
    
    # Let's compute seed at activation_height - 1
    h = activation_height - 1
    h_hash_hex = rpc.call("getblockhash", [h])
    if not h_hash_hex:
        raise Exception(f"Failed to get block hash at height {h}")
    
    seed_bytes = bytes.fromhex(h_hash_hex)[::-1]
    
    for height in range(activation_height, target_height + 1):
        h_hash_hex = rpc.call("getblockhash", [height])
        if not h_hash_hex:
            raise Exception(f"Failed to get block hash at height {height}")
        block = rpc.call("getblock", [h_hash_hex])
        if not block:
            raise Exception(f"Failed to get block details at height {height}")
            
        vrf_hex = block.get("vAdamVRFProof", "")
        vrf_bytes = bytes.fromhex(vrf_hex)
        
        # Hash prevSeed (seed_bytes) + serialized vAdamVRFProof
        serialized_vrf = write_compact_size(len(vrf_bytes)) + vrf_bytes
        serialized = seed_bytes + serialized_vrf
        
        seed_bytes = double_sha256(serialized)
        
    return seed_bytes

def main():
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    
    # Get current tip info
    info = rpc.call("getblockchaininfo")
    if not info:
        print("Failed to get blockchain info from host node")
        return
        
    height = info['blocks']
    best_hash = info['bestblockhash']
    print(f"Current Height: {height}, Best Hash: {best_hash}")
    
    # Compute seed for the tip block (which will be used to select nodes for height+1)
    print("Computing ADAM seed recursively from height 200 to tip...")
    seed_bytes = get_adam_seed_at(rpc, height)
    print(f"Computed Seed: {seed_bytes[::-1].hex()}")
    
    # Get miners list
    miners = rpc.call("getadamminers")
    if not miners:
        print("Failed to get miners")
        return
        
    # Get active masternodes list
    mn_list = rpc.call("listmasternodes")
    if mn_list is None:
        mn_list = []
        
    mn_keys = []
    for mn in mn_list:
        if mn.get('status') == 'ACTIVE':
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
        
        ranked_pool.append({
            "pubkey": pubkey_hex,
            "address": m['address'],
            "hash": h,
            "hash_hex": h[::-1].hex()
        })
        
    # Sort with reversed hash to match C++ uint256 operator<
    ranked_pool.sort(key=lambda x: x['hash'][::-1])
    
    threshold = 3
    min_count = 11
    elect_count = min(min_count, len(ranked_pool))
    
    selected_miners = ranked_pool[:elect_count]
    print(f"\nElected Miners (top {elect_count}):")
    for idx, sm in enumerate(selected_miners):
        print(f"  {idx + 1}. Address: {sm['address']} | PubKey: {sm['pubkey'][:30]}... | Hash: {sm['hash_hex']}")
        
    if len(mn_keys) > threshold:
        print("\nRule 1 applies (Active Masternodes > 3)")
        ranked_mns = []
        for key in mn_keys:
            pubkey_bytes = bytes.fromhex(key)
            compact_len = bytes([len(pubkey_bytes)])
            serialized = seed_bytes + compact_len + pubkey_bytes
            h = double_sha256(serialized)
            ranked_mns.append({
                "pubkey": key,
                "hash": h,
                "hash_hex": h[::-1].hex()
            })
        ranked_mns.sort(key=lambda x: x['hash'][::-1])
        coordinator_pubkey = ranked_mns[0]['pubkey']
        print(f"Elected Coordinator PubKey (Masternode): {coordinator_pubkey}")
    else:
        print("\nRule 2 applies (Active Masternodes <= 3)")
        if len(ranked_pool) > elect_count:
            coordinator = ranked_pool[elect_count]
        else:
            coordinator = ranked_pool[0]
        coordinator_pubkey = coordinator['pubkey']
        print(f"Elected Coordinator PubKey (Miner): {coordinator_pubkey} | Address: {coordinator['address']}")
        
    # Let's find out which port owns the elected coordinator key
    # Key ID of coordinator is Ripemd160(Sha256(pubkey))
    pubkey_bytes = bytes.fromhex(coordinator_pubkey)
    sha = hashlib.sha256(pubkey_bytes).digest()
    h = hashlib.new('ripemd160')
    h.update(sha)
    coordinator_keyid = h.digest().hex()
    print(f"Elected Coordinator Key ID: {coordinator_keyid}")
    
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    found = False
    for port in ports:
        node_rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
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
                        print(f"\n--> SUCCESS! Port {port} owns coordinator address {addr}")
                        found = True
                        break
        if found:
            break
            
    if not found:
        print("\n--> WARNING: No node wallet owns the elected coordinator key!")

if __name__ == "__main__":
    main()
