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
int send_dyn = 0;   //flag per capire se mandare a dynamics in base a input



typedef enum {
    PROTO_INIT,           // Iniziale
    PROTO_WAIT_OOK,       // Server: aspetta "ook" dal client
    PROTO_WAIT_SIZE_ACK,  // Server: aspetta "sok" dopo invio size
    PROTO_WAIT_OK,        // Client: aspetta "ok" dal server
    PROTO_WAIT_SIZE,      // Client: aspetta "size" dal server
    PROTO_READY,          // Connessione stabilita, pronto per scambio dati
    // Server states nel loop
    PROTO_S_SEND_DRONE,   // Server: manda "drone"
    PROTO_S_SEND_DRONE_POS, // Server: manda "x y"
    PROTO_S_WAIT_DOK,     // Server: aspetta "dok"
    PROTO_S_SEND_OBST,    // Server: manda "obst"
    PROTO_S_RECEIVE_OBST_POS, // Server: aspetta "x y"
    PROTO_S_SEND_POK,     // Server: manda "pok"
    
    // Client states nel loop
    PROTO_C_WAIT_CMD,     // Client: aspetta comando (q/drone/obst)
    PROTO_C_RECEIVE_DRONE_POS, // Client: aspetta posizione drone
    PROTO_C_SEND_DOK,     // Client: manda "dok"
    PROTO_C_SEND_OBST_POS, // Client: manda posizione ostacolo
    PROTO_C_WAIT_POK,     // Client: aspetta "pok"
    PROTO_ERROR           // Errore nel protocollo
}State;

typedef struct {
    State state;
    char buffer[256];     // Buffer per messaggi parziali
    int buf_len;          // Lunghezza corrente del buffer
    int quit_requested;
    float loop_again;
} NetworkProtocol;

NetworkProtocol net_proto = {
    .state = PROTO_INIT,
    .buf_len = 0,
    .quit_requested = 0
};

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
    fclose(wd_log_file);
    fclose(common_log);
    exit(0);

}
// Rendi il socket non-bloccante
int set_nonblocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

// Setup migliorato con socket non-bloccante
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
        
        close(sock); // Chiudi socket di ascolto
        logger(log_file, "Client connected!");
        
        // Rendi non-bloccante
        if(set_nonblocking(client_sock) < 0) {
            
            close(sock);
            logger(log_file, "Blocking bad");
            perror("non blocking");
            return -1;
        }
        
        // Invia subito "ok\n"
        write(client_sock, "ok\n", 3);
        net_proto.state = PROTO_WAIT_OOK;
        
        return client_sock;
        
    } else if(nc->mode == MODE_CLIENT) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            logger(log_file, "Failed to create socket");
            perror("socket");
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
        
        // Rendi non-bloccante
        if(set_nonblocking(sock) < 0) {
            close(sock);
            logger(log_file, "Blocking bad");
            perror("non blocking");
            return -1;
        }
        
        net_proto.state = PROTO_WAIT_OK;
        
        return sock;
    }
    
    return -1;
}

void check_block(NetworkProtocol * prot){

    if(prot->loop_again > 500){
       prot->loop_again = 0; 
    if(network_mode == MODE_SERVER) prot->state = PROTO_S_SEND_DRONE;
    else prot->state = PROTO_C_WAIT_CMD;
    }
}


char* find_line(NetworkProtocol *proto) {
    for(int i = 0; i < proto->buf_len; i++) {
        if(proto->buffer[i] == '\n') {
            proto->buffer[i] = '\0'; // Termina la stringa
            return proto->buffer;
        }
    }
    return NULL; // Nessuna linea completa
}
// Rimuove una linea processata dal buffer
// void consume_line(NetworkProtocol *proto) {
//     char *newline_pos = strchr(proto->buffer, '\0');
//     if(!newline_pos) return;
    
//     int consumed = (newline_pos - proto->buffer) + 1; // +1 per \n
//     int remaining = proto->buf_len - consumed;
    
//     if(remaining > 0) {
//         memmove(proto->buffer, proto->buffer + consumed, remaining);
//     }
//     proto->buf_len = remaining;
// }
void consume_line(NetworkProtocol *proto) {
    int i;
    for(i = 0; i < proto->buf_len; i++) {
        if(proto->buffer[i] == '\0') break; // termina al primo \0
    }
    int consumed = i + 1; // +1 per \0 sostituito al \n
    int remaining = proto->buf_len - consumed;
    if(remaining > 0) {
        memmove(proto->buffer, proto->buffer + consumed, remaining);
    }
    proto->buf_len = remaining;
}
// Gestione protocollo server
void handle_server_protocol(int sock, NetworkProtocol *proto, WorldState *state) {
    char *line;
    char msg[128];
    switch(proto->state) {
        case PROTO_WAIT_OOK: {
            line = find_line(proto);
            if(!line) return; // Aspetta più dati
            
            if(strcmp(line, "ook") == 0) {
                logger(log_file, "Received 'ook' from client");
                
                // Invia dimensioni finestra
                //SEND RESIZE TO WINDOW
                ResizeMessage rmsg;
                rmsg.x = config.map_width;
                rmsg.y = config.map_height;
                write(write_window_fd, &rmsg, sizeof(ResizeMessage));
                state->mapx = rmsg.x;
                state->mapy = rmsg.y;
                logger(log_file, "Sent Resize Network");

                // snprintf(msg, sizeof(msg), "size %d %d\n", 
                //         config.map_width, config.map_height);
                // write(sock, msg, strlen(msg));
                char size_msg[64];
                snprintf(size_msg, sizeof(size_msg), "size %d %d\n", 
                        rmsg.x, rmsg.y);
                write(sock, size_msg, strlen(size_msg));
                
                proto->state = PROTO_WAIT_SIZE_ACK;
                logger(log_file, "Sent window size, waiting for 'sok'");
            } else {
                logger(log_file, "Protocol error: expected 'ook'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
        }
        
        case PROTO_WAIT_SIZE_ACK: {
            line = find_line(proto);
            if(!line) return;
            
            int w, h;
            if(sscanf(line, "sok %d %d", &w, &h) == 2) {
                logger(log_file, "Received 'sok', protocol complete");
                char slog[100];
                
                sprintf(slog, "ACK window size: %d, %d", w,h);
                logger(log_file,slog);
                // proto->window_width = w;
                // proto->window_height = h;
                proto->state = PROTO_S_SEND_DRONE;
            } else {
                logger(log_file, "Protocol error: expected 'sok'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
        }
        
        
        // === LOOP DATI ===
        case PROTO_S_SEND_DRONE:
            // Controlla se dobbiamo fare quit
            if(proto->quit_requested) {
                write(sock, "q\n", 2);
                proto->state = PROTO_ERROR; // Aspetta qok e poi chiudi
                logger(log_file, "SERVER: Sent quit command");
                return;
            }
            
            // Invia "drone\n"
            write(sock, "drone\n", 6);
            proto->state = PROTO_S_SEND_DRONE_POS;
            logger(log_file, "SERVER: Sent 'drone'");
            break;
            
        case PROTO_S_SEND_DRONE_POS:
            // Invia posizione drone
            snprintf(msg, sizeof(msg), "%.2f %.2f\n", state->drone.x, state->drone.y);
            write(sock, msg, strlen(msg));
            proto->state = PROTO_S_WAIT_DOK;
            logger(log_file, "SERVER: Sent drone position");
            break;
            
        case PROTO_S_WAIT_DOK:
            check_block(proto);
            line = find_line(proto);
            if(!line) return;
            
            if(strcmp(line, "dok") == 0) {
                logger(log_file, "SERVER: Received 'dok'");
                proto->state = PROTO_S_SEND_OBST;
            } else {
                logger(log_file, "SERVER: Protocol error, expected 'dok'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
            
        case PROTO_S_SEND_OBST:
            // Invia "obst\n"
            check_block(proto);
            write(sock, "obst\n", 5);
            proto->state = PROTO_S_RECEIVE_OBST_POS;
            logger(log_file, "SERVER: Sent 'obst'");
            break;
            
        case PROTO_S_RECEIVE_OBST_POS:
            check_block(proto);
            line = find_line(proto);
            if(!line) return;
            
            float ox, oy;
            if(sscanf(line, "%f %f", &ox, &oy) == 2) {
                // proto->obst_x = ox;
                // proto->obst_y = oy;
                
                // // Aggiungi ostacolo allo stato
                // for(int i = 0; i < MAX_OBS; i++) {
                //     if(!state->obstacles[i].active) {
                //         state->obstacles[i].x = ox;
                //         state->obstacles[i].y = oy;
                //         state->obstacles[i].active = 1;
                //         state->num_obstacles++;
                //         break;
                //     }
                // }

                //Aggiorno client drone position
                state->obstacles[0].active = 1;
                state->obstacles[0].x = ox;
                state->obstacles[0].y = oy;
                state->num_obstacles = 1;

                
                snprintf(msg, sizeof(msg), "pok %.2f %.2f\n", ox, oy);
                write(sock, msg, strlen(msg));
                proto->state = PROTO_S_SEND_DRONE; // Torna all'inizio del loop
                
                logger(log_file, "SERVER: Received obstacle, sent 'pok', loop restart");
            } else {
                logger(log_file, "SERVER: Protocol error, expected obstacle position");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
            
        default:
            break;
    }
}

// Gestione protocollo client
void handle_client_protocol(int sock, NetworkProtocol *proto, WorldState *state) {
    char * line;
    char msg[128];
    switch(proto->state) {
        case PROTO_WAIT_OK: {
            line = find_line(proto);
            if(!line) return;
            
            if(strcmp(line, "ok") == 0) {
                logger(log_file, "Received 'ok' from server");
                write(sock, "ook\n", 4);
                proto->state = PROTO_WAIT_SIZE;
                logger(log_file, "Sent 'ook', waiting for size");
            } else {
                logger(log_file, "Protocol error: expected 'ok'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
        }
        
        case PROTO_WAIT_SIZE: {
            line = find_line(proto);
            if(!line) return;
            
            int w, h;
            if(sscanf(line, "size %d %d", &w, &h) == 2) {
                char slog[100];
                
                sprintf(slog, "Received window size: %d, %d", w,h);
                logger(log_file,slog);
                //SEND RESIZE TO WINDOW
                ResizeMessage rmsg;
                rmsg.x = w;
                rmsg.y = h;
                write(write_window_fd, &rmsg, sizeof(ResizeMessage));
                logger(log_file, "Sent Resize Network");

                

                // proto->window_width = w;
                // proto->window_height = h;
                
                // Invia conferma
                char ack[64];
                snprintf(ack, sizeof(ack), "sok %d %d\n", w, h);
                write(sock, ack, strlen(ack));
                
                proto->state = PROTO_C_WAIT_CMD;
                logger(log_file, "Protocol complete, ready for data exchange");
            } else {
                logger(log_file, "Protocol error: expected 'size'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
        }
        
          // === LOOP DATI ===
        case PROTO_C_WAIT_CMD:
            line = find_line(proto);
            if(!line) return;
            
            if(strcmp(line, "q") == 0) {
                write(sock, "qok\n", 4);
                logger(log_file, "CLIENT: Received quit, sent 'qok', exiting");
                proto->state = PROTO_ERROR;
                handle_quit();
            } else if(strcmp(line, "drone") == 0) {
                logger(log_file, "CLIENT: Received 'drone' command");
                proto->state = PROTO_C_RECEIVE_DRONE_POS;
            } else if(strcmp(line, "obst") == 0) {
                logger(log_file, "CLIENT: Received 'obst' command");
                proto->state = PROTO_C_SEND_OBST_POS;
            } else {
                logger(log_file, "CLIENT: Protocol error, unknown command");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
            
        case PROTO_C_RECEIVE_DRONE_POS:
            check_block(proto);
            line = find_line(proto);
            if(!line) return;
            
            float dx, dy;
            if(sscanf(line, "%f %f", &dx, &dy) == 2) {
                // proto->drone_x = dx;
                // proto->drone_y = dy;
                
                // // Tratta il drone remoto come un ostacolo
                // for(int i = 0; i < MAX_OBS; i++) {
                //     if(!state->obstacles[i].active) {
                //         state->obstacles[i].x = dx;
                //         state->obstacles[i].y = dy;
                //         state->obstacles[i].active = 1;
                //         state->num_obstacles++;
                //         break;
                //     }
                // }

                //Update server drone position
                state->obstacles[0].active = 1;
                state->obstacles[0].x = dx;
                state->obstacles[0].y = dy;
                state->num_obstacles = 1;
                
                
                write(sock, "dok\n", 4);
                proto->state = PROTO_C_WAIT_CMD;
                logger(log_file, "CLIENT: Received drone pos, sent 'dok'");
            } else {
                logger(log_file, "CLIENT: Protocol error, expected drone position");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
            
        case PROTO_C_SEND_OBST_POS:
            // Invia posizione del nostro drone come ostacolo
            snprintf(msg, sizeof(msg), "%.2f %.2f\n", state->drone.x, state->drone.y);
            write(sock, msg, strlen(msg));
            proto->state = PROTO_C_WAIT_POK;
            logger(log_file, "CLIENT: Sent obstacle position (our drone)");
            break;
            
        case PROTO_C_WAIT_POK:
            check_block(proto);

            line = find_line(proto);
            if(!line) return;
            
            float px, py;
            if(sscanf(line, "pok %f %f", &px, &py) == 2) {
                logger(log_file, "CLIENT: Received 'pok', loop restart");
                proto->state = PROTO_C_WAIT_CMD;
            } else {
                logger(log_file, "CLIENT: Protocol error, expected 'pok'");
                proto->state = PROTO_ERROR;
            }
            consume_line(proto);
            break;
            
        default:
            break;
            
    }
}
// Controlla se lo stato corrente richiede dati in arrivo
int state_needs_input(State state) {
    switch(state) {
        // Stati che aspettano dati dal socket
        case PROTO_WAIT_OOK:
        case PROTO_WAIT_SIZE_ACK:
        case PROTO_WAIT_OK:
        case PROTO_WAIT_SIZE:
        case PROTO_S_WAIT_DOK:
        case PROTO_C_RECEIVE_DRONE_POS:
        case PROTO_C_WAIT_CMD:
        case PROTO_S_RECEIVE_OBST_POS:
        case PROTO_C_WAIT_POK:
            return 1;
        
        // Stati che inviano dati (non aspettano)
        case PROTO_S_SEND_DRONE:
        case PROTO_S_SEND_DRONE_POS:
        case PROTO_S_SEND_OBST:
        case PROTO_C_SEND_DOK:
        case PROTO_C_SEND_OBST_POS:
        case PROTO_READY:
            return 0;
        
        default:
            return 1;
    }
}
// Funzione ausiliaria: processa il protocollo (senza leggere dal socket)
void process_protocol(int sock, NetworkMode mode, WorldState *state) {
    if(mode == MODE_SERVER) {
        handle_server_protocol(sock, &net_proto, state);
    } else if(mode == MODE_CLIENT) {
        handle_client_protocol(sock, &net_proto, state);
    }
}


// ========== FUNZIONE PRINCIPALE DA CHIAMARE NEL SELECT ==========
void handle_network_data(int sock, NetworkMode mode, WorldState *state) {
    // Prima processa gli stati che NON richiedono input (invii)
    if(!state_needs_input(net_proto.state)) {

        process_protocol(sock, mode, state);
        return;
    }
    
    // Leggi dati disponibili
    char temp_buf[256];
    ssize_t n = read(sock, temp_buf, sizeof(temp_buf));
    
    if(n < 0) {
        if(errno == EAGAIN || errno == EWOULDBLOCK) {
            // Nessun dato disponibile, ma non è un errore
            return;
        }
        logger(log_file, "Socket read error");
        close(sock);
        network_socket = -1;
        return;
    }
    
    // if(n == 0) {
    //     logger(log_file, "Connection closed by peer");
    //     close(sock);
    //     network_socket = -1;
    //     return;
    // }
    
    // Aggiungi al buffer
    if(net_proto.buf_len + n < sizeof(net_proto.buffer)) {
        memcpy(net_proto.buffer + net_proto.buf_len, temp_buf, n);
        net_proto.buf_len += n;
    } else {
        logger(log_file, "Protocol buffer overflow");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // Processa i dati ricevuti
    process_protocol(sock, mode, state);
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
            
            
        case CMD_RESET:
            //TODDO da implementare nel second ass
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


    //config
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

        log_file = fopen("log_s/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }
    else if(nc.mode == MODE_CLIENT){

        log_file = fopen("log_c/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }else{

        log_file = fopen("log/server_log.text","w");
        logger(log_file, "Server started");
        wd_log_file = fopen(WD_LOG_PATH, "a");
        common_log = fopen(COMMON_LOG, "a");
    }

    char bufpid[200];
    sprintf(bufpid, "Input: %s, Window: %s, Dynamic: %s, Obs: %s, Tar: %s, WD: %s ",
         read_input_fd_char, read_window_fd_char , read_dynamic_fd_char, read_obs_fd_char, read_tar_fd_char, watchdog_pid_str);
    logger(log_file, bufpid);



    

    

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
    char sbuf[20];
    sprintf(sbuf, "Socker FD: %d", network_socket);
    logger(log_file, sbuf);
    logger(log_file, "Entering main loop");
    
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
            // logger(log_file, baf);
        }
         // Leggi messaggi da obstacle generator
        if (nc.mode == MODE_STANDALONE && FD_ISSET(read_obs_fd, &read_fds)) {
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
        if (nc.mode == MODE_STANDALONE && FD_ISSET(read_tar_fd, &read_fds)) {
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
                if(nc.mode == MODE_STANDALONE){
                    if(write_obs_fd > 0 && write_tar_fd > 0){
                     write(write_obs_fd, &msg, sizeof(ResizeMessage));
                     write(write_tar_fd, &msg, sizeof(ResizeMessage));
                 }
                }

                send_dyn = 1;

            }
            
        }

        static struct timeval last_protocol_call = {0, 0};

        // if(network_socket > 0) {
        //     struct timeval now_tv;
        //     gettimeofday(&now_tv, NULL);
    
        //     // Calcola millisecondi dall'ultima chiamata
        //     long ms_elapsed = (now_tv.tv_sec - last_protocol_call.tv_sec) * 1000 +
        //                     (now_tv.tv_usec - last_protocol_call.tv_usec) / 1000;
    
        //     // Chiama solo se:
        //     // 1. C'è un nuovo dato nel socket (FD_ISSET), OPPURE
        //     // 2. Siamo in uno stato che deve inviare E sono passati almeno 50ms
        //     if(FD_ISSET(network_socket, &read_fds) || 
        //     // (!state_needs_input(net_proto.state) && ms_elapsed >= 20)) {
        //     ( ms_elapsed >= 5)) {

        //         logger(log_file,"ENTRO");
        //         net_proto.loop_again = ms_elapsed;
                
        
        //         handle_network_data(network_socket, nc.mode, &state);
        //         gettimeofday(&last_protocol_call, NULL);
        //     }

        //     char elbuf[20];
        //     sprintf(elbuf, "Elaspsed: %ld", ms_elapsed);
        //     logger(log_file, elbuf);
        // }
        if(network_socket > 0){

                handle_network_data(network_socket, nc.mode, &state);

        }
        if(network_socket < 0 ){
            logger(log_file, "SOCKET ANDATO GIU");
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
        if(send_dyn){
            //invio a dynamic
            write(write_dynamic_fd, &state, sizeof(WorldState));
            send_dyn = 0;

        }
        // logger(log_file, "---- End of iteration ----");

    }
    
    fclose(log_file);
    return 0;
    
}
