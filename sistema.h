
#ifndef SISTEMA_H
#define SISTEMA_H
#include <semaphore.h>
#include "bloque.h"

#define SHM_NAME "/memoria_compartida" //Nombre memoria compartida
#define SEM_MINER_WINNER "/miner_winner_sem"

#define MAX_MINEROS 100
#define MAX_ROUNDS 100
#define NO_PID -1




typedef struct {
    pid_t pid;
    Boolean activo;
    int monedas;
    Boolean votos [MAX_ROUNDS];
    int numero_votos;
} minero_t;


typedef struct {
    minero_t mineros[MAX_MINEROS]; //listado con los PID de cada minero activo
    int numero_mineros;

    int numero_mineros_activos;
    
    bloque ultimo_bloque; //último bloque resuelto
    bloque bloque_actual; //bloque actual que se está resolviendo

    sem_t mutex; //sémaforo para exclusión mutua al acceder a los datos del sistema
    sem_t semaforoMinero_ganador;
    Boolean sems_creados ; //inicializado a 0=FALSE por defecto


}info_sistema;
#endif