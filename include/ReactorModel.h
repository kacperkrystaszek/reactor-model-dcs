#pragma once
#define BUFFER_SIZE 2

#include <vector>

typedef struct {
    float y1[BUFFER_SIZE];
    float y2[BUFFER_SIZE];
    float u1[BUFFER_SIZE];
    float u2[BUFFER_SIZE];
} ReactorModel;

const std::vector<std::vector<float>> Amatrix = {
    {1.8629f, -0.8669f}, {0, 0},
    {0, 0}, {1.8695f, -0.8737f}
};

const std::vector<std::vector<float>> Bmatrix = {
    {0.0420f, -0.0380f}, {0.4758f, -0.4559f},
    {0.0582f, -0.0540f}, {0.1445f, -0.1361f}
};

class Reactor {
    private:
        float y1[BUFFER_SIZE];
        float y2[BUFFER_SIZE];
        float u1[BUFFER_SIZE];
        float u2[BUFFER_SIZE];

    public:
        Reactor(float initial_y1=0.0f, float initial_y2=0.0f, float initial_u1=0.0f, float initial_u2=0.0f);
        void step(float u1_in, float u2_in, float& y1_out, float& y2_out);
};