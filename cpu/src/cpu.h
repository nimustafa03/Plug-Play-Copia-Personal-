#ifndef CPUS_H
#define CPUS_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <stdint.h>
#include <utils/tipos.h>
#include <utils/serializacion.h>

#define SEG_FAULT (-5)

// OPCODES
typedef enum {
    OP_NOOP =100,
    OP_SET =101,
    OP_MOV_IN =102,
    OP_MOV_OUT =103,
    OP_SUM =104,
    OP_SUB =105,
    OP_JNZ =106,
    OP_COPY_MEM =107,
    OP_MUTEX_CREATE =108,
    OP_MUTEX_LOCK =109,
    OP_MUTEX_UNLOCK =110,
    OP_SLEEP =111,
    OP_STDIN =112,
    OP_STDOUT =113,
    OP_INVALID =114,
    OP_MEM_ALLOC =115,
    OP_MEM_FREE =116,
    OP_INIT_PROC =117,
    OP_EXIT =118,
} operacion;

// STRUCTS

typedef struct {
    uint32_t pid;
    uint32_t pc;
} t_fetch;
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
char* fetch(int conexion_servidor,uint32_t pid, t_registros* cpu, t_log* logger_cpu);
operacion decode(char* instruccion);
int execute(operacion codigo, char* instruccion, t_registros* cpu, int fd_ks, int fd_km, int fd_ms, uint32_t pid, t_list* tabla_segmentos, t_log* logger_cpu,t_mapa_memory_sticks_cpu* mapa,int fd_ms_agregados[3], t_contexto* contexto);
int atender_interrupcion(int fd_ks,int fd_km,t_contexto* contexto, uint32_t pid, t_log* logger_cpu);
int hay_mensaje_completo(int fd, t_log* logger_cpu);
void atender_interrupcion_en_espera (t_interrupcion* interrupcion_entrante, uint32_t pid,int fd_ks,int fd_km,t_contexto* contexto,t_log* logger_cpu);

// MMU
int memory_management_unit(uint32_t direccion_logica, uint32_t tamanio_acceso, t_list* tabla_segmentos, t_log* logger_cpu);
void* lectura_ms(uint32_t direccion_global,uint32_t tamanio_lectura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3], t_log* logger_cpu);
void escritura_ms(uint32_t direccion_global,void* buffer_origen,uint32_t tamanio_escritura,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3], t_log* logger_cpu);

// DEFINICIONES
extern uint32_t segment_max_size;
extern int ms_conectados;
extern int fd_ms_agregados[3];
extern t_interrupcion* interrupcion_entrante;
extern bool interrupcion_en_espera;

// MANEJO DE INFORMACION
t_mapa_memory_sticks_cpu* recibir_mapa(int fd_km, t_log* logger_cpu);
void destruir_mapa_memory_sticks(t_mapa_memory_sticks_cpu* mapa);
int conectar_memory_sticks_faltantes(t_mapa_memory_sticks_cpu* mapa,t_log* logger_cpu, int identificador_cpu);
int actualizar_conexiones_ms(t_info_memory_stick_cpu* info_ms,t_log* logger_cpu, int identificador_cpu);
int buscar_indice_ms(uint32_t direccion_global,t_mapa_memory_sticks_cpu* mapa);
int obtener_fd_ms(uint32_t indice_ms,int fd_ms,int fd_ms_agregados[3]);

void escribir_en_buffer(void* buffer,uint32_t* desplazamiento,const void* dato,uint32_t tamanio);
t_list* deserializar_tabla_segmentos(void* buffer, int tamanio_buffer);

void actualizar_tabla_segmentos(t_contexto* contexto,int fd_km,t_log* logger_cpu);
void manejar_seg_fault(int fd_ks, uint32_t pid, t_log* logger_cpu, int fd_km);

// CONTEXTOS
void enviar_contexto(t_contexto* contexto, int fd_km, t_log* logger_cpu);
void destruir_contexto(t_contexto* contexto);
void guardar_contexto_km(int fd_km, t_contexto* contexto, uint32_t pid, t_log* logger_cpu); 


// OPERACIONES CON REGISTROS
uint32_t obtener_valor(char* posicion, t_registros* registro);
void escribir_registro(char* posicion,t_registros* registro,uint32_t valor);
uint32_t tamanio_registro(char* nombre_registro);

// INSTRUCCIONES
void set(char* instruccion, t_registros* registro);
void sum(char* instruccion, t_registros* registro);
void sub(char* instruccion, t_registros* registro);
int mov_in(char* instruccion,t_registros* registros,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3],t_list* tabla_segmentos, t_log* logger_cpu, uint32_t pid);
int mov_out(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa,int fd_ms,int fd_ms_agregados[3], t_log* logger_cpu, uint32_t pid);
void jnz(char* instruccion, t_registros* registro);
int copy_mem(char* instruccion,t_registros* registros,t_list* tabla_segmentos,t_mapa_memory_sticks_cpu* mapa_ms,int fd_ms, int fd_ms_agregados[3],uint32_t pid,t_log* logger_cpu);
void noop(t_registros* registro);

// SYSCALLS
void syscall_init_proc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid, t_log* logger_cpu);
int syscall_mutex_create(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu);
// FIX: ahora puede terminar en bloqueo (devuelve 2), por eso necesita fd_km y
// contexto para poder guardar el contexto en KM antes de soltar la CPU.
int syscall_mutex_lock(char* instruccion, int fd_ks, int fd_km, uint32_t pid, t_registros* cpu, t_contexto* contexto, t_log* logger_cpu);
int syscall_mutex_unlock(char* instruccion, int fd_ks, uint32_t pid, t_registros* cpu, t_log* logger_cpu);
int syscall_sleep(char* instruccion, int fd_ks, int fd_km, uint32_t pid, t_registros* cpu, t_contexto* contexto, t_log* logger_cpu);
int syscall_stdin(char* instruccion, t_registros* registros, int fd_ks, int fd_km, uint32_t pid, t_contexto* contexto, t_log* logger_cpu);
int syscall_stdout(char* instruccion,t_registros* registro, int fd_ks, int fd_km, uint32_t pid, t_contexto* contexto, t_log* logger_cpu);
void syscall_mem_alloc(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid, t_log* logger_cpu);
void syscall_mem_free(char* instruccion, t_registros* registro, int fd_ks, uint32_t pid, t_log* logger_cpu);
void syscall_exit(int fd_km, int fd_ks, t_contexto* contexto, uint32_t pid, t_log* logger_cpu);

#endif