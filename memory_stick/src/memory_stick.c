// =============================================================
//  memory_stick.c  —  Módulo Memory Stick
//  Cómo ejecutar: ./bin/memory_stick memory_stick.config [tamaño]
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
 
t_log*    logger;
t_config* config;
void* espacio_memoria;
int tamanio_memoria; 
int   memory_delay;

// mutex para evitar race condition en lecturas/escrituras concurrentes
pthread_mutex_t mutex_memoria = PTHREAD_MUTEX_INITIALIZER;

void atender_cpu(int fd_cpu) {
    int size_id;
    int* id_cpu_ptr = recibir_mensaje(fd_cpu, &size_id);
    if (id_cpu_ptr == NULL) {
        log_error(logger, "[DBG][atender_cpu] No se pudo recibir ID de la CPU. Cerrando socket FD: %d", fd_cpu);
        close(fd_cpu);
        return;
    }
    int id_cpu = *id_cpu_ptr;
    free(id_cpu_ptr);

    log_info(logger, "## CPU %d CONECTADA", id_cpu);
    op_code ok = MSG_OK;
    enviar_mensaje(fd_cpu, &ok, sizeof(op_code));

    while (1) {
        int size_orden;
        op_code* orden = recibir_mensaje(fd_cpu, &size_orden);

        if (orden == NULL) {
            log_warning(logger, "La CPU %d se desconectó", id_cpu);
            break;
        }

        log_debug(logger, "[DBG][atender_cpu] CPU %d envió orden opcode: %d (size: %d)", id_cpu, *orden, size_orden);

        int size_dir;
        int* dir_ptr = recibir_mensaje(fd_cpu, &size_dir);
        if (dir_ptr == NULL) {
            log_warning(logger, "La CPU %d se desconectó inesperadamente esperando la dirección física.", id_cpu);
            free(orden);
            break;
        }

        int direccion_fisica = *dir_ptr;
        free(dir_ptr);

        log_debug(logger, "[DBG][atender_cpu] CPU %d -> Dirección física local: %d", id_cpu, direccion_fisica);

        usleep(memory_delay * 1000); // delay configurado

        switch (*orden) {
            case MSG_WRITE: {
                int size_cant;
                int* cant_ptr = recibir_mensaje(fd_cpu, &size_cant);
                if (cant_ptr == NULL) {
                    log_error(logger, "[DBG][atender_cpu] Error al recibir cantidad de bytes a escribir desde CPU %d", id_cpu);
                    break;
                }
                int bytes_a_escribir = *cant_ptr;
                free(cant_ptr);

                log_debug(logger, "[DBG][atender_cpu] CPU %d -> Bytes a escribir indicados: %d", id_cpu, bytes_a_escribir);

                int size_datos;
                void* datos = recibir_mensaje(fd_cpu, &size_datos);
                if (datos == NULL) {
                    log_error(logger, "[DBG][atender_cpu] Error al recibir el buffer de datos desde CPU %d", id_cpu);
                    break;
                }

                // proteccion de limites de memoria
                if (direccion_fisica + bytes_a_escribir > tamanio_memoria) {
                    log_error(logger, "##ERROR: CPU %d intentó escribir fuera de limites (%d > %d)", id_cpu, direccion_fisica + bytes_a_escribir, tamanio_memoria);
                    free(datos);
                    break;
                }

                log_debug(logger, "[DBG][atender_cpu] CPU %d -> Datos recibidos correctamente (size_datos: %d)", id_cpu, size_datos);

                pthread_mutex_lock(&mutex_memoria);
                memcpy(espacio_memoria + direccion_fisica, datos, bytes_a_escribir);
                pthread_mutex_unlock(&mutex_memoria);

                log_info(logger, "## Escritura de %d bytes", bytes_a_escribir);
                
                op_code done = MSG_DONE;
                enviar_mensaje(fd_cpu, &done, sizeof(op_code));

                log_debug(logger, "[DBG][atender_cpu] CPU %d -> Enviado MSG_DONE a CPU", id_cpu);
                
                free(datos);
                break;
            }

            case MSG_READ: {
                int size_tam;
                int* tam_ptr = recibir_mensaje(fd_cpu, &size_tam);
                if (tam_ptr == NULL) {
                    log_error(logger, "[DBG][atender_cpu] Error al recibir tamaño a leer desde CPU %d", id_cpu);
                    break;
                }
                int tamanio_a_leer = *tam_ptr;
                free(tam_ptr);

                log_debug(logger, "[DBG][atender_cpu] CPU %d -> Tamaño a leer: %d bytes", id_cpu, tamanio_a_leer);

                // proteccion de limites de memoria
                if (direccion_fisica + tamanio_a_leer > tamanio_memoria) {
                    log_error(logger, "##ERROR: CPU %d intentó leer fuera de limites (%d > %d)", id_cpu, direccion_fisica + tamanio_a_leer, tamanio_memoria);
                    break;
                }

                void* buffer = malloc(tamanio_a_leer);

                pthread_mutex_lock(&mutex_memoria);
                memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);
                pthread_mutex_unlock(&mutex_memoria);

                log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);
                enviar_mensaje(fd_cpu, buffer, tamanio_a_leer);

                log_debug(logger, "[DBG][atender_cpu] CPU %d -> Enviados %d bytes leídos a CPU", id_cpu, tamanio_a_leer);

                free(buffer);
                break;
            }

            default:
                log_error(logger, "## Llegó código inesperado de CPU %d: %d", id_cpu, *orden);
                break;
        }

        free(orden);
    }  
}

void* atender_km(void* arg) {
    int fd_km = *((int*) arg);
    free(arg);

    while (1) {
        int size_orden;
        op_code* orden = recibir_mensaje(fd_km, &size_orden);
        if (orden == NULL) {
            log_warning(logger, "## Kernel Memory se desconectó del Memory Stick");
            break;
        }

        log_debug(logger, "[DBG][atender_km] KM envió orden opcode: %d (size: %d)", *orden, size_orden);

        int size_dir;
        int* dir_ptr = recibir_mensaje(fd_km, &size_dir);
        if (dir_ptr == NULL) {
            log_warning(logger, "[DBG][atender_km] KM se desconectó esperando dirección física");
            free(orden);
            break;
        }
        int direccion_fisica = *dir_ptr;
        free(dir_ptr);

        log_debug(logger, "[DBG][atender_km] KM -> Dirección física local: %d", direccion_fisica);

        usleep(memory_delay * 1000);

        switch (*orden) {
            case MSG_WRITE: {
                int size_cant;
                int* cant_ptr = recibir_mensaje(fd_km, &size_cant);
                if (cant_ptr == NULL) {
                    log_error(logger, "[DBG][atender_km] Error al recibir cantidad de bytes a escribir desde KM");
                    break;
                }
                int bytes_a_escribir = *cant_ptr;
                free(cant_ptr);

                log_debug(logger, "[DBG][atender_km] KM -> Bytes a escribir: %d", bytes_a_escribir);

                int size_datos;
                void* datos = recibir_mensaje(fd_km, &size_datos);
                if (datos == NULL) {
                    log_error(logger, "[DBG][atender_km] Error al recibir buffer de datos desde KM");
                    break;
                }

                // proteccion de memoria
                if (direccion_fisica + bytes_a_escribir > tamanio_memoria) {
                    log_error(logger, "## ERROR: KM intentó escribir fuera de limites (%d > %d)", direccion_fisica + bytes_a_escribir, tamanio_memoria);
                    free(datos);
                    break;
                }

                pthread_mutex_lock(&mutex_memoria);
                memcpy(espacio_memoria + direccion_fisica, datos, bytes_a_escribir);
                pthread_mutex_unlock(&mutex_memoria);

                log_info(logger, "## Escritura de %d bytes", bytes_a_escribir);

                op_code done = MSG_DONE;
                enviar_mensaje(fd_km, &done, sizeof(op_code));
                log_debug(logger, "[DBG][atender_km] KM -> Enviado MSG_DONE a KM");

                free(datos);
                break;
            }
            case MSG_READ: {
                int size_tam;
                int* tam_ptr = recibir_mensaje(fd_km, &size_tam);
                if (tam_ptr == NULL) {
                    log_error(logger, "[DBG][atender_km] Error al recibir tamaño a leer desde KM");
                    break;
                }
                int tamanio_a_leer = *tam_ptr;
                free(tam_ptr);

                log_debug(logger, "[DBG][atender_km] KM -> Tamaño a leer: %d bytes", tamanio_a_leer);

                if (direccion_fisica + tamanio_a_leer > tamanio_memoria) {
                    log_error(logger, "## ERROR: KM intentó leer fuera de limites (%d > %d)", direccion_fisica + tamanio_a_leer, tamanio_memoria);
                    break;
                }

                void* buffer = malloc(tamanio_a_leer);

                pthread_mutex_lock(&mutex_memoria);
                memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);
                pthread_mutex_unlock(&mutex_memoria);

                log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);

                enviar_mensaje(fd_km, buffer, tamanio_a_leer);

                log_debug(logger, "[DBG][atender_km] KM -> Enviados %d bytes leídos a KM", tamanio_a_leer);

                free(buffer);
                break;
            }

            default:
                log_error(logger, "## Llegó código inesperado desde KM: %d", *orden);
                break;
        }
        free(orden);
    }
    return NULL;
}

void* esperar_cpu(void * arg) { 
    int fd_cliente = *((int*)arg);
    free(arg);
    
    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    if (codigo == NULL) {
        log_warning(logger, "[DBG][esperar_cpu] Conexión cliente cerrada antes del handshake. FD: %d", fd_cliente);
        close(fd_cliente);
        return NULL;
    }

    log_debug(logger, "[DBG][esperar_cpu] Handshake recibido en FD %d -> opcode: %d", fd_cliente, *codigo);

    switch (*codigo) {
        case MSG_HANDSHAKE_CPU:
            atender_cpu(fd_cliente);
            break;
        default:
            log_warning(logger, "## CONEXION NO CORRESPONDE AL CPU. \n## FD RECIBIDA: %d \n## CODIGO DE OPERACION RECIBIDO: %d", fd_cliente, *codigo);
            close(fd_cliente);
            break;
    }
    free(codigo);
    return NULL;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Uso: %s <config_path> <tamanio>\n", argv[0]);
        return EXIT_FAILURE;
    }
    char *config_path = argv[1]; 
    char *char_size = argv[2]; 
    int size = atoi(char_size);

    if (size <= 0) {
        printf("Error: el tamaño debe ser mayor a 0\n");
        return EXIT_FAILURE;
    }

    config = config_create(config_path);
    if (config == NULL) {
        printf("Error al abrir config en: %s\n", config_path);
        return EXIT_FAILURE;
    }

    memory_delay = config_get_int_value(config, "MEMORY_DELAY");
 
    char* LOG_LEVEL = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("memstickinfo.log", "MSINFO", true, log_level_from_string(LOG_LEVEL));
    
    log_debug(logger, "[DBG][main] MS iniciado. Config: %s | Tamaño: %d | Delay: %d ms", config_path, size, memory_delay);

    espacio_memoria = malloc(size);
    if (espacio_memoria == NULL) {
        log_error(logger, "[DBG][main] No se pudo reservar espacio_memoria de %d bytes", size);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
 
    char * KM_IP = config_get_string_value(config, "KM_IP");
    char * KM_PORT = config_get_string_value(config, "KM_PORT");
    int fd_km = crear_conexion(KM_IP, KM_PORT);
    
    if (fd_km == -1) {
        log_error(logger, "No se pudo conectar a Kernel Memory en %s:%s", KM_IP, KM_PORT);
        free(espacio_memoria);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }
    
    op_code handshake = MSG_HANDSHAKE_MS;
    enviar_mensaje(fd_km, &handshake, sizeof(op_code));
    
    enviar_mensaje(fd_km, &size, sizeof(int));
    char* mi_ip = config_get_string_value(config, "IP"); 
    char* mi_port = config_get_string_value(config, "PORT");
    
    enviar_mensaje(fd_km, mi_ip, strlen(mi_ip) + 1);
    enviar_mensaje(fd_km, mi_port, strlen(mi_port) + 1);

    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (respuesta != NULL && *respuesta == MSG_OK) {
        log_info(logger, "## Conectado a Kernel Memory");
        free(respuesta);
    } else {
        log_warning(logger, "## ATENCION. NO FUE POSIBLE CONECTARSE A KERNEL MEMORY.");
        if (respuesta) free(respuesta);
        close(fd_km);
        free(espacio_memoria);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    int* fd_km_ptr = malloc(sizeof(int));
    *fd_km_ptr = fd_km;
    pthread_t hilo_km;
    pthread_create(&hilo_km, NULL, atender_km, fd_km_ptr);
    pthread_detach(hilo_km);

    char * PORT = config_get_string_value(config, "PORT");
    int fd_servidor = iniciar_servidor(PORT);
    log_info(logger, "Servidor MS escuchando CPUs en puerto %s", PORT);

    while (1) {
        log_info(logger, "## Esperando conexion de CPU...");

        int * fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        log_debug(logger, "[DBG][main] Aceptado nuevo cliente FD: %d", *fd_cliente);

        pthread_t hilo;
        pthread_create(&hilo, NULL, esperar_cpu, fd_cliente);
        pthread_detach(hilo);
    }

    pthread_mutex_destroy(&mutex_memoria);
    free(espacio_memoria);
    config_destroy(config);
    log_destroy(logger);

    return 0;
}