#!/bin/bash
PLAYERS=${1:-1}  # default 1 jugador si no se pasa argumento

PLAYER_ARGS=""
for i in $(seq 1 $PLAYERS); do
    PLAYER_ARGS="$PLAYER_ARGS ./jugador"
done

docker run -v "${PWD}:/SO/TPE_ChompChamps" --privileged -ti agodio/itba-so-multiarch:3.1 /bin/bash -c "cd /SO/TPE_ChompChamps && make clean && make all && ./master -v ./vista -p $PLAYER_ARGS"