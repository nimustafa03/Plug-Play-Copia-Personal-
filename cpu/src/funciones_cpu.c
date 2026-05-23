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
#include "tipos.h"


char* fetch(int fd_km, u_int32_t pid, t_registros* cpu){

    // Aviso de fetch
    op_code codigo_fetch = MSG_FETCH_CPU;
    enviar_mensaje(fd_km,&codigo_fetch,sizeof(op_code));
    // Envio PC
    int pc = cpu->PC;
    enviar_mensaje(fd_km, &pc, sizeof(pc)); 
    // KM responde por OK/ERROR
    int size_respuesta;
    op_code* respuesta_km = (op_code*) recibir_mensaje(fd_km,&size_respuesta);
    if (*respuesta_km == MSG_ERROR){
        free(respuesta_km);
        return NULL;
    free(respuesta_km);
    // Recibe Instruccion
    int size_instruccion;
    char* instruccion = (char*) recibir_mensaje(fd_km, &size_instruccion);
    if (instruccion == NULL) {
        return NULL;
    }
    return instruccion;
    }
}

op_code_cpu decode(char* instruccion) {
    char codeop[32];
    sscanf(instruccion, "%s", codeop);

    if (strcmp(codeop, "NOOP") == 0) 
        return OP_NOOP;
    if (strcmp(codeop, "SET") == 0) 
        return OP_SET;
    if (strcmp(codeop, "MOV_IN") == 0) 
        return OP_MOV_IN;
    if (strcmp(codeop, "MOV_OUT") == 0) 
        return OP_MOV_OUT;
    if (strcmp(codeop, "SUM") == 0) 
        return OP_SUM;
    if (strcmp(codeop, "SUB") == 0) 
        return OP_SUB;
    if (strcmp(codeop, "JNZ") == 0) 
        return OP_JNZ;
    if (strcmp(codeop, "COPY_MEM") == 0) 
        return OP_COPY_MEM;
    
    return OP_INVALID;
}

void execute(op_code_cpu codeop, char* instruccion, t_registros* cpu){
    switch (codeop){
    case OP_SET:
        set(instruccion, cpu);
        break;
    case OP_SUM:
        sum(instruccion, cpu);
        break;
    case OP_SUB:
        sub(instruccion, cpu);
        break;
    case OP_MOV_IN:
        mov_in(instruccion, cpu);
        break;
    case OP_MOV_OUT:
        mov_out(instruccion, cpu);
        break;
    case OP_JNZ:
        jnz(instruccion, cpu);
        break;
    case OP_COPY_MEM:
        copy_mem(instruccion, cpu);
        break;
    case OP_NOOP:
        noop(instruccion, cpu);
        break;
    }
}

int check_interrupcion(int fd_ks, op_code* codigo_recibido) {   //Consulto si llego un mensaje de interrupcion desde el KS
    op_code op;
    int bytes = recv(fd_ks, &op, sizeof(op_code), MSG_DONTWAIT);    //DONTWAIT para no quedarse escuchando

    if (bytes == 0)
        return 0;
    if (bytes < 0) 
        return 0;
    if (bytes != sizeof(op_code))
        return 0; // por si es una lectura incompleta
    *codigo_recibido = op;
    return 1;
}