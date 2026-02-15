#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <string.h>


#include "pow.h"
#include "sistema.h"
#include "cola.h"
int numero_mineros_activos_al_recuperar_bloques;

Boolean sigintinterrupt = FALSE;
Boolean temporizador = FALSE;
mqd_t queue;

sem_t *semaforo_cola;
sem_t *semaforo_recuperacion_bloques;
//sem_t *semaforo_recuperacion_bloques = NULL;
/*Segmento_monitoreo *memoria = NULL;*/

int pos_write = 0;

struct sigaction action_sigint;
struct sigaction action_alarm;

void Comprobador(Segmento_monitoreo *memoria);
void Monitor(Segmento_monitoreo *memoria);

info_sistema * Conexion_Memoria_Comprobador(int descriptor_memoria_compartida);

void Recuperacion_de_bloques(int descriptor_memoria_compartida, Segmento_monitoreo *memoria, info_sistema *informacion_sistema);
void Escribir_bloque_en_memoria_Compartida(bloque_comprobador_monitor verificacion_bloque, Segmento_monitoreo *memoria );
Boolean Verificar_bloque(bloque blocker);

void handler_sigint(int sig);
void handler_alarm(int sig);

//---------Manejadores de señal-----------------------------
void handler_sigint(int sig){
    sigintinterrupt = TRUE;
    return;
}
void handler_alarm(int sig){
    temporizador = TRUE;
}

//--------------------------------------------------------------------
info_sistema * Conexion_Memoria_Comprobador(int descriptor_memoria_compartida){
    info_sistema *informacion_sistema = NULL;  
    struct stat stat_sistema;   
    Boolean memoria_compartida_creada = FALSE;
    Boolean sems_memoria_compartida_creado = FALSE;
    do {
        //Comprobamos el tamaño del segmento de memoria compartida
        if (!memoria_compartida_creada){
            //Mapeamos la memoria compartida 
            if ((informacion_sistema = mmap(NULL, sizeof(info_sistema), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_memoria_compartida, 0)) == MAP_FAILED){
                perror ("mmap");
                sem_close(semaforo_cola);
                mq_close(queue);
                munmap(informacion_sistema, sizeof(info_sistema));
                exit(EXIT_FAILURE);
            }
        
            if (fstat(descriptor_memoria_compartida, &stat_sistema) == -1){
                perror("stat");
                sem_close(semaforo_cola);
                mq_close(queue);
                munmap(informacion_sistema, sizeof(info_sistema));
                exit(EXIT_FAILURE);
            }

            if (stat_sistema.st_size != sizeof(info_sistema)){
                memoria_compartida_creada = FALSE;
            } else {
                memoria_compartida_creada = TRUE;
            }
        }

        //Comprobamos que el semáforo esté creado
        if (memoria_compartida_creada){
            sems_memoria_compartida_creado = informacion_sistema->sems_creados;
        }

        usleep(1);
    
    } while(!memoria_compartida_creada || !sems_memoria_compartida_creado);

    //retornamos la variable mapeada información del sistema 
    return informacion_sistema;
}


Boolean Verificar_bloque(bloque blocker){
    
    if(blocker.target == pow_hash(blocker.solucion)){
        return TRUE;
    }
    
    return FALSE;

}


void Escribir_bloque_en_memoria_Compartida(bloque_comprobador_monitor verificacion_bloque, Segmento_monitoreo *memoria){
    sem_wait(&(memoria->sem_fill));
    sem_wait(&(memoria->mutex));
    if (memcpy(&(memoria->coladebloques[pos_write]), &verificacion_bloque, sizeof(bloque_comprobador_monitor)) == NULL){
        perror("memcpy");
        sem_destroy(&(memoria->mutex));
        sem_destroy(&(memoria->sem_empty));
        sem_destroy(&(memoria->sem_fill));
        munmap(memoria, sizeof(Segmento_monitoreo));
        shm_unlink(MEM_COMP_COMPROBADOR);
        sem_close(semaforo_cola);
        mq_close(queue);
        exit(EXIT_FAILURE);
    } 
           
    pos_write = (pos_write + 1)%5; //actualizamos el índice del buffer circular
    sem_post(&(memoria->mutex));
    sem_post(&(memoria->sem_empty));
}



void Recuperacion_de_bloques(int descriptor_memoria_sistema,Segmento_monitoreo *memoria, info_sistema *informacion_sistema){
    int i,k, bloques_recuperados = 0;
    bloque_comprobador_monitor array_bloques_verificados[MAXIMO_BLOQUES_RECUPERABLES];
    Boolean recibidos_bloques[MAXIMO_BLOQUES_RECUPERABLES];
    int mineros_que_han_terminado =0;
    sigset_t set;
    
    bloque blocker;
    bloque_comprobador_monitor verificacion_bloque;


    for(i=0; i < MAXIMO_BLOQUES_RECUPERABLES; i++){
        recibidos_bloques[i] = FALSE;
    }
    
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);   
     
    sem_wait(&(informacion_sistema->mutex));
    numero_mineros_activos_al_recuperar_bloques = informacion_sistema->numero_mineros_activos;
    for(i=0; k < informacion_sistema->numero_mineros_activos; i++){
        if(informacion_sistema->mineros[i].activo == TRUE && informacion_sistema->mineros[i].pid != getpid()){
            kill(informacion_sistema->mineros[i].pid, SIGHUP);
            k++;
        }
    }
    sem_post(&(informacion_sistema->mutex));
    munmap(informacion_sistema, sizeof(info_sistema)); 
    
    alarm(TEMPORIZADOR);
    while(temporizador==FALSE){
        sem_post(semaforo_recuperacion_bloques);
        if ((sigprocmask(SIG_BLOCK, &set, NULL)) < 0){
            perror("sigprocmask");
            exit(EXIT_FAILURE);
        }
        if(mq_receive(queue, (char*)&blocker, sizeof(bloque), NULL) == -1){
            
            if ((sigprocmask(SIG_UNBLOCK, &set, NULL)) < 0){
                perror("sigprocmask");
                exit(EXIT_FAILURE);
            }
            
            perror("mq_receive");
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            sem_close(semaforo_cola);
            sem_close(semaforo_recuperacion_bloques);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
        
        else{
            if ((sigprocmask(SIG_UNBLOCK, &set, NULL)) < 0){
                perror("sigprocmask");
                exit(EXIT_FAILURE);
            }
            //sem_wait(semaforo_recuperacion_bloques);
            if(blocker.bloque_especial == TRUE){
                    mineros_que_han_terminado++;
                    if(mineros_que_han_terminado == numero_mineros_activos_al_recuperar_bloques)break;
            }
            
            if(recibidos_bloques[blocker.id] == FALSE){
                verificacion_bloque.block = blocker;    
                verificacion_bloque.correcta = Verificar_bloque(blocker);
                    
                array_bloques_verificados[blocker.id] = verificacion_bloque;
                    
                recibidos_bloques[blocker.id] = TRUE;
                bloques_recuperados++;

            }
        }
    }
    for(i=0; i < bloques_recuperados; i++){
        for(k=0; k < MAXIMO_BLOQUES_RECUPERABLES; k++){
            if(recibidos_bloques[k] == TRUE){
                Escribir_bloque_en_memoria_Compartida(array_bloques_verificados[k], memoria);
                recibidos_bloques[k] = FALSE;
                break;

            } 
            
        }

    }

}




void Comprobador(Segmento_monitoreo *memoria){
    bloque blocker;
    bloque_comprobador_monitor verificacion_bloque;
    int status_monitor;
    int descriptor_memoria_sistema;
    sigset_t set;
     /*Cuando el principal reciba la señal SIGINT el manejador capturará esa señal */
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    
    action_alarm.sa_handler = handler_alarm;
    sigfillset(&(action_alarm.sa_mask));
    action_alarm.sa_flags = 0;
    
    
    if (sigaction(SIGALRM, &action_alarm, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    

    
    action_sigint.sa_handler = handler_sigint;
    sigfillset(&(action_sigint.sa_mask));
    action_sigint.sa_flags = 0;
    
    /*Cuando el principal reciba la señal SIGINT el manejador capturará esa señal */
    
    if (sigaction(SIGINT, &action_sigint, NULL) < 0){
        perror("sigaction");
        munmap(memoria, sizeof(Segmento_monitoreo));
        shm_unlink(MEM_COMP_COMPROBADOR);
        exit(EXIT_FAILURE);
    }

    //semáforo para acceder a la cola de mensajes
    if((semaforo_cola = sem_open(SEM_COLA , O_CREAT , S_IRUSR | S_IWUSR , 0)) == SEM_FAILED){
        perror("sem_open");
        munmap(memoria, sizeof(Segmento_monitoreo));
        shm_unlink(MEM_COMP_COMPROBADOR);
        exit (EXIT_FAILURE);
    }
    if((semaforo_recuperacion_bloques = sem_open(SEM_RECUP_BLOQUES , O_CREAT , S_IRUSR | S_IWUSR , 0)) == SEM_FAILED){
        perror("sem_open");
        munmap(memoria, sizeof(Segmento_monitoreo));
        shm_unlink(MEM_COMP_COMPROBADOR);
        exit (EXIT_FAILURE);
    }


    queue = mq_open(MQ_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attributes);
    if(queue == (mqd_t)-1){
        fprintf(stderr, "Error opening the queue\n");
        munmap(memoria, sizeof(Segmento_monitoreo));
        shm_unlink(MEM_COMP_COMPROBADOR);
        sem_close(semaforo_cola);
        exit(EXIT_FAILURE);
    }
    if((descriptor_memoria_sistema = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR)) == -1){
        if (errno == EEXIST){
            if((descriptor_memoria_sistema = shm_open(SHM_NAME,  O_RDWR, 0)) == -1){
                perror("shm_open");
                sem_destroy(&(memoria->mutex));
                sem_destroy(&(memoria->sem_empty));
                sem_destroy(&(memoria->sem_fill));
                munmap(memoria, sizeof(Segmento_monitoreo));
                shm_unlink(MEM_COMP_COMPROBADOR);
                sem_close(semaforo_cola);
                mq_close(queue);
                exit(EXIT_FAILURE);

            }
            Recuperacion_de_bloques(descriptor_memoria_sistema, memoria, Conexion_Memoria_Comprobador(descriptor_memoria_sistema));
        }

    }
    else{
        close(descriptor_memoria_sistema);
        shm_unlink(SHM_NAME);
    }
    if ((sigprocmask(SIG_BLOCK, &set, NULL)) < 0){
            perror("sigprocmask");
            exit(EXIT_FAILURE);
    }
    
    
    while(sigintinterrupt == FALSE){


        sem_post(semaforo_cola);
        if(mq_receive(queue, (char*)&blocker, sizeof(bloque), NULL) == -1){
            perror("mq_receive");
            sem_destroy(&(memoria->mutex));
            sem_destroy(&(memoria->sem_empty));
            sem_destroy(&(memoria->sem_fill));
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            sem_close(semaforo_cola);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
        else{
            //Comprobar si es correcto o no
            sem_wait(semaforo_cola);
            verificacion_bloque.block = blocker;
            
            if(blocker.bloque_especial == TRUE){
                Escribir_bloque_en_memoria_Compartida(verificacion_bloque, memoria);
                wait(&status_monitor);
                if(status_monitor == EXIT_FAILURE){
                    fprintf(stdout,"Error en el Monitor\n");
                }
                sem_destroy(&(memoria->mutex));
                sem_destroy(&(memoria->sem_empty));
                sem_destroy(&(memoria->sem_fill));
                munmap(memoria, sizeof(Segmento_monitoreo));
                shm_unlink(MEM_COMP_COMPROBADOR);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_RECUP_BLOQUES);
                sem_close(semaforo_cola);
                mq_close(queue);
                mq_unlink(SEM_COLA);
                exit(EXIT_SUCCESS);
            }
            
            verificacion_bloque.correcta = Verificar_bloque(blocker);
            //Escribirlo en la memoria compartida
            Escribir_bloque_en_memoria_Compartida(verificacion_bloque, memoria);
        }

    }

}



void Monitor(Segmento_monitoreo *memoria){
    int pos_read=0,i;
    //int valor_mutex_memoria;

        
    while(1){
        /*leemos de la memoria compartida*/
       
        sem_wait(&(memoria->sem_empty));
        sem_wait(&(memoria->mutex));
      
                //si es bloque especial salimos de bucle
        if(memoria->coladebloques[pos_read].block.bloque_especial == TRUE){
            //desvinculamos la memoria compartida
            munmap(memoria, sizeof(Segmento_monitoreo));
            exit(EXIT_SUCCESS);
        }
        fprintf(stdout, "Id :   %d\n", memoria->coladebloques[pos_read].block.id);
        fprintf(stdout, "Winner :   %d\n", memoria->coladebloques[pos_read].block.minero_ganador);
        fprintf(stdout, "Target :   %08ld\n", memoria->coladebloques[pos_read].block.target);

        if(memoria->coladebloques[pos_read].correcta == TRUE){
            fprintf(stdout, "Solution :   %08ld (validated)\n", memoria->coladebloques[pos_read].block.solucion);
        }
        else{
            fprintf(stdout, "Solution :   %08ld (rejected)\n", memoria->coladebloques[pos_read].block.solucion);
        }

        fprintf(stdout, "Votes :   %d/%d\n",memoria->coladebloques[pos_read].block.num_votos_positivos, memoria->coladebloques[pos_read].block.num_votos);

        fprintf(stdout, "Wallets :   ");
        for(i = 0; i < memoria->coladebloques[pos_read].block.num_votos + 1 ; i++){
            fprintf(stdout, "%d:%d  ",  memoria->coladebloques[pos_read].block.carteras[i].pid,memoria->coladebloques[pos_read].block.carteras[i].monedas);
        }
        
        fprintf(stdout, "\n\n");
        pos_read = (pos_read + 1)%5;
            
        sem_post(&(memoria->mutex));
        sem_post(&(memoria->sem_fill));
                
    }

}

int main(){
    int descriptor_memoria_compartida;
    Segmento_monitoreo *memoria = NULL;
    pid_t monitor; //proceso monitor del comprobador
    

    
    descriptor_memoria_compartida = shm_open(MEM_COMP_COMPROBADOR, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if(descriptor_memoria_compartida == -1){
            perror("shm_open");
            exit(EXIT_FAILURE);
    }
    else{
        if(ftruncate(descriptor_memoria_compartida, sizeof(Segmento_monitoreo)) == -1){
            perror("ftruncate");
            exit(EXIT_FAILURE);
        }
        
        if ((memoria = mmap(NULL, sizeof(Segmento_monitoreo), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_memoria_compartida, 0)) == MAP_FAILED){
            perror ("mmap");
            exit(EXIT_FAILURE);
        }
        close(descriptor_memoria_compartida);

        if (sem_init (&(memoria->mutex), 1, 1) == -1){
            perror("sem_init");
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            exit(EXIT_FAILURE);
        }
        if (sem_init (&(memoria->sem_empty), 1, 0) == -1){
            perror("sem_init");
            sem_destroy(&(memoria->mutex));
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            exit(EXIT_FAILURE);
        }

        if (sem_init (&(memoria->sem_fill), 1, 5) == -1){
            perror("sem_init");
            sem_destroy(&(memoria->mutex));
            sem_destroy(&(memoria->sem_empty));
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            exit(EXIT_FAILURE);
        }
        monitor = fork();
        
        if (monitor < 0){
            perror("fork");
            sem_destroy(&(memoria->mutex));
            sem_destroy(&(memoria->sem_empty));
            sem_destroy(&(memoria->sem_fill));
            munmap(memoria, sizeof(Segmento_monitoreo));
            shm_unlink(MEM_COMP_COMPROBADOR);
            exit(EXIT_FAILURE);
        }
    
        /*Entra en juego el Proceso Monitor*/
        else if (monitor == 0){
            Monitor(memoria);
        }

        else{
            Comprobador(memoria);
        }

    }
    
}