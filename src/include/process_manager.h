// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// process_manager.h
#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include "shared.h"
#include <sys/types.h>

// Forward declaration
typedef struct masterCDT masterCDT;
typedef masterCDT * masterADT;

// Crea la estructura de estado del juego en memoria compartida
GameState *create_game_state(int width, int height);

// Crea la estructura de sincronización en memoria compartida
SyncData *create_sync(int n_players);

// Inicializa estado compartido, sincronización y datos iniciales del juego
void setup_game_data(masterADT master, GameState **gs_out, SyncData **sd_out);

// Inicializa el arreglo de pipes del master en estado cerrado
void init_pipes_array(masterADT master);

// Crea un pipe por jugador. Devuelve 0 si OK, -1 en error
int create_player_pipes(masterADT master, int write_ends[MAX_PLAYERS]);

// Crea un proceso hijo (fork + exec) para un jugador
pid_t spawn_process(char *path, int width, int height,
                    int write_fd,        // -1 si no hay redirección
                    int *all_write_fds,
                    int n_write_fds,
                    int *read_fds,
                    int n_read_fds);

// Lanza todos los procesos de jugadores y, si existe, la vista
int spawn_game_processes(masterADT master,
                         int write_ends[MAX_PLAYERS],
                         pid_t pids[MAX_PLAYERS + 1]);

// Cierra todos los pipes de lectura de jugadores aún abiertos
void close_open_player_pipes(masterADT master);

// Actualiza el conjunto de file descriptors activos para select()
void build_pipes_set(masterADT m);

// Marca el pipe del jugador i como bloqueado y limpia su estado asociado
void pipe_set_blocked(masterADT m, int i);

// Limpia recursos: cierra pipes, espera hijos, destruye semáforos, elimina SHMs
void cleanup(GameState *gs, SyncData *sd, masterADT master,
             pid_t pids[], int total_pids);

#endif
