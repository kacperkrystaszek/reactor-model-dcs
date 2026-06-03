#include <cmath>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <random>
// FreeRTOS includes
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "stm32f2xx_hal.h"

// LwIP includes for sockets (replace POSIX)
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip.h"

// ==========================================
// 1. Data Structures & Config
// ==========================================

enum State {
    INIT,
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
            cfg.logger_port = getInt(json, "LOGGER_PORT", cfg.logger_port);

            getString(json, "MODEL_IP", cfg.model_ip, sizeof(cfg.model_ip));
            cfg.model_port = getInt(json, "MODEL_PORT", cfg.model_port);
            getString(json, "MODEL_ID", cfg.model_id, sizeof(cfg.model_id));

            getString(json, "FEED_CONTROLLER_IP", cfg.feed_ip, sizeof(cfg.feed_ip));
            cfg.feed_port = getInt(json, "FEED_CONTROLLER_PORT", cfg.feed_port);
            getString(json, "FEED_ID", cfg.feed_id, sizeof(cfg.feed_id));

            getString(json, "COOLANT_CONTROLLER_IP", cfg.coolant_ip, sizeof(cfg.coolant_ip));
            cfg.coolant_port = getInt(json, "COOLANT_CONTROLLER_PORT", cfg.coolant_port);
            getString(json, "COOLANT_ID", cfg.coolant_id, sizeof(cfg.coolant_id));

            cfg.sp_y1 = getFloat(json, "SP_Y1", cfg.sp_y1);
            cfg.sp_y2 = getFloat(json, "SP_Y2", cfg.sp_y2);
            cfg.sp_y1_step2 = getFloat(json, "SP_Y1_STEP2", cfg.sp_y1_step2);
            cfg.sp_y2_step2 = getFloat(json, "SP_Y2_STEP2", cfg.sp_y2_step2);
            cfg.y1_0 = getFloat(json, "Y1_0", cfg.y1_0);
            cfg.y2_0 = getFloat(json, "Y2_0", cfg.y2_0);
            cfg.beta_y1 = getFloat(json, "BETA_Y1", cfg.beta_y1);
            cfg.beta_y2 = getFloat(json, "BETA_Y2", cfg.beta_y2);
            cfg.hmax_y1 = getInt(json, "HMAX_Y1", cfg.hmax_y1);
            cfg.hmax_y2 = getInt(json, "HMAX_Y2", cfg.hmax_y2);
            cfg.t_base = getInt(json, "T_BASE", cfg.t_base);
            cfg.alpha = getInt(json, "ALPHA", cfg.alpha);

//            if (cfg.alpha != 0) {
//                cfg.t_base = cfg.t_base / cfg.alpha;
//            }

            if (strstr(json, "\"LAMBDA\"") != nullptr) {
                cfg.lambda = getFloat(json, "LAMBDA", cfg.lambda);
            }

            cfg.event_based = getBool(json, "EVENT_BASED", cfg.event_based);
        }

    private:
        static void getString(const char* json, const char* key, char* out, size_t max_len) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            const char* pos = strstr(json, searchKey);

            // KLUCZOWA POPRAWKA: Jeśli nie ma klucza w JSON, po prostu wychodzimy.
            // NIE kasujemy zawartości 'out'!
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

        static int getInt(const char* json, const char* key, int default_val) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            const char* pos = strstr(json, searchKey);
            if (!pos) return default_val;

            pos = strchr(pos, ':');
            if (!pos) return default_val;

            const char* start = pos + 1;
            while (*start == ' ' || *start == '\t' || *start == '\"') start++;

            return atoi(start);
        }

        static float getFloat(const char* json, const char* key, float default_val) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            const char* pos = strstr(json, searchKey);
            if (!pos) return default_val;

            pos = strchr(pos, ':');
            if (!pos) return default_val;

            const char* start = pos + 1;
            while (*start == ' ' || *start == '\t' || *start == '\"') start++;

            return atof(start);
        }

        static bool getBool(const char* json, const char* key, bool default_val) {
            char searchKey[32];
            snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
            const char* pos = strstr(json, searchKey);
            if (!pos) return default_val;

            pos = strchr(pos, ':');
            if (!pos) return default_val;

            const char* startTrue = strstr(pos, "true");
            const char* startFalse = strstr(pos, "false");

            if (startTrue && (!startFalse || startTrue < startFalse)) return true;
            if (startFalse && (!startTrue || startFalse < startTrue)) return false;

            return default_val;
        }
};

class MessageConstructor {
    public:
        static void createInitMsg(char* buf, size_t max_len) {
            snprintf(buf, max_len, "{\"type\":\"INIT\"}");
        }

        static void createAckMsg(char* buf, size_t max_len, const char* ackFor) {
            snprintf(buf, max_len, "{\"type\":\"ACK\", \"ack_for\": %s}", ackFor);
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
            float alpha = cfg.alpha == 0 ? 1.0f : static_cast<float>(cfg.alpha);
            float current_T_BASE = static_cast<float>(cfg.t_base) / alpha;
            float T_min = (current_T_BASE / 1000.0f) / 60.0f;

            float p1 = std::exp(-T_min / (0.7f));
            float p2 = std::exp(-T_min / (0.3f));
            float p3 = std::exp(-T_min / (0.5f));
            float p4 = std::exp(-T_min / (0.4f));

//            float p1 = std::exp(-T_min / 0.7f);
//            float p2 = std::exp(-T_min / 0.3f);
//            float p3 = std::exp(-T_min / 0.5f);
//            float p4 = std::exp(-T_min / 0.4f);

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
// 4. LwIP Wrapper (No exceptions)
// ==========================================

class UDPSocket {
    private:
        int sockfd;
        struct sockaddr_in my_addr;
        bool initialized;

    public:
        UDPSocket() : sockfd(-1), initialized(false) {
            memset(&my_addr, 0, sizeof(my_addr));
        }

        ~UDPSocket() {
            if (initialized && sockfd >= 0) {
                lwip_close(sockfd);
            }
        }

        bool begin(int port) {
            if (initialized) return true;

            sockfd = lwip_socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                return false;
            }

            my_addr.sin_family = AF_INET;
            my_addr.sin_addr.s_addr = INADDR_ANY;
            my_addr.sin_port = lwip_htons(port);

            if (lwip_bind(sockfd, (const struct sockaddr*)&my_addr, sizeof(my_addr)) < 0) {
                lwip_close(sockfd);
                sockfd = -1;
                return false;
            }

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;
            lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            int tos = IPTOS_LOWDELAY;
            lwip_setsockopt(sockfd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));

            initialized = true;
            return true;
        }

        bool isInitialized() const { return initialized; }

        void setNonBlocking(bool nonBlocking) {
        	if (!initialized || sockfd < 0) return;
        	int non_blocking = nonBlocking ? 1 : 0;
        	lwip_ioctl(sockfd, FIONBIO, &non_blocking);
        }

        bool sendTo(const char* msg, size_t len, const char* ip, int port) {
            if (!initialized) return false;

            struct sockaddr_in dest_addr;
            memset(&dest_addr, 0, sizeof(dest_addr));
            dest_addr.sin_family = AF_INET;
            dest_addr.sin_port = lwip_htons(port);
            dest_addr.sin_addr.s_addr = inet_addr(ip);

            ssize_t res = lwip_sendto(sockfd, msg, len, 0, (const struct sockaddr*)&dest_addr, sizeof(dest_addr));
            return (res >= 0);
        }

        int recvFrom(char* buffer, int max_len) {
            if (!initialized) return -1;
            struct sockaddr_in client_address;
            socklen_t len = sizeof(client_address);
            return lwip_recvfrom(sockfd, buffer, max_len, 0, (struct sockaddr*)&client_address, &len);
        }
};

class STM32HardwareRNG {
private:
    RNG_HandleTypeDef* hrng_ptr;

public:
    using result_type = uint32_t;

    STM32HardwareRNG(RNG_HandleTypeDef* handle) : hrng_ptr(handle) {}

    static constexpr result_type min() { return 0; }
    static constexpr result_type max() { return 0xFFFFFFFF; }

    result_type operator()() {
        uint32_t random_number = 0;
        HAL_RNG_GenerateRandomNumber(hrng_ptr, &random_number);
        return random_number;
    }
};

class NoiseGen {
private:
    std::mt19937 prng;
    std::normal_distribution<float> distY1;
    std::normal_distribution<float> distY2;

public:
    NoiseGen(RNG_HandleTypeDef* hrng_handle, float dev1, float dev2)
        : distY1(0.0f, dev1),
          distY2(0.0f, dev2) {

        uint32_t seed_val = 12345;
        if (hrng_handle != nullptr) {
            HAL_RNG_GenerateRandomNumber(hrng_handle, &seed_val);
        }
        prng.seed(seed_val);
    }

    float getY1Noise() {
        return distY1(prng);
    }

    float getY2Noise() {
        return distY2(prng);
    }
};
// ==========================================
// 5. Main RTOS Application Logic
// ==========================================
extern RNG_HandleTypeDef hrng;
SystemConfig cfg;
UDPSocket sock;

float u1_global = 0.0f;
float u2_global = 0.0f;

SemaphoreHandle_t mutex_u;
std::atomic<bool> in_handshake(true);
std::atomic<bool> need_restart(false);

void performHandshake(UDPSocket& sock_ref, SystemConfig& cfg_ref) {
    enum State state = INIT;
    char buffer[1024];

    while (state != State::RUNNING) {
        if (state == State::INIT) {
            MessageConstructor::createInitMsg(buffer, sizeof(buffer));
            sock_ref.sendTo(buffer, strlen(buffer), "192.168.70.1", 5000);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        if (state == State::WAIT_START){
        	snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\", \"ack_for\":\"CONFIG\"}");
            sock_ref.sendTo(buffer, strlen(buffer), "192.168.70.1", 5000);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        int n = sock_ref.recvFrom(buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';

            if (strstr(buffer, "RESTART") != nullptr) {
                continue;
            }

            if (state == State::INIT && strstr(buffer, "ACK") != nullptr && strstr(buffer, "\"INIT\"") != nullptr) {
                if (strstr(buffer, "config") != nullptr) {
                    ConfigLoader::loadFromString(buffer, cfg_ref);
                }
                snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\", \"ack_for\":\"CONFIG\"}");
                sock_ref.sendTo(buffer, strlen(buffer), "192.168.70.1", 5000);
                state = State::WAIT_START;
                continue;
            }
            if (state == State::WAIT_START && strstr(buffer, "START") != nullptr) {
            	snprintf(buffer, sizeof(buffer), "{\"type\":\"ACK\", \"ack_for\":\"START\"}");
                sock_ref.sendTo(buffer, strlen(buffer), "192.168.70.1", 5000);
                need_restart.store(false);
                state = State::RUNNING;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void CommunicationTask(void *pvParameters) {
    char cmd[512];

    while (true) {
        if (!sock.isInitialized() || in_handshake.load()) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        int n = sock.recvFrom(cmd, sizeof(cmd) - 1);
        if (n > 0) {
            cmd[n] = '\0';
            if (strstr(cmd, "RESTART") != nullptr) {
                need_restart.store(true);
                while (!in_handshake.load()) {
					vTaskDelay(pdMS_TO_TICKS(10));
				}
                continue;
            }

            const char* pU1 = strstr(cmd, "\"u1\":");
            const char* pU2 = strstr(cmd, "\"u2\":");

            xSemaphoreTake(mutex_u, portMAX_DELAY);
            if (pU1) { float v = (float)atof(pU1 + 5); if (std::isfinite(v)) u1_global = v; }
            if (pU2) { float v = (float)atof(pU2 + 5); if (std::isfinite(v)) u2_global = v; }
            xSemaphoreGive(mutex_u);
        }
    }
}

void SimulationTask(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(2000));

    while (!sock.begin(5001)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    char stateJson[512];

    while (true) {
        if (!sock.isInitialized()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            sock.begin(5001);
            continue;
        }

        sock.setNonBlocking(true);
        in_handshake.store(true);
        vTaskDelay(pdMS_TO_TICKS(150));
        performHandshake(sock, cfg);
        in_handshake.store(false);
        sock.setNonBlocking(false);

        Reactor reactor(cfg, cfg.y1_0, cfg.y2_0, 0.0f, 0.0f);
        NoiseGen generator(&hrng, 0.002f, 0.002f);

        xSemaphoreTake(mutex_u, portMAX_DELAY);
        u1_global = 0.0f;
        u2_global = 0.0f;
        xSemaphoreGive(mutex_u);

        float y1 = cfg.y1_0, y2 = cfg.y2_0;
        int psc1 = 1; int psc2 = 1;

        float actual_t_base_ms = cfg.t_base;
        if (cfg.alpha != 0) {
            actual_t_base_ms = (float)cfg.t_base / cfg.alpha;
        }

        TickType_t xFrequency = pdMS_TO_TICKS((uint32_t)actual_t_base_ms);
		if (xFrequency == 0) {
			xFrequency = 1;
		}

		TickType_t xStartWakeTime = xTaskGetTickCount();
		TickType_t xLastWakeTime = xStartWakeTime;
		float sim_time = 0.0f;

        const TickType_t LOGGER_MIN_INTERVAL = pdMS_TO_TICKS(25);
        const TickType_t LOGGER_MAX_SILENCE = pdMS_TO_TICKS(2000);
        TickType_t last_logger_tx = xTaskGetTickCount() - LOGGER_MAX_SILENCE;

        while (!need_restart.load()) {
            vTaskDelayUntil(&xLastWakeTime, xFrequency);
            TickType_t elapsed_ticks = xTaskGetTickCount() - xStartWakeTime;
            sim_time = (elapsed_ticks * portTICK_PERIOD_MS) / 60000.0f;

            if (sim_time > 6.0f){
                int finishedLen = snprintf(stateJson, sizeof(stateJson), "{\"type\":\"FINISHED\"}");
                sock.sendTo(stateJson, finishedLen, cfg.logger_ip, cfg.logger_port);
                vTaskDelay(pdMS_TO_TICKS(200));
                continue;
            }

            if (sim_time > 2.0f) {
                cfg.sp_y1 = cfg.sp_y1_step2;
                cfg.sp_y2 = cfg.sp_y2_step2;
            }

            float current_u1, current_u2;
            xSemaphoreTake(mutex_u, portMAX_DELAY);
            current_u1 = u1_global;
            current_u2 = u2_global;
            xSemaphoreGive(mutex_u);

            reactor.step(current_u1, current_u2, y1, y2);

            if (sim_time >= 4.0f) {
                y1 += generator.getY1Noise();
                y2 += generator.getY2Noise();
            }

            bool control_trigger = true;
            bool trigger_y1 = true;
            bool trigger_y2 = true;

            if (cfg.event_based) {
                trigger_y1 = (std::abs(y1 - cfg.sp_y1) >= cfg.beta_y1) || (psc1 >= cfg.hmax_y1);
                trigger_y2 = (std::abs(y2 - cfg.sp_y2) >= cfg.beta_y2) || (psc2 >= cfg.hmax_y2);
                control_trigger = trigger_y1 || trigger_y2;
            }

            MessageConstructor::createStateMsg(current_u1, current_u2, y1, y2, cfg.sp_y1, cfg.sp_y2, psc1, psc2, trigger_y1, trigger_y2, cfg.beta_y1, cfg.beta_y2, cfg.hmax_y1, cfg.hmax_y2, stateJson, sizeof(stateJson));

            if (control_trigger) {
                sock.sendTo(stateJson, strlen(stateJson), cfg.feed_ip, cfg.feed_port);
                sock.sendTo(stateJson, strlen(stateJson), cfg.coolant_ip, cfg.coolant_port);
            }

            TickType_t now = xTaskGetTickCount();
            bool logger_due = (now - last_logger_tx) >= LOGGER_MAX_SILENCE;
            bool logger_allowed = (now - last_logger_tx) >= LOGGER_MIN_INTERVAL;
            if ((control_trigger && logger_allowed) || logger_due) {
                sock.sendTo(stateJson, strlen(stateJson), cfg.logger_ip, cfg.logger_port);
                last_logger_tx = now;
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
        }
    }
}

// ------------------------------------------
// CubeIDE integration point
// ------------------------------------------
extern "C" void app_main() {
    // Initial static config
    cfg.model_port = 5001;
    strncpy(cfg.logger_ip, "192.168.70.1", sizeof(cfg.logger_ip) - 1);
    cfg.logger_ip[sizeof(cfg.logger_ip) - 1] = '\0';
    cfg.logger_port = 5000;

    strncpy(cfg.model_id, "model", sizeof(cfg.model_id) - 1);
    cfg.model_id[sizeof(cfg.model_id) - 1] = '\0';


    mutex_u = xSemaphoreCreateMutex();

    if (mutex_u != NULL) {
        xTaskCreate(CommunicationTask, "CommTask", 1024, NULL, tskIDLE_PRIORITY + 1, NULL);
        xTaskCreate(SimulationTask, "SimTask", 2048, NULL, tskIDLE_PRIORITY + 2, NULL);
    }
}
