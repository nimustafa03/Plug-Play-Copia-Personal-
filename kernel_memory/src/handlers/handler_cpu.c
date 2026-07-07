#include "../estructuras.h"
#include "handler_cpu.h"

#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"
#include "../src/managers/process_manager.h"


extern t_log*logger;
extern t_config*config;

void atender_cpu(int fd_cpu){

    int size;

    int *ptr_id_cpu = recibir_mensaje(fd_cpu,&size);

    log_info(logger, "## CPU %d Conectada", *ptr_id_cpu);
    op_code ok = MSG_OK;
    enviar_mensaje(fd_cpu, &ok, sizeof(op_code));
    free(ptr_id_cpu);
    // NICO M: Loop de espera activa, hasta que reciba el mensaje de iniciar proceso.
    op_code*codigo;
    while(1){
        codigo = recibir_mensaje(fd_cpu,&size);
        if (*codigo == MSG_INIT_CPU) {
            uint32_t*pid = recibir_mensaje(fd_cpu,&size);
            char*path = recibir_mensaje(fd_cpu, &size);
            crear_proceso(*pid,path, fd_cpu);
            free(pid); free(path);
        }
    }
    free(codigo);
}

void enviar_contexto_ejecucion_a_cpu(int fd_cpu, t_contexto contexto){
    enviar_mensaje(fd_cpu, &contexto, sizeof(t_contexto));
}

void enviar_confirmacion_a_CPU(int fd_cpu, bool OKERROR){
    op_code respuesta = MSG_ERROR;
    if (OKERROR) respuesta = MSG_OK;
    enviar_mensaje(fd_cpu, &respuesta, sizeof(op_code));
}

void enviar_proxima_instruccion_a_cpu(int fd_cpu, char*proxima_instruccion){
    enviar_mensaje(fd_cpu, proxima_instruccion,strlen(proxima_instruccion)+1);
}

bool esperar_pedido_de_instruccion(int fd_cpu){
    int size;
    op_code * codigo = recibir_mensaje(fd_cpu, &size);
    if (*codigo == MSG_FETCH_CPU){
        usleep(config_get_int_value(config,"INSTRUCTION_DELAY")*1000);
        return true;
    }
    return false;
}