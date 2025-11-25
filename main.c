#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"

int main(int arc, char ** argv) {
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

    int w_status, i_status, b_status, d_status, o_status;
    pid_t w_pid, i_pid, b_pid, d_pid, o_pid;
    if((w_pid = fork()) == 0){

        //window
        close(bw_pipe[1]);
        close(wb_pipe[0]);
        char write_fd[16];
        char read_fd[16];
        sprintf(read_fd, "%d", bw_pipe[0]);
        sprintf(write_fd, "%d", wb_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1);
        //char cmd[256];
        //sprintf(cmd, "./window %s %s", in_fd, out_fd);
        //execlp("konsole", "konsole", "-e", "-sh", "-c",cmd, NULL);
        execlp("konsole", "konsole", "-e", "./window",NULL);
        
    }
    write(bw_pipe[1], "ciao\n", sizeof("ciao\n"));

    if((i_pid = fork()) == 0){
        //input
        close(bi_pipe[1]); //close write
        close(ib_pipe[0]); //close read
        char write_fd[16];
        char read_fd[16];
        sprintf(read_fd, "%d", bi_pipe[0]);
        sprintf(write_fd, "%d", ib_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1);
        execlp("konsole", "konsole", "-e", "./input",NULL);
    }

     if((b_pid = fork() )== 0){
         //server-blackboard
         close(bi_pipe[0]); //close read close(bw_pipe[0]); //close read
         close(ib_pipe[1]); //close write
         close(bw_pipe[0]); //close write
         close(wb_pipe[1]); //close write
         close(bd_pipe[0]);
        close(db_pipe[1]);
        close(bo_pipe[0]); //lettura
        close(ob_pipe[1]); //scrittura

         char read_input_fd[16];
         char read_window_fd[16];
         char write_input_fd[16];
         char write_window_fd[16];
         char read_dynamic_fd[16];
         char read_obs_fd[16];
         char write_dynamic_fd[16];
         char write_obs_fd[16];
         sprintf(read_input_fd, "%d", ib_pipe[0]);
         sprintf(read_window_fd, "%d", wb_pipe[0]);
         sprintf(write_input_fd, "%d", bi_pipe[1]);
         sprintf(write_window_fd, "%d", bw_pipe[1]);
         sprintf(read_dynamic_fd, "%d", db_pipe[0]);
         sprintf(write_dynamic_fd, "%d", bd_pipe[1]);
         sprintf(read_obs_fd, "%d", ob_pipe[0]);
         sprintf(write_obs_fd, "%d", bo_pipe[1]);
         setenv("IN_INPUT_FD", read_input_fd, 1);
         setenv("IN_WINDOW_FD", read_window_fd, 1);
         setenv("OUT_INPUT_FD", write_input_fd, 1);
         setenv("OUT_WINDOW_FD", write_window_fd, 1);
         setenv("IN_DYNAMIC_FD", read_dynamic_fd, 1);
         setenv("OUT_DYNAMIC_FD", write_dynamic_fd, 1);
         setenv("IN_OBS_FD", read_obs_fd, 1);
         setenv("OUT_OBS_FD", write_obs_fd, 1);
        char * args[] = {"./server", NULL};
        execvp("./server", args);
     }

    if((d_pid = fork()) == 0){
        //obstacles generator
        close(bd_pipe[1]);
        close(db_pipe[0]);
        char write_fd[16];    
        char read_fd[16];
        sprintf(read_fd, "%d", bd_pipe[0]);
        sprintf(write_fd, "%d", db_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1); 
        execlp("./dynamic", "./dynamic", NULL);

    }
    if((o_pid = fork()) == 0){
        //dynamic
        close(bo_pipe[1]);
        close(ob_pipe[0]);
        char write_fd[16];    
        char read_fd[16];
        sprintf(read_fd, "%d", bo_pipe[0]);
        sprintf(write_fd, "%d", ob_pipe[1]);
        setenv("IN_FD", read_fd, 1);
        setenv("OUT_FD", write_fd, 1); 
        execlp("./obs_gen", "./obs_gen", NULL);

    }
    
    waitpid(o_pid, &o_status,0);
    waitpid(b_pid, &b_status, 0);
    waitpid(d_pid, &d_status, 0);
    waitpid(i_pid, &i_status, 0);
    waitpid(w_pid, &w_status, 0);
    return 0;
}
