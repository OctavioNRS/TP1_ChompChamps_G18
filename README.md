# ChompChamps - Juego de Tablero Distribuido

Un simulador multijugador de juego de tablero implementado en C con sincronización por memoria compartida (shared memory) y semáforos. Múltiples procesos jugadores interactúan concurrentemente en un tablero administrado centralmente.

---

## Decisiones de Diseño

### 1. **Arquitectura Modular**
El proyecto fue refactorizado en módulos independientes para separar responsabilidades:

- **`master.c`**: Orquestación principal, parseo de argumentos, game loop principal
- **`lib/process_manager.c`**: Creación, gestión y limpieza de procesos hijo (fork/exec, cleanup)
- **`lib/game_logic.c`**: Lógica del juego (validación de movimientos, tablero, fin del juego)
- **`lib/shared.c`**: Sincronización readers-writers, funciones de utilidad compartida
- **`vista.c`**: Visualización ASCII del estado del juego en tiempo real
- **`players/`**: Implementaciones de estrategias (GreedyPlayer, SurvivorPlayer, jugador base)

### 2. **Patrón Readers-Writers con Prevención de Inanición del Escritor**

Implementa sincronización justa entre:
- **Escritor (Master)**: Actualiza GameState (posiciones, tablero, scores)
- **Lectores (Jugadores)**: Consultan estado para tomar decisiones

```c
// Implementación en shared.c
void reader_enter(SyncData *sd);   // Bloquear lectura
void reader_leave(SyncData *sd);   // Liberar lectura
void writer_enter(SyncData *sd);   // Bloquear escritura
void writer_leave(SyncData *sd);   // Liberar escritura
```

Semáforos utilizados:
- `state_mutex`: Protege el estado del juego
- `no_writer`: Previene inanición del escritor
- `readers_mutex`: Protege contador de lectores

### 3. **Comunicación por Memoria Compartida (Shared Memory)**

Dos regiones compartidas:
- **`SHM_STATE`**: Contiene `GameState` (tablero, posiciones x/y, scores, n_players)
- **`SHM_SYNC`**: Contiene `SyncData` (semáforos de coordinación)

Ventajas: **Cero copias**, comunicación directa entre procesos.

### 4. **Comunicación de Movimientos por Pipes**

- Master → Jugador: Envía byte (0-7) representando dirección
- Jugador → Master: Responde con movimiento calculado

Rationale: Separación clara de responsabilidades entre decisión (jugador) y validación (master).

### 5. **Uso de #DEFINE para Constantes**

```c
#define NUM_DIRECTIONS 8        // 8 direcciones (N, NE, E, SE, S, SO, O, NO)
#define MAX_BOARD_VALUE 9       // Valores recompensa 1-9
#define MIN_BOARD_SIZE 10       // Tablero mínimo 10x10
#define SHM_PERMISSIONS 0666    // Permisos archivos compartidos
#define MAX_PLAYERS 9           // Máximo 9 jugadores simultáneos
```

Mejora: Evita magic numbers, facilita cambios globales.

---

## Instrucciones de Compilación y Ejecución

### Compilación

```bash
# Limpieza y compilación completa
make clean
make

# Esto genera 5 binarios:
# - master    (orquestador del juego)
# - vista     (visualización estándar)
# - jugador   (jugador simple, movimientos aleatorios)
# - greedy    (estrategia greedy, maximiza puntos inmediatos)
# - survivor  (estrategia defensiva, maximiza libertad)
```

### Ejecución Básica

```bash
# Forma más simple (vista + 2 jugadores)
./master -v ./vista -p ./jugador ./greedy

# Con parámetros completos
./master -w 15 -h 15 -d 100 -t 5 -s 42 -v ./vista -p ./jugador ./greedy ./survivor

# Sin vista (solo resultados finales)
./master -p ./jugador ./greedy ./survivor

# Maximizar el juego
./master -w 20 -h 20 -d 50 -t 15 -v ./vista -p ./jugador ./greedy ./survivor
```

### Parámetros de Línea de Comandos

| Flag | Parámetro | Default | Descripción |
|------|-----------|---------|-------------|
| `-w` | WIDTH | 10 | Ancho del tablero (mín. 10) |
| `-h` | HEIGHT | 10 | Alto del tablero (mín. 10) |
| `-d` | DELAY_MS | 200 | Delay entre movimientos (ms) |
| `-t` | TIMEOUT_S | 10 | Timeout total del juego (segundos) |
| `-s` | SEED | time(NULL) | Semilla RNG (para reproducibilidad) |
| `-v` | VISTA_PATH | - | Ruta ejecutable de la vista |
| `-p` | PLAYER_PATHS | - | Rutas de ejecutables de jugadores (min. 1) |

---

## Rutas Relativas para Tournament

### Players Disponibles

```bash
./jugador           # Jugador básico - movimientos completamente aleatorios
./greedy            # Greedy - maximiza recompensa inmediata + libertad
./survivor          # Defensivo - maximiza espacios alcanzables (BFS)
```

### Vistas Disponibles

```bash
./vista             # Visualización ASCII estándar (RECOMENDADO)
./cursedVista       # Visualización ncurses (experimental, opcional)
```

### Comando Recomendado para Tournament

```bash
# Torneo estándar
./master -w 12 -h 12 -d 150 -t 8 -v ./vista -p ./jugador ./greedy ./survivor

# Este comando funcionará con cualquier combinación de los players disponibles.
```

---

## Limitaciones Conocidas

1. **Máximo de Jugadores**: 9 (`MAX_PLAYERS`)
   - Limitación arquitectónica: array fijo de semáforos player_ack[MAX_PLAYERS]
   - Ampliable modificando `#define MAX_PLAYERS` en shared.h

2. **Tamaño Mínimo de Tablero**: 10x10
   - Auto-ajuste: si se pasan valores menores, se fuerzan a 10
   
---

## Problemas Encontrados y Soluciones

### 1. **Procesos Zombie (WNOHANG)**

**Problema**: 
Cuando jugadores morían (pipe cerrado), sus procesos hijos persistían en estado zombie, contaminando la tabla de procesos.

**Solución**:
Implementar recolección no-bloqueante en `pipe_set_blocked()`:
```c
// process_manager.c
int status;
waitpid(m->game_state->players[i].pid, &status, WNOHANG);
// WNOHANG = no bloquear, recolectar si está listo
```

**Impacto**: Elimina entradas zombie, evita corrupción de estado.

---

### 2. **Inanición del Escritor (Writer Starvation)**

**Problema**:
Múltiples jugadores (lectores) podían acumular bloqueos, impidiendo que el master (escritor) actualizara el estado del juego indefinidamente.

**Solución**:
Implementar semáforo `no_writer` que indica si hay escritor esperando:

```c
// shared.c - reader_enter()
sem_wait(&sd->no_writer);          // Si hay escritor, no entro
sem_wait(&sd->readers_mutex);      // Proteger contador
sd->readers++;
if (sd->readers == 1) 
    sem_wait(&sd->state_mutex);    // Primer lector bloquea estado
sem_post(&sd->readers_mutex);
sem_post(&sd->no_writer);          // Liberar para próximos lectores
```

**Impacto**: Master siempre puede progresar el juego, sin deadlock.

---

### 3. **Duplicación de Código (DRY Violation)**

**Problema**:
Loop `find_player_id()` repetido idénticamente en 3 archivos:
- jugador.c (línea 54)
- GreedyPlayer.c (línea 90)
- SurvivorPlayer.c (línea 145)

**Solución**:
Extraer a función reutilizable en shared.c:
```c
// shared.c
int find_player_id(GameState *gs, pid_t my_pid) {
    for (int i = 0; i < (int)gs->n_players; i++) {
        if (gs->players[i].pid == my_pid) return i;
    }
    return -1;
}
```

**Impacto**: Una única fuente de verdad, reducción de 9 líneas duplicadas.

---

### 4. **Patrón "Game Over" Repetido**

**Problema**:
Código idéntico para marcar fin del juego aparecía 3 veces en `game_start()`:
```c
writer_enter(m->game_sync);
m->game_state->game_over = true;
writer_leave(m->game_sync);
for (int j = 0; j < m->n_players; j++)
    sem_post(&m->game_sync->player_ack[j]);
if (m->view_path) {
    sem_post(&m->game_sync->view_ready);
    sem_wait(&m->game_sync->view_done);
}
```

**Solución**:
Crear función `end_game()` en game_logic.c:
```c
// game_logic.c
void end_game(masterADT m) {
    writer_enter(m->game_sync);
    m->game_state->game_over = true;
    writer_leave(m->game_sync);
    
    for (int j = 0; j < m->n_players; j++)
        sem_post(&m->game_sync->player_ack[j]);
    
    if (m->view_path) {
        sem_post(&m->game_sync->view_ready);
        sem_wait(&m->game_sync->view_done);
    }
}
```

**Impacto**: Reducción de ~36 líneas duplicadas, punto único de cambio.

---

### 5. **Posible Overflow en Multiplicación (PVS-Studio V1028)**

**Problema**:
En `SurvivorPlayer.c:bfs_open_spaces()`, multiplicación en int antes del cast:
```c
char *visited = calloc((size_t)(w * h), sizeof(char));
// ↓ Si w=20000, h=20000: 20000*20000 = INT_MAX overflow
```

**Solución**:
Castear operandos ANTES de multiplicar:
```c
char *visited = calloc((size_t)w * (size_t)h, sizeof(char));
// size_t * size_t → nunca overflow para tamaños razonables (≤2GB)
```

**Impacto**: Eliminación de advertencia estática, código más robusto para tableros grandes.

---

### 6. **Acceso No Protegido a `game_over` (PVS-Studio V547)**

**Problema**:
Players accedían a `gs->game_over` sin protección de semáforo:
```c
while (!gs->game_over) {
    sem_wait(&sd->player_ack[my_id]);
    if (gs->game_over) break;  // ← Sin protección reader
}
```

**Solución**:
Proteger lectura con semáforos readers-writers:
```c
while (1) {
    sem_wait(&sd->player_ack[my_id]);
    
    reader_enter(sd);
    int over = gs->game_over;
    reader_leave(sd);
    
    if (over) break;
}
```

**Impacto**: Patrón explícito de sincronización, eliminación de data race potencial.

---

### 7. **Debugging con Herramientas Asistidas**

**Scripts de Debugging**:
Dos scripts creados con asistencia de IA para facilitar debugging y análisis:

- **`check-code.sh`**: Automatiza análisis estático con PVS-Studio
  - Ejecuta `pvs-studio-analyzer` para detectar posibles vulnerabilidades
  - Genera reportes en texto e HTML para revisión
  - Fue utilizado para identificar warnings V1028 (overflow) y V547 (data races)

- **`ver-pipes.sh`**: Monitorea pipes durante ejecución
  - Usa `lsof` para inspeccionar file descriptors abiertos del master
  - Rastrea procesos hijo y sus conexiones
  - Verifica limpieza de recursos al finalizar
  - Ayuda a confirmar que no quedan pipes residuales o zombies

**Colorización en vista.c**:
El array `player_colors[]` en vista.c fue desarrollado con asistencia de IA para visualizar jugadores diferenciados:

```c
// vista.c - Códigos ANSI para colorización
static const char *player_colors[] = {
    "\033[36m",  // cyan
    "\033[32m",  // verde
    "\033[31m",  // rojo
    "\033[35m",  // magenta
    "\033[34m",  // azul
    "\033[33m",  // amarillo
    "\033[37m",  // blanco
    "\033[92m",  // verde claro
    "\033[96m",  // cyan claro
};
```

Permite identificar visualmente a cada jugador de 0-8 en la visualización del tablero.

---

## Herramientas Utilizadas en Desarrollo

- **Análisis estático**: PVS-Studio (Static Code Analysis) - Detección de overflows, data races
- **Debugger**: GDB con flags `-g`) - Análisis de comportamiento de procesos
- **Control de versiones**: Git - Modularización incremental por PR
- **Utilities**: strace, valgrind (memory leaks), ps/top (monitoreo procesos)

---

## Notas de Implementación

### Compilación Alternativa (ncurses)
```bash
make all_ncurses         # Compila cursedVista
./master -v ./cursedVista -p ./jugador ./greedy  # Uso
make clean_ncurses       # Limpieza selectiva
```

### Buffer de Pruebas
Para reproducir comportamiento:
```bash
./master -s 12345 -w 10 -h 10 -d 100 -t 5 -p ./jugador ./greedy
# -s 12345: Semilla fija → tablero idéntico cada ejecución
```


## Archivos Principales

```
src/
├── include/
│   ├── shared.h           # Definiciones y structs compartidas
│   ├── game_logic.h       # API de lógica del juego
│   └── process_manager.h  # API de gestión de procesos
├── lib/
│   ├── shared.c           # Sincronización readers-writers
│   ├── game_logic.c       # Lógica de validación y movimientos
│   └── process_manager.c  # Crear/limpiar procesos
├── players/
│   ├── GreedyPlayer.c     # Estrategia greedy
│   └── SurvivorPlayer.c   # Estrategia defensiva
├── master.c               # Orquestador principal
├── vista.c                # Visualización ASCII
├── cursedVista.c          # Visualización ncurses (experimental)
└── jugador.c              # Jugador aleatorio
```

---