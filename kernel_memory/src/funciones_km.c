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

char* devolver_instruccion(uint32_t pc, char** lista_instrucciones) {
    if (lista_instrucciones == NULL || lista_instrucciones[pc] == NULL)
        return NULL;
    return strdup(lista_instrucciones[pc]);
}