#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
FRONTEND_DIR="$ROOT_DIR/frontend"
BACKEND_BIN="$BUILD_DIR/card_game"

check_port() {
  local host="$1"
  local port="$2"
  bash -lc "exec 3<>/dev/tcp/${host}/${port}" >/dev/null 2>&1
}

try_start_postgres() {
  if command -v systemctl >/dev/null 2>&1; then
    systemctl start postgresql && return 0
  fi

  if command -v service >/dev/null 2>&1; then
    service postgresql start && return 0
  fi

  if command -v pg_ctlcluster >/dev/null 2>&1; then
    local cluster_version
    local cluster_name
    cluster_version="$(pg_lsclusters -h 2>/dev/null | awk 'NR==1 {print $1}')"
    cluster_name="$(pg_lsclusters -h 2>/dev/null | awk 'NR==1 {print $2}')"
    if [[ -n "$cluster_version" && -n "$cluster_name" ]]; then
      pg_ctlcluster "$cluster_version" "$cluster_name" start && return 0
    fi
  fi

  return 1
}

if [[ ! -x "$BACKEND_BIN" ]]; then
  echo "missing backend binary: $BACKEND_BIN"
  echo "build it first:"
  echo "  mkdir -p build && cd build && cmake .. && make -j"
  exit 1
fi

if [[ ! -f "$FRONTEND_DIR/package.json" ]]; then
  echo "missing frontend/package.json"
  exit 1
fi

if ! check_port 127.0.0.1 5432; then
  echo "postgresql is not reachable on 127.0.0.1:5432"
  echo "trying to start postgresql service"
  if ! try_start_postgres; then
    echo "failed to start postgresql automatically"
    echo "check your database service and config.json credentials first"
    exit 1
  fi

  for _ in {1..20}; do
    if check_port 127.0.0.1 5432; then
      break
    fi
    sleep 0.5
  done

  if ! check_port 127.0.0.1 5432; then
    echo "postgresql still not reachable on 127.0.0.1:5432"
    exit 1
  fi
fi

if check_port 127.0.0.1 5555; then
  echo "port 5555 is already in use"
  echo "stop the existing backend first, then rerun ./start-dev.sh"
  echo "this project must start backend from build/ so ../config.json resolves correctly"
  exit 1
else
  echo "starting backend from $BUILD_DIR"
  (
    cd "$BUILD_DIR"
    ./card_game
  ) &
  BACKEND_PID=$!

  for _ in {1..20}; do
    if check_port 127.0.0.1 5555; then
      break
    fi
    sleep 0.5
  done

  if ! check_port 127.0.0.1 5555; then
    echo "backend failed to open 127.0.0.1:5555"
    kill "$BACKEND_PID" >/dev/null 2>&1 || true
    exit 1
  fi
fi

echo "starting frontend dev server"
cd "$FRONTEND_DIR"
npm run dev
