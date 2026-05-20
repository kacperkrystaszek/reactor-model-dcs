#include <iostream>
#include <sstream>
#include <cmath>
#include <string>
#include <atomic>
#include <cstdlib>
#include <iomanip>

// FreeRTOS includes
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "State.h"
#include "config/SystemConfig.h"
#include "config/ConfigLoader.h"
#include "ReactorModel.h"
#include "UDPSocket.h"
#include "messages/MessageConstructor.h"

// Global variables for FreeRTOS tasks synchronization
SystemConfig cfg;
UDPSocket* sock = nullptr;

float u1_global = 0.0f;
float u2_global = 0.0f;

SemaphoreHandle_t mutex_u;
std::atomic<bool> in_handshake(true);

std::string getCurrentTimeStr() {
    // Basic tick-based time conversion for STM32 FreeRTOS
    uint32_t ticks = xTaskGetTickCount();
    uint32_t ms = ticks * portTICK_PERIOD_MS;
    uint32_t s = ms / 1000;
    
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << (s / 3600) << ":"
       << std::setfill('0') << std::setw(2) << ((s % 3600) / 60) << ":"
       << std::setfill('0') << std::setw(2) << (s % 60);
    return ss.str();
}

void performHandshake(UDPSocket& sock_ref, SystemConfig& cfg_ref) {
    std::cout << "[MODEL] Connecting to Logger at " << cfg_ref.logger_ip << ":" << cfg_ref.logger_port << "..." << std::endl;
    
    enum State state = INIT;
    
    while (state < State::WAIT_START) {
        if (state == State::INIT) {
            std::stringstream ss;
            MessageConstructor::createInitMsg(cfg_ref, ss);
            sock_ref.sendTo(ss.str(), cfg_ref.logger_ip, cfg_ref.logger_port);
            vTaskDelay(pdMS_TO_TICKS(1000));
        }

        std::string msg = sock_ref.recvFrom();
        if (!msg.empty()) {
            if (state == 0 && msg.find("\"ACK\"") != std::string::npos) {
                std::cout << "[MODEL] ACK received. Standby." << std::endl;
                
                size_t config_pos = msg.find("\"config\"");
                if (config_pos != std::string::npos) {
                    SystemConfig new_cfg = ConfigLoader::loadFromString(msg);
                    cfg_ref = new_cfg; // Update full config
                    std::cout << "[MODEL] Config updated from Logger." << std::endl;
                }

                state = State::WAIT_ACK;
            }
            else if (state == State::WAIT_ACK && msg.find("\"START\"") != std::string::npos) {
                std::cout << "[MODEL] START received!" << std::endl;
                std::stringstream ss;
                MessageConstructor::createAckMsg(ss);
                sock_ref.sendTo(ss.str(), cfg_ref.logger_ip, cfg_ref.logger_port);
                state = State::WAIT_START;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

// Communication Task - Acts like an interrupt for receiving data
void CommunicationTask(void *pvParameters) {
    while (true) {
        // If socket is not ready or we are in handshake mode, don't steal packets
        if (sock == nullptr || in_handshake.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        std::string cmd = sock->recvFrom();
        
        if (!cmd.empty()) {
            std::cout << "[" << getCurrentTimeStr() << "] [MODEL] Parsing cmd: " << cmd << std::endl;
            size_t pU1 = cmd.find("\"u1\":");
            size_t pU2 = cmd.find("\"u2\":");

            // Update globally shared variables protected by mutex
            xSemaphoreTake(mutex_u, portMAX_DELAY);
            if (pU1 != std::string::npos) {
                u1_global = std::stof(cmd.substr(pU1 + 5));
            }
            if (pU2 != std::string::npos) {
                u2_global = std::stof(cmd.substr(pU2 + 5));
            }
            xSemaphoreGive(mutex_u);
        }
        // Assuming recvFrom has a timeout, it will periodically unblock.
        // If it's fully non-blocking, we need vTaskDelay to yield.
        // vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// Simulation Task - Periodically calculates the simulation state
void SimulationTask(void *pvParameters) {
    while (true) {
        // Handshake phase
        in_handshake.store(true);
        performHandshake(*sock, cfg);
        in_handshake.store(false);

        Reactor reactor(cfg, cfg.y1_0, cfg.y2_0, 0.0f, 0.0f);

        std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Starting simulation loop..." << std::endl;

        xSemaphoreTake(mutex_u, portMAX_DELAY);
        u1_global = 0.0f;
        u2_global = 0.0f;
        xSemaphoreGive(mutex_u);

        float y1 = cfg.y1_0, y2 = cfg.y2_0;
        int psc1 = 1; int psc2 = 1;
        float sim_time = 0.0f;
        float t_step_min = (cfg.t_base / 1000.0f) / 60.0f;

        TickType_t xLastWakeTime = xTaskGetTickCount();
        const TickType_t xFrequency = pdMS_TO_TICKS(cfg.t_base);

        bool connection_lost = false;

        while (!connection_lost) {
            // Block until exactly the next cycle
            vTaskDelayUntil(&xLastWakeTime, xFrequency);

            if (sim_time > 2.0f) {
                cfg.sp_y1 = cfg.sp_y1_step2;
                cfg.sp_y2 = cfg.sp_y2_step2;
            }

            // Read the latest control commands (simulating an interrupt-updated state)
            float current_u1, current_u2;
            xSemaphoreTake(mutex_u, portMAX_DELAY);
            current_u1 = u1_global;
            current_u2 = u2_global;
            xSemaphoreGive(mutex_u);

            std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Making step -> u1: " << current_u1 << " u2: " << current_u2 << std::endl;
            reactor.step(current_u1, current_u2, y1, y2);

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
            MessageConstructor::createStateMsg(current_u1, current_u2, y1, y2, cfg.sp_y1, cfg.sp_y2, psc1, psc2, trigger_y1, trigger_y2, ss);
            std::string stateJson = ss.str();

            try {
                sock->sendTo(stateJson, cfg.logger_ip, cfg.logger_port);

                if (trigger) {
                    std::cout << "[" << getCurrentTimeStr() <<  "] [MODEL] Event triggered. error feed: " << error1 << ", error coolant: " << error2 << " psc1: " << psc1 << " psc2: " << psc2 << std::endl;
                    sock->sendTo(stateJson, cfg.feed_ip, cfg.feed_port);
                    sock->sendTo(stateJson, cfg.coolant_ip, cfg.coolant_port);
                } 
            } catch (const std::runtime_error& e) {
                std::cout << "[" << getCurrentTimeStr() << "] [MODEL] Connection lost (" << e.what() << "). Restarting handshake..." << std::endl;
                connection_lost = true;
            } 

            if (trigger_y1) {
                psc1 = 1;
            } else {
                psc1++;
            }
            if (trigger_y2) {
                psc2 = 1;
            } else {
                psc2++;
            }

            sim_time += t_step_min;
        }
    }
}

extern "C" void app_main() {
    // Initial static config
    cfg.model_port = 5001;
    cfg.logger_ip = "127.0.0.1";
    cfg.logger_port = 5000;
    cfg.model_id = "model";

    std::cout << "[CONFIG] Started. Initial Model Port: " << cfg.model_port << std::endl;

    sock = new UDPSocket(cfg.model_port);
    mutex_u = xSemaphoreCreateMutex();

    if (mutex_u != NULL) {
        // Communication task should have a higher priority to act like an interrupt
        xTaskCreate(CommunicationTask, "CommTask", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
        
        // Simulation task runs strictly periodically
        xTaskCreate(SimulationTask, "SimTask", 8192, NULL, tskIDLE_PRIORITY + 1, NULL);
    } else {
        std::cerr << "Failed to create mutex!" << std::endl;
    }
}

int main() {
    // For standard STM32 C++ projects, hardware init happens before this or inside main
    // HAL_Init();
    // SystemClock_Config();
    
    app_main();
    
    // Start FreeRTOS scheduler
    vTaskStartScheduler();
    
    // Should never reach here
    while (true) {}
    
    return 0;
}