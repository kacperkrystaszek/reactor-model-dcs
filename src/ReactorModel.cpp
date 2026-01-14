#include "ReactorModel.h"

Reactor::Reactor(float initial_y1, float initial_y2, float initial_u1, float initial_u2) {
    for (int i = 0; i < BUFFER_SIZE; i++)
    {
        y1[i] = initial_y1;
        y2[i] = initial_y2;
        u1[i] = initial_u1;
        u2[i] = initial_u2;
    }   
}

void Reactor::step(float u1_in, float u2_in, float& y1_out, float& y2_out) {
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