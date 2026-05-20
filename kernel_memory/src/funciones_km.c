#ifndef FUNCIONES_KM
#define FUNCIONES_KM
#include <commons/collections/dictionary.h>
#include <kernel_memory.h>
#include <pthread.h>
#include <stdint.h>

void iniciar_proceso(uint32_t fd_cpu, t_dictionary*diccionario){
    pthread_t nuevo_proceso;
    // NICO M: Recibimos pid.
    int size;
    uint32_t pid;
    pid = recibir_mensaje(fd_cpu, &size);
    // NICO M: Creamos el proceso en si, creamos nuevo thread para manejarlo por separado.
    crear_proceso(pidm,diccionario);
    nuevo_proceso = pthread_create(&nuevo_proceso,NULL, manejar_proceso,fd_cpu);
}

void manejar_proceso(uint32_t fd_cpu){

}

void crear_proceso(uint32_t pid, t_dictionary*diccionario){
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
    contexto_ejecucion->instrucciones = cargar_instrucciones(pid);
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