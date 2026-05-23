#ifndef CPUS_H
#define CPUS_H

#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <cpu.h>
#include <stdint.h>


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
    OP_INVALID = -1
} op_code_cpu;

// REQUEST FETCH
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch;

// INTERRUPCIONES
typedef struct {
    uint32_t pid;
    int motivo;
} t_interrupcion;

// CICLO DE INSTRUCCION

char* fetch(int conexion_servidor,uint32_t pid,RegistrosCPU* cpu);

op_code_cpu decode(char* instruccion);

void execute(op_code_cpu codeop,char* instruccion,RegistrosCPU* cpu);


#endif