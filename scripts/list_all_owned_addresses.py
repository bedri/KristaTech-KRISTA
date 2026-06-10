#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "list_owned", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        groupings = rpc.call("listaddressgroupings")
        owned = []
        if groupings:
            for group in groupings:
                for entry in group:
                    addr = entry[0]
                    # Validate to see if it ismine
                    info = rpc.call("validateaddress", [addr])
                    if info and info.get("ismine"):
                        # Get key ID
                        script_pub_key = info.get("scriptPubKey", "")
                        keyid = ""
                        if script_pub_key.startswith("76a914") and script_pub_key.endswith("88ac"):
                            keyid = script_pub_key[6:-4]
                        owned.append((addr, keyid))
        print(f"Port {port}: owned addresses={len(owned)}")
        for addr, kid in owned:
            print(f"  {addr} (keyid: {kid})")

if __name__ == "__main__":
    main()
