#ifndef HANDLER_CPU_H_
#define HANDLER_CPU_H_

#include "../estructuras.h"
#include <stdint.h>

void atender_cpu(int fd_cpu);
void enviar_contexto_ejecucion_a_cpu(int fd_cpu, t_contexto contexto);
bool esperar_pedido_de_instruccion(int fd_cpu);
uint32_t recibir_pc(int fd_cpu);
void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR); // Envia un OK o un ERROR a CPU. True = OK, false = ERROR.
void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion);

#endif