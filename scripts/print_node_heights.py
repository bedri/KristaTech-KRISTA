#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "check_height", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=3.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        info = rpc.call("getblockchaininfo")
        if info:
            print(f"Port {port}: height={info.get('blocks')}, bestblock={info.get('bestblockhash')[:16]}...")
        else:
            print(f"Port {port}: Offline or unresponsive")

if __name__ == "__main__":
    main()
