#!/bin/bash

echo "=== Analizando código con PVS-Studio ==="

# Limpiar
make clean
rm -f PVS-Studio.log

# Analizar
pvs-studio-analyzer trace -- make all
pvs-studio-analyzer analyze -o PVS-Studio.log -j4

# Mostrar solo problemas importantes en consola
echo ""
echo "=== ERRORES DE ALTA PRIORIDAD ==="
plog-converter -t tasklist PVS-Studio.log | grep "High"

echo ""
echo "=== ERRORES DE MEDIA PRIORIDAD ==="
plog-converter -t tasklist PVS-Studio.log | grep "Medium" | head -10

echo ""
echo "Reporte completo guardado en PVS-Studio.log"
echo "Para ver todo: plog-converter -t tasklist PVS-Studio.log"