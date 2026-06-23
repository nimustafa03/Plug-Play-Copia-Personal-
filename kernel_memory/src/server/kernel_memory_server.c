#include "kernel_memory_server.h"

#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>

#include <utils/conexiones.h>
#include <utils/mensajes.h>

#include "../handlers/handler_kernel_scheduler.h"
#include "../handlers/handler_cpu.h"
#include "../handlers/handler_memory_stick.h"
#include "../handlers/handler_swap.h"

extern t_log* logger;

static void* atender_cliente_km(void* arg)
{
    int fd_cliente = *((int*) arg);
    free(arg);

    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    if (codigo == NULL || size != sizeof(op_code)) {
        log_warning(logger, "Handshake inválido - FD: %d", fd_cliente);
        free(codigo);
        return NULL;
    }

    switch (*codigo) {
        case MSG_HANDSHAKE_KS:
            atender_kernel_scheduler(fd_cliente);
            break;

        case MSG_HANDSHAKE_CPU:
            atender_cpu(fd_cliente);
            break;

        case MSG_HANDSHAKE_MS:
            atender_memory_stick(fd_cliente);
            break;

        case MSG_HANDSHAKE_SWAP:
            atender_swap(fd_cliente);
            break;

        default:
            log_warning(logger, "Conexion desconocida - codigo: %d - FD: %d",
                        *codigo, fd_cliente);
            break;
    }

    free(codigo);
    return NULL;
}

void iniciar_servidor_kernel_memory(char* puerto)
{
    int fd_servidor = iniciar_servidor(puerto);

    log_info(logger, "Kernel Memory listo en puerto %s. Esperando conexiones...", puerto);

    while (1) {
        int* fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente_km, fd_cliente);
        pthread_detach(hilo);
    }
}