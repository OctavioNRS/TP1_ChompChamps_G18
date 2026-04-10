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

static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

static int has_valid_move(GameState *gs, int my_id, SyncData *sd) {
    reader_enter(sd);
    int x = (int)gs->players[my_id].x;
    int y = (int)gs->players[my_id].y;
    int w = (int)gs->width;
    int h = (int)gs->height;

    int found = 0;
    for (int dir = 0; dir < 8 && !found; dir++) {
        int nx = x + dx[dir];
        int ny = y + dy[dir];
        if (nx < 0 || nx >= w || ny < 0 || ny >= h) continue;
        if (gs->board[ny * w + nx] > 0) found = 1;
    }
    reader_leave(sd);
    return found;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "jugador: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);

    GameState *gs;
    SyncData *sd;
    if (shm_open_game_state(width, height, &gs) == -1) return 1;
    if (shm_open_sync_data(&sd) == -1) return 1;

    // Sin busy-wait: el master escribe gs->players[i].pid = pid del hijo
    // ANTES de que el hijo arranque (fork retorna primero en el padre),
    // por lo que cuando llegamos acá el PID ya está en la shm.
    pid_t my_pid = getpid();
    int my_id = -1;
    for (int i = 0; i < (int)gs->n_players; i++) {
        if (gs->players[i].pid == my_pid) {
            my_id = i;
            break;
        }
    }
    if (my_id == -1) {
        fprintf(stderr, "jugador: no encontré mi PID en game_state\n");
        return 1;
    }

    fprintf(stderr, "jugador: conectado — tablero %dx%d  id=%d\n",
        gs->width, gs->height, my_id);

    srand((unsigned int)my_pid);

    // Primer movimiento: player_ack[i] arranca en 1, así que
    // sem_wait lo decrementa a 0 (no bloquea) y recién ahí enviamos.
    // Esto mantiene el invariante: siempre esperamos el ACK del master
    // antes de enviar, incluyendo el primer envío.
    while (1) {
        sem_wait(&sd->player_ack[my_id]);   // bloquea hasta que master procese
 
        reader_enter(sd);
        int over = gs->game_over;
        reader_leave(sd);
        if (over) break;
 
        if (!has_valid_move(gs, my_id, sd)) break;  // sin movimientos posibles → salir limpio
 
        unsigned char move = (unsigned char)(rand() % 8);
        write(STDOUT_FILENO, &move, 1);
    }

    shm_close_game_state(gs, width, height);
    shm_close_sync_data(sd);
    return 0;
}