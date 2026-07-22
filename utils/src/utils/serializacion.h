#ifndef SERIALIZACION_H_
#define SERIALIZACION_H_

#include <commons/log.h>
#include "../../utils/src/utils/tipos.h"

void escribir_en_buffer(void*buffer, uint32_t*desplazamiento, const void*dato,uint32_t tamanio);

void*serializar_contexto(t_contexto*contexto,int*tamanio_buffer,t_log*logger);
t_contexto*deserializar_contexto(void*buffer,int tamanio_buffer,t_log*logger);

#endif