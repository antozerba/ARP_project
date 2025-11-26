#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include <ncurses.h>
#include <string.h>
#include "protocol.h"
#include "fcntl.h"

#define CLOCK_TICK 16000
WINDOW *create_input_win(int height, int width, int starty, int startx);
WINDOW *create_input_win_2(int height, int width, int starty, int startx) ;
WINDOW *create_dynamics_win(int height, int width, int starty, int startx);
void update_dynamics_win(WINDOW *win, float pos_x, float pos_y, float vel_x, float vel_y, float force_x, float force_y);

int map_key_to_command(int key, InputCommand *cmd) {
    float force_step = 1.0; // 1 Newton per pressione
    
    memset(cmd, 0, sizeof(InputCommand));
    
    switch(key) {
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

    
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    //Ncurses init
    initscr();
    refresh();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // Non-blocking input
    curs_set(0);

    
    // WINDOW *win = create_input_win(20, 60, 0, 0);

    // Crea le due finestre affiancate
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int win_width = max_x / 2 - 2;
    int win_height = max_y - 2;
    WINDOW *input_win = create_input_win(win_height, win_width, 1, 1);
    WINDOW *dynamics_win = create_dynamics_win(win_height, win_width, 1, win_width + 3);
    Drone drone;

    //dynamic info
    float pos_x =0;
    float pos_y =0;
    float  vel_x =0;
    float vel_y =0;
    float fx =0;
    float fy =0;




    for(;;){

        size_t n = read(read_fd, &drone, sizeof(Drone));
        if(n== sizeof(Drone)){
            pos_x = drone.x;
            pos_y = drone.y;
            vel_x = drone.vx;
            vel_y = drone.vy;
            fx = drone.fx;
            fy = drone.fy;
            char ops[256];
            snprintf(ops, sizeof(ops),
                        "DRONE RECEIVED FROM SEVRVER - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                        drone.x, drone.y,
                        drone.vx, drone.vy,
                        drone.fx, drone.fy);
            logger(log_file, ops);
        }

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
        update_dynamics_win(dynamics_win, pos_x,pos_y, vel_x,vel_y, fx, fy);
    }

    endwin();
    fclose(log_file);
    return 0;
}
WINDOW *create_input_win(int height, int width, int starty, int startx) {
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    box(win, 0 , 0);     
    mvwprintw(win,starty, startx + width*3/6, "INPUT WINDOW"); 
    mvwaddch(win,starty + height*2/6, startx+ width* 2/6, 'q');
    mvwaddch(win,starty + height*2/6, startx+ width *3/6, 'w');
    mvwaddch(win,starty + height*2/6, startx+ width *4/6, 'e');
    mvwaddch(win,starty + height*3/6, startx+ width* 2/6, 'a');
    mvwaddch(win,starty + height*3/6, startx+ width *3/6, 's');
    mvwaddch(win,starty + height*3/6, startx+ width *4/6, 'd');
    mvwaddch(win,starty + height*4/6, startx+ width* 2/6, 'z');
    mvwaddch(win,starty + height*4/6, startx+ width *3/6, 'x');
    mvwaddch(win,starty + height*4/6, startx+ width *4/6, 'c');


    wrefresh(win);         
    return win;
}
WINDOW *create_input_win_2(int height, int width, int starty, int startx) {
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    box(win, 0, 0);
    
    // Titolo
    mvwprintw(win, 1, (width - 13) / 2, "INPUT DISPLAY");
    
    // Calcola le dimensioni della griglia 3x3
    int grid_width = 30;
    int grid_height = 12;
    int grid_start_x = (width - grid_width) / 2;
    int grid_start_y = (height - grid_height) / 2;
    
    // Disegna la griglia 3x3
    // Linee orizzontali
    for (int i = 0; i <= 3; i++) {
        mvwhline(win, grid_start_y + i * 4, grid_start_x, ACS_HLINE, grid_width);
        // Intersections
        if (i > 0 && i < 3) {
            for (int j = 0; j <= 3; j++) {
                mvwaddch(win, grid_start_y + i * 4, grid_start_x + j * 10, ACS_PLUS);
            }
        }
    }
    
    // Linee verticali
    for (int i = 0; i <= 3; i++) {
        mvwvline(win, grid_start_y, grid_start_x + i * 10, ACS_VLINE, grid_height);
    }
    
    // Angoli della griglia
    mvwaddch(win, grid_start_y, grid_start_x, ACS_ULCORNER);
    mvwaddch(win, grid_start_y, grid_start_x + grid_width, ACS_URCORNER);
    mvwaddch(win, grid_start_y + grid_height, grid_start_x, ACS_LLCORNER);
    mvwaddch(win, grid_start_y + grid_height, grid_start_x + grid_width, ACS_LRCORNER);
    
    // Aggiungi i tasti nella griglia
    // Riga 1 - Q W E
    mvwprintw(win, grid_start_y + 2, grid_start_x + 4, "Q");
    mvwprintw(win, grid_start_y + 1, grid_start_x + 3, "\\");
    mvwprintw(win, grid_start_y + 2, grid_start_x + 3, "|");
    
    mvwprintw(win, grid_start_y + 2, grid_start_x + 15, "W");
    mvwprintw(win, grid_start_y + 1, grid_start_x + 15, "|");
    mvwprintw(win, grid_start_y + 3, grid_start_x + 15, "^");
    
    mvwprintw(win, grid_start_y + 2, grid_start_x + 26, "E");
    mvwprintw(win, grid_start_y + 1, grid_start_x + 26, "/");
    mvwprintw(win, grid_start_y + 2, grid_start_x + 27, "|");
    
    // Riga 2 - A S D
    mvwprintw(win, grid_start_y + 6, grid_start_x + 4, "A");
    mvwprintw(win, grid_start_y + 6, grid_start_x + 2, "<-");
    
    mvwprintw(win, grid_start_y + 6, grid_start_x + 15, "S");
    mvwprintw(win, grid_start_y + 5, grid_start_x + 15, "X");
    
    mvwprintw(win, grid_start_y + 6, grid_start_x + 26, "D");
    mvwprintw(win, grid_start_y + 6, grid_start_x + 27, "->");
    
    // Riga 3 - Z X C
    mvwprintw(win, grid_start_y + 10, grid_start_x + 4, "Z");
    mvwprintw(win, grid_start_y + 9, grid_start_x + 4, "|");
    mvwprintw(win, grid_start_y + 11, grid_start_x + 3, "\\");
    
    mvwprintw(win, grid_start_y + 10, grid_start_x + 15, "X");
    mvwprintw(win, grid_start_y + 9, grid_start_x + 15, "|");
    mvwprintw(win, grid_start_y + 11, grid_start_x + 15, "v");
    
    mvwprintw(win, grid_start_y + 10, grid_start_x + 26, "C");
    mvwprintw(win, grid_start_y + 9, grid_start_x + 27, "|");
    mvwprintw(win, grid_start_y + 11, grid_start_x + 26, "/");
    
    // Messaggio in basso
    mvwprintw(win, height - 2, 2, "Press p to close everything");
    
    wrefresh(win);
    return win;
}

WINDOW *create_dynamics_win(int height, int width, int starty, int startx) {
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    box(win, 0, 0);
    
    // Titolo
    mvwprintw(win, 1, (width - 16) / 2, "DYNAMICS DISPLAY");
    
    wrefresh(win);
    return win;
}

void update_dynamics_win(WINDOW *win, float pos_x, float pos_y, float vel_x, float vel_y, float force_x, float force_y) {
    // Pulisci il contenuto interno (mantieni il box)
    int height, width;
    getmaxyx(win, height, width);
    
    for(int i = 2; i < height - 1; i++) {
        mvwhline(win, i, 1, ' ', width - 2);
    }
    
    // Ridisegna il titolo
    mvwprintw(win, 1, (width - 16) / 2, "DYNAMICS DISPLAY");
    
    // Posizione iniziale per i dati
    int start_y = 5;
    int start_x = 5;
    
    // Mostra position
    mvwprintw(win, start_y, start_x, "position {");
    mvwprintw(win, start_y + 1, start_x + 2, "x: %.6f", pos_x);
    mvwprintw(win, start_y + 2, start_x + 2, "y: %.6f", pos_y);
    mvwprintw(win, start_y + 3, start_x, "}");
    
    // Mostra velocity
    mvwprintw(win, start_y + 5, start_x, "velocity {");
    mvwprintw(win, start_y + 6, start_x + 2, "x: %.6f", vel_x);
    mvwprintw(win, start_y + 7, start_x + 2, "y: %.6f", vel_y);
    mvwprintw(win, start_y + 8, start_x, "}");
    
    // Mostra force
    mvwprintw(win, start_y + 10, start_x, "force {");
    mvwprintw(win, start_y + 11, start_x + 2, "x: %.6f", force_x);
    mvwprintw(win, start_y + 12, start_x + 2, "y: %.6f", force_y);
    mvwprintw(win, start_y + 13, start_x, "}");
    
    box(win, 0, 0);
    wrefresh(win);
}
