#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <thread>
#include <mutex>
#include <chrono>

// POSIX includes for Linux Real-Time and Sockets
#ifdef __linux__
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>

// ==========================================
// 0. Linux Real-Time Configuration
// ==========================================
#ifdef __linux__
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

void setupRealTime(int core_id) {
    // 1. Lock memory to prevent page faults
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        // Warning: Run as root
    }
    
    // 2. Pre-fault stack
    prefaultStack();

    // 3. Pin thread to CPU
    long num_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (core_id < num_cores) {
        pinThreadToCore(core_id);
    }

    // 4. Set scheduler to SCHED_FIFO
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = 90;
    sched_setscheduler(0, SCHED_FIFO, &param);
}
#endif

// ==========================================
// 1. Data Structures & Config
// ==========================================

enum State {
    INIT,
    WAIT_ACK,
    WAIT_START,
    RUNNING
};

struct SystemConfig {
    char logger_ip[16];
    int logger_port;

    char model_ip[16];
    int model_port;
    char model_id[16];

    char feed_ip[16];
    int feed_port;
    char feed_id[16];

    char coolant_ip[16];
    int coolant_port;
    char coolant_id[16];

    float sp_y1;
    float sp_y2;
    float sp_y1_step2;
    float sp_y2_step2;
    float y1_0;
    float y2_0;

    float beta_y1;
    float beta_y2;
    int t_base;
    int alpha;
    int hmax_y1;
    int hmax_y2;
    float lambda;
    
    bool event_based;
};

// ==========================================
// 2. Parsers & Builders
// ==========================================

class ConfigLoader {
    public:
        static void loadFromString(const char* json, SystemConfig& cfg) {
            getString(json, "LOGGER_IP", cfg.logger_ip, sizeof(cfg.logger_ip));
            cfg.logger_port = getInt(json, "LOGGER_PORT");

            getString(json, "MODEL_IP", cfg.model_ip, sizeof(cfg.model_ip));
            cfg.model_port = getInt(json, "MODEL_PORT");
            getString(json, "MODEL_ID", cfg.model_id, sizeof(cfg.model_id));

            getString(json, "FEED_CONTROLLER_IP", cfg.feed_ip, sizeof(cfg.feed_ip));
            cfg.feed_port = getInt(json, "FEED_CONTROLLER_PORT");
            getString(json, "FEED_ID", cfg.feed_id, sizeof(cfg.feed_id));

            getString(json, "COOLANT_CONTROLLER_IP", cfg.coolant_ip, sizeof(cfg.coolant_ip));
            cfg.coolant_port = getInt(json, "COOLANT_CONTROLLER_PORT");
            getString(json, "COOLANT_ID", cfg.coolant_id, sizeof(cfg.coolant_id));

            cfg.sp_y1 = getFloat(json, "SP_Y1");
            cfg.sp_y2 = getFloat(json, "SP_Y2");
            cfg.sp_y1_step2 = getFloat(json, "SP_Y1_STEP2");
            cfg.sp_y2_step2 = getFloat(json, "SP_Y2_STEP2");
            cfg.y1_0 = getFloat(json, "Y1_0");
            cfg.y2_0 = getFloat(json, "Y2_0");
            cfg.beta_y1 = getFloat(json, "BETA_Y1");
            cfg.beta_y2 = getFloat(json, "BETA_Y2");
            cfg.hmax_y1 = getInt(json, "HMAX_Y1");
            cfg.hmax_y2 = getInt(json, "HMAX_Y2");
            cfg.t_base = getInt(json, "T_BASE");
            cfg.alpha = getInt(json, "ALPHA");
            
            if (cfg.alpha != 0) {
                cfg.t_base = cfg.t_base / cfg.alpha;
            }
            
            if (strstr(json, "\"LAMBDA\"") != nullptr) {
                cfg.lambda = getFloat(json, "LAMBDA");
            } else {
                cfg.lambda = 0.5f;
            }

            cfg.event_based = getBool(json, "EVENT_BASED");
        }

    private:
        static void getString(const char* json, const char* key, char* out, size_t max_len) {
            out[0] = '\0';
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            
            const char* pos = strstr(json, searchKey);
            if (!pos) return;

            pos = strchr(pos, ':');
            if (!pos) return;
            pos = strchr(pos, '\"');
            if (!pos) return;
            
            const char* start = pos + 1;
            const char* end = strchr(start, '\"');
            if (!end) return;
            
            size_t len = end - start;
            if (len >= max_len) len = max_len - 1;
            strncpy(out, start, len);
            out[len] = '\0';
        }

        static int getInt(const char* json, const char* key) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            
            const char* pos = strstr(json, searchKey);
            if (!pos) return 0;

            pos = strchr(pos, ':');
            if (!pos) return 0;
            
            const char* start = pos + 1;
            while (*start == ' ' || *start == '\t') start++;
            
            return atoi(start);
        }

        static float getFloat(const char* json, const char* key) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            
            const char* pos = strstr(json, searchKey);
            if (!pos) return 0.0f;

            pos = strchr(pos, ':');
            if (!pos) return 0.0f;
            
            const char* start = pos + 1;
            while (*start == ' ' || *start == '\t') start++;
            
            return atof(start);
        }

        static bool getBool(const char* json, const char* key) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            
            const char* pos = strstr(json, searchKey);
            if (!pos) return false;

            pos = strchr(pos, ':');
            if (!pos) return false;
            
            const char* startTrue = strstr(pos, "true");
            const char* startFalse = strstr(pos, "false");

            if (startTrue && (!startFalse || startTrue < startFalse)) {
                return true;
            }
            
            return false;
        }
};

class MessageConstructor {
    public:
        static void createInitMsg(const SystemConfig& cfg, char* buf, size_t max_len) {
            snprintf(buf, max_len, "{\"type\":\"INIT\", \"id\": \"%s\"}", cfg.model_id);
        }
        
        static void createAckMsg(char* buf, size_t max_len) {
            snprintf(buf, max_len, "{\"type\":\"ACK\"}");
        }
        
        static void createStateMsg(float u1, float u2, float y1, float y2, float sp_y1, float sp_y2, int psc1, int psc2, bool is_event_y1, bool is_event_y2, float beta_y1, float beta_y2, int hmax_y1, int hmax_y2, char* buf, size_t max_len) {
            snprintf(buf, max_len, 
                "{\"type\":\"STATUS\", \"payload\": {"
                "\"u1\": %.4f, \"u2\": %.4f, "
                "\"y1\": %.4f, \"y2\": %.4f, "
                "\"sp_y1\": %.4f, \"sp_y2\": %.4f, "
                "\"psc1\": %d, \"psc2\": %d, "
                "\"is_event_y1\": %s, \"is_event_y2\": %s, "
                "\"beta_y1\": %.4f, \"beta_y2\": %.4f, "
                "\"hmax_y1\": %d, \"hmax_y2\": %d}}",
                u1, u2, y1, y2, sp_y1, sp_y2, psc1, psc2,
                is_event_y1 ? "true" : "false",
                is_event_y2 ? "true" : "false",
                beta_y1, beta_y2, hmax_y1, hmax_y2
            );
        }
};

// ==========================================
// 3. Logic Models
// ==========================================

#define BUFFER_SIZE 2

class Reactor {
    private:
        float y1[BUFFER_SIZE];
        float y2[BUFFER_SIZE];
        float u1[BUFFER_SIZE];
        float u2[BUFFER_SIZE];

        float Amatrix[4][2];
        float Bmatrix[4][2];

    public:
        Reactor(const SystemConfig& cfg, float initial_y1=0.0f, float initial_y2=0.0f, float initial_u1=0.0f, float initial_u2=0.0f) {
            for (int i = 0; i < BUFFER_SIZE; i++) {
                y1[i] = initial_y1;
                y2[i] = initial_y2;
                u1[i] = initial_u1;
                u2[i] = initial_u2;
            }   
            float alpha = cfg.alpha == 0 ? 1.0f : cfg.alpha;
            float current_T_BASE = cfg.t_base / alpha;
            float T_min = (current_T_BASE / 1000.0f) / 60.0f;
            float p1 = std::exp(-T_min / (0.7f / alpha));
            float p2 = std::exp(-T_min / (0.3f / alpha));
            float p3 = std::exp(-T_min / (0.5f / alpha));
            float p4 = std::exp(-T_min / (0.4f / alpha));

            Amatrix[0][0] = p1 + p2; Amatrix[0][1] = -(p1 * p2);
            Amatrix[1][0] = 0.0f;    Amatrix[1][1] = 0.0f;
            Amatrix[2][0] = 0.0f;    Amatrix[2][1] = 0.0f;
            Amatrix[3][0] = p3 + p4; Amatrix[3][1] = -(p3 * p4);

            float b1 = 1.0f * (1.0f - p1);
            float b2 = 5.0f * (1.0f - p2);
            float b3 = 1.0f * (1.0f - p3);
            float b4 = 2.0f * (1.0f - p4);

            Bmatrix[0][0] = b1; Bmatrix[0][1] = -b1 * p2;
            Bmatrix[1][0] = b2; Bmatrix[1][1] = -b2 * p1;
            Bmatrix[2][0] = b3; Bmatrix[2][1] = -b3 * p4;
            Bmatrix[3][0] = b4; Bmatrix[3][1] = -b4 * p3;
        }

        void step(float u1_in, float u2_in, float& y1_out, float& y2_out) {
            float y1_new = 
                Amatrix[0][0] * y1[0] + Amatrix[0][1] * y1[1] + 
                Bmatrix[0][0] * u1[0] + Bmatrix[0][1] * u1[1] +
                Bmatrix[1][0] * u2[0] + Bmatrix[1][1] * u2[1];

            float y2_new = 
                Amatrix[3][0] * y2[0] + Amatrix[3][1] * y2[1] + 
                Bmatrix[2][0] * u1[0] + Bmatrix[2][1] * u1[1] +
                Bmatrix[3][0] * u2[0] + Bmatrix[3][1] * u2[1];

            y1[1] = y1[0]; y1[0] = y1_new;
            y2[1] = y2[0]; y2[0] = y2_new;
            u1[1] = u1[0]; u1[0] = u1_in;
            u2[1] = u2[0]; u2[0] = u2_in;

            y1_out = y1_new;
            y2_out = y2_new;
        }
};

// ==========================================
// 4. POSIX Socket Wrapper (No exceptions)
// ==========================================

class UDPSocket {
    private:
        int sockfd;
        struct sockaddr_in my_addr;
        bool initialized;

    public:
        UDPSocket(int port) : initialized(false) {
            sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                return;
            }

            memset(&my_addr, 0, sizeof(my_addr));
            my_addr.sin_family = AF_INET;
            my_addr.sin_addr.s_addr = INADDR_ANY;
            my_addr.sin_port = htons(port);

            if (bind(sockfd, (const struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
                close(sockfd);
                return;
            }

            // Set socket timeout
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            // Real-Time optimizations for the socket
            int priority = 6;
            setsockopt(sockfd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));

            int tos = IPTOS_LOWDELAY;
            setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
            
            initialized = true;
        }

        ~UDPSocket() {
            if (initialized) {
                close(sockfd);
            }
        }

        bool isInitialized() const { return initialized; }

        bool sendTo(const char* msg, size_t len, const char* ip, int port) {
            if (!initialized) return false;
            
            struct sockaddr_in dest_addr;
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = htons(port);
            inet_aton(ip, &dest_addr.sin_addr);

            ssize_t res = sendto(sockfd, msg, len, 0, (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
            if (res < 0) {
                return false;
            }
            return true;
        }

        int recvFrom(char* buffer, int max_len) {
            if (!initialized) return -1;
            struct sockaddr_in client_address;
            socklen_t len = sizeof(client_address);
            int n = recvfrom(sockfd, buffer, max_len, 0, (struct sockaddr*)&client_address, &len);
            return n;
        }
};

// ==========================================
// 5. Main RTOS Application Logic
// ==========================================

SystemConfig cfg;
UDPSocket* sock = nullptr;

float u1_global = 0.0f;
float u2_global = 0.0f;

std::mutex mutex_u;
std::atomic<bool> in_handshake(true);

void performHandshake(UDPSocket& sock_ref, SystemConfig& cfg_ref) {
    enum State state = INIT;
    char buffer[1024];
    
    while (state < State::WAIT_START) {
        if (state == State::INIT) {
            MessageConstructor::createInitMsg(cfg_ref, buffer, sizeof(buffer));
            sock_ref.sendTo(buffer, strlen(buffer), cfg_ref.logger_ip, cfg_ref.logger_port);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        int n = sock_ref.recvFrom(buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            
            if (state == State::INIT && strstr(buffer, "\"ACK\"") != nullptr) {
                if (strstr(buffer, "\"config\"") != nullptr) {
                    ConfigLoader::loadFromString(buffer, cfg_ref);
                }

                state = State::WAIT_ACK;
            }
            else if (state == State::WAIT_ACK && strstr(buffer, "\"START\"") != nullptr) {
                MessageConstructor::createAckMsg(buffer, sizeof(buffer));
                sock_ref.sendTo(buffer, strlen(buffer), cfg_ref.logger_ip, cfg_ref.logger_port);
                state = State::WAIT_START;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}

// Communication Task - Acts like an interrupt for receiving data
void CommunicationTask() {
#ifdef __linux__
    setupRealTime(2); // Pin Communication task to Core 2
#endif

    char cmd[512];
    
    while (true) {
        if (sock == nullptr || in_handshake.load() || !sock->isInitialized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        int n = sock->recvFrom(cmd, sizeof(cmd) - 1);
        
        if (n > 0) {
            cmd[n] = '\0';
            const char* pU1 = strstr(cmd, "\"u1\":");
            const char* pU2 = strstr(cmd, "\"u2\":");

            std::lock_guard<std::mutex> lock(mutex_u);
            if (pU1) u1_global = atof(pU1 + 5);
            if (pU2) u2_global = atof(pU2 + 5);
        }
    }
}

// Simulation Task - Periodically calculates the simulation state
void SimulationTask() {
#ifdef __linux__
    setupRealTime(3); // Pin Simulation task to Core 3
#endif

    char stateJson[512];
    
    while (true) {
        if (sock == nullptr || !sock->isInitialized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            continue;
        }

        // Handshake phase
        in_handshake.store(true);
        performHandshake(*sock, cfg);
        in_handshake.store(false);

        Reactor reactor(cfg, cfg.y1_0, cfg.y2_0, 0.0f, 0.0f);

        {
            std::lock_guard<std::mutex> lock(mutex_u);
            u1_global = 0.0f;
            u2_global = 0.0f;
        }

        float y1 = cfg.y1_0, y2 = cfg.y2_0;
        int psc1 = 1; int psc2 = 1;
        float sim_time = 0.0f;
        float t_step_min = 0.0f;
        
        if (cfg.alpha != 0) {
            t_step_min = (cfg.t_base / 1000.0f) / 60.0f;
        }

        auto next_wake_time = std::chrono::steady_clock::now();
        const auto cycle_duration = std::chrono::milliseconds(cfg.t_base);

        bool connection_lost = false;

        while (!connection_lost) {
            // Block until exactly the next cycle
            next_wake_time += cycle_duration;
            std::this_thread::sleep_until(next_wake_time);

            if (sim_time > 2.0f) {
                cfg.sp_y1 = cfg.sp_y1_step2;
                cfg.sp_y2 = cfg.sp_y2_step2;
            }

            float current_u1, current_u2;
            {
                std::lock_guard<std::mutex> lock(mutex_u);
                current_u1 = u1_global;
                current_u2 = u2_global;
            }

            reactor.step(current_u1, current_u2, y1, y2);

            if (sim_time >= 5.0f) {
                y1 += ((rand() % 2000) / 1000.0f - 1.0f) * 0.01f;
                y2 += ((rand() % 2000) / 1000.0f - 1.0f) * 0.01f;
            }

            bool trigger = true;
            bool trigger_y1 = true;
            bool trigger_y2 = true;

            if (cfg.event_based) {
                trigger_y1 = (std::abs(y1 - cfg.sp_y1) >= cfg.beta_y1) || (psc1 >= cfg.hmax_y1);
                trigger_y2 = (std::abs(y2 - cfg.sp_y2) >= cfg.beta_y2) || (psc2 >= cfg.hmax_y2);
                trigger = trigger_y1 || trigger_y2;
            }

            MessageConstructor::createStateMsg(current_u1, current_u2, y1, y2, cfg.sp_y1, cfg.sp_y2, psc1, psc2, trigger_y1, trigger_y2, cfg.beta_y1, cfg.beta_y2, cfg.hmax_y1, cfg.hmax_y2, stateJson, sizeof(stateJson));

            if (!sock->sendTo(stateJson, strlen(stateJson), cfg.logger_ip, cfg.logger_port)) {
                connection_lost = true;
                break;
            }

            if (trigger) {
                sock->sendTo(stateJson, strlen(stateJson), cfg.feed_ip, cfg.feed_port);
                sock->sendTo(stateJson, strlen(stateJson), cfg.coolant_ip, cfg.coolant_port);
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

int main() {
    // Initial static config
    cfg.model_port = 5001;
    strncpy(cfg.logger_ip, "127.0.0.1", sizeof(cfg.logger_ip) - 1);
    cfg.logger_ip[sizeof(cfg.logger_ip) - 1] = '\0';
    cfg.logger_port = 5000;
    
    strncpy(cfg.model_id, "model", sizeof(cfg.model_id) - 1);
    cfg.model_id[sizeof(cfg.model_id) - 1] = '\0';

    sock = new UDPSocket(cfg.model_port);

    std::thread comm_thread(CommunicationTask);
    std::thread sim_thread(SimulationTask);

    comm_thread.join();
    sim_thread.join();
    
    return 0;
}