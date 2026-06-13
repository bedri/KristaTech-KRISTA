#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.port = port
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "import_mn", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=15.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            print(f"Error on port {self.port} calling {method}: {e}")
            return None

def main():
    # Masternode private keys from host testnet1/masternode.conf
    mn_keys = [
        "935CwwZTyJiogQVyF9c7MUUwvLVJzMy3rty2i52fKNgoLcMTDry",
        "92UqgQUHU8sLprrEBzYdun8CuqmxDTthNPka4c64mJ9y8BQV1ti"
    ]
    
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    
    print("Importing masternode private keys into all wallets...")
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        for idx, key in enumerate(mn_keys):
            print(f"  Port {port}: Importing MN{idx+1} key...")
            # importprivkey "key" "label" rescan
            res = rpc.call("importprivkey", [key, f"mn{idx+1}", False])
            # If successful, returns None (null) in JSON-RPC

    print("Finished importing masternode keys!")

if __name__ == "__main__":
    main()
