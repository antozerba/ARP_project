#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

#define MAX_OBS 10

typedef struct Drone
{
    float x;
    float y;
    float vx;
    float vy;
    float fx, fy;
}Drone;

//typedef struct Target{
    //int id;
    //float x;
    //float y;
    //int active;
//}Target;

typedef struct Obstacle{
    float x;
    float y;
    int active;
}Obstacle;

typedef struct WorldState{
    Drone drone;
//    Target targets[5];
   Obstacle obstacles[10];
    
    //int num_active_targets;
    int num_obstacles;
    //int target_reached;
    //int collisions;
    //float elapsed_time;
    //int paused;
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
    MSG_DRONE_UPDATE = 'D',    // Aggiornamento posizione drone
    MSG_TARGET = 'T',          // Nuovo/rimosso target
    MSG_OBSTACLE = 'O'         // Nuovo/rimosso ostacolo
} MessageType;

/**
 * Message - Messaggio generico tra processi
 * Usato da: DYNAMICS, TARGET_GEN, OBSTACLE_GEN → SERVER
 */
typedef struct {
    char type;            // 'D', 'T', o 'O' (vedi MessageType)
    union {
        Drone drone;
        //Target target;
        Obstacle obstacle;
    } data;
} Message;

// ============================================
// MESSAGGI SPECIALIZZATI
// ============================================

/**
 * DynamicsInput - Input per processo DYNAMICS
 * SERVER → DYNAMICS
 * Contiene solo le info necessarie per calcolo fisica
 */
typedef struct {
    float force_x, force_y;      // Forze comando correnti
    Obstacle obstacles[MAX_OBS]; // Ostacoli per repulsione
    //int num_obstacles;
    //int paused;                  // Flag pausa simulazione
} DynamicsInput;

/**
 * DynamicsOutput - Output da processo DYNAMICS
 * DYNAMICS → SERVER
 * Contiene solo posizione/velocità aggiornate
 * (le forze rimangono gestite dal SERVER)
 */
typedef struct {
    float x, y;           // Posizione aggiornata
    float vx, vy;         // Velocità aggiornata
} DynamicsOutput;





#endif // __PROTOCOL_H__