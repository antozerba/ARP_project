#include <ncurses.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include "protocol.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/file.h>
#include <time.h>


#define CLOCK_TICK 33000
#define MARGIN_Y 0.5  // margine verticale
#define MARGIN_X 2  // margine orizzontale
#define RATIO 1

void resize_win(WINDOW *win);
WINDOW *create_newwin(int height, int width, int starty, int startx) ;
void update_drone();
void update_world();
void draw_obstacles();
void clear_screen();
void draw_targets();

volatile sig_atomic_t running = 1;
FILE* log_file;
FILE* wd_log_file;
FILE * common_log;
WorldState *state;
WorldState *old_state;
WINDOW *win;
pid_t watchdog_pid = -1;
NetworkMode network_mode = MODE_STANDALONE;

void send_heartbeat() {
    if(watchdog_pid > 0) {
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to watchdog");
    }
}


void termination_handler(int signum){
    //gestione terminazione 
    logger(log_file, "Window Terminated");
    running =0;
    
}


int main(int argc, char **argv) {
    
    //Load param from config
    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      return 1;
    }
    //Config network
    NetworkConfig nc;
    
    nc.mode = network_mode;
    network_mode = getenv("NETWORK_MODE") ? atoi(getenv("NETWORK_MODE")) : nc.mode; //set nc.mode only if getenv != null
    strcpy(nc.server_ip, config.server_ip);
    nc.serve_port = config.server_port;
    nc.mode = network_mode;
    //Logger
    if(nc.mode == MODE_SERVER){

        log_file = fopen("log_s/window_log.text","w");
        logger(log_file, "Window started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }
    else if(nc.mode == MODE_CLIENT){

        log_file = fopen("log_c/window_log.text","w");
        logger(log_file, "Window started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }else{

        log_file = fopen("log/window_log.text","w");
        logger(log_file, "Window started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }

    //scrittura pid in pid.txt
    FILE * pid_file = fopen(PID_FILE,"a");
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "window", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    //Fd from env
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    if(read_fd_char == NULL || write_fd_char == NULL){
        logger(log_file, "Error getting file descriptors from environment variables");
        return 1;
    }
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);
    char * watchdog_pid_str = getenv("WATCHDOG_PID");
    watchdog_pid = atoi(watchdog_pid_str);
    char buffer[50];
    sprintf(buffer, "Read FD: %d, Write FD: %d", read_fd, write_fd);
    logger(log_file, buffer);

    logger(log_file, "PARAM PATH: ");
    logger(log_file, PARAM_PATH);
    char conf_buf[200];  
    sprintf(conf_buf, "Config - map_width: %lf, map_height: %lf, drone_x: %lf, drone_y: %lf, MASS: %f, K: %f, DT: %f, STEP_FORCE: %f, RHO: %f, ETA: %f",
            config.map_width, config.map_height, config.drone_x, config.drone_y, config.MASS, config.K, config.DT, config.STEP_FORCE, config.RHO, config.ETA);
    logger(log_file, conf_buf);


    int startx, starty, width, height;
    initscr();
    refresh();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // Non bloccare l'attesa dell'input
    curs_set(0);

    startx = 0;
	starty = 0;
    int height_max, width_max;
    getmaxyx(stdscr, height_max, width_max);
    win = create_newwin(height_max, width_max, starty, startx);
    
    //State
    state = malloc(sizeof(WorldState) );
    memset(state, 0, sizeof(WorldState));
    old_state = malloc(sizeof(WorldState));
    memset(old_state, 0, sizeof(WorldState) );

    //Setto read non blocking per gestire anche le letture parziali e non bloccare il processo
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    // Setting sigaction for SIGTERM
    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }
    
    // Heartbeat variables for watchdog
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Invia ogni 1.5s
    
    // state->drone.x = config.drone_x;
    // state->drone.y = config.drone_y;
    state->drone.x = 100; 
    state->drone.y = 100; 
    memcpy(old_state, state, sizeof(WorldState));
    int newwin_x;
    int newwin_y;
    getmaxyx(win, newwin_y, newwin_x );
    ResizeMessage msg;
    msg.x = newwin_x;
    msg.y = newwin_y;
    write(write_fd, &msg, sizeof(ResizeMessage));

    int iteration = 0;
    while(running){

        iteration++;
        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
            char  buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "window", "main loop", iteration);
            safe_logger(wd_log_file, buf);
        }

        //Rezise case
        int ch = getch();
        if(ch  == KEY_RESIZE) {
            resize_win(win);

            //invio server-blackboard
            int newwin_x;
            int newwin_y;
            getmaxyx(win, newwin_y, newwin_x );
            ResizeMessage msg;
            msg.x = newwin_x;
            msg.y = newwin_y;
            write(write_fd, &msg, sizeof(ResizeMessage));
            char buf[100];
            sprintf(buf, "Window Rezised: x: %d, y: %d", newwin_x, newwin_y);
            logger(log_file, buf);
            char log_buf[256];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(log_buf, "<%s><%s><%s>", t, "WINDOW", buf);
            safe_logger(common_log, log_buf);
            
        }
        ssize_t n = read(read_fd, state, sizeof(WorldState));
        // ssize_t n = read(read_fd, state, sizeof(WorldState));
        if (n == sizeof(WorldState)) {
            // abbiamo nuovi dati: cancella la vecchia posizione e disegna quella nuova
            logger(log_file, "LETTURA COMPLETATA");

            char ops[256];
            snprintf(ops, sizeof(ops),
                        "DRONE RECEIVED- x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                        state->drone.x, state->drone.y,
                        state->drone.vx, state->drone.vy,
                        state->drone.fx, state->drone.fy);

            logger(log_file, ops);
            if(state->mapx != old_state->mapx || state->mapy != old_state->mapy){
                werase(win);
                wrefresh(win);
                wresize(win, state->mapy,  state->mapx);
                logger(log_file, "WINDOW RESIZE FROM SERVER");
            }


            clear_screen();
            update_world();
            
        } else if (n == -1 && errno == EAGAIN) {
            // nessun dato: non fare nulla (mantieni lo schermo così com'è)
        } else if (n == 0) {
            logger(log_file, "Server disconnected");
            break;
        } else {
            // lettura parziale o errore
            char tmp[100];
            sprintf(tmp, "Unexpected read size: %zd (errno=%d)", n, errno);
            logger(log_file, tmp);
        }
        memcpy(old_state, state, sizeof(WorldState));
        char wbuf[20];
        sprintf(wbuf, "SIZE: %d, %d", state->mapx, state->mapy);
        logger(log_file, wbuf);
        
    }
    //closing fds
    close(read_fd);
    close(write_fd);

    delwin(win);
    endwin();  
    free(state);
    free(old_state);
    return 0;
}


void update_world(){
    update_drone();
    draw_obstacles();
    draw_targets();
    wrefresh(win);
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

void update_drone() {
    int term_y = state->drone.y/RATIO;
    int term_x = state->drone.x;
    int win_width ;
    int win_height;
    getmaxyx(win, win_height, win_width);

     
    mvwaddch(win, term_y, state->drone.x, '+');
}
void draw_obstacles() {
    int n_obs = sizeof(state->obstacles) / sizeof(state->obstacles[0]);
    for(int i=0; i < n_obs; i++){
         if (state->obstacles[i].active) {
            int term_y = state->obstacles[i].y/RATIO;
            int term_x = state->obstacles[i].x;
            mvwaddch(win, term_y, term_x, 'O');
            char buf[256];
            sprintf(buf, "Obstacle CREATED: x:%lf, y:%lf", state->obstacles[i].x, state->obstacles[i].y);
            logger(log_file, buf);
        }
    }
    
}
void draw_targets() {
    int n_tar = sizeof(state->targets) / sizeof(state->targets[0]);
    for(int i=0; i < n_tar; i++){
         if (state->targets[i].active) {
            int term_y = state->targets[i].y/RATIO;
            int term_x = state->targets[i].x;
            mvwaddch(win, term_y, term_x, 'T');
            char buf[256];
            sprintf(buf, "Target CREATED: x:%lf, y:%lf", state->targets[i].x, state->targets[i].y);
            logger(log_file, buf);
        }
    }
    
}


void resize_win(WINDOW *win) {
     int H, W;
    getmaxyx(stdscr, H, W);

    // Calcola le nuove dimensioni con margini
    int new_height = H - 2 * MARGIN_Y;
    int new_width  = W - 2 * MARGIN_X;

    // Limiti minimi per la finestra
    if (new_height < 3) new_height = 3;
    if (new_width < 3)  new_width = 3;

    // Calcola posizione per centrare la finestra
    int starty = (H - new_height) / 2;
    int startx = (W - new_width) / 2;

    // Ridimensiona e sposta la finestra
    wresize(win, new_height, new_width);
    mvwin(win, starty, startx);

    // Pulisce e ridisegna
    werase(win);
    box(win, 0, 0);
    update_world();
    wrefresh(win);


}

void clear_screen(){
    werase(win);
    box(win, 0, 0);
}


