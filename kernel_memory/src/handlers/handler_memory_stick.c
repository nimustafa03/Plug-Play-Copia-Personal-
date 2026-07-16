#include "handler_memory_stick.h"

#include <stdlib.h>
#include <commons/log.h>

#include <utils/mensajes.h>

#include "../managers/memory_manager.h"

extern t_log* logger;

void atender_memory_stick(int fd_memory_stick) {
  int size;
  int* ptr_memory_stick_size = recibir_mensaje(fd_memory_stick, &size);

  if (ptr_memory_stick_size == NULL || size != sizeof(int)) {
      log_error(logger, "Error al recibir tamaño de Memory Stick - FD: %d", fd_memory_stick);
      free(ptr_memory_stick_size);
      return;
  }

  int memory_stick_size = *ptr_memory_stick_size;
  free(ptr_memory_stick_size);

  conectar_memory_stick((uint32_t) memory_stick_size, fd_memory_stick);


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