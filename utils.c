#include "utils.h"
#include <stdint.h>
#include <stdio.h>


int load_config(const char *filename, struct Config *config) {
    FILE *file = fopen("config.txt", "r");
    if (file == NULL) {
        perror("Error opening config file");
        return 0;
    }
    fscanf(file, "map_width=%lf\n", &config->map_width);
    fscanf(file, "map_height=%lf\n", &config->map_height);
    fscanf(file, "drone_x=%lf\n", &config->drone_x);
    fscanf(file, "drone_y=%lf\n", &config->drone_y);
    fclose(file);
    return 1;

}