/* ============================================
 * target_gen.c - Generatore di target sequenziali
 * ============================================ */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "protocol.h"

static volatile sig_atomic_t running = 1;


float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

int main(int argc, char *argv[]) {
    
    char *fd_to_server_env = getenv("TARGET_TO_SERVER_FD");
    char *fd_from_server_env = getenv("SERVER_TO_TARGET_FD");
    
    if (!fd_to_server_env) {
        fprintf(stderr, "[TARGET_GEN] ERROR: TARGET_TO_SERVER_FD not set\n");
        return 1;
    }
    
    int pipe_to_server = atoi(fd_to_server_env);
    int pipe_from_server = fd_from_server_env ? atoi(fd_from_server_env) : -1;
    
    srand(time(NULL) ^ getpid());
    
    fprintf(stderr, "[TARGET_GEN] Started\n");
    
    int next_target_id = 1;
    const float REACH_RADIUS = 3.0; // Raggio per considerare target raggiunto
    
    Target current_target;
    current_target.active = 0;
    
    PartialState state;
    memset(&state, 0, sizeof(PartialState));
    
    // Genera primo target
    Message msg;
    msg.type = 'T';
    msg.data.target.id = next_target_id++;
    msg.data.target.x = random_float(15.0, 85.0);
    msg.data.target.y = random_float(15.0, 85.0);
    msg.data.target.active = 1;
    
    write(pipe_to_server, &msg, sizeof(Message));
    current_target = msg.data.target;
    
    fprintf(stderr, "[TARGET_GEN] Created target %d at (%.1f, %.1f)\n",
            current_target.id, current_target.x, current_target.y);
    
    while (running) {
        // Leggi stato dal server per controllare posizione drone
        if (pipe_from_server >= 0) {
            ssize_t n = read(pipe_from_server, &state, sizeof(PartialState));
            
            if (n > 0 && current_target.active) {
                // Controlla se drone ha raggiunto il target
                float dist = distance(state.drone.x, state.drone.y,
                                     current_target.x, current_target.y);
                
                if (dist < REACH_RADIUS) {
                    fprintf(stderr, "[TARGET_GEN] Target %d REACHED! Distance: %.2fm\n",
                            current_target.id, dist);
                    
                    // Disattiva target corrente
                    msg.type = 'T';
                    msg.data.target = current_target;
                    msg.data.target.active = 0;
                    write(pipe_to_server, &msg, sizeof(Message));
                    
                    // Genera nuovo target dopo breve pausa
                    usleep(500000); // 500ms
                    
                    msg.data.target.id = next_target_id++;
                    msg.data.target.x = random_float(15.0, 85.0);
                    msg.data.target.y = random_float(15.0, 85.0);
                    msg.data.target.active = 1;
                    
                    write(pipe_to_server, &msg, sizeof(Message));
                    current_target = msg.data.target;
                    
                    fprintf(stderr, "[TARGET_GEN] Created target %d at (%.1f, %.1f)\n",
                            current_target.id, current_target.x, current_target.y);
                }
            }
        }
        
        usleep(100000); // Controlla ogni 100ms
    }
    
    close(pipe_to_server);
    if (pipe_from_server >= 0) close(pipe_from_server);
    
    fprintf(stderr, "[TARGET_GEN] Terminated\n");
    
    return 0;
}
