CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LIBS   = -lrt -pthread

all: master vista jugador greedy survivor

all_ncurses: master cursedVista jugador greedy

master: src/master.c src/shared.c src/include/shared.h
	$(CC) $(CFLAGS) -o master src/master.c src/shared.c $(LIBS)

vista: src/vista.c src/include/shared.h
	$(CC) $(CFLAGS) -o vista src/vista.c src/shared.c $(LIBS)

cursedVista: src/cursedVista.c src/shared.h
	$(CC) $(CFLAGS) -o cursedVista src/cursedVista.c src/shared.c $(LIBS) -lncurses

jugador: src/jugador.c src/include/shared.h
	$(CC) $(CFLAGS) -o jugador src/jugador.c src/shared.c $(LIBS)

greedy: src/players/GreedyPlayer.c src/include/shared.h
	$(CC) $(CFLAGS) -o greedy src/players/GreedyPlayer.c src/shared.c $(LIBS)

survivor: src/players/SurvivorPlayer.c src/include/shared.h
	$(CC) $(CFLAGS) -o survivor src/players/SurvivorPlayer.c src/shared.c $(LIBS)

run: all
	./master -v ./vista -p ./jugador ./greedy

clean:
	rm -f master vista jugador greedy survivor

clean_ncurses:
	rm -f master cursedVista jugador greedy