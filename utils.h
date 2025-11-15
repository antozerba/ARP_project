#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
typedef struct Config
{
    
    double map_width;
    double map_height;
    double drone_x;
    double drone_y;
}Config;

void logger(FILE * handler, const char *message);
int load_config(const char *filename, struct Config *config);

#endif