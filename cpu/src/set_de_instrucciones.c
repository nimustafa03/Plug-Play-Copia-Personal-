#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <unistd.h>
#include <string.h>
#include <cpu.h>

uint32_t obtener_valor(char* posicion, t_registros* registro) {
    if (strcmp(posicion, "AX") == 0) return registro->ax;
    if (strcmp(posicion, "BX") == 0) return registro->bx;
    if (strcmp(posicion, "CX") == 0) return registro->cx;
    if (strcmp(posicion, "DX") == 0) return registro->dx;
    if (strcmp(posicion, "EAX") == 0) return registro->eax;
    if (strcmp(posicion, "EBX") == 0) return registro->ebx;
    if (strcmp(posicion, "ECX") == 0) return registro->ecx;
    if (strcmp(posicion, "EDX") == 0) return registro->edx;
    if (strcmp(posicion, "SI") == 0) return registro->si;
    if (strcmp(posicion, "DI") == 0) return registro->di;
    return 0;
}

void escribir_registro(char* posicion, t_registros* registro, uint32_t valor) {
    if (strcmp(posicion, "AX") == 0) registro->ax = (uint8_t)valor;
    else if (strcmp(posicion, "BX") == 0) registro->bx = (uint8_t)valor;
    else if (strcmp(posicion, "CX") == 0) registro->cx = (uint8_t)valor;
    else if (strcmp(posicion, "DX") == 0) registro->dx = (uint8_t)valor;
    else if (strcmp(posicion, "EAX") == 0) registro->eax = valor;
    else if (strcmp(posicion, "EBX") == 0) registro->ebx = valor;
    else if (strcmp(posicion, "ECX") == 0) registro->ecx = valor;
    else if (strcmp(posicion, "EDX") == 0) registro->edx = valor;
    else if (strcmp(posicion, "SI") == 0) registro->si = valor;
    else if (strcmp(posicion, "DI") == 0) registro->di = valor;
}

uint32_t tamanio_registro(char* nombre_registro) {
    if (strcmp(nombre_registro, "AX") == 0) return sizeof(uint8_t);
    if (strcmp(nombre_registro, "BX") == 0) return sizeof(uint8_t);
    if (strcmp(nombre_registro, "CX") == 0) return sizeof(uint8_t);
    if (strcmp(nombre_registro, "DX") == 0) return sizeof(uint8_t);

    if (strcmp(nombre_registro, "EAX") == 0) return sizeof(uint32_t);
    if (strcmp(nombre_registro, "EBX") == 0) return sizeof(uint32_t);
    if (strcmp(nombre_registro, "ECX") == 0) return sizeof(uint32_t);
    if (strcmp(nombre_registro, "EDX") == 0) return sizeof(uint32_t);
    if (strcmp(nombre_registro, "SI") == 0) return sizeof(uint32_t);
    if (strcmp(nombre_registro, "DI") == 0) return sizeof(uint32_t);
    return 0;
}


/*------------------------ INSTRUCCIONES ------------------------*/

// SUMA
void sum(char* instruccion, t_registros* registro) {
    char posicion_destino[32];
    char posicion_origen[32];
    sscanf(instruccion, "%*s %s %s", posicion_destino, posicion_origen);

    uint32_t valor_destino = obtener_valor(posicion_destino, registro);
    uint32_t valor_origen = obtener_valor(posicion_origen, registro);
    uint32_t resultado = valor_destino + valor_origen;
    escribir_registro(posicion_destino, registro, resultado);

    registro->pc++;
}

// SUB
void sub(char* instruccion, t_registros* registro) {
    char posicion_destino[32];
    char posicion_origen[32];
    sscanf(instruccion, "%*s %s %s", posicion_destino, posicion_origen);

    uint32_t valor_destino = obtener_valor(posicion_destino, registro);
    uint32_t valor_origen = obtener_valor(posicion_origen, registro);
    uint32_t resultado = valor_destino - valor_origen;
    escribir_registro(posicion_destino, registro, resultado);

    registro->pc++;
}

// SET
void set(char* instruccion, t_registros* registro) {
    char posicion_destino[32];
    char valor_str[32];
    sscanf(instruccion, "%*s %s %s", posicion_destino, valor_str);

    uint32_t valor = atoi(valor_str);
    escribir_registro(posicion_destino, registro, valor);

    registro->pc++;
}

// JNZ
void jnz(char* instruccion, t_registros* registro) {
    char registro_destino[32];
    char pc_ptr[32];
    sscanf(instruccion, "%*s %31s %31s", registro_destino, pc_ptr);

    uint32_t valor = obtener_valor(registro_destino, registro);
    uint32_t nuevo_pc = (uint32_t) strtol(pc_ptr, NULL, 10);

    if (valor != 0) {
        registro->pc = nuevo_pc;
    } else {
        registro->pc++;
    }
}

// NOOP
void noop(t_registros* registros) {
    registros->pc++;
}

// INIT_PROC
void syscall_init_proc(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid, t_log* logger_cpu) {
    char archivo[256];
    int prioridad;
    sscanf(instruccion, "%*s %255s %d", archivo, &prioridad);

    op_code codigo = MSG_INIT_PROC;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &prioridad, sizeof(int));
    enviar_mensaje(fd_ks, archivo, strlen(archivo) + 1);

    int size;
    op_code* respuesta = recibir_mensaje(fd_ks, &size);

    if (respuesta == NULL) {
        log_error(logger_cpu, "Error al recibir respuesta en INIT PROC");
        exit(EXIT_FAILURE);
    }

    if (*respuesta == MSG_INTERRUPT) {
        log_info(logger_cpu,"## Interrupcion recibida");
        int size_interrupcion = 0;
        interrupcion_entrante = recibir_mensaje(fd_ks, &size_interrupcion);

        if (interrupcion_entrante == NULL) {
            log_error(logger_cpu,"Se recibió MSG_INTERRUPT pero no llegó la estructura t_interrupt");
            exit(EXIT_FAILURE);
        }
        if (size_interrupcion != sizeof(t_interrupcion)) {
            log_error(logger_cpu,"Tamaño inválido para t_interrupt. Recibido: %d - Esperado: %zu",size_interrupcion,sizeof(t_interrupcion));
            free(interrupcion_entrante);
            exit(EXIT_FAILURE);
        }

        log_warning(logger_cpu,"===== INTERRUPCIÓN RECIBIDA DURANTE INIT_PROC =====");
        log_warning(logger_cpu,"PID de la interrupción: %u",interrupcion_entrante->pid);
        log_warning(logger_cpu,"Motivo de la interrupción: %d",interrupcion_entrante->motivo);
        log_warning(logger_cpu,"===============================================");
        interrupcion_en_espera = true;

        op_code* nueva_respuesta = recibir_mensaje(fd_ks, &size);
        if (*nueva_respuesta == MSG_OK) {
            registros->pc++;
        } else {
            log_warning(logger_cpu, "INIT_PROC recibio error. Hubo deficit de memoria");
        }
    free(nueva_respuesta);
    free(respuesta);
    return;
    }
    if (*respuesta == MSG_OK) {
        registros->pc++;
    } else {
        log_warning(logger_cpu, "INIT_PROC recibio error. Hubo deficit de memoria");
    }
    free(respuesta);
}

// MOV_IN
int mov_in(char* instruccion, t_registros* registros, t_mapa_memory_sticks_cpu* mapa, int fd_ms, int fd_ms_agregados[3], t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid) {
    char registro_destino[8];

    sscanf(instruccion, "%*s %7s", registro_destino);

    uint32_t direccion_logica = registros->si;
    uint32_t tamanio = tamanio_registro(registro_destino);

    if (tamanio == 0) {
        log_error(logger_cpu, "Error de tamanio en MOV IN");
        exit(EXIT_FAILURE);
    }
    int direccion_fisica = memory_management_unit(direccion_logica, tamanio, tabla_segmentos, logger_cpu);
    if (direccion_fisica == SEG_FAULT)
        return SEG_FAULT;

    void* datos = lectura_ms(direccion_fisica, tamanio, mapa, fd_ms, fd_ms_agregados, logger_cpu);
    if (datos == NULL) {
        log_error(logger_cpu, "LECTURA devolvio NULL");
        exit(EXIT_FAILURE);
    }
    uint32_t valor = 0;

    memcpy(&valor, datos, tamanio);
    free(datos);

    log_info(logger_cpu, "## PID: %u - LEER - Direccion Fisica: %d - Valor: %u", pid, direccion_fisica, valor);

    escribir_registro(registro_destino, registros, valor);
    registros->pc++;
    return 0;
}

// MOV_OUT
int mov_out(char* instruccion, t_registros* registros, t_list* tabla_segmentos, t_mapa_memory_sticks_cpu* mapa, int fd_ms, int fd_ms_agregados[3], t_log* logger_cpu, uint32_t pid) {
    char registro_origen[8];

    sscanf(instruccion, "%*s %7s", registro_origen);

    uint32_t direccion_logica = registros->di;
    uint32_t tamanio = tamanio_registro(registro_origen);

    if (tamanio == 0) {
        log_error(logger_cpu, "Error de tamanio en MOV OUT");
        exit(EXIT_FAILURE);
    }
    int direccion_fisica = memory_management_unit(direccion_logica, tamanio, tabla_segmentos, logger_cpu);
    if (direccion_fisica == SEG_FAULT)
        return SEG_FAULT;

    uint32_t valor = obtener_valor(registro_origen, registros);

    escritura_ms((uint32_t)direccion_fisica, &valor, tamanio, mapa, fd_ms, fd_ms_agregados, logger_cpu);

    log_info(logger_cpu, "## PID: %u - ESCRIBIR - Direccion Fisica: %d - Valor: %u", pid, direccion_fisica, valor);

    registros->pc++;
    return 0;
}

// COPY_MEM
int copy_mem(char* instruccion, t_registros* registros, t_list* tabla_segmentos, t_mapa_memory_sticks_cpu* mapa_ms, int fd_ms, int fd_ms_agregados[3], uint32_t pid, t_log* logger_cpu) {
    char registro_tamanio[8];

    sscanf(instruccion, "%*s %7s", registro_tamanio);

    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    uint32_t direccion_logica_origen = registros->si;
    uint32_t direccion_logica_destino = registros->di;

    int direccion_fisica_origen = memory_management_unit(direccion_logica_origen, tamanio, tabla_segmentos, logger_cpu);
    if (direccion_fisica_origen == SEG_FAULT)
        return SEG_FAULT;
        
    int direccion_fisica_destino = memory_management_unit(direccion_logica_destino, tamanio, tabla_segmentos, logger_cpu);
    if (direccion_fisica_destino == SEG_FAULT)
        return SEG_FAULT;

    void* buffer = lectura_ms(direccion_fisica_origen, tamanio, mapa_ms, fd_ms, fd_ms_agregados, logger_cpu);
    if (buffer == NULL) {
        log_error(logger_cpu, "LECTURA devolvio NULL");
        exit(EXIT_FAILURE);
    }
    uint8_t valor_leido = *(uint8_t*)buffer;
    log_info(logger_cpu, "## PID: %u - LEER - Direccion Fisica: %d - Valor: %u", pid, direccion_fisica_origen, valor_leido);

    escritura_ms(direccion_fisica_destino, buffer, tamanio, mapa_ms, fd_ms, fd_ms_agregados, logger_cpu);

    log_info(logger_cpu, "## PID: %u - ESCRIBIR - Direccion Fisica: %d - Valor: %u", pid, direccion_fisica_destino, valor_leido);

    free(buffer);
    registros->pc++;

    log_info(logger_cpu, "## PID: %u - Ejecutando: COPY_MEM - %s", pid, registro_tamanio);
    return 0;
}

// STDIN
int syscall_stdin(char* instruccion, t_registros* registros, int fd_ks, int fd_km, uint32_t pid, t_contexto* contexto, t_log* logger_cpu) {
    char registro_direccion[32];
    char registro_tamanio[32];
    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    registros->pc++; 
    guardar_contexto_km(fd_km, contexto, pid, logger_cpu);

    op_code codigo = MSG_STDIN;
    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    return 2; // bloqueado por IO -> la CPU se libera
}

// STDOUT
int syscall_stdout(char* instruccion, t_registros* registros, int fd_ks, int fd_km, uint32_t pid, t_contexto* contexto, t_log* logger_cpu) {
    char registro_direccion[32];
    char registro_tamanio[32];
    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    registros->pc++;
    guardar_contexto_km(fd_km, contexto, pid, logger_cpu);

    op_code codigo = MSG_STDOUT;
    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    return 2; // bloqueado por IO -> la CPU se libera
}

// MEM_ALLOC
void syscall_mem_alloc(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid, t_log* logger_cpu) {
    char id_segmento_str[32];
    char tamanio_str[32];

    sscanf(instruccion, "%*s %31s %31s", id_segmento_str, tamanio_str);

    op_code codigo = MSG_MEM_ALLOC;
    uint32_t id_segmento = (uint32_t) atoi(id_segmento_str);
    uint32_t tamanio = (uint32_t) atoi(tamanio_str);

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &id_segmento, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    int size;
    op_code* respuesta = recibir_mensaje(fd_ks, &size);

    if (respuesta == NULL) {
        log_error(logger_cpu, "En MEM ALLOC se recibio NULL");
        exit(EXIT_FAILURE);
    }
    if (*respuesta == MSG_INTERRUPT) {
        log_info(logger_cpu,"## Interrupcion recibida");
        int size_interrupcion = 0;
        interrupcion_entrante = recibir_mensaje(fd_ks, &size_interrupcion);

        if (interrupcion_entrante == NULL) {
            log_error(logger_cpu,"Se recibió MSG_INTERRUPT pero no llegó la estructura t_interrupt");
            exit(EXIT_FAILURE);
        }
        if (size_interrupcion != sizeof(t_interrupcion)) {
            log_error(logger_cpu,"Tamaño inválido para t_interrupt. Recibido: %d - Esperado: %zu",size_interrupcion,sizeof(t_interrupcion));
            free(interrupcion_entrante);
            exit(EXIT_FAILURE);
        }

        log_warning(logger_cpu,"===== INTERRUPCIÓN RECIBIDA DURANTE MEM_ALLOC =====");
        log_warning(logger_cpu,"PID de la interrupción: %u",interrupcion_entrante->pid);
        log_warning(logger_cpu,"Motivo de la interrupción: %d",interrupcion_entrante->motivo);
        log_warning(logger_cpu,"===============================================");
        interrupcion_en_espera = true;

        op_code* nueva_respuesta = recibir_mensaje(fd_ks, &size);
        if (*nueva_respuesta == MSG_OK) {
            registros->pc++;
        } else {
            log_warning(logger_cpu, "MEM ALLOC recibio error. Hubo deficit de memoria");
        }
    free(nueva_respuesta);
    free(respuesta);
    return;
    }
    if (*respuesta == MSG_OK) {
        registros->pc++;
    } else {
        log_warning(logger_cpu, "MEM ALLOC recibio error. Hubo deficit de memoria");
    }
    free(respuesta);
}

// MEM_FREE
void syscall_mem_free(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid, t_log* logger_cpu) {
    char id_segmento_str[32];

    sscanf(instruccion, "%*s %31s", id_segmento_str);

    uint32_t id_segmento = (uint32_t) atoi(id_segmento_str);

    op_code codigo = MSG_MEM_FREE;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &id_segmento, sizeof(uint32_t));

    int size;
    op_code* respuesta = recibir_mensaje(fd_ks, &size);

    if (respuesta == NULL) {
        log_error(logger_cpu, "En MEM FREE se recibio NULL");
        exit(EXIT_FAILURE);
    }
    if (*respuesta == MSG_INTERRUPT) {
        free(respuesta);
        log_info(logger_cpu,"## Interrupcion recibida");
        int size_interrupcion = 0;
        interrupcion_entrante = recibir_mensaje(fd_ks, &size_interrupcion);

        if (interrupcion_entrante == NULL) {
            log_error(logger_cpu,"Se recibió MSG_INTERRUPT pero no llegó la estructura t_interrupt");
            exit(EXIT_FAILURE);
        }
        if (size_interrupcion != sizeof(t_interrupcion)) {
            log_error(logger_cpu,"Tamaño inválido para t_interrupt. Recibido: %d - Esperado: %zu",size_interrupcion,sizeof(t_interrupcion));
            free(interrupcion_entrante);
            exit(EXIT_FAILURE);
        }

        log_warning(logger_cpu,"===== INTERRUPCIÓN RECIBIDA DURANTE MEM_ALLOC =====");
        log_warning(logger_cpu,"PID de la interrupción: %u",interrupcion_entrante->pid);
        log_warning(logger_cpu,"Motivo de la interrupción: %d",interrupcion_entrante->motivo);
        log_warning(logger_cpu,"===============================================");
        interrupcion_en_espera = true;

        op_code* nueva_respuesta = recibir_mensaje(fd_ks, &size);
        if (*nueva_respuesta == MSG_OK) {
            registros->pc++;
        } else {
            log_error(logger_cpu, "En MEM FREE recibio un error desde KS. Posible error al liberar memoria");
        }
    free(nueva_respuesta);
    return;
    }
    if (*respuesta == MSG_OK) {
        registros->pc++;
    } else {
        log_error(logger_cpu, "En MEM FREE recibio un error desde KS. Posible error al liberar memoria");
    }
    free(respuesta);
}

// EXIT
void syscall_exit(int fd_km, int fd_ks, t_contexto* contexto, uint32_t pid, t_log* logger_cpu) {
    if (contexto == NULL) {
        log_error(logger_cpu, "Error al utilizar contexto en instruccion: EXIT");
        exit(EXIT_FAILURE);
    }

    // Avisa a KS que el proceso finalizo
    op_code cod = MSG_DONE;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    // Se envia contexto final a KM
    contexto->proximo_a_detener = true;

    op_code codigo = MSG_EXIT_CPU;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    log_info(logger_cpu, "Se envio MSG_EXIT_CPU");
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    log_info(logger_cpu, "Se envio PID");

    int size;
    void* buffer = serializar_contexto(contexto, &size, logger_cpu);
    if (buffer == NULL) {
        log_error(logger_cpu, "Error al serializar el contexto");
        exit(EXIT_FAILURE);
    }

    enviar_mensaje(fd_km, buffer, size);
    op_code* ok = recibir_mensaje(fd_km, &size);
    if (ok == NULL) {
        log_error(logger_cpu, "Se recibio NULL en instruccion EXIT");
        exit(EXIT_FAILURE);
    }
    if (*ok != MSG_OK) {
        log_error(logger_cpu, "Se recibio un mensaje distinto al esperado: %d", *ok);
        free(ok);
        free(buffer);
        exit(EXIT_FAILURE);
    }

    free(ok);
    free(buffer);
}

// MUTEX_CREATE
int syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* registros) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_CREATE %s", nombre);

    op_code cod = MSG_MUTEX_CREATE;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    if (ok == NULL)
        return -1;
    if (*ok != MSG_OK) {
        free(ok);
        return -1;
    }
    free(ok);
    registros->pc++;
    return 0;
}

// MUTEX_LOCK
int syscall_mutex_lock(char* instruccion, int fd_ks, int fd_km, uint32_t pid, t_registros* registros, t_contexto* contexto, t_log* logger_cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_LOCK %s", nombre);

    op_code cod = MSG_MUTEX_LOCK;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    while (1) {
        int size = 0;
        op_code* respuesta = recibir_mensaje(fd_ks, &size);

        if (respuesta == NULL) {
            log_error(logger_cpu,"MUTEX_LOCK devolvió NULL para el PID %u",pid);
            return -1;
        }

        if (*respuesta == MSG_INTERRUPT) {
            free(respuesta);

            int size_interrupcion = 0;
            t_interrupcion* nueva_interrupcion = recibir_mensaje(fd_ks, &size_interrupcion);
            if (nueva_interrupcion == NULL) {
                log_error(logger_cpu,"Se recibió MSG_INTERRUPT pero no llegó t_interrupcion");
                return -1;
            }
            if (size_interrupcion != sizeof(t_interrupcion)) {
                log_error(logger_cpu,"Tamaño inválido para t_interrupcion. ""Recibido: %d - Esperado: %zu",size_interrupcion,sizeof(t_interrupcion));
                free(nueva_interrupcion);
                return -1;
            }

            interrupcion_entrante = nueva_interrupcion;
            interrupcion_en_espera = true;

            log_warning(logger_cpu,"===== INTERRUPCIÓN RECIBIDA DURANTE MUTEX_LOCK =====");
            log_warning(logger_cpu,"PID de la interrupción: %u",interrupcion_entrante->pid);
            log_warning(logger_cpu,"Motivo de la interrupción: %d",interrupcion_entrante->motivo);
            log_warning(logger_cpu,"===================================================");
            continue;
        }

        if (*respuesta == MSG_OK) {
            free(respuesta);
            registros->pc++;
            log_debug(logger_cpu,"## PID: %u - MUTEX_LOCK %s ejecutado correctamente",pid,nombre);
            return 0;
        }

        if (*respuesta == MSG_BLOQUEADO) {
            free(respuesta);
            registros->pc++;
            log_debug(logger_cpu,"## PID: %u - Bloqueado por MUTEX_LOCK %s. ""Se libera la CPU",pid,nombre);
            guardar_contexto_km(fd_km,contexto,pid,logger_cpu);
            return 2;
        }

        log_error(logger_cpu,"MUTEX_LOCK recibió una respuesta inesperada: %d",*respuesta);
        free(respuesta);
        return -1;
    }
}

// MUTEX_UNLOCK
int syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* registros, t_log* logger_cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_UNLOCK %s", nombre);

    op_code cod = MSG_MUTEX_UNLOCK;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    if (ok == NULL)
        return -1;
    if (*ok == MSG_INTERRUPT) {
        log_info(logger_cpu,"## Interrupcion recibida");
        int size_interrupcion = 0;
        interrupcion_entrante = recibir_mensaje(fd_ks, &size_interrupcion);

        if (interrupcion_entrante == NULL) {
            log_error(logger_cpu,"Se recibió MSG_INTERRUPT pero no llegó la estructura t_interrupt");
            exit(EXIT_FAILURE);
        }
        if (size_interrupcion != sizeof(t_interrupcion)) {
            log_error(logger_cpu,"Tamaño inválido para t_interrupt. Recibido: %d - Esperado: %zu",size_interrupcion,sizeof(t_interrupcion));
            free(interrupcion_entrante);
            exit(EXIT_FAILURE);
        }

        log_warning(logger_cpu,"===== INTERRUPCIÓN RECIBIDA DURANTE MUTEX UNLOCK =====");
        log_warning(logger_cpu,"PID de la interrupción: %u",interrupcion_entrante->pid);
        log_warning(logger_cpu,"Motivo de la interrupción: %d",interrupcion_entrante->motivo);
        log_warning(logger_cpu,"===============================================");
        interrupcion_en_espera = true;

        op_code* nueva_respuesta = recibir_mensaje(fd_ks, &size);
        if (*nueva_respuesta == MSG_OK) {
            registros->pc++;
        } else {
            log_error(logger_cpu, "MUTEX UNLOCK esta recibiendo mensajes inesperados");
            exit(EXIT_FAILURE);
        }
        free(nueva_respuesta);
        free(ok);
        return 0;
    }
    if (*ok != MSG_OK) {
        log_warning(logger_cpu, "MUTEX UNLOCK recibio un %d durante la ejecucion.", *ok);
        free(ok);
        return -1;
    }
    free(ok);
    registros->pc++;
    return 0;
}

// SLEEP
int syscall_sleep(char* instruccion, int fd_ks, int fd_km, uint32_t pid, t_registros* registros, t_contexto* contexto, t_log* logger_cpu) {
    char tiempo_str[32];
    sscanf(instruccion, "SLEEP %31s", tiempo_str);
    int tiempo = atoi(tiempo_str);

    registros->pc++;
    guardar_contexto_km(fd_km, contexto, pid, logger_cpu);

    op_code cod = MSG_SLEEP;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tiempo, sizeof(int));

    return 2; // bloqueado por IO -> la CPU se libera
}