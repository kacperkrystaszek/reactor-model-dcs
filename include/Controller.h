#pragma once
#include <string>
#include <array>
#include "config/SystemConfig.h"
#include "UDPSocket.h"
#include "State.h"
#include "MatrixMath.h"

class Controller {
public:
    Controller(UDPSocket& sock, const std::string& controller_id);
    ~Controller();
    
    void performHandshake();
    void mainLoop();

private:
    void updateModel();
    void cacheKmpc();
    Matrix<4, 6> calculateKmpc(int psc);
    Matrix<4, 6> getKmpc(int psc);
    void resampleStatesLagrange(float T_f, float& y1, float& y1_prev, float& y2, float& y2_prev);
    Matrix<6, 1> calculateFreeResponse(const std::array<float, 2>& y1_h, const std::array<float, 2>& y2_h, 
                                       const std::array<float, 2>& u1_h, const std::array<float, 2>& u2_h, int psc);

    SystemConfig config;
    UDPSocket& sock;
    std::string my_id;
    State state;

    Matrix<4, 6> k_mpc_cache[6];
    bool k_mpc_valid[6];

    size_t history_max;
    float* history_t;
    float* history_y1;
    float* history_y2;
    size_t history_count = 0;
    float current_t = 0.0f;

    float A1_Y1, A2_Y1;
    float B0_U1_Y1, B1_U1_Y1;
    float B0_U2_Y1, B1_U2_Y1;

    float A1_Y2, A2_Y2;
    float B0_U1_Y2, B1_U1_Y2;
    float B0_U2_Y2, B1_U2_Y2;
};