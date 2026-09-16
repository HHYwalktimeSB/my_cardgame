#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
match_steps=${MATCH_STEPS:-"10 25 50 100"}
: "${RUN_ID:?set a short RUN_ID shared with the load-generator machine}"
: "${PASSWORD:?set PASSWORD shared with the load-generator machine}"

[[ $RUN_ID =~ ^[A-Za-z0-9_]{1,16}$ ]] || {
    echo "RUN_ID must contain 1-16 letters, numbers, or underscores" >&2
    exit 1
}
command -v docker >/dev/null || {
    echo "docker is required on the game server" >&2
    exit 1
}
docker compose version >/dev/null

total_matches=0
for matches in $match_steps; do
    [[ $matches =~ ^[1-9][0-9]*$ ]] || {
        echo "invalid match count in MATCH_STEPS: $matches" >&2
        exit 1
    }
    total_matches=$((total_matches + matches))
done
password_hash=$(printf %s "$PASSWORD" | sha256sum | awk '{print $1}')

printf 'Preparing %s accounts for %s total matches (RUN_ID=%s)\n' \
    "$((total_matches * 2))" "$total_matches" "$RUN_ID"
docker compose exec -T db sh -c '
    exec psql -U "$POSTGRES_USER" -d "$POSTGRES_DB" \
        -v ON_ERROR_STOP=1 \
        -v "matches=$1" \
        -v "run_id=$2" \
        -v "password_hash=$3"
' sh "$total_matches" "$RUN_ID" "$password_hash" \
    < "$repo_dir/loadtest/prepare.sql"

echo "Preparation complete. Use the same RUN_ID, PASSWORD, and MATCH_STEPS locally."
