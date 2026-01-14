import csv
import os
import socket
import json
import sys
import time
import datetime

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.MessageType import MessageType
from utils.utils import load_config
from controller.gpc_config import SP_Y1, SP_Y2

class Logger:
    def __init__(self, config: dict):
        self.config = config
        self.EXPECTED_CLIENTS = [value for key, value in config.items() if key.endswith("ID")]
        self._clients_addr = {}
        self._sock = self.start_socket()
        self._log_file = None
        self.csv_writer = None
        
    def start_socket(self):
        port = self.config['LOGGER_PORT']
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("0.0.0.0", port))
        sock.settimeout(0.2)
        print(f"--- SUPERVISOR LOGGER STARTED on port {port} ---")
        return sock

    def init_phase(self):
        while len(self._clients_addr) < len(self.EXPECTED_CLIENTS):
            try:
                data, addr = self._sock.recvfrom(1024)
                msg = json.loads(data.decode())
                
                if msg.get("type") != MessageType.INIT.value:
                    continue
                
                client_id = msg.get("id")
                if client_id not in self.EXPECTED_CLIENTS:
                    continue
                
                if client_id not in self._clients_addr:
                    print(f"[REGISTER] {client_id} connected from {addr}. Sending ACK.")
                self._clients_addr[client_id] = addr
                
                ack_msg = json.dumps({"type": MessageType.ACK.value})
                self._sock.sendto(ack_msg.encode(), addr)
            except socket.timeout:
                pass
            except Exception as e:
                print(f"Error during handshake: {e}")

    def start_phase(self):
        for target_id in self.EXPECTED_CLIENTS:
            target_addr = self._clients_addr[target_id]
            print(f"[SEQUENCE] Sending START to {target_id}...")
            
            confirmed = False
            while not confirmed:
                start_msg = json.dumps({"type": MessageType.START.value})
                self._sock.sendto(start_msg.encode(), target_addr)
                
                try:
                    start_time = time.time()
                    while time.time() - start_time < 1.0:
                        data, addr = self._sock.recvfrom(1024)
                        if addr != target_addr:
                            continue
                        msg = json.loads(data.decode())
                        if msg.get("type") != MessageType.ACK.value:
                            continue
                        print(f"[SEQUENCE] {target_id} CONFIRMED START.")
                        confirmed = True
                        break
                except socket.timeout:
                    print(f"[RETRY] No ACK from {target_id}, retrying...")

        time.sleep(0.5)

    def monitoring_phase(self):
        print(f"TIMESTAMP  | Y1      Y2       | U1       U2")
        self.init_log_file()
        while True:
            data, addr = self._sock.recvfrom(4096)
            if addr not in list(self._clients_addr.values()):
                continue
            try:
                msg = json.loads(data.decode())
                if msg.get("type") != MessageType.STATUS.value:
                    continue
                payload = msg.get("payload")
                if payload is None:
                    continue
                
                ts = datetime.datetime.now().strftime('%H:%M:%S')
                y1, y2 = payload.get("y1"), payload.get("y2")
                u1, u2 = payload.get("u1"), payload.get("u2")

                c_rst = "\033[0m"
                c_y1 = "\033[92m" if abs(y1 - 0.5) < 0.05 else "\033[91m"
                print(f"{ts:<10} | {c_y1}{y1:<8.4f}{c_rst} {y2:<8.4f} | {u1:<8.4f} {u2:<8.4f}")
                self.csv_writer.writerow([
                    ts,
                    SP_Y1,
                    SP_Y2,
                    y1,
                    y2,
                    u1,
                    u2
                ])
                self._log_file.flush()
            except Exception as e:
                self._log_file.close()
                pass

    def init_log_file(self):
        timestamp = datetime.datetime.now()
        timestamp_str = timestamp.strftime("%d-%m-%Y_%H-%M-%S")
        filename = f"data_{timestamp_str}.csv"
        self._log_file = open(os.path.join("logger", "logs", filename), "w", encoding="utf-8", newline="")
        self.csv_writer = csv.writer(self._log_file, delimiter=";")
        self.csv_writer.writerow([
            "Timestamp",
            "SP_Y1",
            "SP_Y2",
            "Y1",
            "Y2",
            "U1",
            "U2"
        ])
        
def main(config: dict):
    logger = Logger(config)
    
    print("Waiting for components to Initialize...")
    logger.init_phase()

    print("\nAll components are READY (Standby Mode).")
    print("Starting sequence in 2 seconds...")
    time.sleep(2)
    logger.start_phase()

    logger._sock.settimeout(None)
    print("\n--- SYSTEM RUNNING - MONITORING MODE ---")
    logger.monitoring_phase()

if __name__ == "__main__":
    loaded_config = load_config()
    main(loaded_config)