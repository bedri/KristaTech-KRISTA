#!/usr/bin/env python3
import json
import urllib.request
import base64
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
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    target_keyid = "fa82bd7b304d8a9e71389afc5657920549ca5a6e".lower()
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        
        addresses = []
        # Get labels
        labels = rpc.call("listlabels")
        if labels is not None:
            # listlabels might not include empty label if there's no address under it, but let's check it anyway
            if "" not in labels:
                labels.append("")
            for label in labels:
                addr_dict = rpc.call("getaddressesbylabel", [label])
                if addr_dict:
                    # getaddressesbylabel returns a dict where keys are addresses
                    addresses.extend(addr_dict.keys())
        
        # Fallback to groupings/received
        groupings = rpc.call("listaddressgroupings")
        if groupings:
            for group in groupings:
                for entry in group:
                    if entry[0] not in addresses:
                        addresses.append(entry[0])
                        
        received = rpc.call("listreceivedbyaddress", [0, True])
        if received:
            for entry in received:
                if entry.get('address') not in addresses:
                    addresses.append(entry['address'])

        # Now validate each address to find the key ID
        for addr in addresses:
            info = rpc.call("validateaddress", [addr])
            if info and info.get('isvalid'):
                script_pub_key = info.get('scriptPubKey', '')
                if script_pub_key.startswith("76a914") and script_pub_key.endswith("88ac"):
                    keyid = script_pub_key[6:-4]
                    if keyid.lower() == target_keyid:
                        print(f"FOUND! Port {port} contains address {addr} with key ID {keyid}")
                        sys.exit(0)
                        
    print("Target key ID not found in any of the wallets.")

if __name__ == "__main__":
    main()
