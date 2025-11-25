#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <unistd.h>
#include "protocol.h"
typedef struct Config
{
    double map_width;
    double map_height;
    double drone_x;
    double drone_y;
    float MASS;
    float K;
    float DT;
    float STEP_FORCE;
    float RHO;
    float ETA;
}Config;



void logger(FILE * handler, const char *message);
int load_config(const char *filename, struct Config *config);
#endif