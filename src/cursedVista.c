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
#include <ncurses.h>

// ═══════════════════════════════════════════
// Colores (ncurses)
// ═══════════════════════════════════════════

#define RESET           0
#define BOLD            A_BOLD

static const short player_colors[] = {
    COLOR_CYAN,
    COLOR_GREEN,
    COLOR_RED,
    COLOR_MAGENTA,
    COLOR_BLUE,
    COLOR_YELLOW,
    COLOR_WHITE,
    COLOR_GREEN,
    COLOR_CYAN,
};


static void draw_board(GameState *gs) {
    int w = gs->width;
    int h = gs->height;

    mvprintw(0, 0, "+");
    for (int x = 0; x < w; x++) printw("---");
    printw("+\n");

    for (int y = 0; y < h; y++) {
        mvprintw(y + 1, 0, "|");
        for (int x = 0; x < w; x++) {
            char cell = gs->board[y * w + x];

            int player_here = -1;
            for (int p = 0; p < gs->n_players; p++) {
                if (gs->players[p].x == (unsigned short)x &&
                    gs->players[p].y == (unsigned short)y)
                    player_here = p;
            }

            if (player_here >= 0) {
                attron(COLOR_PAIR(player_here + 1) | BOLD);
                mvprintw(y + 1, 1 + x * 3, "P%d ", player_here);
                attroff(COLOR_PAIR(player_here + 1) | BOLD);
            } else if (cell > 0) {
                mvprintw(y + 1, 1 + x * 3, " %d ", cell);
            } else {
                int owner = -cell;
                attron(COLOR_PAIR(owner + 1));
                if (owner == 0)
                    mvprintw(y + 1, 1 + x * 3, " 0 ");
                else
                    mvprintw(y + 1, 1 + x * 3, "-%d ", owner);
                attroff(COLOR_PAIR(owner + 1));
            }
        }
        printw("|\n");
    }

    mvprintw(h + 1, 0, "+");
    for (int x = 0; x < w; x++) printw("---");
    printw("+\n");
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

    mvprintw(gs->height + 3, 0, "=== JUGADORES ===");
    for (int i = 0; i < gs->n_players; i++) {
        int idx = order[i];
        PlayerInfo *p = &gs->players[idx];

        attron(COLOR_PAIR(idx + 1) | BOLD);
        mvprintw(gs->height + 4 + i, 0, "P%d %s: (%u) (validos: %d) (invalidos: %d) ",
                 idx, p->name, p->score, p->valid_moves, p->invalid_moves);
        attroff(COLOR_PAIR(idx + 1) | BOLD);

        if (p->blocked) {
            printw(" (bloqueado)");
        }
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

    initscr();
    start_color();
    use_default_colors();

    for (int i = 0; i < 9; i++) {
        init_pair(i + 1, player_colors[i], -1);
    }

    curs_set(0);
    noecho();

    bool should_render = true;
    while (should_render) {
        sem_wait(&sd->view_ready);

        erase();
        draw_board(gs);
        draw_players(gs);
        refresh();

        sem_post(&sd->view_done);

        should_render = !gs->game_over;
    }

    endwin();

    shm_close_game_state(gs, width, height);
    shm_close_sync_data(sd);
    return 0;
}