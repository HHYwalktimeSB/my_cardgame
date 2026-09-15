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

Run the staged capacity test on the deployed server from the repository root:

```bash
./loadtest/run-capacity.sh
```

It defaults to `http://119.91.72.158` and tests 20, 50, 100, then 200 concurrent
players. Each k6 VU controls a match with two logged-in players and two WebSocket
connections. The script stops at the first failed threshold and reports the last
stable stage. It uses the Compose PostgreSQL container to prepare test accounts;
run it from the deployed repository where `docker compose` can access `db`.

Customize the target, stages, and duration with environment variables:

```bash
BASE_URL=http://119.91.72.158 \
MATCH_STEPS="25 50 100 150 200" \
DURATION=120 \
ACTION_INTERVAL_MS=750 \
./loadtest/run-capacity.sh
```

`MATCH_STEPS` counts matches, so the player count is twice each value. A locally
installed `k6` is used when available; otherwise the script runs
`grafana/k6:0.54.0`. JSON summaries are written to `loadtest/results/`.
