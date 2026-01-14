#include "config/ConfigLoader.h"
#include <fstream>
#include <sstream>
#include <stdexcept>

SystemConfig ConfigLoader::load(const std::string& filename) {
    SystemConfig cfg;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    cfg.logger_ip = getString(json, "LOGGER_IP");
    cfg.logger_port = getInt(json, "LOGGER_PORT");

    cfg.model_ip = getString(json, "MODEL_IP");
    cfg.model_port = getInt(json, "MODEL_PORT");
    cfg.model_id = getString(json, "MODEL_ID");

    cfg.feed_ip = getString(json, "FEED_CONTROLLER_IP");
    cfg.feed_port = getInt(json, "FEED_CONTROLLER_PORT");
    cfg.feed_id = getString(json, "FEED_ID");

    cfg.coolant_ip = getString(json, "COOLANT_CONTROLLER_IP");
    cfg.coolant_port = getInt(json, "COOLANT_CONTROLLER_PORT");
    cfg.coolant_id = getString(json, "COOLANT_ID");

    return cfg;
}

std::string ConfigLoader::getString(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return "";

    pos = json.find(":", pos);
    pos = json.find("\"", pos);
    if (pos == std::string::npos) return "";
    
    size_t start = pos + 1;
    size_t end = json.find("\"", start);
    return json.substr(start, end - start);
}

int ConfigLoader::getInt(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\"";
    size_t pos = json.find(searchKey);
    if (pos == std::string::npos) return 0;

    pos = json.find(":", pos);
    size_t start = json.find_first_of("0123456789", pos);
    if (start == std::string::npos) return 0;
    
    size_t end = json.find_first_of(",}\n\r\t ", start);
    return std::stoi(json.substr(start, end - start));
}