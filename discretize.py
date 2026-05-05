# import control as ctrl
import numpy as np

# # Model ciągły z artykułu (Stirred Tank Reactor)
# s = ctrl.TransferFunction.s
# G11 = 1 / (1 + 0.7 * s)
# G12 = 5 / (1 + 0.3 * s)
# G21 = 1 / (1 + 0.5 * s)
# G22 = 2 / (1 + 0.4 * s)

# # T=0.03 (przyspieszone z 0.01)
# T = 0.03
# G11_d = ctrl.c2d(G11, T)
# G12_d = ctrl.c2d(G12, T)
# G21_d = ctrl.c2d(G21, T)
# G22_d = ctrl.c2d(G22, T)

# print("G11_d:", G11_d)
# print("G12_d:", G12_d)
# print("G21_d:", G21_d)
# print("G22_d:", G22_d)

T_min = 0.01
p1, p2 = np.exp(-T_min / 0.7), np.exp(-T_min / 0.3)
p3, p4 = np.exp(-T_min / 0.5), np.exp(-T_min / 0.4)

A1_Y1, A2_Y1 = p1 + p2, -(p1 * p2)
b1, b2 = 1.0 * (1 - p1), 5.0 * (1 - p2)
B0_U1_Y1, B1_U1_Y1 = b1, -b1 * p2
B0_U2_Y1, B1_U2_Y1 = b2, -b2 * p1

A1_Y2, A2_Y2 = p3 + p4, -(p3 * p4)
b3, b4 = 1.0 * (1 - p3), 2.0 * (1 - p4)
B0_U1_Y2, B1_U1_Y2 = b3, -b3 * p4
B0_U2_Y2, B1_U2_Y2 = b4, -b4 * p3

print(A1_Y1, A2_Y1, B0_U1_Y1, B1_U1_Y1, B0_U2_Y1, B1_U2_Y1)
print(A1_Y2, A2_Y2, B0_U1_Y2, B1_U1_Y2, B0_U2_Y2, B1_U2_Y2)