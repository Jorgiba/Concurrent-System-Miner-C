/**
 * @file monitor.c
 * @author Carlos Vives & Jorge Ibarreta
 * @brief File for the Project.
 * @version 1.0.
 * @date 2024-05-1
 *
 * @copyright Copyright (c) 2024
 *
 */



/* librerias necesarias */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>     /* procesos */
#include <sys/wait.h>   /* waits */
#include <semaphore.h>  /* semaforos */
#include <errno.h>      /* errores */
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>   /* informacion de archivos */
#include <mqueue.h>
#include <signal.h>

#include "mensajesEstruct.h"

/* MACROS */
#define SHM_NAME "/shmComp"

/* variables estaticas y globales para llevar un control de seniales */
static volatile sig_atomic_t sigint_used = 0;

/* estructura de la memoria compartida */
typedef struct{
    Mensaje cola[6];
    /* semaforos anonimos que nos ayudaran a realizar el 'productor-consumidor' (ver pag 16 enunciado) */
    sem_t sem_fill;
    sem_t sem_mutex;
    sem_t sem_empty;
} MemCompartida;

/* funcion manejador de seniales con sigaction */
void handler(int signum){
    if (signum == SIGINT)
        sigint_used = 1;
}


int main(int argc, char *argv[]){
    int fd_shm;                 /* descriptor de fichero de memoria compartida */
    pid_t comprobador;          /* identificador de proceso tras fork */
    pid_t monitor;              /* identificador de monitor tras fork */
    int status;                 /* comprobador de estado de salida de proceso */
    sem_t *creado = NULL;       /* semaforo para memoria compartida */
    MemCompartida *mem = NULL;  /* estructura con memoria compartida */
    int ind = 0;                /* indice para el monitor en la cola ciclica de mensajes */
    mqd_t cola;                 /* cola de mensajes */
    Mensaje msg;
    int id = 0;
    int i = 0;

    struct sigaction act;       /* variables para señales */
    sigset_t set, oset;   /* conjuntos de seniales */

    sigemptyset(&act.sa_mask);
    act.sa_handler = handler;

    /* establecemos un manejador de seniales por cada una de ellas */
    if (sigaction(SIGINT, &act, NULL) < 0){
        perror("Estableciendo manejador de SIGNINT");
        exit(EXIT_FAILURE);
    }

    /* creamos el semaforo que nos asegurara que monitor se ejecute una vez la memoria se haya creado */
    if((creado = sem_open(SEM_NAME, O_CREAT , S_IRUSR | S_IWUSR , 0)) == SEM_FAILED){
        perror("Creando semaforo");
        exit(EXIT_FAILURE);
    }

    /* creamos el proceso comprobador */
    comprobador = fork();
    if(comprobador < 0){
        perror("Creando proceso comprobador");
        exit(EXIT_FAILURE);
    }
    /* entra solo el proceso comprobador que es el proceso principal */
    else if(comprobador > 0){

        printf("SOY COMPROBADOR\n");

        /* esperamos a que monitor desbloquee comprobador */
        sem_wait(creado);

        /* comprobamos que la memoria esta creada, si no, se crea */
        if((fd_shm = shm_open(SHM_NAME, O_RDWR, 0)) == -1){

            /* creamos la memoria compartida */
            if((fd_shm = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1) {
                perror("Creando memoria compartida.");
                exit(EXIT_FAILURE);
            }

            /*Se le asigna el tamanio a la memoria compartida*/
            if (ftruncate(fd_shm, sizeof(MemCompartida)) == -1) {
                perror("Asignando tamanyo a memoria compartida");
                shm_unlink(SHM_NAME);
                exit(EXIT_FAILURE);
            }

            /* la siguiente sentencia eliminara la memoria compartida una vez los procesos dejen de usarla y se elimine
               el descriptor de fichero asociado a esta */
            if (shm_unlink(SHM_NAME) == -1) {
                perror("Eliminando la memoria compartida");
                exit(EXIT_FAILURE);
            }

            /* mapeamos la memoria para comprobador */
            mem = (MemCompartida*) mmap(NULL, sizeof(MemCompartida), PROT_WRITE | PROT_READ, MAP_SHARED, fd_shm, 0);
            if(mem == MAP_FAILED){
                perror("Mapeando memoria de el comprobador");
                /* unlink de la memoria */
                // shm_unlink(SHM_NAME);
                /* cerramos descriptor de fichero de memoria compartida */
                close(fd_shm);
                exit(EXIT_FAILURE);          
            }

            /* cerramos descriptor de fichero de memoria compartida */
            close(fd_shm);

            /* inicializamos los semaforos de la memoria compartida */
            /* primero iniciamos el semaforo del recurso en si a uno */
            sem_init(&mem->sem_mutex, 1, 1);
            /* este semaforo se ira decrementando cuando un proceso se meta y use un hueco de la cola de mensajes*/
            sem_init(&mem->sem_empty, 1, 5);
            /* al igual que el anterior pero al reves, se va llenando */
            sem_init(&mem->sem_fill, 1, 0);

            /* creamos la cola de mensajes */
            cola = mq_open(MQ_NAME, O_RDONLY);         
            if (cola == (mqd_t)-1) {
                perror("Abriendo cola de mensajes");
                //shm_unlink(SHM_NAME);
                exit(EXIT_FAILURE);
            }

            printf("[%d] Checking blocks...\n", getpid());

            /* manejamos la señal SIGNINT */
            sigemptyset(&set);
            sigaddset(&set, SIGINT);
            if (sigprocmask(SIG_BLOCK, &set, &oset) < 0){
                perror("Error. Modificando mascara de seniales (sigprocmask).");
                /* AQUI NO SE SI SALIR DEL PROGRAMA */
            }

            /* se lee de la cola de mensaje y se almacena en memoria compartida */
            while(1){
                
                /* seguimos la estructura de Productor-Consumidor siendo aqui la parte de Productor */
                sem_wait(&(mem->sem_empty));
                sem_wait(&(mem->sem_mutex));
                printf("Justo antes de recibir el mensaje\n");
                if(mq_receive(cola, (char *)&msg, sizeof(msg), NULL) == -1) {
                    fprintf(stderr, "Recibiendo mensaje\n");
                    shm_unlink(SHM_NAME);
                    exit(EXIT_FAILURE);
                }

                mem->cola[ind] = msg;
                ind = (ind+1)%6;       

                sem_post(&(mem->sem_mutex));
                sem_post(&(mem->sem_fill)) ;
                if(msg.finalizado == 1){
                    break;
                }
            }

            if (sigprocmask(SIG_UNBLOCK, &set, NULL) < 0){
                perror("Error. Eliminando la senial de la mascara.");
                /* AQUI NO SE SI SALIR DEL PROGRAMA */
            }

            /*Se liberan los recursos del proceso y se sale*/
            sem_post(creado);
            printf("[%d] Finishing\n", getpid());
            /* cerramos cola */
            mq_close(cola);
            /* destruimos descriptor de cola */
            mq_unlink(MQ_NAME);
            sem_destroy(&(mem->sem_mutex));
            sem_destroy(&(mem->sem_empty));
            sem_destroy(&(mem->sem_fill));
            sem_close(creado);
            munmap(mem, sizeof(MemCompartida));
            exit(EXIT_SUCCESS);
        }

        exit(EXIT_SUCCESS);
    }

    /* entra el proceso hijo que sera el monitor */
    else {

        printf("SOY MONITOR\n");

        /* verificamos que la memoria compartida existe */
        if((fd_shm = shm_open(SHM_NAME, O_RDWR, 0)) == -1){
            /* hacemos 'up' al semaforo creado */
            sem_post(creado);
            exit(EXIT_SUCCESS);
        }

        /* mapeamos la memoria para monitor */
        mem = (MemCompartida*) mmap(NULL, sizeof(MemCompartida), PROT_READ | PROT_WRITE, MAP_SHARED, fd_shm, 0);
        if(mem == MAP_FAILED){
            perror("Mapeando memoria de el monitor");
            /* unlink de la memoria */
            shm_unlink(SHM_NAME);
            /* cerramos descriptor de fichero de memoria compartida */
            close(fd_shm);
            exit(EXIT_FAILURE);            
        }
        if (shm_unlink(SHM_NAME) == -1) {
            perror("Unlink de la memoria compartida");
            exit(EXIT_FAILURE);
        }
        /* cerramos descriptor de fichero de memoria compartida */
        close(fd_shm);

        while(1){
            
            /* seguimos la estructura de Productor-Consumidor teniendo en cuenta que la comprobacion se realiza en el miner.c */
            sem_wait(&(mem->sem_fill));
            sem_wait(&(mem->sem_mutex));

            /* si el dato finalizado de la estructura de mensaje es 1, significa que terminamos programa */
            if(mem->cola[ind].finalizado == 1){
                /* salimos */
                break;
            }

            /* imprimimos los bloques */
            printf("Id: %04d\n", id);
            printf("Winner: %d\n", getpid());
            printf("Target: %08ld\n", (long)mem->cola[ind].objetivo);
            printf("Solution: %08ld (validated)\n", (long)mem->cola[ind].solucion);
            printf("Votes: \n");
            printf("Wallets: \n");
            /*
            for ( i = 0; mem->cola[ind]. blck.wallets[i].id != -1; i++){
                printf(file_des, "%d:%d\t", blck.wallets[i].id), blck.wallets[i].coins;
            }
            */

            
            /* actualizamos el indice teniendo en cuenta que es una cola ciclica (pos = 6 --> pos = 1)*/
            ind = (ind + 1) % 6;
            id++;

            /* salimos de los recursos */
            sem_post(&(mem->sem_mutex));
            sem_post(&(mem->sem_empty));
        }

        /* destruimos todos los semaforos anonimos usados */
        sem_destroy(&(mem->sem_mutex));
        sem_destroy(&(mem->sem_empty));
        sem_destroy(&(mem->sem_fill));
        /* cerramos descriptor de comprobador de memoria creada */
        sem_close(creado);
        /* deshacemos el mapeo */
        munmap(mem, sizeof(MemCompartida));
        exit(EXIT_SUCCESS);
    }
    /* el padre tendra que esperar a sus dos hijos */
    if (wait(&status) == -1){
        perror("Realizando wait");
        exit(EXIT_FAILURE);
    }

    /* lo hariamos en la creacion pero hay que esperar a que se cree */
    shm_unlink(SHM_NAME);

    /* liberamos recursos */
    sem_close(creado);
    sem_unlink(SEM_NAME);

    mq_close(cola);


    exit(EXIT_SUCCESS);
}

