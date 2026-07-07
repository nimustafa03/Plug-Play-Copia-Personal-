#include "service_compactacion.h"

#include <stdint.h>
#include <stdlib.h>
#include <commons/collections/list.h>

#include "../estructuras.h"
#include "../managers/process_manager.h"
#include "../managers/memory_manager.h"

static bool segmento_ocupado_menor_base(void* a, void* b) {
  t_segmento_ocupado* seg_a = a;
  t_segmento_ocupado* seg_b = b;

  return seg_a->segmento->base < seg_b->segmento->base;
}

static void destruir_segmento_ocupado(void* elemento) {
  t_segmento_ocupado* segmento_ocupado = elemento;

  free(segmento_ocupado);
}

void ejecutar_compactacion(void) {
  t_list* segmentos_ocupados = obtener_todos_los_segmentos(); // Obtenemos una lista de punteros a indices de segmentos y sus procesos.

  list_sort(segmentos_ocupados, segmento_ocupado_menor_base); // Los ordenamos de menor a mayor base.

  uint32_t cursor = 0;

  for (int i = 0; i < list_size(segmentos_ocupados); i++) { // Por cada segmento...
    t_segmento_ocupado* ocupado = list_get(segmentos_ocupados, i);
    t_segmento* segmento = ocupado->segmento;

    segmento->base = cursor; // Establecemos su base al valor del cursor.
    cursor += segmento->tamanio; // Movemos el cursor al final del segmento.
  }

  reconstruir_huecos_desde(cursor);

  list_destroy_and_destroy_elements(
    segmentos_ocupados,
    destruir_segmento_ocupado
  );
}