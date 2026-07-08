// =============================================================
//  swap.c  —  Módulo SWAP  (completo)
//  
//  Para eajecutar: ./bin/swap swap.config
// =============================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>

t_log*    logger;
t_config* config;

int   swap_fd;        // fd del archivo de swap
int   block_size;

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Uso: ./bin/swap [archivo_config]\n");
        return EXIT_FAILURE;
    }

    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el config: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    logger = log_create("swap.log", "SWAP", true,
                        log_level_from_string(config_get_string_value(config, "LOG_LEVEL")));
    if (logger == NULL) { printf("Error: no se pudo crear el logger\n"); return EXIT_FAILURE; }

    char* km_ip    = config_get_string_value(config, "KM_IP");
    char* km_port  = config_get_string_value(config, "KM_PORT");
    char* path     = config_get_string_value(config, "SWAP_FILE_PATH");
    int   swap_size = config_get_int_value(config, "SWAP_FILE_SIZE");
    block_size     = config_get_int_value(config, "BLOCK_SIZE");

    // archivo de swap: se crea al tamaño total. Se asume vacío (no hace falta limpiarlo).
    swap_fd = open(path, O_RDWR | O_CREAT, 0644);
    if (swap_fd == -1) {
        log_error(logger, "No se pudo abrir/crear el archivo de swap: %s", path);
        return EXIT_FAILURE;
    }
    if (ftruncate(swap_fd, swap_size) != 0) {
        log_error(logger, "No se pudo dimensionar el archivo de swap a %d bytes", swap_size);
        return EXIT_FAILURE;
    }

    // conexión a KM + handshake (informa block_size y tamaño total)
    int fd_km = crear_conexion(km_ip, km_port);
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%s", km_ip, km_port);
        return EXIT_FAILURE;
    }
    op_code codigo = MSG_HANDSHAKE_SWAP;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    enviar_mensaje(fd_km, &block_size, sizeof(int));
    enviar_mensaje(fd_km, &swap_size,  sizeof(int));

    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (respuesta != NULL && *respuesta == MSG_OK)
        log_info(logger, "## Conectado a Kernel Memory");
    free(respuesta);

    // loop de operaciones de bloque (siempre 1 bloque por operación)
    while (1) {
        int size;
        op_code* orden = recibir_mensaje(fd_km, &size);
        if (orden == NULL) {
            log_warning(logger, "## Kernel Memory se desconectó");
            break;
        }

        int* nro_ptr = recibir_mensaje(fd_km, &size);
        int nro_bloque = *nro_ptr;
        free(nro_ptr);
        off_t offset = (off_t) nro_bloque * block_size;

        switch (*orden) {
            case MSG_SWAP_WRITE: {
                int size_datos;
                void* datos = recibir_mensaje(fd_km, &size_datos);
                pwrite(swap_fd, datos, block_size, offset);
                log_info(logger, "## Escritura del bloque: %d", nro_bloque);
                op_code ok = MSG_OK;
                enviar_mensaje(fd_km, &ok, sizeof(op_code));
                free(datos);
                break;
            }
            case MSG_SWAP_READ: {
                void* buffer = malloc(block_size);
                pread(swap_fd, buffer, block_size, offset);
                log_info(logger, "## Lectura del bloque: %d", nro_bloque);
                enviar_mensaje(fd_km, buffer, block_size);
                free(buffer);
                break;
            }
            default:
                log_warning(logger, "## Operación desconocida: %d", *orden);
                break;
        }
        free(orden);
    }

    close(swap_fd);
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}