#include "memory_manager.h"

#include <stdlib.h>
#include <commons/collections/list.h>
#include "../estructuras.h"
#include <string.h>
#include <stdint.h>
#include <utils/conexiones.h> // CP3: R/W físico a los Memory Sticks
#include <utils/mensajes.h>
#include "../../utils/src/utils/tipos.h"

static t_administrador_memoria administrador;

// forward declarations de funciones static (se usan antes de definirse)
static void registrar_hueco(uint32_t base, uint32_t tamanio);
static void insertar_hueco_ordenado(t_hueco* nuevo_hueco);
static void consolidar_huecos_contiguos(void);
static void destruir_hueco(void* elemento);

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

// CP3: devuelve el Memory Stick que contiene la dirección global dada (o NULL).
static t_memory_stick* stick_de_direccion(uint32_t dir_global) {
  for (int i = 0; i < list_size(administrador.memory_sticks); i++) {
    t_memory_stick* s = list_get(administrador.memory_sticks, i);
    if (dir_global >= s->base_global && dir_global < s->base_global + s->tamanio)
      return s;
  }
  return NULL;
}

// CP3: escribe `tamanio` bytes desde `datos` a partir de la dir global, partiendo
// el pedido entre los Memory Sticks involucrados (cada stick direcciona desde 0).
bool escribir_memoria_fisica(uint32_t dir_global, uint32_t tamanio, void* datos) {
  uint32_t restante = tamanio;
  uint32_t cursor = dir_global;
  uint8_t* origen = datos;

  while (restante > 0) {
    t_memory_stick* s = stick_de_direccion(cursor);
    if (s == NULL) return false;

    uint32_t offset_local = cursor - s->base_global;
    uint32_t espacio_en_stick = s->tamanio - offset_local;
    uint32_t a_escribir = restante < espacio_en_stick ? restante : espacio_en_stick;

    op_code orden = MSG_WRITE;
    enviar_mensaje(s->socket, &orden, sizeof(op_code));
    int dir_fisica = (int) offset_local;
    enviar_mensaje(s->socket, &dir_fisica, sizeof(int));
    enviar_mensaje(s->socket, origen, (int) a_escribir);

    int size;
    op_code* done = recibir_mensaje(s->socket, &size);
    if (done == NULL || *done != MSG_DONE) { free(done); return false; }
    free(done);

    cursor += a_escribir;
    origen += a_escribir;
    restante -= a_escribir;
  }
  return true;
}

// CP3: lee `tamanio` bytes a partir de la dir global hacia `buffer_out`, partiendo
// el pedido entre los Memory Sticks involucrados y consolidando el resultado.
bool leer_memoria_fisica(uint32_t dir_global, uint32_t tamanio, void* buffer_out) {
  uint32_t restante = tamanio;
  uint32_t cursor = dir_global;
  uint8_t* destino = buffer_out;

  while (restante > 0) {
    t_memory_stick* s = stick_de_direccion(cursor);
    if (s == NULL) return false;

    uint32_t offset_local = cursor - s->base_global;
    uint32_t espacio_en_stick = s->tamanio - offset_local;
    uint32_t a_leer = restante < espacio_en_stick ? restante : espacio_en_stick;

    op_code orden = MSG_READ;
    enviar_mensaje(s->socket, &orden, sizeof(op_code));
    int dir_fisica = (int) offset_local;
    enviar_mensaje(s->socket, &dir_fisica, sizeof(int));
    int len = (int) a_leer;
    enviar_mensaje(s->socket, &len, sizeof(int));

    int size;
    void* datos = recibir_mensaje(s->socket, &size);
    if (datos == NULL) return false;
    memcpy(destino, datos, a_leer);
    free(datos);

    cursor += a_leer;
    destino += a_leer;
    restante -= a_leer;
  }
  return true;
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