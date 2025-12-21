#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <string.h>
#include <sys/types.h>
#include <ncurses.h>
#include "utils.h"

#define MAX_PROCESSES 6
#define TIMEOUT_MULTIPLIER 3  // Timeout = T * 3

typedef struct {
    char name[32];
    pid_t pid;
    time_t last_signal;
    int signal_count;
    int active;
    char status[64];
} ProcessInfo;

ProcessInfo processes[MAX_PROCESSES];
int num_processes = 0;
FILE *log_file;
WINDOW *status_win;
float poll_interval; // T in seconds
volatile sig_atomic_t running = 1;

// Signal handler per ricevere segnali dai processi
void signal_handler(int signo, siginfo_t *info, void *context) {
    pid_t sender_pid = info->si_pid;
    time_t now = time(NULL);
    
    // Trova il processo che ha inviato il segnale
    for(int i = 0; i < num_processes; i++) {
        if(processes[i].pid == sender_pid && processes[i].active) {
            processes[i].last_signal = now;
            processes[i].signal_count++;
            
            char log_buf[200];
            sprintf(log_buf, "[%ld] Signal received from %s (PID %d) - count: %d",
                    now, processes[i].name, sender_pid, processes[i].signal_count);
            logger(log_file, log_buf);
            
            strcpy(processes[i].status, "OK");
            return;
        }
    }
    
    // Segnale da processo sconosciuto
    char log_buf[200];
    sprintf(log_buf, "[%ld] Signal received from unknown process (PID %d)",
            now, sender_pid);
    logger(log_file, log_buf);
}

void register_process(const char *name, pid_t pid) {
    if(num_processes >= MAX_PROCESSES) {
        fprintf(stderr, "Too many processes!\n");
        return;
    }
    
    strncpy(processes[num_processes].name, name, 31);
    processes[num_processes].pid = pid;
    processes[num_processes].last_signal = time(NULL);
    processes[num_processes].signal_count = 0;
    processes[num_processes].active = 1;
    strcpy(processes[num_processes].status, "INIT");
    
    char log_buf[200];
    sprintf(log_buf, "Registered process: %s (PID %d)", name, pid);
    logger(log_file, log_buf);
    
    num_processes++;
}

void check_processes() {
    logger(log_file, "ARRIVO 2");
    time_t now = time(NULL);
    double timeout = poll_interval * TIMEOUT_MULTIPLIER;
    
    for(int i = 0; i < num_processes; i++) {
        if(!processes[i].active) continue;
        
        double elapsed = difftime(now, processes[i].last_signal);
        
        if(elapsed > timeout) {
            // Processo non risponde
            strcpy(processes[i].status, "TIMEOUT");
            
            char alert[256];
            sprintf(alert, "ALERT: Process %s (PID %d) not responding! Last signal %.0f seconds ago",
                    processes[i].name, processes[i].pid, elapsed);
            logger(log_file, alert);
            
            // Beep per alert
            beep();
        } else {
            sprintf(processes[i].status, "OK (%.0fs ago)", elapsed);
            logger(log_file, "OK Process");
        }
    }
}

void update_status_window() {
    werase(status_win);
    box(status_win, 0, 0);
    
    mvwprintw(status_win, 0, 2, " WATCHDOG STATUS ");
    mvwprintw(status_win, 1, 2, "Poll Interval: %.1fs | Timeout: %.1fs",
              poll_interval, poll_interval * TIMEOUT_MULTIPLIER);
    
    mvwprintw(status_win, 3, 2, "%-12s %-8s %-10s %-20s",
              "PROCESS", "PID", "SIGNALS", "STATUS");
    mvwprintw(status_win, 4, 2, "%-12s %-8s %-10s %-20s",
              "------------", "--------", "----------", "--------------------");
    
    for(int i = 0; i < num_processes; i++) {
        if(!processes[i].active) continue;
        
        int color = 1; // Green
        if(strcmp(processes[i].status, "TIMEOUT") == 0) {
            color = 2; // Red
        } else if(strstr(processes[i].status, "ago") != NULL) {
            // Parse seconds
            float secs;
            sscanf(processes[i].status, "OK (%fs ago)", &secs);
            if(secs > poll_interval * 2) {
                color = 3; // Yellow (warning)
            }
        }
        
        wattron(status_win, COLOR_PAIR(color));
        mvwprintw(status_win, 5 + i, 2, "%-12s %-8d %-10d %-20s",
                  processes[i].name,
                  processes[i].pid,
                  processes[i].signal_count,
                  processes[i].status);
        wattroff(status_win, COLOR_PAIR(color));
    }
    
    mvwprintw(status_win, 5 + num_processes + 2, 2, "Press 'q' to quit");
    
    wrefresh(status_win);
}

int main(int argc, char **argv) {

    //scrittura pid
    FILE * wd_pid  = fopen(WATCHDOG_FILE, "w");
    if(wd_pid){
        fprintf(wd_pid, "%d\n", getpid());
        fflush(wd_pid);
        fclose(wd_pid);
    }   

    log_file = fopen("log/watchdog_log.txt", "w");
    logger(log_file, "Watchdog started");
    
    // Leggi configurazione
    Config config;
    if(!load_config("config/parameters.txt", &config)) {
        fprintf(stderr, "Error loading config\n");
        return 1;
    }
    poll_interval = 2.0f; // Default 2 secondi (puoi aggiungere al config)
    
    // Setup signal handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = signal_handler;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGUSR1, &sa, NULL);
    
    // Registra processi (leggi PID da file o environment)
    // Per semplicità, leggi da file PIDs

    usleep(1000000); //to allow porcesso to store pids
    FILE *pid_file = fopen("pid.txt", "r");
    if(pid_file) {
        char name[32];
        pid_t pid;
        while(fscanf(pid_file, "%s %d", name, &pid) == 2) {
            register_process(name, pid);
        }
        fclose(pid_file);
    }
    
    // Setup ncurses
    initscr();
    refresh();
    start_color();
    cbreak();
    noecho();
    nodelay(stdscr, TRUE);
    curs_set(0);
    
    init_pair(1, COLOR_GREEN, COLOR_BLACK);   // OK
    init_pair(2, COLOR_RED, COLOR_BLACK);     // TIMEOUT
    init_pair(3, COLOR_YELLOW, COLOR_BLACK);  // WARNING
    
    int height = 15;
    int width = 70;
    int starty = (LINES - height) / 2;
    int startx = (COLS - width) / 2;
    status_win = newwin(height, width, starty, startx);
    char buf[100];
    sprintf(buf, "Ncurses window created at (%d,%d) size %dx%d", startx, starty, width, height);
    // sprintf(buf, "Ncurses window created at (%d,%d) size %dx%d", 0, 0, 0, 0);
    logger(log_file, buf);
    
    char log_buf[200];
    sprintf(log_buf, "Watchdog monitoring %d processes with poll interval %.1fs",
            num_processes, poll_interval);
    logger(log_file, log_buf);
    
    // Main loop
    time_t last_check = time(NULL);
    
    while(running) {
        int ch = getch();
        if(ch == 'q' || ch == 'Q') {
            break;
        }
        
        time_t now = time(NULL);
        if(difftime(now, last_check) >= poll_interval) {
            check_processes();
            last_check = now;
        }
        
        update_status_window();
        usleep(100000); // 100ms
        logger(log_file, "Arrivo");
    }
    
    // Cleanup
    delwin(status_win);
    logger(log_file, "Watchdog shutting down");
    endwin();
    fclose(log_file);
    return 0;
}