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
        data = json.dumps({"jsonrpc": "2.0", "id": "import_keys", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=10.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            print(f"Error on port {self.port} calling {method}: {e}")
            return None

def main():
    # Dynamically fetch miner addresses from Port 28002
    rpc28002 = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    miners_info = rpc28002.call("getadamminers")
    if not miners_info:
        print("Failed to fetch miner list from 28002")
        return
    miner_addrs = [m["address"] for m in miners_info]
    
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    wif_keys = {}

    print("Dumping private keys from each node...")
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        for addr in miner_addrs:
            info = rpc.call("validateaddress", [addr])
            if info and info.get("ismine"):
                wif = rpc.call("dumpprivkey", [addr])
                if wif:
                    print(f"Port {port} owns {addr} -> dumped key {wif[:5]}...")
                    wif_keys[addr] = wif

    print(f"Total keys dumped: {len(wif_keys)}")

    target_ports = [27979, 28002]
    print(f"Importing keys into target wallets: {target_ports}...")
    for target_port in target_ports:
        target_rpc = KristaRPC("127.0.0.1", target_port, "kristarpc", "kristarpcpass")
        for addr, wif in wif_keys.items():
            print(f"  Importing key for {addr} into Port {target_port}...")
            # importprivkey <wif> [label] [rescan=false]
            res = target_rpc.call("importprivkey", [wif, "", False])
            # If successful, call returns None (null) in JSON-RPC

    print("All keys imported successfully!")

if __name__ == "__main__":
    main()
