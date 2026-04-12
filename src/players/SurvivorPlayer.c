// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <semaphore.h>
#include "../include/shared.h"

static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

// Breadth-First Search para medir el "espacio abierto" alcanzable en un máximo de profundidad.
// Cuantos más nodos alcance, más libertad tendrá en ese camino.
static int bfs_open_spaces(GameState *gs, int start_x, int start_y, int max_distance) {
    int w = (int)gs->width;
    int h = (int)gs->height;

    char *visited = calloc((size_t)w * (size_t)h, sizeof(char));
    if (!visited) return 0;

    int *queue_x = malloc((size_t)w * (size_t)h * sizeof(int));
    int *queue_y = malloc((size_t)w * (size_t)h * sizeof(int));
    int *queue_d = malloc((size_t)w * (size_t)h * sizeof(int));
    
    if (!queue_x || !queue_y || !queue_d) {
        free(visited);
        free(queue_x); free(queue_y); free(queue_d);
        return 0;
    }

    int head = 0, tail = 0;

    queue_x[tail] = start_x;
    queue_y[tail] = start_y;
    queue_d[tail] = 0;
    visited[start_y * w + start_x] = 1;
    tail++;

    int spaces = 0;

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        int d = queue_d[head];
        head++;

        spaces++;

        bool within_max_depth = (d < max_distance);
        if (within_max_depth) {
            for (int i = 0; i < 8; i++) {
                int nx = cx + dx[i];
                int ny = cy + dy[i];

                if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                    int idx = ny * w + nx;
                    if (gs->board[idx] > 0 && !visited[idx]) {
                        visited[idx] = 1;
                        queue_x[tail] = nx;
                        queue_y[tail] = ny;
                        queue_d[tail] = d + 1;
                        tail++;
                    }
                }
            }
        }
    }

    free(visited);
    free(queue_x);
    free(queue_y);
    free(queue_d);

    return spaces;
}

static double evaluate_survival_move(GameState *gs, int current_x, int current_y, int dir) {
    int w = (int)gs->width;
    int h = (int)gs->height;
    int nx = current_x + dx[dir];
    int ny = current_y + dy[dir];

    // 1. Descartar movimientos inválidos por choque de paredes o cuerpos (<= 0)
    if (nx < 0 || nx >= w || ny < 0 || ny >= h) return -10000.0;
    
    char cell_val = gs->board[ny * w + nx];
    if (cell_val <= 0) return -10000.0;

    // 2. Calcular los "grados de libertad" a largo plazo (BFS looking ahead depth=6)
    // Esto equivale a contar cuántos casilleros limpios y contiguos hay.
    double spaces_available = (double)bfs_open_spaces(gs, nx, ny, 6);

    // 3. Penalidad de Bordes / Centro
    // El Survivor le tiene fobia a las paredes. Restamos puntos por estar lejos del centro geométrico.
    double dist_x = (double)(nx - w / 2);
    double dist_y = (double)(ny - h / 2);
    // distance squared to penalize outer ring quadratically
    double center_penalty = (dist_x * dist_x + dist_y * dist_y) * 0.1;
    
    // 4. Utilizar los puntos inmediatos en la celda sólo como un tie-breaker
    // (A esta altura ya escapamos hacia la libertad)
    double immediate_pts = (double)cell_val * 0.01;

    // Score final: La supervivencia domina masivamente (Factor 10x). Penalizaciones leves y puntos ínfimos.
    double score = (spaces_available * 10.0) - center_penalty + immediate_pts;
    
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

            for (int dir = 0; dir < NUM_DIRECTIONS; dir++) {
                double score = evaluate_survival_move(gs, current_x, current_y, dir);

                if (score > -9000.0) {
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

            bool no_safe_moves_found = (best_moves_count == 0);
            if (no_safe_moves_found) {
                continue_playing = false;
            } else {
                unsigned char move = (unsigned char)best_moves[rand() % best_moves_count];
                write(STDOUT_FILENO, &move, 1);
            }
        } else {
            continue_playing = false;
        }
    }

    munmap(gs, gs_size(width, height));
    munmap(sd, sizeof(SyncData));
    return 0;
}
