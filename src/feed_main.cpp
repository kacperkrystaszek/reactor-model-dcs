#include <cstdio>
#include <cstdlib>
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
        // Warning: Failed to lock memory (mlockall). Run as root for real-time performance.
    }

    // 2. Pre-fault our stack
    prefaultStack();

    // 3. Pin thread to a specific CPU core (avoid context switching and cache misses)
    // Core 0 is usually busy with OS tasks, so we pin to core 1, 2, or 3.
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < num_cores) {
        pinThreadToCore(core_id);
    }

    // 4. Set real-time scheduler (SCHED_FIFO)
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 90; // High priority (1-99)

    sched_setscheduler(0, SCHED_FIFO, &param);
#else
    (void)core_id; // Unused parameter warning fix
#endif
}

int main() {
    // Pin to core 1 by default for the feed controller
    setupRealTime(1);

    SystemConfig cfg = ConfigLoader::load("config.json");
    UDPSocket sock(cfg.feed_port);
    
    Controller controller(cfg, sock, cfg.feed_id);
    
    while (true) {
        controller.performHandshake();
        controller.mainLoop();
    }
    
    return 0;
}