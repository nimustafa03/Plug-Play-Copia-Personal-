#include "handler_kernel_scheduler.h"

#include <stdlib.h>
#include <commons/log.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"

#include "../estructuras.h"
#include "../managers/process_manager.h"
#include "../compactacion/service_compactacion.h"

extern t_log* logger;

static int fd_kernel_scheduler = -1;

static t_resultado_solicitud_desalojo solicitar_desalojo_por_compactacion(void)
{
    op_code mensaje = MSG_SOLICITAR_DESALOJO;
    enviar_mensaje(fd_kernel_scheduler, &mensaje, sizeof(op_code));

    int size;
    op_code* respuesta = recibir_mensaje(fd_kernel_scheduler, &size);

    if (respuesta == NULL || size != sizeof(op_code)) {
        log_error(logger, "Error esperando confirmación de desalojo por compactación");
        free(respuesta);
        return DESALOJO_ERROR_SIN_RESPUESTA;
    }

    if (*respuesta != MSG_DESALOJO_REALIZADO) {
        log_error(logger, "Respuesta inesperada ante desalojo por compactación: %d", *respuesta);
        return DESALOJO_ERROR_RESPUESTA_INESPERADA;
    }

    free(respuesta);
    return DESALOJO_OK;
}

static t_resultado_crear_segmento atender_creacion_segmento () {
  uint32_t*pid, *id_segmento, *tamanio; int size;
  pid = recibir_mensaje(fd_kernel_scheduler, &size);
  id_segmento = recibir_mensaje(fd_kernel_scheduler, &size);
  tamanio = recibir_mensaje(fd_kernel_scheduler, &size);

  t_resultado_crear_segmento resultado = crear_segmento(*pid, *id_segmento, *tamanio);

  if (resultado == CREAR_SEGMENTO_REQUIERE_COMPACTACION) {
    t_resultado_solicitud_desalojo resultado_desalojo = solicitar_desalojo_por_compactacion();
    if (resultado_desalojo == DESALOJO_OK){
      ejecutar_compactacion();
      resultado = crear_segmento(*pid, *id_segmento, *tamanio); // Recursividad. Es improbable, pero si llegó a no realizarse correctamente la compactación se detectaría de nuevo y se intenta hacerla nuevamente.
    }
    else {
      return CREAR_SEGMENTO_REQUIERE_COMPACTACION_DESALOJO_FALLO;
    }
  }
  return resultado;
    /*
     * TODO:
     * Cuando esté implementado el protocolo de compactación:
     *
     * 1. Solicitar desalojo.
     * 2. Esperar confirmación.
     * 3. Ejecutar compactación.
     * 4. Reintentar crear el segmento.
     */
    // CREO que está hecho. Pero dejo la instrucciones acá por si hice algo mal.

}

bool atender_destruccion_proceso(){
    int size;
    uint32_t*pid = recibir_mensaje(fd_kernel_scheduler, &size);
  return destruir_proceso(*pid);
}

bool atender_destruccion_segmento(){
  int size;
  uint32_t*pid = recibir_mensaje(fd_kernel_scheduler, &size);
  uint32_t *id_segmento = recibir_mensaje(fd_kernel_scheduler, &size);
  return eliminar_segmento(*pid, *id_segmento);
}

void atender_kernel_scheduler(int fd) {
    fd_kernel_scheduler = fd;

    log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd_kernel_scheduler);

    op_code respuesta = MSG_OK;
    enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));

    while (1) {
      int size;
      op_code* codigo_recibido = recibir_mensaje(fd_kernel_scheduler, &size);

      if (codigo_recibido == NULL) break;


      if (size != sizeof(op_code)) {
        log_error(logger, "Mensaje inválido recibido de Kernel Scheduler");
        free(codigo_recibido);
        continue;
      }

      switch (*codigo_recibido) {
        case MSG_DONE:
          respuesta = MSG_ERROR;
          if (atender_destruccion_proceso()){
            respuesta = MSG_OK;
          };
          enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
          // TODO: recibir PID y llamar a destruir_proceso(pid)
          break;

        case MSG_MEM_ALLOC:
          respuesta = MSG_OK;
          if (atender_creacion_segmento() != CREAR_SEGMENTO_OK){
            respuesta = MSG_ERROR;
          }
          enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
          // TODO:
          // 1. recibir PID
          // 2. recibir id_segmento
          // 3. recibir tamanio
          // 4. llamar a atender_creacion_segmento(pid, id_segmento, tamanio)
          // 5. responder resultado al Kernel Scheduler
          break;

        case MSG_MEM_FREE:
          respuesta = MSG_ERROR;
          if (atender_destruccion_segmento()){
            respuesta = MSG_OK;
          }

          enviar_mensaje(fd_kernel_scheduler,&respuesta,sizeof(op_code));
          // TODO:
          // 1. recibir PID
          // 2. recibir id_segmento
          // 3. llamar a eliminar_segmento(pid, id_segmento)
          // 4. responder OK / ERROR
          break;

        case MSG_STDIN:
          enviar_mensaje(fd_kernel_scheduler, codigo_recibido, sizeof(op_code));
          break;

        case MSG_STDOUT:
          enviar_mensaje(fd_kernel_scheduler, codigo_recibido, sizeof(op_code));
          break;

        default:
          log_warning(logger, "Código desconocido recibido de Kernel Scheduler: %d", *codigo_recibido);
          break;
      }
      free(codigo_recibido);
    }
}