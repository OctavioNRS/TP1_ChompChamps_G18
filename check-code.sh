#!/bin/bash

echo "=== Analizando código con PVS-Studio ==="

# Limpiar
make clean
rm -f PVS-Studio.log report.txt report.html

# Analizar
pvs-studio-analyzer trace -- make all
pvs-studio-analyzer analyze -o PVS-Studio.log -j4

# Convertir a formatos legibles
echo ""
echo "=== Generando reportes legibles ==="

plog-converter -t tasklist PVS-Studio.log -o report.txt
plog-converter -t html PVS-Studio.log -o report.html

# Mostrar solo problemas importantes en consola
echo ""
echo "=== ERRORES DE ALTA PRIORIDAD ==="
grep "High" report.txt

echo ""
echo "=== ERRORES DE MEDIA PRIORIDAD ==="
grep "Medium" report.txt | head -10

echo ""
echo "Reportes generados:"
echo "- Texto: report.txt"
echo "- HTML: report.html"
echo ""
echo "Abrir en navegador:"
echo "xdg-open report.html  # Linux"
echo "open report.html      # Mac"