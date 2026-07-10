#ifndef TIPOS_H
#define TIPOS_H

#include <stdint.h>
#include <commons/collections/list.h>

typedef struct {
    uint32_t pid;
    int motivo;
} t_interrupcion;

typedef struct {
    uint32_t pc;

    uint8_t ax;
    uint8_t bx;
    uint8_t cx;
    uint8_t dx;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    uint32_t si;
    uint32_t di;
} t_registros;
typedef struct {
    t_registros registros;
    t_list* tabla_segmentos;
    bool proximo_a_detener;
} t_contexto;

typedef struct {
    uint32_t pid;
    char* script_path;
    t_contexto* contexto;
} t_proceso_memoria;
typedef struct {
    int fd_cpu;
    t_proceso_memoria* proceso;
} t_args_proceso;

#endif // TIPOS_H