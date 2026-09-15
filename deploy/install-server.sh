#!/usr/bin/env bash
set -euo pipefail

if (( EUID != 0 )); then
    echo "run this script as root" >&2
    exit 1
fi

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_dir=$(cd "$script_dir/.." && pwd)
package_dir=${PACKAGE_DIR:-"$repo_dir/.deploy/package"}
install_root=${INSTALL_ROOT:-/opt/card-game}
config_dir=${CONFIG_DIR:-/etc/card-game}
service_user=${SERVICE_USER:-card-game}
backend_port=${BACKEND_PORT:-5555}
http_port=${HTTP_PORT:-80}
server_name=${SERVER_NAME:-_}
threads=${CARD_GAME_THREADS:-2}
db_host=${DB_HOST:-127.0.0.1}
db_port=${DB_PORT:-5432}
db_name=${DB_NAME:-cardgame_db}
db_user=${DB_USER:-cardgame_user}

for command_name in nginx python3 systemctl; do
    command -v "$command_name" >/dev/null || {
        echo "missing required command: $command_name" >&2
        exit 1
    }
done
[[ -x "$package_dir/bin/card_game" ]] || {
    echo "missing package; run deploy/build-release.sh first" >&2
    exit 1
}
[[ -d "$package_dir/frontend" ]] || {
    echo "missing frontend package" >&2
    exit 1
}

if ! id "$service_user" >/dev/null 2>&1; then
    useradd --system --home-dir "$install_root" --shell /usr/sbin/nologin \
        "$service_user"
fi

install -d -o root -g root -m 0755 "$install_root/bin" "$install_root/frontend"
install -d -o "$service_user" -g "$service_user" -m 0750 \
    "$install_root/run" /var/log/card-game
install -d -o root -g "$service_user" -m 0750 "$config_dir"
install -m 0755 "$package_dir/bin/card_game" "$install_root/bin/card_game"
rm -rf "$install_root/frontend"
install -d -o root -g root -m 0755 "$install_root/frontend"
cp -a "$package_dir/frontend/." "$install_root/frontend/"

config_file="$config_dir/config.json"
if [[ ! -f "$config_file" || ${OVERWRITE_CONFIG:-0} == 1 ]]; then
    : "${DB_PASSWORD:?set DB_PASSWORD before the first installation}"
    export INSTALL_ROOT_VALUE="$install_root"
    export THREADS_VALUE="$threads"
    export DB_HOST_VALUE="$db_host"
    export DB_PORT_VALUE="$db_port"
    export DB_NAME_VALUE="$db_name"
    export DB_USER_VALUE="$db_user"
    export DB_PASSWORD_VALUE="$DB_PASSWORD"
    python3 - "$script_dir/config.server.json" "$config_file" <<'PY'
import json
import os
import sys

with open(sys.argv[1], encoding="utf-8") as source:
    config = json.load(source)
database = config["db_clients"][0]
database["host"] = os.environ["DB_HOST_VALUE"]
database["port"] = int(os.environ["DB_PORT_VALUE"])
database["dbname"] = os.environ["DB_NAME_VALUE"]
database["user"] = os.environ["DB_USER_VALUE"]
database["passwd"] = os.environ["DB_PASSWORD_VALUE"]
config["app"]["number_of_threads"] = int(os.environ["THREADS_VALUE"])
config["app"]["document_root"] = os.environ["INSTALL_ROOT_VALUE"] + "/frontend"
with open(sys.argv[2], "w", encoding="utf-8") as destination:
    json.dump(config, destination, ensure_ascii=False, indent=2)
    destination.write("\n")
PY
    chown root:"$service_user" "$config_file"
    chmod 0640 "$config_file"
fi

cat > "$config_dir/card-game.env" <<EOF
CARD_GAME_LISTEN_ADDRESS=127.0.0.1
CARD_GAME_LISTEN_PORT=$backend_port
CARD_GAME_CONFIG=$config_file
EOF
chown root:"$service_user" "$config_dir/card-game.env"
chmod 0640 "$config_dir/card-game.env"

render_template() {
    local source=$1
    local destination=$2
    SOURCE_VALUE=$source DESTINATION_VALUE=$destination \
    INSTALL_ROOT_VALUE=$install_root CONFIG_DIR_VALUE=$config_dir \
    SERVICE_USER_VALUE=$service_user BACKEND_PORT_VALUE=$backend_port \
    HTTP_PORT_VALUE=$http_port SERVER_NAME_VALUE=$server_name \
    python3 - <<'PY'
import os

with open(os.environ["SOURCE_VALUE"], encoding="utf-8") as source:
    text = source.read()
replacements = {
    "__INSTALL_ROOT__": os.environ["INSTALL_ROOT_VALUE"],
    "__CONFIG_DIR__": os.environ["CONFIG_DIR_VALUE"],
    "__SERVICE_USER__": os.environ["SERVICE_USER_VALUE"],
    "__BACKEND_PORT__": os.environ["BACKEND_PORT_VALUE"],
    "__HTTP_PORT__": os.environ["HTTP_PORT_VALUE"],
    "__SERVER_NAME__": os.environ["SERVER_NAME_VALUE"],
}
for key, value in replacements.items():
    text = text.replace(key, value)
with open(os.environ["DESTINATION_VALUE"], "w", encoding="utf-8") as destination:
    destination.write(text)
PY
}

render_template "$script_dir/card-game.service.template" \
    /etc/systemd/system/card-game.service
render_template "$script_dir/nginx.conf.template" \
    /etc/nginx/conf.d/card-game.conf
rm -f /etc/nginx/sites-enabled/default

systemctl daemon-reload
systemctl enable --now card-game.service
nginx -t
systemctl reload nginx

echo "card-game deployed: http://$server_name:$http_port/"
