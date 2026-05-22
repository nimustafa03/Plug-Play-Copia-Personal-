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


char* fetch(int conexion_servidor, u_int32_t pid, RegistrosCPU* cpu){
    op_code codigo = MSG_FETCH_CPU;     //envio codigo de operacion al servidor
    enviar_mensaje(conexion_servidor,&codigo,sizeof(op_code));

    t_fetch request;
    request.pid = pid;
    request.pc  = cpu->PC;

    enviar_mensaje(conexion_servidor, &request, sizeof(t_fetch)); //envio peticion de instruccion

    int size_instruccion;
    char* instruccion = (char*) recibir_mensaje(conexion_servidor, &size_instruccion);
    if (instruccion == NULL) {
        printf("Error al recibir instruccion. Se cerrara el programa.\n");
        exit(EXIT_FAILURE);
    }
    return instruccion;
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

void execute(op_code_cpu codeop, char* instruccion,RegistrosCPU* cpu){
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


void cpu(int conexion_servidor, u_int32_t pid){
    t_log* logger_cpu;
    logger_cpu = log_create("funcion_cpu.log","FUNCION CPU LOGGER",1,LOG_LEVEL_INFO);
    if (logger_cpu == NULL){
	    perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
	    exit(EXIT_FAILURE);
    }
    log_info(logger_cpu, "EJECUTANDO CPU");

    RegistrosCPU cpu = {0}; //inicializo registros
    cpu.PC = 0;
    log_info(logger_cpu, "FASE FETCH");
    char* instruccion = fetch(conexion_servidor, pid, &cpu);
    log_info(logger_cpu, "FASE DECODE");
    op_code_cpu codop = decode(instruccion);
    log_info(logger_cpu, "FASE EXECUTE");
    execute(codop,instruccion,&cpu);

    op_code op;

    if (check_interrupcion(fd_ks, &op)) {
        if (op == MSG_INTERRUPT) {
            int size;
            t_interrupcion* intr = recibir_mensaje(fd_ks, &size);

            if (intr == NULL)
                return;
            if (intr->pid == pid) {
                enviar_mensaje(fd_km, cpu, sizeof(RegistrosCPU));
                enviar_mensaje(fd_ks, &MSG_INTERRUPCION_ATENDIDA, sizeof(op_code));
            }
            free(intr);
        }
}
    free(instruccion);
}