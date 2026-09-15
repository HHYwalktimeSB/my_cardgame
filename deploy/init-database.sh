#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
: "${DATABASE_URL:?set DATABASE_URL to an empty PostgreSQL database}"

psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f "$repo_dir/db/schema.sql"
