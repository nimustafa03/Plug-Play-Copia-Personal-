// =============================================================
//  swap.c  —  Módulo SWAP
//  Cómo ejecutar: ./bin/swap swap.config
//
//  CP1: conectarse a Kernel Memory e informar BLOCK_SIZE y tamaño
//
//  Responsable CP1: Santiago
//    - Cliente: SWAP → KM (conexión 3, lado cliente)
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
 
t_log*    logger;
t_config* config;
 
int main(int argc, char* argv[]) {
 
    // ---------------------------------------------------------
    // 1. Verificar argumentos
    // ---------------------------------------------------------
    if (argc < 2) {
        printf("Uso: ./bin/swap [archivo_config]\n");
        return EXIT_FAILURE;
    }
 
    // ---------------------------------------------------------
    // 2. Cargar configuración
    // ---------------------------------------------------------
    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
 
    // ---------------------------------------------------------
    // 3. Crear logger
    // ---------------------------------------------------------
    logger = log_create("swap.log",
                        "SWAP",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger == NULL) {
        printf("Error: no se pudo crear el logger\n");
        return EXIT_FAILURE;
    }
 
    // ---------------------------------------------------------
    // 4. Leer parámetros del config
    // ---------------------------------------------------------
    char* km_ip      = config_get_string_value(config, "KM_IP");
    char* km_port    = config_get_string_value(config, "KM_PORT");
    int   block_size = config_get_int_value(config, "BLOCK_SIZE");
    int   swap_size  = config_get_int_value(config, "SWAP_FILE_SIZE");
 
    // ---------------------------------------------------------
    // 5. Conectarse a Kernel Memory
    // ---------------------------------------------------------
    log_info(logger, "Conectando a Kernel Memory en %s:%s...", km_ip, km_port);
 
    int fd_km = crear_conexion(km_ip, km_port);
 
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%s",
                  km_ip, km_port);
        return EXIT_FAILURE;
    }
 
    // ---------------------------------------------------------
    // 6. Identificarse ante KM y enviar BLOCK_SIZE + tamaño total
    // ---------------------------------------------------------
    op_code codigo = MSG_HANDSHAKE_SWAP;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    enviar_mensaje(fd_km, &block_size, sizeof(int));
    enviar_mensaje(fd_km, &swap_size,  sizeof(int));
     
    // ---------------------------------------------------------
    // 7. Esperar OK de KM y loguear conexión exitosa (log obligatorio)
    // ---------------------------------------------------------
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (*respuesta == MSG_OK)
        log_info(logger, "## Conectado a Kernel Memory");
    free(respuesta);

 
    // ---------------------------------------------------------
    // 8. Esperar operaciones de KM
    //    TODO CP3: loop de lectura/escritura de bloques
    // ---------------------------------------------------------
    log_info(logger, "SWAP listo. Esperando operaciones de Kernel Memory...");
    pause();
 
    // ---------------------------------------------------------
    // 9. Limpieza
    // ---------------------------------------------------------
    config_destroy(config);
    log_destroy(logger);
 
    return EXIT_SUCCESS;
}