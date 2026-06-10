#!/usr/bin/env python3
import json
import urllib.request
import base64

class KristaRPC:
    def __init__(self, ip, port, user, password):
        self.url = f"http://{ip}:{port}"
        self.auth = base64.b64encode(f"{user}:{password}".encode('utf-8')).decode('utf-8')

    def call(self, method, params=[]):
        data = json.dumps({"jsonrpc": "2.0", "id": "get_reg", "method": method, "params": params}).encode('utf-8')
        req = urllib.request.Request(self.url, data=data, headers={'content-type': 'application/json'})
        req.add_header("Authorization", f"Basic {self.auth}")
        try:
            with urllib.request.urlopen(req, timeout=5.0) as response:
                return json.loads(response.read().decode('utf-8'))['result']
        except Exception as e:
            return None

def main():
    rpc = KristaRPC("127.0.0.1", 28002, "kristarpc", "kristarpcpass")
    
    txids = [
        "2c9b8f97aa8fe152ca22277f7ed57d60635654501b23f311e1f4d959a2d115ff",
        "5ded2c7f8696bd23be2495822db62197a9c53d9a79aee75ec0b79be115a5dd55",
        "158943d91ea7dc0d26a8b018c278e167fce33a241bc06fd12e05b1b41d3a2e62",
        "6076b9decdfc181ee20ef6667403eabb6c827021e89da49f8fc34fba4463d5af",
        "65f0a7fa231b75a2625467e7b5f0dbdd936b2d68af862773cc1af6d644909f55",
        "5a8dd4848eb8c7eec819d047e75bfdd442cc12008b267a23aff3811e4ad8317e",
        "9fa2f3edadc981ae5b403de0527eb9987f3e438b3e0c58db90a22633c553ce04",
        "9e01f4acbb2463ba8f3040b1b161bb96f7b52a85497d2451353ec5ea164f4b9f",
        "5486d34f3d8651d9f87fafca62a7814f83918f38d5700c852e800e127ff99868",
        "a05527a4f758bc0340f16bd4463a5b06b17c050a0b5e2cffe1b2464b88b9003e",
        "7671299b7a43f2addd9ca3a057317b3ca0e00b9d6681d03621a36a55ec6a1eda",
        "cdebd8fd6583dc219a098589fa71b73ba0b177f8032689a81e373d95a843f8c3",
        "6f5463d73ae234033c6f40ee0c76c513ba3a8c346ef562f2ed9b3faef0113413",
        "7a1cbd8d24eb8efcaaeea57e394b3495d6c17cf36567339d42e9fb1cc89aa0e8",
        "fed5d80b2ecbd3e605c893dadb9fe40d662127b8c88118b51ce95eb1742f2c47",
        "5e8af7d3b5f00ef58039956f09c9e33a2ba10fe95c60dcd2e14665d28ec6aa79",
        "eebb9441a2c863be6ac9125b6e706787138f06bab3294402b037f88915c007ae",
        "ee1983e8c7bc3e3bd63e1893ad1670c09a79ec8e46ed7853cbf5b1bd28ef105f",
        "6bb421d2068b3bf2cdc1c0bcbd355daa3f96e51ab9f609f084e74620fd9bddf8",
        "85fe6848fee252b3e42efa2f9a532e225045e4e46dc7df3eb3067c6a1d0d4678",
        "1a748ee3c830e2280cbc8cd001609320d4b9ebc3f5e06c03f06c4317921f5834",
        "3cb6ab55253782a14c728a0991db357142827b8e10180b62857ed813aea49566",
        "97f644306f18f61e0fcb11e62199d187be815dcb100c79dc055814f0c702b9d7",
        "247b0352c943a4a236c01ec09e763c651c2c8fb5bf673db12b24b003056b0763",
        "d10d1f5a0f437f613295e82669eb3127841ff76f5c2d1339cad9c4f0cb0a34dd",
        "435ba687918e541a06019f31f63c588940c751f2e83197c79087fa213e866136",
        "0aef223c6513845fe80b065e36b66003e9758b08794dcd4e9dbd7e2315b16b3a"
    ]
    
    for txid in txids:
        tx = rpc.call("getrawtransaction", [txid, 1])
        if not tx:
            continue
            
        print(f"Tx: {txid}")
        for v in tx.get('vout', []):
            script = v.get('scriptPubKey', {})
            asm = script.get('asm', '')
            if 'OP_CHECKLOCKTIMEVERIFY' in asm:
                # This is a miner registration!
                print(f"  [REGISTRATION] Value: {v.get('value')}")
                print(f"  ASM: {asm[:150]}...")
                if 'addresses' in script:
                    print(f"  Addresses: {script['addresses']}")

if __name__ == "__main__":
    main()
