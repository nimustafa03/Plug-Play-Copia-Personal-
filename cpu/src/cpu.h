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

typedef struct {
    char* ip;
    char* puerto;
    uint32_t base_global;
    uint32_t tamanio;
} t_info_memory_stick_cpu;

typedef struct {
    uint32_t cantidad;
    t_info_memory_stick_cpu memory_sticks[4];
} t_mapa_memory_sticks_cpu;

// CICLO DE INSTRUCCION

extern int ms_conectados;
extern int fd_ms_agregados[3];
char* fetch(int conexion_servidor,uint32_t pid, t_registros* cpu);
operacion decode(char* instruccion);
int memory_management_unit(uint32_t direccion_logica, uint32_t tamanio_acceso, t_list* tabla_segmentos);
int atender_interrupcion(int fd_ks,int fd_km,t_contexto* contexto, uint32_t pid, t_log* logger_cpu);
void destruir_mapa_memory_sticks(t_mapa_memory_sticks_cpu* mapa);
t_mapa_memory_sticks_cpu* recibir_mapa(int fd_km, t_log* logger_cpu);
int actualizar_conexiones_ms(t_info_memory_stick_cpu* info_ms,t_log* logger_cpu);
int conectar_memory_sticks_faltantes(t_mapa_memory_sticks_cpu* mapa,t_log* logger_cpu);
int buscar_indice_ms(uint32_t direccion_global,t_mapa_memory_sticks_cpu* mapa);
int obtener_fd_ms(uint32_t indice_ms,int fd_ms,int fd_ms_agregados[3]);
void* lectura_ms(uint32_t direccion_global,uint32_t tamanio_lectura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3]);
int escritura_ms(uint32_t direccion_global,void* buffer_origen,uint32_t tamanio_escritura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3]);
uint32_t obtener_valor(char* posicion, t_registros* registro);
void escribir_registro(char* posicion,t_registros* registro,uint32_t valor);
uint32_t tamanio_registro(char* nombre_registro);

// CP3 -> Para las syscalls del mutex necesita tambien el fd_ks y el PID
int execute(operacion codigo, char* instruccion, t_registros* cpu, int fd_ks, int fd_km, int fd_ms, uint32_t pid, t_list* tabla_segmentos, t_log* logger_cpu,t_mapa_memory_sticks_cpu* mapa,int fd_ms_agregados[3]);

void set(char* instruccion, t_registros* registro);
void sum(char* instruccion, t_registros* registro);
void sub(char* instruccion, t_registros* registro);
int mov_in(char* instruccion,t_registros* registros,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3],t_list* tabla_segmentos);
int mov_out(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3]);
void jnz(char* instruccion, t_registros* registro);
int copy_mem(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa_ms,int fd_ms, int fd_ms_agregados[3],uint32_t pid,t_log* logger_cpu);
void noop(t_registros* registro);
void syscall_init_proc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid);
int syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
int syscall_mutex_lock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
int syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
void syscall_sleep(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
int syscall_stdin(char* instruccion,t_registros* registro, int fd_ks, uint32_t pid);
int syscall_stdout(char* instruccion,t_registros* registro, int fd_ks, uint32_t pid);
int syscall_mem_alloc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid);
int syscall_mem_free(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid);
int syscall_exit(int fd_ks, uint32_t pid);
t_contexto* deserializar_contexto_inicial(const void* buffer,int tamanio_buffer);

#endif