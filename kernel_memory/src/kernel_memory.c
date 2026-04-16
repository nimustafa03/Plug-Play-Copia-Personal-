// =============================================================
//  kernel_memory.c  —  Módulo Kernel Memory
//  Cómo ejecutar: ./bin/kernel_memory kernel_memory.config
//
//  Responsables CP1:
//    Nico S  → servidor KM acepta KS  (conexión 1, lado servidor)
//              servidor KM acepta CPU (conexión 5, lado servidor)
//              servidor KM acepta MS  (conexión 6, lado servidor)
//    Santiago → servidor KM acepta SWAP (conexión 3, lado servidor)
// =============================================================

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>

t_log*    logger;
t_config* config;

// ── Variables de info del SWAP ─────────────────────────
int fd_swap         = -1;
int swap_block_size = 0;
int swap_total_size = 0;

// ── Variables de info del Kernel Scheduler ─────────────
int fd_kernel_scheduler = 0;

// ── Variables de info de Memory Stick ──────────────────
int fd_memory_stick   = 0;
int memory_stick_size = 0;

// ── Variables de info de CPU ───────────────────────────
int fd_cpu = 0;
int id_cpu = 0;

// -----------------------------------------------------------------
//  atender_swap  —  SANTIAGO
//  Lee el BLOCK_SIZE y tamaño total que manda el SWAP al conectarse.
// -----------------------------------------------------------------
void atender_swap(int fd) {
    fd_swap = fd;

    int size;

    int* block_size = recibir_mensaje(fd, &size);
    swap_block_size = *block_size;
    free(block_size);

    int* total_size = recibir_mensaje(fd, &size);
    swap_total_size = *total_size;
    free(total_size);

    log_info(logger,
             "SWAP conectado - Block size: %d bytes - Tamaño total: %d bytes",
             swap_block_size, swap_total_size);

    // Checkpoint 3: acá va el loop de lectura/escritura de bloques
}

void atender_kernel_scheduler(int fd_kernel_scheduler) {
    log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd_kernel_scheduler);
}

void atender_cpu(int fd_cpu) {
    int size;
    int* ptr_id_cpu = recibir_mensaje(fd_cpu, &size);
    id_cpu = *ptr_id_cpu;

    free(ptr_id_cpu);

    log_info(logger, "## CPU %d Conectada", id_cpu);
}

void atender_memory_stick(int fd_memory_stick) {
    int memory_stick_size;

    int* ptr_memory_stick_size = recibir_mensaje(fd_memory_stick, &memory_stick_size);
    memory_stick_size = *ptr_memory_stick_size;
    free(ptr_memory_stick_size);
    
    log_info(logger, "## Memory Stick de %d bytes Conectada", memory_stick_size);
}

// -----------------------------------------------------------------
//  atender_cliente_km
//  Corre en un hilo por cada cliente que se conecta al servidor KM.
//  Lee el handshake e identifica quién es.
// -----------------------------------------------------------------
void* atender_cliente_km(void* arg) {
    int fd_cliente = *((int*) arg);
    free(arg);

    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    switch (*codigo) {

        case MSG_HANDSHAKE_KS:
            atender_kernel_scheduler(fd_cliente);
            break;

        case MSG_HANDSHAKE_CPU:
            atender_cpu(fd_cliente);
            break;

        case MSG_HANDSHAKE_MS:
            atender_memory_stick(fd_cliente);
            break;

        case MSG_HANDSHAKE_SWAP:
            atender_swap(fd_cliente);
            break;
        // ─────────────────────────────────────────────────────

        default:
            log_warning(logger, "Conexion desconocida - codigo: %d - FD: %d",
                        *codigo, fd_cliente);
            break;
    }

    free(codigo);
    return NULL;
}

// -----------------------------------------------------------------
//  main
// -----------------------------------------------------------------
int main(int argc, char* argv[]) {

    // ---------------------------------------------------------
    // 1. Verificar argumentos
    // ---------------------------------------------------------
    if (argc < 2) {
        printf("Uso: ./bin/kernel_memory [archivo_config]\n");
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
    logger = log_create("kernel_memory.log",
                        "KernelMemory",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger == NULL) {
        printf("Error: no se pudo crear el logger\n");
        return EXIT_FAILURE;
    }

    // ---------------------------------------------------------
    // 4. Leer puerto del config
    // ---------------------------------------------------------
    char* km_port = config_get_string_value(config, "PORT");

    // ---------------------------------------------------------
    // 5. Levantar servidor — TODO NICO S
    //    KM es el servidor central, todos se conectan acá
    // ---------------------------------------------------------
    int fd_servidor = iniciar_servidor(km_port);
    log_info(logger, "Kernel Memory listo en puerto %s. Esperando conexiones...", km_port);

    // ---------------------------------------------------------
    // 6. Loop que acepta todas las conexiones entrantes
    //    (KS, CPUs, Memory Sticks y SWAP)
    // ---------------------------------------------------------
    while (1) {
        int* fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente_km, fd_cliente);
        pthread_detach(hilo);
    }

    // ---------------------------------------------------------
    // 7. Limpieza
    // ---------------------------------------------------------
    config_destroy(config);
    log_destroy(logger);

    return EXIT_SUCCESS;
}