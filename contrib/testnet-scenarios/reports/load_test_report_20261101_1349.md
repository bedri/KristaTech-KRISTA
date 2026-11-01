# KristaTech Testnet Load Test Report

Generated: 2026-11-01 13:52:04 +0300

## Stage: 10 tx/min for 10.0 min

- Transactions accepted: **100** (10.0/min), rejected: **0**
- Blocks sampled: 43, interval avg/max: 17.1s / 30s
- Tx per block avg/max: 3.6 / 7, block size max: 22692 bytes
- CreateNewBlock quorum wait p50/p95/max: 100 / 300 / 300 ms (29 samples)
- ConnectBlock p50/p95/max: None / None / None ms (0 samples)

| Node | ok | fail | send RPC p50/p95 ms | mempool max | getmempoolinfo p95 ms | Top error |
| :--- | ---: | ---: | :--- | ---: | ---: | :--- |
| local | 20 | 0 | 42 / 803 | 5 | 5 |  |
| seed1 | 20 | 0 | 75 / 654 | 5 | 12 |  |
| seed2 | 20 | 0 | 45 / 408 | 5 | 9 |  |
| seed3 | 20 | 0 | 36 / 764 | 5 | 5 |  |
| seed4 | 20 | 0 | 61 / 461 | 5 | 15 |  |

Top lock contention sites (`LOCKCONTENTION` Locker lines):

| Site | Count |
| :--- | ---: |
| `main.cpp:709` | 3952 |
| `net.cpp:2377` | 530 |
| `net.cpp:1914` | 155 |
| `main.cpp:1543` | 142 |
| `net.cpp:1290` | 130 |
| `net.cpp:1236` | 80 |
| `net.cpp:1395` | 65 |
| `qt/transactiontablemodel.cpp:301` | 50 |
| `adam.cpp:871` | 44 |
| `net.cpp:1944` | 39 |
| `main.cpp:5847` | 32 |
| `adam.cpp:584` | 30 |
| `net.cpp:1143` | 30 |
| `net.cpp:1306` | 30 |
| `net.cpp:1220` | 27 |

## Stage: 50 tx/min for 10.0 min

- Transactions accepted: **500** (50.0/min), rejected: **0**
- Blocks sampled: 33, interval avg/max: 23.0s / 30s
- Tx per block avg/max: 16.6 / 30, block size max: 37728 bytes
- CreateNewBlock quorum wait p50/p95/max: 100 / 300 / 300 ms (24 samples)
- ConnectBlock p50/p95/max: None / None / None ms (0 samples)

| Node | ok | fail | send RPC p50/p95 ms | mempool max | getmempoolinfo p95 ms | Top error |
| :--- | ---: | ---: | :--- | ---: | ---: | :--- |
| local | 100 | 0 | 55 / 746 | 24 | 4 |  |
| seed1 | 100 | 0 | 72 / 598 | 24 | 12 |  |
| seed2 | 100 | 0 | 45 / 339 | 25 | 8 |  |
| seed3 | 100 | 0 | 32 / 648 | 24 | 4 |  |
| seed4 | 100 | 0 | 62 / 522 | 25 | 15 |  |

Top lock contention sites (`LOCKCONTENTION` Locker lines):

| Site | Count |
| :--- | ---: |
| `main.cpp:709` | 9235 |
| `net.cpp:2377` | 963 |
| `net.cpp:1236` | 365 |
| `net.cpp:1914` | 194 |
| `qt/transactiontablemodel.cpp:301` | 183 |
| `net.cpp:1290` | 175 |
| `main.cpp:1543` | 118 |
| `net.cpp:1395` | 108 |
| `net.cpp:1944` | 99 |
| `net.cpp:1306` | 92 |
| `masternodeman.cpp:932` | 91 |
| `main.cpp:5847` | 91 |
| `adam.cpp:871` | 79 |
| `qt/transactiontablemodel.cpp:359` | 51 |
| `main.cpp:7090` | 51 |

## Stage: 200 tx/min for 10.0 min

- Transactions accepted: **2000** (200.0/min), rejected: **0**
- Blocks sampled: 26, interval avg/max: 30.0s / 45s
- Tx per block avg/max: 78.6 / 175, block size max: 216458 bytes
- CreateNewBlock quorum wait p50/p95/max: 200 / 700 / 700 ms (18 samples)
- ConnectBlock p50/p95/max: None / None / None ms (0 samples)

| Node | ok | fail | send RPC p50/p95 ms | mempool max | getmempoolinfo p95 ms | Top error |
| :--- | ---: | ---: | :--- | ---: | ---: | :--- |
| local | 400 | 0 | 79 / 777 | 185 | 4 |  |
| seed1 | 400 | 0 | 85 / 668 | 184 | 14 |  |
| seed2 | 400 | 0 | 53 / 361 | 145 | 9 |  |
| seed3 | 400 | 0 | 35 / 742 | 146 | 4 |  |
| seed4 | 400 | 0 | 72 / 526 | 147 | 16 |  |

Top lock contention sites (`LOCKCONTENTION` Locker lines):

| Site | Count |
| :--- | ---: |
| `main.cpp:709` | 23021 |
| `net.cpp:2377` | 1373 |
| `net.cpp:1236` | 818 |
| `qt/transactiontablemodel.cpp:301` | 630 |
| `main.cpp:5847` | 496 |
| `masternodeman.cpp:932` | 403 |
| `main.cpp:5997` | 361 |
| `net.cpp:1914` | 267 |
| `main.cpp:7090` | 230 |
| `qt/transactiontablemodel.cpp:359` | 227 |
| `net.cpp:1290` | 218 |
| `main.cpp:1543` | 190 |
| `net.cpp:1944` | 156 |
| `./crypter.h:178` | 152 |
| `main.cpp:5359` | 149 |

## Stage: 500 tx/min for 10.0 min

- Transactions accepted: **5000** (500.0/min), rejected: **0**
- Blocks sampled: 24, interval avg/max: 36.5s / 60s
- Tx per block avg/max: 212.3 / 451, block size max: 584043 bytes
- CreateNewBlock quorum wait p50/p95/max: 200 / 1700 / 1700 ms (17 samples)
- ConnectBlock p50/p95/max: None / None / None ms (0 samples)

| Node | ok | fail | send RPC p50/p95 ms | mempool max | getmempoolinfo p95 ms | Top error |
| :--- | ---: | ---: | :--- | ---: | ---: | :--- |
| local | 1000 | 0 | 152 / 859 | 459 | 472 |  |
| seed1 | 1000 | 0 | 109 / 666 | 467 | 60 |  |
| seed2 | 1000 | 0 | 67 / 397 | 472 | 181 |  |
| seed3 | 1000 | 0 | 42 / 616 | 447 | 5 |  |
| seed4 | 1000 | 0 | 87 / 547 | 471 | 69 |  |

Top lock contention sites (`LOCKCONTENTION` Locker lines):

| Site | Count |
| :--- | ---: |
| `main.cpp:709` | 42331 |
| `main.cpp:5847` | 1954 |
| `main.cpp:5997` | 1623 |
| `qt/transactiontablemodel.cpp:301` | 1541 |
| `qt/transactiontablemodel.cpp:359` | 1050 |
| `masternodeman.cpp:932` | 871 |
| `net.cpp:2377` | 726 |
| `net.cpp:1236` | 656 |
| `main.cpp:5359` | 601 |
| `main.cpp:7090` | 529 |
| `main.cpp:1543` | 357 |
| `adam.cpp:871` | 258 |
| `wallet/rpcwallet.cpp:763` | 240 |
| `adam.cpp:584` | 191 |
| `./crypter.h:178` | 176 |

