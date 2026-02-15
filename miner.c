#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>

#include "sistema.h"
#include "hilo.h"
#include "pow.h"
#include "cola.h"



//MACROS----------------------------------------------------------------------------
#define MAX_INTENTOS 100 //Número máximo de comprobaciones que realiza el minero ganador, para ver que hayan votado su solución el resto de mineros 
#define SEM_HISTORIAL "/sema_historial" //Nombre del semáforo que manejan los Registradores de cada minero para cuando tengan que enviar el historial, lo hagan respetando la exclusión mutua entre ellos
#define ERROR -1
//VARIABLES GLOBALES------------------------------------------------------
info_sistema *informacion_sistema;//Segmento de memoria del sistema

Boolean solucionencontrada; //global para que accedan todos los hilos
Boolean votacion=FALSE;//caso especial en que están minando y el proceso Minero ganador envía la señal SIGUSR2 para emepzar a votar(no estarían el Sigsuspend)
Boolean solicitud_historial = FALSE;//bandera para indicar los mineros han recibido SIGHUB y deben enviar la señal a su respectivo Registrador 
Boolean enviar_historial = FALSE;//bandera para indicar que los Registradores han recibido SIGURS1 y tienen que enviar todos los bloques registrados en el fichero
Boolean temporizador;//bandera para indicar que ha recibido SIGALRM
int rounds; //número de rondas

int descriptores_tuberia[2]; //tubería canal escritura lectura
int descriptor_memoria_compartida;

mqd_t queue;//cola de mensajes

sem_t *orden_envio_historial;//semáforo con nombre para la sincronización y exclusión mutua entre los Registradores a la hora de enviar el historial de bloques por la cola
sem_t *semaforo_recuperacion_bloques;//semaforo cola para los Registradores
sem_t *semaforo_cola;//semaforo cola para los Mineros


pid_t registrador; //proceso registrador del minero

struct sigaction action_sigint;
struct sigaction action_sigusr1;
struct sigaction action_sigusr2;
struct sigaction action_sighup;



Boolean finalizacion_con_exito = TRUE;
//CABECERA FUNCIONES------------------------------------------------------
int buscar_minero (minero_t mineros[], pid_t pid);


void Conexion_Memoria_Primer_Minero();
void Conexion_Memoria_NO_Primer_Minero();

void Minero (int n_threads);
int Registro_minero ();

void Minado (int n_threads);
void *mythread(void *arg);


void Minero_ganador(long int solucion);
void envio_de_bloque(bloque *bloque_actual);
void preparacion_siguiente_ronda();

void Minero_perdedor();
void Registro_voto();


void registro_bloque(bloque *bloque_actual);

void finalizacion_minero();


void Envio_bloques_por_cola_a_comprobador();
void lectura_bloque_de_descriptor_de_fichero(FILE *fichero);


void handler_sigint(int sig);
void handler_sigusr1(int sig);
void handler_sigusr2(int sig);
void handler_alarm(int sig);
void handler_registrador_enviar_historial(int sig);
void handler_sighup(int sig);




//MANEJADORES DE SEÑAL------------------
void handler_sigint(int sig){
    finalizacion_minero();
    //fprintf(stdout, "%d: manejado señal SIGint1\n", getpid());
}
void handler_sighup(int sig){
    solicitud_historial = TRUE;
    //fprintf(stdout,"manejo sighup %d\n", getpid());

}
void handler_sigusr2(int sig){
    //fprintf(stdout, "recibo la señal Sigusr2\n");
    votacion = TRUE;
}

void handler_alarm(int sig){
    temporizador = TRUE;
}
void handler_registrador_enviar_historial(int sig){
    //fprintf(stdout, "voy a enviar el historial %d\n", getpid());
    enviar_historial = TRUE;
}
void handler_sigusr1(int sig){
    
}
//-----------------------------------------------------


//REGISTRADOR---------------------------------
void Envio_bloques_por_cola_a_comprobador(FILE *file){
    struct sigaction action_alarm;   
    bloque *Bloque = NULL;
    int i=0;
    char *linea = NULL;
    ssize_t result;
    size_t lenght;
    char *cadena = NULL;
    char *token = NULL;

    action_alarm.sa_handler = handler_alarm;
    sigfillset(&(action_alarm.sa_mask));
    action_alarm.sa_flags = 0;
    if (sigaction(SIGALRM, &action_alarm, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
        
    
    if((Bloque = (bloque *)malloc(sizeof(bloque)))== NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    temporizador = FALSE;
    alarm(TEMPORIZADOR);
   
        
    do{
        result =getline(&linea,&lenght,file);
        if(result < 1){
            break;
        }
        /*if(result == -1){
            perror("getline");
        }*/
        sscanf(linea,"Id :   %d\n", &(Bloque->id));
        free(linea);
        linea = NULL;
        
        result = getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/

        sscanf(linea, "Winner :   %d\n", &(Bloque->minero_ganador));
        free(linea);
        linea = NULL;
        result = getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/
        sscanf(linea, "Target :   %08ld\n", &(Bloque->target));
        free(linea);
        linea = NULL;

        result = getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/
        sscanf(linea, "Solution :   %08ld (%s)\n", &(Bloque->solucion), cadena);
        free(linea);
        linea = NULL;

        result = getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/
        sscanf(linea, "Votes :   %d/%d\n", &(Bloque->num_votos_positivos), &(Bloque->num_votos));
        free(linea);
        linea = NULL;

        result =getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/
        
        token = strtok(linea, " ");
        for(i=0; i < Bloque->num_votos +1; i++){
            if((token = strtok(NULL, " ")) == NULL){
                perror("token");
            }
            sscanf(token, "%d:%d", &(Bloque->carteras[i].pid), &(Bloque->carteras[i].monedas));
        }
        

        result =getline(&linea,&lenght,file);
        /*if(result == -1){
            perror("getline");
        }*/
        free(linea);
        linea = NULL;
        
        sem_wait(orden_envio_historial);
        sem_wait(semaforo_recuperacion_bloques);
        envio_de_bloque(Bloque);
        sem_post(orden_envio_historial);
        
    }while(!feof(file) && temporizador==FALSE);
    
    Bloque->bloque_especial = TRUE;
    
    sem_wait(orden_envio_historial);
    sem_wait(semaforo_recuperacion_bloques);
    envio_de_bloque(Bloque);
    sem_post(orden_envio_historial);

}

void Registro_bloque_registrador(bloque *Bloque, int fichero){
    int i;


    dprintf(fichero, "Id :   %d\n", Bloque->id);
    dprintf(fichero, "Winner :   %d\n", Bloque->minero_ganador);
    dprintf(fichero, "Target :   %08ld\n", Bloque->target);
        
        
    if(Bloque->num_votos_positivos >= Bloque->num_votos){
        dprintf(fichero, "Solution :   %08ld (validated)\n", Bloque->solucion);
    }
    else{
        dprintf(fichero, "Solution :   %08ld (rejected)\n", Bloque->solucion);
    }
        
    dprintf(fichero, "Votes :   %d/%d\n", Bloque->num_votos_positivos,Bloque->num_votos);
    dprintf(fichero, "Wallets: ");
    
    
    for(i = 0; i < Bloque->num_votos + 1; i++){
        dprintf(fichero, "%d:%d ", Bloque->carteras[i].pid, Bloque->carteras[i].monedas);
    }
    dprintf(fichero, "\n\n");

}

void Registrador(){
    ssize_t nbytes;
    bloque *Bloque = NULL;
    int i;
    struct sigaction action_sigusr1;
    char nombrefichero[200];
    int fichero;
    FILE *file  = NULL;


    
    action_sigusr1.sa_handler = handler_registrador_enviar_historial;
    sigfillset(&(action_sigusr1.sa_mask));
    action_sigusr1.sa_flags = 0;
    
    if (sigaction(SIGUSR1, &action_sigusr1, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    if((Bloque = (bloque *)malloc(sizeof(bloque)))== NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    
    
    close(descriptores_tuberia[1]);
    
    sprintf(nombrefichero,"registro_pid_%d.txt", (int) getppid());
    
    
    queue = mq_open(MQ_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attributes);
    if(queue == (mqd_t)-1){
        fprintf(stderr, "Error opening the queue\n");
        sem_close(semaforo_cola);
        exit(EXIT_FAILURE);
    } 
    
    if((semaforo_recuperacion_bloques = sem_open(SEM_RECUP_BLOQUES , O_CREAT , S_IRUSR | S_IWUSR , 0)) == SEM_FAILED){
        perror("sem_open");
        exit (EXIT_FAILURE);
    }

    if((orden_envio_historial = sem_open(SEM_HISTORIAL,  O_CREAT , S_IRUSR | S_IWUSR , 1)) == SEM_FAILED){
        perror("sem_open");
        exit (EXIT_FAILURE);
    }


    do{
        nbytes = read(descriptores_tuberia[0] , &(Bloque->id) , sizeof(Bloque->id));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            //free(Bloque);
            exit(EXIT_FAILURE);
        }
        
        nbytes = read(descriptores_tuberia[0] , &(Bloque->minero_ganador) , sizeof(Bloque->minero_ganador));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            //free(Bloque);
            exit(EXIT_FAILURE);
        }
        

        nbytes = read(descriptores_tuberia[0] , &(Bloque->num_votos) , sizeof(Bloque->num_votos));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            //free(Bloque);
             exit(EXIT_FAILURE);
        }
            
        
        
        nbytes = read(descriptores_tuberia[0] , &(Bloque->num_votos_positivos) , sizeof(Bloque->num_votos_positivos));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            //free(Bloque);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
        
        
            
        nbytes = read(descriptores_tuberia[0] , &(Bloque->solucion) , sizeof(Bloque->solucion));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            //free(Bloque);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
        
        
        nbytes = read(descriptores_tuberia[0] , &(Bloque->target) , sizeof(Bloque->target));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            //free(Bloque);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
      
        
        for(i=0; i<Bloque->num_votos; i++){
            nbytes = read(descriptores_tuberia[0] , &(Bloque->votos[i]) , sizeof(Bloque->votos[i]));
            if(nbytes <= 0) {
                close(descriptores_tuberia[0]);
                //free(Bloque);
                sem_close(semaforo_cola);
                sem_close(orden_envio_historial);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_HISTORIAL);
                sem_unlink(SEM_RECUP_BLOQUES);
                mq_close(queue);
                exit(EXIT_FAILURE);
            }
        }
        
        
        for(i=0; i<Bloque->num_votos+1 ; i++){  
            nbytes = read(descriptores_tuberia[0] , &(Bloque->carteras[i].pid), sizeof(Bloque->carteras[i].pid));
            if(nbytes <= 0) {
                close(descriptores_tuberia[0]);            
                //free(Bloque);
                sem_close(semaforo_cola);
                sem_close(orden_envio_historial);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_HISTORIAL);
                sem_unlink(SEM_RECUP_BLOQUES);
                mq_close(queue);
                exit(EXIT_FAILURE);
            }
        }
        for(i=0; i<Bloque->num_votos +1; i++){  
            nbytes = read(descriptores_tuberia[0] , &(Bloque->carteras[i].monedas), sizeof(Bloque->carteras[i].monedas));
            if(nbytes <= 0) {
                close(descriptores_tuberia[0]);
                //free(Bloque);
                sem_close(semaforo_cola);
                sem_close(orden_envio_historial);
                sem_unlink(SEM_HISTORIAL);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_RECUP_BLOQUES);
                mq_close(queue);
                exit(EXIT_FAILURE);
            }
        }
        nbytes = read(descriptores_tuberia[0] , &(Bloque->bloque_especial) , sizeof(Bloque->bloque_especial));
        if(nbytes <= 0) {
            close(descriptores_tuberia[0]);
            //free(Bloque);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }

        if(Bloque->bloque_especial == TRUE){
            close(descriptores_tuberia[0]);
            //free(Bloque);
            sem_close(semaforo_cola);
            sem_close(orden_envio_historial);
            sem_close(semaforo_recuperacion_bloques);
            sem_unlink(SEM_HISTORIAL);
            sem_unlink(SEM_RECUP_BLOQUES);
            mq_close(queue);
            exit(EXIT_FAILURE);
        }
        
        if (nbytes != 0){
            if((fichero = open(nombrefichero, O_CREAT | O_APPEND | O_WRONLY,  S_IRUSR | S_IWUSR  |S_IXUSR )) == -1) {
                perror("open") ;
                close(descriptores_tuberia[0]);
                sem_close(semaforo_cola);
                sem_close(orden_envio_historial);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_HISTORIAL);
                sem_unlink(SEM_RECUP_BLOQUES);
                mq_close(queue);
                exit(EXIT_FAILURE);
            }
            Registro_bloque_registrador(Bloque, fichero);
            close(fichero);
        }

        if(enviar_historial == TRUE){
            if((file = fopen(nombrefichero, "r")) == NULL) {
                perror("fopen") ;
                close(descriptores_tuberia[0]);
                sem_close(semaforo_cola);
                sem_close(orden_envio_historial);
                sem_close(semaforo_recuperacion_bloques);
                sem_unlink(SEM_HISTORIAL);
                sem_unlink(SEM_RECUP_BLOQUES);
                mq_close(queue);
                exit(EXIT_FAILURE);
            }

            Envio_bloques_por_cola_a_comprobador(file);
            fclose(file);
            enviar_historial = FALSE;

        }

    }while (nbytes != 0);
    sem_close(orden_envio_historial);
    sem_close(semaforo_recuperacion_bloques);
    sem_unlink(SEM_HISTORIAL);
    sem_unlink(SEM_RECUP_BLOQUES);
    close(descriptores_tuberia[0]);
    //free(Bloque);
    exit(EXIT_SUCCESS);
}


//-----------------------------------------------------------------------------------------------------
int buscar_minero (minero_t  mineros[], pid_t pid){
    int i;
    for (i=0; i<MAX_MINEROS; i++){
        if (mineros[i].pid == pid)
            return i;
    }
    return ERROR;
}
//------------------------------------------------------------------------
void Conexion_Memoria_Primer_Minero(){
    int i;           
    //1º damos tamaño a la memoria compartida
    if(ftruncate(descriptor_memoria_compartida, sizeof(info_sistema)) == ERROR){
        perror("ftruncate");
        exit(EXIT_FAILURE);
    }
    //Mapeamos la memoria compartida 
    if ((informacion_sistema = mmap(NULL, sizeof(info_sistema), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_memoria_compartida, 0)) == MAP_FAILED){
        perror ("mmap");
        exit(EXIT_FAILURE);
    }

    //semáforo SIN NOMBRE de acceso a la memoria compartida
    if (sem_init (&(informacion_sistema->mutex), 1, 1) == ERROR){
        perror("sem_init");
        sem_close(semaforo_cola);
        mq_close(queue);
        munmap(informacion_sistema, sizeof(info_sistema));
        exit(EXIT_FAILURE);
    }

    //semáforo para acceder segmento memoria en caso de ser minero ganador
    if (sem_init (&(informacion_sistema->semaforoMinero_ganador), 1, 1) == ERROR){
        perror("sem_init");
        sem_close(semaforo_cola);
        mq_close(queue);
        munmap(informacion_sistema, sizeof(info_sistema));
        exit(EXIT_FAILURE);
    }
    
    informacion_sistema->sems_creados = TRUE;

    //establecemos valor por defecto
    informacion_sistema->numero_mineros = 0;
    for(i = 0; i < MAX_MINEROS; i++){
        informacion_sistema->mineros[i].pid = NO_PID;
        informacion_sistema->mineros[i].activo = FALSE;
        informacion_sistema->mineros[i].monedas = 0;
        informacion_sistema->mineros[i].numero_votos = 0;
    }
    informacion_sistema->numero_mineros_activos  = 0;
    sem_post(&informacion_sistema->mutex);
    //retornamos la variable mapeada información del sistema 
    return;
}

//******************************************************************************************
//FIN FUNCION CONEXION_MEMORIA_PRIMER_MINERO
//******************************************************************************************
void Conexion_Memoria_NO_Primer_Minero(){  
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
    return;
}

//******************************************************************************************
//FIN FUNCION CONEXION_MEMORIA_NO_PRIMER_MINERO
//******************************************************************************************

void Minero (int n_threads){ 
    int i;
    sigset_t mascara_no_primer_minero;
    Boolean primer_minero = FALSE;

    action_sigusr2.sa_handler = handler_sigusr2;
    sigfillset(&(action_sigusr2.sa_mask));
    action_sigusr2.sa_flags = 0;

   
   action_sigint.sa_handler = handler_sigint;
    sigfillset(&(action_sigint.sa_mask));
    action_sigint.sa_flags = 0;

    action_sigusr1.sa_handler = handler_sigusr1;
    sigfillset(&(action_sigusr1.sa_mask));
    action_sigusr1.sa_flags = 0;

    
    action_sighup.sa_handler = handler_sighup;
    sigfillset(&(action_sighup.sa_mask));
    action_sighup.sa_flags = 0;

    if (sigaction(SIGHUP, &action_sighup, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    if (sigaction(SIGINT, &action_sigint, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    
    if (sigaction(SIGUSR2, &action_sigusr2, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    if (sigaction(SIGUSR1, &action_sigusr1, NULL) < 0){
        perror("sigaction");
        exit(EXIT_FAILURE);
    }


    close(descriptores_tuberia[0]);

    //semáforo para acceder a la cola de mensajes
    if((semaforo_cola = sem_open(SEM_COLA , O_CREAT , S_IRUSR | S_IWUSR , 0)) == SEM_FAILED){
        perror("sem_open");
        exit (EXIT_FAILURE);
    }


    
    queue = mq_open(MQ_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR, &attributes);
    if(queue == (mqd_t)-1){
        fprintf(stderr, "Error opening the queue\n");
        sem_close(semaforo_cola);
        sem_unlink(SEM_COLA);
        exit(EXIT_FAILURE);
    } 
    
    descriptor_memoria_compartida = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (descriptor_memoria_compartida == ERROR){
    //Comprobamos que el error que nos reporta es porque ya la hemos creado
        //No soy primer minero---------------------------------------------
        if (errno == EEXIST){
            primer_minero = FALSE;
            
            descriptor_memoria_compartida = shm_open(SHM_NAME, O_RDWR, 0);
            if(descriptor_memoria_compartida == ERROR) {
                perror("Error opening the shared memory segment");
                sem_close(semaforo_cola);
                sem_unlink(SEM_COLA);
                mq_close(queue);
                mq_unlink(MQ_NAME);
                exit(EXIT_FAILURE);
            }
            
            Conexion_Memoria_NO_Primer_Minero();

        } 
        else {
            perror("Error creating the shared memory segment\n");
            sem_close(semaforo_cola);
            sem_unlink(SEM_COLA);
            mq_close(queue);
            mq_unlink(MQ_NAME);
            exit(EXIT_FAILURE);
        }
    }

    //Soy primer minero---------------------------------------------
    else{
        primer_minero = TRUE;
        Conexion_Memoria_Primer_Minero();
    }
    //Para todos los mineros-------------------------------------------
    //Registro del minero en el sistema
    if (Registro_minero(informacion_sistema) == ERROR){
        fprintf(stdout, "Miners limit reached.\n");
        sem_close(semaforo_cola);
        mq_close(queue);
        munmap(informacion_sistema, sizeof(info_sistema));
        exit(EXIT_FAILURE);
    }
    
    //Preparación
    if (primer_minero){
        //Soy primer minero---------------------------------------------
        sem_wait(&(informacion_sistema->mutex));
        
        
        //establece el objetivo inicial para el primer bloque
        informacion_sistema->bloque_actual.target = 0;
        informacion_sistema->bloque_actual.id = 0;
       
        //Arranca el sistema enviando la señal SIGUSR1 al resto de mineros
        usleep(10);
        
        for (i=0; i<informacion_sistema->numero_mineros_activos; i++){
            
            if (informacion_sistema->mineros[i].pid != getpid()){
                kill(informacion_sistema->mineros[i].pid, SIGUSR1);
            }
        }
        sem_post(&(informacion_sistema->mutex));
    } 
    else {
        //No soy primer minero---------------------------------------------
        
        //Espera la llegada de la señal SIGUSR1
        sigfillset(&mascara_no_primer_minero);
        sigdelset(&mascara_no_primer_minero, SIGUSR1);
        sigsuspend(&mascara_no_primer_minero);


    }

    //Para todos los mineros-------------------------------------------
    //Minado
    Minado (n_threads);


    finalizacion_minero();

}


//******************************************************************************************
//FIN FUNCION MINERO
//******************************************************************************************

int Registro_minero (){
    
    int result = 0;
    int posicion;

    sem_wait(&(informacion_sistema->mutex));

    posicion = buscar_minero(informacion_sistema->mineros, getpid());
    if(posicion == ERROR){

        if (informacion_sistema->numero_mineros == MAX_MINEROS){
            result = ERROR;

        } 
        else {
            informacion_sistema->mineros[informacion_sistema->numero_mineros].pid = getpid();
            informacion_sistema->mineros[informacion_sistema->numero_mineros].monedas = 0;
            informacion_sistema->mineros[informacion_sistema->numero_mineros].numero_votos = 0;
            informacion_sistema->mineros[informacion_sistema->numero_mineros].activo = TRUE;
            informacion_sistema->numero_mineros++;
            informacion_sistema->numero_mineros_activos++;

        }
    }

    else{
        informacion_sistema->mineros[posicion].activo = TRUE;
        informacion_sistema->numero_mineros_activos++;
    }

    sem_post(&(informacion_sistema->mutex));

    return result;
}


//******************************************************************************************
//FIN FUNCION REGISTRO_MINERO
//******************************************************************************************
void Minado (int n_threads){

    pthread_t *hilo; /*cadena de los hilos que se van a crear*/
    long int taget1;
    long int taget_init;         /*Y del f(X)=Y a resolver*/
    return_t **rvals;   /*cadena de los retornos de hilos que se van a crear*/
    parametro_t *arg; /*informacion inicial de un hilo,antes de la busqueda*/   
    int i,j,k;
    int thread_status;  
    sigset_t mascara_minero_perdedor;  
    long int solucion;


    sigfillset(&mascara_minero_perdedor);
    sigdelset(&mascara_minero_perdedor, SIGUSR2);

    
    sem_wait(&(informacion_sistema->mutex));
    taget_init = informacion_sistema->bloque_actual.target;
    sem_post(&(informacion_sistema->mutex));

    arg = (parametro_t*)malloc(n_threads*sizeof(parametro_t));
    rvals = (return_t**)malloc(n_threads*sizeof(return_t));
    hilo = (pthread_t*)malloc(n_threads*sizeof(pthread_t));

    
    /*variable taget_init es argumento de entrada que recibe el main*/
    taget1 = taget_init; /*Y del f(X)=Y a resolver*/
    
    /*se incializa la estructura de datos de inicial que va a tener como referencia un hilo antes de iniciar su proceso de búsqueda*/
     /*el número de hilos lo va a necesitar para entre posiciones tiene que buscar ese hilo concreto*/
    /*Proceso Minero se encarga de cada ronda de minado*/
    for (i = 0; i < rounds; i++){
        
        /*Cada ronda de minado actualizamos la variable  solucionencontrada*/
        solucionencontrada = FALSE;
        votacion = FALSE;

        //Crea los hilos
        for (j = 0; j < n_threads; j++){
            arg[j].taget = taget1; 
            arg[j].position = j;
            arg[j].n_threads = n_threads; /*Minero le pasa a la estructura inicial antes de leer a un hilo un parametro position que va permitirle definir posición inicial y final de búsqueda*/
            arg[j].sistema = informacion_sistema;
            thread_status = pthread_create(&hilo[j], NULL, mythread, &arg[j]);
            if (thread_status != 0) {
                exit(EXIT_FAILURE);
            }
        }
        //Espera a que terminen los hilos
        for (k = 0; k < n_threads; k++){
            thread_status = pthread_join(hilo[k], (void **)&rvals[k]);
            if (thread_status != 0) {
                exit(EXIT_FAILURE);
            }
            if (rvals[k]->encontrado == TRUE){
                solucion = rvals[k]->taget2;
            }
        }
    
        
        if(votacion == TRUE){
            Minero_perdedor();

        } 
        
        else if((sem_trywait(&(informacion_sistema->semaforoMinero_ganador))) < 0){ 
            /*minero perdedor que ha terminado de minar, espera SIGUSR2*/
            sigsuspend(&mascara_minero_perdedor);
            Minero_perdedor();
        }

        else{
            /*minero ganador*/
            Minero_ganador(solucion);
            sem_post(&(informacion_sistema->semaforoMinero_ganador));
            taget1 = solucion;
        }


        sem_wait(&(informacion_sistema->mutex));
        registro_bloque(&(informacion_sistema->ultimo_bloque));
        sem_post(&(informacion_sistema->mutex));

        if(solicitud_historial == TRUE){
            kill(registrador, SIGUSR1);
            solicitud_historial = FALSE;
            
        }
    }    
    //se retorna a la funcion Minero
    for (j = 0; j < n_threads; j++){
            free(rvals[j]);

    }
    free(hilo);
    free(arg);
    free(rvals);
    finalizacion_minero();
}

//******************************************************************************************
//FIN FUNCION MINADO
//******************************************************************************************

void *mythread(void *arg){
    parametro_t *args = (parametro_t *)arg;
    
    return_t *rvals = malloc(sizeof(return_t));
    if(rvals == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    long int ini = args->position * ((POW_LIMIT - 1) / args->n_threads);       /*se indica la posicion inicial de busqueda de ese hilo*/
    long int fin = (args->position + 1) * ((POW_LIMIT - 1) / args->n_threads); /*se indica la posicion final de busqueda de ese hilo*/
    long int y;
    
    rvals->taget1 = args->taget;
    rvals->encontrado = FALSE;
    
    for (; ini <= fin && solucionencontrada == FALSE; ini++){
        y = pow_hash(ini);
        if (y == rvals->taget1){
            rvals->taget2 = ini; 
            rvals->encontrado = TRUE;
            solucionencontrada = TRUE;
            return (void *)rvals;
        }
    
    }
    

    return (void *)rvals;
}
//******************************************************************************************
//FIN FUNCION MY_THREAD
//******************************************************************************************


void Minero_ganador(long int solucion){
    int i, j=0, k=0, intentos = 0, posicion;
    Boolean votado_todos = FALSE;
    bloque *bloque = NULL;
 
    /*prepara la memoria compartida para la votación*/
    
    /*no soltamos el semáforo hasta que enviemos la señal SIGUSR2 para el arranque de la votación, para evitar que otro 
    proceso que encuentre la solución inicialice el sistema*/
    
    sem_wait(&informacion_sistema->mutex);
    /*introduciendo la solución obtenida en el bloque actual*/
    
    informacion_sistema->bloque_actual.solucion = solucion;
    informacion_sistema->bloque_actual.num_votos_positivos = 0;

    /*envía la señal SIGUSR2 al resto de mineros que han participado en la ronda para que arranquen la votación*/
    for(i=0; k < informacion_sistema->numero_mineros_activos -1; i++){
        if(informacion_sistema->mineros[i].activo == TRUE && informacion_sistema->mineros[i].pid != getpid()){
            kill(informacion_sistema->mineros[i].pid, SIGUSR2);
            k++;
        }
    }
    
    sem_post(&(informacion_sistema->mutex));

    /*Realiza espera no activas hasta que todos los mineros hayan registrado su voto o hasta que transcurra un número máximo de intentos*/
    
    while(votado_todos == FALSE && intentos < MAX_INTENTOS){
        sem_wait(&informacion_sistema->mutex);
        if (informacion_sistema->bloque_actual.num_votos == informacion_sistema->numero_mineros_activos -1){
            votado_todos = TRUE;
        }

        sem_post(&(informacion_sistema->mutex));
        
        
        usleep(10);
        intentos++;
    }
    
    sem_wait(&informacion_sistema->mutex);
    
    for(i=0;i < informacion_sistema->bloque_actual.num_votos;i++){
        if(informacion_sistema->bloque_actual.votos[i] == TRUE){
            informacion_sistema->bloque_actual.num_votos_positivos++;
            
        }

    }
    
    
    informacion_sistema->bloque_actual.minero_ganador = getpid();
    if((informacion_sistema->bloque_actual.num_votos == 0) || (informacion_sistema->bloque_actual.num_votos == 1 && informacion_sistema->bloque_actual.num_votos_positivos ==1)|| (informacion_sistema->bloque_actual.num_votos > 1 && informacion_sistema->bloque_actual.num_votos_positivos >= informacion_sistema->bloque_actual.num_votos/2)){
        posicion = buscar_minero(informacion_sistema->mineros, informacion_sistema->bloque_actual.minero_ganador);
        informacion_sistema->mineros[posicion].monedas++;
    }


    for(i=0; i<informacion_sistema->numero_mineros && j<informacion_sistema->numero_mineros_activos;i++){
            if(informacion_sistema->mineros[i].activo == TRUE){
                informacion_sistema->bloque_actual.carteras[j].monedas = informacion_sistema->mineros[i].monedas;
                informacion_sistema->bloque_actual.carteras[j].pid = informacion_sistema->mineros[i].pid;
                j++;
            }
    }
    bloque = &(informacion_sistema->bloque_actual); 
    sem_post(&(informacion_sistema->mutex));
    if(sem_trywait(semaforo_cola) < 0){ 
        preparacion_siguiente_ronda();
    }

    else{
        envio_de_bloque(bloque);
        sem_post(semaforo_cola);
        preparacion_siguiente_ronda();

    }
}

//******************************************************************************************
//FIN FUNCION MINERO_GANADOR
//******************************************************************************************
void Minero_perdedor(){
    sigset_t mascara_nueva_ronda;
    
    Registro_voto();
    
    
    
    sigfillset(&mascara_nueva_ronda);
    sigdelset(&mascara_nueva_ronda, SIGUSR1);

    sigsuspend(&mascara_nueva_ronda);
}

//******************************************************************************************
//FIN FUNCION MINERO_PERDEDOR
//******************************************************************************************
void Registro_voto(){
    sem_wait(&(informacion_sistema->mutex));
    informacion_sistema->bloque_actual.votos[informacion_sistema->bloque_actual.num_votos ] = informacion_sistema->bloque_actual.target == pow_hash(informacion_sistema->bloque_actual.solucion);

    informacion_sistema->bloque_actual.num_votos++;

    sem_post(&(informacion_sistema->mutex));

}
//******************************************************************************************
//FIN FUNCION REGISTRO_VOTO
//******************************************************************************************


void envio_de_bloque(bloque *bloque_actual){
    //Enviar la solución al monitor por COLA DE MENSAJE
    if(mq_send(queue, (char*)bloque_actual, sizeof(bloque), 1) == ERROR){
        perror("mq_send");
        exit(EXIT_FAILURE);
    }


}

//******************************************************************************************
//FIN FUNCION ENVIO_BLOQUE
//******************************************************************************************
void preparacion_siguiente_ronda(){
    int i,k =0;
   
    sem_wait(&(informacion_sistema->mutex));
    
    informacion_sistema->ultimo_bloque = informacion_sistema->bloque_actual;
    informacion_sistema->bloque_actual.id = informacion_sistema->ultimo_bloque.id +1;
    informacion_sistema->bloque_actual.target = informacion_sistema->ultimo_bloque.solucion;
    informacion_sistema->bloque_actual.minero_ganador = -1;
    informacion_sistema->bloque_actual.num_votos = 0; 
    informacion_sistema->bloque_actual.num_votos_positivos = 0;

    for(i=0; k < informacion_sistema->numero_mineros_activos -1; i++){
        if(informacion_sistema->mineros[i].activo == TRUE && informacion_sistema->mineros[i].pid != getpid()){
            kill(informacion_sistema->mineros[i].pid, SIGUSR1);
            k++;
        }
    }
    sem_post(&(informacion_sistema->mutex));

}

//******************************************************************************************
//FIN FUNCION PREPARACION_SIGUIENTE_RONDA
//******************************************************************************************

void registro_bloque(bloque *bloque_actual){
    ssize_t size_written;
    int i;
    
    
    size_written = write(descriptores_tuberia[1], &(bloque_actual->id), sizeof(bloque_actual->id));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    size_written = write(descriptores_tuberia[1], &(bloque_actual->minero_ganador), sizeof(bloque_actual->minero_ganador));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    size_written = write(descriptores_tuberia[1], &(bloque_actual->num_votos), sizeof(bloque_actual->num_votos));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    size_written = write(descriptores_tuberia[1], &(bloque_actual->num_votos_positivos), sizeof(bloque_actual->num_votos_positivos));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    size_written = write(descriptores_tuberia[1], &(bloque_actual->solucion), sizeof(bloque_actual->solucion));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    size_written = write(descriptores_tuberia[1], &(bloque_actual->target), sizeof(bloque_actual->target));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }

    for(i=0; i <  bloque_actual->num_votos; i++){
        size_written = write(descriptores_tuberia[1], &(bloque_actual->votos[i]), sizeof(bloque_actual->votos[i]));
        if(size_written == ERROR) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }

    for(i=0; i <  bloque_actual->num_votos+1; i++){
        size_written = write(descriptores_tuberia[1], &(bloque_actual->carteras[i].pid), sizeof(bloque_actual->carteras[i].pid));
        if(size_written == ERROR) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }
    
    for(i=0; i <  bloque_actual->num_votos+1; i++){
        size_written = write(descriptores_tuberia[1], &(bloque_actual->carteras[i].monedas), sizeof(bloque_actual->carteras[i].monedas));
        if(size_written ==ERROR) {
            perror("write");
            exit(EXIT_FAILURE);
        }
    }
    
    size_written = write(descriptores_tuberia[1], &(bloque_actual->bloque_especial), sizeof(bloque_actual->bloque_especial));
    if(size_written == ERROR) {
        perror("write");
        exit(EXIT_FAILURE);
    }
}

//******************************************************************************************
//FIN FUNCION REGISTRO_BLOQUE
//******************************************************************************************
void finalizacion_minero(){
    int posicion;
    Boolean soy_el_ultimo = FALSE;
    bloque *Bloque = NULL;
    int registrador_status;

    
    sem_wait(&informacion_sistema->mutex);
    

    posicion = buscar_minero(informacion_sistema->mineros, getpid());
    informacion_sistema->mineros[posicion].activo = FALSE;
    informacion_sistema->numero_mineros_activos--;
    
    if(informacion_sistema->numero_mineros_activos == 0){
        sem_close(&(informacion_sistema->semaforoMinero_ganador));
        sem_close(&informacion_sistema->mutex);
        sem_unlink(SEM_MINER_WINNER);
        close(descriptor_memoria_compartida);
        shm_unlink(SHM_NAME);
        munmap(informacion_sistema, sizeof(info_sistema));
        soy_el_ultimo = TRUE;
    }

    else{
        sem_close(&(informacion_sistema->semaforoMinero_ganador));
        sem_close(&informacion_sistema->mutex);
        sem_post(&informacion_sistema->mutex);

        close(descriptor_memoria_compartida);
    }

    
    
    if(soy_el_ultimo == TRUE){
        if(sem_trywait(semaforo_cola) >= 0){
            if((Bloque = (bloque *)malloc(sizeof(bloque)))== NULL){
                perror("malloc");
                exit(EXIT_FAILURE);
            }
            Bloque->bloque_especial = TRUE;
            envio_de_bloque(Bloque);
            sem_post(semaforo_cola);
            //free(Bloque);
            //Bloque = NULL;
        }
    }
    
    
    
    if(soy_el_ultimo == TRUE){
        sem_close(semaforo_cola);
        mq_close(queue);
        sem_unlink(SEM_COLA);
        mq_unlink(MQ_NAME);
    }
    
    close(descriptores_tuberia[1]);
    wait(&registrador_status);
    
    exit(EXIT_SUCCESS);
}
//******************************************************************************************
//FIN FUNCION FINALIZACION_MINERO
//******************************************************************************************

int main(int argc, char *argv[]){
    int n_threads; //número de hilos que se van a utilizar
    int pipe_status;
    
    if (argc != 3){
        fprintf(stdout, "Error de argumentos\n");
        exit(EXIT_FAILURE);
    }
    rounds = atoi(argv[1]);
    n_threads = atoi(argv[2]);


    pipe_status = pipe(descriptores_tuberia);
    if(pipe_status ==ERROR){
        perror ("pipe");
        exit (EXIT_FAILURE);
    }

    registrador = fork();
    if (registrador < 0){
        perror("fork");
        exit(EXIT_FAILURE);
    }
    
    /*Entra en juego el Proceso Registrador*/
    else if (registrador == 0){
        Registrador();
    }

    /*Proceso Minero*/
    else{
        Minero(n_threads);
    }
}





