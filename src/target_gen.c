#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "protocol.h"
#include "utils.h"
#include "fcntl.h"
#include <signal.h>
#include <sys/file.h>


static volatile sig_atomic_t running = 1;
FILE *log_file;
pid_t watchdog_pid = -1;

void termination_handler(int signum){
    logger(log_file, "Target Generator Terminated");
    running =0;
}

float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

float distance(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

void send_heartbeat() {
    if(watchdog_pid > 0) {
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to watchdog");
    }
}
int main(int argc, char *argv[]) {

    log_file = fopen("log/targets_log.txt","w");
    logger(log_file, "Target Generator Started");

    //scrittura pid in pid.txt
    FILE * pid_file = fopen("pid.txt","a");
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "tar_gen", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    
    //PIPE from ENV
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    char * watchdog_pid_str = getenv("WATCHDOG_PID");
    watchdog_pid = atoi(watchdog_pid_str);
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);
    
    //Read non bloking
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
    srand(time(NULL) ^ getpid());
    
    
    // int next_target_id = 1;
    // const float REACH_RADIUS = 3.0; // Raggio per considerare target raggiunto
    
    int target_count = 0;
    int mapx = config.map_width-5;
    int mapy = config.map_height -5;

    //Setting sigaction
    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }

    ResizeMessage res;

    // Heartbeat variables for watchdog
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Invia ogni 1.5s



    while (running) {

        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
        }
        //Gestione Rezise
        size_t n = read(read_fd, &res, sizeof(ResizeMessage));
        if(n ==sizeof(ResizeMessage))
        {
            mapx = res.x;
            mapy = res.y;
            target_count = 0;
        }
        //Creazione target
        if (target_count < MAX_TAR ) {
            Message msg;
            msg.type = 'T';
            msg.data.target.x = random_float(5, mapx-5);
            msg.data.target.y = random_float(5, mapy-5);

            msg.data.target.id = target_count;
            msg.data.target.active = 1;
            
            
            ssize_t written = write(write_fd, &msg, sizeof(Message));
            if (written == sizeof(Message)) {
                target_count++;
                char buf[256];
                sprintf(buf, "[TARGET_GEN] Created target at (%.1f, %.1f) [Total: %d]\n",
                        msg.data.target.x, msg.data.target.y, target_count);
                logger(log_file, buf);
            }
        }
        
    }
    //Closing fds
    close(write_fd);
    close(read_fd);
    fclose(log_file);
    return 0;
}
