#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include <sys/select.h>
#include "protocol.h"
#include <string.h>
#include <sys/file.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>


#define COLL_RAD 0.5

FILE * log_file;
FILE * wd_log_file;
FILE * common_log;
Config config = {};
pid_t watchdog_pid = -1;
int read_input_fd, write_input_fd;
int read_window_fd, write_window_fd;
int read_dynamic_fd, write_dynamic_fd;
int read_obs_fd, write_obs_fd;
int read_tar_fd, write_tar_fd;
int network_socket = -1;
NetworkMode network_mode = MODE_STANDALONE;


// Setup network socket
int setup_network_socket(NetworkConfig *nc) {
    int sock;
    struct sockaddr_in addr;
    
    if(nc->mode == MODE_SERVER) {
        // Server: crea socket e attendi connessione
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {

            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY; //tutti i client sia locali che remoti possono connettersi
        addr.sin_port = htons(nc->serve_port);
        
        if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("bind");
            close(sock);
            logger(log_file, "Failed to bind socket");
            return -1;
        }
        
        if(listen(sock, 1) < 0) {
            perror("listen");
            close(sock);
            logger(log_file, "Failed to listen on socket");
            return -1;
        }
        
        logger(log_file, "Server waiting for connection...");
        
        int client_sock = accept(sock, NULL, NULL);
        if(client_sock < 0) {
            perror("accept");
            close(sock);
            logger(log_file, "Failed to accept connection");
            return -1;
        }
        
        close(sock); // Chiudi socket di ascolto
        logger(log_file, "Client connected!");

        //FARLO SUL SELECT
        
        // // Protocollo: invia "ok"
        // write(client_sock, "ok\n", 3);
        
        // // Ricevi "ook"
        // char buf[32];
        // read(client_sock, buf, sizeof(buf));
        
        // // Invia dimensioni window
        // char size_msg[64];
        // sprintf(size_msg, "size %d %d\n", 100, 40); // Da config
        // write(client_sock, size_msg, strlen(size_msg));
        
        // // Ricevi "sok"
        // read(client_sock, buf, sizeof(buf));
        
        return client_sock;
        
    } else if(nc->mode == MODE_CLIENT) {
        // Client: connetti al server
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            perror("socket");
            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nc->serve_port);
        inet_pton(AF_INET, nc->server_ip, &addr.sin_addr);
        
        logger(log_file, "Connecting to server...");
        
        if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            perror("connect");
            close(sock);
            return -1;
        }
        
        logger(log_file, "Connected to server!");
        
        // // Protocollo: ricevi "ok"
        // char buf[32];
        // read(sock, buf, sizeof(buf));
        
        // // Invia "ook"
        // write(sock, "ook\n", 4);
        
        // // Ricevi dimensioni
        // char size_msg[64];
        // read(sock, size_msg, sizeof(size_msg));
        // int width, height;
        // sscanf(size_msg, "size %d %d", &width, &height);

        // //TODO: MANDARE DIREZIONI AL WINDOW
        // //idea: setterli nello state e poi fare il check in window.c
        
        // char log_buf[100];
        // sprintf(log_buf, "Received window size: %dx%d", width, height);
        // logger(log_file, log_buf);
        
        // // Invia "sok"
        // sprintf(buf, "sok %d %d\n", width, height);
        // write(sock, buf, strlen(buf));
        
        return sock;
    }
    
    return -1;
}

void send_drone_position(int sock, float x, float y) {
    if(sock < 0) return;
    
    char msg[128];
    sprintf(msg, "drone\n%.2f %.2f\n", x, y);
    write(sock, msg, strlen(msg));
    
    // Ricevi "dok"
    char ack[32];
    read(sock, ack, sizeof(ack));
}

void receive_obstacle_position(int sock, Obstacle *obs) {
    if(sock < 0) return;
    
    // Invia "obst"
    write(sock, "obst\n", 5);
    
    // Ricevi posizione
    char buf[64];
    read(sock, buf, sizeof(buf));
    
    float x, y;
    sscanf(buf, "%f %f", &x, &y);
    
    obs->x = (int)x;
    obs->y = (int)y;
    obs->active = 1;
    
    // Invia "pok"
    sprintf(buf, "pok %f %f\n", obs->x, obs->y);
    write(sock, buf, strlen(buf));
}
//initialize world state object with pos of drone form config
void init_world_state(WorldState * state){
    memset(state, 0, sizeof(WorldState));
    state->drone.x = config.drone_x;
    state->drone.y = config.drone_y;
    for (int i = 0; i < MAX_OBS; i++) {
        state->obstacles[i].active = 0;
        state->obstacles[i].x = -1;
        state->obstacles[i].y = -1;
    }
    state->mapx = config.map_width;
    state->mapy = config.map_height;
}
//closing server
void handle_quit(){
    //chiudo tutti i fds
    close(read_input_fd);
    close(write_input_fd);
    close(read_window_fd);
    close(write_window_fd);
    close(read_dynamic_fd);
    close(write_dynamic_fd);
    close(read_obs_fd);
    close(write_obs_fd);
    close(read_tar_fd);
    close(write_tar_fd);

    //chiusura logger
    fclose(log_file);
    exit(0);

}
void replace_obs(WorldState * state, const Obstacle * obs){
    int finding = 1;
    while(finding){
        int r = rand() % MAX_OBS;
        if(state->obstacles[r].active){
            state->obstacles[r] = *obs;
            finding = 0;
        }
    }
    
}

void handle_input_command(WorldState *state, InputCommand *cmd) {
    switch(cmd->type) {
        case CMD_BRAKE:
            state->drone.fx = 0;
            state->drone.fy = 0;
            fprintf(stderr, "[SERVER] Brake applied\n");
            break;
            
        case CMD_PAUSE:
            //TODDO da implementare nel second ass
            break;
            
        case CMD_RESET:
            //TODDO da implementare nel second ass
            break;
            
        case CMD_QUIT:
            logger(log_file, "CHIUSURA SERVER");
            handle_quit();

            break;
            
        default:
            // Aggiorna forze comando
            state->drone.fx += cmd->force_x;
            state->drone.fy += cmd->force_y;
            break;
    }
}
void handle_message(WorldState *state, Message *msg) {
    switch(msg->type) {
        case 'D':
            //modifico solo le pos e le vel del drone
            state->drone.x = msg->data.drone.x;
            state->drone.y = msg->data.drone.y;
            state->drone.vx = msg->data.drone.vx;
            state->drone.vy = msg->data.drone.vy;
            state->drone.fx = msg->data.drone.fx;
            state->drone.fy = msg->data.drone.fy;
            break;
        case 'T':
            for (int i = 0; i < MAX_TAR; i++) {
                if (!state->targets[i].active) {
                    state->targets[i] = msg->data.target;
                    state->num_active_targets++;
                    break;
                }
            }
            break;
        case 'O':
            int  i = state->num_obstacles;
            if(i == MAX_OBS){
                replace_obs(state, &msg->data.obstacle);
                break;
            }
            for (i = 0; i < MAX_OBS; i++) {
                if (!state->obstacles[i].active) {
                    state->obstacles[i] = msg->data.obstacle;
                    state->num_obstacles++;
                    break;
                }
            }
            break;

        default:
            logger(log_file, "Unknown message type received");
            break;
    }
}

void checking_target(WorldState * state){
    int drone_x = state->drone.x;
    int drone_y = state->drone.y;
    for(int i=0; i<MAX_TAR; i++){
        if(state->targets[i].active){
            if(abs(state->targets[i].x - drone_x)< COLL_RAD && abs(state->targets[i].y - drone_y)< COLL_RAD)
            {
                logger(log_file, "TARGET REACHED ;)");
                state->targets[i].active = 0;
                state->num_active_targets--;
                state->target_reached++;
                //logger common
                char log_buf[100];
                time_t now = time(NULL);
                char *t = ctime(&now);
                t[strlen(t) - 1] = '\0';
                sprintf(log_buf, "<%s><%s><%s>", t, "SERVER", "Target Reached");
                safe_logger(common_log, log_buf);
            }
        }
    }

}
void checking_obs(WorldState * state){
    int drone_x = state->drone.x;
    int drone_y = state->drone.y;
    for(int i=0; i<MAX_OBS; i++){
        if(state->obstacles[i].active){
            if(abs(state->obstacles[i].x - drone_x)< COLL_RAD && abs(state->obstacles[i].y - drone_y)< COLL_RAD)
            {
                logger(log_file, "OBS COLLISION :(");
                state->obstacles[i].active = 0;
                state->num_obstacles--;
                //Logger common
                time_t now = time(NULL);
                char log_buf[100];
                char *t = ctime(&now);
                t[strlen(t) - 1] = '\0';
                sprintf(log_buf, "<%s><%s><%s>", t, "SERVER", "Obstacle Collision Detected");
                safe_logger(common_log, log_buf);
            }
        }
    }

}
void checking_collisions(WorldState *state){
    // checking_obs(state);
    checking_target(state);
}

void send_heartbeat(){
    if(watchdog_pid > 0){
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to Watchdog");
    }
}

int main(int argc, char **argv){

    //Logger
    log_file = fopen("log/server_log.text","w");
    logger(log_file, "Server started");
    wd_log_file = fopen(WD_LOG_PATH, "a");
    common_log = fopen(COMMON_LOG, "a");


    //PIPE from EN
    char * read_input_fd_char = getenv("IN_INPUT_FD");
    char * read_window_fd_char = getenv("IN_WINDOW_FD");
    char * write_input_fd_char = getenv("OUT_INPUT_FD");
    char * write_window_fd_char = getenv("OUT_WINDOW_FD");
    char * read_dynamic_fd_char = getenv("IN_DYNAMIC_FD");
    char * write_dynamic_fd_char = getenv("OUT_DYNAMIC_FD");
    char * write_obs_fd_char = getenv("OUT_OBS_FD");
    char * read_obs_fd_char = getenv("IN_OBS_FD");
    char * write_tar_fd_char = getenv("OUT_TAR_FD");
    char * read_tar_fd_char = getenv("IN_TAR_FD");
    char *watchdog_pid_str = getenv("WATCHDOG_PID");

    watchdog_pid = atoi(watchdog_pid_str);
    read_input_fd = atoi(read_input_fd_char);
    write_input_fd = atoi(write_input_fd_char);
    read_window_fd = atoi(read_window_fd_char);
    write_window_fd = atoi(write_window_fd_char);
    read_dynamic_fd = atoi(read_dynamic_fd_char);
    write_dynamic_fd = atoi(write_dynamic_fd_char);
    read_obs_fd = atoi(read_obs_fd_char);
    write_obs_fd = atoi(write_obs_fd_char);
    read_tar_fd = atoi(read_tar_fd_char);
    write_tar_fd = atoi(write_tar_fd_char);

    //scrittura pid 
    FILE * pid_file = fopen(PID_FILE, "a"); //append mode
    if(pid_file){
        //lock to avoid race condition
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "server", getpid());
        // fflush(pid_file);
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }
    // Carica configurazione di rete
    NetworkConfig nc;


    //Config method
    network_mode = getenv("NETWORK_MODE") ? atoi(getenv("NETWORK_MODE")) : nc.mode;
    strcpy(nc.server_ip, config.server_ip);
    nc.serve_port = config.server_port;
    nc.mode = network_mode;
    

    char buf[200];
    sprintf(buf, "NETWORK: %s, serverip: %s, serve_port : %d",
            nc.mode == MODE_STANDALONE ? "STANDALONE" :
            nc.mode == MODE_SERVER ? "SERVER" : 
            nc.mode == MODE_CLIENT ? "CLIENT" : "UNKNOWN",
            nc.server_ip,
            nc.serve_port);

    logger(log_file, buf);
    

    // Setup network se necessario
    if(nc.mode != MODE_STANDALONE) {
        network_socket = setup_network_socket(&nc);
        if(network_socket < 0) {
            logger(log_file, "Network setup failed, exiting");
            return 1;
        }
    }


    WorldState state;
    init_world_state(&state);

    // Heartbeat variables for watchdog
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Invia ogni 1.5s
    
    int iteration = 0;
    for(;;){
        iteration++;
        // Invia heartbeat periodicamente
        time_t now = time(NULL);
        if(difftime(now, last_heartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_heartbeat = now;
            char buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "server", "main", iteration);
            safe_logger(wd_log_file, buf);
        }
        
        
        //SELECT
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(read_input_fd, &read_fds);
        FD_SET(read_window_fd, &read_fds);
        FD_SET(read_dynamic_fd, &read_fds);
        if(read_obs_fd >= 0) FD_SET(read_obs_fd, &read_fds);
        if(read_tar_fd >= 0) FD_SET(read_tar_fd, &read_fds);
        if(network_socket >= 0) FD_SET(network_socket, &read_fds);

        int max_fd = 0;
        if(read_input_fd > max_fd) max_fd = read_input_fd;
        if(read_window_fd > max_fd) max_fd = read_window_fd;
        if(read_dynamic_fd > max_fd) max_fd = read_dynamic_fd;
        if(read_obs_fd > max_fd) max_fd = read_obs_fd;
        if(read_tar_fd > max_fd) max_fd = read_tar_fd;
        if(network_socket > max_fd) max_fd = network_socket;
        //flag per capire se mandare a dynamics in base a input
        int send_dyn = 0;

        //set timer for select
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;

        int r = select(max_fd + 1, &read_fds, NULL, NULL, &timeout); //ritorna numero di fd pronti
        if(r == -1){
            logger(log_file, "Error in select");
            return 1;
        }

        if(FD_ISSET(read_input_fd, &read_fds)){
            logger(log_file, "Reading InputCommand from input...");
            InputCommand cmd;
            ssize_t bytes_read = read(read_input_fd, &cmd, sizeof(InputCommand));
            if(bytes_read != sizeof(InputCommand)){
                logger(log_file, "Error reading InputCommand from input");
                continue;
            }
            char buffer[200];
            sprintf(buffer, "INPUT COMMAND RECEIVED - type: %d, force_x: %f, force_y: %f", cmd.type, cmd.force_x, cmd.force_y);
            logger(log_file, buffer);
            handle_input_command(&state, &cmd);
            send_dyn = 1;
        }   
        
        //Leggo dynamic
        if(FD_ISSET(read_dynamic_fd, &read_fds)){
            Message msg;
            ssize_t bytes_read = read(read_dynamic_fd, &msg, sizeof(Message));
            if(bytes_read != sizeof(Message)){
                logger(log_file, "Error reading Message from dynamic");
                continue;
            }
            handle_message(&state, &msg);
            char baf[256];
            snprintf(baf, sizeof(baf),
                     "DRONE RECEIVED FROM DYNAMIC - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                     msg.data.drone.x, msg.data.drone.y,
                     msg.data.drone.vx, msg.data.drone.vy,
                     msg.data.drone.fx, msg.data.drone.fy);
            logger(log_file, baf);
        }
         // Leggi messaggi da obstacle generator
        if (read_obs_fd > 0 && FD_ISSET(read_obs_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_obs_fd, &msg, sizeof(Message));
            char ob[256];
            sprintf(ob,
                    "OBS RECEIVED  - obs: x: %lf, y: %lf",
                    msg.data.obstacle.x, msg.data.obstacle.y
                    );
            logger(log_file, ob);


            if (n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        // Ricevo target 
        if (read_tar_fd > 0 && FD_ISSET(read_tar_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_tar_fd, &msg, sizeof(Message));
            char tar[256];
            sprintf(tar,
                    "TARGET RECEIVED  - tar: x: %lf, y: %lf",
                    msg.data.target.x, msg.data.target.y
                    );
            logger(log_file, tar);


            if (n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        //Leggo dalla window se avviene un resize
        if (FD_ISSET(read_window_fd, &read_fds)){
            ResizeMessage msg;
            ssize_t n = read(read_window_fd, &msg, sizeof(ResizeMessage));
            char buf[100];
            
            if (n == sizeof(ResizeMessage)) {
                // write
                sprintf(buf, "Window Rezised: x: %d, y: %d", msg.x, msg.y);
                logger(log_file, buf);
                memset(state.obstacles, 0, sizeof(state.obstacles));
                memset(state.targets, 0, sizeof(state.targets));
                state.num_active_targets = 0;
                state.mapx = msg.x;
                state.mapy = msg.y;
                state.num_obstacles = 0;
                write(write_obs_fd, &msg, sizeof(ResizeMessage));
                write(write_tar_fd, &msg, sizeof(ResizeMessage));
                send_dyn = 1;

            }
            
        }
        if(network_socket >= 0 && FD_ISSET(network_socket, &read_fds)) {

            char buf[256];
            ssize_t n = read(network_socket, buf, sizeof(buf)-1);

            if (n <= 0) {
                close(network_socket);
                network_socket = -1;
            } else {
                buf[n] = '\0';
                // handle_network_message(buf, &state);
            }
        }
        // //Network handling
        // if(network_socket >= 0 && FD_ISSET(network_socket, &read_fds)) {
        //     char buf[256];
        //     ssize_t n = read(network_socket, buf, sizeof(buf));
        
        //     if(n <= 0) {
        //         logger(log_file, "Network connection closed");
        //         close(network_socket);
        //         network_socket MODE= -1;
        //     } else {
        //         buf[n] = '\0';
            
        //         if(strncmp(buf, "q", 1) == 0) {
        //             // Quit ricevuto
        //             write(network_socket, "qok\n", 4);
        //             logger(log_file, "Quit command from network");
        //             close(network_socket);
        //             exit(0);
        //         } else if(strncmp(buf, "drone", 5) == 0) {
        //             // Posizione drone ricevuta (client mode)
        //             float x, y;
        //             sscanf(buf + 6, "%f %f", &x, &y);
                
        //             // Tratta come ostacolo
        //             Obstacle obs;
        //             obs.x = (int)x;
        //             obs.y = (int)y;
        //             obs.active = 1;
                
        //             state.obstacles[0] = obs;
        //             write(network_socket, "dok\n", 4);
        //         }
        //     }
        // }
        // // Invia posizione drone via rete (se server o client)
        // if(network_socket >= 0) {
        //     if(nc.mode == MODE_SERVER) {
        //         send_drone_position(network_socket, state.drone.x, state.drone.y);
            
        //         // Ricevi posizione ostacolo dal client
        //         Obstacle obs;
        //         receive_obstacle_position(network_socket, &obs);
            
        //         // Aggiungi ostacolo
        //         for(int i = 0; i < 10; i++) {
        //             if(!state.obstacles[i].active) {
        //                 state.obstacles[i] = obs;
        //                 state.num_obstacles++;
        //                 break;
        //             }
        //         }
            
        //     } else if(nc.mode == MODE_CLIENT) {
        //         // Invia posizione drone come se fosse ostacolo
        //         char msg[64];
        //         sprintf(msg, "%.2f %.2f\n", state.drone.x, state.drone.y);
        //         write(network_socket, msg, strlen(msg));

        //         //MANCA RICEZIONE DRONE DAL SERVER
            
        //         // Ricevi "pok"
        //         char ack[32];
        //         read(network_socket, ack, sizeof(ack));
        //     }
        // }
        //Checking only in stanalone
        checking_collisions(&state);


        //Invio dello stato alla window per il rendering 
        ssize_t written = write(write_window_fd, &state, sizeof(WorldState));
        if(written < 0) {
            logger(log_file, "Error writing to window");
        }
        //Invio del drone a input per gestire la collisione con i muri che invertono forza e vel del drone (possibile alternativa implementare la repulsione questo mi sembra più semplice e bello)
        ssize_t i_write = write(write_input_fd, &state, sizeof(WorldState));
        if(i_write < 0) {
            logger(log_file, "Error writing to input");
        }
        //Log per controllare
        char ops[256];
        snprintf(ops, sizeof(ops),
                    "DRONE SENT TO WINDOW - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                    state.drone.x, state.drone.y,
                    state.drone.vx, state.drone.vy,
                    state.drone.fx, state.drone.fy);
        logger(log_file, ops);
        char wtar[512];
        int pos = 0;
        pos += snprintf(wtar + pos, sizeof(wtar) - pos, "TAR SENT TO DRONE - active targets:");
        for (int ti = 0; ti < MAX_TAR && pos < (int)sizeof(wtar); ++ti) {
            if (state.targets[ti].active) {
            pos += snprintf(wtar + pos, sizeof(wtar) - pos,
                            " [%d]=(%.2f,%.2f)",
                            state.targets[ti].id,
                            state.targets[ti].x, state.targets[ti].y);
            }
        }
        logger(log_file, wtar);
        //Invio a dynamic solo se ho ricevuto da input per limitare il traffico
        if(send_dyn){
        //invio a dynamic
        write(write_dynamic_fd, &state, sizeof(WorldState));

        }

    }
    
    fclose(log_file);
    return 0;
    
}