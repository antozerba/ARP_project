#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "utils.h"
#include "protocol.h"
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define CLOCK_TICK 300000

void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) ;

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

    //RENDERE PIPE NON BLOCCANTE: questo mi permette di non dover invare a dynamic dal server ogni volta.
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    WorldState state;
    memset(&state, 0, sizeof(WorldState));
    state.drone.x = config.drone_x;
    state.drone.y = config.drone_y;


    for(;;){
        ssize_t bytes_read = read(read_fd, &state, sizeof(WorldState));
        
        char buffer[200];
        
        if(bytes_read == sizeof(WorldState)) {
            
        sprintf(buffer, "DRONE STATE RECEIVED - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        } 
        else if(bytes_read == -1 && errno == EAGAIN) {
            // Nessun nuovo dato, normale, continua con stato precedente
            // Non serve loggare, succede sempre
        }
        else if(bytes_read == 0) {
            // Pipe chiusa, server disconnesso
            logger(log_file, "Server disconnected, exiting");
            break;
        }
        else if(bytes_read > 0) {
            // Lettura parziale (non dovrebbe succedere con pipe piccole)
            sprintf(buffer, "Partial read: %zd bytes, expected %zu", 
                    bytes_read, sizeof(WorldState));
            logger(log_file, buffer);
            // Ignora e continua
        }
        else {
            // Altro errore
            sprintf(buffer, "Read error: errno=%d", errno);
            logger(log_file, buffer);
            break;
        }

        // Calcola forze repulsive dagli ostacoli (Latombe)
        float frx = 0.0f, fry = 0.0f;
        compute_repulsive_forces(&state, &config, &frx, &fry);
        
        // Forza totale = forza comando + forza repulsiva
        float total_fx = state.drone.fx + frx;
        float total_fy = state.drone.fy + fry;
        
        // Log forze
        sprintf(buffer, "Forces - cmd: (%.2f,%.2f), repulsive: (%.2f,%.2f), total: (%.2f,%.2f)",
                state.drone.fx, state.drone.fy, frx, fry, total_fx, total_fy);
        logger(log_file, buffer);
        
        //update drone position based on forces
        float ax = (total_fx/ config.MASS) - (config.K * state.drone.vx / config.MASS);
        float ay = (total_fy / config.MASS) - (config.K * state.drone.vy / config.MASS);
        
        state.drone.vx += ax * config.DT;
        state.drone.vy += ay * config.DT;
        
        state.drone.x += state.drone.vx * config.DT;
        state.drone.y += state.drone.vy * config.DT;

        // // clamp within map
        // if (state.drone.x < 0) { state.drone.x = 0; state.drone.vx = 0; }
        // if (state.drone.y < 0) { state.drone.y = 0; state.drone.vy = 0; }
        // if (state.drone.x > config.map_width- 1) { state.drone.x = (float)(config.map_width - 1); state.drone.vx = 0; }
        // if (state.drone.y > config.map_height - 1) { state.drone.y = (float)(config.map_height - 1); state.drone.vy = 0; }

        Message msg;
        msg.type = MSG_DRONE_UPDATE;
        msg.data.drone = state.drone;
        // char buffer_state[200];
        // sprintf(buffer_state, "Updated DRONE STATE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
        //         msg.data.drone.x, msg.data.drone.y, msg.data.drone.vx, msg.data.drone.vy, msg.data.drone.fx, msg.data.drone.fy); 
        // logger(log_file, buffer_state);
        sprintf(buffer, "Updated DRONE STATE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        write(write_fd, &msg, sizeof(Message));
        logger(log_file, "Updated drone state sent to server");
        usleep(config.DT*CLOCK_TICK);

    }


    return 0;
}

void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) {
    *frx = 0.0f;
    *fry = 0.0f;
    
    float drone_x = state->drone.x;
    float drone_y = state->drone.y;
    
    // Itera su tutti gli ostacoli attivi
    for(int i = 0; i < MAX_OBS; i++) {
        if(!state->obstacles[i].active) continue;
        
        float obs_x = (float)state->obstacles[i].x;
        float obs_y = (float)state->obstacles[i].y;
        
        // Distanza drone-ostacolo
        float dist = sqrt((state->drone.x-obs_x)*(state->drone.x-obs_x)+(state->drone.y-obs_y)*(state->drone.y-obs_y));
        // if(dist < 0.1f) {
    
        //     dist = 0.1f;
        // }
        
        // Forza repulsiva di Latombe:
        // F_rep = ETA * (1/d - 1/RHO) * (1/d^2) * direzione
        // dove:
        // - ETA: intensità della forza repulsiva
        // - RHO: raggio di influenza dell'ostacolo
        // - d: distanza dall'ostacolo
        
        if(dist < config->RHO) {
            // Calcola l'intensità della forza
            float repulsion_magnitude = config->ETA * 
                                       (1.0f / dist - 1.0f / config->RHO) * 
                                       (1.0f / (dist * dist));
            
            // Direzione del vettore repulsivo (da ostacolo verso drone)
            float dx = drone_x - obs_x;
            float dy = drone_y - obs_y;
            
            // Normalizza il vettore direzione
            float norm = sqrt(dx * dx + dy * dy);
            if(norm > 0.001f) {
                dx /= norm;
                dy /= norm;
            }
            
            // Aggiungi la componente repulsiva
            *frx += repulsion_magnitude * dx;
            *fry += repulsion_magnitude * dy;
            
            // Log per debug
            char log_buf[200];
            sprintf(log_buf, "Obstacle %d at (%.1f,%.1f) dist=%.2f, F_rep=(%.2f,%.2f)", 
                    i, obs_x, obs_y, dist, repulsion_magnitude * dx, repulsion_magnitude * dy);
            logger(log_file, log_buf);
        }
    }
}