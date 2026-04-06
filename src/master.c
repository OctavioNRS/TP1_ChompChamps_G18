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
#include "shared.h"
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
    int        last_player;   // índice del último jugador que movió válido
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

    // minimos
    if (master->width  < 10) master->width  = 10;
    if (master->height < 10) master->height = 10;

    if (master->n_players < 1) {
        fprintf(stderr, "Se necesita al menos un jugador (-p)\n");
        return -1;
    }
    return 0;
}
static size_t gs_size(int w, int h) {
    return sizeof(GameState) + (size_t)(w * h) * sizeof(char);
}

static GameState *create_game_state(int width, int height) {
    size_t total = gs_size(width, height);

    shm_unlink(SHM_STATE);
    int fd = shm_open(SHM_STATE, O_CREAT | O_RDWR, 0666);
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

static SyncData *create_sync(int n_players) {
    shm_unlink(SHM_SYNC);
    int fd = shm_open(SHM_SYNC, O_CREAT | O_RDWR, 0666);
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

//mi generador de boards segun seed
static void init_board(GameState *gs, masterADT master) {
    srand((unsigned int)master->seed);

    for (int i = 0; i < master->width * master->height; i++)
        gs->board[i] = (char)(rand() % 9 + 1);

    gs->n_players = (unsigned char)master->n_players;
}

//donde quiero colocar a los jugadores, evitando colisiones
static void place_players(GameState *gs, masterADT master) {
    int margin = 1;

    for (int i = 0; i < master->n_players; i++) {
        int col, row;
        int collision;

        do {
            col = margin + rand() % (master->width  - 2 * margin);
            row = margin + rand() % (master->height - 2 * margin);

            collision = 0;
            for (int j = 0; j < i; j++) {
                if (gs->players[j].x == (unsigned short)col &&
                    gs->players[j].y == (unsigned short)row)
                    collision = 1;
            }
        } while (collision);

        gs->players[i].x             = (unsigned short)col;
        gs->players[i].y             = (unsigned short)row;
        gs->players[i].score         = 0;
        gs->players[i].valid_moves   = 0;
        gs->players[i].invalid_moves = 0;
        gs->players[i].blocked       = false;
        gs->players[i].pid           = 0;

        snprintf(gs->players[i].name, sizeof(gs->players[i].name),
                 "player%d", i);

        gs->board[row * master->width + col] = (char)(-i);
    }
}

//fork + exec de los jugadores, guardando su PID
static pid_t spawn_process(char *path, int width, int height,
                           int write_fd,        // -1 si no hay redirección
                           int *read_fds,       // array de read-ends a cerrar en el hijo
                           int n_read_fds)
{
    pid_t pid = fork();
    if (pid == -1) { perror("master: fork"); exit(1); }

    if (pid == 0) {
        // hijo

        if (write_fd != -1) {
            // redirigir write_fd -> stdout (fd 1)
            // así el jugador escribe en el pipe sin saberlo
            if (dup2(write_fd, STDOUT_FILENO) == -1) {
                perror("master: dup2"); 
                close(write_fd);
                exit(1);
            }
            close(write_fd);
        }

        // cerrar todos los read-ends heredados (no le pertenecen a este hijo)
        for (int k = 0; k < n_read_fds; k++) {
            if (read_fds[k] != -1)
                close(read_fds[k]);
        }

        char w_str[16], h_str[16];
        snprintf(w_str, sizeof(w_str), "%d", width);
        snprintf(h_str, sizeof(h_str), "%d", height);

        char *args[] = { path, w_str, h_str, NULL };
        execv(path, args);
        perror("master: execv");
        exit(1);
    }

    return pid;
}

static void print_winner(GameState *gs) {
    int winner = 0;
    for (int i = 1; i < (int)gs->n_players; i++) {
        PlayerInfo *w = &gs->players[winner];
        PlayerInfo *p = &gs->players[i];

        if (p->score > w->score) {
            winner = i;
        } else if (p->score == w->score) {
            if (p->valid_moves < w->valid_moves) {
                winner = i;
            } else if (p->valid_moves == w->valid_moves) {
                if (p->invalid_moves < w->invalid_moves) {
                    winner = i;
                }
            }
        }
    }

    // verificar empate
    int is_tie = 0;
    for (int i = 0; i < (int)gs->n_players; i++) {
        if (i == winner) continue;
        PlayerInfo *w = &gs->players[winner];
        PlayerInfo *p = &gs->players[i];
        if (p->score == w->score &&
            p->valid_moves == w->valid_moves &&
            p->invalid_moves == w->invalid_moves) {
            is_tie = 1;
        }
    }

    if (is_tie)
        printf("\n=== EMPATE ===\n");
    else
        printf("\n=== GANADOR: %s (score=%u  valid=%u  invalid=%u) ===\n",
               gs->players[winner].name,
               gs->players[winner].score,
               gs->players[winner].valid_moves,
               gs->players[winner].invalid_moves);
}

//finalizacion: esperar hijos, imprimir resultados, cerrar pipes, destruir semáforos, eliminar SHMs
static void cleanup(GameState *gs, SyncData *sd, masterADT master,
                    pid_t pids[], int total_pids) {

    // guardar tamaño antes de desmapear
    size_t gs_total = gs_size(gs->width, gs->height);

    // esperar hijos e imprimir resultado
    // los primeros n_players son jugadores; el último (si existe) es la vista
    for (int i = 0; i < total_pids; i++) {
        if (pids[i] <= 0) continue;
        int status;
        waitpid(pids[i], &status, 0);

        int is_player = (i < master->n_players);

        if (WIFEXITED(status)) {
            if (is_player)
                printf("pid %d (jugador %d): exit(%d) score=%u\n",
                       (int)pids[i], i, WEXITSTATUS(status),
                       gs->players[i].score);
            else
                printf("pid %d: exit(%d)\n", (int)pids[i], WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            if (is_player)
                printf("pid %d (jugador %d): signal %d score=%u\n",
                       (int)pids[i], i, WTERMSIG(status),
                       gs->players[i].score);
            else
                printf("pid %d: signal %d\n", (int)pids[i], WTERMSIG(status));
        }
    }

    // imprimir puntajes
    printf("\n=== resultado final ===\n");
    for (int i = 0; i < (int)gs->n_players; i++) {
        PlayerInfo *p = &gs->players[i];
        printf("  [%d] %s  score=%u  invalid=%u  valid=%u\n",
               i, p->name, p->score, p->invalid_moves, p->valid_moves);
    }

    // imprimir ganador
    print_winner(gs);

    // cerrar pipes (pueden estar en -1 si el jugador fue bloqueado durante el juego)
    for (int i = 0; i < master->n_players; i++) {
        if (master->pipes[i] != -1)
            close(master->pipes[i]);
    }

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

// Reconstruye pipes_set y pipes_max_fd a partir de los pipes activos
static void build_pipes_set(masterADT m) {
    FD_ZERO(&m->pipes_set);
    m->pipes_max_fd = 0;
    for (int i = 0; i < m->n_players; i++) {
        if (m->pipes[i] == -1) continue;
        FD_SET(m->pipes[i], &m->pipes_set);
        if (m->pipes[i] > m->pipes_max_fd)
            m->pipes_max_fd = m->pipes[i];
    }
}

// Devuelve 1 si todos los jugadores están bloqueados
static int no_player_can_move(masterADT m) {
    reader_enter(m->game_sync);
    int all_blocked = 1;
    for (int i = 0; i < m->n_players; i++) {
        if (!m->game_state->players[i].blocked) {
            all_blocked = 0;
            break;
        }
    }
    reader_leave(m->game_sync);
    return all_blocked;
}

// Marca al jugador i como bloqueado y cierra su pipe
static void pipe_set_blocked(masterADT m, int i) {
    writer_enter(m->game_sync);
    m->game_state->players[i].blocked = true;
    writer_leave(m->game_sync);
    close(m->pipes[i]);
    m->pipes[i] = -1;
    sem_post(&m->game_sync->player_ack[i]);  // despertar al jugador para que pueda salir
}

// Deltas para las 8 direcciones: 0=arriba, sentido horario
// dir:  0   1   2   3   4   5   6   7
static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

// Lee el movimiento del jugador i, lo valida y lo aplica.
// Devuelve 0 si el movimiento fue válido, -1 si no.
static int check_player(masterADT m, int i) {
    unsigned char move;
    if (read(m->pipes[i], &move, 1) <= 0) {
        pipe_set_blocked(m, i);
        return -1;
    }

    if (move > 7) {
        writer_enter(m->game_sync);
        m->game_state->players[i].invalid_moves++;
        writer_leave(m->game_sync);
        sem_post(&m->game_sync->player_ack[i]);
        return -1;
    }

    reader_enter(m->game_sync);
    int x = (int)m->game_state->players[i].x;
    int y = (int)m->game_state->players[i].y;
    reader_leave(m->game_sync);

    int nx = x + dx[move];
    int ny = y + dy[move];

    // Validar límites
    if (nx < 0 || nx >= m->width || ny < 0 || ny >= m->height) {
        writer_enter(m->game_sync);
        m->game_state->players[i].invalid_moves++;
        writer_leave(m->game_sync);
        sem_post(&m->game_sync->player_ack[i]);
        return -1;
    }

    int idx = ny * m->width + nx;
    writer_enter(m->game_sync);
    char cell = m->game_state->board[idx];
    if (cell <= 0) {
        // celda ocupada, movimiento inválido
        m->game_state->players[i].invalid_moves++;
        writer_leave(m->game_sync);
        sem_post(&m->game_sync->player_ack[i]);
        return -1;
    }
    // Aplicar movimiento
    m->game_state->players[i].score += (unsigned int)cell;
    m->game_state->board[idx] = (char)(-i);
    m->game_state->players[i].x = (unsigned short)nx;
    m->game_state->players[i].y = (unsigned short)ny;
    m->game_state->players[i].valid_moves++;
    writer_leave(m->game_sync);
    sem_post(&m->game_sync->player_ack[i]);
    return 0;
}

static int game_start(masterADT m) {
    struct timeval tv = {m->timeout_s, 0};
    time_t last_time = time(NULL);

    while (1) {
        build_pipes_set(m);

        if (no_player_can_move(m) || m->pipes_max_fd == 0) {
            writer_enter(m->game_sync);
            m->game_state->game_over = true;
            writer_leave(m->game_sync);

            for (int j = 0; j < m->n_players; j++)
                sem_post(&m->game_sync->player_ack[j]);

            if (m->view_path) {
                sem_post(&m->game_sync->view_ready);
                sem_wait(&m->game_sync->view_done);
            }
            return 0;
        }

        int elapsed = (int)(time(NULL) - last_time);
        tv.tv_sec = m->timeout_s - elapsed;
        if (tv.tv_sec < 0) tv.tv_sec = 0;

        int ready = select(m->pipes_max_fd + 1, &m->pipes_set, NULL, NULL, &tv);

        if (ready == 0 || elapsed >= m->timeout_s) {
            writer_enter(m->game_sync);
            m->game_state->game_over = true;
            writer_leave(m->game_sync);

            for (int j = 0; j < m->n_players; j++)
                sem_post(&m->game_sync->player_ack[j]);

            if (m->view_path) {
                sem_post(&m->game_sync->view_ready);
                sem_wait(&m->game_sync->view_done);
            }
            return 0;
        }

        if (ready < 0) {
            writer_enter(m->game_sync);
            m->game_state->game_over = true;
            writer_leave(m->game_sync);
            perror("master: select");
            return -1;
        }

        for (unsigned int k = 0; k < (unsigned int)m->n_players; k++) {
            unsigned int i = ((unsigned int)m->last_player + 1 + k) % (unsigned int)m->n_players;

            reader_enter(m->game_sync);
            bool is_blocked = m->game_state->players[i].blocked;
            reader_leave(m->game_sync);

            if (m->pipes[i] == -1 || is_blocked)
                continue;

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
                } else {
                    if (m->pipes[i] == -1 && m->view_path) {
                        sem_post(&m->game_sync->view_ready);
                        sem_wait(&m->game_sync->view_done);
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

    GameState *gs = create_game_state(master->width, master->height);
    master->game_state = gs;

    SyncData *sd = create_sync(master->n_players);
    master->game_sync = sd;

    init_board(gs, master);

    place_players(gs, master);

    // inicializar pipes a -1 (indica pipe cerrado/no asignado)
    for (int i = 0; i < MAX_PLAYERS; i++)
        master->pipes[i] = -1;

    // crear pipes: uno por jugador
    // pipes[i]     -> master lee movimientos
    // write_ends[i] -> se convierte en stdout del hijo
    int write_ends[MAX_PLAYERS];

    for (int i = 0; i < master->n_players; i++) {
        int fds[2];
        if (pipe(fds) == -1) { perror("master: pipe"); return 1; }
        master->pipes[i] = fds[0];
        write_ends[i]    = fds[1];
    }

    pid_t pids[MAX_PLAYERS + 1];
    int   total_pids = 0;

    for (int i = 0; i < master->n_players; i++) {
        pids[total_pids] = spawn_process(master->player_paths[i],
                                         master->width, master->height,
                                         write_ends[i],
                                         master->pipes,
                                         master->n_players);
        gs->players[i].pid = pids[total_pids];
        total_pids++;

        // padre cierra el write_end: solo el hijo lo necesita
        close(write_ends[i]);
    }

    if (master->view_path) {
        pids[total_pids++] = spawn_process(master->view_path,
                                           master->width, master->height,
                                           -1,
                                           master->pipes,
                                           master->n_players);
    }

    master->last_player = master->n_players - 1;  // primera iteración arranca en jugador 0
    game_start(master);

    cleanup(gs, sd, master, pids, total_pids);
    return 0;
}