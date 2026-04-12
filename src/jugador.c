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

// Parámetros de estrategia BalancedPlayer (modificar para ajustar la estrategia)
#define FREEDOM_WEIGHT        5.0    // Peso para grados de libertad (prioridad de seguridad)
#define POINTS_WEIGHT         0.1    // Peso para puntos inmediatos (oportunista)
#define FUTURE_WEIGHT         0.05   // Peso para promedio de puntos futuros
#define FREEDOM_PENALTY       -1000.0 // Penalidad por poca libertad (atrapado)
#define MIN_FREEDOM_THRESHOLD 2       // Grados de libertad mínimos aceptables
#define INVALID_MOVE_SCORE    -10000.0 // Puntuación para movimientos inválidos     

static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

// Estrategia BalancedPlayer: evalúa un movimiento basándose en grados de libertad local y puntos
// Retorna una puntuación: mayor es mejor. Movimientos inválidos retornan INVALID_MOVE_SCORE.
static double evaluate_move_balanced(GameState *gs, int current_x, int current_y, int dir) {
    int w = (int)gs->width;
    int h = (int)gs->height;

    int nx = current_x + dx[dir];
    int ny = current_y + dy[dir];

    if (nx < 0 || nx >= w || ny < 0 || ny >= h)
        return INVALID_MOVE_SCORE;

    char cell_val = gs->board[ny * w + nx];
    if (cell_val <= 0)
        return INVALID_MOVE_SCORE;

    int degrees_of_freedom = 0;
    double future_points_sum = 0.0;
    for (int d = 0; d < 8; d++) {
        int nnx = nx + dx[d];
        int nny = ny + dy[d];
        if (nnx >= 0 && nnx < w && nny >= 0 && nny < h) {
            char val = gs->board[nny * w + nnx];
            if (val > 0) {
                degrees_of_freedom++;
                future_points_sum += (double)val;
            }
        }
    }

    if (degrees_of_freedom < MIN_FREEDOM_THRESHOLD) {
        return FREEDOM_PENALTY + ((double)cell_val * POINTS_WEIGHT);
    }

    double future_avg = (degrees_of_freedom > 0)
        ? (future_points_sum / (double)degrees_of_freedom)
        : 0.0;

    double score = ((double)degrees_of_freedom * FREEDOM_WEIGHT)
                 + ((double)cell_val * POINTS_WEIGHT)
                 + (future_avg * FUTURE_WEIGHT);

    return score;
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
    int my_id = find_player_id(gs, my_pid);
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
    bool continue_playing = true;
    while (continue_playing) {
        sem_wait(&sd->player_ack[my_id]);

        reader_enter(sd);
        int over = gs->game_over;
        reader_leave(sd);
        bool game_ended = over;

        if (!game_ended) {
            reader_enter(sd);
            int current_x = (int)gs->players[my_id].x;
            int current_y = (int)gs->players[my_id].y;

            double best_score = -999999.0;
            int best_moves[NUM_DIRECTIONS];
            int best_moves_count = 0;

            // Evaluar todas las 8 direcciones usando la estrategia BalancedPlayer
            for (int dir = 0; dir < NUM_DIRECTIONS; dir++) {
                double score = evaluate_move_balanced(gs, current_x, current_y, dir);

                if (score > INVALID_MOVE_SCORE) {
                    if (score > best_score) {
                        best_score = score;
                        best_moves[0] = dir;
                        best_moves_count = 1;
                    } else if (score == best_score) {
                        best_moves[best_moves_count++] = dir;
                    }
                }
            }
            reader_leave(sd);

            if (best_moves_count > 0) {
                unsigned char move = (unsigned char)best_moves[rand() % best_moves_count];
                write(STDOUT_FILENO, &move, 1);
            } else {
                continue_playing = false;
            }
        } else {
            continue_playing = false;
        }
    }

    shm_close_game_state(gs, width, height);
    shm_close_sync_data(sd);
    return 0;
}