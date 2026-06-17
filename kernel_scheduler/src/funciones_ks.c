#include "funciones_ks.h"

#include <commons/log.h>
#include <utils/conexiones.h>  
#include <utils/mensajes.h>    
#include <utils/tipos.h>   


char const* estadosProcesos[] = {"NEW", "READY", "EXEC", "BLOCK", "EXIT", "SUSP_READY", "SUSP_BLOCK"};

t_list* listaProcesosNew = NULL;
t_list* listaProcesosReady = NULL;
t_list* listaProcesosExec = NULL;
t_list* listaProcesosBlock = NULL;
t_list* listaProcesosSuspBlock = NULL;
t_list* listaProcesosSuspReady = NULL;
t_list* listaProcesosExit = NULL;
t_list* listaCPUsLibres = NULL; // lista de FDs de CPUs libres 
t_list* listaIOsLibres = NULL; // lo mismo que arriba, pero para IO

// semáforos y mutex nuevos 
sem_t sem_hay_proceso_ready;
sem_t sem_hay_cpu_libre;
sem_t sem_hay_io_libre;
pthread_mutex_t mutex_listas;


void inicializarListasProcesos() {
  listaProcesosNew = list_create();
  listaProcesosReady = list_create();
  listaProcesosExec = list_create();
  listaProcesosBlock = list_create();
  listaProcesosSuspBlock = list_create();
  listaProcesosSuspReady = list_create();
  listaProcesosExit = list_create(); // faltaba crear esta linea
  listaCPUsLibres = list_create();
  listaIOsLibres = list_create();

  // arrancan bloqueados
  sem_init(&sem_hay_proceso_ready, 0, 0);
  sem_init(&sem_hay_cpu_libre, 0, 0);
  sem_init(&sem_hay_io_libre, 0, 0);
  // mutex para proteger acceso concurrente a las listas
  pthread_mutex_init(&mutex_listas, NULL);
}

// crea el proceso PID 0 desde argv[2] y lo mete en NEW
void crear_proceso_inicial(char* path) {
    Proceso* p = malloc(sizeof(Proceso));
    p->id_proceso = 0;
    p->estado = NEW;
    p->prioridad = 0;   // máxima prioridad
    p->fd_cpu = -1;  // sin CPU asignada todavía
 
    // log obligatorio
    log_info(logger_ks, "## (%d) Se crea el proceso - Estado: NEW", p->id_proceso);
 
    pthread_mutex_lock(&mutex_listas);
    list_add(listaProcesosNew, p);
    pthread_mutex_unlock(&mutex_listas);
}

void procesoAReady(Proceso* proceso){
  list_add(listaProcesosReady, proceso);
  log_info(logger_ks, "proceso %d agregado a lista READY", proceso->id_proceso);
}

void procesoAExec(Proceso* proceso){
  list_add(listaProcesosExec, proceso);
  log_info(logger_ks, "proceso %d agregado a lista EXEC", proceso->id_proceso);
}

void procesoAExit(Proceso* proceso){
  list_add(listaProcesosExit, proceso);
  log_info(logger_ks, "Se terminó la proceso %d agregado a lista EXIT", proceso->id_proceso);
}

void procesoABlock(Proceso* proceso){
  list_add(listaProcesosBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista BLOCK", proceso->id_proceso);
}

void procesoASuspBlock(Proceso* proceso){
  list_add(listaProcesosSuspBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP BLOCK", proceso->id_proceso);
}

void procesoASuspReady(Proceso* proceso){
  list_add(listaProcesosSuspReady, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP READY", proceso->id_proceso);
}

void* iniciar_planificador_largo_plazo () {
  log_info(logger_ks, "Planificador de Largo Plazo iniciado.");
  while (1) {

    // un solo lock que chequea y remueve. Evita race condition entre dos locks separados
    pthread_mutex_lock(&mutex_listas);
    if (list_is_empty(listaProcesosNew)) {
      pthread_mutex_unlock(&mutex_listas);
      usleep(1000); // evita busy-wait
      continue;     // vuelve al while(1)
    }
    Proceso* nuevoProceso = list_remove(listaProcesosNew, 0); //cambio list_get por list_remove, el proceso sale de NEW
    pthread_mutex_unlock(&mutex_listas);

    actualizarEstadoProceso(nuevoProceso, READY);
    sem_post(&sem_hay_proceso_ready); // avisa al planificador de corto plazo
  }
}

void* timer_rr(void* arg) {
    t_args_rr* args = (t_args_rr*) arg;

    // dormimos el quantum (usleep espera microsegundos, el config está en ms)
    usleep(args->quantum * 1000);

    // enviamos la interrupción
    op_code interrupcion = MSG_INTERRUPT;
    enviar_mensaje(args->fd_cpu, &interrupcion, sizeof(op_code));

    t_interrupcion intr;
    intr.pid    = args->pid;
    intr.motivo = 0; // fin de quantum
    enviar_mensaje(args->fd_cpu, &intr, sizeof(t_interrupcion));

    free(args);
    return NULL;
}

// Planif. Corto plazo: implementa FIFO y RR
void* iniciar_planificador_corto_plazo() {
  log_info(logger_ks, "Planificador de Corto Plazo iniciado.");
 
  char* algoritmo = config_get_string_value(config, "PLANIFICATION_ALGORITHM");
 
  while (1) {
    // bloquea hasta que haya proceso en READY Y cpu libre
    sem_wait(&sem_hay_proceso_ready);
    sem_wait(&sem_hay_cpu_libre);
 
    pthread_mutex_lock(&mutex_listas);
 
    if (list_is_empty(listaProcesosReady) || list_is_empty(listaCPUsLibres)) {
      pthread_mutex_unlock(&mutex_listas);
      continue;
    }
 
    // FIFO y RR toman el primero de READY
    Proceso* proceso = list_remove(listaProcesosReady, 0);
    int* fd_cpu_ptr = list_remove(listaCPUsLibres, 0);
    int  fd_cpu = *fd_cpu_ptr;
    free(fd_cpu_ptr);
 
    pthread_mutex_unlock(&mutex_listas);
 
    proceso->fd_cpu = fd_cpu;
    actualizarEstadoProceso(proceso, EXEC);
 
    // envia el PID a la CPU para que empiece a ejecutar
    enviar_mensaje(fd_cpu, &proceso->id_proceso, sizeof(uint32_t));
 
    // Round Robin
    if (strcmp(algoritmo, "RR") == 0) {
        t_args_rr* args = malloc(sizeof(t_args_rr));
        args->fd_cpu = fd_cpu;
        args->pid = proceso->id_proceso;
        args->quantum = config_get_int_value(config, "RR_QUANTUM");

        pthread_t thread_timer;
        pthread_create(&thread_timer, NULL, timer_rr, args);
        pthread_detach(thread_timer);

        int size;
        op_code* respuesta = recibir_mensaje(fd_cpu, &size);
        if (*respuesta == MSG_INTERRUPCION_ATENDIDA) {
            // log obligatorio
            log_info(logger_ks, "## (%d) - Desalojado por fin de quantum", proceso->id_proceso);

            // CPU vuelve a estar libre
            pthread_mutex_lock(&mutex_listas);
            int* fd_libre = malloc(sizeof(int));
            *fd_libre = fd_cpu;
            list_add(listaCPUsLibres, fd_libre);
            pthread_mutex_unlock(&mutex_listas);
            sem_post(&sem_hay_cpu_libre);

            // Proceso vuelve a READY (al final de la cola - FIFO dentro de RR)
            actualizarEstadoProceso(proceso, READY);
            sem_post(&sem_hay_proceso_ready);
        }
        free(respuesta);
    }
  }
}


void actualizarEstadoProceso (Proceso* proceso, estado_proceso nuevoEstado){
  pthread_mutex_lock(&mutex_listas); // evita race conditions entre planificadores

  Proceso* procesoEncontrado = NULL;
  log_debug(logger_ks, "Actualizando estado de Query %d de %s a %s", proceso->id_proceso, estadosProcesos[proceso->estado], estadosProcesos[nuevoEstado]);
  
  estado_proceso estado_anterior = proceso->estado;
    switch(estado_anterior){
        case NEW:
          for(int i = 0; i < list_size(listaProcesosNew); i++) {
              Proceso* procesoTemporal= list_get(listaProcesosNew, i);
              if(procesoTemporal->id_proceso == proceso->id_proceso) {
                  procesoEncontrado = list_remove(listaProcesosNew, i);
                  break;
              }
          }
          break;
        case READY:
          for(int i = 0; i < list_size(listaProcesosReady); i++) {
              Proceso* procesoTemporal= list_get(listaProcesosReady, i);
              if(procesoTemporal->id_proceso == proceso->id_proceso) {
                procesoEncontrado = list_remove(listaProcesosReady, i);
                break;
              }
          }
          break;
        case EXEC:
          for(int i = 0; i < list_size(listaProcesosExec); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosExec, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosExec, i);
              break;
            }
          }
          break;
        case BLOCK:
          for(int i = 0; i < list_size(listaProcesosBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosBlock, i);
              break;
            }
          }
          break;
        case SUSP_BLOCK:
          for(int i = 0; i < list_size(listaProcesosSuspBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosSuspBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosSuspBlock, i);
              break;
            }
          }
          break;
        case SUSP_READY:
          for(int i = 0; i < list_size(listaProcesosSuspReady); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosSuspReady, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              log_debug(logger_ks, "Query encontrada en lista EXIT, removiendo");
              procesoEncontrado = list_remove(listaProcesosSuspReady, i);
              break;
            }
          }
          break;
        default:
          log_error(logger_ks, "Error: estado de proceso desconocido");
          pthread_mutex_unlock(&mutex_listas);  // unlock antes de cada return
          return;
    }

    if(procesoEncontrado == NULL) {
      log_error(logger_ks, "Error: no se encontro el proceso en la lista correspondiente a su estado actual");
      pthread_mutex_unlock(&mutex_listas); // hay que unlockear antes de cada return
      return;
    } else {
        // log obligatorio 
        log_info(logger_ks, "## (%d) Pasa del estado %s al estado %s",
                 proceso->id_proceso,
                 estadosProcesos[estado_anterior],
                 estadosProcesos[nuevoEstado]);

        switch(nuevoEstado){
            case READY:
                procesoEncontrado->estado = READY;
                procesoAReady(procesoEncontrado);
                break;
            case EXEC:
                procesoEncontrado->estado = EXEC;
                procesoAExec(procesoEncontrado);
                break;
            case EXIT:
                procesoEncontrado->estado = EXIT;
                procesoAExit(procesoEncontrado);
                break;
            case BLOCK:
                procesoEncontrado->estado = BLOCK;
                procesoABlock(procesoEncontrado);
                break;
            case SUSP_READY:
                procesoEncontrado->estado = SUSP_READY;
                procesoASuspReady(procesoEncontrado);
                break;
            case SUSP_BLOCK:
                procesoEncontrado->estado = SUSP_BLOCK;
                procesoASuspBlock(procesoEncontrado);
                break;
            default:
                log_error(logger_ks, "Error: nuevo estado de query invalido");
                // Devolvemos la query a su lista original para no perderla
                switch(estado_anterior) {
                    case READY: procesoAReady(procesoEncontrado); break;
                    case EXEC: procesoAExec(procesoEncontrado); break;
                    case EXIT: procesoAExit(procesoEncontrado); break;
                    case BLOCK: procesoABlock(procesoEncontrado); break;
                    case SUSP_READY: procesoASuspReady(procesoEncontrado); break;
                    case SUSP_BLOCK: procesoABlock(procesoEncontrado); break;
                }
                pthread_mutex_unlock(&mutex_listas); // unlockear antes del return
                return;
        }
    }
    pthread_mutex_unlock(&mutex_listas); // unlock al salir normalmente
}