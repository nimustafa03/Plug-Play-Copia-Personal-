#include "memory_manager.h"

#include <stdlib.h>
#include <commons/collections/list.h>
#include "../estructuras.h"
#include <string.h>
#include <stdint.h>

static t_administrador_memoria administrador;

void inicializar_administrador_memoria(t_estrategia_asignacion estrategia) {
  administrador.memory_sticks = list_create();
  administrador.huecos_libres = list_create();
  administrador.memoria_total = 0;
  administrador.proximo_id_stick = 0;
  administrador.estrategia = estrategia;
}

void conectar_memory_stick(uint32_t tamanio, int socket)
{
    uint32_t base = administrador.memoria_total;

    t_memory_stick* stick = malloc(sizeof(t_memory_stick));
    stick->id = administrador.proximo_id_stick++;
    stick->base_global = base;
    stick->tamanio = tamanio;
    stick->socket = socket;

    list_add(administrador.memory_sticks, stick);

    administrador.memoria_total += tamanio;

    registrar_hueco(base, tamanio);
}

static void registrar_hueco(uint32_t base, uint32_t tamanio) {
  if (tamanio == 0) return;

  t_hueco* nuevo_hueco = malloc(sizeof(t_hueco));
  nuevo_hueco->base = base;
  nuevo_hueco->tamanio = tamanio;

  insertar_hueco_ordenado(nuevo_hueco);
  consolidar_huecos_contiguos();
}

static void insertar_hueco_ordenado(t_hueco* nuevo_hueco) {
  int posicion = 0;

  while (posicion < list_size(administrador.huecos_libres)) {
      t_hueco* hueco_actual = list_get(administrador.huecos_libres, posicion);

      if (nuevo_hueco->base < hueco_actual->base) {
          break;
      }

      posicion++;
  }

  list_add_in_index(administrador.huecos_libres, posicion, nuevo_hueco);
}

static void consolidar_huecos_contiguos(void) {
  int i = 0;

  while (i < list_size(administrador.huecos_libres) - 1) {
      t_hueco* actual = list_get(administrador.huecos_libres, i);
      t_hueco* siguiente = list_get(administrador.huecos_libres, i + 1);

      uint32_t fin_actual = actual->base + actual->tamanio;

      if (fin_actual == siguiente->base) {
          actual->tamanio += siguiente->tamanio;

          t_hueco* eliminado = list_remove(administrador.huecos_libres, i + 1);
          free(eliminado);
      } else {
          i++;
      }
  }
}

static t_hueco* buscar_best_fit(uint32_t tamanio) {
  t_hueco* mejor_hueco = NULL;

  for (int i = 0; i < list_size(administrador.huecos_libres); i++) {
      t_hueco* hueco = list_get(administrador.huecos_libres, i);

      if (hueco->tamanio >= tamanio) {
          if (mejor_hueco == NULL || hueco->tamanio < mejor_hueco->tamanio) {
              mejor_hueco = hueco;
          }
      }
  }

  return mejor_hueco;
}

static t_hueco* buscar_worst_fit(uint32_t tamanio){
  t_hueco* peor_hueco = NULL;

  for (int i = 0; i < list_size(administrador.huecos_libres); i++) {
    t_hueco* hueco = list_get(administrador.huecos_libres, i);

    if (hueco->tamanio >= tamanio) {
      if (peor_hueco == NULL || hueco->tamanio > peor_hueco->tamanio) {
        peor_hueco = hueco;
      }
    }
  }
  return peor_hueco;
}

static t_hueco* buscar_hueco_candidato(uint32_t tamanio) {
  switch (administrador.estrategia) {
    case BEST_FIT:
      return buscar_best_fit(tamanio);

    case WORST_FIT:
      return buscar_worst_fit(tamanio);

    default:
      return NULL;
  }
}

uint32_t reservar_espacio(uint32_t tamanio) {
  if (tamanio == 0) {
    return UINT32_MAX;
  }

  t_hueco* hueco = buscar_hueco_candidato(tamanio);

  if (hueco == NULL) {
      return UINT32_MAX;
  }

  uint32_t base_asignada = hueco->base;

  hueco->base += tamanio;
  hueco->tamanio -= tamanio;

  if (hueco->tamanio == 0) {
    list_remove_element(administrador.huecos_libres, hueco);
    free(hueco);
  }

  return base_asignada;
}

bool hay_espacio_total(uint32_t tamanio)
{
  return obtener_memoria_libre_total() >= tamanio;
}

bool hay_hueco_contiguo(uint32_t tamanio) {
  return buscar_hueco_candidato(tamanio) != NULL;
}

bool requiere_compactacion(uint32_t tamanio) {
  return hay_espacio_total(tamanio) && !hay_hueco_contiguo(tamanio);
}

uint32_t obtener_memoria_total(void) {
  return administrador.memoria_total;
}

uint32_t obtener_memoria_libre_total(void) {
  uint32_t total_libre = 0;

  for (int i = 0; i < list_size(administrador.huecos_libres); i++) {
    t_hueco* hueco = list_get(administrador.huecos_libres, i);
    total_libre += hueco->tamanio;
  }

  return total_libre;
}

void liberar_espacio(uint32_t base, uint32_t tamanio) {
  registrar_hueco(base, tamanio);
}

static void limpiar_huecos(void) {
  list_clean_and_destroy_elements(administrador.huecos_libres, destruir_hueco);
}

void reconstruir_huecos_desde(uint32_t base) {
  limpiar_huecos();

  if (base >= administrador.memoria_total) {
    return;
  }

  uint32_t tamanio_libre = administrador.memoria_total - base;

  registrar_hueco(base, tamanio_libre);
}

static void destruir_memory_stick(void* elemento) {
  t_memory_stick* stick = elemento;
  free(stick);
}

static void destruir_hueco(void* elemento) {
  t_hueco* hueco = elemento;
  free(hueco);
}

void destruir_administrador_memoria(void) {
  list_destroy_and_destroy_elements(administrador.memory_sticks, destruir_memory_stick);
  list_destroy_and_destroy_elements(administrador.huecos_libres, destruir_hueco);
  administrador.memory_sticks = NULL;
  administrador.huecos_libres = NULL;
  administrador.memoria_total = 0;
  administrador.proximo_id_stick = 0;
}


