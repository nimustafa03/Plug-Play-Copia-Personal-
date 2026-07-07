#ifndef MEMORY_MANAGER_H_
#define MEMORY_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>

void inicializar_administrador_memoria(char* estrategia);
void destruir_administrador_memoria(void);

void conectar_memory_stick(uint32_t tamanio, int socket);

uint32_t reservar_espacio(uint32_t tamanio);
void liberar_espacio(uint32_t base, uint32_t tamanio);

bool hay_espacio_total(uint32_t tamanio);
bool hay_hueco_contiguo(uint32_t tamanio);
bool requiere_compactacion(uint32_t tamanio);

uint32_t obtener_memoria_total(void);
uint32_t obtener_memoria_libre_total(void);

void conectar_memory_stick(uint32_t tamanio, int socket);

static t_hueco* buscar_best_fit(uint32_t tamanio);
static t_hueco* buscar_worst_fit(uint32_t tamanio);

void reconstruir_huecos_desde(uint32_t base); // Marca todo el espacio de memoria desde la base hasta el fin de memoria como hueco libre.

#endif