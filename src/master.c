
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>    
#include <sys/mman.h>   
#include <sys/stat.h>  
#include <unistd.h>
#include "shared.h"
#include <sys/wait.h>


#define DEFAULT_WIDTH   10
#define DEFAULT_HEIGHT  10
#define DEFAULT_DELAY   200
#define DEFAULT_TIMEOUT 10

typedef struct {
    int    width;
    int    height;
    int    delay_ms;
    int    timeout_s;
    long   seed;
    char  *view_path;
    char  *player_paths[MAX_PLAYERS];
    int    n_players;
} Config;

// Devuelve 0 si OK, -1 si faltan jugadores o hay error
int parse_args(int argc, char *argv[], Config *cfg) {
    cfg->width      = DEFAULT_WIDTH;
    cfg->height     = DEFAULT_HEIGHT;
    cfg->delay_ms   = DEFAULT_DELAY;
    cfg->timeout_s  = DEFAULT_TIMEOUT;
    cfg->seed       = (long)time(NULL);
    cfg->view_path  = NULL;
    cfg->n_players  = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-w") && i+1 < argc) cfg->width      = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i+1 < argc) cfg->height     = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i+1 < argc) cfg->delay_ms   = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i+1 < argc) cfg->timeout_s  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s") && i+1 < argc) cfg->seed       = atol(argv[++i]);
        else if (!strcmp(argv[i], "-v") && i+1 < argc) cfg->view_path  = argv[++i];
        else if (!strcmp(argv[i], "-p")) {
            // consume todo lo que sigue hasta el próximo flag o fin
            while (i+1 < argc && argv[i+1][0] != '-') {
                if (cfg->n_players >= MAX_PLAYERS) {
                    fprintf(stderr, "Máximo %d jugadores\n", MAX_PLAYERS);
                    return -1;
                }
                cfg->player_paths[cfg->n_players++] = argv[++i];
            }
        }
    }

    // minimos
    if (cfg->width  < 10) cfg->width  = 10;
    if (cfg->height < 10) cfg->height = 10;

    if (cfg->n_players < 1) {
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
static void init_board(GameState *gs, Config *cfg) {
    srand((unsigned int)cfg->seed);
 
    for (int i = 0; i < cfg->width * cfg->height; i++)
        gs->board[i] = (char)(rand() % 9 + 1);
 
    gs->n_players = (unsigned char)cfg->n_players;
}

//donde quiero colocar a los jugadores, evitando colisiones
static void place_players(GameState *gs, Config *cfg) {
    int margin = 1;

    for (int i = 0; i < cfg->n_players; i++) {
        int col, row;
        int collision;

        do {
            col = margin + rand() % (cfg->width  - 2 * margin);
            row = margin + rand() % (cfg->height - 2 * margin);

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

        gs->board[row * cfg->width + col] = (char)(-i);
    }
}

//fork + exec de los jugadores, guardando su PID
static pid_t spawn_process(char *path, int width, int height,
                           int write_fd)  // -1 si no hay redirección
{
    pid_t pid = fork();
    if (pid == -1) { perror("master: fork"); exit(1); }
 
    if (pid == 0) {
        // hijo
 
        if (write_fd != -1) {
            // redirigir write_fd -> stdout (fd 1)
            // así el jugador escribe en el pipe sin saberlo
            if (dup2(write_fd, STDOUT_FILENO) == -1) {
                perror("master: dup2"); exit(1);
            }
            close(write_fd);
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

//finalizacion: esperar hijos, imprimir resultados, cerrar pipes, destruir semáforos, eliminar SHMs
static void cleanup(GameState *gs, SyncData *sd,
                    int read_ends[], int n_players,
                    pid_t pids[], int total_pids) {
 
    // esperar hijos e imprimir resultado
    for (int i = 0; i < total_pids; i++) {
        if (pids[i] <= 0) continue;
        int status;
        waitpid(pids[i], &status, 0);
 
        if (WIFEXITED(status))
            printf("pid %d: exit(%d)\n", pids[i], WEXITSTATUS(status));
        else if (WIFSIGNALED(status))
            printf("pid %d: signal %d\n", pids[i], WTERMSIG(status));
    }
 
    // imprimir puntajes
    printf("\n=== resultado final ===\n");
    for (int i = 0; i < (int)gs->n_players; i++) {
        PlayerInfo *p = &gs->players[i];
        printf("  [%d] %s  score=%u  valid=%u  invalid=%u\n",
               i, p->name, p->score, p->valid_moves, p->invalid_moves);
    }
 
    // cerrar pipes
    for (int i = 0; i < n_players; i++)
        close(read_ends[i]);
 
    // destruir semáforos
    sem_destroy(&sd->view_ready);
    sem_destroy(&sd->view_done);
    sem_destroy(&sd->no_writer);
    sem_destroy(&sd->state_mutex);
    sem_destroy(&sd->readers_mutex);
    for (int i = 0; i < n_players; i++)
        sem_destroy(&sd->player_ack[i]);
 
    // desmapear y eliminar SHMs
    munmap(gs, gs_size(gs->width, gs->height));
    munmap(sd, sizeof(SyncData));
    shm_unlink(SHM_STATE);
    shm_unlink(SHM_SYNC);
}

int main(int argc, char *argv[]) {
 
    Config cfg;
    if (parse_args(argc, argv, &cfg) == -1)
        return 1;
 
    GameState *gs = create_game_state(cfg.width, cfg.height);
 
    SyncData *sd = create_sync(cfg.n_players);
 
    init_board(gs, &cfg);
 
    place_players(gs, &cfg);
 
    // crear pipes: uno por jugador
    // read_ends[i]  -> master lee movimientos
    // write_ends[i] -> se convierte en stdout del hijo
    int read_ends[MAX_PLAYERS];
    int write_ends[MAX_PLAYERS];
 
    for (int i = 0; i < cfg.n_players; i++) {
        int fds[2];
        if (pipe(fds) == -1) { perror("master: pipe"); return 1; }
        read_ends[i]  = fds[0];
        write_ends[i] = fds[1];
    }
 
    pid_t pids[MAX_PLAYERS + 1];
    int   total_pids = 0;
 
    for (int i = 0; i < cfg.n_players; i++) {
        pids[total_pids] = spawn_process(cfg.player_paths[i],
                                         cfg.width, cfg.height,
                                         write_ends[i]);
        gs->players[i].pid = pids[total_pids];
        total_pids++;
 
        // padre cierra el write_end: solo el hijo lo necesita
        close(write_ends[i]);
    }
 
    if (cfg.view_path) {
        pids[total_pids++] = spawn_process(cfg.view_path,
                                           cfg.width, cfg.height,
                                           -1);
    }
 
    //game loop iría acá
 
    cleanup(gs, sd, read_ends, cfg.n_players, pids, total_pids);
    return 0;
}