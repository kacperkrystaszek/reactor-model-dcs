import csv
import os
import socket
import json
import subprocess
import sys
import time
import threading
import datetime

from States import States

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.MessageType import MessageType
from utils.utils import load_config

class Logger:
    def __init__(self, config: dict):
        self.config = config
        self.EXPECTED_CLIENTS = [('192.168.70.3', 5003), ('192.168.70.2', 5002), ('192.168.70.5', 5001)]
        self._clients = {}
        self._sock = self.start_socket()
        self._log_file = None
        self.csv_writer = None
        self._init_phase_result = None
        
    def start_socket(self):
        port = self.config['LOGGER_PORT']
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("0.0.0.0", port))
        sock.settimeout(0.2)
        print(f"--- SUPERVISOR LOGGER STARTED on port {port} ---")
        return sock

    def init_phase(self):
        self._sock.settimeout(0.2)
        self._clients.clear()
        self._log_file = None
        self.csv_writer = None
        
        attempts = 0

        while len(self._clients) < len(self.EXPECTED_CLIENTS):
            try:
                data, addr = self._sock.recvfrom(1024)
                msg = json.loads(data.decode())

                if msg.get("type") != MessageType.INIT.value:
                    attempts += 1
                    continue
                
                if addr not in self._clients:
                    print(f"[INIT_PHASE] Connection from {addr}. Sending ACK with config.")
                self._clients[addr] = States.INIT
                
                ack_msg = json.dumps({"type": MessageType.ACK.value, "config": self.config})
                self._sock.sendto(ack_msg.encode(), addr)

            except socket.timeout:
                attempts += 1

            if attempts > 500:
                print("[INIT_PHASE] Reached attempts maximum. Retrying system...")
                self._init_phase_result = False
                return
                
        self._init_phase_result = True
        return

    def post_init_phase(self):
        attempts = 0

        while not all([True if val == States.WAIT_START else False for val in self._clients.values()]):
            try:
                data, addr = self._sock.recvfrom(1024)
                msg = json.loads(data.decode())

                if msg.get("type") == MessageType.INIT.value:
                    ack_msg = json.dumps({"type": MessageType.ACK.value, "config": self.config})
                    self._sock.sendto(ack_msg.encode(), addr)
                    continue

                if msg.get("type") != MessageType.ACK.value:
                    attempts += 1
                    continue

                self._clients[addr] = States.WAIT_START
                print(f"[POST_INIT_PHASE] {addr} is in WAIT_START state.")

            except socket.timeout:
                attempts += 1
                
            if attempts > 500:
                print("[POST_INIT_PHASE] Reached attempts maximum. Retrying system...")
                return False

        return True
            
    def start_phase(self):
        self._sock.settimeout(0.2)
        for target_addr in self.EXPECTED_CLIENTS:
            print(f"[START_PHASE] Sending START to {target_addr}...")
            attempts = 0
            while self._clients[target_addr] != States.RUNNING:
                start_msg = json.dumps({"type": MessageType.START.value})
                self._sock.sendto(start_msg.encode(), target_addr)
                attempts += 1

                try:
                    data, addr = self._sock.recvfrom(1024)
                    if addr != target_addr:
                        continue
                    msg = json.loads(data)
                    if msg.get("type") == MessageType.ACK.value:
                        self._clients[target_addr] = States.RUNNING
                        print(f"[START_PHASE] {target_addr} is in RUNNING state.")
                        break
                except socket.timeout:
                    print(f"[START_PHASE] {target_addr} not responding. Attempt: {attempts}")
                
                if attempts > 500:
                    print(f"[START_PHASE] {target_addr} definitely not responding. Restarting system...")
                    return False
        return True

    def monitoring_phase(self):
        print(f"TIMESTAMP  | Y1SP     Y2SP     | Y1       Y2       | U1       U2       | EVENT")
        zero_time = None
        breakLoop = False
        lastMoment = 0
        while True:
            if breakLoop:
                self._log_file.close()
                break
            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                filepath = self._log_file.name
                self._log_file.close()
                return False
            if zero_time is None:
                zero_time = time.perf_counter()
            ts = time.perf_counter() - zero_time
            if ts > 360:
                breakLoop = True
            if addr not in list(self._clients):
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
                if lastMoment == 0 or ts - lastMoment > 10:
                    print(f"{ts:<3} | {sp_y1:<8.4f} {sp_y2:<8.4f} | {c_y1}{y1:<8.4f}{c_rst} {c_y2}{y2:<8.4f}{c_rst} | {u1:<8.4f} {u2:<8.4f} | {ev1_str} {ev2_str}")
                    lastMoment += 10
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
        return True

    def init_log_file(self, scenario, rp1Load, rp4Load, beta, alpha, event_based):
        timestamp = datetime.datetime.now()
        timestamp_str = timestamp.strftime("%d-%m-%Y_%H-%M-%S")
        main_part = f"data_{timestamp_str}_BETA-{beta}_ALPHA-{alpha}_EB-{event_based}" 
        filename = f"{main_part}.csv"
        logpathList = ["logger", "logs", scenario]
        if rp1Load and rp4Load:
            logpathList.append("BOTHLOAD")
        elif rp1Load:
            logpathList.append("RP1LOAD")
        logpathList.append(f"ALPHA-{alpha}")
        path = os.path.join(*logpathList)
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

active_processes_controllers = {}

def ssh_run(ip, cmd):
    subprocess.run(["ssh", f"pi@{ip}", cmd], capture_output=True)

def run_controller(ip):
    print(f"[{ip}] RUN CONTROLLER")

    ssh_run(ip, "sudo pkill -9 -f ./controller")

    proc = subprocess.Popen(
        ["ssh", f"pi@{ip}", "cd ~/dcs/ && sudo ./controller"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    
    active_processes_controllers[ip] = proc
    print(f"[{ip}] FINISHED RUN CONTROLLER")

def apply_cpu_load(ip):
    ssh_run(ip, "sudo killall -9 stress-ng")
    cores = "1" if ip.endswith("2") else "4"

    proc = subprocess.Popen(
        ["ssh", f"pi@{ip}", f"sudo stress-ng --cpu {cores} --cpu-method matrixprod --cpu-load 80"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )
    active_processes_controllers[f"{ip}_stress"] = proc

def apply_network_load(ip, scenario):
    print(f"[{ip}] RUN NETWORK LOAD")

    ssh_run(ip, f"sudo tc qdisc del dev eth0 root 2>/dev/null")

    base_cmd = f"sudo tc qdisc add dev eth0 root netem"

    if scenario == "CONSTANTDELAY":
        ssh_run(ip, f"{base_cmd} delay 30ms")
        print(f"[{ip}] END RUN NETWORK LOAD")
        return

    if scenario == "VARIABLEDELAY":
        ssh_run(ip, f"{base_cmd} delay 50ms 20ms distribution normal")
        print(f"[{ip}] END RUN NETWORK LOAD")
        return
    
    if scenario == "PACKETLOSS":
        ssh_run(ip, f"{base_cmd} loss 5%")
        print(f"[{ip}] END RUN NETWORK LOAD")
        return
    
    if scenario == "COMBINEDNETWORK" or scenario == "COMBINEDALL":
        ssh_run(ip, f"{base_cmd} delay 50ms 20ms loss 5%")
        print(f"[{ip}] END RUN NETWORK LOAD")
        return

def reset_node(ip):
    print(f"[{ip}] RUN RESET")
    
    ssh_run(ip, "sudo tc qdisc del dev eth0 root 2>/dev/null")

    ssh_run(ip, "sudo pkill -9 -f ./controller")
    
    ssh_run(ip, "sudo killall -9 stress-ng")

    if ip in active_processes_controllers:
        try:
            active_processes_controllers[ip].terminate()
        except Exception:
            pass
            
    if f"{ip}_stress" in active_processes_controllers:
        try:
            active_processes_controllers[f"{ip}_stress"].terminate()
        except Exception:
            pass

    print(f"[{ip}] END RESET")

def main(logger: Logger, rpi1Ip, rpi4Ip, firstRun, scenario, rp1Load, rp4Load, beta, alpha, event_based):
    print(f"RUNNING SIM FOR: SCENARIO-{scenario} RP1LOAD-{rp1Load} RP4LOAD-{rp4Load} EB-{logger.config['EVENT_BASED']} ALPHA-{logger.config['ALPHA']} BETA-{logger.config['BETA_Y1']}")
    print("Waiting for components to initialize...")

    clear_buffer(logger)

    init_thread = threading.Thread(target=logger.init_phase)
    init_thread.start()

    if firstRun:
        time.sleep(2)
        run_controller(rpi1Ip)
        run_controller(rpi4Ip)
        send_restart(logger, ("192.168.70.5", 5001))

    init_thread.join()
    if not logger._init_phase_result:
        return False
    
    if not logger.post_init_phase():
        return False

    print("\nAll components are READY (Standby Mode).")
    print("Starting sequence in 2 seconds...")
    time.sleep(2)
    if not logger.start_phase():
        return False

    if rp1Load:
        if scenario == "CPULOAD" or scenario == "COMBINEDALL":
            apply_cpu_load(RP1IP)
        if scenario != "STANDARD" and scenario != "CPULOAD":
            apply_network_load(RP1IP, scenario)
    if rp4Load:
        if scenario == "CPULOAD" or scenario == "COMBINEDALL":
            apply_cpu_load(RP4IP)
        if scenario != "STANDARD" and scenario != "CPULOAD":
            apply_network_load(RP4IP, scenario)

    logger._sock.settimeout(20)
    print("\n--- SYSTEM RUNNING - MONITORING MODE ---")
    logger.init_log_file(scenario, rp1Load, rp4Load, beta, alpha, event_based)
    if not logger.monitoring_phase():
        return False

    for addr in logger._clients.keys():
        send_restart(logger, addr)

    return True

def send_restart(logger, addr):
    restart_msg = json.dumps({"type": MessageType.RESTART.value}).encode()
    for _ in range(10): 
        try:
            logger._sock.sendto(restart_msg, addr)
        except Exception as e:
            print(f"[SEND_RESTART] Nie udało się wysłać RESTART do {addr}: {e}")
        time.sleep(0.05)

def clear_buffer(logger: Logger):
    logger._sock.setblocking(False)
    while True:
        try:
            logger._sock.recvfrom(4096)
        except Exception:
            break
    logger._sock.settimeout(0.2)

def run_investigation(logger, scenario, rp1Load, rp4Load, rpi1Ip, rpi4Ip):
    firstRun = True
    for ebValue in [False, True]:
    # for ebValue in [True]:
        for alphaValue in [1, 250, 500, 900]:
        # for alphaValue in [500, 900]:
            logger.config['EVENT_BASED'] = ebValue
            logger.config['ALPHA'] = alphaValue
            if not ebValue:
                while not main(logger, rpi1Ip, rpi4Ip, firstRun, scenario, rp1Load, rp4Load, logger.config['BETA_Y1'], logger.config['ALPHA'], logger.config['EVENT_BASED']):
                    print("[RUN_INVESTIGATION] Timeout! Performing hard reset...")
                    reset_node(rpi1Ip)
                    reset_node(rpi4Ip)
                    clear_buffer(logger)
                    send_restart(logger, ('192.168.70.5', 5001))
                    firstRun = True
                firstRun = False
            else:
                for betaValue in [0.005, 0.01, 0.015]:
                    logger.config['BETA_Y1'] = betaValue
                    logger.config['BETA_Y2'] = betaValue
                    while not main(logger, rpi1Ip, rpi4Ip, firstRun, scenario, rp1Load, rp4Load, logger.config['BETA_Y1'], logger.config['ALPHA'], logger.config['EVENT_BASED']):
                        print("[RUN_INVESTIGATION] Timeout! Performing hard reset...")
                        reset_node(rpi1Ip)
                        reset_node(rpi4Ip)
                        clear_buffer(logger)
                        send_restart(logger, ('192.168.70.5', 5001))
                        firstRun = True
                    firstRun = False
    
    # main(logger, rpi1Ip, rpi4Ip, firstRun, scenario, logger.config['BETA_Y1'], logger.config['ALPHA'], logger.config['EVENT_BASED'])

    reset_node(rpi1Ip)
    reset_node(rpi4Ip)

if __name__ == "__main__":
    loaded_config = load_config()
    logger = Logger(loaded_config)

    RP1IP = "192.168.70.2"
    RP4IP = "192.168.70.3"

    reset_node(RP1IP)
    reset_node(RP4IP)

    scenario = "STANDARD"
    run_investigation(logger, scenario, None, None, RP1IP, RP4IP)

    scenario = "CPULOAD"
    for rpi1Load in [True]:
        for rpi4Load in [False, True]:
            run_investigation(logger, scenario, rpi1Load, rpi4Load, RP1IP, RP4IP)

    scenario = "CONSTANTDELAY"
    for rpi1Load in [True]:
        for rpi4Load in [False, True]:
            if rpi1Load:
                apply_network_load(RP1IP, scenario)
            if rpi4Load:
                apply_network_load(RP4IP, scenario)
            run_investigation(logger, scenario, rpi1Load, rpi4Load, RP1IP, RP4IP)

    scenario = "VARIABLEDELAY"
    for rpi1Load in [True]:
        for rpi4Load in [False, True]:
            if rpi1Load:
                apply_network_load(RP1IP, scenario)
            if rpi4Load:
                apply_network_load(RP4IP, scenario)
            run_investigation(logger, scenario, rpi1Load, rpi4Load, RP1IP, RP4IP)

    scenario = "PACKETLOSS"
    for rpi1Load in [True]:
        for rpi4Load in [True]:
            if rpi1Load:
                apply_network_load(RP1IP, scenario)
            if rpi4Load:
                apply_network_load(RP4IP, scenario)
            run_investigation(logger, scenario, rpi1Load, rpi4Load, RP1IP, RP4IP)

    scenario = "COMBINEDNETWORK"
    apply_network_load(RP1IP, scenario)
    apply_network_load(RP4IP, scenario)
    run_investigation(logger, scenario, True, True, RP1IP, RP4IP)

    scenario = "COMBINEDALL"
    apply_cpu_load(RP1IP)
    apply_cpu_load(RP4IP)
    apply_network_load(RP1IP, scenario)
    apply_network_load(RP4IP, scenario)
    run_investigation(logger, scenario, True, True, RP1IP, RP4IP)