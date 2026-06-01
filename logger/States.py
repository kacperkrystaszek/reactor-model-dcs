from enum import Enum


class States(Enum):
    INIT = "INIT"
    WAIT_START = "WAIT_START"
    RUNNING = "RUNNING"