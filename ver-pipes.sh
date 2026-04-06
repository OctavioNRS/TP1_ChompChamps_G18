#!/bin/bash

echo "=== Ejecutando master con 2 jugadores ==="
./master -w 10 -h 10 -d 500 -t 30 -p ./jugador ./jugador &
MASTER_PID=$!

echo "Master PID: $MASTER_PID"
echo ""
sleep 2

echo "=== PIPES DEL MASTER ==="
lsof -n -p $MASTER_PID | head -1  # header
lsof -n -p $MASTER_PID | grep pipe
echo ""

echo "=== TOTAL DE PIPES ABIERTOS ==="
lsof -n -p $MASTER_PID | grep -c pipe
echo ""

echo "=== PROCESOS HIJOS (jugadores) ==="
ps --ppid $MASTER_PID
echo ""

echo "=== PIPES DE LOS HIJOS ==="
for child_pid in $(ps --ppid $MASTER_PID -o pid --no-headers); do
    echo "Player PID $child_pid:"
    lsof -n -p $child_pid | grep pipe || echo "  (sin pipes visibles)"
done
echo ""

echo "Esperando a que termine el juego..."
wait $MASTER_PID

echo ""
echo "=== VERIFICANDO LIMPIEZA ==="
echo "Pipes residuales del master:"
lsof -n -p $MASTER_PID 2>/dev/null | grep pipe || echo "  ✓ Ninguno (proceso terminado)"