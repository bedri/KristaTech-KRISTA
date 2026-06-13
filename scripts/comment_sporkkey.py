#!/usr/bin/env python3
import os

def main():
    nodes_count = 12
    base_dir = "/home/bedri/Coin-Projects/KristaTech-KRISTA/net"

    print("Commenting out sporkkey in kristatech.conf files...")
    for i in range(1, nodes_count + 1):
        config_path = os.path.join(base_dir, f"node{i}", "kristatech.conf")
        if not os.path.exists(config_path):
            print(f"Config not found: {config_path}")
            continue

        with open(config_path, "r") as f:
            lines = f.readlines()

        new_lines = []
        for line in lines:
            if line.strip().startswith("sporkkey="):
                new_lines.append("#" + line)
                print(f"  Commented sporkkey in node{i}")
            else:
                new_lines.append(line)

        with open(config_path, "w") as f:
            f.writelines(new_lines)

if __name__ == "__main__":
    main()
