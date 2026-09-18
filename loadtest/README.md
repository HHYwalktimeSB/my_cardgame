# Battle load test

The load test uses one k6 VU for one match, so `MATCHES=10` represents 20 players
and 20 WebSocket connections.

## Prepare data

Run this against a non-production database. `RUN_ID` must match the value passed
to k6.

```bash
psql postgresql://cardgame_user:cardgame_password@127.0.0.1/cardgame_db \
  -v matches=10 -v run_id=run01 -f loadtest/prepare.sql
```

This creates 15 zero-cost 1/1 minions, two copies of each per player, and a
30-card deck. Every minion produces 16 battlecry effect events. When two minions
trade, their deathrattles enqueue another 64 effect events in total.

## Run

Start the backend, then run:

```bash
k6 run \
  -e BASE_URL=http://127.0.0.1:5555 \
  -e MATCHES=10 \
  -e RUN_ID=run01 \
  -e DURATION=60 \
  loadtest/k6_battle.js
```

Increase `MATCHES` between runs. Use a new `RUN_ID` if the previous backend
process still contains finished rooms or matchmaking state. `ACTION_INTERVAL_MS`
controls the delay between operations and defaults to 750 ms.

The setup phase creates each match sequentially so concurrently starting VUs do
not get paired with players from another VU. The default thresholds require less
than 1% failed operations and snapshots, p95 operation latency below 200 ms, all
expected WebSocket connections, and no WebSocket errors.

## Find player capacity

Prepare accounts once on the game server:

```bash
RUN_ID=cap01 \
PASSWORD='replace-with-a-random-test-password' \
MATCH_STEPS="10 25 50 100" \
./loadtest/prepare-capacity.sh
```

Then run the load from the local load-generator machine. Use the same values:

```bash
RUN_ID=cap01 \
PASSWORD='replace-with-a-random-test-password' \
BASE_URL=http://119.91.72.158 \
MATCH_STEPS="10 25 50 100" \
./loadtest/run-capacity.sh
```

The default stages represent 20, 50, 100, then 200 concurrent players. Run the
load script on a separate machine with either k6 or Docker. Only the preparation
step needs access to the server's Compose database. Each stage uses a separate
range of accounts, so rooms left by an earlier stage cannot affect later stages.
Each k6 VU controls a match with two logged-in players and two WebSocket
connections. The script stops at the first failed threshold and reports the last
stable stage. `DURATION` and `ACTION_INTERVAL_MS` customize the workload.

The JSON summary reports operation counts and latency separately for attacks,
card plays, and end turns. Operations slower than `SLOW_OPERATION_MS` (1000 ms by
default) are written to the matching `.log` file with their room, card, submitted
and acknowledged versions, WebSocket round-trip time, and server error code. At
most `SLOW_OPERATION_LOG_LIMIT` entries (5 by default) are logged per VU to keep
diagnostic output bounded. Battle operations and their acknowledgements use the
same WebSocket connection as battle events; HTTP is only used for setup, login,
catalog, and initial snapshots.

WebSocket traffic is reported as `battle_websocket_bytes_received` and
`battle_websocket_message_size`, including operation acknowledgements. The server
`/metrics` endpoint exposes event-payload
`battle_websocket_payload_bytes_total` and `battle_websocket_messages_total`
counters for measuring outbound payload independently of k6 HTTP traffic.

`MATCH_STEPS` counts matches, so the player count is twice each value. A locally
installed `k6` is used when available; otherwise the script runs
`grafana/k6:0.54.0`. JSON summaries are written to `loadtest/results/`.
