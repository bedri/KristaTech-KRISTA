#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "print_miner_keyids", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    miners = rpc.call("getadamminers")
    if not miners:
        print("Failed to get miners")
        return
        
    for miner in miners:
        addr = miner["address"]
        info = rpc.call("validateaddress", [addr])
        if info and info.get("isvalid"):
            script_pub_key = info.get("scriptPubKey", "")
            if script_pub_key.startswith("76a914") and script_pub_key.endswith("88ac"):
                keyid = script_pub_key[6:-4]
                print(f"Index: {miner['index']}, KeyID: {keyid}, Address: {addr}, ismine: {info.get('ismine')}")
            else:
                print(f"Index: {miner['index']}, scriptPubKey: {script_pub_key}, Address: {addr}, ismine: {info.get('ismine')}")

if __name__ == "__main__":
    main()
