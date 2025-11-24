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


// #define CLOCK_TICK 50000 //sincronizzato con dynamic

FILE * log_file;

void init_world_state(WorldState * state){
    memset(state, 0, sizeof(WorldState));
    state->drone.x = 5.0f;
    state->drone.y = 5.0f;
}

void handle_input_command(WorldState *state, InputCommand *cmd) {
    switch(cmd->type) {
        case CMD_BRAKE:
            state->drone.fx = 0;
            state->drone.fy = 0;
            fprintf(stderr, "[SERVER] Brake applied\n");
            break;
            
        case CMD_PAUSE:
            // state->paused = !state->paused;
            // fprintf(stderr, "[SERVER] Pause toggled: %d\n", state->paused);
            break;
            
        case CMD_RESET:
            init_world_state(state);
            fprintf(stderr, "[SERVER] World reset\n");
            break;
            
        case CMD_QUIT:
            fprintf(stderr, "[SERVER] Quit requested\n");
            // running = 0;
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
            break;
        // case 'T':
            // Aggiorna target
        //     break;
        // case 'O':
            // Aggiorna ostacolo
        //     break;
        default:
            logger(log_file, "Unknown message type received");
            break;
    }
}


int main(int argc, char **argv){

    log_file = fopen("log/server_log.text","w");
    logger(log_file, "Server started");

    //PIPE from ENV
    char * read_input_fd_char = getenv("IN_INPUT_FD");
    char * read_window_fd_char = getenv("IN_WINDOW_FD");
    char * write_input_fd_char = getenv("OUT_INPUT_FD");
    char * write_window_fd_char = getenv("OUT_WINDOW_FD");
    char * read_dynamic_fd_char = getenv("IN_DYNAMIC_FD");
    char * write_dynamic_fd_char = getenv("OUT_DYNAMIC_FD");

    int read_input_fd = atoi(read_input_fd_char);
    int write_input_fd = atoi(write_input_fd_char);
    int read_window_fd = atoi(read_window_fd_char);
    int write_window_fd = atoi(write_window_fd_char);
    int read_dynamic_fd = atoi(read_dynamic_fd_char);
    int write_dynamic_fd = atoi(write_dynamic_fd_char);

    WorldState state;
    init_world_state(&state);    

    for(;;){

        //SELECT
        
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(read_input_fd, &read_fds);
        FD_SET(read_window_fd, &read_fds);
        FD_SET(read_dynamic_fd, &read_fds);

        int max_fd = 0;
        if(read_input_fd > max_fd) max_fd = read_input_fd;
        if(read_window_fd > max_fd) max_fd = read_window_fd;
        if(read_dynamic_fd > max_fd) max_fd = read_dynamic_fd;

        int r = select(max_fd + 1, &read_fds, NULL, NULL, NULL); //ritorna numero di fd pronti

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
        }   
        
        
        if(FD_ISSET(read_dynamic_fd, &read_fds)){
            Message msg;
            ssize_t bytes_read = read(read_dynamic_fd, &msg, sizeof(Message));
            if(bytes_read != sizeof(Message)){
                logger(log_file, "Error reading Message from dynamic");
                continue;
            }
            handle_message(&state, &msg);
            char buffer[200];
            sprintf(buffer, "DRONE RECEIVED FROM DYNAMIC - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                    msg.data.drone.x, msg.data.drone.y, msg.data.drone.vx, msg.data.drone.vy, msg.data.drone.fx, msg.data.drone.fy);  

            logger(log_file, buffer);

            //invio a window
            write(write_window_fd, &state.drone, sizeof(struct Drone));
        }
        
        //invio a dynamic
        write(write_dynamic_fd, &state, sizeof(struct WorldState));
        // usleep(CLOCK_TICK);

    }

    





    return 0;
    
}