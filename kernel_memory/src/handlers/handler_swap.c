#include "handler_swap.h"

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/bitarray.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"

extern t_log* logger;


static t_bitarray*bitmap_swap = NULL;
static char*bitarray_buffer = NULL;
static int cantidad_bloques_swap = 0;

static int fd_swap_global = -1;
static int swap_block_size = 0;
static int swap_total_size = 0;
static pthread_mutex_t mutex_swap = PTHREAD_MUTEX_INITIALIZER;



int swap_get_block_size(void) { return swap_block_size; }
int swap_get_total_size(void) { return swap_total_size; }


int encontrar_bloque_libre() {
    for (int i=0;i<cantidad_bloques_swap;i++)
    {
        if (!bitarray_test_bit(bitmap_swap,i))
        {
            bitarray_set_bit(bitmap_swap,i);
            return i;
        }
    }
    log_error(logger, "## ERROR: No se ha encontrado espacio libre en el swap.");
    return -1;
}

void liberar_bloque_swap(int nro_bloque)
{
    if (nro_bloque >= 0 && nro_bloque < cantidad_bloques_swap)
    {
        bitarray_clean_bit(bitmap_swap,nro_bloque);
    }
    if (bitarray_test_bit(bitmap_swap,nro_bloque) == 0)
    {
        log_debug(logger, "Se ha liberado correctamente el bloque.");
        return;
    }
    log_error(logger, "## ERROR: No se ha podido liberar el bloque de swap.");
    return;
}

void inicializar_bitmap_swap(int block_size, int total_size) {
    cantidad_bloques_swap = total_size / block_size;

    int bytes_bitmap = (cantidad_bloques_swap +7)/8;
    bitarray_buffer = calloc(1,bytes_bitmap);

    bitmap_swap = bitarray_create_with_mode(
        bitarray_buffer,
        bytes_bitmap,
        MSB_FIRST  
    );

    log_info(logger, "Bitmap de SWAP creado para %d bloques.", cantidad_bloques_swap);
}

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

    inicializar_bitmap_swap(swap_get_block_size(),swap_get_total_size());

    op_code ok = MSG_OK;
    enviar_mensaje(fd_swap, &ok, sizeof(op_code));
    // no hace falta loop: SWAP no manda mensajes por su cuenta; KM le pide bloques
    // cuando suspende/des-suspende un proceso usando las funciones de abajo.
}


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