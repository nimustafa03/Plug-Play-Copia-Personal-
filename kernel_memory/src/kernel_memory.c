#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>

#include "kernel_memory.h"
#include "managers/memory_manager.h"
#include "managers/process_manager.h"
#include "server/kernel_memory_server.h"

t_log* logger;
t_config* config;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: ./bin/kernel_memory [archivo_config]\n");
        return EXIT_FAILURE;
    }

    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    logger = log_create(
        "kernel_memory.log",
        "KernelMemory",
        true,
        log_level_from_string(config_get_string_value(config, "LOG_LEVEL"))
    );

    if (logger == NULL) {
        printf("Error: no se pudo crear el logger\n");
        config_destroy(config);
        return EXIT_FAILURE;
    }

    char* estrategia = config_get_string_value(config, "ALLOCATION_STRATEGY");

    if (strcmp(estrategia, "BEST") == 0)
        inicializar_administrador_memoria(BEST_FIT);
    else
        inicializar_administrador_memoria(WORST_FIT);

    inicializar_administrador_procesos();

    char* puerto = config_get_string_value(config, "PORT");
    iniciar_servidor_kernel_memory(puerto);

    destruir_administrador_procesos();
    destruir_administrador_memoria();

    config_destroy(config);
    log_destroy(logger);

    return EXIT_SUCCESS;
}