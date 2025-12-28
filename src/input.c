#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include <ncurses.h>
#include <string.h>
#include "protocol.h"
#include "fcntl.h"
#include <signal.h>
#include <sys/file.h>
#include <time.h>

#define CLOCK_TICK 16000 //clock per gestire frequenza input
WINDOW *create_input_win(int height, int width, int starty, int startx);
WINDOW *create_dynamics_win(int height, int width, int starty, int startx);
void update_dynamics_win(WINDOW *win, float pos_x, float pos_y, float vel_x, float vel_y, float force_x, float force_y, int score);
int map_key_to_command(int key, InputCommand *cmd);
void termination_handler(int signum);

FILE *log_file;
FILE * wd_log_file;
FILE * common_log;
Config config;
volatile sig_atomic_t running = 1;
pid_t watchdog_pid = -1;

void send_heartbeat() {
    if(watchdog_pid > 0) {
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to watchdog");
    }
}

int main(int argc, char **argv) {
    //Logger 
    log_file = fopen("log/input_log.text","w");
    logger(log_file, "Input started");
    wd_log_file = fopen(WD_LOG_PATH, "a");
    common_log = fopen(COMMON_LOG, "a");
    

    //scrittura pid 
    FILE * pid_file = fopen(PID_FILE, "a");
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "input", getpid());
        fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    //Config file
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }



    //FD from env
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    char *watchdog_pid_str = getenv("WATCHDOG_PID");
    if(read_fd_char == NULL || write_fd_char == NULL){
        logger(log_file, "Error getting file descriptors from environment variables");
        return 1;
    }
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);
    watchdog_pid = atoi(watchdog_pid_str);
    char buffer[100];
    sprintf(buffer, "Read FD: %d, Write FD: %d", read_fd, write_fd);
    logger(log_file, buffer);

    // Heartbeat variables for watchdog
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Invia ogni 1.5s

    //Setto read non blocking
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

    // Crea le due finestre affiancate
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int win_width = max_x / 2 - 2;
    int win_height = max_y - 2;
    WINDOW *input_win = create_input_win(win_height, win_width, 1, 1);
    WINDOW *dynamics_win = create_dynamics_win(win_height, win_width, 1, win_width + 3);
    WorldState state;

    //dynamic info
    float pos_x =0;
    float pos_y =0;
    float  vel_x =0;
    float vel_y =0;
    float fx =0;
    float fy =0;

    //Sigaction for termination signal

    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }

    int iteration =0;

    while(running){
        iteration++;

        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
            char buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "input", "main loop", iteration);
            safe_logger(wd_log_file, buf);
        }
        

        size_t n = read(read_fd, &state, sizeof(WorldState));
        if(n== sizeof(WorldState)){
            pos_x = state.drone.x;
            pos_y = state.drone.y;
            vel_x = state.drone.vx;
            vel_y = state.drone.vy;
            fx = state.drone.fx;
            fy = state.drone.fy;
            char ops[256];
            snprintf(ops, sizeof(ops),
                        "DRONE RECEIVED FROM SEVRVER - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                        state.drone.x, state.drone.y,
                        state.drone.vx, state.drone.vy,
                        state.drone.fx, state.drone.fy);
            logger(log_file, ops);
        }

        int ch = getch();
        if(ch != ERR) {
            InputCommand cmd;
            if(map_key_to_command(ch, &cmd)) {
                write(write_fd, &cmd, sizeof(InputCommand));   
                sprintf(buffer, "Sent command of type %d with forces fx: %f, fy: %f", cmd.type, cmd.force_x, cmd.force_y);
                logger(log_file, buffer);
                char log_buf[256];
                char *t = ctime(&now);
                t[strlen(t) - 1] = '\0';
                sprintf(log_buf, "<%s><%s><%s>", t, "input", buffer);
                safe_logger(common_log, log_buf);
                
            } else {
                sprintf(buffer, "Unmapped key pressed: %d", ch);
                logger(log_file, buffer);    
            }
        }
        update_dynamics_win(dynamics_win, pos_x,pos_y, vel_x,vel_y, fx, fy, state.target_reached);
    }
    logger(log_file, "Input Process Terminated Successfully");
    endwin();
    fclose(log_file);
    return 0;
}
void termination_handler(int signum){
    running = 0;

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
    mvwaddstr(win, starty + height*5/6, startx + width *1/6, "X to close game, s to break");
    wrefresh(win);         
    return win;
}

WINDOW *create_dynamics_win(int height, int width, int starty, int startx) {
    WINDOW *win;
    win = newwin(height, width, starty, startx);
    box(win, 0, 0);
    
    mvwprintw(win, 1, (width - 16) / 2, "DYNAMICS DISPLAY");
    
    wrefresh(win);
    return win;
}

void update_dynamics_win(WINDOW *win, float pos_x, float pos_y, float vel_x, float vel_y, float force_x, float force_y, int score) {
    int height, width;
    getmaxyx(win, height, width);
    
    for(int i = 2; i < height - 1; i++) {
        mvwhline(win, i, 1, ' ', width - 2);
    }
    
    mvwprintw(win, 1, (width - 16) / 2, "DYNAMICS DISPLAY");
    
    int start_y = 5;
    int start_x = 5;
    
    mvwprintw(win, start_y, start_x, "position {");
    mvwprintw(win, start_y + 1, start_x + 2, "x: %.6f", pos_x);
    mvwprintw(win, start_y + 2, start_x + 2, "y: %.6f", pos_y);
    mvwprintw(win, start_y + 3, start_x, "}");
    
    mvwprintw(win, start_y + 5, start_x, "velocity {");
    mvwprintw(win, start_y + 6, start_x + 2, "x: %.6f", vel_x);
    mvwprintw(win, start_y + 7, start_x + 2, "y: %.6f", vel_y);
    mvwprintw(win, start_y + 8, start_x, "}");
    
    mvwprintw(win, start_y + 10, start_x, "force {");
    mvwprintw(win, start_y + 11, start_x + 2, "x: %.6f", force_x);
    mvwprintw(win, start_y + 12, start_x + 2, "y: %.6f", force_y);
    mvwprintw(win, start_y + 13, start_x, "}");

    mvwprintw(win, start_y + 15, start_x, "score: %d", score);

    
    box(win, 0, 0);
    wrefresh(win);
}

int map_key_to_command(int key, InputCommand *cmd) {
    float force_step = config.STEP_FORCE;
    
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
            
        // x per giù
        case 'x':
            cmd->type = CMD_FORCE_DOWN;
            cmd->force_y = force_step;
            return 1;
            
        case 'p': case 'P': //TODO
            cmd->type = CMD_PAUSE;
            return 1;
        case 'r': case 'R': //TODO
            cmd->type = CMD_RESET;
            return 1;
        case 'X': // X maiuscola per quit
            cmd->type = CMD_QUIT;
            return 1;
            
        default:
            return 0;
    }
}

