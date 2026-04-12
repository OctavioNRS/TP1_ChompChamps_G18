CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g -Isrc
LIBS   = -lrt -pthread

all: master vista jugador

master: src/master.c src/lib/shared.c src/lib/game_logic.c src/lib/process_manager.c src/include/shared.h
	$(CC) $(CFLAGS) -o master src/master.c src/lib/shared.c src/lib/game_logic.c src/lib/process_manager.c $(LIBS)

vista: src/vista.c src/include/shared.h
	$(CC) $(CFLAGS) -o vista src/vista.c src/lib/shared.c $(LIBS)

jugador: src/jugador.c src/include/shared.h
	$(CC) $(CFLAGS) -o jugador src/jugador.c src/lib/shared.c $(LIBS)


run: all
	./master -v ./vista -p ./jugador

clean:
	rm -f master vista jugador 