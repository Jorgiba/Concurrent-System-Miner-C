/**
 * @file miner.c
 * @author Carlos Vives & Jorge Ibarreta
 * @brief File for the Proyect, Reusing P3 miner.c.
 * @version 1.0.
 * @date 2024-04-27
 *
 * @copyright Copyright (c) 2024
 *
 */

/* Librerias necesarias para la ejecucion del programa */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <math.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/wait.h>
#include <mqueue.h>
#include <sys/stat.h>   /* flags para mq_open() */
#include <semaphore.h>  /* semaforos */

#include "pow.h"
#include "mensajesEstruct.h"

#define MAX_MINEROS 100             /* maximo de mineros a la vez*/
#define MUTEX_MINER "mutex_miner"   /* nombre del semaforo binario */
#define MAX_NAME_FILE 20
#define SHM_NAME "/shmMiner"

/* esta variable global la usaremos para saber cuando un hilo ha encontrado la solucion */
volatile int flag = 0;
/* esta variable sirve para que el hilo que encuentre solucion guarde el nuevo objetivo */
volatile int new_target;

/**
 * Estructura de datos que guarda la informacion de un hilo
*/
typedef struct{
    int ini;
    int fin;
    int target;
    int id;             /* esta variable es solo para los test y ver que hilo es */
} estrHilo;

/**
 * Estructura de datos que guarda la informacion de la cartera de un minero
*/
typedef struct{
    int id;
    int coins;
} Wallet;

/**
 * Estructura de datos que guarda la informacion de un bloque
*/
typedef struct{
    int id_block;
    int target;
    int solution;
    int pid_winner;
    Wallet wallets[MAX_MINEROS];
    int num_votos;
    int positivos;
} Bloque;



typedef struct{
    int nMiners;
    int id_miners[MAX_MINEROS];
    int nActMiners;
    //listado de votos de cada minero
    Wallet wallets[MAX_MINEROS];
    Bloque lastBlock;
    Bloque currentBlock;
}MemCompartida;

/**
 * Funcion que ejecuta la funcion POW por cada numero 
 */
void funHilo(void *param){
    int i, res;
    estrHilo *var1 = (estrHilo *) param;

    for(i = var1->ini; i < var1->fin && flag == 0; i++){
        res = pow_hash(i);
        if(res == var1->target){
            flag = 1;
            new_target = i;
            return;
        }
    }

    return;
}

/**
 * Funcion para pasar los segundos a nanosegundos
 */
int msleep(long tms) {
  struct timespec ts;
  int ret;

  ts.tv_sec = tms / 1000;

  /* hacemos conversion del resto */
  ts.tv_nsec = (tms % 1000) * 1000000;

  /* realizamos espera no activa */
  ret = nanosleep(&ts, &ts);

  return ret;
}


/* Funcion que organiza los procesos de miner y monitor */
int main(int argc, char* argv[]){
    
    /* variables para los hilos */
    int num_sons_level1 = 0;
    int num_sons_level2 = 0;
    int i = 0, j=0;
    int partes;
    pthread_t *hilos = NULL;
    estrHilo **buscar = NULL;
    int error;

    
    int solution;
    int target;
    int solutionMonitor;

    /* variables para el monitor (tuberias) */
    int solRead, tarRead;
    int solutionFound;

    /* variables P3 */
    mqd_t cola;        /* descriptor de fichero de cola */
    Mensaje msg;   /* cola de mensajes */
    struct mq_attr attr;




    /* VARIABLES PARA PROYECTO */
    

    /* para los argumentos */
    int n_threads;              /* numero de hilos (pasado como argumento)*/
    int lag;                    /* lag (pasado como argumento)*/
    char *endptr;               /* util para la recogida de argumentos */

    /* para tuberias */
    int miner_reg_fd[2];        /* tuberia para comunicar minero y registrador */
    int status;                 /* comprobador de creacion de tuberia */

    /* para semaforos */
    sem_t *mutex = NULL;        /* semaforo para memoria compartida */

    /* para guardar pid de los procesos */
    pid_t miner, registrador;

    /* para el bloque */
    Bloque blck;                        /* bloque */
    char fichero[MAX_NAME_FILE];        /* nombre del fichero */
    FILE *fd =NULL;                     /* puntero para fichero */
    int file_des;                       /* descriptor del fichero */

    /* para la memoria compartida */
    MemCompartida *mem = NULL;
    int fd_mem;

    
    /* verificamos si el primer argumento (LAG) es un numero entero valido */
    /* el 10 de strtol es la base a la que se pasa el argumento (base decimal) */
    lag = (int)strtol(argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || lag < 0){
        printf("%s no es un numero entero valido para los segundos.\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    
    /* verificamos segundo argumento */
    n_threads = (int)strtol(argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || n_threads < 0){
        printf("%s no es un numero entero valido para el numero de hilos.\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    /* primero comprobaremos que los argumentos se han pasado/recogido de forma correcta */
    if(argc != 3 || n_threads <= 0){
        printf("Error en los argumentos. Formato: ./miner <S_SECONDS> <N_THREADS>\n");
        return EXIT_FAILURE;
    }

    /* creamos tuberia de comunicacion entre minero y registrador */
    status = pipe(miner_reg_fd);
    if(status == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    /* iniciamos semaforo mutex (binario) para recurso de memoria compartida */
    if ((mutex = sem_open(MUTEX_MINER, O_CREAT | O_EXCL, S_IRUSR | S_IWUSR, 1)) == SEM_FAILED) {
        perror("Abriendo semaforo mutex");
        exit(EXIT_FAILURE);
    }
    /* realizamos unlink del recurso para que se elimine cuando todos dejen de usarlo */
    sem_unlink(MUTEX_MINER);

    /* guardamos el pid del padre para que el hijo registrador cree un fichero con ese nombre */
    miner = getpid();

    /* Cada minero tendra su proceso registro con su fichero de texto */
    registrador = fork();

    
    if(!registrador) {
        /* solo entra registrador */

        /* cerramos parte de escritura en extremo de registrador ya que este solo va a leer datos */
        close(miner_reg_fd[1]);

        /* leemos el bloque */
        read(miner_reg_fd[0], &blck, sizeof(blck));

        /* creamos el nombre para el fichero */
        sprintf(fichero, "%d.txt", miner);

        /* abrimos el fichero en modo escritor */
        fd = open(fichero, 'w');
        if(!fd){
            perror("Abriendo el fichero de registro");
            exit(EXIT_FAILURE);
        }

        /* usamos fileno para obtener el descriptor de fichero */
        file_des = fileno(fd);
        if(file_des == -1){
            perror("Obteniendo el descriptor de fichero");
            exit(EXIT_FAILURE);
        }

        /* con el descriptor del fichero podemos escribir el bloque en el fichero */
        dprintf(file_des, "Id:\t\t%d", blck.id_block);
        dprintf(file_des, "Winner:\t\t%d", blck.pid_winner);
        dprintf(file_des, "Target:\t\t%d", blck.target);
        dprintf(file_des, "Solution:\t\t%d", blck.solution);
        dprintf(file_des, "Votes:\t\t%d/%d", blck.positivos, blck.num_votos);
        dprintf(file_des, "Wallets:\t\t");
        for ( i = 0; blck.wallets[i].id != -1; i++){
            dprintf(file_des, "%d:%d\t", blck.wallets[i].id), blck.wallets[i].coins;
        }
        dprintf(file_des, "\n");
        
        
        /* una vez escrito, cerramos tuberias */
        close(miner_reg_fd[0]);

        /* proceso hijo termina con exito */
        exit(EXIT_SUCCESS);

    } else if(registrador > 0) {
        /* solo entra minero */

        /* cerramos parte de lectura en extremo de minero ya que este solo va a escribir datos */
        close(miner_reg_fd[0]);

        if ((fd_mem = shm_open(SHM_NAME, O_RDWR, 0)) == -1) {

            /* somos el primer minero por lo que nos encargaremos de la creacion de la memoria compartida */
            
            if ((fd_mem = shm_open(SHM_NAME, O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1) {
                perror("Creando la memoria compartida del minero");
                exit(EXIT_FAILURE);
            }

            /* asignamos la memoria compartida que vayamos a usar */
            if (ftruncate(fd_mem, sizeof(MemCompartida)) == -1) {
                perror("Asignando tamanio de memoria compartida en minero");
                shm_unlink(SHM_NAME);
                exit(EXIT_FAILURE);
            }

            if (shm_unlink(SHM_NAME) == -1) {
                perror("Eliminando la memoria compartida");
                exit(EXIT_FAILURE);
            }

            /* mapeamos la memoria compartida */
            mem = (MemCompartida *)mmap(NULL, sizeof(MemCompartida), PROT_WRITE | PROT_READ, MAP_SHARED, fd_mem, 0);
            if (mem == MAP_FAILED) {
                perror("Mapeando la memoria compartida del minero");
                close(fd_mem);
                exit(EXIT_FAILURE);
            }
            
            close(fd_mem);

            /* inicializamos los datos */
            sem_wait(mutex);

            mem->nMiners = 1;
            mem->nActMiners = 0;
            /* aqui faltan cosas pero no se muy bien como hacerlo ahora */

            sem_post(mutex);
        } else {
            /* si no fuesemos el primer minero */
        }
        

    } else {
        /* error al crear proceso registrador */
        perror("Haciendo fork para crear registro");
        exit(EXIT_FAILURE);
    }

    /* esperamos al registrador */
    wait(NULL);

    exit(EXIT_SUCCESS);
}