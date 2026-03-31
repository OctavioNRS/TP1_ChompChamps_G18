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

    if (argc < 3) {
        fprintf(stderr, "jugador: uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    int width  = atoi(argv[1]);
    int height = atoi(argv[2]);
    size_t total = sizeof(GameState) + (size_t)(width * height) * sizeof(char);

    // abrir /game_state (solo lectura)
    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) { perror("jugador: shm_open game_state"); return 1; }
    GameState *gs = mmap(NULL, total, PROT_READ, MAP_SHARED, fd1, 0);
    if (gs == MAP_FAILED) { perror("jugador: mmap game_state"); return 1; }
    close(fd1);

    // abrir /game_sync (lectura/escritura para semáforos)
    int fd2 = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd2 == -1) { perror("jugador: shm_open game_sync"); return 1; }
    SyncData *sd = mmap(NULL, sizeof(SyncData),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (sd == MAP_FAILED) { perror("jugador: mmap game_sync"); return 1; }
    close(fd2);

    // buscar mi id con reintentos por si el master no seteó el PID todavía
    int my_id = -1;
    pid_t my_pid = getpid();
    while (my_id == -1) {
        for (int i = 0; i < (int)gs->n_players; i++) {
            if (gs->players[i].pid == my_pid)
                my_id = i;
        }
    }

    fprintf(stderr, "jugador: conectado — tablero %dx%d  id=%d\n",
        gs->width, gs->height, my_id);

    srand((unsigned int)getpid());  // seed única por proceso

    // primer movimiento sin esperar semáforo
    unsigned char move = (unsigned char)(rand() % 8);
    write(STDOUT_FILENO, &move, 1);

    // loop: esperar confirmación → enviar siguiente movimiento
    while (!gs->game_over) {
    sem_wait(&sd->player_ack[my_id]);

    if (!gs->game_over) {
        move = (unsigned char)(rand() % 8);
        write(STDOUT_FILENO, &move, 1);
    }
}

    munmap(gs, total);
    munmap(sd, sizeof(SyncData));
    return 0;
}