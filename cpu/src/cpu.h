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
    OP_INVALID = -1
} op_code_cpu;


// REGISTROS CPU

typedef struct {
    uint32_t PC;
    uint8_t AX;
    uint8_t BX;
    uint8_t CX;
    uint8_t DX;
    uint32_t EAX;
    uint32_t EBX;
    uint32_t ECX;
    uint32_t EDX;
    uint32_t SI;
    uint32_t DI;
} RegistrosCPU;

// REQUEST FETCH
typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch;

// INTERRUPCIONES -> tipos.h

// CICLO DE INSTRUCCION

char* fetch(int conexion_servidor,uint32_t pid,RegistrosCPU* cpu);

op_code_cpu decode(char* instruccion);

void execute(op_code_cpu codeop,char* instruccion,RegistrosCPU* cpu);


#endif