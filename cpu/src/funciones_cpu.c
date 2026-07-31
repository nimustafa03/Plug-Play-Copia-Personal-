#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <unistd.h>
#include <string.h>
#include "cpu.h"
#include <utils/tipos.h>
#include <sys/socket.h> 
#include <errno.h>

extern bool romper_ciclo;

char* fetch(int fd_km, u_int32_t pid, t_registros* cpu, t_log* logger_cpu){
    // Aviso de fetch
    op_code codigo_fetch = MSG_FETCH_CPU;
    enviar_mensaje(fd_km,&codigo_fetch,sizeof(op_code));
    log_info(
        logger_cpu,
        "FETCH: enviando MSG_FETCH_CPU=%d",
        codigo_fetch
    );
    // Envio PC
    int pc = cpu->pc;
    enviar_mensaje(fd_km, &pc, sizeof(pc));
    log_info(logger_cpu,"FETCH: enviando PC=%d",cpu->pc);
    // KM responde por OK/ERROR
    int size_respuesta;
    op_code* respuesta_km = (op_code*) recibir_mensaje(fd_km,&size_respuesta);
    if (respuesta_km == NULL){
        log_error(logger_cpu, "KM cerró la conexión o no envió respuesta");
        free(respuesta_km);
        return NULL;
    } else if (*respuesta_km == MSG_ERROR){
            log_error(logger_cpu,"FETCH: KM respondio ERROR");
            free(respuesta_km);
            return NULL;
            }
    log_debug(logger_cpu, "llego la respuesta");
    free(respuesta_km);
    
    // Recibe Instruccion
    int size_instruccion;
    char* instruccion = recibir_mensaje(fd_km, &size_instruccion);
    if (instruccion == NULL) {
        log_error(logger_cpu, "Error al recibir instruccion");
        free(instruccion);
        return NULL;
    }
    log_debug(logger_cpu, "llego la instruccion");
    return instruccion;
}

operacion decode(char* instruccion) {
    if (strstr(instruccion, "NOOP") != NULL) return OP_NOOP;
    if (strstr(instruccion, "SET") != NULL) return OP_SET;
    if (strstr(instruccion, "MOV_IN") != NULL) return OP_MOV_IN;
    if (strstr(instruccion, "MOV_OUT") != NULL) return OP_MOV_OUT;
    if (strstr(instruccion, "SUM") != NULL) return OP_SUM;
    if (strstr(instruccion, "SUB") != NULL) return OP_SUB;
    if (strstr(instruccion, "JNZ") != NULL) return OP_JNZ;
    if (strstr(instruccion, "COPY_MEM") != NULL) return OP_COPY_MEM;
    if (strstr(instruccion, "MUTEX_CREATE") != NULL) return OP_MUTEX_CREATE;
    if (strstr(instruccion, "MUTEX_LOCK") != NULL) return OP_MUTEX_LOCK;
    if (strstr(instruccion, "MUTEX_UNLOCK") != NULL) return OP_MUTEX_UNLOCK;
    if (strstr(instruccion, "STDIN") != NULL) return OP_STDIN;
    if (strstr(instruccion, "STDOUT") != NULL) return OP_STDOUT;
    if (strstr(instruccion, "SLEEP") != NULL) return OP_SLEEP;
    if (strstr(instruccion, "INIT_PROC") != NULL) return OP_INIT_PROC;
    if (strstr(instruccion, "MEM_ALLOC") != NULL) return OP_MEM_ALLOC;
    if (strstr(instruccion, "MEM_FREE") != NULL) return OP_MEM_FREE;
    if (strstr(instruccion, "EXIT") != NULL) return OP_EXIT;
    return OP_INVALID;
}

int execute(operacion codigo, char* instruccion, t_registros* registros, int fd_ks, int fd_km, int fd_ms, uint32_t pid, t_list* tabla_segmentos, t_log* logger_cpu, t_mapa_memory_sticks_cpu* mapa, int fd_ms_agregados[3], t_contexto* contexto){
    switch (codigo){
        case OP_SET:
            set(instruccion, registros);
            break;
        case OP_SUM:
            sum(instruccion, registros);
            break;
        case OP_SUB:
            sub(instruccion, registros);
            break;
        case OP_MOV_IN:
            int op_mov_in = mov_in(instruccion,registros,mapa,fd_ms,fd_ms_agregados,tabla_segmentos, logger_cpu, pid);
            if (op_mov_in == SEG_FAULT)
                return SEG_FAULT;
            break;
        case OP_MOV_OUT:
            int op_mov_out = mov_out(instruccion,registros,tabla_segmentos,mapa,fd_ms,fd_ms_agregados, logger_cpu, pid);
            if (op_mov_out == SEG_FAULT)
                return SEG_FAULT;
            break;
        case OP_JNZ:
            jnz(instruccion, registros);
            break;
        case OP_COPY_MEM:
            int op_copy = copy_mem(instruccion,registros,tabla_segmentos,mapa,fd_ms,fd_ms_agregados,pid,logger_cpu);
            if (op_copy == SEG_FAULT)
                return SEG_FAULT;
            break;
        case OP_NOOP:
            noop(registros);
            break;
        // CP3:
        case OP_MUTEX_CREATE:
            int comprobacion_m_create = syscall_mutex_create(instruccion, fd_ks, pid, registros); 
            if (comprobacion_m_create == -1){
                log_error(logger_cpu, "ERROR - MUTEX_CREATE devolvio -1");
                exit(EXIT_FAILURE);
            }
            break;
        case OP_MUTEX_LOCK:
            int comprobacion_m_lock = syscall_mutex_lock(instruccion, fd_ks, fd_km, pid, registros, contexto, logger_cpu);
            if (comprobacion_m_lock == -1){
                log_error(logger_cpu, "ERROR - MUTEX_LOCK devolvio -1");
                exit(EXIT_FAILURE);
            }
            // FIX: 2 = el mutex estaba tomado, hay que cortar el ciclo de instruccion
            if (comprobacion_m_lock == 2)
                return 2;
            break;
        case OP_MUTEX_UNLOCK:
            int comprobacion_m_unlock = syscall_mutex_unlock(instruccion, fd_ks, pid, registros, logger_cpu);
            if (comprobacion_m_unlock == -1){
                log_error(logger_cpu, "ERROR - MUTEX_UNLOCK devolvio -1");
                exit(EXIT_FAILURE);
            }
            break;
        case OP_INIT_PROC:
            syscall_init_proc(instruccion, registros, fd_ks, pid, logger_cpu); 
            break;
        case OP_SLEEP:
            return syscall_sleep(instruccion, fd_ks, fd_km, pid, registros, contexto, logger_cpu); // CP3: 2 = bloqueo IO
        case OP_MEM_ALLOC:
            syscall_mem_alloc(instruccion, registros, fd_ks, pid, logger_cpu);
            break;
        case OP_MEM_FREE:
            syscall_mem_free(instruccion, registros, fd_ks, pid, logger_cpu);
            break;
        case OP_STDIN:
            return syscall_stdin(instruccion, registros, fd_ks, fd_km, pid, contexto, logger_cpu); // CP3: 2 = bloqueo IO
        case OP_STDOUT:
            return syscall_stdout(instruccion, registros, fd_ks, fd_km, pid, contexto, logger_cpu); // CP3: 2 = bloqueo IO
        case OP_EXIT:
            syscall_exit(fd_km, fd_ks, contexto, pid, logger_cpu);
            romper_ciclo = true;
            break;
        case OP_INVALID:
            log_error(logger_cpu, "Operacion invalida: %d", codigo);
            exit(EXIT_FAILURE);
    }
    return 0;
}

int memory_management_unit(uint32_t direccion_logica, uint32_t tamanio_acceso, t_list* tabla_segmentos, t_log* logger_cpu) {
    uint32_t num_segmento = direccion_logica / segment_max_size;
    uint32_t desplazamiento = direccion_logica % segment_max_size;

    t_segmento* segmento = NULL;

    for (int i = 0; i < list_size(tabla_segmentos); i++) {
        t_segmento* seg = list_get(tabla_segmentos, i);

        if (seg->id_segmento == num_segmento) {
            segmento = seg;
            break;
        }
    }
    if (segmento == NULL) {
        log_error(logger_cpu,"Error de segmento en MMU");
        exit(EXIT_FAILURE);
        }

        if (desplazamiento + tamanio_acceso > segmento->tamanio) {  // CONDICION CONCEPTUAL DE SEGMENTATION FAULT
        return SEG_FAULT;
        }

    return segmento->base + desplazamiento;
}

void destruir_mapa_memory_sticks(t_mapa_memory_sticks_cpu* mapa){
    if (mapa == NULL) {
        return;
    }
    for (uint32_t i = 0; i < 4; i++) {
        free(mapa->memory_sticks[i].ip);
        free(mapa->memory_sticks[i].puerto);
    }
    free(mapa);
}

t_mapa_memory_sticks_cpu* recibir_mapa(int fd_km, t_log* logger_cpu) {

    // Recibir opcode
    int tamanio_opcode = 0;
    op_code* codigo = recibir_mensaje(fd_km, &tamanio_opcode);

    if (codigo == NULL) {
        return NULL;
    }

    if (tamanio_opcode != sizeof(op_code)) {
        log_error(logger_cpu,"Tamaño inválido del opcode: recibido=%d esperado=%zu",tamanio_opcode,sizeof(op_code));
        free(codigo);
        return NULL;
    }

    if (*codigo != MSG_ACTUALIZAR_MEMORY_STICKS) {
        log_error(logger_cpu,"Opcode inesperado: recibido=%d",*codigo);
        free(codigo);
        return NULL;
    }

    free(codigo);

    // Recibir buffer serializado
    int tamanio_buffer = 0;
    void* buffer = recibir_mensaje(fd_km, &tamanio_buffer); //  BUFFER

    if (buffer == NULL) {
        log_error(logger_cpu, "No se pudo recibir el buffer del mapa");
        return NULL;
    }

    if (tamanio_buffer < (int)sizeof(uint32_t)) {
        log_error(logger_cpu, "Buffer del mapa demasiado pequeño: %d bytes", tamanio_buffer);
        free(buffer);
        return NULL;
    }

    t_mapa_memory_sticks_cpu* mapa = calloc(1, sizeof(t_mapa_memory_sticks_cpu));

    if (mapa == NULL) {log_error(logger_cpu,"No se pudo reservar memoria para el mapa");
        free(buffer);
        return NULL;
    }

    uint32_t desplazamiento = 0;

    // Cantidad de Memory Sticks
    memcpy(&mapa->cantidad, (char*)buffer + desplazamiento, sizeof(uint32_t));

    desplazamiento += sizeof(uint32_t);

    if (mapa->cantidad == 0 || mapa->cantidad > 4) {
        log_error(logger_cpu,"Cantidad inválida de Memory Sticks: %u",mapa->cantidad);
        free(buffer);
        destruir_mapa_memory_sticks(mapa);
        return NULL;
    }

    log_info(logger_cpu, "Cantidad de Memory Sticks recibida: %u", mapa->cantidad);

    for (uint32_t i = 0; i < mapa->cantidad; i++) {

        uint32_t longitud_ip;
        uint32_t longitud_puerto;

        /* Longitud de IP */
        if (desplazamiento + sizeof(uint32_t) > (uint32_t)tamanio_buffer) {

            log_error(logger_cpu, "Error al reconstruir la longitud de IP del MS %u", i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(&longitud_ip, (char*)buffer + desplazamiento,sizeof(uint32_t));

        desplazamiento += sizeof(uint32_t);

        if (longitud_ip == 0 ||
            desplazamiento + longitud_ip >
            (uint32_t)tamanio_buffer) {

            log_error(logger_cpu, "Error al reconstruir la IP del MS %u", i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        mapa->memory_sticks[i].ip = malloc(longitud_ip);

        if (mapa->memory_sticks[i].ip == NULL) {
            log_error(logger_cpu, "No se pudo reservar memoria para la IP del MS %u", i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(mapa->memory_sticks[i].ip, (char*)buffer + desplazamiento, longitud_ip);

        desplazamiento += longitud_ip;

        // Longitud de puerto
        if (desplazamiento + sizeof(uint32_t) >
            (uint32_t)tamanio_buffer) {

            log_error(logger_cpu, "Error al reconstruir la longitud del puerto del MS %u", i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(&longitud_puerto,(char*)buffer + desplazamiento, sizeof(uint32_t));

        desplazamiento += sizeof(uint32_t);

        if (longitud_puerto == 0 ||
            desplazamiento + longitud_puerto >
            (uint32_t)tamanio_buffer) {

            log_error(logger_cpu,"Error al reconstruir el puerto del MS %u",i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        mapa->memory_sticks[i].puerto = malloc(longitud_puerto);

        if (mapa->memory_sticks[i].puerto == NULL) {
            log_error(logger_cpu, "No se pudo reservar memoria para el puerto del MS %u",i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(mapa->memory_sticks[i].puerto, (char*)buffer + desplazamiento,longitud_puerto);

        desplazamiento += longitud_puerto;

        // Base global
        if (desplazamiento + sizeof(uint32_t) >
            (uint32_t)tamanio_buffer) {

            log_error(logger_cpu,"Error al reconstruir la base global del MS %u",i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(&mapa->memory_sticks[i].base_global,(char*)buffer + desplazamiento,sizeof(uint32_t));

        desplazamiento += sizeof(uint32_t);

        // Tamaño
        if (desplazamiento + sizeof(uint32_t) >
            (uint32_t)tamanio_buffer) {

            log_info(logger_cpu,"Error al reconstruir el tamaño del MS %u",i);
            destruir_mapa_memory_sticks(mapa);
            free(buffer);
            return NULL;
        }

        memcpy(
            &mapa->memory_sticks[i].tamanio,
            (char*)buffer + desplazamiento,
            sizeof(uint32_t)
        );

        desplazamiento += sizeof(uint32_t);

        log_info(logger_cpu,"MS %u recibido: IP=%s Puerto=%s Base=%u Tamaño=%u",i,
            mapa->memory_sticks[i].ip,
            mapa->memory_sticks[i].puerto,
            mapa->memory_sticks[i].base_global,
            mapa->memory_sticks[i].tamanio
        );
    }
    
    free(buffer);
    return mapa;
}


int actualizar_conexiones_ms(t_info_memory_stick_cpu* info_ms,t_log* logger_cpu,int identificador_cpu) {
    if (info_ms == NULL)
        return -1;

    int fd_ms = crear_conexion(info_ms->ip,info_ms->puerto);
    if (fd_ms == -1) {
        log_warning(logger_cpu,"No se pudo conectar al MS %s:%s",info_ms->ip,info_ms->puerto);
        return -1;
    }

    op_code handshake = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_ms, &handshake, sizeof(op_code));
    enviar_mensaje(fd_ms,&identificador_cpu,sizeof(int));

    int size;
    op_code* respuesta = recibir_mensaje(fd_ms, &size);
        if (respuesta == NULL ||size != sizeof(op_code) ||*respuesta != MSG_OK){
        free(respuesta);
        close(fd_ms);
        return -1;
    }
    free(respuesta);

    log_info(logger_cpu,"Nueva conexión con MS %s:%s establecida. FD=%d",info_ms->ip,info_ms->puerto,fd_ms);
    return fd_ms;
}

int conectar_memory_sticks_faltantes(t_mapa_memory_sticks_cpu* mapa,t_log* logger_cpu,int identificador_cpu){
    if (mapa->cantidad <= ms_conectados) 
        return 0;

    for (uint32_t i = ms_conectados; i < mapa->cantidad; i++) {

        t_info_memory_stick_cpu* nuevo_ms =&mapa->memory_sticks[i];

        int nuevo_fd = actualizar_conexiones_ms(nuevo_ms, logger_cpu, identificador_cpu);
        if (nuevo_fd == -1) {
            log_warning(logger_cpu,"No se pudo conectar al MS %u: %s:%s",i,nuevo_ms->ip,nuevo_ms->puerto);
            return -1;
        }
        fd_ms_agregados[i-1] = nuevo_fd;
        ms_conectados++;
        
        log_info(logger_cpu,"Conexión establecida con MS %u: %s:%s | ""Base=%u | Tamaño=%u | Total conectados=%d",
            i,
            nuevo_ms->ip,
            nuevo_ms->puerto,
            nuevo_ms->base_global,
            nuevo_ms->tamanio,
            ms_conectados
        );
    }
    return 0;
}

int buscar_indice_ms(uint32_t direccion_global,t_mapa_memory_sticks_cpu* mapa) {
    for (uint32_t i = 0; i < mapa->cantidad; i++) {
        t_info_memory_stick_cpu* ms =&mapa->memory_sticks[i];

        uint32_t inicio = ms->base_global;
        uint32_t fin = inicio + ms->tamanio;

        if (direccion_global >= inicio &&direccion_global < fin) 
            return (int)i;
    }
    return -1;
}

int obtener_fd_ms(uint32_t indice_ms,int fd_ms,int fd_ms_agregados[3]) {
    if (indice_ms == 0) 
        return fd_ms;

    uint32_t indice_agregado = indice_ms - 1;

    if (indice_agregado >= 3)
        return -1;
    return fd_ms_agregados[indice_agregado];
}

void* lectura_ms(uint32_t direccion_global,uint32_t tamanio_lectura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3], t_log* logger_cpu) {
    if (mapa == NULL ||fd_ms < 0 ||tamanio_lectura == 0 ||mapa->cantidad == 0 ||mapa->cantidad > 4){
        log_error(logger_cpu,"Error de validacion en funcion LECTURA");
        return NULL;
    }
    uint8_t* buffer_resultado = malloc(tamanio_lectura);

    if (buffer_resultado == NULL) {
        log_error(logger_cpu,"Error de LECTURA");
        return NULL;
    }

    uint32_t direccion_actual = direccion_global;
    uint32_t bytes_restantes = tamanio_lectura;
    uint32_t desplazamiento_buffer = 0;

    while (bytes_restantes > 0) {
        int indice_ms = buscar_indice_ms(direccion_actual,mapa);

        if (indice_ms == -1) {
            free(buffer_resultado);
            return NULL;
        }

        t_info_memory_stick_cpu* ms_actual = &mapa->memory_sticks[indice_ms];

        int fd_actual = obtener_fd_ms((uint32_t)indice_ms,fd_ms,fd_ms_agregados);

        if (fd_actual < 0) {
            free(buffer_resultado);
            return NULL;
        }

        uint32_t direccion_local = direccion_actual - ms_actual->base_global;
        uint32_t bytes_disponibles =ms_actual->tamanio - direccion_local;
        uint32_t bytes_a_leer;

        if (bytes_restantes < bytes_disponibles) {
            bytes_a_leer = bytes_restantes;
        } else {
            bytes_a_leer = bytes_disponibles;
        }
        op_code codigo = MSG_READ;
        enviar_mensaje(fd_actual,&codigo,sizeof(op_code));
        enviar_mensaje(fd_actual,&direccion_local,sizeof(uint32_t));
        enviar_mensaje(fd_actual,&bytes_a_leer,sizeof(uint32_t));

        int tamanio_respuesta = 0;

        void* respuesta = recibir_mensaje(fd_actual,&tamanio_respuesta);

        if (respuesta == NULL) {
            free(buffer_resultado);
            log_error(logger_cpu,"Error de respuesta en LECTURA");
            return NULL;
        }

        if (tamanio_respuesta < 0 ||(uint32_t)tamanio_respuesta != bytes_a_leer) {
            log_error(logger_cpu,"Error de tamanio en LECTURA");
            free(respuesta);
            free(buffer_resultado);
            return NULL;
        }

        memcpy(buffer_resultado + desplazamiento_buffer,respuesta,bytes_a_leer);

        free(respuesta);

        direccion_actual += bytes_a_leer;
        desplazamiento_buffer += bytes_a_leer;
        bytes_restantes -= bytes_a_leer;
    }
    return buffer_resultado;
}

void escritura_ms(uint32_t direccion_global,void* buffer_origen,uint32_t tamanio_escritura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3], t_log* logger_cpu) {
    if (buffer_origen == NULL||mapa == NULL||tamanio_escritura == 0||mapa->cantidad == 0||mapa->cantidad > 4) {
        log_error(logger_cpu,"Error de validacion en funcion ESCRITURA");
        exit(EXIT_FAILURE);
    }

    uint32_t direccion_actual = direccion_global;
    uint32_t bytes_restantes = tamanio_escritura;
    uint32_t desplazamiento_buffer = 0;

    uint8_t* buffer = (uint8_t*)buffer_origen;

    while (bytes_restantes > 0) {
        int indice_ms = buscar_indice_ms(direccion_actual,mapa);

        if (indice_ms == -1) {
            log_error(logger_cpu,"Error de indice en ESCRITURA");
            exit(EXIT_FAILURE);
        }
        t_info_memory_stick_cpu* ms_actual = &mapa->memory_sticks[indice_ms];

        int fd_actual = obtener_fd_ms((uint32_t)indice_ms,fd_ms,fd_ms_agregados);
        if (fd_actual < 0){
            log_error(logger_cpu,"Error de dato en ESCRITURA");
            exit(EXIT_FAILURE);
        }
        uint32_t direccion_local = direccion_actual - ms_actual->base_global;
        uint32_t bytes_disponibles = ms_actual->tamanio - direccion_local;
        uint32_t bytes_a_escribir;

        if (bytes_restantes < bytes_disponibles) {
            bytes_a_escribir = bytes_restantes;
        } else {
            bytes_a_escribir = bytes_disponibles;
        }

        op_code codigo = MSG_WRITE;
        enviar_mensaje(fd_actual,&codigo,sizeof(op_code));

        enviar_mensaje(fd_actual,&direccion_local,sizeof(uint32_t));
        enviar_mensaje(fd_actual,&bytes_a_escribir,sizeof(uint32_t));
        enviar_mensaje(fd_actual,buffer + desplazamiento_buffer,bytes_a_escribir);

        int tamanio_respuesta = 0;

        op_code* respuesta = recibir_mensaje(fd_actual,&tamanio_respuesta);
        if (respuesta == NULL){
            log_error(logger_cpu,"MEMORIA CORRUPTA");
        }
        if (tamanio_respuesta != sizeof(op_code) ||*respuesta != MSG_DONE) {
            log_error(logger_cpu,"Error de tamanio en ESCRITURA");
            free(respuesta);
            exit(EXIT_FAILURE);
        }

        free(respuesta);
        direccion_actual += bytes_a_escribir;
        desplazamiento_buffer += bytes_a_escribir;
        bytes_restantes -= bytes_a_escribir;
    }
    return;
}

void guardar_contexto_km(int fd_km, t_contexto* contexto, uint32_t pid, t_log* logger_cpu) {

    op_code cod = MSG_INTERRUPT;
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));

    int size;
    void* buffer = serializar_contexto(contexto, &size, logger_cpu);
    if (buffer == NULL) {
        log_error(logger_cpu, "Error al serializar contexto");
        exit(EXIT_FAILURE);
    }

    enviar_mensaje(fd_km, buffer, size);
    free(buffer);

    op_code* ok = recibir_mensaje(fd_km, &size);
    if (ok == NULL) {
        log_error(logger_cpu, "Error al recibir respuesta");
        exit(EXIT_FAILURE);
    }
    if (*ok != MSG_OK) {
        log_error(logger_cpu, "No se recibio respuesta esperada. %d", *ok);
        exit(EXIT_FAILURE);
    }
    free(ok);
}

void actualizar_tabla_segmentos(t_contexto* contexto,int fd_km, t_log* logger_cpu){
    int size_respuesta;
    op_code* mensaje_km = recibir_mensaje(fd_km,&size_respuesta);
    if (*mensaje_km == MSG_TABLA_SEGMENTOS_NO_VACIA){
        int tamanio_buffer = 0;

        log_debug(logger_cpu,"## se recibira buffer.");
        void* buffer = recibir_mensaje(fd_km,&tamanio_buffer);
        log_debug(logger_cpu,"## se recibio buffer");

        if (buffer == NULL) {
            log_error(logger_cpu,"## ERROR: No se pudo recibir el buffer de segmentos.");
            free(mensaje_km);
            return;
        }

        t_list* tabla_actualizada = deserializar_tabla_segmentos(buffer,tamanio_buffer);

        free(buffer);

        if (tabla_actualizada == NULL) {
            log_error(logger_cpu,"## ERROR: No se pudo deserializar la tabla de segmentos.");
            return;
        }

        /*
        * Se elimina la tabla anterior junto con
        * los segmentos que contenía.
        */
        if (contexto->tabla_segmentos != NULL) {
            list_destroy_and_destroy_elements(contexto->tabla_segmentos,free);
        }

        contexto->tabla_segmentos = tabla_actualizada;

        log_info(logger_cpu,"Tabla de segmentos actualizada. Cantidad: %d",list_size(contexto->tabla_segmentos));
        for (int i = 0; i < list_size(contexto->tabla_segmentos); i++) {

        t_segmento* segmento =list_get(contexto->tabla_segmentos, i);

        log_info(logger_cpu,
            "Segmento[%d] -> ID=%u BASE=%u TAMANIO=%u",
            i,
            segmento->id_segmento,
            segmento->base,
            segmento->tamanio);
        }
    } else if (*mensaje_km == MSG_TABLA_SEGMENTOS_VACIA){
        free(mensaje_km);
        return;
    } else {log_error(logger_cpu, "No llego el mensaje esperado al consultar por la tabla de segmentos. %d", *mensaje_km);
        free(mensaje_km);
        return;
        }
}
    

t_list* deserializar_tabla_segmentos(void* buffer, int tamanio_buffer){
    if (buffer == NULL)
        return NULL;

    if (tamanio_buffer % sizeof(t_segmento) != 0) 
        return NULL;
    
    int cantidad_segmentos = tamanio_buffer / sizeof(t_segmento);

    t_list* segmentos = list_create();
    if (segmentos == NULL)
        return NULL;

    uint32_t desplazamiento = 0;

    for (int i = 0; i < cantidad_segmentos; i++) {

        t_segmento* segmento = malloc(sizeof(t_segmento));

        if (segmento == NULL){
            list_destroy_and_destroy_elements(segmentos, free);
            return NULL;
        }
        memcpy(
            segmento,
            (char*)buffer + desplazamiento,
            sizeof(t_segmento)
        );

        desplazamiento += sizeof(t_segmento);

        list_add(segmentos, segmento);
    }
    return segmentos;
}

void manejar_seg_fault(int fd_ks, uint32_t pid, t_log* logger_cpu, int fd_km) {
    op_code seg_fault = MSG_SEG_FAULT;
    enviar_mensaje(fd_ks, &seg_fault, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    // ESPERAR CONFIRMACIÓN DEL SCHEDULER:
    // Garantiza que KS ya pasó el PID a EXIT y que la red quedó limpia.
    int size;
    op_code* ack = recibir_mensaje(fd_ks, &size);
    if (ack != NULL) {
        free(ack);
    }
    
    log_info(logger_cpu, "## PID: %u - Notificado SEG_FAULT a KS y confirmado", pid);
    
    enviar_mensaje(fd_km,&seg_fault, sizeof(uint32_t));
}

int hay_mensaje_completo(int fd, t_log* logger_cpu){
    int size = 0;

    /*
     * Primer PEEK: verificar el encabezado.
     */
    ssize_t bytes = recv(fd,&size,sizeof(int),MSG_PEEK | MSG_DONTWAIT);

    if (bytes == -1) {if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;
        log_error(logger_cpu, "error bytes");
        return -1;
    }
    if (bytes == 0){
        log_error(logger_cpu, "error ==0");
        return -1;}
    if (bytes < (ssize_t)sizeof(int))
        return 0;
    if (size <= 0){
        log_error(logger_cpu, "error tamanio");
        return -1;
    }
    /*
     * Segundo PEEK: verificar encabezado más payload.
     */
    size_t tamanio_total = sizeof(int) + (size_t)size;

    void* buffer = malloc(tamanio_total);

    if (buffer == NULL){
        log_error(logger_cpu, "buffer null");
        return -1;}

    bytes = recv(fd,buffer,tamanio_total,MSG_PEEK | MSG_DONTWAIT);

    free(buffer);

    if (bytes == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK){
            return 0;
        log_error(logger_cpu, "error segundo bytes");
        return -1;}
    }

    if ((size_t)bytes < tamanio_total)
        return 0;

    return 1;
}

int atender_interrupcion(int fd_ks, int fd_km, t_contexto* contexto,uint32_t pid,t_log* logger_cpu){
    int estado = hay_mensaje_completo(fd_ks, logger_cpu);

    if (estado == 0) {
        // No hay interrupción pendiente.
        return 1;
    }

    if (estado == -1) {
        log_error(logger_cpu,"Error al revisar el socket de Kernel Scheduler");
        return -1;
    }

    /*
     * Primer mensaje: MSG_INTERRUPT
     */
    int size = 0;

    op_code* codigo = recibir_mensaje(fd_ks, &size);

    if (codigo == NULL) {
        log_error(logger_cpu,"No se pudo recibir el código de interrupción");
        return -1;
    }

    if (size != sizeof(op_code)) {
        log_error(logger_cpu,"Tamaño inválido para el código de interrupción. ""Esperado: %zu - Recibido: %d",sizeof(op_code),size);

        free(codigo);
        return -1;
    }

    if (*codigo != MSG_INTERRUPT) {
        log_error(logger_cpu,"En seccion Interrupciones, se recibió un mensaje inesperado desde KS: %d",*codigo);

        free(codigo);
        return -1;
    }
    log_info(logger_cpu,"## Interrupcion recibida");
    free(codigo);

    /*
     * Segundo mensaje: t_interrupcion.
     */
    t_interrupcion* interrupcion =recibir_mensaje(fd_ks, &size);

    if (interrupcion == NULL) {
        log_error(logger_cpu,"No se pudo recibir la estructura de interrupción");
        return -1;
    }

    if (size != sizeof(t_interrupcion)) {
        log_error(logger_cpu,"Tamaño inválido para t_interrupcion. ""Esperado: %zu - Recibido: %d",sizeof(t_interrupcion),size);

        free(interrupcion);
        return -1;
    }

    uint32_t pid_int = interrupcion->pid;
    int motivo = interrupcion->motivo;

    free(interrupcion);

    if (pid_int != pid) {
        log_warning(logger_cpu,
            "Se descarto interrupcion por no corresponder "
            "al proceso actual. PID INTERRUPCION: %u, "
            "PID ACTUAL: %u",
            pid_int,
            pid
        );

        return 1;
    }

    log_info(logger_cpu,"Se recibio una interrupcion para el PID %u. Motivo: %d",pid_int,motivo);

    guardar_contexto_km(fd_km, contexto, pid, logger_cpu);

    // avisar al KS que se interrumpió
    
    op_code atendido = MSG_INTERRUPCION_ATENDIDA;
    enviar_mensaje(fd_ks, &atendido, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &motivo, sizeof(int));

    return 0;
}

void atender_interrupcion_en_espera (t_interrupcion* interrupcion_entrante, uint32_t pid,int fd_ks,int fd_km,t_contexto* contexto,t_log* logger_cpu){

    uint32_t pid_int = interrupcion_entrante->pid;
    int motivo = interrupcion_entrante->motivo;


    if (pid_int != pid) {
        log_warning(logger_cpu,
            "Se descarto interrupcion por no corresponder "
            "al proceso actual. PID INTERRUPCION: %u, "
            "PID ACTUAL: %u",
            pid_int,
            pid
        );
        return;
    }

    log_info(logger_cpu,"Se recibio una interrupcion para el PID %u. Motivo: %d",pid_int,motivo);

    guardar_contexto_km(fd_km, contexto, pid, logger_cpu);

    // avisar al KS que se interrumpió
    
    op_code atendido = MSG_INTERRUPCION_ATENDIDA;
    enviar_mensaje(fd_ks, &atendido, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &motivo, sizeof(int));
}

void destruir_contexto(t_contexto* contexto) {
    if (contexto == NULL) {
        return;
    }
    
    // Destruye la lista commons y aplica free a cada t_segmento
    if (contexto->tabla_segmentos != NULL) {
        list_destroy_and_destroy_elements(contexto->tabla_segmentos, free);
    }
    
    free(contexto);
}