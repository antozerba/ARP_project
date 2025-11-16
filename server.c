#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <sys/select.h>

FILE * log_file;

int main(int argc, char **argv){

    log_file = fopen("log/server_log.text","w");
    logger(log_file, "Server started");

    //PIPE from ENV
    char * read_input_fd_char = getenv("IN_INPUT_FD");
    char * read_window_fd_char = getenv("IN_WINDOW_FD");
    char * write_input_fd_char = getenv("OUT_INPUT_FD");
    char * write_window_fd_char = getenv("OUT_WINDOW_FD");

    if(read_input_fd_char == NULL || write_input_fd_char == NULL || read_window_fd_char == NULL || write_window_fd_char == NULL){
        logger(log_file, "Error getting file descriptors from environment variables"); 
        return 1;
    }
    int read_input_fd = atoi(read_input_fd_char);
    int write_input_fd = atoi(write_input_fd_char);
    int read_window_fd = atoi(read_window_fd_char);
    int write_window_fd = atoi(write_window_fd_char);

    int initial_drone_x = 5;
    int initial_drone_y = 5;
    Drone drone = {initial_drone_x, initial_drone_y};


    for(;;){
        //SELECT
        fd_set read_fds;
        fd_set write_fds;
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(read_input_fd, &read_fds);
        FD_SET(read_window_fd, &read_fds);
        FD_SET(write_input_fd, &write_fds);
        FD_SET(write_window_fd, &write_fds);

        int max_fd = 0;
        if(read_input_fd > max_fd) max_fd = read_input_fd;
        if(read_window_fd > max_fd) max_fd = read_window_fd;
        if(write_input_fd > max_fd) max_fd = write_input_fd;
        if(write_window_fd > max_fd) max_fd = write_window_fd;

        logger(log_file, "Server waiting for data..."); 
        int r = select(max_fd + 1, &read_fds, NULL, NULL, NULL); //ritorna numero di fd pronti

        if(r == -1){
            logger(log_file, "Error in select");
            return 1;
        }
        if(FD_ISSET(read_input_fd, &read_fds)){

            char msg_from_input[2];
            ssize_t bytes_read = read(read_input_fd, msg_from_input, sizeof(msg_from_input));
            char log_buff[50];
            sprintf(log_buff, "COMANDO RECEIVED FROM INPUT: %s", msg_from_input);
            logger(log_file, log_buff);
            if (strcmp(msg_from_input, "w") == 0) {
                drone.y -= 1.0;
            } else if (strcmp(msg_from_input, "s") == 0) {
                drone.y += 1.0;
            } else if (strcmp(msg_from_input, "a") == 0) {
                drone.x -= 1.0;
            } else if (strcmp(msg_from_input, "d") == 0) {
                drone.x += 1.0;
            } else if (strcmp(msg_from_input, "q") == 0)
            {
                /* code */
                
            }
            //invio Drone a window
            write(write_window_fd, &drone, sizeof(struct Drone));
            logger(log_file, "DRONE POSITION SENT TO WINDOW");
        }   
    }

    





    return 0;
    
}