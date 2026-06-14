#!/usr/bin/env python3
import json
import urllib.request
import base64
import subprocess

class KristaRPC:
    def __init__(self, port):
        self.url = f"http://127.0.0.1:{port}"
        self.auth = base64.b64encode(b"kristarpc:kristarpcpass").decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "connect_nodes", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    # Run podman network inspect dsw-net to get current IP addresses
    res = subprocess.run(["podman", "network", "inspect", "dsw-net"], capture_output=True, text=True)
    if res.returncode != 0:
        print("Failed to run podman network inspect")
        return
    
    try:
        net_data = json.loads(res.stdout)[0]
    except Exception as e:
        print(f"Failed to parse podman network inspect output: {e}")
        return

    containers = net_data.get("containers", {})
    
    resolved_nodes = []
    name_to_port = {
        "krista-node1": 28002,
        "krista-node2": 28004,
        "krista-node3": 28006,
        "krista-node4": 28008,
        "krista-node5": 28010,
        "krista-node6": 28012,
        "krista-node7": 28014,
        "krista-node8": 28016,
        "krista-node9": 28018,
        "krista-node10": 28020,
        "krista-node11": 28022,
        "krista-node12": 28024,
    }
    
    for c_id, c_info in containers.items():
        name = c_info.get("name")
        if name in name_to_port:
            interfaces = c_info.get("interfaces", {})
            for eth, eth_info in interfaces.items():
                subnets = eth_info.get("subnets", [])
                if subnets:
                    ip = subnets[0].get("ipnet", "").split("/")[0]
                    resolved_nodes.append({"port": name_to_port[name], "name": name, "ip": ip})
                    print(f"Found container {name} with IP {ip}")

    print(f"Connecting {len(resolved_nodes)} nodes together...")
    for src in resolved_nodes:
        rpc = KristaRPC(src["port"])
        for dest in resolved_nodes:
            if src["port"] == dest["port"]:
                continue
            # Try both onetry and add
            rpc.call("addnode", [f"{dest['ip']}:27989", "onetry"])
            rpc.call("addnode", [f"{dest['ip']}:27989", "add"])
            print(f"Node on port {src['port']} connecting to {dest['name']} ({dest['ip']})")

if __name__ == "__main__":
    main()
