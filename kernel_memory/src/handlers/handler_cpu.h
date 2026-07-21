#ifndef HANDLER_CPU_H_
#define HANDLER_CPU_H_

#include "../estructuras.h"
#include "../../utils/src/utils/tipos.h"
#include <stdint.h>

void atender_cpu(int fd_cpu);
void enviar_contexto_ejecucion_a_cpu(int fd_cpu, void*contexto, int tamanio_buffer);
bool esperar_pedido_de_instruccion(int fd_cpu);
uint32_t recibir_pc(int fd_cpu);
void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR); // Envia un OK o un ERROR a CPU. True = OK, false = ERROR.
void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion);
bool cpu_esta_conectada(void);
bool notificar_mapa_memory_sticks_a_cpu(void);
void*serializar_contexto_inicial(t_contexto*contexto, int*tamanio_contexto);
void atender_mensaje_cpu();

#endif