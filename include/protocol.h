#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#define MAX_OBS 10
#define MAX_TAR 3
#define NETWORK_PORT 5555
#define NETWORK_BUFFER_SIZE 256

typedef struct Drone
{
    float x;
    float y;
    float vx;
    float vy;
    float fx, fy;
}Drone;

typedef struct Target{
    int id;
    float x;
    float y;
    int active;
}Target;

typedef struct Obstacle{
    float x;
    float y;
    int active;
}Obstacle;

typedef struct WorldState{
    Drone drone;
   Target targets[MAX_TAR];
   Obstacle obstacles[MAX_OBS];
    
    int num_active_targets;
    int num_obstacles;
    int target_reached;
    int mapx;
    int mapy;
}WorldState;

typedef enum CommandType{
    CMD_FORCE_UP,
    CMD_FORCE_DOWN,
    CMD_FORCE_LEFT,
    CMD_FORCE_RIGHT,
    CMD_FORCE_UP_LEFT,
    CMD_FORCE_UP_RIGHT,
    CMD_FORCE_DOWN_LEFT,
    CMD_FORCE_DOWN_RIGHT,
    CMD_BRAKE,
    CMD_PAUSE,
    CMD_RESET,
    CMD_QUIT
}CommandType;

typedef struct InputCommand{
    CommandType type;
    float force_x;
    float force_y;
}InputCommand;

typedef enum {
    MSG_DRONE_UPDATE = 'D',   
    MSG_TARGET = 'T',         
    MSG_OBSTACLE = 'O',     
} MessageType;

typedef struct {
    char type;            // 'D', 'T', o 'O' (vedi MessageType)
    union {
        Drone drone;
        Target target;
        Obstacle obstacle;
    } data;

} Message;

typedef struct 
{
    int x;
    int y;
}ResizeMessage;


typedef enum {
    MODE_STANDALONE,
    MODE_SERVER,
    MODE_CLIENT
} NetworkMode;

typedef struct {
    NetworkMode mode;
    char server_ip[64];
    int serve_port;
} NetworkConfig;


#endif // __PROTOCOL_H__