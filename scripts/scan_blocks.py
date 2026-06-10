#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "scan", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    rpc = KristaRPC("127.0.0.1", 27979, "kristarpc", "kristarpcpass")
    
    chain_info = rpc.call("getblockchaininfo")
    if not chain_info:
        print("Failed to get blockchain info")
        return
        
    blocks = chain_info['blocks']
    print(f"Scanning {blocks} blocks...")
    
    registrations = 0
    for h in range(1, blocks + 1):
        block_hash = rpc.call("getblockhash", [h])
        if not block_hash:
            continue
        block = rpc.call("getblock", [block_hash])
        if not block:
            continue
            
        # Coinbase is the first tx, skip it
        for txid in block['tx'][1:]:
            # Since txindex may not be enabled, let's look if it's in the wallet
            tx = rpc.call("gettransaction", [txid])
            if tx:
                print(f"Block {h}: Found wallet transaction {txid}")
                # Print details
                for details in tx.get('details', []):
                    print(f"  Category: {details.get('category')}, Address: {details.get('address')}, Amount: {details.get('amount')}")
            else:
                # If not in host wallet, it might be in another node's wallet.
                # Since we just want to know if there are non-coinbase txs, let's print them.
                print(f"Block {h}: Non-coinbase transaction {txid}")
                registrations += 1
                
    print(f"Scan complete. Found {registrations} non-coinbase transactions.")

if __name__ == "__main__":
    main()
