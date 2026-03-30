#include "include/playerADT.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

struct PlayerCDT {
	GameState* game_state;
	SyncData* game_sync;
	unsigned short x, y, width, height;
	int id;						// Game state values
	char blocked;
	char game_finished;
	int board[];
};

PlayerADT init_player(int argc, char **argv) {

    if (argc < 3) {
        fprintf(stderr, "Usage: ./player [width] [height]\n");
        return NULL;
    }

    unsigned int width = atoi(argv[1]);
    unsigned int height = atoi(argv[2]);

	PlayerADT p = malloc(sizeof(struct PlayerCDT) + sizeof(int) * width * height);
    if (p == NULL) {
        fprintf(stderr, "Error: no se pudo asignar memoria para PlayerADT\n");
        return NULL;
    }
	p->width = width;
	p->height = height;
	p->blocked = 0;
	p->id = -1;

	return p;
}

int init_shm(PlayerADT p) {

    // Abrir game_state (read only)
    size_t state_size = sizeof(GameState) + (size_t)(p->width * p->height) * sizeof(char);
    int fd1 = shm_open(SHM_STATE, O_RDONLY, 0666);
    if (fd1 == -1) {
        perror("PLAYER::INIT_SHM: Error opening game_state shmem");
        return -1;
    }
    p->game_state = mmap(NULL, state_size, PROT_READ, MAP_SHARED, fd1, 0);
    if (p->game_state == MAP_FAILED) {
        perror("PLAYER::INIT_SHM: Error mmapping game_state");
        close(fd1);
        return -1;
    }
    close(fd1);

    // Abrir game_sync (read/write para semaforos)
    int fd2 = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (fd2 == -1) {
        perror("PLAYER::INIT_SHM: Error opening game_sync shmem");
        return -1;
    }
    p->game_sync = mmap(NULL, sizeof(SyncData), PROT_READ | PROT_WRITE, MAP_SHARED, fd2, 0);
    if (p->game_sync == MAP_FAILED) {
        perror("PLAYER::INIT_SHM: Error mmapping game_sync");
        close(fd2);
        return -1;
    }
    close(fd2);

    // Obtener id del jugador buscando su PID en la lista de players
    reader_enter(p->game_sync);
    if (p->id == -1) {
        pid_t my_pid = getpid();
        for (unsigned int i = 0; p->id < 0 && i < p->game_state->n_players; i++) {
            if (p->game_state->players[i].pid == my_pid) {
                p->id = i;
            }
        }
    }
    reader_leave(p->game_sync);

    return 0;
}

void get_state_snapshot(PlayerADT p) {

	reader_enter(p->game_sync);
	
	p->x = p->game_state->players[p->id].x;
	p->y = p->game_state->players[p->id].y;
	p->blocked = p->game_state->players[p->id].blocked;
	p->game_finished = p->game_state->game_over;
	
	// Copiar tablero (Tp1 usa char[], lo convertimos a int[] en el snapshot)
	for (int i = 0; i < p->height; i++) {
		for (int j = 0; j < p->width; j++) {
			p->board[i*p->width + j] = (int)p->game_state->board[i*p->width + j];
		}
	}

	reader_leave(p->game_sync);
}

bool still_playing(PlayerADT p) {
	return !p->game_finished && !p->blocked;
}

unsigned int get_x(PlayerADT p) {
    return p->x;
}

unsigned int get_y(PlayerADT p) {
    return p->y;
}

unsigned int get_width(PlayerADT p) {
    return p->width;
}

unsigned int get_height(PlayerADT p) {
    return p->height;
}

int get_id(PlayerADT p) {
    return p->id;
}

unsigned int get_player_count(PlayerADT p) {
    if (p == NULL || p->game_state == NULL)
        return 0;
    return p->game_state->n_players;
}

GameState* get_game_state(PlayerADT p) {
    return p->game_state;
}

SyncData* get_game_sync(PlayerADT p) {
    return p->game_sync;
}

int send_movement(PlayerADT p, unsigned char move) {

    // Esperar permiso para enviar movimiento
    sem_wait(&p->game_sync->player_ack[p->id]);

    if (write(STDOUT_FILENO, &move, 1) != 1) {
        perror("PLAYER::SEND_MOVEMENT: Write error");
		return -1;
    }

	return 0;
}

