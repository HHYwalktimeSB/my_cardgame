#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
base_url=${BASE_URL:-http://119.91.72.158}
match_steps=${MATCH_STEPS:-"10 25 50 100"}
duration=${DURATION:-60}
action_interval_ms=${ACTION_INTERVAL_MS:-750}
slow_operation_ms=${SLOW_OPERATION_MS:-1000}
slow_operation_log_limit=${SLOW_OPERATION_LOG_LIMIT:-5}
k6_image=${K6_IMAGE:-grafana/k6:0.54.0}
results_dir=${RESULTS_DIR:-"$repo_dir/loadtest/results"}
: "${RUN_ID:?set RUN_ID to the value used by prepare-capacity.sh}"
: "${PASSWORD:?set PASSWORD to the value used by prepare-capacity.sh}"

if ! command -v k6 >/dev/null && ! command -v docker >/dev/null; then
    echo "install k6 or Docker on the load-generator machine" >&2
    exit 1
fi
install -d "$results_dir"

last_stable_players=0
failed_players=0
stage_number=0
player_offset=0

run_k6()
{
    local matches=$1
    local run_id=$2
    local summary_file="$results_dir/${run_id}.json"
    if command -v k6 >/dev/null; then
        k6 run \
            -e "BASE_URL=$base_url" \
            -e "MATCHES=$matches" \
            -e "RUN_ID=$RUN_ID" \
            -e "PASSWORD=$PASSWORD" \
            -e "PLAYER_OFFSET=$player_offset" \
            -e "DURATION=$duration" \
            -e "ACTION_INTERVAL_MS=$action_interval_ms" \
            -e "SLOW_OPERATION_MS=$slow_operation_ms" \
            -e "SLOW_OPERATION_LOG_LIMIT=$slow_operation_log_limit" \
            --console-output "$results_dir/${run_id}.log" \
            --summary-export "$summary_file" \
            "$repo_dir/loadtest/k6_battle.js"
    else
        sudo docker run --rm \
            -v "$repo_dir/loadtest:/scripts:ro" \
            -v "$results_dir:/results" \
            "$k6_image" run \
            -e "BASE_URL=$base_url" \
            -e "MATCHES=$matches" \
            -e "RUN_ID=$RUN_ID" \
            -e "PASSWORD=$PASSWORD" \
            -e "PLAYER_OFFSET=$player_offset" \
            -e "DURATION=$duration" \
            -e "ACTION_INTERVAL_MS=$action_interval_ms" \
            -e "SLOW_OPERATION_MS=$slow_operation_ms" \
            -e "SLOW_OPERATION_LOG_LIMIT=$slow_operation_log_limit" \
            --console-output "/results/${run_id}.log" \
            --summary-export "/results/${run_id}.json" \
            /scripts/k6_battle.js
    fi
}

for matches in $match_steps; do
    [[ $matches =~ ^[1-9][0-9]*$ ]] || {
        echo "invalid match count in MATCH_STEPS: $matches" >&2
        exit 1
    }
    stage_number=$((stage_number + 1))
    players=$((matches * 2))
    run_id="${RUN_ID}_${stage_number}_${players}p"

    printf '\nPreparing and running %s concurrent players, run_id=%s\n' \
        "$players" "$run_id"
    if run_k6 "$matches" "$run_id"; then
        last_stable_players=$players
        printf 'PASS: %s concurrent players\n' "$players"
    else
        failed_players=$players
        printf 'FAIL: thresholds exceeded at %s concurrent players\n' "$players"
        break
    fi
    player_offset=$((player_offset + players))
done

printf '\nCapacity result for %s\n' "$base_url"
if (( failed_players > 0 )); then
    printf 'Last stable stage: %s players\n' "$last_stable_players"
    printf 'First failed stage: %s players\n' "$failed_players"
else
    printf 'All stages passed; tested capacity is at least %s players\n' \
        "$last_stable_players"
fi
printf 'Detailed summaries: %s\n' "$results_dir"
