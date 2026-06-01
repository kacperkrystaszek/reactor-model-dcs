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

#define MAX_SAFE_STACK (8 * 1024)

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
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("Warning: mlockall failed. Run as root!");
    }

    prefaultStack();

    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < num_cores) {
        pinThreadToCore(core_id);
    }

    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 90;

    sched_setscheduler(0, SCHED_FIFO, &param);
#else
    (void)core_id;
#endif
}

int main() {
    setupRealTime(2);

    UDPSocket sock(5003);
    
    while (true) {
        Controller controller(sock, "coolant");
        controller.performHandshake();
        controller.mainLoop();
    }
    
    return 0;
}