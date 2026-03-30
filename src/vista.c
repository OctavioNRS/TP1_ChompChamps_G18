
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
        fprintf(stderr, "vista: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);

    size_t total = sizeof(GameState) + (size_t)(width * height) * sizeof(char);

    // abrir /game_state
    // la vista solo lee el estado → O_RDONLY alcanza
    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) { perror("vista: shm_open game_state"); return 1; }

    GameState *gs = mmap(NULL, total,
                         PROT_READ, MAP_SHARED, fd1, 0);
    if (gs == MAP_FAILED) { perror("vista: mmap game_state"); return 1; }
    close(fd1);

    // abrir /game_sync
    // necesita O_RDWR porque hace sem_wait y sem_post en A y B
    int fd2 = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd2 == -1) { perror("vista: shm_open game_sync"); return 1; }

    SyncData *sd = mmap(NULL, sizeof(SyncData),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (sd == MAP_FAILED) { perror("vista: mmap game_sync"); return 1; }
    close(fd2);

    fprintf(stderr, "vista: conectada — tablero %dx%d\n",
            gs->width, gs->height);

    //aaca irá el loop de impresion
    //REVISAR CODIGO HECHO APURADO
    // sem_wait(A) → imprimir estado → sem_post(B)
    while (!gs->game_over) {
    sem_wait(&sd->view_ready);    // esperar que el master indique cambios (A)
    
    // imprimir estado (por ahora solo un placeholder)
    fprintf(stderr, "vista: imprimiendo estado\n");
    
    sem_post(&sd->view_done);     // notificar al master que terminó (B)
    }
    // (se implementa en la proxima etapa)
    //REVISAR CODIGO HECHO APURADO

    // limpieza
    munmap(gs, total);
    munmap(sd, sizeof(SyncData));
    return 0;
}