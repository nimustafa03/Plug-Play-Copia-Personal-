#include "handler_memory_stick.h"

#include <stdlib.h>
#include <commons/log.h>
#include <commons/log.h>
#include "../../utils/src/utils/conexiones.h"
#include "../../utils/src/utils/mensajes.h"
#include "../managers/memory_manager.h"
#include "../handlers/handler_cpu.h"

extern t_log* logger;

void atender_memory_stick2(int fd_memory_stick) {
  int size;
  int*ptr_memory_stick_size = recibir_mensaje(fd_memory_stick, &size);
  if (ptr_memory_stick_size == NULL || size != sizeof(int)) {
    log_error(logger, "Error al recibir tamaño de Memory Stick - FD: %d", fd_memory_stick);
    free(ptr_memory_stick_size);
    return;
  }
  

  int memory_stick_size = *ptr_memory_stick_size;
  free(ptr_memory_stick_size);
  const char* ip = "test"; 
  const char * puerto = "test2";
  conectar_memory_stick(ip, puerto, (uint32_t) memory_stick_size, fd_memory_stick);


  if (!notificar_mapa_memory_sticks_a_cpu()) {
      log_warning(
          logger,
          "El Memory Stick fue registrado, pero CPU no recibió el mapa actualizado"
      );
  }

  log_info(logger, "## Memory Stick de %d bytes Conectada", memory_stick_size);

  op_code ok = MSG_OK;
  enviar_mensaje(fd_memory_stick, &ok, sizeof(op_code));
}


void atender_memory_stick(int fd_memory_stick) {
    int size;

    /*
     * Recibir tamaño.
     */
    int* ptr_memory_stick_size =
        recibir_mensaje(fd_memory_stick, &size);

    if (
        ptr_memory_stick_size == NULL ||
        size != sizeof(int)
    ) {
        log_error(
            logger,
            "Error al recibir tamaño de Memory Stick - FD: %d",
            fd_memory_stick
        );

        free(ptr_memory_stick_size);
        return;
    }

    int memory_stick_size = *ptr_memory_stick_size;
    free(ptr_memory_stick_size);

    /*
     * Recibir IP publicada por el Memory Stick.
     */
    char* memory_stick_ip =
        recibir_mensaje(fd_memory_stick, &size);

    if (
        memory_stick_ip == NULL ||
        size <= 1 ||
        memory_stick_ip[size - 1] != '\0'
    ) {
        log_error(
            logger,
            "Error al recibir IP de Memory Stick - FD: %d",
            fd_memory_stick
        );

        free(memory_stick_ip);
        return;
    }

    /*
     * Recibir puerto donde escucha conexiones de CPU.
     */
    char* memory_stick_puerto =
        recibir_mensaje(fd_memory_stick, &size);

    if (
        memory_stick_puerto == NULL ||
        size <= 1 ||
        memory_stick_puerto[size - 1] != '\0'
    ) {
        log_error(
            logger,
            "Error al recibir puerto de Memory Stick - FD: %d",
            fd_memory_stick
        );

        free(memory_stick_ip);
        free(memory_stick_puerto);
        return;
    }

    /*
     * Registrar el Memory Stick.
     */
    conectar_memory_stick(
        memory_stick_ip,
        memory_stick_puerto,
        (uint32_t)memory_stick_size,
        fd_memory_stick
    );

    log_info(
        logger,
        "## Memory Stick de %d bytes conectada - IP: %s - Puerto: %s",
        memory_stick_size,
        memory_stick_ip,
        memory_stick_puerto
    );

    /*
     * Confirmar el registro.
     */
    op_code ok = MSG_OK;

    enviar_mensaje(
        fd_memory_stick,
        &ok,
        sizeof(op_code)
    );

    /*
     * Actualizar el mapa enviado a las CPUs.
     */
    if (!notificar_mapa_memory_sticks_a_cpu()) {
        log_warning(
            logger,
            "El Memory Stick fue registrado, pero CPU no recibió el mapa actualizado"
        );
    }

    /*
     * conectar_memory_stick() debe guardar copias.
     */
    free(memory_stick_ip);
    free(memory_stick_puerto);
}