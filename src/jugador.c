#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <semaphore.h>
#include "shared.h"

int main(int argc, char *argv[]) {

    //recibir width y height sin flags
    if (argc < 3) {
        fprintf(stderr, "jugador: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);

    // calcular tamaño total del estado (header + tablero)
    size_t total = sizeof(GameState) + (size_t)(width * height) * sizeof(char);

    // abrir /game_state
    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) { perror("jugador: shm_open game_state"); return 1; }

    GameState *gs = mmap(NULL, total,
                         PROT_READ, MAP_SHARED, fd1, 0);
    if (gs == MAP_FAILED) { perror("jugador: mmap game_state"); return 1; }
    close(fd1);

    //abrir /game_sync
    int fd2 = shm_open(SHM_SYNC, O_RDONLY, 0666);
    if (fd2 == -1) { perror("jugador: shm_open game_sync"); return 1; }

    SyncData *sd = mmap(NULL, sizeof(SyncData),
                        PROT_READ, MAP_SHARED, fd2, 0);
    if (sd == MAP_FAILED) { perror("jugador: mmap game_sync"); return 1; }
    close(fd2);

    // verificar que la conexión fue exitosa
    fprintf(stderr, "jugador: conectado — tablero %dx%d  game_over=%d\n",
            gs->width, gs->height, gs->game_over);

    // aca ira el loop de movimientos
    // (se implementa en la próxima etapa)

    // limpieza
    munmap(gs, total);
    munmap(sd, sizeof(SyncData));
    return 0;
}