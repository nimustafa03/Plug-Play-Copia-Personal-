#ifndef CPUS_H
#define CPUS_H

#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <stdint.h>
#include <utils/tipos.h>  // t_interrupcion viene de acá


// OPCODES
typedef enum {
    OP_NOOP,
    OP_SET,
    OP_MOV_IN,
    OP_MOV_OUT,
    OP_SUM,
    OP_SUB,
    OP_JNZ,
    OP_COPY_MEM,
    OP_MUTEX_CREATE,
    OP_MUTEX_LOCK,
    OP_MUTEX_UNLOCK,
    OP_SLEEP,
    OP_STDIN,
    OP_STDOUT,
    OP_INVALID,
    OP_MEM_ALLOC,
    OP_MEM_FREE,
    OP_INIT_PROC,
    OP_EXIT,
} operacion;

// STRUCT

typedef struct {
    uint32_t pid;
    uint32_t id_segmento;
    uint32_t tamanio;
} t_mem_alloc;

typedef struct {
    uint32_t pid;
    uint32_t id_segmento;
} t_mem_free;

// REQUEST FETCH
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch;
typedef struct {
    uint32_t id_segmento;
    uint32_t base;
    uint32_t tamanio;
} t_segmento;


// INTERRUPCIONES -> tipos.h

// CICLO DE INSTRUCCION

char* fetch(int conexion_servidor,uint32_t pid, t_registros* cpu);
operacion decode(char* instruccion);
int memory_management_unit(uint32_t direccion_logica, uint32_t tamanio_acceso, t_list* tabla_segmentos);
int lectura_ms (int direccion, int fd_ms);
int escritura_ms(int direccion, uint32_t dato, uint32_t tamanio, int fd_ms);
int atender_interrupcion(int fd_ks,int fd_km,t_contexto* contexto);

// CP3 -> Para las syscalls del mutex necesita tambien el fd_ks y el PID
int execute(operacion codigo, char* instruccion, t_registros* cpu, int fd_ks, int fd_km, int fd_ms, uint32_t pid, t_list* tabla_segmentos, t_log* logger_cpu);

void set(char* instruccion, t_registros* registro);
void sum(char* instruccion, t_registros* registro);
void sub(char* instruccion, t_registros* registro);
int mov_in(char* instruccion, t_registros* registro, int fd_ms,t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid);
int mov_out(char* instruccion, t_registros* registro, int fd_ms,t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid);
void jnz(char* instruccion, t_registros* registro);
int copy_mem(char* instruccion, t_registros* registro, int fd_ms,t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid);
void noop(t_registros* registro);
void syscall_init_proc(char* instruccion, t_registros* registro, int fd_ks);
void syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
void syscall_mutex_lock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
void syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
void syscall_sleep(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
void syscall_stdin(char* instruccion,t_registros* registro, int fd_ks, uint32_t pid);
void syscall_stdout(char* instruccion,t_registros* registro, int fd_ks, uint32_t pid);
void syscall_mem_alloc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid);
void syscall_mem_free(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid);
int syscall_exit(t_registros* registro, int fd_ks, uint32_t pid);

#endif