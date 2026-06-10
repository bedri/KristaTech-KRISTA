#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "map_miners", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    miner_addrs = [
        "KTNrvQkwv1YoDosP5X9vvnqMmnWAXCYv35p",
        "KTPnaXkeZm9LM5pcWaHgkao41ea8EHdUzMh",
        "KTcMeQwy4PC9HRQJfaWetSFXLJz4BDZ12FP",
        "KTR7kPiGRNbGKToT8gv1zfjRzrgr1SuuYoy",
        "KTfV6cXR3cFxW9tCYoz78C5xUsZRwgKCjF8",
        "KTZjk75Fq2p7dw1pfWBHxHbVsnRMiBFqKz1",
        "KTRNsNZXrKYj6DYLnpPt8SWZgyJujQPpA9g",
        "KTQREm5emtf4GFyuuQrqs2zW5DsKfNf4u8K",
        "KTWtMY97YB7R7vTL1q6SMaskvyApo9or95J",
        "KTRUfPo8evwwFQb5P4BG636U8fCMda21ta8",
        "KTW6yDA3ghxiKwWMQGJxKgL8ABufqHBmW6L",
        "KTMyUNkWJTPiRUL56i9Se4CsRWELjuBTvre",
        "KTgvxMwKyQ7haFo98Jj1kYzmCJP5AUdRbH8"
    ]
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    
    mapping = {}
    for addr in miner_addrs:
        mapping[addr] = []

    for port in ports:
        rpc = KristaRPC("127.0.0.1", port, "kristarpc", "kristarpcpass")
        for addr in miner_addrs:
            info = rpc.call("validateaddress", [addr])
            if info and info.get("ismine"):
                mapping[addr].append(port)

    print("Mapping of registered miner addresses to wallet ports:")
    for addr, owned_ports in mapping.items():
        print(f"{addr} -> Ports: {owned_ports}")

if __name__ == "__main__":
    main()
