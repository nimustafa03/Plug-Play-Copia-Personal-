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

#define MMU_ERROR -1

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
        }  else {
        registro->pc++;
    }
}
// NOOP
void noop(t_registros* registros) {
    registros->pc++;
}

// INIT_PROC
void syscall_init_proc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid){
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

    if (respuesta == NULL)
    return;
    if (*respuesta != MSG_OK) {
        free(respuesta);
        return;
    }

    free(respuesta);
    registro->pc++;
}

// MOV_IN
int mov_in(char* instruccion,t_registros* registros,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3],t_list* tabla_segmentos){
    char registro_destino[8];

    sscanf(instruccion,"%*s %7s",registro_destino);

    uint32_t direccion_logica = registros->si;
    uint32_t tamanio = tamanio_registro(registro_destino);

    if (tamanio == 0)
        return -1;

    int direccion_fisica =memory_management_unit(direccion_logica,tamanio,tabla_segmentos);
    if (direccion_fisica == MMU_ERROR)
        return -1;

    void* datos = lectura_ms(direccion_fisica,tamanio,mapa,fd_ms,fd_ms_agregados);
    if (datos == NULL)
        return -1;

    uint32_t valor = 0;

    memcpy(&valor,datos,tamanio);
    free(datos);

    escribir_registro(registro_destino,registros,valor);
    registros->pc++;
    return 0;
}

// MOV_OUT

int mov_out(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3]){
    char registro_origen[8];

    sscanf(instruccion,"%*s %7s",registro_origen);

    uint32_t direccion_logica = registros->di;
    uint32_t tamanio = tamanio_registro(registro_origen);

    if (tamanio == 0)
        return -1;

    int direccion_fisica =memory_management_unit(direccion_logica,tamanio,tabla_segmentos);
    if (direccion_fisica == MMU_ERROR) 
        return -1;

    uint32_t valor = obtener_valor(registro_origen,registros);

    int resultado = escritura_ms((uint32_t)direccion_fisica,&valor,tamanio,mapa,fd_ms,fd_ms_agregados);
    if (resultado == -1)
        return -1;

    registros->pc++;
    return 0;
}

// COPY_MEM
int copy_mem(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa_ms,int fd_ms,int fd_ms_agregados[3],uint32_t pid,t_log* logger_cpu){
    char registro_tamanio[8];

    sscanf(instruccion, "%*s %7s", registro_tamanio);

    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    uint32_t direccion_logica_origen = registros->si;
    uint32_t direccion_logica_destino = registros->di;

    int direccion_fisica_origen = memory_management_unit(direccion_logica_origen,tamanio,tabla_segmentos);
    if (direccion_fisica_origen == MMU_ERROR) {
        log_error(logger_cpu,"## PID: %u - COPY_MEM produjo SEG_FAULT en el origen",pid);
        return -1;
    }

    int direccion_fisica_destino = memory_management_unit(direccion_logica_destino,tamanio,tabla_segmentos);
    if (direccion_fisica_destino == MMU_ERROR) {
        log_info(logger_cpu,"## PID: %u - COPY_MEM produjo SEG_FAULT en el destino",pid);
        return -1;
    }

    void* buffer = lectura_ms(direccion_fisica_origen,tamanio,mapa_ms,fd_ms,fd_ms_agregados);
    if (buffer == NULL) {
        log_info(logger_cpu,"## PID: %u - Error al leer memoria en COPY_MEM",pid);
        return -1;
    }

    int resultado_escritura = escritura_ms(direccion_fisica_destino,buffer,tamanio,mapa_ms,fd_ms,fd_ms_agregados);
    if (resultado_escritura == -1) {
        log_info(logger_cpu,"## PID: %u - Error al escribir memoria en COPY_MEM",pid);

        free(buffer);
        return -1;
    }

    free(buffer);
    registros->pc++;

    log_info(logger_cpu,"## PID: %u - Ejecutando: COPY_MEM - %s",pid,registro_tamanio);
    return 0;
}

// STDIN
int syscall_stdin(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {

    char registro_direccion[32];
    char registro_tamanio[32];

    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    op_code codigo = MSG_STDIN;
    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t)); 
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    int size;
    op_code* respuesta = recibir_mensaje(fd_ks, &size);
    if (respuesta == NULL)
        return -1;

    if (*respuesta != MSG_OK) {
        free(respuesta);
        return -1;
    }

    free(respuesta);
    registros->pc++;
    return 0;
}

// STDOUT
int syscall_stdout(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {

    char registro_direccion[32];
    char registro_tamanio[32];

    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    op_code codigo = MSG_STDOUT;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t)); 
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    int size;
    op_code* respuesta = recibir_mensaje(fd_ks, &size);
    if (respuesta == NULL)
        return -1;

    if (*respuesta != MSG_OK) {
        free(respuesta);
        return -1;
    }
    free(respuesta);
    registros->pc++;
    return 0;
}

// MEM_ALLOC
int syscall_mem_alloc(char* instruccion,t_registros* registros,int fd_ks,uint32_t pid) {
    char id_segmento_str[32];
    char tamanio_str[32];

    sscanf(instruccion,"%*s %31s %31s",id_segmento_str,tamanio_str);

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
        free(respuesta);
        return -1;
    }
    if (*respuesta == MSG_OK) {
        registros->pc++;
    } else {
        free(respuesta);
        return 1;
    }

    free(respuesta);
    return 0;
}

// MEM_FREE
int syscall_mem_free(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {
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
        free(respuesta);
        return -1;
    }
    if (*respuesta == MSG_OK) {
        registros->pc++;
    } else {
        free(respuesta);
        return 1;
    }

    free(respuesta);
    return 0;
}

// EXIT
int syscall_exit(int fd_ks, uint32_t pid){
    op_code cod = MSG_DONE;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    return 1;
}

// MUTEX_CREATE
int syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
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
    cpu->pc++;
    return 0;
}

// MUTEX_LOCK
int syscall_mutex_lock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_LOCK %s", nombre);

    op_code cod = MSG_MUTEX_LOCK;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    // bloqueante — espera hasta que KS lo desbloquee
    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    if (ok == NULL)
        return -1;
    if (*ok != MSG_OK) {
        free(ok);
        return -1;
    }
    free(ok);
    cpu->pc++;
    return 0;
}

// MUTEX_UNLOCK
int syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
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
    if (*ok != MSG_OK) {
        free(ok);
        return -1;
    }

    free(ok);
    cpu->pc++;
    return 0;
}

// SLEEP
// Bloquea hasta que KS responda (el proceso vuelve a READY y la CPU recién ahí recibe el MSG_OK que la destraba).
void syscall_sleep(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
    char tiempo_str[32];
    sscanf(instruccion, "SLEEP %s", tiempo_str);
    int tiempo = atoi(tiempo_str);

    op_code cod = MSG_SLEEP;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tiempo, sizeof(int));

    // bloqueante - espera hasta que KS confirme que el sleep terminó
    int size;
    op_code* ok = recibir_mensaje(fd_ks, &size);
    if (*ok != MSG_OK) {
        free(ok);
        return;
    }
    free(ok);

    cpu->pc++;
}
