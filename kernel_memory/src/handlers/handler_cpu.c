#include "handler_cpu.h"

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

#include "../../utils/src/utils/mensajes.h"
#include "../../utils/src/utils/conexiones.h"
#include "../src/managers/process_manager.h"


extern t_log*logger;
extern t_config*config;
static int fd_cpu = -1;

uint32_t recibir_pid(){
    int size;
    uint32_t*pid = recibir_mensaje(fd_cpu,&size);
    return *pid;
}

t_contexto *recibir_contexto(){
    int size;
    t_contexto*contexto;
    contexto = recibir_mensaje(fd_cpu, &size);
    return contexto;
}

void atender_cpu(int fd_cpu){
    fd_cpu = fd_cpu;

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
        switch(*codigo){
            case MSG_INIT_CPU:
                inicializar_proceso(recibir_pid(), fd_cpu);
                break;
            case MSG_INTERRUPT:
                enviar_confirmacion_a_CPU(fd_cpu,actualizar_contexto(recibir_pid(),recibir_contexto()));
                break;
            default:
                log_warning(logger, "Código desconocido recibido de CPU: %d", *codigo);
                break;
          break;
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

uint32_t recibir_pc(int fd_cpu){
    int size;
    uint32_t * pc = recibir_mensaje(fd_cpu, &size);

    return *pc;
}