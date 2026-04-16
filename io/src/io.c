// =============================================================
//  io.c  —  Módulo IO
//  Cómo ejecutar: ./bin/io io.config [tipo]
//  Tipos posibles: STDIN / STDOUT / SLEEP
//
//  Responsable CP1:
//    Bianca → cliente IO→KS
//
//  Te toca conectarte al Kernel Scheduler e informar tu tipo.
//  Dale que se puede
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
 
t_config* config;
t_log* logger;

 
int main(int argc, char* argv[]) {
 
    // Verificar argumentos (config + tipo)
    // El tipo viene por parámetro: argv[2] → "STDIN", "STDOUT" o "SLEEP"

    // (Si argc es menor a 2, es que no escribieron el nombre del archivo)
    if (argc < 2) {
        printf("Error: Falta el archivo de configuracion\n Uso: ./bin/io [archivo_config] [TIPO]\n TIPO: STDIN/STDOUT/SLEEP");
        return EXIT_FAILURE;
    }
 
    // Cargar config con config_create()
    config = config_create(argv[1]);
    // Verificar que exista
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
 
    // Crear logger 
    // 1. Primero leemos la palabra del archivo config
    char* nivel_de_log_texto = config_get_string_value(config, "LOG_LEVEL");
    // 2. La traducimos al "idioma" que entiende el Logger
    t_log_level nivel_traducido = log_level_from_string(nivel_de_log_texto);
    // 3. Usamos ese nivel traducido al crear el Logger
    logger = log_create("io.log", "IO", true, nivel_traducido);

    // Conexión a Kernel Scheduler
    // 1. Obtener el value de las variables del config
    char* ip = config_get_string_value(config, "KS_IP");
    char* puerto = config_get_string_value(config, "KS_PORT");

    // 2. Conectarse 
    int fd_ks = crear_conexion(ip, puerto); // int fd_ks = crear_conexion(KS_IP, KS_PORT);

    // 3. Manejo de errores en la conexión
    if (fd_ks == -1) {
        log_error(logger, "No se pudo conectar a Kernel Scheduler en %s:%s",
                  ip, puerto);
        return EXIT_FAILURE;
    }

    // Realizar handshake (con MSG_HANDSHAKE_IO)
    // 1. Enviar saludo
    op_code saludo = MSG_HANDSHAKE_IO;
    enviar_mensaje(fd_ks, &saludo, sizeof(op_code));

    // 2. Esperar OK 
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_ks, &size_resp);
    
    if (*respuesta == MSG_OK) {
        // Log obligatorio: log_info(logger, "## Conectado a Kernel Scheduler");
        log_info(logger, "## Conectado a Kernel Scheduler y aceptado");
    }
    free(respuesta); // Liberar la memoria del mensaje recibido
 
    // Quedarse esperando peticiones del KS
    pause();
    // pause(); por ahora alcanza para CP1
    
    // TODO CP2: implementar STDIN, STDOUT y SLEEP

    // Eliminar el config y el log para liberar la memoria
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
    
