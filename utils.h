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
ssize_t write_all(int fd, const void *buf, size_t size) ;
ssize_t read_all(int fd, void *buf, size_t size);
size_t serialize_worldstate(const WorldState *state, char *buf) ;
void deserialize_worldstate(const char *buf, WorldState *state) ;
#endif