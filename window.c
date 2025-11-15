#include <ncurses.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"

FILE* log_file;

WINDOW *create_newwin(int height, int width, int starty, int startx) ;
void update_window(WINDOW *win, int drone_x, int drone_Y);

int main(int argc, char **argv) {
    
    log_file = fopen("log/window_log.text","w");
    logger(log_file, "Window started");

    for(int i=0; i<argc; i++){
        logger(log_file, argv[i]);
    }
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    if(read_fd_char == NULL || write_fd_char == NULL){
        logger(log_file, "Error getting file descriptors from environment variables");
        return 1;
    }
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);
    char buffer[50];
    sprintf(buffer, "Read FD: %d, Write FD: %d", read_fd, write_fd);
    logger(log_file, buffer);
    char message[10];
    read(read_fd, message, sizeof(message));
    sprintf(buffer, "Received message: %s", message);
    logger(log_file, buffer);

    Config config = {};
    if(!load_config("/config/parameters.txt", &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    char conf_buf[100];  
    sprintf(conf_buf, "Config - map_width: %lf, map_height: %lf, drone_x: %lf, drone_y: %lf",
            config.map_width, config.map_height, config.drone_x, config.drone_y);
    logger(log_file, conf_buf);



    WINDOW *win;
    int startx, starty, width, height;
    initscr();
    refresh();
    cbreak();
    keypad(stdscr, TRUE);
    starty = 0;
	startx = 0;
    curs_set(0);
    win = create_newwin(config.map_height, config.map_width, starty, startx);
    int drone_x = 1;
    int drone_y = 1;
    //for(int i=0; i<10; i++){
        //update_window(win, drone_x + i, drone_y);
        //usleep(100000);
    //}
    getch();
    delwin(win);
    endwin();  

    return 0;
}

WINDOW *create_newwin(int height, int width, int starty, int startx) {
    WINDOW *local_win;
    local_win = newwin(height, width, starty, startx);
    char message[100];
    sprintf(message, "Creating new window at (%d,%d) with dimensions (%d,%d)", starty, startx, height, width);
    logger(log_file, message);
    box(local_win, 0 , 0);
    wrefresh(local_win);
    return local_win;
}

void update_window(WINDOW *win, int drone_x, int drone_Y) {
    wclear(win);
    box(win, 0 , 0);
    mvwaddch(win, drone_Y, drone_x, 'X');
    wrefresh(win);
}
