// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "include/shared.h"
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// Readers-Writers con prevención de inanición del escritor

void reader_enter(SyncData *sd) {
   sem_wait(&sd->no_writer);      // C: esperar que no haya escritor activo o esperando
    sem_wait(&sd->readers_mutex);  // E: mutex para el contador F
    sd->readers++;                // F: incrementar cantidad de lectores
    if (sd->readers == 1)          // si soy el primer lector, bloquear el acceso a los escritores
        sem_wait(&sd->state_mutex); // D: mutex del estado  
    sem_post(&sd->readers_mutex);  // E: liberar mutex del contador F
    sem_post(&sd->no_writer);      // C: liberar para que otros lectores puedan
}

void reader_leave(SyncData *sd) {
    sem_wait(&sd->readers_mutex);  // E: mutex para el contador F
    sd->readers--;                // F: decrementar cantidad de lectores
    if (sd->readers == 0)          // si soy el último lector, liberar el acceso a los escritores
        sem_post(&sd->state_mutex); // D: mutex del estado
    sem_post(&sd->readers_mutex);  // E: liberar mutex del contador F
}

void writer_enter(SyncData *sd) {
    sem_wait(&sd->no_writer);      // C: indicar que hay un escritor esperando o activo
    sem_wait(&sd->state_mutex);     // D: mutex del estado (esperar a que no haya lectores ni escritores)
}

void writer_leave(SyncData *sd) {
   sem_post(&sd->state_mutex);     // D: liberar mutex del estado
    sem_post(&sd->no_writer);   // C: indicar que ya no hay escritor esperando o activo
}

static size_t gs_size(int w, int h) {
    return sizeof(GameState) + ((size_t)w * (size_t)h) * sizeof(char);
}

int shm_open_game_state(int width, int height, GameState **gs_ptr) {
    size_t total = gs_size(width, height);
    
    int fd = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd == -1) { 
        perror("shm_open game_state"); 
        return -1; 
    }
    
    *gs_ptr = mmap(NULL, total, PROT_READ, MAP_SHARED, fd, 0);
    if (*gs_ptr == MAP_FAILED) { 
        perror("mmap game_state"); 
        close(fd);
        return -1; 
    }
    
    close(fd);
    return 0;
}

int shm_open_sync_data(SyncData **sd_ptr) {
    int fd = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd == -1) { 
        perror("shm_open game_sync"); 
        return -1; 
    }
    
    *sd_ptr = mmap(NULL, sizeof(SyncData), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*sd_ptr == MAP_FAILED) { 
        perror("mmap game_sync"); 
        close(fd);
        return -1; 
    }
    
    close(fd);
    return 0;
}

int shm_close_game_state(GameState *gs, int width, int height) {
    size_t total = gs_size(width, height);
    if (munmap(gs, total) == -1) {
        perror("munmap game_state");
        return -1;
    }
    return 0;
}

int shm_close_sync_data(SyncData *sd) {
    if (munmap(sd, sizeof(SyncData)) == -1) {
        perror("munmap game_sync");
        return -1;
    }
    return 0;
}