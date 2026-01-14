import os
import socket
import sys
from controller.mpc_controller import Controller

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.utils import load_config


def main(config: dict[str, str|int]):
    PORT: int = config["COOLANT_CONTROLLER_PORT"]
    MY_ID: str = config["COOLANT_ID"]
    
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT))

    controller = Controller(config, sock, MY_ID)
    controller.perform_handshake()
    
    print(f"[{MY_ID.upper()}] Control Loop Started.")
    
    y1_h = [0.0, 0.0]
    y2_h = [0.0, 0.0]
    u1_h = [0.0, 0.0]
    u2_h = [0.0, 0.0]

    controller.main_loop(y1_h, y2_h, u1_h, u2_h)


if __name__ == "__main__":
    loaded_config = load_config()
    main(loaded_config)