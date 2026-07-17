#ifndef MEMORY_MANAGER_H_
#define MEMORY_MANAGER_H_

#include <stdint.h>
#include <stdbool.h>
#include "../estructuras.h" // t_estrategia_asignacion

void inicializar_administrador_memoria(t_estrategia_asignacion estrategia);
void destruir_administrador_memoria(void);

void conectar_memory_stick(
    const char* ip,
    const char* puerto,
    uint32_t tamanio,
    int socket);

uint32_t reservar_espacio(uint32_t tamanio);
void liberar_espacio(uint32_t base, uint32_t tamanio);

bool hay_espacio_total(uint32_t tamanio);
bool hay_hueco_contiguo(uint32_t tamanio);
bool requiere_compactacion(uint32_t tamanio);

uint32_t obtener_memoria_total(void);
uint32_t obtener_memoria_libre_total(void);

void reconstruir_huecos_desde(uint32_t base); // Marca todo el espacio de memoria desde la base hasta el fin de memoria como hueco libre.

// CP3: lectura/escritura física a los Memory Sticks (partiendo por stick si hace falta)
bool escribir_memoria_fisica(uint32_t dir_global, uint32_t tamanio, void* datos);
bool leer_memoria_fisica(uint32_t dir_global, uint32_t tamanio, void* buffer_out);
uint32_t obtener_cantidad_memory_sticks(void);

bool obtener_info_memory_stick(uint32_t indice, t_info_memory_stick* info);

#endif