#ifndef __UTILS_H__
#define __UTILS_H__

#define PARAM_PATH "config/parameters.txt"
#define WD_LOG_PATH "log/watchdog_log_msg.txt"
#define WATCHDOG_FILE "watchdog.txt"

#include <stdio.h>
#include <unistd.h>
#include "protocol.h"

//Struttura load configuaration
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


//logger function
void logger(FILE * handler, const char *message);
void safe_logger(FILE * handler, const char *message);
//function to load configuration file 
int load_config(const char *filename, struct Config *config);

#endif