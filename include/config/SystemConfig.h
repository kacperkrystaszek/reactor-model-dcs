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
    
    bool event_based;
};