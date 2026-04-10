// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include "shared.h"

static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

// Breadth-First Search para medir el "espacio abierto" alcanzable en un máximo de profundidad.
// Cuantos más nodos alcance, más libertad tendrá en ese camino.
static int bfs_open_spaces(GameState *gs, int start_x, int start_y, int max_distance) {
    int w = (int)gs->width;
    int h = (int)gs->height;
    
    char *visited = calloc((size_t)(w * h), sizeof(char));
    if (!visited) return 0;
    
    int *queue_x = malloc((size_t)(w * h) * sizeof(int));
    int *queue_y = malloc((size_t)(w * h) * sizeof(int));
    int *queue_d = malloc((size_t)(w * h) * sizeof(int));
    
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

        spaces++; // Un casillero libre alcanzable más

        if (d >= max_distance) continue;

        for (int i = 0; i < 8; i++) {
            int nx = cx + dx[i];
            int ny = cy + dy[i];

            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                int idx = ny * w + nx;
                // Considerar transitable sólo celdas con > 0
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
    if (cell_val <= 0) return -10000.0; // Movimiento inválido, ya pisado o pared

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
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <ID>\n", argv[0]);
        return 1;
    }
    int my_id = atoi(argv[1]);
    pid_t my_pid = getpid();
    srand((unsigned int)my_pid);

    GameState *gs = get_game_state(SHM_STATE);
    SyncData *sd = get_sync_data(SHM_SYNC);
    if (!gs || !sd) return 1;

    // Ciclo de juego idéntico al greedy/random, sólo cambia la heurística elegida de evaluación
    while (1) {
        sem_wait(&sd->player_ack[my_id]);

        reader_enter(sd);
        int over = gs->game_over;
        reader_leave(sd);
        if (over) break;

        reader_enter(sd);
        int current_x = (int)gs->players[my_id].x;
        int current_y = (int)gs->players[my_id].y;

        double best_score = -999999.0;
        int best_moves[8];
        int best_moves_count = 0;

        for (int dir = 0; dir < 8; dir++) {
            double score = evaluate_survival_move(gs, current_x, current_y, dir);
            
            // Si el score es mayor a -9000, es seguro y la celda es libre (> 0).
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

        if (best_moves_count == 0) break; // Sin escapatoria → acorralados, morir dignamente.

        unsigned char move = (unsigned char)best_moves[rand() % best_moves_count];
        write(STDOUT_FILENO, &move, 1);
    }

    unmap_shared_memory(gs, sd);
    return 0;
}
