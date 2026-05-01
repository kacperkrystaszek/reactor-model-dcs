import json

LAMBDA = 0.5
SP_Y1 = 0.5
SP_Y2 = 0.3
T_BASE = 1.8

with open("config.json", "r") as f:
    config = json.load(f)
    SP_Y1 = config["SP_Y1"]
    SP_Y2 = config["SP_Y2"]
    T_BASE = config['T_BASE'] / 1000
