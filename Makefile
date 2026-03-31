CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -g
LIBS   = -lrt -pthread

all: vista jugador

master: src/master.c src/shared.c src/shared.h
	$(CC) $(CFLAGS) -o master src/master.c src/shared.c $(LIBS)

vista: src/vista.c src/shared.h
	$(CC) $(CFLAGS) -o vista src/vista.c $(LIBS)

jugador: src/jugador.c src/shared.h
	$(CC) $(CFLAGS) -o jugador src/jugador.c $(LIBS)

run: all
	./ChompChamps -v ./vista -p ./jugador

clean:
	rm -f master vista jugador