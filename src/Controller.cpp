#include "Controller.h"
#include "config/ConfigLoader.h"
#include "messages/MessageConstructor.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <algorithm>

Controller::Controller(UDPSocket& sock, const std::string& controller_id)
    : sock(sock), my_id(controller_id), state(State::INIT) {
    for (int i = 0; i < 6; ++i) {
        k_mpc_valid[i] = false;
    }
    history_count = 0;
}

void Controller::updateModel() {
    float alpha = config.alpha > 0 ? static_cast<float>(config.alpha) : 1.0f;
    float current_T_BASE = static_cast<float>(config.t_base);
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

    for (int i = 0; i < 6; ++i) {
        k_mpc_valid[i] = false;
    }
}

Matrix<4, 6> Controller::calculateKmpc(int psc){
    int n_steps = 3 * psc;
    
    float g_11[n_steps] = {0}, g_21[n_steps] = {0};
    float g_12[n_steps] = {0}, g_22[n_steps] = {0};
    
    float y1 = 0.0f, y1_prev = 0.0f;
    float y2 = 0.0f, y2_prev = 0.0f;
    for (int k = 0; k < n_steps; ++k) {
        float u1_now = 1.0f;
        float u1_old = (k == 0) ? 0.0f : 1.0f;
        
        float y1_new = A1_Y1 * y1 + A2_Y1 * y1_prev + B0_U1_Y1 * u1_now + B1_U1_Y1 * u1_old;
        float y2_new = A1_Y2 * y2 + A2_Y2 * y2_prev + B0_U1_Y2 * u1_now + B1_U1_Y2 * u1_old;
                       
        g_11[k] = y1_new;
        g_21[k] = y2_new;
        y1_prev = y1; y1 = y1_new;
        y2_prev = y2; y2 = y2_new;
    }

    y1 = 0.0f; y1_prev = 0.0f;
    y2 = 0.0f; y2_prev = 0.0f;
    for (int k = 0; k < n_steps; ++k) {
        float u2_now = 1.0f;
        float u2_old = (k == 0) ? 0.0f : 1.0f;
        
        float y1_new = A1_Y1 * y1 + A2_Y1 * y1_prev + B0_U2_Y1 * u2_now + B1_U2_Y1 * u2_old;
        float y2_new = A1_Y2 * y2 + A2_Y2 * y2_prev + B0_U2_Y2 * u2_now + B1_U2_Y2 * u2_old;
                       
        g_12[k] = y1_new;
        g_22[k] = y2_new;
        y1_prev = y1; y1 = y1_new;
        y2_prev = y2; y2 = y2_new;
    }
    
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
    
    float lambda = 0.5;
    for (int i = 0; i < 4; ++i) {
        HTH.data[i][i] += lambda;
    }
    
    Matrix<4, 4> invHTH = invert4x4(HTH);
    Matrix<4, 6> K = invHTH * GT;
    return K;
}

void Controller::cacheKmpc(){
    int biggestHmax = std::max(config.hmax_y1, config.hmax_y2);
    for(int i = 0; i < biggestHmax; i++){
        k_mpc_cache[i] = calculateKmpc(i);
        k_mpc_valid[i] = true;
    }
}

Matrix<4, 6> Controller::getKmpc(int psc) {
    if (psc >= 0 && psc < config.hmax_y1 && psc < config.hmax_y2 && k_mpc_valid[psc]) {
        return k_mpc_cache[psc];
    }
    return k_mpc_cache[0];
}

static float evaluateLagrange(const float* x, const float* y, size_t count, float target_x) {
    float result = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float term = y[i];
        for (size_t j = 0; j < count; ++j) {
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
    
    if (history_count == 1) {
        y1 = history_y1[0]; y1_prev = history_y1[0];
        y2 = history_y2[0]; y2_prev = history_y2[0];
        return;
    }
    if (history_count == 2 || std::abs(T_f - T_BASE) < 0.01f) {
        y1 = history_y1[history_count - 1]; y1_prev = history_y1[history_count - 2];
        y2 = history_y2[history_count - 1]; y2_prev = history_y2[history_count - 2];
        return;
    }

    float t_curr_real = history_t[history_count - 1];
    float normalized_t[HISTORY_MAX];
    for (size_t i = 0; i < history_count; ++i) {
        normalized_t[i] = history_t[i] - t_curr_real;
    }

    float y1_resampled = evaluateLagrange(normalized_t, history_y1, history_count, -T_BASE);
    float y2_resampled = evaluateLagrange(normalized_t, history_y2, history_count, -T_BASE);

    float min_y1 = history_y1[0];
    float max_y1 = history_y1[0];
    float min_y2 = history_y2[0];
    float max_y2 = history_y2[0];

    for (size_t i = 1; i < history_count; ++i) {
        if (history_y1[i] < min_y1) min_y1 = history_y1[i];
        if (history_y1[i] > max_y1) max_y1 = history_y1[i];
        if (history_y2[i] < min_y2) min_y2 = history_y2[i];
        if (history_y2[i] > max_y2) max_y2 = history_y2[i];
    }

    y1 = history_y1[history_count - 1];
    y1_prev = std::clamp(y1_resampled, min_y1, max_y1);
    y2 = history_y2[history_count - 1];
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
    char buffer[1024];
    
    struct timespec ts;
    if (my_id == "coolant"){
        ts.tv_sec = 2;
        ts.tv_nsec = 0; //0.1s;
    } else {
        ts.tv_sec = 1;
        ts.tv_nsec = 0; //0.1s;
    }

    while (state != State::RUNNING) {
        if (state == State::INIT) {
            snprintf(buffer, sizeof(buffer), "{\"type\":\"INIT\"}");
            sock.sendTo(buffer, "192.168.70.1", 5000);
            nanosleep(&ts, NULL);
            
        }
        if (state == State::WAIT_START){
            snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\"}");
            sock.sendTo(buffer, "192.168.70.1", 5000);
            nanosleep(&ts, NULL);
        }
        
        int n = sock.recvFrom(buffer, sizeof(buffer) - 1);

        if (n > 0){
            buffer[n] = '\0';

            if (strstr(buffer, "RESTART") != nullptr) {
                state = State::INIT;
                nanosleep(&ts, NULL);
                continue;
            }
            if (state == State::INIT && strstr(buffer, "ACK") != nullptr) {
                if (strstr(buffer, "config") != nullptr){
                    config = ConfigLoader::loadFromString(buffer);
                }
                snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\"}");
                sock.sendTo(buffer, "192.168.70.1", 5000);
                state = State::WAIT_START;
                updateModel();
                cacheKmpc();
            }
            if (state == State::WAIT_START && strstr(buffer, "START") != nullptr) {
                snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\"}");
                sock.sendTo(buffer, "192.168.70.1", 5000);
                state = State::RUNNING;
            }
        }
        else{
            nanosleep(&ts, NULL);
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
        
        if (strstr(recv_buf, "\"RESTART\"") != nullptr) break;
        if (strstr(recv_buf, "\"STATUS\"") == nullptr) continue;
        
        auto getFloatVal = [&](const char* key, float default_val) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
            char* pos = strstr(recv_buf, searchKey);
            if (!pos) return default_val;
            
            pos += strlen(searchKey);
            while (*pos == ' ' || *pos == '\t') pos++;
            return strtof(pos, NULL);
        };
        
        auto getIntVal = [&](const char* key, int default_val) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\":", key);
            char* pos = strstr(recv_buf, searchKey);
            if (!pos) return default_val;
            
            pos += strlen(searchKey);
            while (*pos == ' ' || *pos == '\t') pos++;
            return (int)strtol(pos, NULL, 10);
        };

        float y1 = getFloatVal("y1", 0.0f);
        float y2 = getFloatVal("y2", 0.0f);
        float u1_curr = getFloatVal("u1", 0.0f);
        float u2_curr = getFloatVal("u2", 0.0f);
        float sp_y1 = getFloatVal("sp_y1", config.sp_y1);
        float sp_y2 = getFloatVal("sp_y2", config.sp_y2);
        int psc1 = getIntVal("psc1", 1);
        int psc2 = getIntVal("psc2", 1);

        int my_psc = (my_id == "feed") ? psc1 : psc2;
        float T_f = my_psc * (config.t_base / 1000.0f);
        current_t += T_f;
        
        if (history_count < HISTORY_MAX) {
            history_t[history_count] = current_t;
            history_y1[history_count] = y1;
            history_y2[history_count] = y2;
            history_count++;
        } else {
            for (size_t i = 0; i < HISTORY_MAX - 1; ++i) {
                history_t[i] = history_t[i+1];
                history_y1[i] = history_y1[i+1];
                history_y2[i] = history_y2[i+1];
            }
            history_t[HISTORY_MAX - 1] = current_t;
            history_y1[HISTORY_MAX - 1] = y1;
            history_y2[HISTORY_MAX - 1] = y2;
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

        sock.sendTo(cmd_buf, cmd_len, "192.168.70.5", 5001);
    }
}