#!/bin/bash

# Navigate to the script directory
cd "$(dirname "$0")"

echo "Stopping any currently running krista_auto_register.py instances..."
pkill -f krista_auto_register.py
sleep 1

echo "Starting Auto-Registration Daemons in background..."

# 1. Start for Host GUI Wallet (port 27979)
python3 krista_auto_register.py pow 27979 > krista_auto_register_27979.err 2>&1 &
echo "  Started daemon for Host GUI (Port 27979)"

# 2. Start for Nodes 2-12 (Ports 28004 to 28024)
for i in {2..12}
do
    port=$((28000 + 2 * i))
    python3 krista_auto_register.py pow $port > krista_auto_register_$port.err 2>&1 &
    echo "  Started daemon for Node $i (Port $port)"
done

echo "All Auto-Registration Daemons started successfully!"
wait
