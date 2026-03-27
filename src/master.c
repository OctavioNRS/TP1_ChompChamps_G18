// master.c  (sólo la parte de argumentos)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "shared.h"

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

    // mínimos
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