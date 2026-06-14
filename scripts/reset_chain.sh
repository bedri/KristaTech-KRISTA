#!/bin/bash
# reset_chain.sh: Complete reset of the KristaTech blockchain to block height 0

echo "=== Wiping and Resetting KristaTech Blockchain Network ==="

# 1. Stop auto-register daemons
echo "Stopping all auto-register daemons..."
pkill -f krista_auto_register.py || true
sleep 1

# 2. Stop container nodes
echo "Stopping container nodes..."
/home/bedri/Coin-Projects/KristaTech-KRISTA/stop_nodes.sh

# 3. Stop host node
echo "Stopping host node..."
/home/bedri/Coin-Projects/KristaTech-KRISTA/src/kristatech-cli -rpcuser=kristarpc -rpcpassword=kristarpcpass -rpcport=27979 stop 2>/dev/null || true
pkill -f kristatech-qt || true
pkill -f kristatechd || true
sleep 3

# 4. Clean host node data directory (~/.kristatech)
echo "Cleaning host data directory (~/.kristatech/)..."
HOST_DIR="$HOME/.kristatech"

# Backup wallet.dat if it exists
if [ -f "$HOST_DIR/testnet1/wallet.dat" ]; then
    echo "Backing up host wallet.dat..."
    cp "$HOST_DIR/testnet1/wallet.dat" /tmp/host_wallet_backup.dat
fi

rm -rf "$HOST_DIR"/blocks
rm -rf "$HOST_DIR"/chainstate
rm -rf "$HOST_DIR"/sporks
rm -rf "$HOST_DIR"/database
rm -rf "$HOST_DIR"/testnet1
rm -f "$HOST_DIR"/peers.dat
rm -f "$HOST_DIR"/banlist.dat
rm -f "$HOST_DIR"/mncache.dat
rm -f "$HOST_DIR"/mnpayments.dat
rm -f "$HOST_DIR"/fee_estimates.dat
rm -f "$HOST_DIR"/wallet.dat
rm -f "$HOST_DIR"/.lock
rm -f "$HOST_DIR"/db.log
rm -f "$HOST_DIR"/debug.log
rm -f "$HOST_DIR"/kristatech.pid

# Restore wallet.dat
if [ -f /tmp/host_wallet_backup.dat ]; then
    echo "Restoring host wallet.dat..."
    mkdir -p "$HOST_DIR/testnet1"
    mv /tmp/host_wallet_backup.dat "$HOST_DIR/testnet1/wallet.dat"
fi

# 5. Clean container nodes directories (net/node1 to net/node12)
for i in {1..12}
do
    NODE_DIR="/home/bedri/Coin-Projects/KristaTech-KRISTA/net/node$i"
    echo "Cleaning $NODE_DIR..."
    rm -rf "$NODE_DIR"/blocks
    rm -rf "$NODE_DIR"/chainstate
    rm -rf "$NODE_DIR"/sporks
    rm -rf "$NODE_DIR"/database
    rm -rf "$NODE_DIR"/testnet1
    rm -f "$NODE_DIR"/peers.dat
    rm -f "$NODE_DIR"/banlist.dat
    rm -f "$NODE_DIR"/mncache.dat
    rm -f "$NODE_DIR"/mnpayments.dat
    rm -f "$NODE_DIR"/fee_estimates.dat
    rm -f "$NODE_DIR"/wallet.dat
    rm -f "$NODE_DIR"/.lock
    rm -f "$NODE_DIR"/db.log
    rm -f "$NODE_DIR"/debug.log
    rm -f "$NODE_DIR"/kristatech.pid
done

# 6. Clean daemon persistent address and state files
echo "Cleaning daemon persistent state files..."
rm -f /home/bedri/Coin-Projects/KristaTech-KRISTA/scripts/miner_address_*.txt /home/bedri/Coin-Projects/KristaTech-KRISTA/miner_address_*.txt
rm -f /home/bedri/Coin-Projects/KristaTech-KRISTA/scripts/miner_state_*.json /home/bedri/Coin-Projects/KristaTech-KRISTA/miner_state_*.json
rm -f /home/bedri/Coin-Projects/KristaTech-KRISTA/scripts/krista_auto_register_*.log /home/bedri/Coin-Projects/KristaTech-KRISTA/krista_auto_register_*.log
rm -f /home/bedri/Coin-Projects/KristaTech-KRISTA/scripts/krista_auto_register_*.err /home/bedri/Coin-Projects/KristaTech-KRISTA/krista_auto_register_*.err


# 7. Clean Explorer Database
echo "Cleaning Explorer Database..."
rm -f "$HOME/.kristatech-explorer/explorer.db"

echo "=== Blockchain Wiped Successfully! ==="

# 8. Restart Host GUI Wallet
echo "Restarting Host GUI Wallet (kristatech-qt)..."
export DISPLAY=:0
nohup /home/bedri/Coin-Projects/KristaTech-KRISTA/src/qt/kristatech-qt -bypasscoordsig=0 >/home/bedri/Coin-Projects/KristaTech-KRISTA/qt_out.log 2>&1 &
sleep 5

# 9. Restart Container Nodes
echo "Restarting container nodes..."
/home/bedri/Coin-Projects/KristaTech-KRISTA/start_nodes.sh
sleep 5

# 10. Start Auto-Registers
echo "Starting auto-registration daemons..."
/home/bedri/Coin-Projects/KristaTech-KRISTA/scripts/start_all_auto_registers.sh

echo "=== System completely reset and restarted from block height 0! ==="
