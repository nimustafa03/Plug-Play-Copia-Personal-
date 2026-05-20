#ifndef FUNCIONES_KM
#define FUNCIONES_KM
#include <commons/collections/dictionary.h>
#include <kernel_memory.h>
#include <pthread.h>
#include <stdint.h>
#include <commons/log.h>

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
}

int contar_lineas_en_cadena(char*cadena[]){
    int lineas_totales = 0;
    for (const char *p= cadena; *p; ++p){
        if (*p= "\n") lineas_totales++;
    }
    return lineas_totales;
}

devolver_lista_instrucciones(uint32_t pid, t_log logger){
    log_info
}