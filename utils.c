#include "utils.h"
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include "protocol.h"
#include <string.h>

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


ssize_t write_all(int fd, const void *buf, size_t size) {
    size_t total = 0;
    const char *p = buf;
    while (total < size) {
        ssize_t n = write(fd, p + total, size - total);
        if (n <= 0) return n; // errore o pipe chiusa
        total += n;
    }
    return total;
}
ssize_t read_all(int fd, void *buf, size_t size) {
    size_t total = 0;
    char *p = buf;
    while (total < size) {
        ssize_t n = read(fd, p + total, size - total);
        if (n == 0) break;        // pipe chiusa
        if (n < 0 && errno == EAGAIN) continue;
        if (n < 0) return -1;     // errore
        total += n;
    }
    return total;
}
// Serializza WorldState in buffer !!!!DA MODIFICARE OGNI VOLTA CHE CAMBIO WORLD
//metodi implementato per evitare padding
size_t serialize_worldstate(const WorldState *state, char *buf) {
    //DRONE
    char *p = buf;
    memcpy(p, &state->drone, sizeof(Drone)); p += sizeof(Drone);
    //OBSCACLES
    for (int i = 0; i < 10; i++) {
        memcpy(p, &state->obstacles[i], sizeof(Obstacle));
        p += sizeof(Obstacle);
    }
    //NUM_OBS
    memcpy(p, &state->num_obstacles, sizeof(int));
    p += sizeof(int);
    return p - buf;
}
void deserialize_worldstate(const char *buf, WorldState *state) {
    const char *p = buf;
    //DRONE
    memcpy(&state->drone, p, sizeof(Drone)); p += sizeof(Drone);
    //OBS
    for (int i = 0; i < 10; i++) {
        memcpy(&state->obstacles[i], p, sizeof(Obstacle));
        p += sizeof(Obstacle);
    }
    //NUM_OBS
    memcpy(&state->num_obstacles, p, sizeof(int));
}