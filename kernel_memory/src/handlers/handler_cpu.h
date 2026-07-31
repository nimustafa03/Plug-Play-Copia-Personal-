#ifndef HANDLER_CPU_H_
#define HANDLER_CPU_H_

#include "../estructuras.h"
#include "../../utils/src/utils/tipos.h"
#include <stdint.h>
#include <commons/log.h>
#include "../../utils/src/utils/mensajes.h"

void atender_cpu(int fd_cpu);
void enviar_contexto_ejecucion_a_cpu(int fd_cpu, t_contexto*contexto);
op_code*esperar_pedido_de_instruccion(int fd_cpu);
uint32_t recibir_pc(int fd_cpu);
void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR); // Envia un OK o un ERROR a CPU. True = OK, false = ERROR.
void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion);
bool notificar_mapa_memory_sticks_a_cpu(int fd_cpu);
bool notificar_mapa_memory_sticks_a_todas_las_cpus(void);
bool notificar_segmentos_a_cpu(int fd_cpu, t_contexto*proceso);
void atender_mensaje_cpu(int fd_cpu);
uint32_t recibir_pid(int fd_cpu);
t_contexto *recibir_contexto(int fd_cpu);

#endif