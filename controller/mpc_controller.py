import json
import os
import socket
import sys

import numpy as np
from controller.gpc_config import *

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from utils.MessageType import MessageType
from utils.States import States

class Controller:
    def __init__(self, config: dict[str, int|str], sock: socket.socket, controller_id: str):
        self.config = config
        self._sock = sock
        self.my_id = controller_id
        self.logger_port = config['LOGGER_PORT']
        self.logger_ip = config['LOGGER_IP']
        self._state = States.INIT

    def _create_init_msg(self) -> str:
        return json.dumps({"type": MessageType.INIT.value, "id": self.my_id})

    def calculate_free_response(self, y1_h: list, y2_h: list, u1_h: list, u2_h: list):
        ly1, ly2 = list(y1_h), list(y2_h)
        lu1, lu2 = list(u1_h), list(u2_h)
        
        current_u1, current_u2 = lu1[0], lu2[0]
        preds = []
        
        for _ in range(3):
            pred_y1 = (1.8629 * ly1[0] - 0.8669 * ly1[1] +
                    0.0420 * current_u1 - 0.0380 * lu1[1] + 
                    0.4758 * current_u2 - 0.4559 * lu2[1])
                    
            pred_y2 = (1.8695 * ly2[0] - 0.8737 * ly2[1] +
                    0.0582 * current_u1 - 0.0540 * lu1[1] +
                    0.1445 * current_u2 - 0.1361 * lu2[1])
            
            preds.extend([pred_y1, pred_y2])
            ly1 = [pred_y1, ly1[0]]
            ly2 = [pred_y2, ly2[0]]
            lu1 = [current_u1, lu1[0]]
            lu2 = [current_u2, lu2[0]]
            
        return np.array(preds)

    def perform_handshake(self) -> None:
        logger_addr = (self.logger_ip, self.logger_port)
        
        print(f"[{self.my_id.upper()}] Connecting to Logger...")
        
        self._sock.settimeout(1.0) # Timeout 1s
        
        while self._state != States.RUNNING:
            try:
                if self._state == States.INIT:
                    msg = self._create_init_msg()
                    self._sock.sendto(msg.encode(), logger_addr)
                    print(f"[{self.my_id.upper()}] Sent INIT...")
                    
                data, addr = self._sock.recvfrom(1024)
                if addr != logger_addr:
                    continue
                msg = json.loads(data.decode())
                mtype = msg.get("type")
                
                if self._state == States.INIT and mtype == MessageType.ACK.value:
                    print(f"[{self.my_id.upper()}] Received ACK. Standby.")
                    self._state = States.STANDBY
                    
                elif self._state == States.STANDBY and mtype == MessageType.START.value:
                    print(f"[{self.my_id.upper()}] Received START! Starting loop.")
                    self._sock.sendto(json.dumps({"type": "ACK"}).encode(), logger_addr)
                    self._state = States.RUNNING
                    
            except socket.timeout:
                pass

    def main_loop(self, y1_h: list, y2_h: list, u1_h: list, u2_h: list):
        FEED_ID = self.config.get("FEED_ID", "feed")
        COOLANT_ID = self.config.get("COOLANT_ID", "coolant")
        MODEL_IP = self.config.get("MODEL_IP", "127.0.0.1")
        MODEL_PORT = self.config.get("MODEL_PORT", 5001)
        
        while True:
            try:
                self._sock.settimeout(None) 
                data, _ = self._sock.recvfrom(1024)
                msg: dict = json.loads(data.decode())

                mtype = msg.get("type")
                if mtype != MessageType.STATUS.value:
                    continue
                
                payload = msg.get("payload")
                if payload is None:
                    continue
                
                y1, y2 = payload["y1"], payload["y2"]
                u1_curr, u2_curr = payload["u1"], payload["u2"]
                
                y1_h = [y1, y1_h[0]]
                y2_h = [y2, y2_h[0]]
                u1_h = [u1_curr, u1_h[0]]
                u2_h = [u2_curr, u2_h[0]]
                f_vec = self.calculate_free_response(y1_h, y2_h, u1_h, u2_h)
                
                w_vec = np.array([SP_Y1, SP_Y2] * 3)
                delta_u = K_mpc @ (w_vec - f_vec)

                if self.my_id == FEED_ID:
                    new_u = u1_curr + delta_u[0]
                    cmd = json.dumps({"u1": new_u})
                elif self.my_id == COOLANT_ID:
                    new_u = u2_curr + delta_u[1]
                    cmd = json.dumps({"u2": new_u})
                else:
                    continue

                print(f"[{self.my_id.upper()}] Sending value: {cmd}")
                self._sock.sendto(cmd.encode(), (MODEL_IP, MODEL_PORT))
            except Exception as e:
                raise
