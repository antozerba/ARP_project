#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "protocol.h"
#include "utils.h"
#include <fcntl.h>

static volatile sig_atomic_t running = 1;

FILE  * log_file;


float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

int main(int argc, char *argv[]) {

    //Loggeer
    log_file = fopen("log/obstacles_log.txt", "w");
    logger(log_file, "OBS Started");
    //Config
    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    //PIPE from ENV
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);
    
    //Setto la read non blocking per gestire il caso Resize
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
    
    srand(time(NULL) ^ getpid());
    
    logger(log_file, "OBSTACLE_GEN Started");
    
    int obstacle_count = 0;
    const int MAX_OBSTACLES = MAX_OBS;
    int mapx = config.map_width-5 ;
    int mapy = config.map_height-5;
    ResizeMessage res;

    while (running) {
        //Caso rezise
        char buf[50];
        sprintf(buf, "WINDOW SIZE: x:%d, y:%d", mapx, mapy);
        logger(log_file, buf);
        size_t n = read(read_fd, &res, sizeof(ResizeMessage));
        if(n ==sizeof(ResizeMessage))
        {
            logger(log_file, "ENTRO");
            mapx = res.x;
            mapy = res.y;
            obstacle_count = 0;
        }
        //Creazione ostacolo
        if (obstacle_count < MAX_OBSTACLES ) {
            Message msg;
            msg.type = 'O';
            msg.data.obstacle.x = random_float(5, mapx-5);
            msg.data.obstacle.y = random_float(5, mapy-5);

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
        
    }
    
    close(write_fd);
    logger(log_file, "OBSTACLE_GEN Terminated");
    return 0;
}