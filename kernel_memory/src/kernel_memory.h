#ifndef KERNELMEMORY_H
#define KERNELMEMORY_H

#include <stdint.h>
#include <commons/collections/list.h>
#include <utils/tipos.h>

//Declaraciones
t_contexto_ejecucion* crear_proceso(uint32_t pid, char* path, t_dictionary* diccionario);
char** cargar_instrucciones(char*path); // NICO M: Devuelve como cadena de caracteres el archivo de pseudocódigo.
int contar_lineas_en_cadena(char** cadena);
char* devolver_instruccion(uint32_t pc, char** lista_instrucciones); // NICO M: Devuelve una cadena con la próxima instrucción basada en el pc.

#endif // KERNELMEMORY_H