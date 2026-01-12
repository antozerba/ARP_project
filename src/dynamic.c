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
#include <signal.h>
#include <sys/file.h>
#include <time.h>

#define CLOCK_TICK 300000
#define MARGIN 1

void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) ;

volatile sig_atomic_t running = 1;
FILE * log_file;
FILE * wd_log_file;
FILE * common_log;
pid_t watchdog_pid = -1;
int network_mode = MODE_STANDALONE;

void termination_handler(int signum){
    //gestione terminazione 
    logger(log_file, "Dynamic Terminated");
    running =0;
    
}
void send_heartbeat(){
    if(watchdog_pid > 0){
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to Watchdog");
    }
}



int main(int argc, char **argv){

    log_file = fopen("log/dynamic_log.text","w");
    logger(log_file, "Dynamic module started");
    wd_log_file= fopen(WD_LOG_PATH, "a");
    common_log = fopen(COMMON_LOG, "a");

    //scrittura pid in pid.txt
    FILE * pid_file = fopen(PID_FILE,"a");
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "dynamic", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }

    //PIPE from ENV
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    char * watchdog_pid_fd = getenv("WATCHDOG_PID");
    int watchdog_pid = atoi(watchdog_pid_fd);
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);


    //Config network
    NetworkConfig nc;
    
    nc.mode = network_mode;
    network_mode = getenv("NETWORK_MODE") ? atoi(getenv("NETWORK_MODE")) : nc.mode; //set nc.mode only if getenv != null
    strcpy(nc.server_ip, config.server_ip);
    nc.serve_port = config.server_port;
    nc.mode = network_mode;
    if(nc.mode == MODE_STANDALONE)
    {
        int flags = fcntl(read_fd, F_GETFL, 0);
        fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    }

    

    WorldState state;
    memset(&state, 0, sizeof(WorldState));
    state.drone.x = config.drone_x;
    state.drone.y = config.drone_y;
    state.mapx = config.map_width;
    state.mapy = config.map_height;

    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }

    //Haartbeat 
    time_t last_hartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // ogni 1.5s

    int iteration = 0;

    while(running){
        iteration +=1;

        time_t now = time(NULL);
        if(difftime(now, last_hartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_hartbeat = now;
            char buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "dynamic", "main", iteration);
            safe_logger(wd_log_file, buf);
        }

        
        ssize_t bytes_read = read(read_fd, &state, sizeof(WorldState));
        
        char buffer[200];
        
        if(bytes_read == sizeof(WorldState)) {
            
        sprintf(buffer, "DRONE STATE RECEIVED - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        char map[50];
        sprintf(map, "MAPPA ATTUALE: xm: %d , ym: %d", state.mapx, state.mapy);
        logger(log_file, map);
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
        // if(nc.mode == MODE_STANDALONE ){

            compute_repulsive_forces(&state, &config, &frx, &fry);
        // }
        
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

        // clamp within map
        if (state.drone.x < MARGIN) { 
            state.drone.x = MARGIN; 
            state.drone.vx = -state.drone.vx; 
            state.drone.fx = -state.drone.fx;
            logger(log_file,"ENTRA");
        }
        if (state.drone.y < MARGIN) { 
            state.drone.y = MARGIN; 
            state.drone.vy = -state.drone.vy; 
            state.drone.fy = -state.drone.fy;
            logger(log_file,"ENTRA");
        }
        if (state.drone.x > state.mapx- MARGIN) {
            state.drone.x = (float) state.mapx -MARGIN;
            state.drone.vx = -state.drone.vx; 
            state.drone.fx = -state.drone.fx;
            logger(log_file,"ENTRA");
        }
        if (state.drone.y > state.mapy- MARGIN) {
            state.drone.y = (float) state.mapy -MARGIN;
            state.drone.vy = -state.drone.vy; 
            state.drone.fy = -state.drone.fy;
            logger(log_file,"ENTRA");
        }

        char buf[50];
        sprintf(buf, "WINDOW SIZE: x:%d, y:%d", state.mapx, state.mapy);
        logger(log_file, buf);

        Message msg;
        msg.type = MSG_DRONE_UPDATE;
        msg.data.drone = state.drone;
        sprintf(buffer, "Updated DRONE STATE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        write(write_fd, &msg, sizeof(Message));
        logger(log_file, "Updated drone state sent to server");
        
        char log_buf[256];
        char *t = ctime(&now);
        t[strlen(t) - 1] = '\0';
        sprintf(log_buf, "<%s><%s><%s>", t, "DYNAMIC", buffer);
        safe_logger(common_log, log_buf);
        

        
        usleep(config.DT*CLOCK_TICK);

    }
    //closing fds
    close(read_fd);
    close(write_fd);
    fclose(wd_log_file);
    fclose(common_log);
    fclose(log_file);
    return 0;
}
void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) {

    float Px = 0.0f;
    float Py = 0.0f;

    float drone_x = state->drone.x;
    float drone_y = state->drone.y;

    for(int i = 0; i < MAX_OBS; i++) {
        if(!state->obstacles[i].active) continue;

        float ox = state->obstacles[i].x;
        float oy = state->obstacles[i].y;

        float dx = drone_x - ox;
        float dy = drone_y - oy;

        float dist = sqrt(dx*dx + dy*dy);
        if(dist < 0.0001f) dist = 0.0001f;

        if(dist < config->RHO) {

            float magn = config->ETA * (1.0/dist - 1.0/config->RHO) * (1.0/(dist*dist));
            char mag[20];
            sprintf(mag, "MAG: %f", magn);
            logger(log_file, mag);
            if (magn > 20){
             magn = 20;
            }

            float nx = dx / dist;
            float ny = dy / dist;

            Px += magn * nx;
            Py += magn * ny;
        }
    }

    if(Px == 0 && Py == 0) {
        *frx = 0;
        *fry = 0;
        return;
    }

    // 8 direzioni normalizzate
    static const float dirs[8][2] = {
        { 1,  0},
        { 1, -1},
        { 0, -1},
        {-1, -1},
        {-1,  0},
        {-1,  1},
        { 0,  1},
        { 1,  1}
    };
    float normdirs[8][2];
    for(int i = 0; i < 8; i++){
        float n = sqrt(dirs[i][0]*dirs[i][0] + dirs[i][1]*dirs[i][1]);
        normdirs[i][0] = dirs[i][0]/n;
        normdirs[i][1] = dirs[i][1]/n;
    }
    float proj[8];
    for(int i = 0; i < 8; i++){
        proj[i] = Px * normdirs[i][0] + Py * normdirs[i][1];
    }
    float maxVal = proj[0];
    int maxIdx = 0;
    for(int i=1; i<8; i++){
        if(proj[i] > maxVal){
            maxVal = proj[i];
            maxIdx = i;
        }
    }

    *frx = maxVal * normdirs[maxIdx][0];
    *fry = maxVal * normdirs[maxIdx][1];
}
