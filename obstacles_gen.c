#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "protocol.h"
#include "utils.h"


static volatile sig_atomic_t running = 1;

FILE  * log_file;


float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

int main(int argc, char *argv[]) {

    log_file = fopen("/log/obstacles_log.txt", "w");
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
    
    
    srand(time(NULL) ^ getpid());
    
    logger(log_file, "OBSTACLE_GEN Started");
    
    int obstacle_count = 0;
    const int MAX_OBSTACLES = 10;
    
    while (running) {
        // Genera nuovo ostacolo casualmente (probabilità ~10% ogni secondo)
        if (obstacle_count < MAX_OBSTACLES && (rand() % 100) < 10) {
            Message msg;
            msg.type = 'O';
            msg.data.obstacle.x = random_float(5, config.map_width-5);
            msg.data.obstacle.y = random_float(5, config.map_height-5);

            msg.data.obstacle.active = 1;
            
            
            ssize_t written = write(write_fd, &msg, sizeof(Message));
            if (written == sizeof(Message)) {
                obstacle_count++;
                char buf[256];
                sprintf(buf, "[OBSTACLE_GEN] Created obstacle at (%.1f, %.1f) [Total: %d]\n",
                        msg.data.obstacle.x, msg.data.obstacle.y, obstacle_count);
                logger(log_file, buf);
            }
        }
        
        // Possibilità di rimuovere ostacolo (probabilità ~5% ogni secondo)
        // if (obstacle_count > 0 && (rand() % 100) < 5) {
        //     Message msg;
        //     msg.type = 'O';
        //     msg.data.obstacle.active = 0; // Disattiva
        //     msg.data.obstacle.x = 0;
        //     msg.data.obstacle.y = 0;
            
        //     write(pipe_fd, &msg, sizeof(Message));
        //     obstacle_count--;
        //     fprintf(stderr, "[OBSTACLE_GEN] Removed obstacle [Total: %d]\n", obstacle_count);
        // }
        
        usleep(1000000);
    }
    
    close(write_fd);
    logger(log_file, "OBSTACLE_GEN Terminated");
    return 0;
}