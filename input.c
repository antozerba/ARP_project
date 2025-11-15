#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include <ncurses.h>

WINDOW *create_input_win(int height, int width, int starty, int startx);

FILE *log_file;

int main(int argc, char **argv) {
    //Logger 
    log_file = fopen("log/input_log.text","w");
    logger(log_file, "Input started");
    //FD from env
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
    //Ncurses init
    initscr();
    refresh();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    create_input_win(20, 60, 0, 0);
    getch();




    return 0;
}
WINDOW *create_input_win(int height, int width, int starty, int startx) {
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    box(win, 0 , 0);       /* 0, 0 gives default characters 
                               for the vertical and horizontal 
                               lines */

    mvwprintw(win,starty, startx + width*3/6, "INPUT WINDOW"); 
    mvwaddch(win,starty + height*2/6, startx+ width* 2/6, 'w');
    mvwaddch(win,starty + height*2/6, startx+ width *3/6, 'e');
    mvwaddch(win,starty + height*2/6, startx+ width *4/6, 'r');
    mvwaddch(win,starty + height*3/6, startx+ width* 2/6, 's');
    mvwaddch(win,starty + height*3/6, startx+ width *3/6, 'd');
    mvwaddch(win,starty + height*3/6, startx+ width *4/6, 'f');
    mvwaddch(win,starty + height*4/6, startx+ width* 2/6, 's');
    mvwaddch(win,starty + height*4/6, startx+ width *3/6, 'd');
    mvwaddch(win,starty + height*4/6, startx+ width *4/6, 'f');


    wrefresh(win);         /* Show that box */
    return win;
}
