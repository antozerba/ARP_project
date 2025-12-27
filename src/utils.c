#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "protocol.h"
#include <string.h>
#include <sys/file.h>

//FILE FUNZIONI COMUNI A TUTTI I PROCESSI

int load_config(const char *filename, struct Config *config) {
    FILE * log= fopen("log/utils_log.txt","w");
    logger(log, "Loading configuration");
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening config file");
        logger(log, "Error opening config file");
        return 0;
    }
    fscanf(file, "map_width=%lf\n", &config->map_width);
    fscanf(file, "map_height=%lf\n", &config->map_height);
    fscanf(file, "drone_x=%lf\n", &config->drone_x);
    fscanf(file, "drone_y=%lf\n", &config->drone_y);
    fscanf(file, "MASS=%f\n", &config->MASS);
    fscanf(file, "K=%f\n", &config->K);
    fscanf(file, "DT=%f\n", &config->DT);
    fscanf(file, "STEP_FORCE=%f\n", &config->STEP_FORCE);
    fscanf(file, "RHO=%f\n", &config->RHO);
    fscanf(file, "ETA=%f\n", &config->ETA);
    fscanf(file, "server_port=%i\n", &config->server_port);
    fscanf(file, "server_ip=%15s\n", config->server_ip);

    logger(log, "Configuration loaded successfully");
    fclose(file);
    return 1;

}
void logger(FILE * handler, const char *message) {
    fprintf(handler, "%s\n", message);
    fflush(handler);
}
void safe_logger(FILE * handler, const char *message) {
    flock(fileno(handler), LOCK_EX);
    fprintf(handler, "%s\n", message);
    fflush(handler);
    flock(fileno(handler), LOCK_UN);
}

