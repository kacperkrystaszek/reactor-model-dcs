#pragma once
#define BUFFER_SIZE 2

#include <vector>
#include "config/SystemConfig.h"

typedef struct {
    float y1[BUFFER_SIZE];
    float y2[BUFFER_SIZE];
    float u1[BUFFER_SIZE];
    float u2[BUFFER_SIZE];
} ReactorModel;

class Reactor {
    private:
        float y1[BUFFER_SIZE];
        float y2[BUFFER_SIZE];
        float u1[BUFFER_SIZE];
        float u2[BUFFER_SIZE];

        std::vector<std::vector<float>> Amatrix;
        std::vector<std::vector<float>> Bmatrix;

    public:
        Reactor(const SystemConfig& cfg, float initial_y1=0.0f, float initial_y2=0.0f, float initial_u1=0.0f, float initial_u2=0.0f);
        void step(float u1_in, float u2_in, float& y1_out, float& y2_out);
};