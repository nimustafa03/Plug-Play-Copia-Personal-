#ifndef ESTRUCTURAS_H_
#define ESTRUCTURAS_H_

#include <stdint.h>
#include <stdbool.h>
#include <commons/collections/list.h>
#include <commons/collections/dictionary.h>
#include "../../utils/src/utils/tipos.h"

typedef struct {
    uint32_t id_segmento;
    uint32_t base;
    uint32_t tamanio;
} t_segmento;

typedef struct {
    uint32_t base;
    uint32_t tamanio;
} t_hueco;

typedef struct {
    uint32_t id;
    char* ip;
    char* puerto;
    uint32_t base_global;
    uint32_t tamanio;
    int socket;
} t_memory_stick;

typedef struct {
    char* ip;
    char* puerto;
    uint32_t base_global;
    uint32_t tamanio;
} t_info_memory_stick;


typedef enum {
    CREAR_SEGMENTO_OK,
    CREAR_SEGMENTO_SIN_MEMORIA,
    CREAR_SEGMENTO_REQUIERE_COMPACTACION,
    CREAR_SEGMENTO_REQUIERE_COMPACTACION_DESALOJO_FALLO,
    CREAR_SEGMENTO_PROCESO_INEXISTENTE,
    CREAR_SEGMENTO_ID_REPETIDO,
    CREAR_SEGMENTO_TAMANIO_INVALIDO
} t_resultado_crear_segmento;

typedef enum {
    DESALOJO_OK,
    DESALOJO_ERROR_SIN_RESPUESTA,
    DESALOJO_ERROR_RESPUESTA_INESPERADA
} t_resultado_solicitud_desalojo;
typedef enum {
    BEST_FIT,
    WORST_FIT
} t_estrategia_asignacion;

typedef struct {
    t_list* memory_sticks;
    t_list* huecos_libres;
    uint32_t memoria_total;
    uint32_t proximo_id_stick;
    t_estrategia_asignacion estrategia;
} t_administrador_memoria;

typedef struct {
    t_dictionary* procesos_por_pid;
} t_administrador_procesos;

typedef enum {
    MEMORIA_DISPONIBLE,
    MEMORIA_REQUIERE_COMPACTACION,
    MEMORIA_INSUFICIENTE
} t_estado_memoria;

typedef struct
{
    t_proceso_memoria* proceso;
    t_segmento* segmento;
} t_segmento_ocupado;



#endif