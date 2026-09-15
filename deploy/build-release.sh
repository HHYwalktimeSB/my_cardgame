#!/usr/bin/env bash
set -euo pipefail

repo_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${BUILD_DIR:-"$repo_dir/.deploy/build"}
package_dir=${PACKAGE_DIR:-"$repo_dir/.deploy/package"}
build_jobs=${BUILD_JOBS:-$(nproc)}

cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel "$build_jobs" \
    --target card_game card_game_test
"$build_dir/test/card_game_test"

npm ci --prefix "$repo_dir/frontend"
npm run build --prefix "$repo_dir/frontend"

rm -rf "$package_dir"
install -d "$package_dir/bin" "$package_dir/frontend"
install -m 0755 "$build_dir/card_game" "$package_dir/bin/card_game"
cp -a "$repo_dir/frontend/dist/." "$package_dir/frontend/"

printf 'Deployment package created at %s\n' "$package_dir"
