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
