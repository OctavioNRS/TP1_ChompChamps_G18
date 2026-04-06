// #ifndef PLAYER_ADT_H
// #define PLAYER_ADT_H

// #include <fcntl.h>  
// #include <string.h>
// #include <time.h>
// #include "../shared.h"

// typedef struct PlayerCDT* PlayerADT;
// /*
//  * Inicializa el jugador
//  */
// PlayerADT init_player(int argc, char **argv);

// int init_shm(PlayerADT p);

// /*
//  * Guarda una porcion del estado actual para
//  * no bloquear la memoria compartida innecesariamente
//  */
// void get_state_snapshot(PlayerADT p);

// bool still_playing(PlayerADT p);


// // Funciones getters

// unsigned int get_x(PlayerADT p);
// unsigned int get_y(PlayerADT p);
// unsigned int get_width(PlayerADT p);
// unsigned int get_height(PlayerADT p);
// unsigned int get_player_count(PlayerADT p);
// int get_id(PlayerADT p);
// GameState* get_game_state(PlayerADT p);
// SyncData* get_game_sync(PlayerADT p);

// /*
//  * Enviar movimiento al master
//  */
// int send_movement(PlayerADT p, unsigned char move);



// #endif // PLAYER_ADT_H
