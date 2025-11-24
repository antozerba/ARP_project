#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "utils.h"
#include "protocol.h"

#define CLOCK_TICK 10000

FILE * log_file;

int main(int argc, char **argv){

    log_file = fopen("log/dynamic_log.text","w");
    logger(log_file, "Dynamic module started");

    Config config = {};
    if(!load_config("config/parameters.txt", &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }

    //PIPE from ENV
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);

    WorldState state;
    memset(&state, 0, sizeof(WorldState));
    state.drone.x = config.drone_x;
    state.drone.y = config.drone_y;


    for(;;){
        read(read_fd, &state, sizeof(struct WorldState));
        char buffer[200];
        sprintf(buffer, "DRONE STATE RECEIVED - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        //update drone position based on forces
        float ax = (state.drone.fx / config.MASS) - (config.K * state.drone.vx / config.MASS);
        float ay = (state.drone.fy / config.MASS) - (config.K * state.drone.vy / config.MASS);
        state.drone.vx += ax * config.DT;
        state.drone.vy += ay * config.DT;
        
        state.drone.x += state.drone.vx * config.DT;
        state.drone.y += state.drone.vy * config.DT;

        // clamp within map
        if (state.drone.x < 0) { state.drone.x = 0; state.drone.vx = 0; }
        if (state.drone.y < 0) { state.drone.y = 0; state.drone.vy = 0; }
        if (state.drone.x > config.map_width- 1) { state.drone.x = (float)(config.map_width - 1); state.drone.vx = 0; }
        if (state.drone.y > config.map_height - 1) { state.drone.y = (float)(config.map_height - 1); state.drone.vy = 0; }

        Message msg;
        msg.type = MSG_DRONE_UPDATE;
        msg.data.drone = state.drone;
        sprintf(buffer, "Updated DRONE STATE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        write(write_fd, &msg, sizeof(Message));
        logger(log_file, "Updated drone state sent to server");
        usleep(config.DT*CLOCK_TICK);

    }


    return 0;
}