# MinerRush-IPC (Sistemas Operativos)

Implementación en C de una red de “mineros” multiproceso inspirada en Blockchain. Varios nodos compiten por resolver una prueba de trabajo (PoW). En cada ronda, el primero en encontrar la solución propone un bloque y el resto vota su validez. Opcionalmente, un **monitor/comprobador** unifica la salida mostrando los bloques minados en tiempo real y soporta **recuperación de bloques históricos** desde ficheros de registro.

El proyecto utiliza **procesos, hilos (pthreads), memoria compartida, semáforos, señales, colas de mensajes y tuberías**.

---

## Estructura del repositorio

```
.
│   bloque.h
│   boolean.h
│   cola.h
│   comprobador
│   comprobador.c
│   hilo.h
│   makefile
│   miner.c
│   pow.c
│   pow.h
│   sistema.h
│   Memoria .pdf
│
└───.vscode
        launch.json
        tasks.json
```

- `miner.c`: proceso principal del minero (gestión de rondas, hilos de PoW, votaciones, señales).
- `comprobador.c`: proceso que recibe bloques por cola de mensajes, valida y produce salida para el monitor.
- `pow.c/.h`: implementación de la prueba de trabajo.
- `bloque.h`, `sistema.h`, `cola.h`, `hilo.h`, `boolean.h`: estructuras y utilidades compartidas.
- `makefile`: reglas de compilación.
- `Memoria .pdf`: memoria técnica del proyecto.

---

## Objetivo y comportamiento del sistema

La red está compuesta por un conjunto de **mineros** que intentan resolver concurrentemente una PoW (búsqueda por fuerza bruta de un operando `s` tal que `f(s)=t`). El primero en encontrar la solución **propone un bloque** y solicita al resto de mineros activos una **votación**; si se aprueba por mayoría, recibe una moneda.

La red es **dinámica**:
- Se pueden lanzar mineros desde una o varias shells.
- Mineros pueden unirse o salir en cualquier momento.
- El comprobador/monitor puede arrancar/parar en cualquier momento sin bloquear el sistema.

---

## Arquitectura: procesos y responsabilidades

### Minero (`miner`)
Cada minero se compone de **dos procesos**:

- **Proceso padre: Minero**
  - Conecta (o crea) el segmento de memoria compartida del sistema.
  - Participa en rondas: mina con **N hilos**, compite por proclamarse ganador, coordina votaciones si gana.
  - Señaliza al resto de mineros el inicio de ronda / inicio de votación.
  - Envía el bloque al comprobador por **cola de mensajes** cuando hay monitor activo.

- **Proceso hijo: Registrador**
  - Recibe bloques desde el proceso Minero por **tubería** y los escribe a un fichero de log asociado al PID del minero.
  - Ante una solicitud de recuperación, lee su log y reenvía bloques por la cola de mensajes.

### Comprobador / Monitor (`comprobador`)
- **Comprobador (productor)**:
  - Recibe bloques desde los ganadores vía **cola de mensajes**.
  - Verifica de nuevo la solución (independientemente del voto de mineros).
  - Escribe los bloques en una memoria compartida de monitoreo con un **buffer circular** y semáforos tipo productor-consumidor.

- **Monitor (consumidor)**:
  - Consume bloques del buffer circular y los imprime por pantalla con el formato del Registrador.

---

## Recursos IPC y sincronización

1) **Memoria compartida del sistema (SHM global)**  
   Contiene el estado compartido: lista de PIDs, monedas, votos, bloque actual/último, y sincronización (mutex/semáforos) para proteger estructuras y elegir ganador.

2) **Señales**  
   - `SIGUSR1`: inicio de nueva ronda.  
   - `SIGUSR2`: inicio de votación (y corte del minado).  
   - `SIGINT`: finalización ordenada.  
   - `SIGHUP` (diseño): solicitud de recuperación de histórico.

3) **Tubería Minero↔Registrador**  
   El minero escribe estructuras de bloque y el registrador las persiste/imprime.

4) **Cola de mensajes POSIX**  
   Comunicación Mineros↔Comprobador (bloques ganadores y recuperación).

5) **SHM de monitoreo**  
   Buffer circular con semáforos `mutex/empty/fill` para productor-consumidor.

---

## Ciclo de una ronda de minado

1. **Inicio de ronda**: un minero fija el target inicial y emite `SIGUSR1`. Los que entran tarde esperan a la siguiente ronda.  
2. **Minado (PoW)**: se divide el rango `[0, POW_LIMIT)` entre `N_THREADS`. Cada hilo explora su subrango.  
3. **Proclamación de ganador**: el primero que encuentra solución intenta adquirir el semáforo de “ganador”. Solo uno gana.  
4. **Votación**: el ganador publica el bloque y envía `SIGUSR2`. Los demás validan y votan.  
5. **Notificación al comprobador**: si hay monitor, el ganador envía el bloque por cola de mensajes.  
6. **Registro**: todos los mineros envían el bloque a su registrador por tubería.  
7. **Siguiente ronda**: el target pasa a ser la solución actual y se emite `SIGUSR1`.

---

## Formato del bloque

Ejemplo (validado):
```
Id : 0019
Winner : 6837
Target : 27818400
Solution : 53980520 ( validated )
Votes : 6/6
Wallets : 6836:05 6837:04 6838:06 ...
```

Ejemplo (rechazado):
```
Id : 0019
Winner : 7072
Target : 27818400
Solution : 00000011 ( rejected )
Votes : 1/6
Wallets : 7040:06 7041:04 ...
```

---

## Recuperación de bloques históricos

1. Al arrancar, el **Comprobador** solicita recuperación (p.ej. `SIGHUP`).  
2. Cada **Minero** reenvía la solicitud a su **Registrador**.  
3. Cada **Registrador** lee su log y reenvía bloques por la cola de mensajes con límite temporal.  
4. El **Comprobador** elimina duplicados (por ID), ordena y publica al **Monitor** mediante el buffer circular.  

Durante la recuperación, el sistema puede seguir minando (con posible retraso de visualización).

---

## Compilación

Requiere Linux con soporte POSIX (mqueue, shm, semáforos, pthreads).

```bash
make
```

Genera ejecutables:
- `miner`
- `comprobador`

---

## Ejecución

Ejemplo con dos mineros y un comprobador:
```bash
./miner 3 1 & ./miner 4 1 & ./comprobador
```

Arrancar el comprobador más tarde:
```bash
./miner 6 1 &
# ... dejar minar ...
./comprobador
```

---

## Limpieza de recursos

Si el último minero termina, debe liberar SHM, semáforos y colas. En caso de salida anómala, puede ser necesario limpiar manualmente:
- `ipcs -m` / `ipcrm -m <id>`
- borrar semáforos con nombre en `/dev/shm/sem.*`
- colas de mensajes con nombre (según tu implementación)

---

## Limitaciones y problemas conocidos

- **Sincronización al unir mineros**: con muchos mineros, alguno puede perder una ronda y entrar en la siguiente.  
- **Inicialización “listo/no listo”**: una bandera booleana simple es frágil si no está protegida por sincronización robusta.  
- **Corte de hilos en `SIGUSR2`**: hay que asegurar que todos los hilos abandonan minado inmediatamente.  
- **Gestión de procesos hijos**: conviene comprobar siempre el `exit status` con `waitpid`.  
- **Eficiencia de tubería**: enviar/recibir la estructura completa del bloque es más eficiente que campo a campo.  
- **Valgrind**: posible warning de bytes no inicializados relacionado con creación de semáforos.

---

## Ideas de mejora

- Reemplazar la bandera de init por una **barrera** o semáforo de inicialización.  
- Cancelación de hilos más robusta (flag atómica + comprobaciones, o cancelación controlada).  
- Handshake explícito para detectar si el comprobador/monitor está activo.  
- Refactor de IO por tubería a `write/read(sizeof(Bloque))`.  
- Pruebas de estrés automatizadas (muchos mineros, reinicios del monitor, rondas largas).

---

## Autor

Santiago de Prada Lorenzo

