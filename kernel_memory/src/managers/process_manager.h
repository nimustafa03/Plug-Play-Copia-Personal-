#ifndef PROCESS_MANAGER_H_
#define PROCESS_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <commons/collections/list.h>
#include "../estructuras.h"
#include "memory_manager.h"

void inicializar_administrador_procesos(void);
void destruir_administrador_procesos(void);

bool crear_proceso(uint32_t pid, char* script_path, int fd_cpu);
bool destruir_proceso(uint32_t pid);

bool existe_proceso(uint32_t pid);
t_proceso_memoria* obtener_proceso(uint32_t pid);

t_resultado_crear_segmento crear_segmento(
    uint32_t pid,
    uint32_t id_segmento,
    uint32_t tamanio
);

void*manejar_proceso(void*arg);
char*devolver_instruccion(uint32_t pc, char*lista_instrucciones);

bool eliminar_segmento(uint32_t pid, uint32_t id_segmento);

t_segmento* obtener_segmento(uint32_t pid, uint32_t id_segmento);
t_list* obtener_todos_los_segmentos(void); // Devuelve una lista con punteros a indices que guardan punteros a cada segmento en memoria y otro puntero a su proceso.

#endif