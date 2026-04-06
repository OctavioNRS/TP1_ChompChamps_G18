// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
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
    unsigned int  invalid_moves;
    unsigned int  valid_moves;
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
    sem_t view_ready;      // A: master → vista (hay cambios)
    sem_t view_done;       // B: vista → master (terminó de imprimir)
    sem_t no_writer;       // C: previene inanición del escritor
    sem_t state_mutex;     // D: mutex del estado
    sem_t readers_mutex;   // E: mutex para el contador F
    unsigned int readers;  // F: cantidad de jugadores leyendo
    sem_t player_ack[MAX_PLAYERS]; // G[i]: master → jugador i (movimiento procesado)
} SyncData;

void reader_enter(SyncData *sd);
void reader_leave(SyncData *sd);
void writer_enter(SyncData *sd);
void writer_leave(SyncData *sd);

#endif