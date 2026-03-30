#ifndef PLAYER_ADT_H
#define PLAYER_ADT_H

#include <fcntl.h>  
#include <string.h>
#include <time.h>
#include "../shared.h"

typedef struct PlayerCDT* PlayerADT;
/*
 * Inicializa el jugador
 */
PlayerADT init_player(int argc, char **argv);

int init_shm(PlayerADT p);

/*
 * Guarda una porcion del estado actual para
 * no bloquear la memoria compartida innecesariamente
 */
void get_state_snapshot(PlayerADT p);

bool still_playing(PlayerADT p);


// Funciones getters

unsigned int get_x(PlayerADT p);
unsigned int get_y(PlayerADT p);
unsigned int get_width(PlayerADT p);
unsigned int get_height(PlayerADT p);
unsigned int get_player_count(PlayerADT p);
int get_id(PlayerADT p);
GameState* get_game_state(PlayerADT p);
SyncData* get_game_sync(PlayerADT p);

/*
 * Enviar movimiento al master
 */
int send_movement(PlayerADT p, unsigned char move);

//Funciones usadas por los players para calcular sus movimientos:

/*
 * Devuelve el valor de la celda en (x, y), o -1 si es inválida
 */
int get_board_cell(PlayerADT p, int x, int y);

/*
 * Verifica si la celda en (x, y) está libre (valor entre 1 y 9)
 */
bool is_cell_free(PlayerADT p, int x, int y);

/*
 * Convierte una dirección a desplazamientos dx, dy
 */
void direction_to_offset(unsigned char dir, int *dx, int *dy);

/*
 * Cuenta la cantidad de celdas libres alrededor de (x, y)
 */
int count_free_neighbors(PlayerADT p, int x, int y);

/*
 * Calcula la profundidad máxima alcanzable desde (x, y) en la dirección (dx, dy)
 */
int calculate_depth(PlayerADT p, int dx, int dy, int max_depth);

/*
 * Verifica si hay jugadores cercanos a (x, y) excluyendo al jugador con my_id
 */
bool has_nearby_players(PlayerADT p, int x, int y, int my_id);

/*
 * Determina si la celda en (x, y) es una trampa potencial
 */
bool is_potential_trap(PlayerADT p, int x, int y);

/*
 * Determina si el juego está en estado de endgame
 */
bool is_endgame(PlayerADT p);

#endif // PLAYER_ADT_H
