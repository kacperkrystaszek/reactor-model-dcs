#include <iostream>
#include <stdexcept>
#include "config/ConfigLoader.h"
#include "Controller.h"
#include "UDPSocket.h"

#ifdef __linux__
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

#define MAX_SAFE_STACK (8 * 1024) // 8KB

void prefaultStack() {
    unsigned char dummy[MAX_SAFE_STACK];
    memset(dummy, 0, MAX_SAFE_STACK);
}

bool pinThreadToCore(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
}
#endif

void setupRealTime(int core_id = 1) {
#ifdef __linux__
    // 1. Lock memory to prevent page faults
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        std::cerr << "Warning: Failed to lock memory (mlockall). Run as root for real-time performance.\n";
    } else {
        std::cout << "Memory locked successfully.\n";
    }

    // 2. Pre-fault our stack
    prefaultStack();

    // 3. Pin thread to a specific CPU core (avoid context switching and cache misses)
    // Core 0 is usually busy with OS tasks, so we pin to core 1, 2, or 3.
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < num_cores) {
        if (pinThreadToCore(core_id)) {
            std::cout << "Thread pinned to CPU core " << core_id << " successfully.\n";
        } else {
            std::cerr << "Warning: Failed to pin thread to core " << core_id << ".\n";
        }
    }

    // 4. Set real-time scheduler (SCHED_FIFO)
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 90; // High priority (1-99)

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        std::cerr << "Warning: Failed to set SCHED_FIFO scheduler. Run as root for real-time performance.\n";
    } else {
        std::cout << "SCHED_FIFO scheduler set successfully with priority " << param.sched_priority << ".\n";
    }
#else
    (void)core_id; // Unused parameter warning fix
    std::cout << "Real-time setup is only supported on Linux.\n";
#endif
}

int main() {
    try {
        // Pin to core 2 by default for the coolant controller
        setupRealTime(2);

        SystemConfig cfg = ConfigLoader::load("config.json");
        UDPSocket sock(cfg.coolant_port);
        
        Controller controller(cfg, sock, cfg.coolant_id);
        
        while (true) {
            controller.performHandshake();
            std::cout << "[" << cfg.coolant_id << "] Control Loop Started.\n";
            controller.mainLoop();
        }
    } catch (const std::exception& e) {
        std::cerr << "CRITICAL ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}