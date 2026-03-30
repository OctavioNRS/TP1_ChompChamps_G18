// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
     
#include "include/playerADT.h"
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv) {

    srand((unsigned int)time(NULL));

    PlayerADT p = init_player(argc, argv);

    if (p == NULL) {
        return -1;
    }
    if(init_shm(p) == -1){
        free(p);
        return -1;
    }

	while (1) {

        // Guardar estado actual
        get_state_snapshot(p);

        // Verificar si el juego terminó o si estamos bloqueados
        if (!still_playing(p)) {
            break;
        }
        
        // Elegir y enviar movimiento (aleatorio entre 0-7, las 8 direcciones)
        unsigned char move = rand() % 8;

        send_movement(p, move);

    }
    free(p);
    return 0;
}