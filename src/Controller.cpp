#include "Controller.h"
#include "messages/MessageConstructor.h"
#include <iostream>
#include <sstream>
#include <cmath>
#include <chrono>
#include <thread>
#include <algorithm>
#include <string_view>

Controller::Controller(SystemConfig config, UDPSocket& sock, const std::string& controller_id)
    : config(config), sock(sock), my_id(controller_id), state(State::INIT) {
    history_t.reserve(history_len + 1);
    history_y1.reserve(history_len + 1);
    history_y2.reserve(history_len + 1);
    updateModel();
}

void Controller::updateModel() {
    float alpha = config.alpha > 0 ? static_cast<float>(config.alpha) : 1.0f;
    float current_T_BASE = static_cast<float>(config.t_base); // Already divided by alpha in ConfigLoader
    float T_min = (current_T_BASE / 1000.0f) / 60.0f;
    
    float p1 = std::exp(-T_min / (0.7f / alpha));
    float p2 = std::exp(-T_min / (0.3f / alpha));
    float p3 = std::exp(-T_min / (0.5f / alpha));
    float p4 = std::exp(-T_min / (0.4f / alpha));

    A1_Y1 = p1 + p2;
    A2_Y1 = -(p1 * p2);
    float b1 = 1.0f * (1.0f - p1);
    float b2 = 5.0f * (1.0f - p2);
    B0_U1_Y1 = b1;
    B1_U1_Y1 = -b1 * p2;
    B0_U2_Y1 = b2;
    B1_U2_Y1 = -b2 * p1;

    A1_Y2 = p3 + p4;
    A2_Y2 = -(p3 * p4);
    float b3 = 1.0f * (1.0f - p3);
    float b4 = 2.0f * (1.0f - p4);
    B0_U1_Y2 = b3;
    B1_U1_Y2 = -b3 * p4;
    B0_U2_Y2 = b4;
    B1_U2_Y2 = -b4 * p3;

    k_mpc_cache.clear();
}

std::string Controller::createInitMsg() {
    return "{\"type\":\"INIT\",\"id\":\"" + my_id + "\"}";
}

std::pair<std::vector<float>, std::vector<float>> Controller::stepResponse(float u1_step, float u2_step, int n_steps) {
    float y1 = 0.0f, y1_prev = 0.0f;
    float y2 = 0.0f, y2_prev = 0.0f;
    std::vector<float> resp_y1(n_steps);
    std::vector<float> resp_y2(n_steps);
    
    for (int k = 0; k < n_steps; ++k) {
        float u1_now = u1_step;
        float u1_old = (k == 0) ? 0.0f : u1_step;
        float u2_now = u2_step;
        float u2_old = (k == 0) ? 0.0f : u2_step;
        
        float y1_new = A1_Y1 * y1 + A2_Y1 * y1_prev +
                       B0_U1_Y1 * u1_now + B1_U1_Y1 * u1_old +
                       B0_U2_Y1 * u2_now + B1_U2_Y1 * u2_old;
                       
        float y2_new = A1_Y2 * y2 + A2_Y2 * y2_prev +
                       B0_U1_Y2 * u1_now + B1_U1_Y2 * u1_old +
                       B0_U2_Y2 * u2_now + B1_U2_Y2 * u2_old;
                       
        resp_y1[k] = y1_new;
        resp_y2[k] = y2_new;
        
        y1_prev = y1; y1 = y1_new;
        y2_prev = y2; y2 = y2_new;
    }
    
    return {resp_y1, resp_y2};
}

Matrix<4, 6> Controller::getKmpc(int psc) {
    auto it = k_mpc_cache.find(psc);
    if (it != k_mpc_cache.end()) {
        return it->second;
    }

    int n_steps = 3 * psc;
    auto [g_11, g_21] = stepResponse(1.0f, 0.0f, n_steps);
    auto [g_12, g_22] = stepResponse(0.0f, 1.0f, n_steps);
    
    int s1 = psc - 1;
    int s2 = 2 * psc - 1;
    int s3 = 3 * psc - 1;
    
    Matrix<6, 4> G;
    G.data[0] = {g_11[s1], g_12[s1], 0.0f,     0.0f};
    G.data[1] = {g_21[s1], g_22[s1], 0.0f,     0.0f};
    G.data[2] = {g_11[s2], g_12[s2], g_11[s1], g_12[s1]};
    G.data[3] = {g_21[s2], g_22[s2], g_21[s1], g_22[s1]};
    G.data[4] = {g_11[s3], g_12[s3], g_11[s2], g_12[s2]};
    G.data[5] = {g_21[s3], g_22[s3], g_21[s2], g_22[s2]};
    
    Matrix<4, 6> GT = G.transpose();
    Matrix<4, 4> HTH = GT * G;
    
    float lambda = config.lambda;
    for (int i = 0; i < 4; ++i) {
        HTH.data[i][i] += lambda;
    }
    
    Matrix<4, 4> invHTH = invert4x4(HTH);
    Matrix<4, 6> K = invHTH * GT;
    
    k_mpc_cache[psc] = K;
    std::cout << "[" << my_id << "] Multi-rate K_mpc cached for psc=" << psc << ".\n";
    return K;
}

static float evaluateLagrange(const std::vector<float>& x, const std::vector<float>& y, float target_x) {
    float result = 0.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        float term = y[i];
        for (size_t j = 0; j < x.size(); ++j) {
            if (i != j) {
                term = term * (target_x - x[j]) / (x[i] - x[j]);
            }
        }
        result += term;
    }
    return result;
}

void Controller::resampleStatesLagrange(float T_f, float& y1, float& y1_prev, float& y2, float& y2_prev) {
    float T_BASE = config.t_base / 1000.0f;
    
    if (history_t.size() == 1) {
        y1 = history_y1.back(); y1_prev = history_y1.back();
        y2 = history_y2.back(); y2_prev = history_y2.back();
        return;
    }
    if (history_t.size() == 2 || std::abs(T_f - T_BASE) < 0.01f) {
        y1 = history_y1.back(); y1_prev = history_y1[history_y1.size() - 2];
        y2 = history_y2.back(); y2_prev = history_y2[history_y2.size() - 2];
        return;
    }

    float t_curr_real = history_t.back();
    std::vector<float> normalized_t(history_t.size());
    for (size_t i = 0; i < history_t.size(); ++i) {
        normalized_t[i] = history_t[i] - t_curr_real;
    }

    float y1_resampled = evaluateLagrange(normalized_t, history_y1, -T_BASE);
    float y2_resampled = evaluateLagrange(normalized_t, history_y2, -T_BASE);

    float min_y1 = *std::min_element(history_y1.begin(), history_y1.end());
    float max_y1 = *std::max_element(history_y1.begin(), history_y1.end());
    float min_y2 = *std::min_element(history_y2.begin(), history_y2.end());
    float max_y2 = *std::max_element(history_y2.begin(), history_y2.end());

    y1 = history_y1.back();
    y1_prev = std::clamp(y1_resampled, min_y1, max_y1);
    y2 = history_y2.back();
    y2_prev = std::clamp(y2_resampled, min_y2, max_y2);
}

Matrix<6, 1> Controller::calculateFreeResponse(const std::array<float, 2>& y1_h, const std::array<float, 2>& y2_h, 
                                               const std::array<float, 2>& u1_h, const std::array<float, 2>& u2_h, int psc) {
    std::array<float, 2> ly1 = y1_h;
    std::array<float, 2> ly2 = y2_h;
    std::array<float, 2> lu1 = u1_h;
    std::array<float, 2> lu2 = u2_h;
    
    float current_u1 = lu1[0];
    float current_u2 = lu2[0];
    
    Matrix<6, 1> preds;
    int pred_idx = 0;
    
    for (int k = 1; k <= 3 * psc; ++k) {
        float pred_y1 = A1_Y1 * ly1[0] + A2_Y1 * ly1[1] +
                        B0_U1_Y1 * current_u1 + B1_U1_Y1 * lu1[1] +
                        B0_U2_Y1 * current_u2 + B1_U2_Y1 * lu2[1];
                        
        float pred_y2 = A1_Y2 * ly2[0] + A2_Y2 * ly2[1] +
                        B0_U1_Y2 * current_u1 + B1_U1_Y2 * lu1[1] +
                        B0_U2_Y2 * current_u2 + B1_U2_Y2 * lu2[1];
                        
        if (k % psc == 0) {
            preds.data[pred_idx][0] = pred_y1;
            preds.data[pred_idx + 1][0] = pred_y2;
            pred_idx += 2;
        }
        
        ly1 = {pred_y1, ly1[0]};
        ly2 = {pred_y2, ly2[0]};
        lu1 = {current_u1, lu1[0]};
        lu2 = {current_u2, lu2[0]};
    }
    
    return preds;
}

void Controller::performHandshake() {
    std::cout << "[" << my_id << "] Connecting to Logger...\n";
    
    while (state != State::RUNNING) {
        if (state == State::INIT) {
            std::string msg = createInitMsg();
            sock.sendTo(msg, config.logger_ip, config.logger_port);
            std::cout << "[" << my_id << "] Sent INIT...\n";
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        std::string data = sock.recvFrom();
        if (data.empty()) continue;
        
        if (state == State::INIT && data.find("\"ACK\"") != std::string::npos) {
            std::cout << "[" << my_id << "] Received ACK. Standby.\n";
            state = State::WAIT_START; // Using WAIT_START as STANDBY
        } else if (state == State::WAIT_START && data.find("\"START\"") != std::string::npos) {
            std::cout << "[" << my_id << "] Received START! Starting loop.\n";
            sock.sendTo("{\"type\":\"ACK\"}", config.logger_ip, config.logger_port);
            state = State::RUNNING;
        }
    }
}

void Controller::mainLoop() {
    std::array<float, 2> y1_h = {0.0f, 0.0f};
    std::array<float, 2> y2_h = {0.0f, 0.0f};
    std::array<float, 2> u1_h = {0.0f, 0.0f};
    std::array<float, 2> u2_h = {0.0f, 0.0f};

    char recv_buf[1024];

    while (true) {
        int n = sock.recvFrom(recv_buf, sizeof(recv_buf) - 1);
        if (n <= 0) continue;
        recv_buf[n] = '\0';
        
        std::string_view data(recv_buf, n);
        if (data.find("\"STATUS\"") == std::string_view::npos) continue;
        
        // Simple JSON parsing
        auto getFloatVal = [&](const std::string& key, float default_val) {
            size_t pos = data.find("\"" + key + "\":");
            if (pos == std::string_view::npos) return default_val;
            size_t start = data.find_first_of("0123456789.-", pos + key.length() + 3);
            if (start == std::string_view::npos) return default_val;
            size_t end = data.find_first_of(",}\n\r\t ", start);
            // std::stof requires null-terminated string, so we copy the substring
            // but we can just use strtof directly on the buffer
            char* end_ptr;
            return strtof(data.data() + start, &end_ptr);
        };
        
        auto getIntVal = [&](const std::string& key, int default_val) {
            size_t pos = data.find("\"" + key + "\":");
            if (pos == std::string_view::npos) return default_val;
            size_t start = data.find_first_of("0123456789-", pos + key.length() + 3);
            if (start == std::string_view::npos) return default_val;
            char* end_ptr;
            return (int)strtol(data.data() + start, &end_ptr, 10);
        };

        float y1 = getFloatVal("y1", 0.0f);
        float y2 = getFloatVal("y2", 0.0f);
        float u1_curr = getFloatVal("u1", 0.0f);
        float u2_curr = getFloatVal("u2", 0.0f);
        float sp_y1 = getFloatVal("sp_y1", config.sp_y1);
        float sp_y2 = getFloatVal("sp_y2", config.sp_y2);
        int psc1 = getIntVal("psc1", 1);
        int psc2 = getIntVal("psc2", 1);

        int my_psc = (my_id == config.feed_id) ? psc1 : psc2;
        float T_f = my_psc * (config.t_base / 1000.0f);
        current_t += T_f;
        
        history_t.push_back(current_t);
        history_y1.push_back(y1);
        history_y2.push_back(y2);

        if (history_t.size() > history_len) {
            history_t.erase(history_t.begin());
            history_y1.erase(history_y1.begin());
            history_y2.erase(history_y2.begin());
        }

        float y1_curr, y1_prev, y2_curr, y2_prev;
        resampleStatesLagrange(T_f, y1_curr, y1_prev, y2_curr, y2_prev);
        
        float u1_prev = (my_psc > 1) ? u1_curr : u1_h[0];
        float u2_prev = (my_psc > 1) ? u2_curr : u2_h[0];

        y1_h = {y1_curr, y1_prev};
        y2_h = {y2_curr, y2_prev};
        u1_h = {u1_curr, u1_prev};
        u2_h = {u2_curr, u2_prev};

        Matrix<6, 1> f_vec = calculateFreeResponse(y1_h, y2_h, u1_h, u2_h, my_psc);
        
        Matrix<6, 1> w_vec;
        for (int i = 0; i < 3; ++i) {
            w_vec.data[2*i][0] = sp_y1;
            w_vec.data[2*i + 1][0] = sp_y2;
        }
        
        Matrix<4, 6> K_mpc_psc = getKmpc(my_psc);
        Matrix<6, 1> diff = w_vec - f_vec;
        Matrix<4, 1> delta_u = K_mpc_psc * diff;

        char cmd_buf[64];
        int cmd_len = 0;
        if (my_id == config.feed_id) {
            float new_u = std::clamp(u1_curr + delta_u.data[0][0], -0.5f, 0.5f);
            cmd_len = snprintf(cmd_buf, sizeof(cmd_buf), "{\"u1\":%f}", new_u);
        } else if (my_id == config.coolant_id) {
            float new_u = std::clamp(u2_curr + delta_u.data[1][0], -0.5f, 0.5f);
            cmd_len = snprintf(cmd_buf, sizeof(cmd_buf), "{\"u2\":%f}", new_u);
        } else {
            continue;
        }

#ifndef DISABLE_RT_LOGGING
        std::cout << "[" << my_id << "] Event received. Sending value: " << cmd_buf << "\n";
#endif
        sock.sendTo(cmd_buf, cmd_len, config.model_ip, config.model_port);
    }
}