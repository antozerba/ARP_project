// Standard C library headers
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>

// Custom project headers
#include "utils.h"
#include <sys/select.h>
#include "protocol.h"

// System headers for file locking, signals, networking
#include <string.h>
#include <sys/file.h>
#include <signal.h>
#include <time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <errno.h>

// Math constants and library
#define _USE_MATH_DEFINES
#include <math.h>
#define M_PI 3.14159265358979323846

// Margin for boundary collision detection
#define MARGIN 1

// Collision radius for target detection
#define COLL_RAD 0.5

// Global variables for logging and configuration
FILE * log_file;                     // Main server log file
FILE * wd_log_file;                  // Watchdog log file
FILE * common_log;                   // Common shared log file
Config config = {};                  // Configuration parameters
pid_t watchdog_pid = -1;             // Watchdog process ID

// File descriptors for inter-process communication pipes
int read_input_fd, write_input_fd;       // Input module pipes
int read_window_fd, write_window_fd;     // Window module pipes
int read_dynamic_fd, write_dynamic_fd;   // Dynamic module pipes
int read_obs_fd, write_obs_fd;           // Obstacle generator pipes
int read_tar_fd, write_tar_fd;           // Target generator pipes

// Network communication variables
int network_socket = -1;                 // Socket for network communication
NetworkMode network_mode = MODE_STANDALONE;  // Current network mode

// State flags
int input_rec = 0;                       // Flag: input received this iteration
int send_dyn = 0;                        // Flag: should send to dynamic module
float alpha = 0;                         // Rotation angle for coordinate transformation
int origin = 0;                          // Origin type for coordinate system

/**
 * Protocol state machine states for network communication
 */
typedef enum {
    PROTO_INIT,           // Initial state
    PROTO_HANDSHAKE,      // Handshake phase (ok/ook/size exchange)
    PROTO_LOOP_SERVER,    // Server main data exchange loop
    PROTO_LOOP_CLIENT,    // Client main data exchange loop
    PROTO_ERROR           // Error state
} State;

/**
 * Network protocol state and buffer
 */
typedef struct {
    State state;          // Current protocol state
    char buffer[256];     // Message buffer
    int buf_len;          // Buffer length
    int quit_requested;   // Flag: quit has been requested
} NetworkProtocol;

// Initialize protocol state
NetworkProtocol net_proto = {
    .state = PROTO_INIT,
    .buf_len = 0,
    .quit_requested = 0
};

/**
 * Clean shutdown handler - closes all resources and exits
 */
void handle_quit(){
    // Close all pipe file descriptors
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
    
    // Close network socket if open
    if(network_socket >= 0) close(network_socket);

    // Close all log files
    fclose(log_file);
    fclose(wd_log_file);
    fclose(common_log);
    exit(0);
}


/**
 * Transform coordinates from local coordinate system to virtual (standardized) system
 * Handles different origin types and rotation
 * 
 * @param x_in Local x coordinate
 * @param y_in Local y coordinate
 * @param x_out Output virtual x coordinate
 * @param y_out Output virtual y coordinate
 * @param alpha Rotation angle in radians
 * @param origin Origin type (0=bottom-left, 1=top-left, 2=center)
 * @param state World state for map dimensions
 */
void local_to_virtual(float  x_in, float y_in, float *x_out, float *y_out, float alpha, int origin, WorldState *state) {
    
    
    float x = x_in;
    float y = y_in;
    
    // Convert local origin to virtual system (bottom-left origin)
    if(origin == 1) {  // top-left -> bottom-left
        y =  - y;
    } else if(origin == 2) {  // center -> bottom-left
        x = x + state->mapx / 2.0f;
        y = y + state->mapy / 2.0f;
    }
    
    // Apply rotation if needed
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

/**
 * Transform coordinates from virtual (standardized) system to local coordinate system
 * Inverse of local_to_virtual
 * 
 * @param x_in Virtual x coordinate
 * @param y_in Virtual y coordinate
 * @param x_out Output local x coordinate
 * @param y_out Output local y coordinate
 * @param alpha Rotation angle in radians
 * @param origin Origin type (0=bottom-left, 1=top-left, 2=center)
 * @param state World state for map dimensions
 */
void virtual_to_local(float x_in, float y_in, float *x_out, float *y_out, float alpha, int origin, WorldState *state) {
    
    
    float x = x_in;
    float y = y_in;
    
    // Apply inverse rotation if needed
    if(alpha != 0.0f) {
        float cos_a = cosf(-alpha);
        float sin_a = sinf(-alpha);
        float temp_x = x * cos_a - y * sin_a;
        float temp_y = x * sin_a + y * cos_a;
        x = temp_x;
        y = temp_y;
    }
    
    // Convert from virtual system (bottom-left) to local origin
    if(origin == 1) {  // bottom-left -> top-left
        y = config.map_height - y;
    } else if(origin == 2) {  // bottom-left -> center
        x = x - state->mapx / 2.0f;
        y = y - state->mapy / 2.0f;
    }
    
    *x_out = x;
    *y_out = y;
    
}


/**
 * Sets up network socket for server or client mode
 * Socket remains in blocking mode for synchronous protocol
 * 
 * @param nc Network configuration
 * @return Socket file descriptor on success, -1 on failure
 */
int setup_network_socket(NetworkConfig *nc) {
    int sock;
    struct sockaddr_in addr;
    
    if(nc->mode == MODE_SERVER) {
        // Create TCP socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        // Allow address reuse
        int opt = 1;
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        // Bind to any address on specified port
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(nc->serve_port);
        
        if(bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            logger(log_file, "Failed to bind socket");
            return -1;
        }
        
        // Listen for incoming connections
        if(listen(sock, 1) < 0) {
            close(sock);
            logger(log_file, "Failed to listen on socket");
            return -1;
        }
        
        logger(log_file, "Server waiting for connection...");
        
        // Accept client connection (blocks until client connects)
        int client_sock = accept(sock, NULL, NULL);
        if(client_sock < 0) {
            close(sock);
            logger(log_file, "Failed to accept connection");
            return -1;
        }
        
        // Close listening socket, keep client socket
        close(sock);
        logger(log_file, "Client connected!");
        
        // Socket remains blocking for synchronous protocol
        write(client_sock, "ok\n", 3);
        net_proto.state = PROTO_HANDSHAKE;
        
        return client_sock;
        
    } else if(nc->mode == MODE_CLIENT) {
        // Create TCP socket
        sock = socket(AF_INET, SOCK_STREAM, 0);
        if(sock < 0) {
            logger(log_file, "Failed to create socket");
            return -1;
        }
        
        // Configure server address
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(nc->serve_port);
        inet_pton(AF_INET, nc->server_ip, &addr.sin_addr);
        
        logger(log_file, "Connecting to server...");
        
        // Connect to server
        if(connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(sock);
            perror("connect");
            return -1;
        }
        
        logger(log_file, "Connected to server!");
        
        // Socket remains blocking for synchronous protocol
        net_proto.state = PROTO_HANDSHAKE;
        
        return sock;
    }
    
    return -1;
}

/**
 * Reads a complete line from socket (blocking)
 * Reads character by character until newline
 * 
 * @param sock Socket file descriptor
 * @param buffer Output buffer
 * @param max_len Maximum buffer length
 * @return Number of bytes read, 0 on connection close, -1 on error
 */
int read_line(int sock, char *buffer, size_t max_len) {
    size_t i = 0;
    while(i < max_len - 1) {
        char c;
        ssize_t n = read(sock, &c, 1);
        
        if(n < 0) {
            return -1;  // Error
        }
        if(n == 0) {
            return 0;   // Connection closed
        }
        
        buffer[i++] = c;
        if(c == '\n') {
            buffer[i-1] = '\0';  // Replace \n with \0
            return i;
        }
    }
    buffer[i] = '\0';
    return i;
}

/**
 * Handles initial handshake protocol for SERVER mode
 * Protocol: ok -> ook <- size -> sok <-
 * 
 * @param sock Network socket
 * @param state World state to initialize with map size
 */
void handle_server_handshake(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Read "ook" from client
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
    
    // Send map dimensions
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
    
    
    // Read confirmation "sok" with dimensions
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

/**
 * Main data exchange loop for SERVER mode
 * Protocol per iteration:
 * 1. Send "drone" + position
 * 2. Receive "dok" + dimensions
 * 3. Send "obst"
 * 4. Receive obstacle position
 * 5. Send "pok" confirmation
 * 
 * @param sock Network socket
 * @param state Current world state
 */
void handle_server_loop(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Check if quit was requested
    if(net_proto.quit_requested) {
        write(sock, "q\n", 2);
        logger(log_file, "SERVER: Sent quit");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // 1. Send drone position
    write(sock, "drone\n", 6);
    float vxd; // Virtual pose drone x
    float vyd; // Virtual pose drone y
    local_to_virtual(state->drone.x, state->drone.y, &vxd, &vyd, alpha, origin, state);
    float dx = state->drone.x;
    float dy = state->drone.y;
    snprintf(msg, sizeof(msg), "%.2f %.2f\n", vxd, vyd); 
    write(sock, msg, strlen(msg));
    logger(log_file, "Send drone");
    char vbuf[100];
    sprintf(vbuf, "Sent Virtual Drone pos: x:%f, y:%f", vxd, vyd);
    logger(log_file, vbuf);
    char dbuf[100];
    sprintf(dbuf, "Send Drone pos: x:%f, y:%f",dx , dy);
    logger(log_file, dbuf);

    // 2. Read "dok + dimensions" (blocks until arrives)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "SERVER: Connection lost waiting for dok");
        net_proto.state = PROTO_ERROR;
        return;
    }
    if(sscanf(line, "dok %f %f", &dx, &dy) != 2) {
        logger(log_file, "SERVER: Invalid dok response");
        net_proto.state = PROTO_ERROR;
       return;
    }
    logger(log_file, "Received dok");
    
    // 3. Request obstacle position
    write(sock, "obst\n", 5);
    logger(log_file, "Ask obs");
    
    // 4. Read obstacle position (blocks)
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
    
    // 5. Update state with received obstacle
    state->obstacles[0].active = 1;
    state->obstacles[0].x = ox;
    state->obstacles[0].y = oy;
    state->num_obstacles = 1;
    
    // 6. Send confirmation
    snprintf(msg, sizeof(msg), "pok %.2f %.2f\n", ox, oy);
    write(sock, msg, strlen(msg));
    logger(log_file, "send pok");
    
    // Loop complete! Will return here next iteration
    logger(log_file, "CLIENT: loop completo");
}

/**
 * Handles initial handshake protocol for CLIENT mode
 * Protocol: -> ok -> ook <- size -> sok
 * 
 * @param sock Network socket
 * @param state World state to initialize with map size
 */
void handle_client_handshake(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // Read "ok" from server
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
    
    // Send "ook"
    write(sock, "ook\n", 4);
    logger(log_file, "Sent 'ook', waiting for size");
    
    // Read map dimensions
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

    // Update state with received dimensions
    state->mapx = w;
    state->mapy = h;
    int dx = w;
    int dy = h;

    write(write_window_fd, state, sizeof(WorldState));
    
    char wbuf[20];
    sprintf(wbuf, "SIZE: %d, %d", dx, dy);
    logger(log_file, wbuf);
    
    // Send confirmation
    snprintf(msg, sizeof(msg), "sok %d %d\n", w, h);
    write(sock, msg, strlen(msg));
    
    logger(log_file, "Handshake complete, entering loop");
    net_proto.state = PROTO_LOOP_CLIENT;
}

/**
 * Main data exchange loop for CLIENT mode
 * Protocol per iteration:
 * 1. Receive "drone" command or "q" for quit
 * 2. Receive drone position
 * 3. Send "dok" confirmation
 * 4. Receive "obst" request
 * 5. Send own position as obstacle
 * 6. Receive "pok" confirmation
 * 
 * @param sock Network socket
 * @param state Current world state
 */
void handle_client_loop(int sock, WorldState *state) {
    char line[256];
    char msg[128];
    
    // 1. Read command (blocks until arrives)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "CLIENT: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    // Check for quit command
    if(strcmp(line, "q") == 0) {
        write(sock, "qok\n", 4);
        logger(log_file, "CLIENT: Received quit");
        handle_quit();
        return;
    }
    
    // 2. Must be "drone" command
    if(strcmp(line, "drone") != 0) {
        logger(log_file, "CLIENT: Protocol error, expected 'drone'");
        net_proto.state = PROTO_ERROR;
        return;
    }

    
    // 3. Read drone position (blocks)
    if(read_line(sock, line, sizeof(line)) <= 0) {
        logger(log_file, "CLIENT: Connection lost");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    
    // Read virtual coordinates and convert to local
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
    
    // 4. Update state with other drone as obstacle
    state->obstacles[0].active = 1;
    state->obstacles[0].x = dx;
    state->obstacles[0].y = dy;
    state->num_obstacles = 1;
    
    // 5. Send confirmation DOK
    snprintf(msg, sizeof(msg), "dok %.2f %.2f\n", dvx, dvy);
    write(sock, msg, strlen(msg));
    
    // 6. Read "obst" request (blocks)
    if(read_line(sock, line, sizeof(line)) <= 0 || strcmp(line, "obst") != 0) {
        logger(log_file, "CLIENT: Protocol error, expected 'obst'");
        net_proto.state = PROTO_ERROR;
        return;
    }
    
    int vxd; // Virtual pose drone x
    int vyd; // Virtual pose drone y
    
    // 7. Send our position (as obstacle for the other drone)
    snprintf(msg, sizeof(msg), "%.2f %.2f\n", state->drone.x, state->drone.y);
    write(sock, msg, strlen(msg));
    char obuf[100];
    sprintf(obuf, "Send Obs pos: x:%f, y:%f", state->drone.x, state->drone.y);
    logger(log_file, obuf);
    
    // 8. Read "pok" confirmation (blocks)
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
    
    // Loop complete! Will return here next iteration
}

/**
 * Initialize world state with default values from config
 * 
 * @param state World state to initialize
 */
void init_world_state(WorldState * state){
    memset(state, 0, sizeof(WorldState));
    state->drone.x = config.drone_x;
    state->drone.y = config.drone_y;
    // Deactivate all obstacles
    for (int i = 0; i < MAX_OBS; i++) {
        state->obstacles[i].active = 0;
        state->obstacles[i].x = -1;
        state->obstacles[i].y = -1;
    }
    state->mapx = config.map_width;
    state->mapy = config.map_height;
}

/**
 * Replace a random active obstacle with a new one
 * Used when obstacle array is full
 * 
 * @param state World state
 * @param obs New obstacle to add
 */
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

/**
 * Process input commands from input module
 * Handles brake, reset, quit, and force application
 * 
 * @param state Current world state
 * @param cmd Input command to process
 */
void handle_input_command(WorldState *state, InputCommand *cmd) {
    switch(cmd->type) {
        case CMD_BRAKE:
            // Stop all forces
            state->drone.fx = 0;
            state->drone.fy = 0;
            logger(log_file, "[SERVER] Brake applied");
            break;
        
        case CMD_RESET:
            // Reset drone to initial position and zero all motion
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
            // Apply force from command
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

/**
 * Process messages from other modules (dynamic, obstacles, targets)
 * Updates world state based on message type
 * 
 * @param state Current world state
 * @param msg Message to process
 */
void handle_message(WorldState *state, Message *msg) {
    switch(msg->type) {
        case 'D':
            // Update drone position and velocity from dynamics module
            state->drone.x = msg->data.drone.x;
            state->drone.y = msg->data.drone.y;
            state->drone.vx = msg->data.drone.vx;
            state->drone.vy = msg->data.drone.vy;
            // Optional: update forces (commented out to preserve input forces)
            // if(!input_rec){
            //     state->drone.fx = msg->data.drone.fx;
            //     state->drone.fy = msg->data.drone.fy;
            // }
            break;
        
        case 'T':
            // Add new target to first available slot
            for (int i = 0; i < MAX_TAR; i++) {
                if (!state->targets[i].active) {
                    state->targets[i] = msg->data.target;
                    state->num_active_targets++;
                    break;
                }
            }
            break;
        
        case 'O':
            // Add new obstacle
            int i = state->num_obstacles;
            if(i == MAX_OBS){
                // Array full, replace random obstacle
                replace_obs(state, &msg->data.obstacle);
                break;
            }
            // Add to first available slot
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