#include "handler_swap.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"

extern t_log* logger;

static int fd_swap_global = -1;
static int swap_block_size = 0;
static int swap_total_size = 0;
static pthread_mutex_t mutex_swap = PTHREAD_MUTEX_INITIALIZER;

void atender_swap(int fd_swap) {
    int size;
    int* bs = recibir_mensaje(fd_swap, &size);
    int* ts = recibir_mensaje(fd_swap, &size);

    swap_block_size = (bs != NULL) ? *bs : 0;
    swap_total_size = (ts != NULL) ? *ts : 0;
    fd_swap_global  = fd_swap;
    free(bs); free(ts);

    log_info(logger, "## SWAP Conectado - Block size: %d - Tamaño total: %d bytes",
             swap_block_size, swap_total_size);

    op_code ok = MSG_OK;
    enviar_mensaje(fd_swap, &ok, sizeof(op_code));
    // no hace falta loop: SWAP no manda mensajes por su cuenta; KM le pide bloques
    // cuando suspende/des-suspende un proceso usando las funciones de abajo.
}

int swap_get_block_size(void) { return swap_block_size; }
int swap_get_total_size(void) { return swap_total_size; }

bool swap_escribir_bloque(int nro_bloque, void* datos) {
    if (fd_swap_global == -1) return false;
    pthread_mutex_lock(&mutex_swap);
    op_code orden = MSG_SWAP_WRITE;
    enviar_mensaje(fd_swap_global, &orden, sizeof(op_code));
    enviar_mensaje(fd_swap_global, &nro_bloque, sizeof(int));
    enviar_mensaje(fd_swap_global, datos, swap_block_size);

    int size;
    op_code* resp = recibir_mensaje(fd_swap_global, &size);
    bool ok = (resp != NULL && *resp == MSG_OK);
    free(resp);
    pthread_mutex_unlock(&mutex_swap);
    return ok;
}

bool swap_leer_bloque(int nro_bloque, void* buffer_out) {
    if (fd_swap_global == -1) return false;
    pthread_mutex_lock(&mutex_swap);
    op_code orden = MSG_SWAP_READ;
    enviar_mensaje(fd_swap_global, &orden, sizeof(op_code));
    enviar_mensaje(fd_swap_global, &nro_bloque, sizeof(int));

    int size;
    void* datos = recibir_mensaje(fd_swap_global, &size);
    if (datos == NULL) { pthread_mutex_unlock(&mutex_swap); return false; }
    memcpy(buffer_out, datos, swap_block_size);
    free(datos);
    pthread_mutex_unlock(&mutex_swap);
    return true;
}