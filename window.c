#include <ncurses.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include "utils.h"
#include "protocol.h"
#include <fcntl.h>
#include <errno.h>


#define CLOCK_TICK 33000
#define MARGIN_Y 0.5  // margine verticale
#define MARGIN_X 1  // margine orizzontale

void resize_win(WINDOW *win);
WINDOW *create_newwin(int height, int width, int starty, int startx) ;
void update_window(WINDOW *win, int drone_x, int drone_Y);
void delete_drone(WINDOW *win, int drone_x, int drone_y);
void update_drone(WINDOW *win, int drone_x, int drone_y);

FILE* log_file;

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
    char conf_buf[200];  
    sprintf(conf_buf, "Config - map_width: %lf, map_height: %lf, drone_x: %lf, drone_y: %lf, MASS: %f, K: %f, DT: %f, STEP_FORCE: %f, RHO: %f, ETA: %f",
            config.map_width, config.map_height, config.drone_x, config.drone_y, config.MASS, config.K, config.DT, config.STEP_FORCE, config.RHO, config.ETA);
    logger(log_file, conf_buf);

    //rendo read non blocking per gestire il frame rate a schermo con usleep
    // int flags = fcntl(read_fd, F_GETFL, 0);
    // fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    WINDOW *win;
    int startx, starty, width, height;
    initscr();
    refresh();
    cbreak();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE); // Non bloccare l'attesa dell'input
    curs_set(0);

    starty = 0;
	startx = 0;
    int height_max, width_max;
    getmaxyx(stdscr, height_max, width_max);
    win = create_newwin(height_max, width_max, starty, startx);

    Drone drone = {0, 0, 0, 0, 0, 0};

    for(;;){
//         int ch;
//         if((ch = getch()) == KEY_RESIZE) {
//             resize_win(win);
//             logger(log_file, "Window resized");
//         }

//         delete_drone(win, (int)drone.x, (int)drone.y);
        
        
//         ssize_t bytes_read = read(read_fd, &drone, sizeof(struct Drone));
//         if(bytes_read == sizeof(struct Drone)) {
//             // Nuovo dato ricevuto, logga
//             char input[100];
//             sprintf(input, "DRONE POSITION - x: %lf, y: %lf, vx: %lf, vy: %lf", 
//                     drone.x, drone.y, drone.vx, drone.vy);
//             logger(log_file, input);
//         } 
//         else if(bytes_read == -1 && errno == EAGAIN) {
//             // Nessun dato disponibile, continuo
//         }
//         else if(bytes_read == 0) {
//             logger(log_file, "Server disconnected");
//             break;
//         }


//         update_drone(win, (int)drone.x, (int)drone.y);
// //        update_window(win, drone.x, drone.y);
//         usleep(CLOCK_TICK); 

        //V1
        int ch = getch();
        if(ch  == KEY_RESIZE) {
            resize_win(win);
            logger(log_file, "Window resized");
        }
        delete_drone(win, (int)drone.x, (int)drone.y);
        read(read_fd, &drone, sizeof(struct Drone));
        update_drone(win, (int)drone.x, (int)drone.y);
//        update_window(win, drone.x, drone.y);
        char input[100];
        sprintf(input, "DRONE POSITION - x: %lf, y: %lf, vx: %lf, vy: %lf, fx: %lf, fy: %lf", drone.x, drone.y,
                drone.vx, drone.vy, drone.fx, drone.fy);
        logger(log_file, input);
    }
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

void update_window(WINDOW *win, int drone_x, int drone_y) {
    //wclear(win);
    //box(win, 0 , 0);
    //mvwaddch(win, drone_y, drone_x, 'X');
    //wrefresh(win);
    
    mvwprintw(win, 1, 1, "Drone Position: (%d, %d)    ", drone_x, drone_y);
}
void delete_drone(WINDOW *win, int drone_x, int drone_y) {
    mvwaddch(win, drone_y, drone_x, ' ');
}
void update_drone(WINDOW *win, int drone_x, int drone_y) {
    mvwaddch(win, drone_y, drone_x, 'X');
    wrefresh(win);
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
    wrefresh(win);

    // Log
    char msg[100];
    sprintf(msg, "Resized window to %d x %d at (%d,%d)", new_height, new_width, starty, startx);
    logger(log_file, msg);

}
