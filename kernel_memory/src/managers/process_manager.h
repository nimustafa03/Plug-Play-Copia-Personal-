#ifndef PROCESS_MANAGER_H_
#define PROCESS_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include <commons/collections/list.h>
#include "../estructuras.h"
#include "memory_manager.h"

void inicializar_administrador_procesos(void);
void destruir_administrador_procesos(void);

bool crear_proceso(uint32_t pid, char*path);
bool inicializar_proceso(uint32_t pid, int fd_cpu);
bool destruir_proceso(uint32_t pid);
bool existe_proceso(uint32_t pid);
t_proceso_memoria* obtener_proceso(uint32_t pid);
void*manejar_proceso(void*arg);

t_resultado_crear_segmento crear_segmento(
    uint32_t pid,
    uint32_t id_segmento,
    uint32_t tamanio
);
bool eliminar_segmento(uint32_t pid, uint32_t id_segmento);
t_segmento* obtener_segmento(uint32_t pid, uint32_t id_segmento);
t_list* obtener_todos_los_segmentos(void); // Devuelve una lista con punteros a indices que guardan punteros a cada segmento en memoria y otro puntero a su proceso.



char*devolver_instruccion(uint32_t pc, char*lista_instrucciones);



// CP3: traducción de dirección lógica a física (STDIN/STDOUT)
typedef enum {
  TRADUCCION_OK,
  TRADUCCION_SEG_FAULT,
  TRADUCCION_INEXISTENTE
} t_resultado_traduccion;

int traducir_direccion(uint32_t pid, uint32_t dir_logica, uint32_t tamanio, uint32_t* dir_global_out);

#endif