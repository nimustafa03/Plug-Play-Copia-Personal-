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
    sscanf(instruccion, "%*s %s %s", registro_destino, pc_ptr);

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
void syscall_init_proc(char* instruccion, t_registros* registro, int fd_ks){
    char archivo[256];
    int prioridad;
    sscanf(instruccion, "%*s %255s %d", archivo, &prioridad);

    op_code codigo = MSG_INIT_PROC;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, archivo, strlen(archivo) + 1);
    enviar_mensaje(fd_ks, &prioridad, sizeof(int));

    registro->pc++;
}

// MOV_IN
int mov_in (char* instruccion, t_registros* registro, int fd_ms, t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid){

    char registro_destino[32];
    sscanf(instruccion, "%*s %s %*s", registro_destino);
    uint32_t tamanio_acceso = tamanio_registro(registro_destino);

    uint32_t direccion_logica = registro->si;

    int direccion_fisica = memory_management_unit(direccion_logica, tamanio_acceso, tabla_segmentos);
    if (direccion_fisica == MMU_ERROR)
        return -1;

    int dato = lectura_ms(direccion_fisica, fd_ms);
    escribir_registro(registro_destino, registro, dato);
    log_info(logger_cpu,"PID: %u - Acción: LEER - Dirección Física: %d - Valor: %d.", pid, direccion_fisica, dato);

    registro->pc++;
    return 0;
}

// MOV_OUT

int mov_out(char* instruccion, t_registros* registro, int fd_ms, t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid) {

    char registro_origen[32];
    sscanf(instruccion, "%*s %31s", registro_origen);

    uint32_t tamanio_acceso = tamanio_registro(registro_origen);
    uint32_t valor = obtener_valor(registro_origen, registro);

    uint32_t direccion_logica = registro->di;

    int direccion_fisica = memory_management_unit(direccion_logica, tamanio_acceso, tabla_segmentos);
    if (direccion_fisica == MMU_ERROR) {
        return -1;
    }
    escritura_ms(direccion_fisica, valor, tamanio_acceso, fd_ms);
    log_info(logger_cpu,"PID: %u - Acción: ESCRIBIR - Dirección Física: %d - Valor: %u.", pid, direccion_fisica, valor);

    registro->pc++;
    return 0;
}

// COPY_MEM
int copy_mem(char* instruccion, t_registros* registro, int fd_ms, t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid) {

    char registro_tamanio[32];
    sscanf(instruccion, "%*s %31s", registro_tamanio);

    uint32_t cantidad_bytes = obtener_valor(registro_tamanio, registro);

    uint32_t direccion_logica_origen = registro->si;
    uint32_t direccion_logica_destino = registro->di;

    int direccion_fisica_origen = memory_management_unit(direccion_logica_origen,cantidad_bytes,tabla_segmentos);
    int direccion_fisica_destino = memory_management_unit(direccion_logica_destino,cantidad_bytes,tabla_segmentos);

    if (direccion_fisica_origen == MMU_ERROR || direccion_fisica_destino == MMU_ERROR) {
        return -1;
    }

    for (uint32_t i = 0; i < cantidad_bytes; i++) {
        uint32_t dato = lectura_ms(direccion_fisica_origen + i, fd_ms);
        log_info(logger_cpu,"PID: %u - Acción: LEER - Dirección Física: %d - Valor: %u.", pid, direccion_fisica_origen, dato);
        escritura_ms(direccion_fisica_destino + i, dato, sizeof(uint8_t), fd_ms);
        log_info(logger_cpu,"PID: %u - Acción: ESCRIBIR - Dirección Física: %d - Valor: %u.", pid, direccion_fisica_destino, dato);
    }


    registro->pc++;
    return 0;
}

// STDIN
void syscall_stdin(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {

    char registro_direccion[32];
    char registro_tamanio[32];

    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    op_code codigo = MSG_STDIN;
    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t)); 
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    registros->pc++;
}

// STDOUT
void syscall_stdout(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {

    char registro_direccion[32];
    char registro_tamanio[32];

    sscanf(instruccion, "%*s %31s %31s", registro_direccion, registro_tamanio);

    uint32_t direccion_logica = obtener_valor(registro_direccion, registros);
    uint32_t tamanio = obtener_valor(registro_tamanio, registros);

    op_code codigo = MSG_STDOUT;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &direccion_logica, sizeof(uint32_t)); 
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    registros->pc++;
}

// MEM_ALLOC
void syscall_mem_alloc(char* instruccion,t_registros* registros,int fd_ks,uint32_t pid) {
    char id_segmento_str[32];
    char tamanio_str[32];

    sscanf(instruccion,"%*s %31s %31s",id_segmento_str,tamanio_str);

    uint32_t id_segmento = (uint32_t) atoi(id_segmento_str);
    uint32_t tamanio = (uint32_t) atoi(tamanio_str);

    op_code codigo = MSG_MEM_ALLOC;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &id_segmento, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &tamanio, sizeof(uint32_t));

    registros->pc++;
}

// MEM_FREE
void syscall_mem_free(char* instruccion, t_registros* registros, int fd_ks, uint32_t pid) {
    char id_segmento_str[32];

    sscanf(instruccion, "%*s %31s", id_segmento_str);

    uint32_t id_segmento = (uint32_t) atoi(id_segmento_str);

    op_code codigo = MSG_MEM_FREE;

    enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_ks, &id_segmento, sizeof(uint32_t));

    registros->pc++;
}

// EXIT
int syscall_exit(t_registros* registro, int fd_ks, uint32_t pid){
    return 1;
}

// MUTEX_CREATE
void syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_CREATE %s", nombre);

    op_code cod = MSG_MUTEX_CREATE;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    free(ok);

    cpu->pc++;
}

// MUTEX_LOCK
void syscall_mutex_lock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_LOCK %s", nombre);

    op_code cod = MSG_MUTEX_LOCK;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    // bloqueante — espera hasta que KS lo desbloquee
    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    free(ok);

    cpu->pc++;
}

// MUTEX_UNLOCK
void syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu) {
    char nombre[64];
    sscanf(instruccion, "MUTEX_UNLOCK %s", nombre);

    op_code cod = MSG_MUTEX_UNLOCK;
    enviar_mensaje(fd_ks, &cod, sizeof(op_code));
    enviar_mensaje(fd_ks, nombre, strlen(nombre) + 1);
    enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));

    int size; 
    op_code* ok = recibir_mensaje(fd_ks, &size);
    free(ok);

    cpu->pc++;
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
    free(ok);

    cpu->pc++;
}
