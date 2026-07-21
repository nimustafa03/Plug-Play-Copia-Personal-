#include "handler_cpu.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <pthread.h>
#include <stdint.h>
#include "../src/managers/memory_manager.h"
#include "../src/estructuras.h"
#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"
#include "../src/managers/process_manager.h"


extern t_log*logger;
extern t_config*config;
static int socket_cpu = -1;

static pthread_mutex_t mutex_envios_cpu = PTHREAD_MUTEX_INITIALIZER;

uint32_t recibir_pid(){
    int size;
    uint32_t*pid = recibir_mensaje(socket_cpu,&size);
    return *pid;
}

t_contexto *recibir_contexto(){
    int size;
    t_contexto*contexto;
    contexto = recibir_mensaje(socket_cpu, &size);
    return contexto;
}

void atender_cpu(int nuevo_socket_cpu){
    socket_cpu = nuevo_socket_cpu;

    int size;

    int* ptr_id_cpu =
        recibir_mensaje(socket_cpu, &size); //  codigo init

    log_info(
        logger,
        "## CPU %d Conectada",
        *ptr_id_cpu
    );

    op_code ok = MSG_OK;

    enviar_mensaje(
        socket_cpu,
        &ok,
        sizeof(op_code)
    );



    free(ptr_id_cpu);
    // NICO M: Loop de espera activa, hasta que reciba el mensaje de iniciar proceso.
    op_code*codigo;

    /* notificar_mapa_memory_sticks_a_cpu(); */

    while(1){
        codigo = recibir_mensaje(socket_cpu,&size);
        switch(*codigo){
            case MSG_INIT_CPU:
                notificar_mapa_memory_sticks_a_cpu();
                log_info(logger, "Mapa enviado. Esperando PID");
                inicializar_proceso(recibir_pid(), socket_cpu);
                break;
            case MSG_INTERRUPT:
                enviar_confirmacion_a_CPU(socket_cpu,actualizar_contexto(recibir_pid(),recibir_contexto()));
                break;
            default:
                log_warning(logger, "Código desconocido recibido de CPU: %d", *codigo);
                break;
          break;
        }
    }
    free(codigo);
}

static void escribir_en_buffer(
    void* buffer,
    uint32_t* desplazamiento,
    const void* dato,
    uint32_t tamanio
) {
    memcpy(
        (uint8_t*) buffer + *desplazamiento,
        dato,
        tamanio
    );

    *desplazamiento += tamanio;
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

void* serializar_contexto_inicial(
    t_contexto* contexto,
    int* tamanio_buffer
) {
    if(contexto == NULL) {
        log_warning(
            logger,
            "Contexto vacío al momento de serializar contexto inicial."
        );
        return NULL;
    }

    int cantidad_segmentos = 0;

    *tamanio_buffer =
        sizeof(t_registros) + sizeof(int);

    void* buffer = malloc(*tamanio_buffer);

    if (buffer == NULL) {
        return NULL;
    }

    uint32_t desplazamiento = 0;

    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &contexto->registros,
        sizeof(t_registros)
    );
    log_info(logger, "Registros guardados: %d", contexto->registros.pc);

    escribir_en_buffer(
        buffer,
        &desplazamiento,
        &cantidad_segmentos,
        sizeof(int)
    );
    log_info(logger, "Segmentos guardados: %d", cantidad_segmentos);

    return buffer;
}

bool cpu_esta_conectada(void) {
    return socket_cpu != -1;
}

bool notificar_mapa_memory_sticks_a_cpu(void) {
    if (!cpu_esta_conectada()) {
        log_warning(
            logger,
            "No se pudo enviar el mapa de Memory Sticks: CPU no conectada"
        );

        return false;
    }

    uint32_t tamanio_buffer = 0;

    void* buffer =
        serializar_mapa_memory_sticks(
            &tamanio_buffer
        );

    if (buffer == NULL) {
        log_error(
            logger,
            "No se pudo serializar el mapa de Memory Sticks"
        );

        return false;
    }

    op_code codigo = MSG_ACTUALIZAR_MEMORY_STICKS;

    pthread_mutex_lock(&mutex_envios_cpu);

    enviar_mensaje(
        socket_cpu,
        &codigo,
        sizeof(op_code)
    );

    enviar_mensaje(
        socket_cpu,
        buffer,
        (int) tamanio_buffer
    );

    pthread_mutex_unlock(&mutex_envios_cpu);

    free(buffer);

    return true;
}

void enviar_contexto_ejecucion_a_cpu(int fd_cpu, void*contexto, int tamanio_buffer){
    enviar_mensaje(fd_cpu, contexto, tamanio_buffer);
}

void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR){
    op_code respuesta = MSG_ERROR;
    if (OKERROR) respuesta = MSG_OK;
    enviar_mensaje(fd_cpu, &respuesta, sizeof(op_code));
}

void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion){
    enviar_mensaje(fd_cpu, proxima_instruccion,strlen(proxima_instruccion)+1);
}

bool esperar_pedido_de_instruccion(int fd_cpu){
    int size;
    log_info(logger, "Esperando codigo de cpu...");
    op_code*codigo;
    while (1){
        codigo = recibir_mensaje(fd_cpu, &size);
        if (*codigo == MSG_FETCH_CPU){
            log_info(logger, "FETCH RECIBIDO.");
            usleep(config_get_int_value(config,"INSTRUCTION_DELAY")*1000);
            return true;
        }
        log_info(logger, "NO SE RECIBIÓ FETCH");
        return false;
    }
}

uint32_t recibir_pc(int fd_cpu){
    int size;
    uint32_t * pc = recibir_mensaje(fd_cpu, &size);

    return *pc;
}