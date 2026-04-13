// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "include/shared.h"

// ANSI escape codes

#define CLEAR_SCREEN    "\033[2J\033[H"
#define RESET           "\033[0m"
#define BOLD            "\033[1m"

static const char *player_colors[] = {
    "\033[36m",  // cyan
    "\033[32m",  // verde
    "\033[31m",  // rojo
    "\033[35m",  // magenta
    "\033[34m",  // azul
    "\033[33m",  // amarillo
    "\033[37m",  // blanco
    "\033[92m",  // verde claro
    "\033[96m",  // cyan claro
};


static void draw_board(GameState *gs) {
    int w = gs->width;
    int h = gs->height;

    printf("+");
    for (int x = 0; x < w; x++) printf("---");
    printf("+\n");

    for (int y = 0; y < h; y++) {
        printf("|");
        for (int x = 0; x < w; x++) {
            char cell = gs->board[y * w + x];

            int player_here = -1;
            for (int p = 0; p < gs->n_players; p++) {
                if (gs->players[p].x == (unsigned short)x &&
                    gs->players[p].y == (unsigned short)y)
                    player_here = p;
            }

            if (player_here >= 0) {
                printf("%s" BOLD "P%d" RESET " ", player_colors[player_here], player_here);
            } else if (cell > 0) {
                printf(" %d ", cell);
            } else {
                int owner = -cell;
                if (owner == 0)
                    printf("%s %d " RESET, player_colors[owner], 0);
                else
                    printf("%s-%d " RESET, player_colors[owner], owner);
            }
        }
        printf("|\n");
    }

    printf("+");
    for (int x = 0; x < w; x++) printf("---");
    printf("+\n");
}

static void draw_players(GameState *gs) {
    int order[MAX_PLAYERS];
    for (int i = 0; i < gs->n_players; i++)
        order[i] = i;

    for (int i = 0; i < gs->n_players - 1; i++) {
        for (int j = 0; j < gs->n_players - 1 - i; j++) {
            if (gs->players[order[j]].score < gs->players[order[j+1]].score) {
                int tmp = order[j];
                order[j] = order[j+1];
                order[j+1] = tmp;
            }
        }
    }

    printf("\n=== JUGADORES ===\n");
    for (int i = 0; i < gs->n_players; i++) {
        int idx = order[i];
        PlayerInfo *p = &gs->players[idx];
        printf("%s" BOLD "P%d %s: (%u) (validos: %d) (invalidos: %d) " RESET "%s\n",
               player_colors[idx], idx, p->name, p->score,
               p->valid_moves, p->invalid_moves,
               p->blocked ? " (bloqueado)" : "");
    }
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "vista: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);

    GameState *gs;
    SyncData *sd;
    if (shm_open_game_state(width, height, &gs) == -1) return 1;
    if (shm_open_sync_data(&sd) == -1) return 1;

    fprintf(stderr, "vista: conectada — tablero %dx%d\n",
            gs->width, gs->height);

    bool should_render = true;
    while (should_render) {
        sem_wait(&sd->view_ready);

        printf(CLEAR_SCREEN);
        reader_enter(sd);
        draw_board(gs);
        draw_players(gs);
        reader_leave(sd);
        fflush(stdout);

        sem_post(&sd->view_done);

        reader_enter(sd);
        should_render = !gs->game_over;
        reader_leave(sd);
    }

    shm_close_game_state(gs, width, height);
    shm_close_sync_data(sd);
    return 0;
}