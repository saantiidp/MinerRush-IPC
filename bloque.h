#ifndef BLOQUE_H
#define BLOQUE_H
#include "boolean.h"
#include <sys/types.h>

#define MAX_MINEROS 100
typedef struct {
    pid_t pid; //PID minero
    int monedas;//monedas obtenidas hasta el momento
}Cartera;


typedef struct {
    int id; //identificador bloque
    long int target; //resultado del que se desea hallar el operando
    long int solucion; //operando que permite hallar el target
    
    pid_t minero_ganador; //PID del minero ganador
    Cartera carteras[MAX_MINEROS]; //carteras mineros actuales 

    
    Boolean votos[MAX_MINEROS];
    
    int num_votos; //número de votos totales que se han usado para evaluar este bloque
    int num_votos_positivos; //número de votos + obtenidos 
      
    Boolean bloque_especial; //valor para indicar que se trata de un bloque especial         
}bloque;

#endif