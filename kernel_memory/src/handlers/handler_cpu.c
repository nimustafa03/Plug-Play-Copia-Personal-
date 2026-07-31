#include "handler_cpu.h"
#include "kernel_memory.h" // agrego este include para poder utilizar la funcion y obtener el valor max de seg

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <pthread.h>
#include <stdint.h>
#include "../src/managers/memory_manager.h"
#include "../../utils/src/utils/conexiones.h"
#include "../../utils/src/utils/serializacion.h"
#include "../src/managers/process_manager.h"

extern t_log*logger;
extern t_config*config;
static t_list* lista_cpus_conectadas = NULL;
static pthread_mutex_t mutex_cpus_conectadas = PTHREAD_MUTEX_INITIALIZER;

uint32_t recibir_pid(int fd_cpu){
    int size;
    uint32_t* pid = recibir_mensaje(fd_cpu, &size);
    if (pid == NULL) return 0;
    uint32_t val = *pid;
    free(pid);
    return val;
}

t_contexto *recibir_contexto(int fd_cpu){
    int size;
    void* buffer = recibir_mensaje(fd_cpu, &size);
    if (buffer == NULL) return NULL;
    t_contexto* contexto = deserializar_contexto(buffer, size, logger);
    free(buffer);
    return contexto;
}

void atender_mensaje_cpu(int fd_cpu) {
    while (1) {
        int size = 0;

        log_info(
            logger,
            "Kernel Memory está esperando mensajes de CPU FD %d...",
            fd_cpu
        );

        op_code* codigo = recibir_mensaje(fd_cpu, &size);

        if (codigo == NULL) {
            log_warning(
                logger,
                "La CPU FD %d cerró la conexión",
                fd_cpu
            );
            break;
        }

        if (size != sizeof(op_code)) {
            log_error(
                logger,
                "Código inválido recibido de CPU FD %d: tamaño=%d",
                fd_cpu,
                size
            );
            free(codigo);
            continue;
        }

        switch (*codigo) {
            case MSG_INIT_CPU: {
                log_info(
                    logger,
                    "Se recibió un pedido para iniciar un proceso en CPU FD %d",
                    fd_cpu
                );

                notificar_mapa_memory_sticks_a_cpu(fd_cpu);

                log_info(
                    logger,
                    "Mapa enviado a CPU FD %d. Esperando PID",
                    fd_cpu
                );

                uint32_t pid = recibir_pid(fd_cpu);

                inicializar_proceso(pid, fd_cpu);
                break;
            }

            case MSG_INTERRUPT: {
                log_info(logger, "INTERRUPCION RECIBIDA de CPU FD %d.", fd_cpu);
                uint32_t pid = recibir_pid(fd_cpu);
                t_contexto* contexto = recibir_contexto(fd_cpu);
                if (contexto != NULL) {
                    actualizar_contexto(pid, contexto);
                }
                enviar_confirmacion_a_CPU(fd_cpu, true);
                break;
            }

            default:
                log_warning(
                    logger,
                    "Código desconocido recibido de CPU FD %d: %d",
                    fd_cpu,
                    *codigo
                );
                break;
        }

        free(codigo);
    }
}

void atender_cpu(int fd_cpu){
    pthread_mutex_lock(&mutex_cpus_conectadas);
    if (lista_cpus_conectadas == NULL) {
        lista_cpus_conectadas = list_create();
    }
    int* ptr_fd = malloc(sizeof(int));
    *ptr_fd = fd_cpu;
    list_add(lista_cpus_conectadas, ptr_fd);
    pthread_mutex_unlock(&mutex_cpus_conectadas);

    int size;
    int* ptr_id_cpu = recibir_mensaje(fd_cpu, &size);

    if (ptr_id_cpu != NULL) {
        log_info(
            logger,
            "## CPU %d Conectada en FD %d",
            *ptr_id_cpu,
            fd_cpu
        );
        free(ptr_id_cpu);
    } else {
        log_warning(logger, "## CPU en FD %d se desconectó durante handshake", fd_cpu);
    }

    op_code ok = MSG_OK;
    enviar_mensaje(fd_cpu, &ok, sizeof(op_code));

    uint32_t max_size = get_segment_max_size();
    enviar_mensaje(fd_cpu, &max_size, sizeof(uint32_t));

    log_info(logger, "Atendiendo CPU en FD %d...", fd_cpu);
    atender_mensaje_cpu(fd_cpu);

    pthread_mutex_lock(&mutex_cpus_conectadas);
    for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
        int* elem = list_get(lista_cpus_conectadas, i);
        if (elem && *elem == fd_cpu) {
            list_remove(lista_cpus_conectadas, i);
            free(elem);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_cpus_conectadas);
    return;
}


static uint32_t calcular_tamanio_mapa_memory_sticks(
    uint32_t cantidad
) {
    uint32_t tamanio_total = sizeof(uint32_t);

    for (uint32_t i = 0; i < cantidad; i++) {
        t_info_memory_stick info;

        if (!obtener_info_memory_stick(i, &info)) {
            return 0;
        }

        uint32_t longitud_ip =
            (uint32_t) strlen(info.ip) + 1;

        uint32_t longitud_puerto =
            (uint32_t) strlen(info.puerto) + 1;

        tamanio_total += sizeof(uint32_t);
        tamanio_total += longitud_ip;

        tamanio_total += sizeof(uint32_t);
        tamanio_total += longitud_puerto;

        tamanio_total += sizeof(uint32_t);
        tamanio_total += sizeof(uint32_t);
    }

    return tamanio_total;
}


static void* serializar_mapa_memory_sticks(
    uint32_t* tamanio_buffer
) {
    uint32_t cantidad =
        obtener_cantidad_memory_sticks();

    uint32_t tamanio_total =
        calcular_tamanio_mapa_memory_sticks(cantidad);

    if (tamanio_total == 0) {
        return NULL;
    }

    void* buffer = malloc(tamanio_total);

    if (buffer == NULL) {
        return NULL;
    }

    uint32_t desplazamiento = 0;

    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &cantidad,
        sizeof(uint32_t)
    );

    for (uint32_t i = 0; i < cantidad; i++) {
        t_info_memory_stick info;

        if (!obtener_info_memory_stick(i, &info)) {
            free(buffer);
            return NULL;
        }

        uint32_t longitud_ip =
            (uint32_t) strlen(info.ip) + 1;

        uint32_t longitud_puerto =
            (uint32_t) strlen(info.puerto) + 1;

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            &longitud_ip,
            sizeof(uint32_t)
        );

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            info.ip,
            longitud_ip
        );

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            &longitud_puerto,
            sizeof(uint32_t)
        );

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            info.puerto,
            longitud_puerto
        );

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            &info.base_global,
            sizeof(uint32_t)
        );

        escribir_en_buffer(
            buffer,
            &desplazamiento,
            &info.tamanio,
            sizeof(uint32_t)
        );
    }

    *tamanio_buffer = tamanio_total;
    return buffer;
}



bool notificar_segmentos_a_cpu(int fd_cpu, t_contexto* proceso){
    if (fd_cpu <= 0){
        log_warning(
            logger,
            "No se pudo enviar los segmentos: FD invalido."
        );
        return false;
    }
    op_code codigo;
    int tamanio_buffer = 0;

    if (list_size(proceso->tabla_segmentos) == 0)
    {
        log_info(logger, "La tabla de segmentos se encuentra vacía. Esto es normal si el proceso recien se creo.");
        codigo = MSG_TABLA_SEGMENTOS_VACIA;
        enviar_mensaje(fd_cpu, &codigo, sizeof(op_code));
        return true;
    }

    void* buffer = serializar_segmentos(
        proceso->tabla_segmentos,
        &tamanio_buffer,
        logger
    );
    
    if (buffer == NULL) {
        log_error(
            logger,
            "## ERROR: No se pudo serializar la tabla de segmentos."
        );
        return false;
    }

    codigo = MSG_TABLA_SEGMENTOS_NO_VACIA;
    enviar_mensaje(fd_cpu, &codigo, sizeof(op_code));
    enviar_mensaje(fd_cpu, buffer, (int) tamanio_buffer);
    free(buffer);

    return true;
}

bool notificar_mapa_memory_sticks_a_cpu(int fd_cpu) {
    if (fd_cpu <= 0) {
        log_warning(
            logger,
            "No se pudo enviar el mapa de Memory Sticks: FD invalido"
        );
        return false;
    }

    uint32_t tamanio_buffer = 0;
    void* buffer = serializar_mapa_memory_sticks(&tamanio_buffer);

    if (buffer == NULL) {
        log_error(
            logger,
            "No se pudo serializar el mapa de Memory Sticks"
        );
        return false;
    }

    op_code codigo = MSG_ACTUALIZAR_MEMORY_STICKS;
    enviar_mensaje(fd_cpu, &codigo, sizeof(op_code));
    enviar_mensaje(fd_cpu, buffer, (int) tamanio_buffer);
    free(buffer);

    return true;
}

bool notificar_mapa_memory_sticks_a_todas_las_cpus(void) {
    pthread_mutex_lock(&mutex_cpus_conectadas);
    if (lista_cpus_conectadas == NULL) {
        pthread_mutex_unlock(&mutex_cpus_conectadas);
        return true;
    }
    for (int i = 0; i < list_size(lista_cpus_conectadas); i++) {
        int* fd_ptr = list_get(lista_cpus_conectadas, i);
        if (fd_ptr && *fd_ptr > 0) {
            notificar_mapa_memory_sticks_a_cpu(*fd_ptr);
        }
    }
    pthread_mutex_unlock(&mutex_cpus_conectadas);
    return true;
}

void enviar_contexto_ejecucion_a_cpu(int fd_cpu, t_contexto*contexto){
    int tamanio_buffer;
    void*buffer = serializar_contexto(contexto,&tamanio_buffer,logger);
    if (buffer==NULL)
    {
        log_error(logger, "## ERROR: Ha ocurrido un error al serializar el contexto inicial.");
    }
    enviar_mensaje(fd_cpu, buffer, tamanio_buffer);
    free(buffer);
    return;
}

void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR){
    op_code respuesta = MSG_ERROR;
    if (OKERROR) respuesta = MSG_OK;
    enviar_mensaje(fd_cpu, &respuesta, sizeof(op_code));
}

void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion){
    enviar_mensaje(fd_cpu, proxima_instruccion,strlen(proxima_instruccion)+1);
}

op_code*esperar_pedido_de_instruccion(int fd_cpu){
    int size;
    log_info(logger, "Esperando codigo de cpu FD %d...", fd_cpu);
    op_code*codigo = recibir_mensaje(fd_cpu, &size);
    if (codigo == NULL) return NULL;

    if (*codigo == MSG_FETCH_CPU){
        log_info(logger, "FETCH RECIBIDO.");
        usleep(config_get_int_value(config,"INSTRUCTION_DELAY")*1000);
        return codigo;
    }
    if (*codigo == MSG_INTERRUPT || *codigo == MSG_EXIT_CPU){
        log_info(logger, "INTERRUPCION O EXIT RECIBIDO.");
        uint32_t pid = recibir_pid(fd_cpu);
        t_contexto*contexto = recibir_contexto(fd_cpu);
        if (contexto != NULL) {
            actualizar_contexto(pid, contexto);
        }
        enviar_confirmacion_a_CPU(fd_cpu, true);
        return codigo;
    }
    if (*codigo == MSG_SEG_FAULT){
        log_info(logger, "OCURRIÓ UN SEGMENTATION FAULT. CORTANDO CICLO DE FETCH.");
        return codigo;
    }
    log_info(logger, "NO SE RECIBIÓ FETCH");
    return codigo;
}

uint32_t recibir_pc(int fd_cpu){
    int size;
    uint32_t * pc = recibir_mensaje(fd_cpu, &size);
    if (pc == NULL) return 0;
    uint32_t val = *pc;
    free(pc);
    return val;
}