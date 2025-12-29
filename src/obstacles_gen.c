#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include "protocol.h"
#include "utils.h"
#include <fcntl.h>
#include <sys/file.h>

static volatile sig_atomic_t running = 1;
FILE  * log_file;
FILE * wd_log_file;
FILE * common_log;
pid_t watchdog_pid = -1;


float random_float(float min, float max) {
    return min + (float)rand() / RAND_MAX * (max - min);
}

void termination_handler(int signum)
{
    logger(log_file, "Obstacles Generator Terminted");
    running =0;
}

void send_heartbeat() {
    if(watchdog_pid > 0) {
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to watchdog");
    }
}

int main(int argc, char *argv[]) {

    //Loggeer
    log_file = fopen("log/obstacles_log.txt", "w");
    logger(log_file, "OBS Started");
    wd_log_file = fopen(WD_LOG_PATH, "a");
    common_log = fopen(COMMON_LOG, "a");

    //scrittura pid in pid.txt
    FILE * pid_file = fopen(PID_FILE,"a");
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "obs_gen", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    //Config
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
    
    //Setto la read non blocking per gestire il caso Resize
    int flags = fcntl(read_fd, F_GETFL, 0);
    fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);
    
    srand(time(NULL) ^ getpid());
    
    logger(log_file, "OBSTACLE_GEN Started");
    
    int obstacle_count = 0;
    const int MAX_OBSTACLES = MAX_OBS;
    int mapx = config.map_width-5 ;
    int mapy = config.map_height-5;
    ResizeMessage res;

    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }

    //Heartbeat 
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // ogni 1.5s

    //Obs time
    time_t last_gen_time = time(NULL);
    const float gen_interval = 5.0f;

    int iteration = 0;

    while (running) {
        
        iteration++;
        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
            char buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "obstacles_gen", "main loop", iteration);
            safe_logger(wd_log_file, buf);
        }

        size_t n = read(read_fd, &res, sizeof(ResizeMessage));
        if(n ==sizeof(ResizeMessage))
        {
            logger(log_file, "Resize Receive");
            mapx = res.x;
            mapy = res.y;
            obstacle_count = 0;
        }

        if(difftime(now, last_gen_time)>= gen_interval) {
            last_gen_time = now;
            obstacle_count --;
            logger(log_file, "ENTRO");
        }
        //Creazione ostacolo
        if (obstacle_count < MAX_OBSTACLES ) {
            Message msg;
            msg.type = 'O';
            msg.data.obstacle.x = random_float(5, mapx-5);
            msg.data.obstacle.y = random_float(5, mapy-5);

            msg.data.obstacle.active = 1;
            ssize_t written = write(write_fd, &msg, sizeof(Message));
            if (written == sizeof(Message)) {
                obstacle_count++;
                char buf[256];
                sprintf(buf, "Created obstacle at (%.1f, %.1f) [Total: %d]",
                        msg.data.obstacle.x, msg.data.obstacle.y, obstacle_count);
                logger(log_file, buf);
                char log_buf[300];
                char *t = ctime(&now);
                t[strlen(t) - 1] = '\0';
                sprintf(log_buf, "<%s><%s><%s>", t, "OBS_GEN", buf);
                safe_logger(common_log, log_buf);
            }
            last_gen_time = now;
        }

        
    }
    //chiusura fds
    close(read_fd);
    close(write_fd);
    fclose(log_file);
    return 0;
}