#include "funciones_ks.h"

#include <commons/log.h>



char const* estadosProcesos[] = {"NEW", "READY", "EXEC", "BLOCK", "EXIT", "SUSP_READY", "SUSP_BLOCK"};

t_list* listaProcesosNew = NULL;
t_list* listaProcesosReady = NULL;
t_list* listaProcesosExec = NULL;
t_list* listaProcesosBlock = NULL;
t_list* listaProcesosSuspBlock = NULL;
t_list* listaProcesosSuspReady = NULL;
t_list* listaProcesosExit = NULL;


void inicializarListasProcesos() {
  listaProcesosNew = list_create();
  listaProcesosReady = list_create();
  listaProcesosExec = list_create();
  listaProcesosBlock = list_create();
  listaProcesosSuspBlock = list_create();
  listaProcesosSuspReady = list_create();
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
    bool hay_procesos_nuevos = !list_is_empty(listaProcesosNew);
    if (hay_procesos_nuevos) {
      Proceso* nuevoProceso = list_get(listaProcesosNew, 0);
      actualizarEstadoProceso(nuevoProceso, READY);
    }
  }
}

void* iniciar_planificador_corto_plazo() {
  log_info(logger_ks, "Planificador de Corto Plazo iniciado.");
}

void actualizarEstadoProceso (Proceso* proceso, estado_proceso nuevoEstado){
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
          return;
    }

    if(procesoEncontrado == NULL) {
      log_error(logger_ks, "Error: no se encontro el proceso en la lista correspondiente a su estado actual");
      return;
    } else {
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
                return;
        }

    }
}