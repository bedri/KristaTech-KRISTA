#!/usr/bin/env bash
# Copyright (c) 2026 The KristaTech developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Simultaneous binary deployment to all seed servers (AGENTS.md rule 4 / rules.md rule 7):
#   1. stop every seed daemon and verify it is down
#   2. upload binaries (with retries) and verify the version string on every seed
#   3. start every seed only after ALL seeds carry the expected version
#
# Usage: contrib/testnet-scenarios/deploy_seeds.sh [path/to/kristatechd] [path/to/kristatech-cli]
set -u
DAEMON=${1:-src/kristatechd}
CLI=${2:-src/kristatech-cli}
SEEDS="root@168.119.227.178 root@95.217.188.96 bedri@157.90.130.224 root@167.233.98.102"
SSH_OPTS="-o StrictHostKeyChecking=no -o ConnectTimeout=15"
export SSH_ASKPASS=${SSH_ASKPASS:-/usr/bin/ksshaskpass} SSH_ASKPASS_REQUIRE=${SSH_ASKPASS_REQUIRE:-force}
EXPECTED=$("$DAEMON" --version | head -1)
echo "Deploying: $EXPECTED"

echo "== 1/3 stopping all seeds"
for h in $SEEDS; do
  ssh $SSH_OPTS "$h" '~/kristatech-cli stop >/dev/null 2>&1; for i in $(seq 1 60); do pgrep -x kristatechd >/dev/null || break; sleep 2; done; pkill -9 -x kristatechd 2>/dev/null; sleep 1; pgrep -x kristatechd >/dev/null && echo "'"$h"': STILL RUNNING" || echo "'"$h"': stopped"' < /dev/null &
done; wait

echo "== 2/3 uploading binaries"
fail=0
for h in $SEEDS; do
  ok=0
  for attempt in 1 2 3 4 5; do
    scp -q $SSH_OPTS "$DAEMON" "$CLI" "$h":~/ < /dev/null 2>/dev/null
    v=$(ssh $SSH_OPTS "$h" 'chmod +x ~/kristatechd ~/kristatech-cli; ~/kristatechd --version | head -1' < /dev/null)
    if [ "$v" = "$EXPECTED" ]; then ok=1; break; fi
    sleep 3
  done
  if [ $ok = 1 ]; then echo "$h: $v"; else echo "$h: UPLOAD FAILED (have: $v)"; fail=1; fi
done
if [ $fail = 1 ]; then echo "ABORT: not all seeds carry $EXPECTED; daemons left stopped"; exit 1; fi

echo "== 3/3 starting all seeds"
for h in $SEEDS; do
  ssh $SSH_OPTS "$h" 'nohup ~/kristatechd -daemon >/dev/null 2>&1 </dev/null &' < /dev/null &
done; wait
echo "DEPLOYED $EXPECTED"
