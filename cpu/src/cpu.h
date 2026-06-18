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


// OPCODES CPU
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
    OP_INVALID = -1,
} op_code_cpu;

// REQUEST FETCH
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch;

// INTERRUPCIONES -> tipos.h

// CICLO DE INSTRUCCION

char* fetch(int conexion_servidor,uint32_t pid, t_registros* cpu);

op_code_cpu decode(char* instruccion);

// CP3 -> Para las syscalls del mutex necesita tambien el fd_ks y el PID
void execute(op_code_cpu codeop, char* instruccion, t_registros* cpu, int fd_ks, uint32_t pid);

void set(char* instruccion, t_registros* cpu);
void sum(char* instruccion, t_registros* cpu);
void sub(char* instruccion, t_registros* cpu);
void mov_in(char* instruccion, t_registros* cpu);
void mov_out(char* instruccion, t_registros* cpu);
void jnz(char* instruccion, t_registros* cpu);
void copy_mem(char* instruccion, t_registros* cpu);
void noop(char* instruccion, t_registros* cpu);
void syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid);
void syscall_mutex_lock(char* instruccion, int fd_ks, uint32_t pid);
void syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid);

#endif