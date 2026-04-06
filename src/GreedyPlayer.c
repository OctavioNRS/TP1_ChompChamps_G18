#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "shared.h"

static const int dx[] = { 0,  1,  1,  1,  0, -1, -1, -1 };
static const int dy[] = {-1, -1,  0,  1,  1,  1,  0, -1 };

static double evaluate_move(GameState *gs, int my_id, int current_x, int current_y, int dir) {
    int w = (int)gs->width;
    int h = (int)gs->height;
    int nx = current_x + dx[dir];
    int ny = current_y + dy[dir];

    // 1. Move is invalid (out of bounds or obstacle)
    if (nx < 0 || nx >= w || ny < 0 || ny >= h) return -10000.0;
    
    char cell_val = gs->board[ny * w + nx];
    if (cell_val <= 0) return -10000.0; // Invalid move

    double immediate_points = (double)cell_val;
    
    // 2. Count degrees of freedom and future points
    int degrees_of_freedom = 0;
    double future_points_sum = 0.0;
    int obstacles_nearby = 0;

    for (int d = 0; d < 8; d++) {
        int nnx = nx + dx[d];
        int nny = ny + dy[d];
        if (nnx >= 0 && nnx < w && nny >= 0 && nny < h) {
            char val = gs->board[nny * w + nnx];
            if (val > 0) {
                degrees_of_freedom++;
                future_points_sum += (double)val;
            } else {
                obstacles_nearby++;
            }
        } else {
            obstacles_nearby++; // Wall counts as obstacle
        }
    }

    if (degrees_of_freedom == 0) {
        // Moving here leads to immediate death (trap)
        // Heavily penalize, but still factor in immediate points if it's the absolute only choice
        return -5000.0 + immediate_points;
    }

    double future_points_avg = future_points_sum / (double)degrees_of_freedom;

    // Weighting function:
    // + Immediate points count the most
    // + Degrees of freedom ensures we move to open spaces
    // + Future points gives a slight edge
    // - Obstacles slightly reduce the score
    double score = immediate_points * 10.0;
    score += (double)degrees_of_freedom * 2.0;
    score += future_points_avg * 1.0;
    score -= (double)obstacles_nearby * 0.5;

    return score;
}

int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "jugador: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);
    size_t total = sizeof(GameState) + (size_t)(width * height) * sizeof(char);

    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) { perror("jugador: shm_open game_state"); return 1; }
    GameState *gs = mmap(NULL, total, PROT_READ, MAP_SHARED, fd1, 0);
    if (gs == MAP_FAILED) { perror("jugador: mmap game_state"); return 1; }
    close(fd1);

    int fd2 = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd2 == -1) { perror("jugador: shm_open game_sync"); return 1; }
    SyncData *sd = mmap(NULL, sizeof(SyncData),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (sd == MAP_FAILED) { perror("jugador: mmap game_sync"); return 1; }
    close(fd2);

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
    while (!gs->game_over) {
        sem_wait(&sd->player_ack[my_id]);   // bloquea hasta que master procese

        if (gs->game_over) break;

        reader_enter(sd);
        int current_x = (int)gs->players[my_id].x;
        int current_y = (int)gs->players[my_id].y;

        double best_score = -999999.0;
        int best_moves[8];
        int best_moves_count = 0;

        for (int dir = 0; dir < 8; dir++) {
            double score = evaluate_move(gs, my_id, current_x, current_y, dir);
            
            // Si el score es mayor a -9000, es un movimiento válido (celda > 0)
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

        if (best_moves_count == 0) break; // Sin movimientos posibles → morir / salir

        // Empate: elegir al azar entre las mejores opciones
        unsigned char move = (unsigned char)best_moves[rand() % best_moves_count];
        write(STDOUT_FILENO, &move, 1);
    }

    munmap(gs, total);
    munmap(sd, sizeof(SyncData));
    return 0;
}