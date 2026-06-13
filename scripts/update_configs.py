#!/usr/bin/env python3
import os

def main():
    nodes_count = 12
    base_dir = "/home/bedri/Coin-Projects/KristaTech-KRISTA/net"

    print("Updating kristatech.conf files...")
    for i in range(1, nodes_count + 1):
        config_path = os.path.join(base_dir, f"node{i}", "kristatech.conf")
        if not os.path.exists(config_path):
            print(f"Config not found: {config_path}")
            continue

        # Read original config and filter out any existing addnode lines
        with open(config_path, "r") as f:
            lines = f.readlines()
        
        filtered_lines = [line for line in lines if not line.startswith("addnode=")]

        # Append new addnode lines
        new_lines = list(filtered_lines)
        if not new_lines[-1].endswith("\n"):
            new_lines.append("\n")

        for j in range(1, nodes_count + 1):
            if i == j:
                continue
            new_lines.append(f"addnode=krista-node{j}:27999\n")

        with open(config_path, "w") as f:
            f.writelines(new_lines)

        print(f"Updated {config_path}")

if __name__ == "__main__":
    main()
