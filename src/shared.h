// shared.h
#ifndef SHARED_H
#define SHARED_H

#include <semaphore.h>
#include <stdbool.h>
#include <sys/types.h>

#define SHM_STATE "/game_state"
#define SHM_SYNC  "/game_sync"
#define MAX_PLAYERS 9

// XXX — información de cada jugador
typedef struct {
    char          name[16];
    unsigned int  score;
    unsigned int  valid_moves;
    unsigned int  invalid_moves;
    unsigned short x, y;
    pid_t         pid;
    bool          blocked;
} PlayerInfo;

// YYY — estado global del juego (en /game_state)
// Nota: board[] es un flexible array member, el tamaño real es width*height
typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned char  n_players;
    PlayerInfo     players[MAX_PLAYERS];
    bool           game_over;
    char           board[];   // valores: 1-9 libre, 0=inicio, -id capturada
} GameState;

// ZZZ — semáforos de sincronización (en /game_sync)
typedef struct {
    sem_t view_ready;      
    sem_t view_done;       
    sem_t no_writer;       
    sem_t state_mutex;     
    sem_t readers_mutex;   
    unsigned int readers;  
    sem_t player_ack[MAX_PLAYERS]; 
} SyncData;


void reader_enter(SyncData *sd);
void reader_leave(SyncData *sd);
void writer_enter(SyncData *sd);
void writer_leave(SyncData *sd);

#endif