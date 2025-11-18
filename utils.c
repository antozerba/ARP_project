#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int load_config(const char *filename, struct Config *config) {
    FILE * log= fopen("log/utils_log.text","w");
    logger(log, "Loading configuration");
    FILE *file = fopen("config/parameters.txt", "r");
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
    logger(log, "Configuration loaded successfully");
    fclose(file);
    return 1;

}
void logger(FILE * handler, const char *message) {
    fprintf(handler, "%s\n", message);
    fflush(handler);
}

ssize_t safe_read(int fd, void *buf, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t n = read(fd, (char*)buf + total, size - total);
        if (n <= 0) return n;  // errore o EOF
        total += n;
    }
    return total;
}
