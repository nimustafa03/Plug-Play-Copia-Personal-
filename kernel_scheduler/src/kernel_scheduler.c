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
#include "conexiones.h"
#include "mensajes.h"
 
t_log*    logger;
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
 
    switch (*codigo) {
 
        // ── PARTE DE BIANCA ──────────────────────────────────
        case MSG_HANDSHAKE_CPU:
            // Bianca completa esto
            log_info(logger, "CPU conectado - FD: %d", fd_cliente);
            // le contestamos que la conexión se estableció OK
            op_code respuesta = MSG_OK;
            enviar_mensaje(fd_cliente, &respuesta, sizeof(op_code));

            break;
 
        // ── PARTE DE SANTIAGO ────────────────────────────────
        case MSG_HANDSHAKE_IO:
            // Una IO se conectó
            // Log obligatorio del enunciado (lo loguea el módulo IO,
            // acá guardamos el fd para usarlo después en CP2)
            log_info(logger, "IO conectada - FD: %d", fd_cliente);
            // le contestamos que la conexión se estableció OK
            op_code respuesta = MSG_OK;
            enviar_mensaje(fd_cliente, &respuesta, sizeof(op_code));
 
            // TODO CP2: acá va la lógica de atención a IOs
            break;
 
        default:
            log_warning(logger, "Conexion desconocida - codigo: %d - FD: %d",
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
 
    logger = log_create("kernel_scheduler.log",
                        "KernelScheduler",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger == NULL) {
        printf("Error: no se pudo crear el logger\n");
        return EXIT_FAILURE;
    }
 
    char* km_ip   = config_get_string_value(config, "KM_IP");
    char* km_port = config_get_string_value(config, "KM_PORT");
    char* ks_port = config_get_string_value(config, "PORT");
 
    // ── PARTE DE BIANCA: conectarse a KM ─────────────────────
    int fd_km = crear_conexion(km_ip, km_port);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%s",
                  km_ip, km_port);
        return EXIT_FAILURE;
    }
    op_code codigo = MSG_HANDSHAKE_KS;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    log_info(logger, "## Conectado a Kernel Memory");
 
    // ── PARTE DE BIANCA: levantar servidor ───────────────────
    fd_servidor_ks = iniciar_servidor(ks_port);
    log_info(logger, "Servidor KS listo en puerto %s", ks_port);
 
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
 
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}