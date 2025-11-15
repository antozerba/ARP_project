#include "utils.h"
#include <stdint.h>
#include <stdio.h>


void logger_utils(FILE * handler, const char *message);

int load_config(const char *filename, struct Config *config) {
    FILE * log= fopen("log/utils_log.text","w");
    logger_utils(log, "Loading configuration");
    FILE *file = fopen("config/parameters.txt", "r");
    if (file == NULL) {
        perror("Error opening config file");
        logger_utils(log, "Error opening config file");
        return 0;
    }
    fscanf(file, "map_width=%lf\n", &config->map_width);
    fscanf(file, "map_height=%lf\n", &config->map_height);
    fscanf(file, "drone_x=%lf\n", &config->drone_x);
    fscanf(file, "drone_y=%lf\n", &config->drone_y);
    fclose(file);
    return 1;

}
void logger_utils(FILE * handler, const char *message) {
    fprintf(handler, "%s\n", message);
    fflush(handler);
}