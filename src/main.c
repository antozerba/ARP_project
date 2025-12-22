#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <signal.h>



int main(int arc, char ** argv) {

    FILE * log_file = fopen("log/main_log.text","w");
    //pulire il file pid.txt all'avvio
    FILE * pid_file = fopen(PID_FILE,"w");
    FILE * wd_file = fopen(WD_LOG_PATH,"w");
    fclose(pid_file);
    fclose(wd_file);
    
    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    //PIPE
    //BLACK-WHINDOW
    int bw_pipe[2]; //prime lettura //seconda scrittura
    int wb_pipe[2];
    //BLACK-INPUT
    int bi_pipe[2];
    int ib_pipe[2];
    //BLACK-DYNAMIC
    int bd_pipe[2];
    int db_pipe[2];
    //BLACk-OBS
    int bo_pipe[2];
    int ob_pipe[2];
    //BLACk-TAR
    int bt_pipe[2];
    int tb_pipe[2];
    
    if(pipe(bw_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(wb_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(bi_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(ib_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(bd_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(db_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(ob_pipe)==-1){
        perror("pipe obs");
        return -1;
    }
    if(pipe(bo_pipe)==-1){
        perror("pipe obs");
        return -1;
    }
    if(pipe(bt_pipe)==-1){
        perror("pipe obs");
        return -1;
    }
    if(pipe(tb_pipe)==-1){
        perror("pipe obs");
        return -1;
    }

    int w_status, i_status, b_status, d_status, o_status, t_status, wd_status;
    pid_t w_pid, i_pid, b_pid, d_pid, o_pid, t_pid, wd_pid;

    if((wd_pid = fork()) == 0){
        //watchdog
        close(bi_pipe[0]); close(bi_pipe[1]);
        close(ib_pipe[0]); close(ib_pipe[1]);
        close(bw_pipe[0]); close(bw_pipe[1]);
        close(wb_pipe[0]); close(wb_pipe[1]);
        close(bd_pipe[0]); close(bd_pipe[1]);
        close(db_pipe[0]); close(db_pipe[1]);
        close(bo_pipe[0]); close(bo_pipe[1]);
        close(ob_pipe[0]); close(ob_pipe[1]);
        close(bt_pipe[0]); close(bt_pipe[1]);
        close(tb_pipe[0]); close(tb_pipe[1]);
        
        execlp("konsole", "konsole", "-e", "./watchdog", NULL);
        perror("process failed");
        exit(1);
    }
    wd_pid = -1;
    FILE *f = NULL;

    usleep(1000000); //to allow watchdog to store pid
    f = fopen(WATCHDOG_FILE, "r");

    if (!f) {
        logger(log_file, "Watchdog PID file not found\n");
        exit(1);
    }

    fscanf(f, "%d", &wd_pid);
    fclose(f);
    char buf[40];
    sprintf(buf, "REAL WATCHDOG PID = %d\n", wd_pid);
    logger(log_file, buf);

    if((b_pid = fork() )== 0){
         //server-blackboard
        close(bi_pipe[0]); //close read close(bw_pipe[0]); //close read
        close(ib_pipe[1]); //close write
        close(bw_pipe[0]); //close read
        close(wb_pipe[1]); //close write
        close(bd_pipe[0]);     
        close(db_pipe[1]);      
        close(bo_pipe[0]); //lettura      
        close(ob_pipe[1]); //scrittura
        close(bt_pipe[0]); //lettura      
        close(tb_pipe[1]); //scrittura
        
        char read_input_fd[16];
        char read_window_fd[16];
        char write_input_fd[16];
        char write_window_fd[16];
        char read_dynamic_fd[16];
        char read_obs_fd[16];
        char write_dynamic_fd[16];
        char write_obs_fd[16];
        char read_tar_fd[16];
        char write_tar_fd[16];
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);

        sprintf(read_input_fd, "%d", ib_pipe[0]);
        sprintf(read_window_fd, "%d", wb_pipe[0]);
        sprintf(write_input_fd, "%d", bi_pipe[1]);
        sprintf(write_window_fd, "%d", bw_pipe[1]);
        sprintf(read_dynamic_fd, "%d", db_pipe[0]);
        sprintf(write_dynamic_fd, "%d", bd_pipe[1]);
        sprintf(read_obs_fd, "%d", ob_pipe[0]);
        sprintf(write_obs_fd, "%d", bo_pipe[1]);
        sprintf(write_tar_fd, "%d", bt_pipe[1]);
        sprintf(read_tar_fd, "%d", tb_pipe[0]);
        setenv("IN_INPUT_FD", read_input_fd, 1);
        setenv("IN_WINDOW_FD", read_window_fd, 1);
        setenv("OUT_INPUT_FD", write_input_fd, 1);
        setenv("OUT_WINDOW_FD", write_window_fd, 1);
        setenv("IN_DYNAMIC_FD", read_dynamic_fd, 1);
        setenv("OUT_DYNAMIC_FD", write_dynamic_fd, 1);
        setenv("IN_OBS_FD", read_obs_fd, 1);
        setenv("OUT_OBS_FD", write_obs_fd, 1);     
        setenv("IN_TAR_FD", read_tar_fd, 1);
        setenv("OUT_TAR_FD", write_tar_fd, 1);     
        char * args[] = {"./server", NULL};   
        execvp("./server", args);
        perror("process failed");
        exit(1);
     }

    if((w_pid = fork()) == 0){

        //window
        close(bw_pipe[1]);
        close(wb_pipe[0]);

        close(bi_pipe[0]); close(bi_pipe[1]);
        close(ib_pipe[0]); close(ib_pipe[1]);
        close(bd_pipe[0]); close(bd_pipe[1]);
        close(db_pipe[0]); close(db_pipe[1]);
        close(bo_pipe[0]); close(bo_pipe[1]);
        close(ob_pipe[0]); close(ob_pipe[1]);
        close(bt_pipe[0]); close(bt_pipe[1]);
        close(tb_pipe[0]); close(tb_pipe[1]);
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);
        
        char write_fd[16];
        char read_fd[16];
        sprintf(read_fd, "%d", bw_pipe[0]);
        sprintf(write_fd, "%d", wb_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1);

        execlp("konsole", "konsole", "-e", "./window",NULL); 

        perror("process failed");
        exit(1);
    }
    if((i_pid = fork()) == 0){

        close(bw_pipe[0]); close(bw_pipe[1]);
        close(wb_pipe[0]); close(wb_pipe[1]);
        close(bd_pipe[0]); close(bd_pipe[1]);
        close(db_pipe[0]); close(db_pipe[1]);
        close(bo_pipe[0]); close(bo_pipe[1]);
        close(ob_pipe[0]); close(ob_pipe[1]);
        close(bt_pipe[0]); close(bt_pipe[1]);
        close(tb_pipe[0]); close(tb_pipe[1]);
        //input
        close(bi_pipe[1]); //close write
        close(ib_pipe[0]); //close read
        char write_fd[16];
        char read_fd[16];
        sprintf(read_fd, "%d", bi_pipe[0]);
        sprintf(write_fd, "%d", ib_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1);
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);


        execlp("konsole", "konsole", "-e", "./input",NULL);
        perror("process failed");
        exit(1);
    }

    if((d_pid = fork()) == 0){
        //DYNAMIC
        close(bd_pipe[1]);
        close(db_pipe[0]);
        close(bw_pipe[0]); close(bw_pipe[1]);
        close(wb_pipe[0]); close(wb_pipe[1]);
        close(bi_pipe[0]); close(bi_pipe[1]);
        close(ib_pipe[0]); close(ib_pipe[1]);
        close(bo_pipe[0]); close(bo_pipe[1]);
        close(ob_pipe[0]); close(ob_pipe[1]);
        close(bt_pipe[0]); close(bt_pipe[1]);
        close(tb_pipe[0]); close(tb_pipe[1]);
        char write_fd[16];    
        char read_fd[16];
        sprintf(read_fd, "%d", bd_pipe[0]);
        sprintf(write_fd, "%d", db_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1); 
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);



        execlp("./dynamic", "./dynamic", NULL);
        perror("process failed");
        exit(1);

    }
    if((o_pid = fork()) == 0){
        //OBS
        close(bo_pipe[1]);
        close(ob_pipe[0]);
        close(bw_pipe[0]); close(bw_pipe[1]);
        close(wb_pipe[0]); close(wb_pipe[1]);
        close(bi_pipe[0]); close(bi_pipe[1]);
        close(ib_pipe[0]); close(ib_pipe[1]);
        close(bd_pipe[0]); close(bd_pipe[1]);
        close(db_pipe[0]); close(db_pipe[1]);
        close(bt_pipe[0]); close(bt_pipe[1]);
        close(tb_pipe[0]); close(tb_pipe[1]);

        char write_fd[16];    
        char read_fd[16];
        sprintf(read_fd, "%d", bo_pipe[0]);
        sprintf(write_fd, "%d", ob_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1); 
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);

        execlp("./obs_gen", "./obs_gen", NULL);

        perror("process failed");
        exit(1);
    }
    if((t_pid = fork()) == 0){
        //TARGET
        close(bt_pipe[1]);
        close(tb_pipe[0]);
        //chiusura pipe non utilizzate
        close(bw_pipe[0]); close(bw_pipe[1]);
        close(wb_pipe[0]); close(wb_pipe[1]);
        close(bi_pipe[0]); close(bi_pipe[1]);
        close(ib_pipe[0]); close(ib_pipe[1]);
        close(bd_pipe[0]); close(bd_pipe[1]);
        close(db_pipe[0]); close(db_pipe[1]);
        close(bo_pipe[0]); close(bo_pipe[1]);
        close(ob_pipe[0]); close(ob_pipe[1]);

        char write_fd[16];    
        char read_fd[16];
        sprintf(read_fd, "%d", bt_pipe[0]);
        sprintf(write_fd, "%d", tb_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1); 
        //watchdog
        char watchdog_pid_fd[16];
        sprintf(watchdog_pid_fd, "%d", wd_pid);
        setenv("WATCHDOG_PID", watchdog_pid_fd, 1);

        execlp("./tar_gen", "./tar_gen", NULL);
        perror("process failed");
        exit(1);
    }
    //chiusura tutte pipes nel padre
    close(bw_pipe[0]); close(bw_pipe[1]);
    close(wb_pipe[0]); close(wb_pipe[1]);
    close(bi_pipe[0]); close(bi_pipe[1]);
    close(ib_pipe[0]); close(ib_pipe[1]);
    close(bd_pipe[0]); close(bd_pipe[1]);
    close(db_pipe[0]); close(db_pipe[1]);
    close(bo_pipe[0]); close(bo_pipe[1]);
    close(ob_pipe[0]); close(ob_pipe[1]);
    close(bt_pipe[0]); close(bt_pipe[1]);
    close(tb_pipe[0]); close(tb_pipe[1]);

    //server chiuso 
    waitpid(b_pid, &b_status, 0);
    //comando chiusura quindi mando segnale a tutti i figli
    logger(log_file, "ARRIVO");
    kill(t_pid, SIGTERM);
    kill(o_pid, SIGTERM);
    kill(d_pid, SIGTERM);
    pid_t pgw = getpgid(w_pid); //w_pid continer il pid di konsole ma io voglio tutti i porcessi eseguiti da konsole tra cui window
    kill(-pgw, SIGTERM);
    pid_t pgi = getpgid(i_pid); //i_pid continer il pid di konsole  ma io voglio tutti i porcessi eseguiti da konsole tra cui input
    kill(-pgi, SIGTERM);
    pid_t pgwd = getpgid(wd_pid); //i_pid continer il pid di konsole  ma io voglio tutti i porcessi eseguiti da konsole tra cui input
    kill(-pgwd, SIGTERM);

    //sleep per far terminare tutti i processi in modo corretto
    sleep(1);

    waitpid(t_pid, &t_status,0);
    waitpid(o_pid, &o_status,0);
    waitpid(d_pid, &d_status, 0);
    waitpid(i_pid, &i_status, 0);
    waitpid(w_pid, &w_status, 0);

    logger(log_file, "All Processes Terminated Successfully");
    fclose(log_file);

    return 0;

}
