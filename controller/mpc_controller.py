import datetime
import json
import os
import socket
import sys

import numpy as np
from scipy.interpolate import lagrange

from controller.gpc_config import *
import numpy as np
from utils.MessageType import MessageType
from utils.States import States

sys.path.append(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))



class Controller:
    A1_Y1, A2_Y1 = 1.8629, -0.8669
    B0_U1_Y1, B1_U1_Y1 = 0.0420, -0.0380
    B0_U2_Y1, B1_U2_Y1 = 0.4758, -0.4559
    
    A1_Y2, A2_Y2 = 1.8695, -0.8737
    B0_U1_Y2, B1_U1_Y2 = 0.0582, -0.0540
    B0_U2_Y2, B1_U2_Y2 = 0.1445, -0.1361

    def __init__(self, config: dict[str, int|str], sock: socket.socket, controller_id: str):
        self.config = config
        self._sock = sock
        self.my_id = controller_id
        self.logger_port = config['LOGGER_PORT']
        self.logger_ip = config['LOGGER_IP']
        self._state = States.INIT
        self.k_mpc_cache: dict[int, np.ndarray] = {}
        self.history_len = 4
        self.history_t = []
        self.history_y1 = []
        self.history_y2 = []
        self.current_t = 0.0

    def _create_init_msg(self) -> str:
        return json.dumps({"type": MessageType.INIT.value, "id": self.my_id})

    def _step_response(self, u1_step: float, u2_step: float, n_steps: int) -> tuple[list, list]:
        y1, y1_prev = 0.0, 0.0
        y2, y2_prev = 0.0, 0.0
        resp_y1, resp_y2 = [], []
        
        for k in range(n_steps):
            u1_now = u1_step
            u1_old = 0.0 if k == 0 else u1_step
            u2_now = u2_step
            u2_old = 0.0 if k == 0 else u2_step
            
            y1_new = (self.A1_Y1 * y1 + self.A2_Y1 * y1_prev +
                      self.B0_U1_Y1 * u1_now + self.B1_U1_Y1 * u1_old +
                      self.B0_U2_Y1 * u2_now + self.B1_U2_Y1 * u2_old)
            y2_new = (self.A1_Y2 * y2 + self.A2_Y2 * y2_prev +
                      self.B0_U1_Y2 * u1_now + self.B1_U1_Y2 * u1_old +
                      self.B0_U2_Y2 * u2_now + self.B1_U2_Y2 * u2_old)
            
            resp_y1.append(y1_new)
            resp_y2.append(y2_new)
            y1_prev, y1 = y1, y1_new
            y2_prev, y2 = y2, y2_new
            
        return resp_y1, resp_y2

    def get_K_mpc(self, psc: int) -> np.ndarray:
        if psc in self.k_mpc_cache:
            return self.k_mpc_cache[psc]

        n_steps = 3 * psc
        g_11, g_21 = self._step_response(1.0, 0.0, n_steps)
        g_12, g_22 = self._step_response(0.0, 1.0, n_steps)
        
        s1, s2, s3 = psc - 1, 2 * psc - 1, 3 * psc - 1
        
        G = np.array([
            [g_11[s1], g_12[s1], 0.0,        0.0],
            [g_21[s1], g_22[s1], 0.0,        0.0],
            [g_11[s2], g_12[s2], g_11[s1],   g_12[s1]],
            [g_21[s2], g_22[s2], g_21[s1],   g_22[s1]],
            [g_11[s3], g_12[s3], g_11[s2],   g_12[s2]],
            [g_21[s3], g_22[s3], g_21[s2],   g_22[s2]],
        ])
        
        HTH = G.T @ G + LAMBDA * np.eye(4)
        K = np.linalg.inv(HTH) @ G.T
        self.k_mpc_cache[psc] = K
        print(f"[{self.my_id.upper()}] Multi-rate K_mpc cached for psc={psc}.")
        return K

    def resample_states_lagrange(self, T_f):
        if len(self.history_t) == 1:
            return self.history_y1[-1], self.history_y1[-1], \
                   self.history_y2[-1], self.history_y2[-1]
                   
        if len(self.history_t) == 2:
            return self.history_y1[-1], self.history_y1[-2], \
                   self.history_y2[-1], self.history_y2[-2]

        if abs(T_f - T_BASE) < 0.01:
            return self.history_y1[-1], self.history_y1[-2], \
                   self.history_y2[-1], self.history_y2[-2]

        t_curr_real = self.history_t[-1]
        normalized_t = np.array(self.history_t) - t_curr_real
        target_times = [0.0, -T_BASE]

        poly_y1 = lagrange(normalized_t, self.history_y1)
        poly_y2 = lagrange(normalized_t, self.history_y2)

        y1_resampled = poly_y1(target_times)
        y2_resampled = poly_y2(target_times)

        min_y1, max_y1 = min(self.history_y1), max(self.history_y1)
        min_y2, max_y2 = min(self.history_y2), max(self.history_y2)

        y1_prev = np.clip(y1_resampled[1], min_y1, max_y1)
        y2_prev = np.clip(y2_resampled[1], min_y2, max_y2)

        return self.history_y1[-1], y1_prev, self.history_y2[-1], y2_prev

    def calculate_free_response(self, y1_h: list, y2_h: list, u1_h: list, u2_h: list, psc: int) -> np.ndarray:
        ly1, ly2 = list(y1_h), list(y2_h)
        lu1, lu2 = list(u1_h), list(u2_h)
        
        current_u1, current_u2 = lu1[0], lu2[0]
        preds = []
        
        for k in range(1, 3 * psc + 1):
            pred_y1 = (self.A1_Y1 * ly1[0] + self.A2_Y1 * ly1[1] +
                       self.B0_U1_Y1 * current_u1 + self.B1_U1_Y1 * lu1[1] +
                       self.B0_U2_Y1 * current_u2 + self.B1_U2_Y1 * lu2[1])
            pred_y2 = (self.A1_Y2 * ly2[0] + self.A2_Y2 * ly2[1] +
                       self.B0_U1_Y2 * current_u1 + self.B1_U1_Y2 * lu1[1] +
                       self.B0_U2_Y2 * current_u2 + self.B1_U2_Y2 * lu2[1])
            
            if k % psc == 0:
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
                psc = payload.get("psc", 1)
                
                T_f = psc * T_BASE
                self.current_t += T_f
                self.history_t.append(self.current_t)
                self.history_y1.append(y1)
                self.history_y2.append(y2)

                if len(self.history_t) > self.history_len:
                    self.history_t.pop(0)
                    self.history_y1.pop(0)
                    self.history_y2.pop(0)

                y1, y1_prev, y2, y2_prev = self.resample_states_lagrange(T_f)
                
                if psc > 1:
                    u1_prev = u1_curr
                    u2_prev = u2_curr
                else:
                    u1_prev = u1_h[0]
                    u2_prev = u2_h[0]

                y1_h = [y1, y1_prev]
                y2_h = [y2, y2_prev]
                u1_h = [u1_curr, u1_prev]
                u2_h = [u2_curr, u2_prev]

                f_vec = self.calculate_free_response(y1_h, y2_h, u1_h, u2_h, psc)
                
                w_vec = np.array([SP_Y1, SP_Y2] * 3)
                K_mpc_psc = self.get_K_mpc(psc)
                delta_u = K_mpc_psc @ (w_vec - f_vec)

                if self.my_id == FEED_ID:
                    new_u = u1_curr + delta_u[0]
                    cmd = json.dumps({"u1": new_u})
                elif self.my_id == COOLANT_ID:
                    new_u = u2_curr + delta_u[1]
                    cmd = json.dumps({"u2": new_u})
                else:
                    continue
                ts = datetime.datetime.now().strftime('%H:%M:%S')
                print(f"{ts} [{self.my_id.upper()}] Event received. Sending value: {cmd}")
                self._sock.sendto(cmd.encode(), (MODEL_IP, MODEL_PORT))

            except Exception as e:
                raise
