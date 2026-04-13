// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
// game_logic.h
#ifndef GAME_LOGIC_H
#define GAME_LOGIC_H

#include "process_manager.h"

// Deltas para las 8 direcciones: 0=arriba, sentido horario
// dir:  0   1   2   3   4   5   6   7
extern const int dx[];
extern const int dy[];

// Inicializa el tablero con valores aleatorios según seed
void init_board(GameState *gs, masterADT master);

// Coloca a los jugadores en posiciones aleatorias evitando colisiones
void place_players(GameState *gs, masterADT master);

// Determina y imprime al ganador
void print_winner(GameState *gs);

// Imprime los resultados finales de todas las partidas
void print_final_results(GameState *gs);

// Verifica si todos los jugadores están bloqueados (sin movimientos posibles)
int no_player_can_move(masterADT m);

// Lee el movimiento del jugador i, lo valida y lo aplica
// Devuelve 0 si el movimiento fue válido, -1 si no
int check_player(masterADT m, int i);

// Finaliza el juego: marca game_over, despierta jugadores, notifica vista
void end_game(masterADT m);

// Imprime la configuración inicial del juego
void print_config(masterADT m);

#endif
