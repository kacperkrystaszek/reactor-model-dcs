#include <iostream>
#include <thread>
#include <chrono>
#include <sstream>

#include "State.h"
#include "config/SystemConfig.h"
#include "config/ConfigLoader.h"
#include "ReactorModel.h"
#include "UDPSocket.h"
#include "messages/MessageConstructor.h"

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

int main() {
    try {
        SystemConfig cfg = ConfigLoader::load("config.json");
        std::cout << "[CONFIG] Loaded. Model Port: " << cfg.model_port << std::endl;

        UDPSocket sock(cfg.model_port);
        Reactor reactor;

        performHandshake(sock, cfg);

        std::cout << "[MODEL] Starting simulation loop..." << std::endl;

        float u1 = 0.0f, u2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;

        const auto cycle_duration = std::chrono::milliseconds(1800);
        auto next_step_time = std::chrono::steady_clock::now();

        while (true) {
            next_step_time += cycle_duration;

            while (std::chrono::steady_clock::now() < next_step_time) {
                std::string cmd = sock.recvFrom();

                if (!cmd.empty()) {
                    std::cout << "[MODEL] Parsing cmd: " << cmd << std::endl;
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
            std::cout << "[MODEL] Making step -> u1: " << u1 << " u2: " << u2 << std::endl;
            reactor.step(u1, u2, y1, y2);
            std::cout << "[MODEL] After step -> y1: " << y1 << " y2: " << y2 << std::endl;

            std::stringstream ss;
            MessageConstructor::createStateMsg(u1, u2, y1, y2, ss);
            std::string stateJson = ss.str();

            sock.sendTo(stateJson, cfg.logger_ip, cfg.logger_port);
            sock.sendTo(stateJson, cfg.feed_ip, cfg.feed_port);
            sock.sendTo(stateJson, cfg.coolant_ip, cfg.coolant_port);
        }

    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}