#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "check_gbt", "method": method, "params": params}).encode('utf-8')
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
        gbt = rpc.call("getblocktemplate", [{"capabilities": ["coinbasetxn", "coinbasevalue", "longpoll", "workid"], "rules": ["segwit"]}])
        if gbt:
            vrf = gbt.get("adamvrfproof", "")
            coord_sig = gbt.get("adamcoordinatorsig", "")
            miners = gbt.get("adamminers", [])
            print(f"Port {port}: GBT height={gbt.get('height')}, miners={len(miners)}, vrf_len={len(vrf)}, sig_len={len(coord_sig)}")
            if len(vrf) > 0:
                print(f"--> FOUND SIGNING NODE AT PORT {port}!")
        else:
            print(f"Port {port}: Failed to get block template")

if __name__ == "__main__":
    main()
