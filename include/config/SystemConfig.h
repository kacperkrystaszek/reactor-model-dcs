#pragma once
#include <string>

struct SystemConfig {
    std::string logger_ip;
    int logger_port;

    std::string model_ip;
    int model_port;
    std::string model_id;

    std::string feed_ip;
    int feed_port;
    std::string feed_id;

    std::string coolant_ip;
    int coolant_port;
    std::string coolant_id;
};