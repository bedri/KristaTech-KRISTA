# Sentry Node (Proxy Relay) Setup Guide

Sentry Node architecture is an advanced infrastructure pattern designed to protect your real Masternode (and its quorum signing private keys) from targeted DoS/DDoS attacks.

In this layout, your active Masternode is completely shielded from the public internet (runs behind a private subnetwork or rejects all direct inbound connections) and communicates with the P2P network exclusively via DDoS-protected relay servers (Sentry Nodes).

---

## How It Works

```mermaid
graph TD
    Attacker[Attacker / Botnet] -- DDoS Attack --x Sentry[Sentry Node - DDoS Protected IP]
    Sentry -- Secure P2P Relay -- Masternode[Masternode - Hidden Private IP]
    Masternode -- outbound connection -- Sentry
```

* **Sentry Node (Edge Server):** A public VPS hosted on a DDoS-protected provider (OVH, Path.net, Cloudflare Magic Transit, etc.). The public network only interacts with this node's IP.
* **Masternode (Core Server):** The server running your masternode key. It does not accept any inbound public connections (`listen=0`) and only makes outbound connections to your own Sentry node.

---

## Setup Steps

### 1. Configure the Sentry Node (Edge Server)
Set up a standard `kristatechd` daemon on your public DDoS-protected Sentry server. This node does not need collateral or masternode private keys.

Edit your `kristatech.conf` file:
```ini
# Sentry Node Config
server=1
listen=1
port=27999
maxconnections=125
```

Save your Sentry server's public IP address (e.g., `198.51.100.50`).

---

### 2. Configure the Real Masternode (Hidden)
On your hidden masternode server, edit your `kristatech.conf` file:

```ini
# Masternode Configuration (Hidden)
server=1
listen=0 # Deny all incoming public P2P connections

# Connect ONLY to your own Sentry node
connect=198.51.100.50:27999

# The public IP address the masternode announces to the network
# (Attackers will target this IP, but your server will instantly drop requests due to listen=0)
externalip=203.0.113.10:27999

# Masternode Private Key
masternodeprivkey=2UNUY2rGp3gDL9wCyjcXoaBqfL4qCpe5oJbEpYaTGyovSh1fa8F
masternode=1
```

Start your daemon:
```bash
kristatechd -daemon
```

---

## Firewall (UFW) Recommendations

On your core masternode server, restrict incoming P2P packets so they are only accepted from your Sentry IP:

```bash
# Apply default UFW rules
sudo ufw default deny incoming
sudo ufw default allow outgoing

# Allow P2P packets only from your Sentry IP (replace 198.51.100.50 with your actual Sentry IP)
sudo ufw allow from 198.51.100.50 to any port 27999 proto tcp comment 'Sentry Link'

# Allow SSH only from your management IP (replace 1.2.3.4 with your admin IP)
sudo ufw allow from 1.2.3.4 to any port 22 proto tcp comment 'Safe SSH'

# Enable the firewall
sudo ufw enable
```

### Why this setup is secure:
1. Even if attackers fetch your `externalip` from the blockchain and attempt a DDoS, your core server drops the packets at the firewall level or ignores them because `listen=0` is set.
2. Your masternode continues to sync block templates and vote on LLMQ quorums securely by using your Sentry server as an outbound proxy.
3. If your Sentry server goes down due to a massive volumetric attack, your core masternode remains unharmed. You can spin up a new Sentry server on a different IP, update `connect=`, and re-establish connection immediately.
