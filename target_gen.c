#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "protocol.h"
#include "utils.h"
#include "fcntl.h"


static volatile sig_atomic_t running = 1;
FILE *log_file;


float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

int main(int argc, char *argv[]) {

    log_file = fopen("log/targets_log.txt","w");
    logger(log_file, "Target Generator Started");
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
    
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
    
    
    srand(time(NULL) ^ getpid());
    
    
    // int next_target_id = 1;
    // const float REACH_RADIUS = 3.0; // Raggio per considerare target raggiunto
    
    
    // PartialState state;
    // memset(&state, 0, sizeof(PartialState));

    int target_count = 0;
    int mapx = config.map_width-5;
    int mapy = config.map_height -5;


    ResizeMessage res;

    while (running) {

        size_t n = read(read_fd, &res, sizeof(ResizeMessage));
        if(n ==sizeof(ResizeMessage))
        {
            mapx = res.x;
            mapy = res.y;
            target_count = 0;
        }
        
        if (target_count < MAX_TAR ) {
            Message msg;
            msg.type = 'T';
            msg.data.target.x = random_float(5, mapx);
            msg.data.target.y = random_float(5, mapy);

            msg.data.target.id = target_count;
            msg.data.target.active = 1;
            
            
            ssize_t written = write(write_fd, &msg, sizeof(Message));
            if (written == sizeof(Message)) {
                target_count++;
                char buf[256];
                sprintf(buf, "[TARGET_GEN] Created target at (%.1f, %.1f) [Total: %d]\n",
                        msg.data.target.x, msg.data.target.y, target_count);
                logger(log_file, buf);
            }
        }
        
    }



    
    // // Genera primo target
    // Target current_target;
    // current_target.active = 0;
    // Message msg;
    // msg.type = 'T';
    // msg.data.target.id = next_target_id++;
    // msg.data.target.x = random_float(15.0, 85.0);
    // msg.data.target.y = random_float(15.0, 85.0);
    // msg.data.target.active = 1;
    
    // write(pipe_to_server, &msg, sizeof(Message));
    // current_target = msg.data.target;
    
    // fprintf(stderr, "[TARGET_GEN] Created target %d at (%.1f, %.1f)\n",
    //         current_target.id, current_target.x, current_target.y);
    
    // while (running) {
    //     // Leggi stato dal server per controllare posizione drone
    //     if (pipe_from_server >= 0) {
    //         ssize_t n = read(pipe_from_server, &state, sizeof(PartialState));
            
    //         if (n > 0 && current_target.active) {
    //             // Controlla se drone ha raggiunto il target
    //             float dist = distance(state.drone.x, state.drone.y,
    //                                  current_target.x, current_target.y);
                
    //             if (dist < REACH_RADIUS) {
    //                 fprintf(stderr, "[TARGET_GEN] Target %d REACHED! Distance: %.2fm\n",
    //                         current_target.id, dist);
                    
    //                 // Disattiva target corrente
    //                 msg.type = 'T';
    //                 msg.data.target = current_target;
    //                 msg.data.target.active = 0;
    //                 write(pipe_to_server, &msg, sizeof(Message));
                    
    //                 // Genera nuovo target dopo breve pausa
    //                 usleep(500000); // 500ms
                    
    //                 msg.data.target.id = next_target_id++;
    //                 msg.data.target.x = random_float(15.0, 85.0);
    //                 msg.data.target.y = random_float(15.0, 85.0);
    //                 msg.data.target.active = 1;
                    
    //                 write(pipe_to_server, &msg, sizeof(Message));
    //                 current_target = msg.data.target;
                    
    //                 fprintf(stderr, "[TARGET_GEN] Created target %d at (%.1f, %.1f)\n",
    //                         current_target.id, current_target.x, current_target.y);
    //             }
    //         }
    //     }
        
    // }
    
    close(write_fd);
    
    logger(log_file, "[TARGET_GEN] Terminated");
    
    return 0;
}
