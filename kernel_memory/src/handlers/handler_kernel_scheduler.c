#include "handler_kernel_scheduler.h"

#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"

#include "../estructuras.h"
#include "../managers/process_manager.h"
#include "../managers/memory_manager.h"
#include "../compactacion/service_compactacion.h"
#include "../../utils/src/utils/tipos.h"

extern t_log* logger;
extern t_config*config;

static int fd_kernel_scheduler = -1;

char*completar_path(char*path){
  char*prefijo = config_get_string_value(config, "SCRIPTS_BASEPATH");
  char*path_completo = string_new();
  
  if (!string_starts_with(path,prefijo))
  {
    string_append(&path_completo, prefijo);
    string_append(&path_completo , "/");
  }
  string_append(&path_completo,path);

  return path_completo;
}

void atender_creacion_proceso(){
  uint32_t*pid; char*path; int size;

  pid = recibir_mensaje(fd_kernel_scheduler,&size);
  if (!pid) { log_error(logger, "## ERROR: No se ha dado ningún PID.");}
  log_info(logger, "PID Provisto: %d", *pid);

  path = recibir_mensaje(fd_kernel_scheduler, &size);
  if (!path) { log_error(logger, "## ERROR: NO se ha dado ningún path.");}
  log_info(logger, "Path provisto: %s", path);

  log_info(logger, "Verificando que el path se encuentre completo y rellenandolo en caso contrario...");
  char*path_completo = completar_path(path);

  if (path_completo == NULL)
  {
    log_error(logger, "## ERROR: Algo salió mal al completar el path.");
  }
  else
  {
    log_info(logger,"Se ha completado el path con exito o no necesitaba ser completado.");
    log_info(logger, "El path completo es: %s", path_completo);
    free(path);
  }

  log_info(logger, "Creando nuevo proceso. PID: %d", *pid);
  bool exito = crear_proceso(*pid,path_completo);
  op_code cod;
  if (exito){
    log_info(logger, "Proceso creado. PID: %d", *pid);
    cod = MSG_OK;
    
  }
  else{
    log_error(logger, "El proceso no se pudo crear.");
    cod = MSG_ERROR;
  }

  free(pid);
  enviar_mensaje(fd_kernel_scheduler, &cod, sizeof(op_code));
  
  return;
}

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

        case MSG_STDIN: {
          // CP3: escribir en memoria de usuario. KS envía: pid + dir_logica + tamanio + datos.
          uint32_t* pid = recibir_mensaje(fd_kernel_scheduler, &size);
          uint32_t* dir = recibir_mensaje(fd_kernel_scheduler, &size);
          uint32_t* tam = recibir_mensaje(fd_kernel_scheduler, &size);
          void* datos  = recibir_mensaje(fd_kernel_scheduler, &size);

          uint32_t dir_global;
          int tr = traducir_direccion(*pid, *dir, *tam, &dir_global);

          if (tr == TRADUCCION_SEG_FAULT) {
            respuesta = MSG_SEG_FAULT;
          } else if (tr == TRADUCCION_INEXISTENTE || !escribir_memoria_fisica(dir_global, *tam, datos)) {
            respuesta = MSG_ERROR;
          } else {
            log_info(logger, "PID: %d - Acción: ESCRIBIR - Dirección Física: %d - Valor: %.*s",
                     *pid, dir_global, (int) *tam, (char*) datos);
            respuesta = MSG_OK;
          }
          enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));

          free(pid); free(dir); free(tam); free(datos);
          break;
        }

        case MSG_STDOUT: {
          // CP3: leer de memoria de usuario. KS envía: pid + dir_logica + tamanio.
          // Respondemos MSG_OK + los bytes leídos, o MSG_SEG_FAULT / MSG_ERROR.
          uint32_t* pid = recibir_mensaje(fd_kernel_scheduler, &size);
          uint32_t* dir = recibir_mensaje(fd_kernel_scheduler, &size);
          uint32_t* tam = recibir_mensaje(fd_kernel_scheduler, &size);

          uint32_t dir_global;
          int tr = traducir_direccion(*pid, *dir, *tam, &dir_global);

          if (tr == TRADUCCION_SEG_FAULT) {
            respuesta = MSG_SEG_FAULT;
            enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
          } else if (tr == TRADUCCION_INEXISTENTE) {
            respuesta = MSG_ERROR;
            enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
          } else {
            void* buffer = malloc(*tam);
            if (!leer_memoria_fisica(dir_global, *tam, buffer)) {
              respuesta = MSG_ERROR;
              enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
            } else {
              log_info(logger, "PID: %d - Acción: LEER - Dirección Física: %d - Valor: %.*s",
                       *pid, dir_global, (int) *tam, (char*) buffer);
              respuesta = MSG_OK;
              enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));
              enviar_mensaje(fd_kernel_scheduler, buffer, (int) *tam);
            }
            free(buffer);
          }

          free(pid); free(dir); free(tam);
          break;
        }
        case MSG_CREAR_PROCESO: {
          atender_creacion_proceso();
          break;
        }

        default:
          log_warning(logger, "Código desconocido recibido de Kernel Scheduler: %d", *codigo_recibido);
          break;
      }
      free(codigo_recibido);
    }
}