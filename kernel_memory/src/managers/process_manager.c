#include "process_manager.h"

#include <stdlib.h>
#include <commons/collections/dictionary.h>
#include <stdlib.h>
#include <string.h>
#include <commons/string.h>
#include <commons/collections/list.h>
#include "memory_manager.h"

static t_administrador_procesos administrador;

void inicializar_administrador_procesos(void) {
  administrador.procesos_por_pid = dictionary_create();
}

static char* pid_to_key(uint32_t pid) {
  return string_itoa(pid);
}

static t_contexto* crear_contexto_inicial(void) {
  t_contexto* contexto = malloc(sizeof(t_contexto));

  contexto->registros.pc = 0;

  contexto->registros.ax = 0;
  contexto->registros.bx = 0;
  contexto->registros.cx = 0;
  contexto->registros.dx = 0;

  contexto->registros.eax = 0;
  contexto->registros.ebx = 0;
  contexto->registros.ecx = 0;
  contexto->registros.edx = 0;

  contexto->registros.si = 0;
  contexto->registros.di = 0;

  contexto->tabla_segmentos = list_create();

  return contexto;
}

bool crear_proceso(uint32_t pid, char* script_path) {
  char* key = pid_to_key(pid);

  if (dictionary_has_key(administrador.procesos_por_pid, key)) {
    free(key);
    return false;
  }

  t_proceso_memoria* proceso = malloc(sizeof(t_proceso_memoria));

  proceso->pid = pid;
  proceso->script_path = strdup(script_path);
  
  proceso->contexto = crear_contexto_inicial();

  dictionary_put(administrador.procesos_por_pid, key, proceso);

  free(key);
  return true;
}

bool existe_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  bool existe = dictionary_has_key(administrador.procesos_por_pid, key);

  free(key);
  return existe;
}

t_proceso_memoria* obtener_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = dictionary_get(administrador.procesos_por_pid, key);

  free(key);
  return proceso;
}

t_segmento* obtener_segmento(uint32_t pid, uint32_t id_segmento) {
  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return NULL;
  }

  for (int i = 0; i < list_size(proceso->contexto->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->contexto->tabla_segmentos, i);

    if (segmento->id_segmento == id_segmento) {
      return segmento;
    }
  }

  return NULL;
}

t_resultado_crear_segmento crear_segmento(uint32_t pid, uint32_t id_segmento, uint32_t tamanio) {
  if (tamanio == 0) {
    return CREAR_SEGMENTO_TAMANIO_INVALIDO;
  }

  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return CREAR_SEGMENTO_PROCESO_INEXISTENTE;
  }

  if (obtener_segmento(pid, id_segmento) != NULL) {
    return CREAR_SEGMENTO_ID_REPETIDO;
  }

  if (!hay_hueco_contiguo(tamanio)) {
    if (requiere_compactacion(tamanio)) {  // Nico M: ¿No estamos chequeando dos veces lo mismo? requiere_compactacion() ya chequea si hay hueco contiguo.
      return CREAR_SEGMENTO_REQUIERE_COMPACTACION;
    }
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  uint32_t base = reservar_espacio(tamanio);

  if (base == UINT32_MAX) {
    return CREAR_SEGMENTO_SIN_MEMORIA;
  }

  t_segmento* segmento = malloc(sizeof(t_segmento));

  segmento->id_segmento = id_segmento;
  segmento->base = base;
  segmento->tamanio = tamanio;

  list_add(proceso->contexto->tabla_segmentos, segmento);

  return CREAR_SEGMENTO_OK;
}

t_list* obtener_todos_los_segmentos(void) {
  t_list* segmentos_ocupados = list_create();
  t_list* pids = dictionary_keys(administrador.procesos_por_pid); // Obtenemos una lista de los pids de los procesos.

  for (int i = 0; i < list_size(pids); i++) { // Por cada proceso indexado en el diccionario de procesos...
    char* key = list_get(pids, i);
    t_proceso_memoria* proceso = dictionary_get(administrador.procesos_por_pid, key); // Obtenemos un puntero al proceso correspondiente a cada pid.

    for (int j = 0; j < list_size(proceso->tabla_segmentos); j++) {  // Por cada segmento de la tabla de segmentos de cada proceso...
      t_segmento* segmento = list_get(proceso->tabla_segmentos, j);

      t_segmento_ocupado* ocupado = malloc(sizeof(t_segmento_ocupado));
      ocupado->proceso = proceso; // Indexamos un puntero al segmento y otro al proceso al que pertenece.
      ocupado->segmento = segmento;

      list_add(segmentos_ocupados, ocupado); // Añadimos el indice del segmento a la lista.
    }
  }

  list_destroy(pids); // Eliminamos de memoria la lista de pids pero no liberamos sus elementos.
  return segmentos_ocupados; // Devolvemos la lista de cada segmento ocupado en memoria.
}

bool destruir_proceso(uint32_t pid) {
  char* key = pid_to_key(pid);

  t_proceso_memoria* proceso = dictionary_remove(administrador.procesos_por_pid, key);

  free(key);

  if (proceso == NULL) return false;

  for (int i = 0; i < list_size(proceso->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->tabla_segmentos, i);
    liberar_espacio(segmento->base, segmento->tamanio);
  }

  destruir_proceso_memoria(proceso);

  return true;
}

bool eliminar_segmento(uint32_t pid, uint32_t id_segmento)
{
  t_proceso_memoria* proceso = obtener_proceso(pid);

  if (proceso == NULL) {
    return false;
  }

  for (int i = 0; i < list_size(proceso->tabla_segmentos); i++) {
    t_segmento* segmento = list_get(proceso->tabla_segmentos, i);

    if (segmento->id_segmento == id_segmento) {
      list_remove(proceso->tabla_segmentos, i);

      liberar_espacio(segmento->base, segmento->tamanio);
      free(segmento);

      return true;
    }
  }

  return false;
}

static void destruir_segmento(void* elemento) {
  t_segmento* segmento = elemento;
  free(segmento);
}

void destruir_segmento_ocupado(void* elemento)
{
    free(elemento);
}

list_destroy_and_destroy_elements(segmentos_ocupados, destruir_segmento_ocupado);

static void destruir_contexto(t_contexto* contexto) {
  free(contexto);
}

static void destruir_proceso_memoria(void* elemento) {
  t_proceso_memoria* proceso = elemento;

  free(proceso->script_path);

  list_destroy_and_destroy_elements(proceso->tabla_segmentos, destruir_segmento);

  destruir_contexto(proceso->contexto);

  free(proceso);
}

void destruir_administrador_procesos(void) {
  dictionary_destroy_and_destroy_elements( administrador.procesos_por_pid, destruir_proceso_memoria);
  administrador.procesos_por_pid = NULL;
}