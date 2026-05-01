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

void performHandshake(UDPSocket& sock, const SystemConfig& cfg) {
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
        SystemConfig cfg = ConfigLoader::load("config.json");
        std::cout << "[CONFIG] Loaded. Model Port: " << cfg.model_port << std::endl;

        UDPSocket sock(cfg.model_port);
        Reactor reactor;

        performHandshake(sock, cfg);

        std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Starting simulation loop..." << std::endl;

        float u1 = 0.0f, u2 = 0.0f;
        float y1 = cfg.y1_0, y2 = cfg.y2_0;
        int psc = 1;

        const auto cycle_duration = std::chrono::milliseconds(cfg.t_base);
        auto next_step_time = std::chrono::steady_clock::now();

        while (true) {
            next_step_time += cycle_duration;

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
            std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] After step -> y1: " << y1 << " y2: " << y2 << std::endl;

            float error1 = std::abs(cfg.sp_y1 - y1);
            float error2 = std::abs(cfg.sp_y2 - y2);

            bool trigger = true;

            if (cfg.event_based) {
                bool trigger = (error1 >= cfg.beta) || (error2 >= cfg.beta) || (psc >= cfg.hmax);
            }

            std::stringstream ss;
            MessageConstructor::createStateMsg(u1, u2, y1, y2, psc, ss);
            std::string stateJson = ss.str();

            sock.sendTo(stateJson, cfg.logger_ip, cfg.logger_port);

            if (trigger) {
                std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Event triggered. error feed: " << error1 << ", error coolant: " << error2 << " psc: " << psc << std::endl;
                sock.sendTo(stateJson, cfg.feed_ip, cfg.feed_port);
                sock.sendTo(stateJson, cfg.coolant_ip, cfg.coolant_port);
                psc = 1;
            } else {
                psc++;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[" << getCurrentTimeStr() <<  "] CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}