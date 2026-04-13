// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/select.h>
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

const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

void init_board(GameState *gs, masterADT master) {
    srand((unsigned int)master->seed);

    for (int i = 0; i < master->width * master->height; i++)
        gs->board[i] = (char)(rand() % MAX_BOARD_VALUE + 1);

    gs->n_players = (unsigned char)master->n_players;
}

void place_players(GameState *gs, masterADT master) {
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

void print_winner(GameState *gs) {
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

    int is_tie = 0;
    for (int i = 0; i < (int)gs->n_players; i++) {
        bool is_other_player = (i != winner);
        if (is_other_player) {
            PlayerInfo *w = &gs->players[winner];
            PlayerInfo *p = &gs->players[i];
            if (p->score == w->score &&
                p->valid_moves == w->valid_moves &&
                p->invalid_moves == w->invalid_moves) {
                is_tie = 1;
            }
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

void print_final_results(GameState *gs) {
    printf("\n=== resultado final ===\n");
    for (int i = 0; i < (int)gs->n_players; i++) {
        PlayerInfo *p = &gs->players[i];
        printf("  [%d] %s  score=%u  valid=%u  invalid=%u\n",
               i, p->name, p->score, p->valid_moves, p->invalid_moves);
    }
    print_winner(gs);
}

int no_player_can_move(masterADT m) {
    reader_enter(m->game_sync);
    int all_blocked = 1;
    bool found_active_player = false;
    for (int i = 0; i < m->n_players && !found_active_player; i++) {
        if (!m->game_state->players[i].blocked) {
            all_blocked = 0;
            found_active_player = true;
        }
    }
    reader_leave(m->game_sync);
    return all_blocked;
}

int check_player(masterADT m, int i) {
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
        m->game_state->players[i].invalid_moves++;
        writer_leave(m->game_sync);
        sem_post(&m->game_sync->player_ack[i]);
        return -1;
    }
    m->game_state->players[i].score += (unsigned int)cell;
    m->game_state->board[idx] = (char)(-i);
    m->game_state->players[i].x = (unsigned short)nx;
    m->game_state->players[i].y = (unsigned short)ny;
    m->game_state->players[i].valid_moves++;
    writer_leave(m->game_sync);
    sem_post(&m->game_sync->player_ack[i]);
    return 0;
}

void end_game(masterADT m) {
    writer_enter(m->game_sync);
    m->game_state->game_over = true;
    writer_leave(m->game_sync);

    for (int j = 0; j < m->n_players; j++)
        sem_post(&m->game_sync->player_ack[j]);

    if (m->view_path) {
        sem_post(&m->game_sync->view_ready);
        sem_wait(&m->game_sync->view_done);
    }
}

void print_config(masterADT m) {
    printf("width: %d\n", m->width);
    printf("height: %d\n", m->height);
    printf("delay: %d\n", m->delay_ms);
    printf("timeout: %d\n", m->timeout_s);
    printf("seed: %ld\n", m->seed);
    printf("view: %s\n", m->view_path ? m->view_path : "null");
    printf("num_players: %d\n", m->n_players);
    for (int i = 0; i < m->n_players; i++) {
        printf("  %s\n", m->player_paths[i]);
    }
}

