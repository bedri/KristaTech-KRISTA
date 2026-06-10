#!/usr/bin/env python3
import subprocess
import sys
import time
import signal

def main():
    ports = [27979] + [28000 + 2 * i for i in range(1, 13)]
    processes = {}

    print(f"Starting auto-registers for ports: {ports}")

    for port in ports:
        cmd = ["python3", "scripts/krista_auto_register.py", "pow", str(port)]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        processes[port] = p
        print(f"  Started auto-register for port {port} (PID: {p.pid})")

    # Handle shutdown signals to terminate subprocesses cleanly
    def signal_handler(sig, frame):
        print("\nShutting down all auto-register daemons...")
        for port, p in processes.items():
            print(f"  Terminating port {port} (PID: {p.pid})...")
            p.terminate()
        for port, p in processes.items():
            p.wait()
        print("All daemons stopped.")
        sys.exit(0)

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    try:
        while True:
            # Check status of each process and print any output
            for port, p in list(processes.items()):
                # Check if process has terminated
                ret = p.poll()
                if ret is not None:
                    print(f"Process for port {port} exited with code {ret}. Restarting...")
                    cmd = ["python3", "scripts/krista_auto_register.py", "pow", str(port)]
                    new_p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                    processes[port] = new_p
                else:
                    # Print any available output without blocking
                    # (To keep stdout clean, we can just let it run in the background,
                    # but we can also log to files if we want. Let's just let it run).
                    pass
            time.sleep(5)
    except KeyboardInterrupt:
        signal_handler(None, None)

if __name__ == "__main__":
    main()
