#include "handler_kernel_scheduler.h"

#include <stdlib.h>
#include <commons/log.h>

#include <utils/mensajes.h>

#include "../estructuras.h"
#include "../managers/process_manager.h"
#include "../compactacion/service_compactacion.h"

extern t_log* logger;

static int fd_kernel_scheduler = -1;

static void solicitar_desalojo_por_compactacion(void)
{
    op_code mensaje = MSG_SOLICITAR_COMPACTACION;
    enviar_mensaje(fd_kernel_scheduler, &mensaje, sizeof(op_code));

    int size;
    op_code* respuesta = recibir_mensaje(fd_kernel_scheduler, &size);

    if (respuesta == NULL || size != sizeof(op_code)) {
        log_error(logger, "Error esperando confirmación de desalojo por compactación");
        free(respuesta);
        return;
    }

    if (*respuesta != MSG_DESALOJO_REALIZADO) {
        log_error(logger, "Respuesta inesperada ante desalojo por compactación: %d", *respuesta);
    }

    free(respuesta);
}

static t_resultado_crear_segmento atender_creacion_segmento (uint32_t pid, uint32_t id_segmento, uint32_t tamanio) {
  t_resultado_crear_segmento resultado = crear_segmento(pid, id_segmento, tamanio);

  if (resultado != CREAR_SEGMENTO_REQUIERE_COMPACTACION)
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

    return CREAR_SEGMENTO_REQUIERE_COMPACTACION;
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
          // TODO: recibir PID y llamar a destruir_proceso(pid)
          break;

        case MSG_MEM_ALLOC:
          // TODO:
          // 1. recibir PID
          // 2. recibir id_segmento
          // 3. recibir tamanio
          // 4. llamar a atender_creacion_segmento(pid, id_segmento, tamanio)
          // 5. responder resultado al Kernel Scheduler
          break;

        case MSG_MEM_FREE:
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