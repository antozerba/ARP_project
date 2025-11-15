#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

int main(int arc, char ** argv) {
    //PIPE
    int bw_pipe[2]; //prime lettura //seconda scrittura
    int wb_pipe[2];
    
    if(pipe(bw_pipe) == -1){
        perror("pipe input");
        return 1;
    }
    if(pipe(wb_pipe) == -1){
        perror("pipe input");
        return 1;
    }

    int w_status;
    pid_t pid;
    
    if((pid = fork()) == 0){

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
    waitpid(pid, &w_status, 0);
    return 0;
}
