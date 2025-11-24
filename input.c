#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include <ncurses.h>
#include <string.h>
#include "protocol.h"

#define CLOCK_TICK 16000
WINDOW *create_input_win(int height, int width, int starty, int startx);
int map_key_to_command(int key, InputCommand *cmd) {
    float force_step = 1.0; // 1 Newton per pressione
    
    memset(cmd, 0, sizeof(InputCommand));
    
    switch(key) {
        // WASD mapping
        case 'w': case 'W':
            cmd->type = CMD_FORCE_UP;
            cmd->force_y = -force_step;
            return 1;
        case 's': case 'S':
            cmd->type = CMD_BRAKE;
            return 1;
        case 'a': case 'A':
            cmd->type = CMD_FORCE_LEFT;
            cmd->force_x = -force_step;
            return 1;
        case 'd': case 'D':
            cmd->type = CMD_FORCE_RIGHT;
            cmd->force_x = force_step;
            return 1;
            
        // Diagonali
        case 'q': case 'Q':
            cmd->type = CMD_FORCE_UP_LEFT;
            cmd->force_x = -force_step * 0.707;
            cmd->force_y = -force_step * 0.707;
            return 1;
        case 'e': case 'E':
            cmd->type = CMD_FORCE_UP_RIGHT;
            cmd->force_x = force_step * 0.707;
            cmd->force_y = -force_step * 0.707;
            return 1;
        case 'z': case 'Z':
            cmd->type = CMD_FORCE_DOWN_LEFT;
            cmd->force_x = -force_step * 0.707;
            cmd->force_y = force_step * 0.707;
            return 1;
        case 'c': case 'C':
            cmd->type = CMD_FORCE_DOWN_RIGHT;
            cmd->force_x = force_step * 0.707;
            cmd->force_y = force_step * 0.707;
            return 1;
            
        // X per giù
        case 'x':
            cmd->type = CMD_FORCE_DOWN;
            cmd->force_y = force_step;
            return 1;
            
        // Comandi speciali
        case ' ':
            cmd->type = CMD_BRAKE;
            return 1;
        case 'p': case 'P':
            cmd->type = CMD_PAUSE;
            return 1;
        case 'r': case 'R':
            cmd->type = CMD_RESET;
            return 1;
        case 'X': // X maiuscola per quit
            cmd->type = CMD_QUIT;
            return 1;
            
        default:
            return 0;
    }
}

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
    char buffer[100];
    sprintf(buffer, "Read FD: %d, Write FD: %d", read_fd, write_fd);
    logger(log_file, buffer);
    //Ncurses init
    initscr();
    refresh();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    WINDOW *win = create_input_win(20, 60, 0, 0);
    for(;;){
        int ch = getch();
        if(ch != ERR) {
            InputCommand cmd;
            if(map_key_to_command(ch, &cmd)) {
                write(write_fd, &cmd, sizeof(InputCommand));   
                sprintf(buffer, "Sent command of type %d with forces fx: %f, fy: %f", cmd.type, cmd.force_x, cmd.force_y);
                logger(log_file, buffer);
            } else {
                sprintf(buffer, "Unmapped key pressed: %d", ch);
                logger(log_file, buffer);    
            }
        }
        // usleep(CLOCK_TICK);
    }
    endwin();
    fclose(log_file);
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
