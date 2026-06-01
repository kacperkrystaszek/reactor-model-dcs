#include "config/ConfigLoader.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <stdexcept>

// SystemConfig ConfigLoader::load(const std::string& filename) {
//     std::ifstream file(filename);
    
//     if (!file.is_open()) {
//         throw std::runtime_error("Cannot open file " + filename);
//     }

//     std::stringstream buffer;
//     buffer << file.rdbuf();
//     std::string json = buffer.str();
    
//     return loadFromString(json);
// }

SystemConfig ConfigLoader::loadFromString(const char* json) {
    SystemConfig cfg;

    ConfigLoader::getString(json, "LOGGER_IP", cfg.logger_ip, sizeof(cfg.logger_ip));
    cfg.logger_port = ConfigLoader::getInt(json, "LOGGER_PORT");

    ConfigLoader::getString(json, "MODEL_IP", cfg.model_ip, sizeof(cfg.model_ip));
    cfg.model_port = ConfigLoader::getInt(json, "MODEL_PORT");
    ConfigLoader::getString(json, "MODEL_ID", cfg.model_id, sizeof(cfg.model_id));

    ConfigLoader::getString(json, "FEED_CONTROLLER_IP", cfg.feed_ip, sizeof(cfg.feed_ip));
    cfg.feed_port = ConfigLoader::getInt(json, "FEED_CONTROLLER_PORT");
    ConfigLoader::getString(json, "FEED_ID", cfg.feed_id, sizeof(cfg.feed_id));

    ConfigLoader::getString(json, "COOLANT_CONTROLLER_IP", cfg.coolant_ip, sizeof(cfg.coolant_ip));
    cfg.coolant_port = ConfigLoader::getInt(json, "COOLANT_CONTROLLER_PORT");
    ConfigLoader::getString(json, "COOLANT_ID", cfg.coolant_id, sizeof(cfg.coolant_id));

    cfg.sp_y1 = ConfigLoader::getFloat(json, "SP_Y1");
    cfg.sp_y2 = ConfigLoader::getFloat(json, "SP_Y2");
    cfg.sp_y1_step2 = ConfigLoader::getFloat(json, "SP_Y1_STEP2");
    cfg.sp_y2_step2 = ConfigLoader::getFloat(json, "SP_Y2_STEP2");
    cfg.y1_0 = ConfigLoader::getFloat(json, "Y1_0");
    cfg.y2_0 = ConfigLoader::getFloat(json, "Y2_0");
    cfg.beta_y1 = ConfigLoader::getFloat(json, "BETA_Y1");
    cfg.beta_y2 = ConfigLoader::getFloat(json, "BETA_Y2");
    cfg.hmax_y1 = ConfigLoader::getInt(json, "HMAX_Y1");
    cfg.hmax_y2 = ConfigLoader::getInt(json, "HMAX_Y2");
    cfg.t_base = ConfigLoader::getInt(json, "T_BASE");
    cfg.alpha = ConfigLoader::getInt(json, "ALPHA");
    cfg.t_base = cfg.t_base / cfg.alpha;
    
    if (strstr(json, "\"LAMBDA\"") != nullptr) {
        cfg.lambda = ConfigLoader::getFloat(json, "LAMBDA");
    }

    cfg.event_based = ConfigLoader::getBool(json, "EVENT_BASED");

    return cfg;
}

void ConfigLoader::getString(const char* json, const char* key, char* out, size_t max_len) {
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

int ConfigLoader::getInt(const char* json, const char* key) {
    char searchKey[32];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* pos = strstr(json, searchKey);

    pos = strchr(pos, ':');

    const char* start = pos + 1;
    while (*start == ' ' || *start == '\t' || *start == '\"') start++;

    return atoi(start);
}

float ConfigLoader::getFloat(const char* json, const char* key) {
    char searchKey[32];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* pos = strstr(json, searchKey);

    pos = strchr(pos, ':');

    const char* start = pos + 1;
    while (*start == ' ' || *start == '\t' || *start == '\"') start++;

    return atof(start);
}

bool ConfigLoader::getBool(const char* json, const char* key) {
    char searchKey[32];
    snprintf(searchKey, sizeof(searchKey), "\"%s\"", key);
    const char* pos = strstr(json, searchKey);

    pos = strchr(pos, ':');

    const char* startTrue = strstr(pos, "true");
    const char* startFalse = strstr(pos, "false");

    if (startTrue && (!startFalse || startTrue < startFalse)) return true;
    if (startFalse && (!startTrue || startFalse < startTrue)) return false;
}