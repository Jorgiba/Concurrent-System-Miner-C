#ifndef P3_H
#define P3_H

#include <mqueue.h>

#define MQ_NAME "/mq_pro3"
#define SEM_NAME "/sem_pro3"

typedef struct{
    int objetivo;
    int solucion;
    int correcto;       /* 0 = correcto */
    int finalizado;     /* 1 = FIN */
} Mensaje;

struct mq_attr attributes = {
    .mq_flags = 0,
    .mq_maxmsg = 7,
    .mq_curmsgs = 0,
    .mq_msgsize = sizeof(Mensaje)
};


#endif