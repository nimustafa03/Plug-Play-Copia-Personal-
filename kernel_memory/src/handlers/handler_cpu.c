#include "handler_cpu.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <pthread.h>
#include <stdint.h>
#include "../src/managers/memory_manager.h"
#include "../../utils/src/utils/conexiones.h"
#include "../src/managers/process_manager.h"

extern t_log*logger;
extern t_config*config;
static int socket_cpu = -1;

static pthread_mutex_t mutex_envios_cpu = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_recibir_procesos = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condicion_recibir_proceso = PTHREAD_COND_INITIALIZER;
bool listo_para_recibir = false;

uint32_t recibir_pid(){
    int size;
    uint32_t*pid = recibir_mensaje(socket_cpu,&size);
    return *pid;
}

t_contexto* deserializar_contexto_inicial(void* buffer,int tamanio_buffer, t_log* logger_km) {
    int tamanio_esperado =
        sizeof(t_registros) +
        sizeof(int);
    
    log_info(logger_km,"Tamanio esperado: %d", tamanio_esperado);
    

    if (buffer == NULL) {
        log_info(logger_km, "Buffer recibido NULL");
        return NULL;
    }
    if (tamanio_buffer != tamanio_esperado){
        log_info(logger_km, "Problemas con el  tamanio");
        return NULL;
    }
    
    t_contexto* contexto = malloc(sizeof(t_contexto));
    if (contexto == NULL) {
        log_info(logger_km, "No se pudo asignar memoria");
        return NULL;
    }
    
    int desplazamiento = 0;

    log_info(logger_km, "Se copiara contexto...");

    memcpy(
        &contexto->registros,
        (char*) buffer + desplazamiento,
        sizeof(t_registros)
    );

    /*printf("AX %d",contexto->registros.ax);
    printf("BX %d",contexto->registros.bx);
    printf("CX %d",contexto->registros.cx);
    printf("DI %d",contexto->registros.di);
    printf("DX %d",contexto->registros.dx);
    printf("EAX %d",contexto->registros.eax);
    printf("EBX %d",contexto->registros.ebx);
    printf("ECX %d",contexto->registros.ecx);
    printf("EDX %d",contexto->registros.edx);
    printf("PC %d",contexto->registros.pc);
    printf("SI %d",contexto->registros.si);*/
    

    desplazamiento += sizeof(t_registros);
    int cantidad_segmentos;
    log_info(logger_km,"Valos desplazamiento: %d", desplazamiento);

    memcpy(
        &cantidad_segmentos,
        (char*) buffer + desplazamiento,
        sizeof(int)
    );
    log_info(logger_km,"ok");

    contexto->tabla_segmentos = list_create();
    if (contexto->tabla_segmentos == NULL) {
        free(contexto);
        log_info(logger_km, "Error al generar la lista de segmentos en el contexto");
        return NULL;
    }

    contexto->proximo_a_detener = false;

    log_info(logger_km,"Contexto recibido: PC=%u - Segmentos=%d - Próximo a detener=%d", contexto->registros.pc, list_size(contexto->tabla_segmentos),contexto->proximo_a_detener);

    free(buffer);
    return contexto;
}

t_contexto *recibir_contexto(){
    int size;
    t_contexto*contexto;
    void*buffer = recibir_mensaje(socket_cpu, &size);
    contexto = deserializar_contexto_inicial(buffer, size,logger);
    return contexto;
}

void atender_mensaje_cpu(){
    log_info(logger, "Kernel Memory está esperando nuevos procesos...");
    int size;
    op_code*codigo;
    while(1){
        codigo = recibir_mensaje(socket_cpu,&size);
        if (*codigo == MSG_INIT_CPU){
            log_info(logger, "Se ha recibido un nuevo pedido de iniciar proceso.");
            notificar_mapa_memory_sticks_a_cpu();
            log_info(logger, "Mapa enviado. Esperando PID");
            inicializar_proceso(recibir_pid(), socket_cpu);
            free(codigo);
            
            pthread_mutex_lock(&mutex_recibir_procesos);
            listo_para_recibir = false;
            while(!listo_para_recibir){
                log_info(logger, "Bloqueando recepcion de mensajes...");
                pthread_cond_wait(&condicion_recibir_proceso, &mutex_recibir_procesos);
            }
            pthread_mutex_unlock(&mutex_recibir_procesos);
        }
    }
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

    /* notificar_mapa_memory_sticks_a_cpu(); */
    log_info(logger, "Atendiendo CPU...");
    atender_mensaje_cpu();
    
    return;
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

op_code*esperar_pedido_de_instruccion(int fd_cpu){
    int size;
    log_info(logger, "Esperando codigo de cpu...");
    op_code*codigo = recibir_mensaje(fd_cpu, &size);
    if (*codigo == MSG_FETCH_CPU){
        log_info(logger, "FETCH RECIBIDO.");
        usleep(config_get_int_value(config,"INSTRUCTION_DELAY")*1000);
        return codigo;
    }
    if (*codigo == MSG_INTERRUPT || *codigo == MSG_EXIT_CPU){
        log_info(logger, "INTERRUPCION O EXIT RECIBIDO.");
        uint32_t pid = recibir_pid();
        t_contexto*contexto = recibir_contexto();
        actualizar_contexto(pid, contexto);
        enviar_confirmacion_a_CPU(socket_cpu,true);
        return codigo;
    }
    log_info(logger, "NO SE RECIBIÓ FETCH");
    return codigo;
}

uint32_t recibir_pc(int fd_cpu){
    int size;
    uint32_t * pc = recibir_mensaje(fd_cpu, &size);
    return *pc;
}