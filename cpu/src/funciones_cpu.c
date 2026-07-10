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
#include <errno.h>

#define MMU_ERROR (-1)

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


operacion decode(char* instruccion) {
    if (strstr(instruccion, "NOOP") != NULL) return OP_NOOP;
    if (strstr(instruccion, "SET") != NULL) return OP_SET;
    if (strstr(instruccion, "MOV_IN") != NULL) return OP_MOV_IN;
    if (strstr(instruccion, "MOV_OUT") != NULL) return OP_MOV_OUT;
    if (strstr(instruccion, "SUM") != NULL) return OP_SUM;
    if (strstr(instruccion, "SUB") != NULL) return OP_SUB;
    if (strstr(instruccion, "JNZ") != NULL) return OP_JNZ;
    if (strstr(instruccion, "COPY_MEM") != NULL) return OP_COPY_MEM;
    if (strstr(instruccion, "MUTEX_CREATE") != NULL) return OP_MUTEX_CREATE;
    if (strstr(instruccion, "MUTEX_LOCK") != NULL) return OP_MUTEX_LOCK;
    if (strstr(instruccion, "MUTEX_UNLOCK") != NULL) return OP_MUTEX_UNLOCK;
    if (strstr(instruccion, "STDIN") != NULL) return OP_STDIN;
    if (strstr(instruccion, "STDOUT") != NULL) return OP_STDOUT;
    if (strstr(instruccion, "SLEEP") != NULL) return OP_SLEEP;
    if (strstr(instruccion, "INIT_PROC") != NULL) return OP_INIT_PROC;
    if (strstr(instruccion, "MEM_ALLOC") != NULL) return OP_MEM_ALLOC;
    if (strstr(instruccion, "MEM_FREE") != NULL) return OP_MEM_FREE;
    if (strstr(instruccion, "EXIT") != NULL) return OP_EXIT;
    return OP_INVALID;
}

int execute(operacion codigo, char* instruccion, t_registros* registros, int fd_ks, int fd_km, int fd_ms, uint32_t pid, t_list* tabla_segmentos){
    switch (codigo){
        case OP_SET:
            set(instruccion, registros);
            break;
        case OP_SUM:
            sum(instruccion, registros);
            break;
        case OP_SUB:
            sub(instruccion, registros);
            break;
        case OP_MOV_IN:
            if (mov_in(instruccion, registros, fd_ms, tabla_segmentos) == -1) {
                op_code codigo = MSG_SEG_FAULT;
                enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
                enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
                return -1;
            }
            break;
        case OP_MOV_OUT:
            if (mov_out(instruccion, registros, fd_ms, tabla_segmentos) == -1) {
                op_code codigo = MSG_SEG_FAULT;
                enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
                enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
                return -1;
            }
            break;
        case OP_JNZ:
            jnz(instruccion, registros);
            break;
        case OP_COPY_MEM:
            if (copy_mem(instruccion, registros, fd_ms, tabla_segmentos) == -1) {
                op_code codigo = MSG_SEG_FAULT;     //falta escribir msj
                enviar_mensaje(fd_ks, &codigo, sizeof(op_code));
                enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
                return -1;
            }
            break;
        case OP_NOOP:
            noop(registros);
            break;
        // CP3:
        case OP_MUTEX_CREATE:
            syscall_mutex_create(instruccion, fd_ks, pid, registros); 
            break;
        case OP_MUTEX_LOCK:
            syscall_mutex_lock(instruccion, fd_ks, pid, registros); 
            break;
        case OP_MUTEX_UNLOCK:
            syscall_mutex_unlock(instruccion, fd_ks, pid, registros);
            break;
        case OP_INIT_PROC:
            syscall_init_proc(instruccion, registros, fd_ks); 
            break;
        case OP_SLEEP:
            syscall_sleep(instruccion, fd_ks, pid, registros); 
            break;
        case OP_MEM_ALLOC:
            syscall_mem_alloc(instruccion, registros, fd_ks, pid); 
            break;
        case OP_MEM_FREE:
            syscall_mem_free(instruccion, registros, fd_ks, pid); 
            break;
        case OP_STDIN:
            syscall_stdin(instruccion, registros, fd_ks, pid);
            break;
        case OP_STDOUT:
            syscall_stdout(instruccion, registros, fd_ks, pid);
            break;
        case OP_EXIT:
            int op_exit = syscall_exit(registros, fd_ks, pid);
            return op_exit;
        case OP_INVALID:
            return -2;
    }
    return 0;
}

int recibir_interrupcion(int fd_ks){
    int size;
    op_code codigo;
    int bytes = recv(fd_ks, &size, sizeof(int), MSG_DONTWAIT);
    if (bytes == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 1;  // No hay mensaje pendiente

        return -1;     // Error de recepcion
    }
    if (bytes == 0)
        return -1;     // KS cerró la conexión.
    bytes = recv(fd_ks, &codigo, sizeof(op_code), MSG_WAITALL);
    if (bytes != sizeof(op_code))
        return -1;
    if (codigo != MSG_INTERRUPT)
        return -1;
    return 0;          // Llego MSG_INTERRUPT
}

int atender_interrupcion(int fd_ks,int fd_km,t_contexto_ejecucion* contexto){
    int resultado = recibir_interrupcion(fd_ks);

    if (resultado == 1)
        return 1;
    if (resultado == -1)
        return -1;

    if (resultado == 0) {
        int size;
        t_interrupcion* interrupcion = recibir_mensaje(fd_ks, &size);
        uint32_t pid = interrupcion->pid;
        int motivo = interrupcion->motivo;
        free(interrupcion);

        op_code guardar_contexto = MSG_INTERRUPT;
        enviar_mensaje(fd_km, &guardar_contexto, sizeof(op_code));
        enviar_mensaje(fd_km, contexto, sizeof(t_contexto_ejecucion));

        // avisar al KS que se interrumpió
        op_code atendido = MSG_INTERRUPCION_ATENDIDA;
        enviar_mensaje(fd_ks, &atendido, sizeof(op_code));
        enviar_mensaje(fd_ks, &pid, sizeof(uint32_t));
        enviar_mensaje(fd_ks, &motivo, sizeof(int));

        return 0;
    }
    return -2;
}

int memory_management_unit(uint32_t direccion_logica, uint32_t tamanio_acceso, t_list* tabla_segmentos) {
    uint32_t num_segmento = direccion_logica / 256;
    uint32_t desplazamiento = direccion_logica % 256;

    t_segmento* segmento = NULL;

    for (int i = 0; i < list_size(tabla_segmentos); i++) {
        t_segmento* seg = list_get(tabla_segmentos, i);

        if (seg->id_segmento == num_segmento) {
            segmento = seg;
            break;
        }
    }
    if (segmento == NULL) 
        return MMU_ERROR;
    if (desplazamiento + tamanio_acceso > segmento->tamanio) {
        return MMU_ERROR;
    }
    return segmento->base + desplazamiento;
}

int lectura_ms (int direccion, int fd_ms){
    op_code lectura = MSG_READ;
    enviar_mensaje(fd_ms, &lectura, sizeof(op_code));

    enviar_mensaje(fd_ms, &direccion, sizeof(direccion));

    int size;
    int* dato_ptr = recibir_mensaje(fd_ms, &size);
    int dato = *dato_ptr;

    free(dato_ptr);
    return dato;
}

int escritura_ms(int direccion, uint32_t dato, uint32_t tamanio, int fd_ms){
    op_code escritura = MSG_WRITE;
    enviar_mensaje(fd_ms, &escritura, sizeof(op_code));

    enviar_mensaje(fd_ms, &direccion, sizeof(direccion));

    enviar_mensaje(fd_ms, &dato, tamanio);

    int size;
    op_code* confirmacion_ptr = recibir_mensaje(fd_ms, &size);
    if (confirmacion_ptr == NULL)
        return -1;
    if (*confirmacion_ptr != MSG_DONE) {
        free(confirmacion_ptr);
        return -1;
    }
    free(confirmacion_ptr);
    return 0;
}

