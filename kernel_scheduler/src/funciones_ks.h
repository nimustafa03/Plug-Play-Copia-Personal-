#ifndef FUNCIONES_KS_H
#define FUNCIONES_KS_H   

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>          // para uint32_t en Proceso
#include <pthread.h>
#include <semaphore.h>       // para sem_t
#include <commons/collections/list.h>
#include "gestor_ks.h"       // trae logger_ks, config, fd_km, fd_servidor_ks


extern t_list* listaProcesosNew;
extern t_list* listaProcesosReady;
extern t_list* listaProcesosExec;
extern t_list* listaProcesosBlock;
extern t_list* listaProcesosSuspBlock;
extern t_list* listaProcesosSuspReady;
extern t_list* listaProcesosExit;
extern t_list* listaCPUsLibres; // FDs de CPUs que están libres esperando un proceso
extern t_list* listaIOsLibres;  // lo mismo que arriba
extern t_list* listaMutex;

// semáforos y mutex
extern sem_t sem_hay_proceso_ready;
extern sem_t sem_hay_cpu_libre;
extern sem_t sem_hay_io_libre;
extern pthread_mutex_t mutex_listas;

typedef enum {
  NEW,
  READY,
  EXEC,
  BLOCK,
  EXIT,
  SUSP_BLOCK,
  SUSP_READY
} estado_proceso;

typedef struct{
    int id_proceso;
    estado_proceso estado;
    int prioridad; // lo usa CMN
    int fd_cpu; //FD de la CPU que lo está ejecutando (-1 si ninguna)
} Proceso;

typedef struct {
    int fd_cpu;
    uint32_t pid;
    int quantum;
} t_args_rr;

typedef struct {
    char* nombre;
    uint32_t pid_tomador;  // PID que lo tiene tomado, -1 si esta libre
    t_list* cola_bloqueados; // procesos esperando este mutex
} t_mutex_ks;

void inicializarListasProcesos();
void* iniciar_planificador_largo_plazo();
void* iniciar_planificador_corto_plazo();
void actualizarEstadoProceso(Proceso* proceso, estado_proceso nuevoEstado);  // faltaba la declaración
void crear_proceso_inicial(char* path); // funcion nueva
void procesoAReady (Proceso* p); // faltaban las declaraciones de los helpers
void procesoAExec (Proceso* p);
void procesoAExit (Proceso* p);
void procesoABlock (Proceso* p);
void procesoASuspBlock (Proceso* p);
void procesoASuspReady (Proceso* p);
void* timer_rr(void* arg);
t_mutex_ks* buscar_mutex(char* nombre);
void mutex_create(char* nombre);
void mutex_lock(char* nombre, Proceso* proceso);
void mutex_unlock(char* nombre, Proceso* proceso);
void atender_cpu_ks(int fd_cpu);
Proceso* buscar_proceso_por_pid(uint32_t pid);

#endif // FUNCIONES_KS_H