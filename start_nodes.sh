#!/bin/bash
# start_nodes.sh: Start 12 KristaTech container nodes

echo "Starting 12 KristaTech container nodes..."

for i in {1..12}
do
    p2p_port=$((28000 + 2 * i - 1))
    rpc_port=$((28000 + 2 * i))
    name="krista-node$i"
    
    echo "Starting $name (P2P: $p2p_port, RPC: $rpc_port)..."
    
    podman run -d \
        --name "$name" \
        --net dsw-net \
        -v "/home/bedri/Coin-Projects/KristaTech-KRISTA:/dsw:z" \
        -v "/home/bedri/Coin-Projects/KristaTech-KRISTA/net/node$i:/dsw-data:z" \
        -w /dsw \
        -p "$rpc_port:27979" \
        -p "$p2p_port:27999" \
        registry.opensuse.org/opensuse/leap-micro/6.1/toolbox:latest \
        ./src/kristatechd -port=27999 -rpcport=27979 -datadir=/dsw-data -bypasscoordsig=0 -debug=1 -printtoconsole
done

echo "All 12 nodes started successfully!"
