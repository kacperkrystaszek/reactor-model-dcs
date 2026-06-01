#include <cstdio>
#include <cstdlib>
#include "Controller.h"
#include "UDPSocket.h"

#ifdef __linux__
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

void prefaultStack() {
    volatile char page_buffer[1024];
    for (size_t i = 0; i < sizeof(page_buffer); i += 256) {
        page_buffer[i] = 0;
    }
}

bool pinThreadToCore(int core_id) {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_cores == 1) {
        core_id = 0;
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    return pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) == 0;
}
#endif

void setupRealTime(int core_id = 1) {
#ifdef __linux__
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("Warning: mlockall failed. Run as root!");
    }
    prefaultStack();
    pinThreadToCore(core_id);

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 90;

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("Warning: sched_setscheduler failed. Run as root!");
    }
#else
    (void)core_id;
#endif
}

int main() {
    setupRealTime(1);

    UDPSocket sock(5002);
    
    while (true) {
        Controller controller(sock, "feed");
        controller.performHandshake();
        controller.mainLoop();
    }
    
    return 0;
}