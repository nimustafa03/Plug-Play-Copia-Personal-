#ifndef FUNCIONES_KS_H
#define FUNCIONES_KS_H   

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/collections/list.h>


extern t_list* listaProcesosNew;
extern t_list* listaProcesosReady;
extern t_list* listaProcesosExec;
extern t_list* listaProcesosBlock;
extern t_list* listaProcesosSuspBlock;
extern t_list* listaProcesosSuspReady;
extern t_list* listaProcesosExit;

typedef enum {
  NEW,
  READY,
  EXEC,
  BLOCK,
  EXIT,
  SUSP_BLOCK,
  SUSP_READY
} estado_proceso;

extern t_log* logger_ks;

typedef struct{
    int id_proceso;
    estado_proceso estado;
} Proceso;

void inicializarListas();
void* iniciar_planificador_largo_plazo();
void* iniciar_planificador_corto_plazo();


#endif FUNCIONES_KS_H