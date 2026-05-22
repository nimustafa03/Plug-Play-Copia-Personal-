// =============================================================
//  kernel_scheduler.c  —  Módulo Kernel Scheduler
//  Cómo ejecutar: ./bin/kernel_scheduler kernel_scheduler.config 
//
//  Responsables CP1:
//    Bianca  → cliente KS→KM  +  servidor KS acepta CPUs
//    Santiago → servidor KS acepta IOs   ← ESTA PARTE
//
//  Agregué el código dentro del switch de
//  atender_cliente_ks(), en el case MSG_HANDSHAKE_IO
//  Te toca el resto, exitos!
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include "funciones_ks.h"
 
t_log*    logger_ks = NULL;
t_config* config;
int       fd_servidor_ks;
 
// -----------------------------------------------------------------
//  atender_cliente_ks
//  Corre en un hilo por cada cliente que se conecta al servidor KS.
//  Lee el handshake e identifica quién es.
//
//  Bianca agrega: case MSG_HANDSHAKE_CPU
//  Santiago agrega: case MSG_HANDSHAKE_IO   ← acá
// -----------------------------------------------------------------
void* atender_cliente_ks(void* arg) {
    int fd_cliente = *((int*) arg);
    free(arg);
 
    // Leemos quién se conectó
    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    // Declaro la respuesta acá arriba para no repetir código
    op_code respuesta_ok = MSG_OK;
 
    switch (*codigo) {
 
        // ── PARTE DE BIANCA ──────────────────────────────────
        case MSG_HANDSHAKE_CPU:
            // Bianca completa esto
            log_info(logger_ks, "CPU conectado - FD: %d", fd_cliente);
            // le contestamos que la conexión se estableció OK
            enviar_mensaje(fd_cliente, &respuesta_ok, sizeof(op_code));

            break;
 
        // ── PARTE DE SANTIAGO ────────────────────────────────
        case MSG_HANDSHAKE_IO:
            // Una IO se conectó
            // Log obligatorio del enunciado (lo loguea el módulo IO,
            // acá guardamos el fd para usarlo después en CP2)
            log_info(logger_ks, "IO conectada - FD: %d", fd_cliente);
            // le contestamos que la conexión se estableció OK
            enviar_mensaje(fd_cliente, &respuesta_ok, sizeof(op_code));
 
            // TODO CP2: acá va la lógica de atención a IOs
            break;
 
        default:
            log_warning(logger_ks, "Conexion desconocida - codigo: %d - FD: %d",
                        *codigo, fd_cliente);
            break;
    }
 
    free(codigo);
    return NULL;
}
 
// -----------------------------------------------------------------
//  Bianca: conecta KS→KM y levanta el servidor
//  Santiago: el servidor que levanta Bianca también acepta IOs,
//            y atender_cliente_ks identifica quién es
// -----------------------------------------------------------------
int main(int argc, char* argv[]) {
 
    if (argc < 2) {
        printf("Uso: ./bin/kernel_scheduler [archivo_config] [path_proceso_inicial]\n");
        return EXIT_FAILURE;
    }
 
    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
 
    logger_ks = log_create("kernel_scheduler.log",
                        "KernelScheduler",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger_ks == NULL) {
        printf("Error: no se pudo crear el logger_ks\n");
        return EXIT_FAILURE;
    }
 
    char* km_ip   = config_get_string_value(config, "KM_IP");
    char* km_port = config_get_string_value(config, "KM_PORT");
    char* ks_port = config_get_string_value(config, "PORT");
 
    // ── PARTE DE BIANCA: conectarse a KM ─────────────────────
    int fd_km = crear_conexion(km_ip, km_port);
    if (fd_km == -1) {
        log_error(logger_ks, "No se pudo conectar a Kernel Memory en %s:%s",
                  km_ip, km_port);
        return EXIT_FAILURE;
    }
    op_code codigo = MSG_HANDSHAKE_KS;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    log_info(logger_ks, "## Conectado a Kernel Memory");

    // 2. Esperar OK 
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    
    if (*respuesta == MSG_OK) {
        log_info(logger_ks, "## Conectado a Kernel Memory y aceptado");
    }
    free(respuesta); // Liberar la memoria del mensaje recibido

    // ── PARTE DE BIANCA: levantar servidor ───────────────────
    fd_servidor_ks = iniciar_servidor(ks_port);
    log_info(logger_ks, "Servidor KS listo en puerto %s", ks_port);
 
    // ── PARTE DE AMBOS: loop que acepta CPUs e IOs ───────────
    // (el mismo servidor atiende a ambos, atender_cliente_ks
    //  los distingue por el handshake)
    while (1) {
        int* fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor_ks);
 
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente_ks, fd_cliente);
        pthread_detach(hilo);
    }

    // Planificador
    inicializarListasProcesos();

    pthread_t threadPlanificadorLargoPlazo;
    pthread_t threadPlanificadorCortoPlazo;
    
    pthread_create(&threadPlanificadorLargoPlazo, NULL, iniciar_planificador_largo_plazo, NULL);
    pthread_create(&threadPlanificadorCortoPlazo, NULL, iniciar_planificador_corto_plazo, NULL);


    pthread_detach(threadPlanificadorLargoPlazo);
    pthread_detach(threadPlanificadorCortoPlazo);
 
    config_destroy(config);
    log_destroy(logger_ks);
    return EXIT_SUCCESS;
}