// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "shared.h"

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
    sem_post(&sd->no_writer);
}

void writer_leave(SyncData *sd) {
   sem_post(&sd->state_mutex);     // D: liberar mutex del estado
     // C: indicar que ya no hay escritor esperando o activo
}
