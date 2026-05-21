#ifndef KERNELMEMORY_H
#define KERNELMEMORY_H

#include <stdint.h>
#include <commons/collections/list.h>
#include <utils/tipos.h>

char** cargar_instrucciones(char*path); // NICO M: Devuelve como cadena de caracteres el archivo de pseudocódigo.

char* devolver_instruccion(uint32_t pc, char*lista_instrucciones); // NICO M: Devuelve una cadena con la próxima instrucción basada en el pc.

#endif // KERNELMEMORY_H