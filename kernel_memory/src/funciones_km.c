#ifndef FUNCIONES_KM
#define FUNCIONES_KM
#include <commons/collections/dictionary.h>
#include <kernel_memory.h>
#include <commons/string.h>
#include <pthread.h>
#include <stdint.h>

void crear_proceso(uint32_t pid,char*path, t_dictionary*diccionario){
    // INICIALIZAMOS ESTRUCTURA
    t_contexto_ejecucion* contexto_ejecucion;
    contexto_ejecucion = new t_contexto_ejecucion();
    contexto_ejecucion->pid = pid;
    // INICIALIZAR REGISTROS EN CERO
    t_registros* registros;
    registros = new t_registros();
    registros->AX=0;registros->BX=0;registros->CX=0;registros->DX=0;
    registros->EAX=0;registros->EBX=0;registros->ECX=0;registros->EDX=0;
    registros->SI=0;registros->DI=0;registros->PC=0;
    // CARGAR INSTRUCCIONES
    contexto_ejecucion->instrucciones = cargar_instrucciones(path);
    contexto_ejecucion->cantidad_instrucciones = contar_lineas_en_cadena(contexto_ejecucion->instrucciones);
    // GUARDAMOS EN DICCIONARIO EL PROCESO
    dictionary_put(diccionario,"%d",pid,contexto_ejecucion);
    return contexto_ejecucion;
}

int contar_lineas_en_cadena(char*cadena[]){
    int lineas_totales = 0;
    for (const char *p= cadena; *p; ++p){
        if (*p= "\n") lineas_totales++;
    }
    return lineas_totales;
}

char**cargar_instrucciones(char*path){
    FILE*archivo = fopen(path,"r");
    char*instrucciones;
    // NICO M: Vemos el tamaño del archivo.
    fseek(archivo,0,SEEK_END);
    long tam = ftell(archivo);
    rewind(archivo);

    // NICO M: Leemos a buffer.
    fread(instrucciones, 1, tam, archivo);
    fclose(archivo);

    instrucciones["\0"]; // NICO M: Añadimos terminador nulo.

    return instrucciones;
}

char*devolver_instruccion(uint32_t pc,char*lista_instrucciones){
    char*instruccion;
    int contador = 0; // NICO M: Según los ejemplos, el PC tomaría la primera linea de una lista de instrucciones como 1.
    char*copia_lista_instrucciones = string_duplicate(lista_instrucciones); // NICO M: CREO que string_split() rompe el string que se le pase. No queremos que la lista de instrucciones se rompa.
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