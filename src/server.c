#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <sys/select.h>
#include "protocol.h"
#include <string.h>
#include <sys/file.h>
#include <signal.h>
#include <time.h>

#define COLL_RAD 0.5

FILE * log_file;
Config config = {};
pid_t watchdog_pid = -1;
int read_input_fd, write_input_fd;
int read_window_fd, write_window_fd;
int read_dynamic_fd, write_dynamic_fd;
int read_obs_fd, write_obs_fd;
int read_tar_fd, write_tar_fd;

void init_world_state(WorldState * state){
    memset(state, 0, sizeof(WorldState));
    state->drone.x = config.drone_x;
    state->drone.y = config.drone_y;
    for (int i = 0; i < MAX_OBS; i++) {
        state->obstacles[i].active = 0;
        state->obstacles[i].x = -1;
        state->obstacles[i].y = -1;
    }
    state->mapx = config.map_width;
    state->mapy = config.map_height;
}

void handle_quit(){
    //chiudo tutti i fds
    close(read_input_fd);
    close(write_input_fd);
    close(read_window_fd);
    close(write_window_fd);
    close(read_dynamic_fd);
    close(write_dynamic_fd);
    close(read_obs_fd);
    close(write_obs_fd);
    close(read_tar_fd);
    close(write_tar_fd);

    //chiusura logger
    fclose(log_file);
    exit(0);

}

void handle_input_command(WorldState *state, InputCommand *cmd) {
    switch(cmd->type) {
        case CMD_BRAKE:
            state->drone.fx = 0;
            state->drone.fy = 0;
            fprintf(stderr, "[SERVER] Brake applied\n");
            break;
            
        case CMD_PAUSE:
            //TODDO da implementare nel second ass
            break;
            
        case CMD_RESET:
            //TODDO da implementare nel second ass
            break;
            
        case CMD_QUIT:
            logger(log_file, "CHIUSURA SERVER");
            handle_quit();

            break;
            
        default:
            // Aggiorna forze comando
            state->drone.fx += cmd->force_x;
            state->drone.fy += cmd->force_y;
            break;
    }
}
void handle_message(WorldState *state, Message *msg) {
    switch(msg->type) {
        case 'D':
            //modifico solo le pos e le vel del drone
            state->drone.x = msg->data.drone.x;
            state->drone.y = msg->data.drone.y;
            state->drone.vx = msg->data.drone.vx;
            state->drone.vy = msg->data.drone.vy;
            state->drone.fx = msg->data.drone.fx;
            state->drone.fy = msg->data.drone.fy;
            break;
        case 'T':
            for (int i = 0; i < MAX_TAR; i++) {
                if (!state->targets[i].active) {
                    state->targets[i] = msg->data.target;
                    state->num_active_targets++;
                    break;
                }
            }
            break;
            
            break;
        case 'O':
            for (int i = 0; i < MAX_OBS; i++) {
                if (!state->obstacles[i].active) {
                    state->obstacles[i] = msg->data.obstacle;
                    state->num_obstacles++;
                    break;
                }
            }
            break;

        default:
            logger(log_file, "Unknown message type received");
            break;
    }
}

void checking_target(WorldState * state){
    int drone_x = state->drone.x;
    int drone_y = state->drone.y;
    for(int i=0; i<MAX_TAR; i++){
        if(state->targets[i].active){
            if(abs(state->targets[i].x - drone_x)< COLL_RAD && abs(state->targets[i].y - drone_y)< COLL_RAD)
            {
                logger(log_file, "TARGET REACHED ;)");
                state->targets[i].active = 0;
                state->num_active_targets--;
                state->target_reached++;
            }
        }
    }

}
void checking_obs(WorldState * state){
    int drone_x = state->drone.x;
    int drone_y = state->drone.y;
    for(int i=0; i<MAX_TAR; i++){
        if(state->obstacles[i].active){
            if(abs(state->obstacles[i].x - drone_x)< COLL_RAD && abs(state->obstacles[i].y - drone_y)< COLL_RAD)
            {
                logger(log_file, "OBS COLLISION :(");
                state->obstacles[i].active = 0;
                state->num_obstacles--;
            }
        }
    }

}
void checking_collisions(WorldState *state){
    // checking_obs(state);
    checking_target(state);
}

void send_heartbeat(){
    if(watchdog_pid > 0){
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to Watchdog");
    }
}

int main(int argc, char **argv){

    //Logger
    log_file = fopen("log/server_log.text","w");
    logger(log_file, "Server started");

    //PIPE from EN
    char * read_input_fd_char = getenv("IN_INPUT_FD");
    char * read_window_fd_char = getenv("IN_WINDOW_FD");
    char * write_input_fd_char = getenv("OUT_INPUT_FD");
    char * write_window_fd_char = getenv("OUT_WINDOW_FD");
    char * read_dynamic_fd_char = getenv("IN_DYNAMIC_FD");
    char * write_dynamic_fd_char = getenv("OUT_DYNAMIC_FD");
    char * write_obs_fd_char = getenv("OUT_OBS_FD");
    char * read_obs_fd_char = getenv("IN_OBS_FD");
    char * write_tar_fd_char = getenv("OUT_TAR_FD");
    char * read_tar_fd_char = getenv("IN_TAR_FD");
    char *watchdog_pid_str = getenv("WATCHDOG_PID");

    watchdog_pid = atoi(watchdog_pid_str);
    read_input_fd = atoi(read_input_fd_char);
    write_input_fd = atoi(write_input_fd_char);
    read_window_fd = atoi(read_window_fd_char);
    write_window_fd = atoi(write_window_fd_char);
    read_dynamic_fd = atoi(read_dynamic_fd_char);
    write_dynamic_fd = atoi(write_dynamic_fd_char);
    read_obs_fd = atoi(read_obs_fd_char);
    write_obs_fd = atoi(write_obs_fd_char);
    read_tar_fd = atoi(read_tar_fd_char);
    write_tar_fd = atoi(write_tar_fd_char);

    //scrittura pid 
    FILE * pid_file = fopen("pid.txt", "a"); //append mode
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "server", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    WorldState state;
    init_world_state(&state);

    // Heartbeat variables for watchdog
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Invia ogni 1.5s
    
    for(;;){
        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
        }
        
        //SELECT
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(read_input_fd, &read_fds);
        FD_SET(read_window_fd, &read_fds);
        FD_SET(read_dynamic_fd, &read_fds);
        FD_SET(read_obs_fd, &read_fds );
        FD_SET(read_tar_fd, &read_fds);

        int max_fd = 0;
        if(read_input_fd > max_fd) max_fd = read_input_fd;
        if(read_window_fd > max_fd) max_fd = read_window_fd;
        if(read_dynamic_fd > max_fd) max_fd = read_dynamic_fd;
        if(read_obs_fd > max_fd) max_fd = read_obs_fd;
        if(read_tar_fd > max_fd) max_fd = read_tar_fd;
        //flag per capire se mandare a dynamics in base a input
        int send_dyn = 0;

        //set timer for select
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;

        int r = select(max_fd + 1, &read_fds, NULL, NULL, &timeout); //ritorna numero di fd pronti
        if(r == -1){
            logger(log_file, "Error in select");
            return 1;
        }

        if(FD_ISSET(read_input_fd, &read_fds)){
            logger(log_file, "Reading InputCommand from input...");
            InputCommand cmd;
            ssize_t bytes_read = read(read_input_fd, &cmd, sizeof(InputCommand));
            if(bytes_read != sizeof(InputCommand)){
                logger(log_file, "Error reading InputCommand from input");
                continue;
            }
            char buffer[200];
            sprintf(buffer, "INPUT COMMAND RECEIVED - type: %d, force_x: %f, force_y: %f", cmd.type, cmd.force_x, cmd.force_y);
            logger(log_file, buffer);
            handle_input_command(&state, &cmd);
            send_dyn = 1;
        }   
        
        //Leggo dynamic
        if(FD_ISSET(read_dynamic_fd, &read_fds)){
            Message msg;
            ssize_t bytes_read = read(read_dynamic_fd, &msg, sizeof(Message));
            if(bytes_read != sizeof(Message)){
                logger(log_file, "Error reading Message from dynamic");
                continue;
            }
            handle_message(&state, &msg);
            char baf[256];
            snprintf(baf, sizeof(baf),
                     "DRONE RECEIVED FROM DYNAMIC - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                     msg.data.drone.x, msg.data.drone.y,
                     msg.data.drone.vx, msg.data.drone.vy,
                     msg.data.drone.fx, msg.data.drone.fy);
            logger(log_file, baf);
        }
         // Leggi messaggi da obstacle generator
        if (FD_ISSET(read_obs_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_obs_fd, &msg, sizeof(Message));
            char ob[256];
            sprintf(ob,
                    "OBS RECEIVED  - obs: x: %lf, y: %lf",
                    msg.data.obstacle.x, msg.data.obstacle.y
                    );
            logger(log_file, ob);


            if (n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        // Ricevo target 
        if (FD_ISSET(read_tar_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_tar_fd, &msg, sizeof(Message));
            char tar[256];
            sprintf(tar,
                    "TARGET RECEIVED  - tar: x: %lf, y: %lf",
                    msg.data.target.x, msg.data.target.y
                    );
            logger(log_file, tar);


            if (n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        //Leggo dalla window se avviene un resize
        if (FD_ISSET(read_window_fd, &read_fds)){
            ResizeMessage msg;
            ssize_t n = read(read_window_fd, &msg, sizeof(ResizeMessage));
            char buf[100];
            
            if (n == sizeof(ResizeMessage)) {
                // write
                sprintf(buf, "Window Rezised: x: %d, y: %d", msg.x, msg.y);
                logger(log_file, buf);
                memset(state.obstacles, 0, sizeof(state.obstacles));
                memset(state.targets, 0, sizeof(state.targets));
                state.num_active_targets = 0;
                state.mapx = msg.x;
                state.mapy = msg.y;
                state.num_obstacles = 0;
                write(write_obs_fd, &msg, sizeof(ResizeMessage));
                write(write_tar_fd, &msg, sizeof(ResizeMessage));
                send_dyn = 1;

            }
            
        }
        //Checking
        checking_collisions(&state);


        //Invio dello stato alla window per il rendering 
        ssize_t written = write(write_window_fd, &state, sizeof(WorldState));
        if(written < 0) {
            logger(log_file, "Error writing to window");
        }
        //Invio del drone a input per gestire la collisione con i muri che invertono forza e vel del drone (possibile alternativa implementare la repulsione questo mi sembra più semplice e bello)
        ssize_t i_write = write(write_input_fd, &state, sizeof(WorldState));
        if(i_write < 0) {
            logger(log_file, "Error writing to input");
        }
        //Log per controllare
        char ops[256];
        snprintf(ops, sizeof(ops),
                    "DRONE SENT TO WINDOW - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                    state.drone.x, state.drone.y,
                    state.drone.vx, state.drone.vy,
                    state.drone.fx, state.drone.fy);
        logger(log_file, ops);
        char wtar[512];
        int pos = 0;
        pos += snprintf(wtar + pos, sizeof(wtar) - pos, "TAR SENT TO DRONE - active targets:");
        for (int ti = 0; ti < MAX_TAR && pos < (int)sizeof(wtar); ++ti) {
            if (state.targets[ti].active) {
            pos += snprintf(wtar + pos, sizeof(wtar) - pos,
                            " [%d]=(%.2f,%.2f)",
                            state.targets[ti].id,
                            state.targets[ti].x, state.targets[ti].y);
            }
        }
        logger(log_file, wtar);
        //Invio a dynamic solo se ho ricevuto da input per limitare il traffico
        if(send_dyn){
        //invio a dynamic
        write(write_dynamic_fd, &state, sizeof(WorldState));
        logger(log_file, "DRONE SENT TO DYNAMICS");
        }
        

    }
    
    fclose(log_file);
    return 0;
    
}