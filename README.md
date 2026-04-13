# ChompChamps - Juego de Tablero Distribuido

Un simulador multijugador de juego de tablero implementado en C con sincronización por memoria compartida (shared memory) y semáforos. Múltiples procesos jugadores interactúan concurrentemente en un tablero administrado centralmente.

---

## Decisiones de Diseño

### 1. **Arquitectura Modular**
El proyecto está estructurado en módulos independientes que separan responsabilidades:

- **`master.c`**: Orquestación principal, parseo de argumentos, game loop principal
- **`jugador.c`**: Implementación del jugador BalancedPlayer (análisis local de seguridad + puntos)
- **`vista.c`**: Visualización ASCII del estado del juego en tiempo real
- **`lib/process_manager.c`**: Creación, gestión y limpieza de procesos hijo (fork/exec, cleanup)
- **`lib/game_logic.c`**: Lógica del juego (validación de movimientos, tablero, fin del juego)
- **`lib/shared.c`**: Sincronización readers-writers, funciones de utilidad compartida


### 2. **Uso de #DEFINE para Constantes**

```c
#define NUM_DIRECTIONS 8        // 8 direcciones (N, NE, E, SE, S, SO, O, NO)
#define MAX_BOARD_VALUE 9       // Valores recompensa 1-9
#define MIN_BOARD_SIZE 10       // Tablero mínimo 10x10
#define SHM_PERMISSIONS 0666    // Permisos archivos compartidos
#define MAX_PLAYERS 9           // Máximo 9 jugadores simultáneos
```
---

### 3. **Round robin con last_player para tener juego justo**
Se implemento en el loop principal del juego un Round robin que guarda cual es el ultimo jugador al cual el master le leyo un movimiento. Esta decision se tomo para que los jugadores que son forkeados inicialmente no tengan ventaja por sobre los otros. La revision del select se hace desde el ultimo jugador que se leyo + 1.

### 4. **Jugador.c que priorice la supervivencia**
Se tomo la decision de implementar un jugador que priorice moverse a donde tiene mas bloques libres al rededor en vez de priorizar la cantidad de puntos obtenidos en principio. Se hicieron comparaciones entre distintas estrategias de juego y se llego a dos estrategias principales para comparar que fueron GreedyPlayer y BalancedPlayer. La estructura de ambos fue similar pero en los pesos de uno valia mas la libertad y en otro los puntos inmediatos. La decision tomada fue optar por el BalancedPlayer como mejor estrategia.

### 5. **Implementacion de no_writer para evitar inanicion del master**
Se tomo la decision de implementar un semaforo que asegura que siempre que el master quiera escribir algo en la memoria compartida que tenga prioridad por sobre los lectores. El master cierra este semaforo de no_writer y espera a que todos los lectores dejen de leer para poder escribir. Todos los lectores pueden entrar a leer unicamente cuando este semaforo no esta encendido.

## Instrucciones de Compilación y Ejecución

### Compilación Local

```bash
# Limpieza y compilación completa
make clean
make

# Esto genera 3 binarios:
# - master    (orquestador del juego)
# - vista     (visualización ASCII)
# - jugador   (jugador BalancedPlayer - análisis local de seguridad)
```

### Compilación en Docker desde Script
```bash
chmod 777 run.sh
./run.sh [N_JUGADORES]

# Ejemplos:
./run.sh           # 1 jugador (default)
./run.sh 2         # 2 jugadores
./run.sh 3         # 3 jugadores
```

El script `run.sh` automáticamente:
- Monta el directorio actual en `/SO/TPE_ChompChamps`
- Compila el código
- Ejecuta master con N jugadores aleatorios
- Observacion: En caso de realizar Ctrl+C en el medio de este script, la terminal se queda en blanco y no vuelve a andar hasta que se reinicie el editor. Esto no constituye ningun problema, pues cuando se ejecuta de manera directa el programa, en el momento que se realiza Ctrl+C, se cierra de manera correcta.

### Ejecución Directa en Docker

```bash
#Se ingresa al docker
docker run -v "${PWD}:/SO/TPE_ChompChamps" --privileged -ti agodio/itba-so-multiarch:3.1

#Se mueve hacia la carpeta donde esta ubicado el proyecto
cd /SO/TPE_ChompChamps

#Se compila el proyecto
make

# Forma más simple (vista + jugador)
./master -v ./vista -p ./jugador

# Con parámetros completos
./master -w 15 -h 15 -d 100 -t 5 -s 42 -v ./vista -p ./jugador
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


## Rutas Relativas para Ejecución en Torneo

### Player Disponible

```bash
./jugador
```

### Vista Disponible

```bash
./vista      
```
---

## Limitaciones Conocidas

1. **Máximo de Jugadores**: 9 (`MAX_PLAYERS`)
   - Limitación arquitectónica: array fijo de semáforos player_ack[MAX_PLAYERS]
   - Ampliable modificando `#define MAX_PLAYERS` en shared.h

2. **Tamaño Mínimo de Tablero**: 10x10
   - Auto-ajuste: si se pasan valores menores, se fuerzan a 10

3. **Vista en Terminal**
   - La vista fue hecha para verse desde la terminal es decir se modifica segun el tamaño de la terminal.
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
Loop `find_player_id()` podría repetirse en múltiples files de jugador.

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

**Impacto**: Una única fuente de verdad, reducción de código duplicado.

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

### 5. **Acceso No Protegido a `game_over` (PVS-Studio V547)**

**Problema**:
Players accedían a `gs->game_over` sin protección de semáforo.

**Solución**:
Proteger lectura con semáforos readers-writers:
```c
// jugador.c
while (1) {
    sem_wait(&sd->player_ack[my_id]);
    
    reader_enter(sd);
    int over = gs->game_over;
    reader_leave(sd);
    
    if (over) break;
}
```

**Impacto**: Patrón explícito de sincronización, eliminación de data race potencial.

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
- **Debugger**: GDB con flags (`-g`) - Análisis de comportamiento de procesos
- **Control de versiones**: Git - Modularización incremental por PR
- **Utilities**: strace, valgrind (memory leaks), ps/top (monitoreo procesos)



---

---