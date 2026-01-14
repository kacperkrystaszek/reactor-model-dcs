from enum import Enum


class MessageType(Enum):
    INIT = "INIT"
    START = "START"
    ACK = "ACK"
    STATUS = "STATUS"