import json


def load_config() -> dict[str, str | int]:
    with open("config.json", "r", encoding="utf-8") as config_file:
        return json.load(config_file)
