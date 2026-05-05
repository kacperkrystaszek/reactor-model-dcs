#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>
#include <cmath>

#include "State.h"
#include "config/SystemConfig.h"
#include "config/ConfigLoader.h"
#include "ReactorModel.h"
#include "UDPSocket.h"
#include "messages/MessageConstructor.h"
#include <iomanip>

void performHandshake(UDPSocket& sock, SystemConfig& cfg) {
    std::cout << "[MODEL] Connecting to Logger at " << cfg.logger_ip << ":" << cfg.logger_port << "..." << std::endl;
    
    enum State state = INIT; // 0=INIT, 1=WAIT_ACK, 2=WAIT_START
    
    while (state < State::WAIT_START) {
        if (state == State::INIT) {
            std::stringstream ss;
            MessageConstructor::createInitMsg(cfg, ss);
            sock.sendTo(ss.str(), cfg.logger_ip, cfg.logger_port);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        std::string msg = sock.recvFrom();
        if (!msg.empty()) {
            if (state == 0 && msg.find("\"ACK\"") != std::string::npos) {
                std::cout << "[MODEL] ACK received. Standby." << std::endl;
                
                size_t config_pos = msg.find("\"config\"");
                if (config_pos != std::string::npos) {
                    SystemConfig new_cfg = ConfigLoader::loadFromString(msg);
                    cfg = new_cfg; // Update full config
                    std::cout << "[MODEL] Config updated from Logger." << std::endl;
                }

                state = State::WAIT_ACK;
            }
            else if (state == WAIT_ACK && msg.find("\"START\"") != std::string::npos) {
                std::cout << "[MODEL] START received!" << std::endl;
                std::stringstream ss;
                MessageConstructor::createAckMsg(ss);
                sock.sendTo(ss.str(), cfg.logger_ip, cfg.logger_port);
                state = State::WAIT_START;
            }
        }
    }
}

std::string getCurrentTimeStr() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&now_c);
    
    std::stringstream ss;
    ss << std::put_time(local_time, "%H:%M:%S");
    
    return ss.str();
}

int main() {
    try {
        SystemConfig cfg;
        cfg.model_port = 5001;
        cfg.logger_ip = "127.0.0.1";
        cfg.logger_port = 5000;
        cfg.model_id = "model";

        std::cout << "[CONFIG] Started. Initial Model Port: " << cfg.model_port << std::endl;

        UDPSocket sock(cfg.model_port);

        while (true) {
            performHandshake(sock, cfg);

            Reactor reactor(cfg, cfg.y1_0, cfg.y2_0, 0.0f, 0.0f);

            std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Starting simulation loop..." << std::endl;

            float u1 = 0.0f, u2 = 0.0f;
            float y1 = cfg.y1_0, y2 = cfg.y2_0;
            int psc1 = 1; int psc2 = 1;
            float sim_time = 0.0f;
            float t_step_min = (cfg.t_base / 1000.0f) / 60.0f;

            const auto cycle_duration = std::chrono::milliseconds(cfg.t_base);
            auto next_step_time = std::chrono::steady_clock::now();

            while (true) {
                next_step_time += cycle_duration;
                
                if (sim_time > 2.0f) {
                    cfg.sp_y1 = cfg.sp_y1_step2;
                    cfg.sp_y2 = cfg.sp_y2_step2;
                }

                while (std::chrono::steady_clock::now() < next_step_time) {
                    std::string cmd = sock.recvFrom();

                    if (!cmd.empty()) {
                        std::cout << "[" << getCurrentTimeStr() << "] [MODEL] Parsing cmd: " << cmd << std::endl;
                        size_t pU1 = cmd.find("\"u1\":");
                        size_t pU2 = cmd.find("\"u2\":");

                    if (pU1 != std::string::npos) {
                        u1 = std::stof(cmd.substr(pU1 + 5));
                    }
                    if (pU2 != std::string::npos) {
                        u2 = std::stof(cmd.substr(pU2 + 5));
                    }
                }
            }
            // U1 - FEED, U2 - COOLANT
            std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Making step -> u1: " << u1 << " u2: " << u2 << std::endl;
            reactor.step(u1, u2, y1, y2);
            
            if (sim_time >= 5.0f) {
                y1 += ((rand() % 2000) / 1000.0f - 1.0f) * 0.01f;
                y2 += ((rand() % 2000) / 1000.0f - 1.0f) * 0.01f;
            }

            std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] After step -> y1: " << y1 << " y2: " << y2 << " (sim_time: " << sim_time << " min)" << std::endl;

            float error1 = std::abs(cfg.sp_y1 - y1);
            float error2 = std::abs(cfg.sp_y2 - y2);

            bool trigger = true;
            bool trigger_y1 = true;
            bool trigger_y2 = true;

            if (cfg.event_based) {
                trigger_y1 = (std::abs(y1 - cfg.sp_y1) >= cfg.beta_y1) || (psc1 >= cfg.hmax_y1);
                trigger_y2 = (std::abs(y2 - cfg.sp_y2) >= cfg.beta_y2) || (psc2 >= cfg.hmax_y2);
                trigger = trigger_y1 || trigger_y2;
            }

            std::stringstream ss;
            MessageConstructor::createStateMsg(u1, u2, y1, y2, cfg.sp_y1, cfg.sp_y2, psc1, psc2, trigger_y1, trigger_y2, ss);
            std::string stateJson = ss.str();

            try {
                sock.sendTo(stateJson, cfg.logger_ip, cfg.logger_port);

                if (trigger) {
                    std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Event triggered. error feed: " << error1 << ", error coolant: " << error2 << " psc1: " << psc1 << " psc2: " << psc2 << std::endl;
                    sock.sendTo(stateJson, cfg.feed_ip, cfg.feed_port);
                    sock.sendTo(stateJson, cfg.coolant_ip, cfg.coolant_port);
                } 
            } catch (const std::runtime_error& e) {
                std::cout << "[" << getCurrentTimeStr() << "] [MODEL] Connection lost (" << e.what() << "). Restarting handshake..." << std::endl;
                break; // Break inner loop to restart handshake
            } 
            if (trigger_y1) {
                psc1 = 1;
            }
            else {
                psc1++;
            }
            if (trigger_y2) {
                psc2 = 1;
            }
            else {
                psc2++;
            }

            sim_time += t_step_min;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[" << getCurrentTimeStr() <<  "] CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}