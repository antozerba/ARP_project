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
#include <errno.h>

#define _USE_MATH_DEFINES
#include <math.h>
#define M_PI 3.14159265358979323846

#define MARGIN 1


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
int input_rec = 0;
int send_dyn = 0;
float alpha = M_PI;
int origin = 0;

typedef enum {
    PROTO_INIT,           // Iniziale
    PROTO_HANDSHAKE,      // Handshake iniziale (ok/ook/size)
    PROTO_LOOP_SERVER,    // Loop dati server (tutto in uno stato!)
    PROTO_LOOP_CLIENT,    // Loop dati client (tutto in uno stato!)
    PROTO_ERROR           // Errore
} State;

typedef struct {
    State state;
    char buffer[256];
    int buf_len;
    int quit_requested;
} NetworkProtocol;

NetworkProtocol net_proto = {
    .state = PROTO_INIT,
    .buf_len = 0,
    .quit_requested = 0
};

void handle_quit(){
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
    if(network_socket >= 0) close(network_socket);

    fclose(log_file);
    fclose(wd_log_file);
    fclose(common_log);
    exit(0);
}


// Funzioni di trasformazione coordinate
void local_to_virtual(float  x_in, float y_in, float *x_out, float *y_out, float alpha, int origin, WorldState *state) {
    
    // float cos_a = cosf(alpha);
    // float sin_a = sinf(alpha);
    // float tx = x_in * cos_a - y_in * sin_a;
    // float ty = x_in * sin_a + y_in * cos_a;
    // *x_out = tx;
    // *y_out = ty;
       float x = x_in;
    float y = y_in;
    
    // Converti l'origine locale al sistema virtuale (bottom-left)
    if(origin == 1) {  // top-left -> bottom-left
        y =  - y;
    } else if(origin == 2) {  // center -> bottom-left
        x = x + state->mapx / 2.0f;
        y = y + state->mapy / 2.0f;
    }
    
    // Applica rotazione se necessaria
    if(alpha != 0.0f) {
        float cos_a = cosf(alpha);
        float sin_a = sinf(alpha);
        float temp_x = x * cos_a - y * sin_a;
        float temp_y = x * sin_a + y * cos_a;
        x = temp_x;
        y = temp_y;
    }
    
    *x_out = x;
    *y_out = y;
    
    
}

void virtual_to_local(float x_in, float y_in, float *x_out, float *y_out, float alpha, int origin, WorldState *state) {
    
    // float cos_a = cosf(-alpha);
    // float sin_a = sinf(-alpha);
    // float tx = x_in * cos_a - y_in * sin_a;
    // float ty = x_in * sin_a + y_in * cos_a;
    // *x_out = tx;
    // *y_out = ty;
    float x = x_in;
    float y = y_in;
    
    // Applica rotazione inversa se necessaria
    if(alpha != 0.0f) {
        float cos_a = cosf(-alpha);
        float sin_a = sinf(-alpha);
        float temp_x = x * cos_a - y * sin_a;
        float temp_y = x * sin_a + y * cos_a;
        x = temp_x;
        y = temp_y;
    }
    
    // Converti dal sistema virtuale (bottom-left) all'origine locale
    if(origin == 1) {  // bottom-left -> top-left
        y = config.map_height - y;
    } else if(origin == 2) {  // bottom-left -> center
        x = x - state->mapx / 2.0f;
        y = y - state->mapy / 2.0f;
    }
    
    *x_out = x;
    *y_out = y;
    
}


// Setup socket bloccante 
int setup_network_socket(NetworkConfig *nc) {
    int sock;
    struct sockaddr_in addr;
    
    if(nc->mode == MODE_SERVER) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(nc->serve_port);
        
        if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            logger(log_file, "Failed to bind socket");
            return -1;
        }
        
        if(listen(sock, 1) < 0) {
            close(sock);
            logger(log_file, "Failed to listen on socket");
            return -1;
        }
        
        logger(log_file, "Server waiting for connection...");
        
        int client_sock = accept(sock, NULL, NULL);
        if(client_sock < 0) {
            close(sock);
            logger(log_file, "Failed to accept connection");
            return -1;
        }
        
        close(sock);
        logger(log_file, "Client connected!");
        
        // Socket rimane bloccante!
        write(client_sock, "ok\n", 3);
        net_proto.state = PROTO_HANDSHAKE;
        
        return client_sock;
        
    } else if(nc->mode == MODE_CLIENT) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nc->serve_port);
        inet_pton(AF_INET, nc->server_ip, &addr.sin_addr);
        
        logger(log_file, "Connecting to server...");
        
        if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            perror("connect");
            return -1;
        }
        
        logger(log_file, "Connected to server!");
        
        // Socket rimane bloccante!
        net_proto.state = PROTO_HANDSHAKE;
        
        return sock;
    }
    
    return -1;
}

// Legge una riga completa dal socket (bloccante)
int read_line(int sock, char *buffer, size_t max_len) {
    size_t i = 0;
    while(i < max_len - 1) {
        char c;
        ssize_t n = read(sock, &c, 1);
        
        if(n < 0) {
            return -1;  // Errore
        }
        if(n == 0) {
            return 0;   // Connessione chiusa
        }
        
        buffer[i++] = c;
        if(c == '\n') {
            buffer[i-1] = '\0';  // Sostituisci \n con \0
            return i;
        }
    }
    buffer[i] = '\0';
    return i;
}

// Gestione handshake iniziale SERVER
void handle_server_handshake(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Leggi "ook"
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "Connection lost during handshake");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    if(strcmp(line, "ook") != 0) {
        logger(log_file, "Protocol error: expected 'ook'");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    logger(log_file, "Received 'ook', sending size");
    
    // Invia dimensioni
    int dx = config.map_width;
    int dy = config.map_height;

    state->mapx = dx;
    state->mapy = dy;
    write(write_window_fd, state, sizeof(WorldState));
    
    snprintf(msg, sizeof(msg), "size %d %d\n", dx, dy);
    write(sock, msg, strlen(msg));
    char wbuf[20];
    sprintf(wbuf, "SIZE: %d, %d", dx, dy);
    logger(log_file, wbuf);
    
    
    // Leggi conferma "sok"
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "Connection lost waiting for sok");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    int w, h;
    if(sscanf(line, "sok %d %d", &w, &h) != 2) {
        logger(log_file, "Protocol error: expected 'sok'");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    logger(log_file, "Handshake complete, entering loop");
    net_proto.state = PROTO_LOOP_SERVER;
}

// Loop dati SERVER - TUTTO IN UNO STATO!
void handle_server_loop(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Check quit
    if(net_proto.quit_requested) {
        write(sock, "q\n", 2);
        logger(log_file, "SERVER: Sent quit");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // 1. Invia drone
    write(sock, "drone\n", 6);
    float vxd; //virtual pose drone x
    float vyd; //virtual pose drone y
    local_to_virtual(state->drone.x, state->drone.y, &vxd, &vyd, alpha, origin, state);
    float dx = state->drone.x;
    float dy = state->drone.y;
    snprintf(msg, sizeof(msg), "%.2f %.2f\n", vxd, vyd); 
    // snprintf(msg, sizeof(msg), "%.2f %.2f\n", state->drone.x, state->drone.y);
    write(sock, msg, strlen(msg));
    logger(log_file, "Send drone");
    char vbuf[100];
    sprintf(vbuf, "Sent Virtual Drone pos: x:%f, y:%f", vxd, vyd);
    logger(log_file, vbuf);
    char dbuf[100];
    sprintf(dbuf, "Send Drone pos: x:%f, y:%f",dx , dy);
    logger(log_file, dbuf);

    // 2. Leggi "dok" (BLOCCA finché non arriva)
    if(read_line(sock, line, sizeof(line)) <= 0 || strcmp(line, "dok") != 0) {
        logger(log_file, "SERVER: Protocol error or disconnect");
        net_proto.state = PROTO_ERROR;
        return;
    }
    logger(log_file, "Received dok");
    
    // 3. Chiedi ostacolo
    write(sock, "obst\n", 5);
    logger(log_file, "Ask obs");
    
    // 4. Leggi posizione ostacolo (BLOCCA)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "SERVER: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    float ox, oy;
    if(sscanf(line, "%f %f", &ox, &oy) != 2) {
        logger(log_file, "SERVER: Invalid obstacle data");
        net_proto.state = PROTO_ERROR;
        return;
    }
    char obuf[100];
    sprintf(obuf, "Obs received: x:%f, y:%f", ox, oy );
    logger(log_file, obuf);
    
    // 5. Aggiorna stato
    state->obstacles[0].active = 1;
    state->obstacles[0].x = ox;
    state->obstacles[0].y = oy;
    state->num_obstacles = 1;
    
    // 6. Invia conferma
    snprintf(msg, sizeof(msg), "pok %.2f %.2f\n", ox, oy);
    write(sock, msg, strlen(msg));
    logger(log_file, "send pok");
    
    // Loop completo! Tornerà qui al prossimo giro
    logger(log_file, "CLIENT: loop completo");
}

// Gestione handshake iniziale CLIENT
void handle_client_handshake(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Leggi "ok"
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "Connection lost during handshake");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    if(strcmp(line, "ok") != 0) {
        logger(log_file, "Protocol error: expected 'ok'");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // Invia "ook"
    write(sock, "ook\n", 4);
    logger(log_file, "Sent 'ook', waiting for size");
    
    // Leggi dimensioni
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "Connection lost waiting for size");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    int w, h;
    if(sscanf(line, "size %d %d", &w, &h) != 2) {
        logger(log_file, "Protocol error: expected 'size'");
        net_proto.state = PROTO_ERROR;
        return;
    }


    state->mapx = w;
    state->mapy = h;
    int dx = w;
    int dy = h;

    write(write_window_fd, state, sizeof(WorldState));
    
    char wbuf[20];
    sprintf(wbuf, "SIZE: %d, %d", dx, dy);
    logger(log_file, wbuf);
    
    snprintf(msg, sizeof(msg), "sok %d %d\n", w, h);
    write(sock, msg, strlen(msg));
    
    logger(log_file, "Handshake complete, entering loop");
    net_proto.state = PROTO_LOOP_CLIENT;
}

// Loop dati CLIENT - TUTTO IN UNO STATO!
void handle_client_loop(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // 1. Leggi comando (BLOCCA finché non arriva)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "CLIENT: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // Check quit
    if(strcmp(line, "q") == 0) {
        write(sock, "qok\n", 4);
        logger(log_file, "CLIENT: Received quit");
        handle_quit();
        return;
    }
    
    // 2. Deve essere "drone"
    if(strcmp(line, "drone") != 0) {
        logger(log_file, "CLIENT: Protocol error, expected 'drone'");
        net_proto.state = PROTO_ERROR;
        return;
    }

    
    
    
    // 3. Leggi posizione drone (BLOCCA)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "CLIENT: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // float dx, dy;
    // if(sscanf(line, "%f %f", &dx, &dy) != 2) {
    //     logger(log_file, "CLIENT: Invalid drone position");
    //     net_proto.state = PROTO_ERROR;
    //     return;
    // }
    // char dbuf[100];
    // sprintf(dbuf, "Received Drone pos: x:%f, y:%f", dx, dy);
    // logger(log_file, dbuf);
    float dvx, dvy;
    float dx, dy;
    if(sscanf(line, "%f %f", &dvx, &dvy) != 2) {
        logger(log_file, "CLIENT: Invalid drone position");
        net_proto.state = PROTO_ERROR;
        return;
    }
    virtual_to_local(dvx, dvy, &dx, &dy, alpha, origin, state);
    char vbuf[100];
    sprintf(vbuf, "Received Virtual Drone pos: x:%f, y:%f", dvx, dvy);
    logger(log_file, vbuf);
    char dbuf[100];
    sprintf(dbuf, "Received Drone pos: x:%f, y:%f", dx, dy);
    logger(log_file, dbuf);
    
    // 4. Aggiorna stato
    state->obstacles[0].active = 1;
    state->obstacles[0].x = dx;
    state->obstacles[0].y = dy;
    state->num_obstacles = 1;
    
    // 5. Invia conferma
    write(sock, "dok\n", 4);
    
    // 6. Leggi "obst" (BLOCCA)
    if(read_line(sock, line, sizeof(line)) <= 0 || strcmp(line, "obst") != 0) {
        logger(log_file, "CLIENT: Protocol error, expected 'obst'");
        net_proto.state = PROTO_ERROR;
        return;
    }
    int vxd; //virtual pose drone x
    int vyd; //virtual pose drone y
    // 7. Invia la nostra posizione (come ostacolo)
    snprintf(msg, sizeof(msg), "%.2f %.2f\n", state->drone.x, state->drone.y);
    write(sock, msg, strlen(msg));
    char obuf[100];
    sprintf(obuf, "Send Obs pos: x:%f, y:%f", state->drone.x, state->drone.y);
    logger(log_file, obuf);
    
    // 8. Leggi "pok" (BLOCCA)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "CLIENT: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    float px, py;
    if(sscanf(line, "pok %f %f", &px, &py) != 2) {
        logger(log_file, "CLIENT: Invalid pok response");
        net_proto.state = PROTO_ERROR;
        return;
    }
    logger(log_file, "CLIENT: loop completo");
    
    // Loop completo! Tornerà qui al prossimo giro
}

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
            logger(log_file, "[SERVER] Brake applied");
            break;
        
        case CMD_RESET:
            state->drone.x = config.drone_x;
            state->drone.y = config.drone_y;
            state->drone.vx = 0;
            state->drone.vy = 0;
            state->drone.fx = 0;
            state->drone.fy = 0;
            send_dyn = 0;
            logger(log_file, "DRONE RESET TO INITIAL POSITION");
            break;
        
        case CMD_QUIT:
            logger(log_file, "CHIUSURA SERVER");
            handle_quit();
            break;
        
        default:
            state->drone.fx += cmd->force_x;
            state->drone.fy += cmd->force_y;
            logger(log_file, "Aggiornamento forze");
        char ops[256];
        snprintf(ops, sizeof(ops),
                    "UPDATE DRONE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                    state->drone.x, state->drone.y,
                    state->drone.vx, state->drone.vy,
                    state->drone.fx, state->drone.fy);
        logger(log_file, ops);
            
            break;
    }
}

void handle_message(WorldState *state, Message *msg) {
    switch(msg->type) {
        case 'D':


            state->drone.x = msg->data.drone.x;
            state->drone.y = msg->data.drone.y;
            state->drone.vx = msg->data.drone.vx;
            state->drone.vy = msg->data.drone.vy;
            // if(!input_rec){
            //     state->drone.fx = msg->data.drone.fx;
            //     state->drone.fy = msg->data.drone.fy;
            // }
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
            int i = state->num_obstacles;
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
            if(abs(state->targets[i].x - drone_x)< COLL_RAD && 
               abs(state->targets[i].y - drone_y)< COLL_RAD) {
                logger(log_file, "TARGET REACHED ;)");
                state->targets[i].active = 0;
                state->num_active_targets--;
                state->target_reached++;
                
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

void checking_collisions(WorldState *state){
    checking_target(state);
}

void send_heartbeat(){
    if(watchdog_pid > 0){
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to Watchdog");
    }
}

int main(int argc, char **argv){
    // PIPE from ENV
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

    // Scrittura PID
    FILE * pid_file = fopen(PID_FILE, "a");
    if(pid_file){
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "server", getpid());
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    // Config
    if(!load_config(PARAM_PATH, &config)) {
        return 1;
    }

    // Config network
    NetworkConfig nc;
    nc.mode = network_mode;
    network_mode = getenv("NETWORK_MODE") ? atoi(getenv("NETWORK_MODE")) : nc.mode;
    strcpy(nc.server_ip, config.server_ip);
    nc.serve_port = config.server_port;
    nc.mode = network_mode;

    // Logger
    if(nc.mode == MODE_SERVER){
        log_file = fopen("log_s/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    } else if(nc.mode == MODE_CLIENT){
        log_file = fopen("log_c/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    } else {
        log_file = fopen("log/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }

    // Setup network se necessario
    if(nc.mode != MODE_STANDALONE) {
        network_socket = setup_network_socket(&nc);
        if(network_socket < 0) {
            logger(log_file, "Network setup failed, exiting");
            return 1;
        }
    }
    
    logger(log_file, "Entering main loop");
    
    WorldState state;
    init_world_state(&state);

    // Heartbeat variables
    time_t last_heartbeat = time(NULL);
    float heartbeat_interval = 1.5f;
    
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
        
        // SELECT
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(read_input_fd, &read_fds);
        FD_SET(read_window_fd, &read_fds);
        FD_SET(read_dynamic_fd, &read_fds);
        if(read_obs_fd >= 0) FD_SET(read_obs_fd, &read_fds);
        if(read_tar_fd >= 0) FD_SET(read_tar_fd, &read_fds);
        if(network_socket >= 0) FD_SET(network_socket, &read_fds);

        int max_fd = read_input_fd;
        if(read_window_fd > max_fd) max_fd = read_window_fd;
        if(read_dynamic_fd > max_fd) max_fd = read_dynamic_fd;
        if(read_obs_fd > max_fd) max_fd = read_obs_fd;
        if(read_tar_fd > max_fd) max_fd = read_tar_fd;
        if(network_socket > max_fd) max_fd = network_socket;

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 15000;

        int r = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        if(r == -1){
            logger(log_file, "Error in select");
            return 1;
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
        //Input
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
            // // send_dyn = 1;
            logger(log_file, "Invio dynamic ");
            // write(write_dynamic_fd, &state, sizeof(WorldState));
            input_rec = 1;
        }

        
        // Obstacles
        if(nc.mode == MODE_STANDALONE && FD_ISSET(read_obs_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_obs_fd, &msg, sizeof(Message));
            if(n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        
        // Targets
        if(nc.mode == MODE_STANDALONE && FD_ISSET(read_tar_fd, &read_fds)) {
            Message msg;
            ssize_t n = read(read_tar_fd, &msg, sizeof(Message));
            if(n == sizeof(Message)) {
                handle_message(&state, &msg);
            }
        }
        
        // Window resize

        if(nc.mode == MODE_STANDALONE && write_obs_fd > 0 && write_tar_fd > 0){
            if(FD_ISSET(read_window_fd, &read_fds)){
                ResizeMessage msg;
                ssize_t n = read(read_window_fd, &msg, sizeof(ResizeMessage));
                if(n == sizeof(ResizeMessage)) {
                    logger(log_file, "Window Resized");
                    memset(state.obstacles, 0, sizeof(state.obstacles));
                    memset(state.targets, 0, sizeof(state.targets));
                    state.num_active_targets = 0;
                    state.mapx = msg.x;
                    state.mapy = msg.y;
                    state.num_obstacles = 0;
                
                        write(write_obs_fd, &msg, sizeof(ResizeMessage));
                        write(write_tar_fd, &msg, sizeof(ResizeMessage));
                }
            }
        }

        // Network protocol
        if(network_socket >= 0 ){
            if(nc.mode == MODE_SERVER) {
                if(net_proto.state == PROTO_HANDSHAKE) {
                    handle_server_handshake(network_socket, &state);
                } else if(net_proto.state == PROTO_LOOP_SERVER) {
                    handle_server_loop(network_socket, &state);
                }
            } else if(nc.mode == MODE_CLIENT) {
                if(net_proto.state == PROTO_HANDSHAKE) {
                    handle_client_handshake(network_socket, &state);
                } else if(net_proto.state == PROTO_LOOP_CLIENT) {
                    handle_client_loop(network_socket, &state);
                }
            }
        }
        
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
        // logger(log_file, wtar);

        char otar[512];
        pos = 0;
        pos += snprintf(otar + pos, sizeof(otar) - pos, "OBS SENT TO WIN - active obs:");
        for (int ti = 0; ti < MAX_OBS && pos < (int)sizeof(otar); ++ti) {
            if (state.obstacles[ti].active) {
            pos += snprintf(otar + pos, sizeof(otar) - pos,
                            " (%.2f,%.2f)",
                            state.obstacles[ti].x, state.obstacles[ti].y);
            }
        }
        // logger(log_file, otar);

        //Invio a dynamic solo se ho ricevuto da input per limitare il traffico
        // if(send_dyn){
        //     logger(log_file, "invio dyn");
        //     //invio a dynamic
            // write(write_dynamic_fd, &state, sizeof(WorldState));
        //     send_dyn = 0;

        // }


        if(nc.mode != MODE_STANDALONE)
        {
            write(write_dynamic_fd, &state, sizeof(WorldState));
        }
        char wbuf[20];
        sprintf(wbuf, "SIZE: %d, %d", state.mapx, state.mapy);
        logger(log_file, wbuf);

        logger(log_file, "---- End of iteration ----");


        input_rec = 0;

    }
    
    fclose(log_file);
    return 0;
    
}