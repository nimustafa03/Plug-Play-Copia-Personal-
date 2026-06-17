#ifndef TIPOS_H
#define TIPOS_H

#include <stdint.h>
#include <commons/collections/list.h>

typedef struct {
    uint8_t AX, BX, CX, DX;
    uint32_t EAX, EBX, ECX, EDX;
    uint32_t SI, DI;
    uint32_t PC;
} t_registros;

typedef struct {
    uint32_t pid;
    t_registros registros;
    char** instrucciones;
    int cantidad_instrucciones;
    t_list* tabla_segmentos; // CP3
    bool proximo_a_detener;
} t_contexto_ejecucion;

typedef struct {
    uint32_t pid;
    int motivo;
} t_interrupcion;

typedef struct {
    int fd_cpu;
    t_contexto_ejecucion* contexto;
} t_args_proceso;

#endif // TIPOS_H