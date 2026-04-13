// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/select.h>
#include "include/shared.h"
#include "include/game_logic.h"
#include "include/process_manager.h"
#include <sys/wait.h>


#define DEFAULT_WIDTH   10
#define DEFAULT_HEIGHT  10
#define DEFAULT_DELAY   200
#define DEFAULT_TIMEOUT 10

typedef struct masterCDT {
    int    width;
    int    height;
    int    delay_ms;
    int    timeout_s;
    long   seed;
    char  *view_path;
    char  *player_paths[MAX_PLAYERS];
    int    n_players;
    int    pipes[MAX_PLAYERS];
    int    pipes_max_fd;
    fd_set pipes_set;
    GameState *game_state;
    SyncData  *game_sync;
    int        last_player;
} masterCDT;

typedef masterCDT * masterADT;

// Devuelve 0 si OK, -1 si faltan jugadores o hay error
int parse_args(int argc, char *argv[], masterADT master) {
    master->width      = DEFAULT_WIDTH;
    master->height     = DEFAULT_HEIGHT;
    master->delay_ms   = DEFAULT_DELAY;
    master->timeout_s  = DEFAULT_TIMEOUT;
    master->seed       = (long)time(NULL);
    master->view_path  = NULL;
    master->n_players  = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-w") && i+1 < argc) master->width      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i+1 < argc) master->height     = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) master->delay_ms   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i+1 < argc) master->timeout_s  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i+1 < argc) master->seed       = atol(argv[++i]);
        else if (!strcmp(argv[i], "-v") && i+1 < argc) master->view_path  = argv[++i];
        else if (!strcmp(argv[i], "-p")) {
            // consume todo lo que sigue hasta el próximo flag o fin
            while (i+1 < argc && argv[i+1][0] != '-') {
                if (master->n_players >= MAX_PLAYERS) {
                    fprintf(stderr, "Máximo %d jugadores\n", MAX_PLAYERS);
                    return -1;
                }
                master->player_paths[master->n_players++] = argv[++i];
            }
        }
    }

    if (master->n_players < 1) {
        fprintf(stderr, "Se necesita al menos un jugador (-p)\n");
        return -1;
    }
    return 0;
}



static int game_start(masterADT m) {
    struct timeval tv = {m->timeout_s, 0};
    time_t last_time = time(NULL);

    while (1) {
        build_pipes_set(m);

        if (no_player_can_move(m) || m->pipes_max_fd == 0) {
            end_game(m);
            return 0;
        }

        int elapsed = (int)(time(NULL) - last_time);
        tv.tv_sec = m->timeout_s - elapsed;
        if (tv.tv_sec < 0) tv.tv_sec = 0;

        int ready = select(m->pipes_max_fd + 1, &m->pipes_set, NULL, NULL, &tv);

        if (ready == 0 || elapsed >= m->timeout_s) {
            end_game(m);
            return 0;
        }

        if (ready < 0) {
            end_game(m);
            perror("master: select");
            return -1;
        }

        bool processed_one_move = false;
        for (unsigned int k = 0; k < (unsigned int)m->n_players && !processed_one_move; k++) {
            unsigned int i = ((unsigned int)m->last_player + 1 + k) % (unsigned int)m->n_players;

            reader_enter(m->game_sync);
            bool is_blocked = m->game_state->players[i].blocked;
            reader_leave(m->game_sync);

            bool pipe_available = (m->pipes[i] != -1 && !is_blocked);
            if (pipe_available) {
                if (FD_ISSET(m->pipes[i], &m->pipes_set)) {
                    if (!check_player(m, i)) {
                        last_time = time(NULL);
                        m->last_player = (int)i;
                        if (m->view_path) {
                            sem_post(&m->game_sync->view_ready);
                            sem_wait(&m->game_sync->view_done);
                        }
                        struct timespec ts = {
                            m->delay_ms / 1000,
                            (long)(m->delay_ms % 1000) * 1000000L
                        };
                        nanosleep(&ts, NULL);
                        processed_one_move = true;  // Decisión de diseño: un movimiento válido por iteración
                    } else {
                        if (m->view_path) {
                            sem_post(&m->game_sync->view_ready);
                            sem_wait(&m->game_sync->view_done);
                        }
                    }
                }
            }
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {

    masterCDT masterData;
    masterADT master = &masterData;
    if (parse_args(argc, argv, master) == -1)
        return 1;

    GameState *gs;
    SyncData *sd;
    setup_game_data(master, &gs, &sd);

    // inicializar pipes a -1 (indica pipe cerrado/no asignado)
    init_pipes_array(master);

    // crear pipes: uno por jugador
    // pipes[i]     -> master lee movimientos
    // write_ends[i] -> se convierte en stdout del hijo
    int write_ends[MAX_PLAYERS];

    if (create_player_pipes(master, write_ends) == -1)
        return 1;

    pid_t pids[MAX_PLAYERS + 1];
    int total_pids = spawn_game_processes(master, write_ends, pids);

    master->last_player = master->n_players - 1;  // primera iteración arranca en jugador 0
    print_config(master);
    game_start(master);

    // Cerrar pipes ANTES de cleanup para evitar que jugadores queden bloqueados escribiendo
    close_open_player_pipes(master);

    cleanup(gs, sd, master, pids, total_pids);
    return 0;
}