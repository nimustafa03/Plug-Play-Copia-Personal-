#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <commons/string.h>   // string_itoa
#include <commons/collections/dictionary.h>
#include "kernel_memory.h"

t_contexto_ejecucion* crear_proceso(uint32_t pid, char*path, t_dictionary*diccionario){
    // INICIALIZAMOS ESTRUCTURA
    t_contexto_ejecucion* contexto_ejecucion = malloc(sizeof(t_contexto_ejecucion));
    contexto_ejecucion->pid = pid;
    contexto_ejecucion->proximo_a_detener=false;
    // INICIALIZAR REGISTROS EN CERO
    memset(&contexto_ejecucion->registros, 0, sizeof(t_registros));
    // CARGAR INSTRUCCIONES
    contexto_ejecucion->instrucciones = cargar_instrucciones(path);
    contexto_ejecucion->cantidad_instrucciones = contar_lineas_en_cadena(contexto_ejecucion->instrucciones);
    contexto_ejecucion->tabla_segmentos = NULL; // CP3
    // GUARDAMOS EN DICCIONARIO EL PROCESO
    char* key = string_itoa(pid);
    dictionary_put(diccionario, key, contexto_ejecucion);
    free(key);
    
    return contexto_ejecucion;
}

int contar_lineas_en_cadena(char** cadena){
    int lineas_totales = 0;
    for (int i=0; cadena[i] != NULL; i++){
        lineas_totales++;
    }
    return lineas_totales;
}

char** cargar_instrucciones(char* path) {
    FILE* archivo = fopen(path, "r");
    if (!archivo) return NULL;

    // contar lineas primero
    int lineas = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), archivo)) lineas++;
    rewind(archivo);

    // alocar array de punteros + centinela NULL
    char** instrucciones = malloc((lineas + 1) * sizeof(char*));
    for (int i = 0; i < lineas; i++) {
        fgets(buf, sizeof(buf), archivo);
        // sacar el \n del final si existe
        buf[strcspn(buf, "\n")] = '\0';
        instrucciones[i] = strdup(buf);
    }
    instrucciones[lineas] = NULL; // centinela

    fclose(archivo);
    return instrucciones;
}

char*devolver_instruccion(uint32_t pc,char**lista_instrucciones){
    char*instruccion;
    int contador = 0; // NICO M: Según los ejemplos, el PC tomaría la primera linea de una lista de instrucciones como 1.
    char**copia_lista_instrucciones = string_duplicate(lista_instrucciones); // NICO M: CREO que string_split() rompe el string que se le pase. No queremos que la lista de instrucciones se rompa.
    char* tokenizado = string_split(copia_lista_instrucciones,"\n"); 
    free(copia_lista_instrucciones);
    do
    {
        instruccion = tokenizado[contador-1];
        contador++;
    } while (contador-1 != pc && tokenizado[contador-1] != NULL); // NICO M: Nos movemos por el array tokenizado hasta donde nos indique el PC, siempre y cuando no nos encontremos en un espacio inválido, lo que indicaría que el PC se sale del rango de la lista.
    string_array_destroy(tokenizado); // NICO M: Eliminamos el tokenizado, para liberar memoria.

    return instruccion;
}