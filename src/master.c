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