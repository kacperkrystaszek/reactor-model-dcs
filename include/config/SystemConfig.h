#pragma once


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