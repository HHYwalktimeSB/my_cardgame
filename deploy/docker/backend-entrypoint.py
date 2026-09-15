#!/usr/bin/env python3
import json
import os


with open("/app/config.template.json", encoding="utf-8") as source:
    config = json.load(source)

database = config["db_clients"][0]
database["host"] = os.environ["POSTGRES_HOST"]
database["port"] = int(os.environ.get("POSTGRES_PORT", "5432"))
database["dbname"] = os.environ["POSTGRES_DB"]
database["user"] = os.environ["POSTGRES_USER"]
database["passwd"] = os.environ["POSTGRES_PASSWORD"]
config["app"]["number_of_threads"] = int(os.environ.get("CARD_GAME_THREADS", "2"))

with open("/app/run/config.json", "w", encoding="utf-8") as destination:
    json.dump(config, destination, ensure_ascii=False, indent=2)
    destination.write("\n")

os.execv("/app/card_game", ["/app/card_game"])
