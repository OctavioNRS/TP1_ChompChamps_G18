// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <semaphore.h>
#include "include/shared.h"
#include "include/process_manager.h"
#include "include/game_logic.h"

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

// Crea la estructura de estado del juego en memoria compartida
GameState *create_game_state(int width, int height) {
    size_t total = gs_size(width, height);

    shm_unlink(SHM_STATE);
    int fd = shm_open(SHM_STATE, O_CREAT | O_RDWR, SHM_PERMISSIONS);
    if (fd == -1) { perror("master: shm_open game_state"); exit(1); }
    if (ftruncate(fd, (off_t)total) == -1) { perror("master: ftruncate game_state"); exit(1); }

    GameState *gs = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (gs == MAP_FAILED) { perror("master: mmap game_state"); exit(1); }
    close(fd);

    gs->width     = (unsigned short)width;
    gs->height    = (unsigned short)height;
    gs->n_players = 0;
    gs->game_over = false;
    return gs;
}

// Crea la estructura de sincronización en memoria compartida
SyncData *create_sync(int n_players) {
    shm_unlink(SHM_SYNC);
    int fd = shm_open(SHM_SYNC, O_CREAT | O_RDWR, SHM_PERMISSIONS);
    if (fd == -1) { perror("master: shm_open game_sync"); exit(1); }
    if (ftruncate(fd, (off_t)sizeof(SyncData)) == -1) { perror("master: ftruncate game_sync"); exit(1); }

    SyncData *sd = mmap(NULL, sizeof(SyncData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (sd == MAP_FAILED) { perror("master: mmap game_sync"); exit(1); }
    close(fd);

    sem_init(&sd->view_ready,    1, 0);
    sem_init(&sd->view_done,     1, 0);
    sem_init(&sd->no_writer,     1, 1);
    sem_init(&sd->state_mutex,   1, 1);
    sem_init(&sd->readers_mutex, 1, 1);
    sd->readers = 0;

    for (int i = 0; i < n_players; i++)
        sem_init(&sd->player_ack[i], 1, 1);

    return sd;
}

// Inicializa estado compartido, sincronización y datos iniciales del juego
void setup_game_data(masterADT master, GameState **gs_out, SyncData **sd_out) {
    GameState *gs = create_game_state(master->width, master->height);
    master->game_state = gs;

    SyncData *sd = create_sync(master->n_players);
    master->game_sync = sd;

    init_board(gs, master);
    place_players(gs, master);

    *gs_out = gs;
    *sd_out = sd;
}

// Inicializa el arreglo de pipes del master en estado cerrado
void init_pipes_array(masterADT master) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        master->pipes[i] = -1;
}

// Crea un pipe por jugador. Devuelve 0 si OK, -1 en error
int create_player_pipes(masterADT master, int write_ends[MAX_PLAYERS]) {
    for (int i = 0; i < master->n_players; i++) {
        int fds[2];
        if (pipe(fds) == -1) {
            perror("master: pipe");
            for (int j = 0; j < i; j++) {
                close(master->pipes[j]);
                close(write_ends[j]);
                master->pipes[j] = -1;
            }
            return -1;
        }
        master->pipes[i] = fds[0];
        write_ends[i]    = fds[1];
    }

    return 0;
}

// Crea un proceso hijo (fork + exec) para un jugador
pid_t spawn_process(char *path, int width, int height,
                    int write_fd,        // -1 si no hay redirección
                    int *all_write_fds,
                    int n_write_fds,
                    int *read_fds,
                    int n_read_fds)
{
    pid_t pid = fork();
    if (pid == -1) { perror("master: fork"); exit(1); }

    if (pid == 0) {
        if (write_fd != -1) {
            if (dup2(write_fd, STDOUT_FILENO) == -1) {
                perror("master: dup2");
                close(write_fd);
                exit(1);
            }
            close(write_fd);
        }

        for (int k = 0; k < n_read_fds; k++) {
            if (read_fds[k] != -1)
                close(read_fds[k]);
        }

        for (int i = 0; i < n_write_fds; i++) {
            if (all_write_fds[i] != -1)
                close(all_write_fds[i]);
        }

        char w_str[COORD_STR_SIZE], h_str[COORD_STR_SIZE];
        snprintf(w_str, sizeof(w_str), "%d", width);
        snprintf(h_str, sizeof(h_str), "%d", height);

        char *args[] = { path, w_str, h_str, NULL };
        execv(path, args);
        perror("master: execv");
        exit(1);
    }

    return pid;
}

// Lanza todos los procesos de jugadores y, si existe, la vista
int spawn_game_processes(masterADT master,
                         int write_ends[MAX_PLAYERS],
                         pid_t pids[MAX_PLAYERS + 1]) {
    int total_pids = 0;

    for (int i = 0; i < master->n_players; i++) {
        pids[total_pids] = spawn_process(master->player_paths[i],
                                         master->width, master->height,
                                         write_ends[i],
                                         write_ends, master->n_players,
                                         master->pipes,
                                         master->n_players);
        master->game_state->players[i].pid = pids[total_pids];
        total_pids++;

        // padre cierra el write_end: solo el hijo lo necesita
        close(write_ends[i]);
    }

    if (master->view_path) {
        pids[total_pids++] = spawn_process(master->view_path,
                                           master->width, master->height,
                                           -1,
                                           write_ends, master->n_players,
                                           master->pipes,
                                           master->n_players);
    }

    return total_pids;
}

// Cierra todos los pipes de lectura de jugadores aún abiertos
void close_open_player_pipes(masterADT master) {
    for (int i = 0; i < master->n_players; i++) {
        if (master->pipes[i] != -1) {
            close(master->pipes[i]);
            master->pipes[i] = -1;
        }
    }
}

// Actualiza el conjunto de file descriptors activos para select()
void build_pipes_set(masterADT m) {
    FD_ZERO(&m->pipes_set);
    m->pipes_max_fd = 0;
    for (int i = 0; i < m->n_players; i++) {
        bool pipe_active = (m->pipes[i] != -1);
        if (pipe_active) {
            FD_SET(m->pipes[i], &m->pipes_set);
            if (m->pipes[i] > m->pipes_max_fd)
                m->pipes_max_fd = m->pipes[i];
        }
    }
}

// Marca el pipe del jugador i como bloqueado y limpia su estado asociado
void pipe_set_blocked(masterADT m, int i) {
    writer_enter(m->game_sync);
    m->game_state->players[i].blocked = true;
    writer_leave(m->game_sync);

    close(m->pipes[i]);
    m->pipes[i] = -1;
    sem_post(&m->game_sync->player_ack[i]);

    int status;
    waitpid(m->game_state->players[i].pid, &status, WNOHANG);
}

// Limpia recursos: cierra pipes, espera hijos, destruye semáforos, elimina SHMs
void cleanup(GameState *gs, SyncData *sd, masterADT master,
             pid_t pids[], int total_pids) {

    // guardar tamaño antes de desmapear
    size_t gs_total = gs_size(gs->width, gs->height);


    for (int i = 0; i < master->n_players; i++) {
        if (master->pipes[i] != -1) {
            close(master->pipes[i]);
            master->pipes[i] = -1;
        }
    }

    for (int i = 0; i < total_pids; i++) {
        bool valid_pid = (pids[i] > 0);
        if (valid_pid) {
            int status;
            waitpid(pids[i], &status, 0);  // Bloquear y esperar a que termine
        }
    }

    // imprimir resultados finales
    print_final_results(gs);

    // destruir semáforos
    sem_destroy(&sd->view_ready);
    sem_destroy(&sd->view_done);
    sem_destroy(&sd->no_writer);
    sem_destroy(&sd->state_mutex);
    sem_destroy(&sd->readers_mutex);
    for (int i = 0; i < master->n_players; i++)
        sem_destroy(&sd->player_ack[i]);

    // desmapear y eliminar SHMs
    munmap(gs, gs_total);
    munmap(sd, sizeof(SyncData));
    shm_unlink(SHM_STATE);
    shm_unlink(SHM_SYNC);
}
