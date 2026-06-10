#!/bin/bash
# stop_nodes.sh: Stop and remove 12 KristaTech container nodes

echo "Stopping and removing 12 KristaTech container nodes..."

for i in {1..12}
do
    name="krista-node$i"
    echo "Stopping $name..."
    podman stop "$name" 2>/dev/null
    echo "Removing $name..."
    podman rm -f "$name" 2>/dev/null
done

echo "Done!"
