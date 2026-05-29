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
from controller.gpc_config import T_BASE, EVENT_BASED, ALPHA, BETA

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
        self._clients_addr.clear()
        self._log_file = None
        self.csv_writer = None
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
                    print(f"[REGISTER] {client_id} connected from {addr}. Sending ACK with config.")
                self._clients_addr[client_id] = addr
                
                ack_msg = json.dumps({"type": MessageType.ACK.value, "config": self.config})
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
        print(f"TIMESTAMP  | Y1SP     Y2SP     | Y1       Y2       | U1       U2       | EVENT")
        self.init_log_file()
        zero_time = None
        breakLoop = False
        while True:
            if breakLoop:

                break
            data, addr = self._sock.recvfrom(4096)
            if zero_time is None:
                zero_time = time.perf_counter()
            ts = time.perf_counter() - zero_time
            if ts > 360:
                breakLoop = True
            if addr not in list(self._clients_addr.values()):
                continue
            try:
                msg = json.loads(data.decode())
                if msg.get("type") != MessageType.STATUS.value:
                    continue
                payload = msg.get("payload")
                if payload is None:
                    continue
                
                y1, y2 = payload.get("y1"), payload.get("y2")
                u1, u2 = payload.get("u1"), payload.get("u2")
                sp_y1, sp_y2 = payload.get("sp_y1"), payload.get("sp_y2")
                is_event_y1 = payload.get("is_event_y1", False)
                is_event_y2 = payload.get("is_event_y2", False)
                beta_y1 = payload.get("beta_y1", 0.0)
                beta_y2 = payload.get("beta_y2", 0.0)
                hmax_y1 = payload.get("hmax_y1", 0)
                hmax_y2 = payload.get("hmax_y2", 0)

                tolerance = 0.05
                c_rst = "\033[0m"
                c_y1 = "\033[92m" if abs(y1 - sp_y1) < self.config.get("BETA_Y1", tolerance) else "\033[91m"
                c_y2 = "\033[92m" if abs(y2 - sp_y2) < self.config.get("BETA_Y2", tolerance) else "\033[91m"
                ev1_str = "E1" if is_event_y1 else "  "
                ev2_str = "E2" if is_event_y2 else "  "
                print(f"{ts:<10} | {sp_y1:<8.4f} {sp_y2:<8.4f} | {c_y1}{y1:<8.4f}{c_rst} {c_y2}{y2:<8.4f}{c_rst} | {u1:<8.4f} {u2:<8.4f} | {ev1_str} {ev2_str}")
                self.csv_writer.writerow([
                    ts,
                    sp_y1,
                    sp_y2,
                    y1,
                    y2,
                    u1,
                    u2,
                    int(is_event_y1),
                    int(is_event_y2),
                    beta_y1,
                    beta_y2,
                    hmax_y1,
                    hmax_y2
                ])
                self._log_file.flush()
            except Exception as e:
                self._log_file.close()
                pass

    def init_log_file(self):
        timestamp = datetime.datetime.now()
        timestamp_str = timestamp.strftime("%d-%m-%Y_%H-%M-%S")
        main_part = f"data_{timestamp_str}_BETA-{BETA}_ALPHA-{ALPHA}_EB_{EVENT_BASED}" 
        filename = f"{main_part}.csv"
        path = os.path.join("logger", "logs", main_part)
        os.makedirs(path, exist_ok=True)
        self._log_file = open(os.path.join(path, filename), "w", encoding="utf-8", newline="")
        self.csv_writer = csv.writer(self._log_file, delimiter=";")
        self.csv_writer.writerow([
            "Timestamp",
            "SP_Y1",
            "SP_Y2",
            "Y1",
            "Y2",
            "U1",
            "U2",
            "Is_Event_Y1",
            "Is_Event_Y2",
            "BETA_Y1",
            "BETA_Y2",
            "HMAX_Y1",
            "HMAX_Y2"
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
    for ebValue in [False, True]:
        for alphaValue in [1, 100, 250, 500]:
            loaded_config['EVENT_BASED'] = ebValue
            loaded_config['ALPHA'] = alphaValue
            if not ebValue:
                main(loaded_config)
            else:
                for betaValue in [0.005, 0.01, 0.015]:
                    loaded_config['BETA_Y1'] = betaValue
                    loaded_config['BETA_Y2'] = betaValue
                    main(loaded_config)