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
#include <sys/socket.h> // Para el recv() que tiene que recibir el MSG_DONTWAIT de esa biblioteca


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
    }
    free(respuesta_km);
    // Recibe Instruccion
    int size_instruccion;
    char* instruccion = recibir_mensaje(fd_km, &size_instruccion);
    if (instruccion == NULL) return NULL;
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
    // CP3
    if (strcmp(codeop, "MUTEX_CREATE") == 0) 
        return OP_MUTEX_CREATE;
    if (strcmp(codeop, "MUTEX_LOCK")   == 0) 
        return OP_MUTEX_LOCK;
    if (strcmp(codeop, "MUTEX_UNLOCK") == 0) 
        return OP_MUTEX_UNLOCK;
    if (strcmp(codeop, "SLEEP") == 0) 
        return OP_SLEEP;
        
    return OP_INVALID;
}

void execute(op_code_cpu codeop, char* instruccion, t_registros* cpu, int fd_ks, uint32_t pid){
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
    // CP3:
    case OP_MUTEX_CREATE:
        syscall_mutex_create(instruccion, fd_ks, pid, cpu); 
        break;
    case OP_MUTEX_LOCK:
        syscall_mutex_lock(instruccion, fd_ks, pid, cpu); 
        break;
    case OP_MUTEX_UNLOCK:
        syscall_mutex_unlock(instruccion, fd_ks, pid, cpu);
        break;
    case OP_SLEEP:
        syscall_sleep(instruccion, fd_ks, pid, cpu);
        break;
    }
}

int atender_interrupcion(int fd_ks,int fd_km,t_contexto_ejecucion* contexto){
    
    op_code codigo;
    int bytes = recv(fd_ks,&codigo,sizeof(op_code), MSG_DONTWAIT);

    if (bytes <= 0)
        return 0;

    if (codigo != MSG_INTERRUPT)
        return 0;

    // Recibo estructura de interrupción
    int size;
    t_interrupcion* intr =recibir_mensaje(fd_ks, &size);
    if (intr == NULL)
        return 0;

    // Interrupción para otro PID
    if (intr->pid != contexto->pid) {
        free(intr);
        return 0;
    }

    // Aviso a KM que voy a actualizar contexto
    op_code guardar_contexto = MSG_CONTEXTO_EJECUCION_CPU;
    enviar_mensaje(fd_km,&guardar_contexto,sizeof(op_code));

    // Envío contexto actualizado
    enviar_mensaje(fd_km,contexto,sizeof(t_contexto_ejecucion));

    // Aviso a KS que atendí la interrupción
    op_code respuesta = MSG_INTERRUPCION_ATENDIDA;

    enviar_mensaje(fd_ks,&respuesta,sizeof(op_code));

    // Devuelvo PID
    enviar_mensaje(fd_ks,&(contexto->pid),sizeof(uint32_t));

    // Devuelvo motivo
    enviar_mensaje(fd_ks,&(intr->motivo),sizeof(int));

    free(intr);

    return 1;
}

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

    cpu->PC++;
}

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

    cpu->PC++;
}

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

    cpu->PC++;
}

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

    cpu->PC++;
}

int memory_management_unit(direccion_logica){
    int segmento = direccion_logica / //tamanio segmento;
    int desplazamiento = direccion_logica % //tamanio segmento;
    int direccion_fisica = segmento + desplazamiento;
    if direccion_fisica > //tamanio segmento{
        return MMU_ERROR
    return direccion_fisica;
}

t_instruccion_traducida* traducir_instruccion(char* instruccion) {
    t_instruccion_traducida* inst =malloc(sizeof(t_instruccion_traducida));

    inst->destino[0] = '\0';
    inst->origen[0] = '\0';
    char opcode_texto[64] = "";
    sscanf(instruccion,"%63s %63s %63s",opcode_texto,inst->destino,inst->origen);

    inst->opcode = decode(opcode_texto);

    if (strchr(inst->destino, '[') != NULL) {
        int dir_fisica_destino =memory_management_unit(inst->destino);

        if (dir_fisica_destino == MMU_ERROR) {
            free(inst);
            return NULL;
        }
        hubo_busqueda_destino = true;
        sprintf(inst->destino,"%d",dir_fisica_destino);
    }

    if (strchr(inst->origen, '[') != NULL) {
        int dir_fisica_origen = memory_management_unit(inst->origen);
        if (dir_fisica_origen == MMU_ERROR) {
            free(inst);
            return NULL;
        }
        hubo_busqueda_origen = true;
        sprintf(inst->origen,"%d",dir_fisica_origen);
    }
    return inst;
}