
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "shared.h"

// ═══════════════════════════════════════════
// ANSI escape codes
// ═══════════════════════════════════════════

#define CLEAR_SCREEN    "\033[2J\033[H"
#define RESET           "\033[0m"
#define BOLD            "\033[1m"

// colores por jugador (0-8)
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

    // borde superior
    printf("+");
    for (int x = 0; x < w; x++) printf("--");
    printf("+\n");

    for (int y = 0; y < h; y++) {
        printf("|");
        for (int x = 0; x < w; x++) {
            char cell = gs->board[y * w + x];

            // ver si hay un jugador parado acá
            int player_here = -1;
            for (int p = 0; p < gs->n_players; p++) {
                if (gs->players[p].x == (unsigned short)x &&
                    gs->players[p].y == (unsigned short)y)
                    player_here = p;
            }

            if (player_here >= 0) {
                printf("%s" BOLD "%2d" RESET, player_colors[player_here], player_here);
            } else if (cell > 0) {
                // celda libre: mostrar recompensa en blanco
                printf(" %d", cell);
            } else {
                // celda capturada: mostrar punto con color del dueño
                int owner = -cell;
                printf("%s ." RESET, player_colors[owner]);
            }
        }
        printf("|\n");
    }

    // borde inferior
    printf("+");
    for (int x = 0; x < w; x++) printf("--");
    printf("+\n");
}

static void draw_players(GameState *gs) {
    printf("\n=== JUGADORES ===\n");
    for (int i = 0; i < gs->n_players; i++) {
        PlayerInfo *p = &gs->players[i];
        printf("%s" BOLD "[%d] %s%s" RESET "\n",
               player_colors[i], i, p->name,
               p->blocked ? " (bloqueado)" : "");
        printf("    pos:       (%u, %u)\n",  p->x, p->y);
        printf("    puntaje:   %u\n",         p->score);
        printf("    validos:   %u  invalidos: %u\n",
               p->valid_moves, p->invalid_moves);
    }

    if (gs->game_over) {
        printf(BOLD "\n*** JUEGO TERMINADO ***\n" RESET);
    }
}


int main(int argc, char *argv[]) {

    if (argc < 3) {
        fprintf(stderr, "vista: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);
    size_t total = sizeof(GameState) + (size_t)(width * height) * sizeof(char);

    // abrir shm estado (solo lectura)
    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) { perror("vista: shm_open game_state"); return 1; }
    GameState *gs = mmap(NULL, total, PROT_READ, MAP_SHARED, fd1, 0);
    if (gs == MAP_FAILED) { perror("vista: mmap game_state"); return 1; }
    close(fd1);

    // abrir shm sync (lectura/escritura para semáforos)
    int fd2 = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd2 == -1) { perror("vista: shm_open game_sync"); return 1; }
    SyncData *sd = mmap(NULL, sizeof(SyncData),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (sd == MAP_FAILED) { perror("vista: mmap game_sync"); return 1; }
    close(fd2);

    fprintf(stderr, "vista: conectada — tablero %dx%d\n",
            gs->width, gs->height);

    // loop principal
    while (1) {
        sem_wait(&sd->view_ready);   // A: esperar que master indique cambios

        printf(CLEAR_SCREEN);
        draw_board(gs);
        draw_players(gs);
        fflush(stdout);

        sem_post(&sd->view_done);    // B: notificar al master que terminamos

        if (gs->game_over) break;
    }

    munmap(gs, total);
    munmap(sd, sizeof(SyncData));
    return 0;
}