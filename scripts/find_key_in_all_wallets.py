#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "search_wallets", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    target_keyid = "b14cf79bac32711d4716c10a5b8088defa24ed80".lower()
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    
    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        # Get all addresses
        addresses = []
        labels = rpc.call("listlabels")
        if labels is not None:
            if "" not in labels:
                labels.append("")
            for label in labels:
                addr_dict = rpc.call("getaddressesbylabel", [label])
                if addr_dict:
                    addresses.extend(addr_dict.keys())
                    
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
                    
        for addr in addresses:
            info = rpc.call("validateaddress", [addr])
            if info and info.get('isvalid'):
                script_pub_key = info.get('scriptPubKey', '')
                if script_pub_key.startswith("76a914") and script_pub_key.endswith("88ac"):
                    keyid = script_pub_key[6:-4]
                    if keyid.lower() == target_keyid:
                        print(f"FOUND MATCH IN WALLET! Port {port} owns address {addr} for Key ID {keyid}")
                        return
                        
    print("Target key ID was NOT found in any node's wallet.")

if __name__ == "__main__":
    main()
