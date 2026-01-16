// Standard C library headers
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// Custom project headers
#include "utils.h"
#include "protocol.h"

// System headers for file operations and signals
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/file.h>
#include <time.h>

// Clock tick for timing calculations (microseconds)
#define CLOCK_TICK 300000
// Margin distance from map boundaries
#define MARGIN 1

// Forward declaration of repulsive force calculation function
void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) ;

// Global variables for program control and logging
volatile sig_atomic_t running = 1;  // Signal-safe flag for main loop control
FILE * log_file;                     // Main log file for this module
FILE * wd_log_file;                  // Watchdog log file
FILE * common_log;                   // Common log shared across modules
pid_t watchdog_pid = -1;             // Process ID of watchdog process
int network_mode = MODE_STANDALONE;  // Network operation mode

/**
 * Signal handler for graceful termination
 * Called when SIGTERM is received
 */
void termination_handler(int signum){
    // Log termination event
    logger(log_file, "Dynamic Terminated");
    // Set flag to exit main loop
    running =0;
    
}

/**
 * Sends heartbeat signal to watchdog process
 * Keeps watchdog informed that this process is alive
 */
void send_heartbeat(){
    if(watchdog_pid > 0){
        // Send SIGUSR1 signal to watchdog
        kill(watchdog_pid, SIGUSR1);
        logger(log_file, "Heartbeat sent to Watchdog");
    }
}



int main(int argc, char **argv){

    // Initialize log files
    log_file = fopen("log/dynamic_log.text","w");
    logger(log_file, "Dynamic module started");
    wd_log_file= fopen(WD_LOG_PATH, "a");
    common_log = fopen(COMMON_LOG, "a");

    // Write process ID to shared PID file
    FILE * pid_file = fopen(PID_FILE,"a");
    if(pid_file){
        // Lock file to prevent race conditions with other processes
        flock(fileno(pid_file), LOCK_EX);
        fprintf(pid_file,"%s %d\n", "dynamic", getpid());
        // fflush(pid_file);
        // Unlock file
        flock(fileno(pid_file), LOCK_UN);
        fclose(pid_file);
    }

    // Load configuration parameters from file
    Config config = {};
    if(!load_config(PARAM_PATH, &config))
    {
      logger(log_file, "Error loading configuration");
      return 1;
    }

    // Get pipe file descriptors and watchdog PID from environment variables
    char * read_fd_char = getenv("IN_FD");
    char * write_fd_char = getenv("OUT_FD");
    char * watchdog_pid_fd = getenv("WATCHDOG_PID");
    int watchdog_pid = atoi(watchdog_pid_fd);
    int read_fd = atoi(read_fd_char);
    int write_fd = atoi(write_fd_char);


    // Configure network settings
    NetworkConfig nc;
    
    nc.mode = network_mode;
    // Override network mode from environment if provided
    network_mode = getenv("NETWORK_MODE") ? atoi(getenv("NETWORK_MODE")) : nc.mode;
    strcpy(nc.server_ip, config.server_ip);
    nc.serve_port = config.server_port;
    nc.mode = network_mode;
    
    // Set read pipe to non-blocking mode in standalone mode
    if(nc.mode == MODE_STANDALONE)
    {
        int flags = fcntl(read_fd, F_GETFL, 0);
        fcntl(read_fd, F_SETFL, flags | O_NONBLOCK);

    }

    

    // Initialize world state with drone position and map dimensions
    WorldState state;
    memset(&state, 0, sizeof(WorldState));
    state.drone.x = config.drone_x;
    state.drone.y = config.drone_y;
    state.mapx = config.map_width;
    state.mapy = config.map_height;

    // Set up signal handler for SIGTERM (graceful termination)
    struct sigaction sa;
    sa.sa_handler = termination_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        logger(log_file, "Failed to install SIGTERM handler");
    }

    // Initialize heartbeat timing
    time_t last_hartbeat = time(NULL);
    float heartbeat_interval = 1.5f; // Send heartbeat every 1.5 seconds

    int iteration = 0;

    // Main simulation loop
    while(running){
        iteration +=1;

        // Check if it's time to send heartbeat
        time_t now = time(NULL);
        if(difftime(now, last_hartbeat) >= heartbeat_interval) {
            send_heartbeat();
            last_hartbeat = now;
            char buf[100];
            char *t = ctime(&now);
            t[strlen(t) - 1] = '\0';  // Remove newline from ctime
            sprintf(buf, "<%s><%s><%s::iteration:%d>", t, "dynamic", "main", iteration);
            safe_logger(wd_log_file, buf);
        }

        
        // Read updated world state from pipe (from server/input module)
        ssize_t bytes_read = read(read_fd, &state, sizeof(WorldState));
        
        char buffer[200];
        
        // Handle different read outcomes
        if(bytes_read == sizeof(WorldState)) {
            // Successfully received complete world state
            sprintf(buffer, "DRONE STATE RECEIVED - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        char map[50];
        sprintf(map, "MAPPA ATTUALE: xm: %d , ym: %d", state.mapx, state.mapy);
        logger(log_file, map);
        } 
        else if(bytes_read == -1 && errno == EAGAIN) {
            // No new data available (non-blocking read), continue with previous state
            // This is normal behavior, no logging needed
        }
        else if(bytes_read == 0) {
            // Pipe closed, server disconnected
            logger(log_file, "Server disconnected, exiting");
            break;
        }
        else if(bytes_read > 0) {
            // Partial read occurred (shouldn't happen with small pipes)
            sprintf(buffer, "Partial read: %zd bytes, expected %zu", 
                    bytes_read, sizeof(WorldState));
            logger(log_file, buffer);
            // Ignore and continue with current state
        }
        else {
            // Other read error occurred
            sprintf(buffer, "Read error: errno=%d", errno);
            logger(log_file, buffer);
            break;
        }


        // Calculate repulsive forces from obstacles using Latombe's method
        float frx = 0.0f, fry = 0.0f;

        compute_repulsive_forces(&state, &config, &frx, &fry);
        
        // Combine command forces with repulsive forces
        float total_fx = state.drone.fx + frx;
        float total_fy = state.drone.fy + fry;
        
        // Log all force components
        sprintf(buffer, "Forces - cmd: (%.2f,%.2f), repulsive: (%.2f,%.2f), total: (%.2f,%.2f)",
                state.drone.fx, state.drone.fy, frx, fry, total_fx, total_fy);
        logger(log_file, buffer);
        
        // Update drone position using physics equations
        // Calculate acceleration: F = ma and damping force
        float ax = (total_fx/ config.MASS) - (config.K * state.drone.vx / config.MASS);
        float ay = (total_fy / config.MASS) - (config.K * state.drone.vy / config.MASS);
        
        // Update velocity: v = v0 + a*dt
        state.drone.vx += ax * config.DT;
        state.drone.vy += ay * config.DT;
        
        // Update position: x = x0 + v*dt
        state.drone.x += state.drone.vx * config.DT;
        state.drone.y += state.drone.vy * config.DT;

        // Keep drone within map boundaries with elastic collision
        if (state.drone.x < MARGIN) { 
            state.drone.x = MARGIN; 
            state.drone.vx = -state.drone.vx;  // Reverse velocity
            state.drone.fx = -state.drone.fx;  // Reverse force
            logger(log_file,"ENTRA");
        }
        if (state.drone.y < MARGIN) { 
            state.drone.y = MARGIN; 
            state.drone.vy = -state.drone.vy; 
            state.drone.fy = -state.drone.fy;
            logger(log_file,"ENTRA");
        }
        if (state.drone.x > state.mapx- MARGIN) {
            state.drone.x = (float) state.mapx -MARGIN;
            state.drone.vx = -state.drone.vx; 
            state.drone.fx = -state.drone.fx;
            logger(log_file,"ENTRA");
        }
        if (state.drone.y > state.mapy- MARGIN) {
            state.drone.y = (float) state.mapy -MARGIN;
            state.drone.vy = -state.drone.vy; 
            state.drone.fy = -state.drone.fy;
            logger(log_file,"ENTRA");
        }

        char buf[50];
        sprintf(buf, "WINDOW SIZE: x:%d, y:%d", state.mapx, state.mapy);
        logger(log_file, buf);

        // Prepare and send updated drone state back to server
        Message msg;
        msg.type = MSG_DRONE_UPDATE;
        msg.data.drone = state.drone;
        sprintf(buffer, "Updated DRONE STATE - x: %f, y: %f, vx: %f, vy: %f, fx: %f, fy: %f",
                state.drone.x, state.drone.y, state.drone.vx, state.drone.vy, state.drone.fx, state.drone.fy);  
        logger(log_file, buffer);
        write(write_fd, &msg, sizeof(Message));
        logger(log_file, "Updated drone state sent to server");
        
        // Write to common log with timestamp
        char log_buf[256];
        char *t = ctime(&now);
        t[strlen(t) - 1] = '\0';
        sprintf(log_buf, "<%s><%s><%s>", t, "DYNAMIC", buffer);
        safe_logger(common_log, log_buf);
        

        
        // Sleep for one time step
        usleep(config.DT*CLOCK_TICK);

    }
    // Clean up: close all file descriptors and files
    close(read_fd);
    close(write_fd);
    fclose(wd_log_file);
    fclose(common_log);
    fclose(log_file);
    return 0;
}

/**
 * Compute repulsive forces from obstacles using artificial potential field method
 * Based on Latombe's approach for obstacle avoidance
 * 
 * @param state Current world state containing obstacle positions
 * @param config Configuration with force parameters (ETA, RHO)
 * @param frx Output: repulsive force in x direction
 * @param fry Output: repulsive force in y direction
 */
void compute_repulsive_forces(WorldState *state, Config *config, float *frx, float *fry) {

    // Initialize potential field force components
    float Px = 0.0f;
    float Py = 0.0f;

    float drone_x = state->drone.x;
    float drone_y = state->drone.y;

    // Iterate through all obstacles
    for(int i = 0; i < MAX_OBS; i++) {
        if(!state->obstacles[i].active) continue;  // Skip inactive obstacles

        float ox = state->obstacles[i].x;
        float oy = state->obstacles[i].y;

        // Calculate distance vector from obstacle to drone
        float dx = drone_x - ox;
        float dy = drone_y - oy;

        // Calculate Euclidean distance
        float dist = sqrt(dx*dx + dy*dy);
        if(dist < 0.0001f) dist = 0.0001f;  // Prevent division by zero

        // Only apply repulsive force if within influence range (RHO)
        if(dist < config->RHO) {

            // Calculate repulsive force magnitude using Latombe's formula
            // magn = ETA * (1/d - 1/RHO) * 1/d^2
            float magn = config->ETA * (1.0/dist - 1.0/config->RHO) * (1.0/(dist*dist));
            char mag[20];
            sprintf(mag, "MAG: %f", magn);
            logger(log_file, mag);
            // Clamp maximum force magnitude
            if (magn > 20){
             magn = 20;
            }

            // Calculate normalized direction vector (away from obstacle)
            float nx = dx / dist;
            float ny = dy / dist;

            // Accumulate force contributions from all obstacles
            Px += magn * nx;
            Py += magn * ny;
        }
    }

    // If no repulsive forces, return zero
    if(Px == 0 && Py == 0) {
        *frx = 0;
        *fry = 0;
        return;
    }

    // Define 8 cardinal and diagonal directions
    static const float dirs[8][2] = {
        { 1,  0},   // East
        { 1, -1},   // North-East
        { 0, -1},   // North
        {-1, -1},   // North-West
        {-1,  0},   // West
        {-1,  1},   // South-West
        { 0,  1},   // South
        { 1,  1}    // South-East
    };
    
    // Normalize all direction vectors
    float normdirs[8][2];
    for(int i = 0; i < 8; i++){
        float n = sqrt(dirs[i][0]*dirs[i][0] + dirs[i][1]*dirs[i][1]);
        normdirs[i][0] = dirs[i][0]/n;
        normdirs[i][1] = dirs[i][1]/n;
    }
    
    // Project repulsive force onto each of the 8 directions
    float proj[8];
    for(int i = 0; i < 8; i++){
        proj[i] = Px * normdirs[i][0] + Py * normdirs[i][1];
    }
    
    // Find direction with maximum projection (best alignment with repulsive force)
    float maxVal = proj[0];
    int maxIdx = 0;
    for(int i=1; i<8; i++){
        if(proj[i] > maxVal){
            maxVal = proj[i];
            maxIdx = i;
        }
    }

    // Return discretized repulsive force in the best direction
    *frx = maxVal * normdirs[maxIdx][0];
    *fry = maxVal * normdirs[maxIdx][1];
}