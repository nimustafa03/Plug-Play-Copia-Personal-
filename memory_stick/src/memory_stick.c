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
int   memory_delay;

void atender_cpu(int fd_cpu){
    int size_id;
    int* id_cpu_ptr = recibir_mensaje(fd_cpu, &size_id);
    
    if (id_cpu_ptr == NULL) {
        log_error(logger, "No se pudo recibir el ID de la CPU al conectar.");
        return;
    }
    
    int id_cpu = *id_cpu_ptr;
    free(id_cpu_ptr);

    // LOG OBLIGATORIO CORREGIDO: Formato exacto del enunciado
    log_info(logger, "## CPU %d Conectada", id_cpu);[cite: 1]
    op_code ok = MSG_OK;
    enviar_mensaje(fd_cpu, &ok, sizeof(op_code));

    while(1){
        int size_orden;
        op_code* orden = recibir_mensaje(fd_cpu, &size_orden);

        if (orden == NULL) {
            log_warning(logger, "La CPU %d se desconectó", id_cpu);
            break;
        }

        int size_dir;
        int* dir_ptr = recibir_mensaje(fd_cpu, &size_dir);
        
        if (dir_ptr == NULL) {
            log_warning(logger, "La CPU %d se desconectó inesperadamente esperando la dirección física.", id_cpu);
            free(orden);
            break; 
        }

        int direccion_fisica = *dir_ptr;
        free(dir_ptr);

        usleep(memory_delay * 1000); // Simulación de retraso de hardware

        switch(*orden) {
            case MSG_WRITE: {
                int size_datos;
                void* datos = recibir_mensaje(fd_cpu, &size_datos);

                if (datos == NULL) {
                    log_error(logger, "CPU %d se desconectó enviando datos para escribir.", id_cpu);
                    break;
                }

                memcpy(espacio_memoria + direccion_fisica, datos, size_datos);

                log_info(logger, "## Escritura de %d bytes", size_datos);[cite: 1]
                op_code done = MSG_DONE;
                enviar_mensaje(fd_cpu, &done, sizeof(op_code));
                free(datos);
                break;
            }

            case MSG_READ: {
                int size_tam;
                int* tam_ptr = recibir_mensaje(fd_cpu, &size_tam);
                
                if (tam_ptr == NULL) {
                    log_error(logger, "CPU %d se desconectó esperando tamaño a leer.", id_cpu);
                    break;
                }
                
                int tamanio_a_leer = *tam_ptr;
                free(tam_ptr);

                void* buffer = malloc(tamanio_a_leer);
                memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);

                log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);[cite: 1]
                enviar_mensaje(fd_cpu, buffer, tamanio_a_leer);
                free(buffer);
                break;
            }
        }
        free(orden);
    }  
}

void* atender_km(void* arg){
    int fd_km = *((int*) arg);
    free(arg);

    while(1){
        int size_orden;
        op_code* orden = recibir_mensaje(fd_km, &size_orden);
        if (orden == NULL) {
            log_warning(logger, "## Kernel Memory se desconectó del Memory Stick");
            break;
        }

        int size_dir;
        int* dir_ptr = recibir_mensaje(fd_km, &size_dir);
        
        // VALIDACIÓN AGREGA EN KM: Protege el hilo administrativo
        if (dir_ptr == NULL) {
            log_error(logger, "Kernel Memory se desconectó esperando dirección física.");
            free(orden);
            break;
        }

        int direccion_fisica = *dir_ptr;
        free(dir_ptr);

        usleep(memory_delay * 1000);[cite: 1]

        switch(*orden){
            case MSG_WRITE: {
                int size_datos;
                void* datos = recibir_mensaje(fd_km, &size_datos);
                
                if (datos == NULL) {
                    log_error(logger, "Kernel Memory se desconectó enviando datos.");
                    break;
                }
                
                memcpy(espacio_memoria + direccion_fisica, datos, size_datos);
                log_info(logger, "## Escritura de %d bytes", size_datos);[cite: 1]
                op_code done = MSG_DONE;
                enviar_mensaje(fd_km, &done, sizeof(op_code));
                free(datos);
                break;
            }
            case MSG_READ: {
                int size_tam;
                int* tam_ptr = recibir_mensaje(fd_km, &size_tam);
                
                if (tam_ptr == NULL) {
                    log_error(logger, "Kernel Memory se desconectó esperando tamaño.");
                    break;
                }
                
                int tamanio_a_leer = *tam_ptr;
                free(tam_ptr);
                
                void* buffer = malloc(tamanio_a_leer);
                memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);
                log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);[cite: 1]
                enviar_mensaje(fd_km, buffer, tamanio_a_leer);
                free(buffer);
                break;
            }
        }
        free(orden);
    }
    return NULL;
}

void* esperar_cpu(void * arg){ 
    int fd_cliente = *((int*)arg);
    free(arg);
    
    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    if (codigo == NULL) {
        return NULL;
    }

    switch(*codigo){
        case(MSG_HANDSHAKE_CPU):
            atender_cpu(fd_cliente);
            break;
        default:
            log_warning(logger, "## CONEXION NO CORRESPONDE AL CPU. FD: %d", fd_cliente);
            break;
    }
    free(codigo);
    return NULL;
}

int main(int argc, char*argv[]) {
    if(argc != 3){
        printf("Error. Uso: ./bin/memory_stick [config] [tamaño]\n");
        return EXIT_FAILURE;
    }

    char *config_path = argv[1]; 
    int size = atoi(argv[2]);

    if(size < 0){
        printf("Error: Tamaño inválido.\n");
        return EXIT_FAILURE;
    }

    config = config_create(config_path);
    if (config == NULL){
        printf("Error: No se pudo abrir el config.\n");
        return EXIT_FAILURE;
    }
    
    memory_delay = config_get_int_value(config, "MEMORY_DELAY");
    char* LOG_LEVEL = config_get_string_value(config, "LOG_LEVEL");
    logger = log_create("memstickinfo.log", "MSINFO", true, log_level_from_string(LOG_LEVEL));

    espacio_memoria = malloc(size);
 
    char * KM_IP = config_get_string_value(config, "KM_IP");
    char * KM_PORT = config_get_string_value(config, "KM_PORT");
    int fd_km = crear_conexion(KM_IP, KM_PORT);
    
    if(fd_km == -1) {
        log_error(logger, "No se pudo conectar con Kernel Memory.");
        return EXIT_FAILURE;
    }

    op_code handshake = MSG_HANDSHAKE_MS;
    enviar_mensaje(fd_km, &handshake, sizeof(MSG_HANDSHAKE_MS));
    enviar_mensaje(fd_km, &size, sizeof(int));
    
    char* mi_ip = config_get_string_value(config, "IP"); 
    char* mi_port = config_get_string_value(config, "PORT");
    
    enviar_mensaje(fd_km, mi_ip, strlen(mi_ip) + 1);
    enviar_mensaje(fd_km, mi_port, strlen(mi_port) + 1);

    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (respuesta != NULL && *respuesta == MSG_OK) {
        log_info(logger, "## Conectado a Kernel Memory");[cite: 1]
    } else {
        log_error(logger, "## ATENCION. NO FUE POSIBLE CONECTARSE A KERNEL MEMORY.");
        if(respuesta != NULL) free(respuesta);
        return EXIT_FAILURE;
    }
    free(respuesta);

    int* fd_km_ptr = malloc(sizeof(int));
    *fd_km_ptr = fd_km;
    pthread_t hilo_km;
    pthread_create(&hilo_km, NULL, atender_km, fd_km_ptr);
    pthread_detach(hilo_km);
    
    char * PORT = config_get_string_value(config, "PORT");
    int fd_servidor = iniciar_servidor(PORT);

    while(1){
        log_info(logger, "## Esperando conexion de CPU...");

        int * fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        pthread_t hilo;
        pthread_create(&hilo, NULL, esperar_cpu, fd_cliente);
        pthread_detach(hilo);
    }
 
    return 0;
}