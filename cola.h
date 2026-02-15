#include <mqueue.h>
#include <semaphore.h>
#include "bloque.h"


#define MQ_NAME "/mq_coladebloques"
#define SEM_COLA "/sema_cola"
#define SEM_RECUP_BLOQUES "/sema_recup_bloques"

#define MEM_COMP_COMPROBADOR "/memoria_compartida_comprobador"

#define MAXIMO_BLOQUES_RECUPERABLES 1000

#define COLA_CIRCULAR_BLOQUES_SIZE 5

#define TEMPORIZADOR 20


typedef struct {    
    bloque block;
    Boolean correcta; //valor para comprobar si la solucion es correcta
} bloque_comprobador_monitor;



typedef struct {
    bloque_comprobador_monitor coladebloques[COLA_CIRCULAR_BLOQUES_SIZE];
    sem_t mutex; //Semáforo tipo mutex
    sem_t sem_empty; //semáforo de control de la memoria compartida
    sem_t sem_fill; //semáforo de control de la memoria compartida
} Segmento_monitoreo;




struct mq_attr attributes = {
 .mq_flags = 0,
 .mq_maxmsg = 1, //se envía 1 elemento solo
 .mq_curmsgs = 0,
 .mq_msgsize = sizeof(bloque)//bloque del tamaño mensaje
};