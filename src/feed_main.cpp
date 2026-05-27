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

// Bezpieczniejszy sposób na pre-fault bez alokacji ogromnych tablic na raz
void prefaultStack() {
    // Rezygnujemy z dummy[81024]. Zamiast tego rezerwujemy małą zmienną lotną, 
    // co zapobiega agresywnej alokacji, a jądro RT i tak zauważy dostęp do pamięci.
    volatile char page_buffer[1024];
    for (size_t i = 0; i < sizeof(page_buffer); i += 256) {
        page_buffer[i] = 0;
    }
}

bool pinThreadToCore(int core_id) {
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    // Jeśli system ma tylko 1 rdzeń (jak RPi 1), wymuszamy core_id = 0
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
    // 1. Lock memory to prevent page faults
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("Warning: mlockall failed. Run as root!");
    }

    // 2. Pre-fault our stack safely
    prefaultStack();

    // 3. Pin thread to a specific CPU core
    pinThreadToCore(core_id);

    // 4. Set real-time scheduler (SCHED_FIFO)
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 80; // Bezpieczna wysoka wartość dla Linux RT

    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("Warning: sched_setscheduler failed. Run as root!");
    }
#else
    (void)core_id;
#endif
}

int main() {
    // UWAGA: Musisz uruchomić program przez 'sudo', aby mlockall i SCHED_FIFO zadziałały!
    setupRealTime(1);

    // Aby upewnić się, czy błąd nie leży głębiej, dodajmy proste logowanie start

    SystemConfig cfg = ConfigLoader::load("config.json");
    UDPSocket sock(cfg.feed_port);
    
    Controller controller(cfg, sock, cfg.feed_id);
    
    while (true) {
        controller.performHandshake();
        controller.mainLoop();
    }
    
    return 0;
}