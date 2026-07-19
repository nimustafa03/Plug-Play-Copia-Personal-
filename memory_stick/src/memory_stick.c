// =============================================================
//  memory_stick.c  —  Módulo Memory Stick
//  Cómo ejecutar: ./bin/memory_stick memory_stick.config [tamaño]
//
//  Responsable CP1:
//    Nico M → cliente MS→KM
//             servidor MS acepta CPUs
//
//  Te toca conectarte a KM e informar tu tamaño,
//  y después levantar un servidor para las CPUs, éxitos!!!
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
int   memory_delay;

// NM: Vamos a leer el handshake que nos envía el cliente para asegurarnos que, efectivamente, es el CPU.


void atender_cpu(int fd_cpu){
        int size_id;
        int* id_cpu_ptr = recibir_mensaje(fd_cpu, &size_id);
        int id_cpu = *id_cpu_ptr;
        free(id_cpu_ptr);

        log_info(logger, "## CPU %d CONECTADA", id_cpu);
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
                break; // Rompe el while(1) de forma limpia sin pisar variables
            }

            int direccion_fisica = *dir_ptr;
            free(dir_ptr);

            usleep(memory_delay * 1000); // simulamos lo que tarda una memoria realmente

            switch(*orden) {
                case MSG_WRITE: {
                    int size_datos;
                    void* datos = recibir_mensaje(fd_cpu, &size_datos);

                    memcpy(espacio_memoria + direccion_fisica, datos, size_datos);

                    log_info(logger, "## Escritura de %d bytes", size_datos);
                    op_code done = MSG_DONE;
                    enviar_mensaje(fd_cpu, &done, sizeof(op_code));
                    free(datos);
                    break;

                }

                case MSG_READ: {
                    int size_tam;
                    int* tam_ptr = recibir_mensaje(fd_cpu, &size_tam);
                    int tamanio_a_leer = *tam_ptr;
                    free(tam_ptr);

                    void* buffer = malloc(tamanio_a_leer);

                    memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);

                    log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);
                    enviar_mensaje(fd_cpu, buffer, tamanio_a_leer);
                    free(buffer);
                    break;

                }

                default:
                    log_error(logger, "## Llegó código inesperado: %d", *orden);
                    break;
            }

            free(orden);
        }  
}

// CP3: la consigna (Módulo Memory Stick) indica que el MS atiende R/W "de las CPUs
// o del Kernel Memory". Este hilo sirve MSG_READ/MSG_WRITE sobre el socket con KM,
// con el mismo protocolo que atender_cpu (dir física local, arranca en 0).
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
        int direccion_fisica = *dir_ptr;
        free(dir_ptr);

        usleep(memory_delay * 1000);

        switch(*orden){
            case MSG_WRITE: {
                int size_datos;
                void* datos = recibir_mensaje(fd_km, &size_datos);
                memcpy(espacio_memoria + direccion_fisica, datos, size_datos);
                log_info(logger, "## Escritura de %d bytes", size_datos);
                op_code done = MSG_DONE;
                enviar_mensaje(fd_km, &done, sizeof(op_code));
                free(datos);
                break;
            }
            case MSG_READ: {
                int size_tam;
                int* tam_ptr = recibir_mensaje(fd_km, &size_tam);
                int tamanio_a_leer = *tam_ptr;
                free(tam_ptr);
                void* buffer = malloc(tamanio_a_leer);
                memcpy(buffer, espacio_memoria + direccion_fisica, tamanio_a_leer);
                log_info(logger, "## Lectura de %d bytes", tamanio_a_leer);
                enviar_mensaje(fd_km, buffer, tamanio_a_leer);
                free(buffer);
                break;
            }

            default:
                    log_error(logger, "## Llegó código inesperado: %d", *orden);
                    break;
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

    switch(*codigo){
        case(MSG_HANDSHAKE_CPU):
            atender_cpu(fd_cliente);
            break;
        default:
            log_warning(logger, "## CONEXION NO CORRESPONDE AL CPU. \n## FD RECIBIDA: %d \n## CODIGO DE OPERACION RECIBIDO: %d", fd_cliente, *codigo);
            break;
    }
    free(codigo);
    return NULL;
}


int main(int argc, char*argv[]) {
    // DONE Nico M: verificar argumentos (config + tamaño)
    // El tamaño viene por parámetro: argv[2]

    // NM: Guardamos los argumentos en variables :D
    if (argc != 3) {
        logger = log_create(
            "memstickerror.log",
            "MSERR",
            true,
            LOG_LEVEL_ERROR
        );

        log_error(
            logger,
            "Uso: %s <config_path> <tamanio>",
            argv[0]
        );

        log_destroy(logger);
        return EXIT_FAILURE;
    }
    char *config_path = argv[1]; char * char_size = argv[2]; 
    int size = atoi(char_size);

    // NM: Inicio logger antes para poder enviar mensaje de error en caso de argumentos nulos. Un tamaño menor a cero, por ejemplo, es inválido.
    if(argc != 3 || size < 0){
        logger = log_create("memstickerror.log","MSERR",true,LOG_LEVEL_ERROR);
        log_error(logger,"## ERROR. EXISTEN ARGUMENTOS INVALIDOS O NULOS \n## ARGUMENTOS RECIBIDOS: %d Abortando...", argc-1);
        log_destroy(logger);
        abort();
    }

    // DONE Nico M: cargar config con config_create()
    config = config_create(config_path);

    if (config == NULL) {
        t_log* error_logger = log_create(
            "memstickerror.log",
            "MSERR",
            true,
            LOG_LEVEL_ERROR
        );

        log_error(
            error_logger,
            "## ERROR. NO SE PUDO CREAR EL CONFIG. Abortando..."
        );

        log_destroy(error_logger);
        return EXIT_FAILURE;
    }

    memory_delay = config_get_int_value(config, "MEMORY_DELAY");

    if (config == NULL){
		t_log *error_logger = log_create("memstickerror.log","MSERR",true,LOG_LEVEL_ERROR);
		log_error(error_logger,"## ERROR. NO SE PUDO CREAR EL CONFIG. Abortando...");
		log_destroy(error_logger);
		abort();
	}
 
    // DONE Nico M: crear logger con log_create()
    char*LOG_LEVEL = config_get_string_value(config,"LOG_LEVEL");
    logger = log_create("memstickinfo.log", "MSINFO",true,log_level_from_string(LOG_LEVEL));
    log_debug(logger,"## config_path recibido: %s",config_path);
    log_debug(logger,"## tamaño de memory stick recibido: %s",char_size);


    // DONE Nico M: reservar memoria con malloc(tamaño)
    espacio_memoria = malloc(size);
 
    // DONE Nico M: conectarse a Kernel Memory
    char * KM_IP = config_get_string_value(config,"KM_IP");
    char * KM_PORT = config_get_string_value(config,"KM_PORT");
    int fd_km = crear_conexion(KM_IP, KM_PORT);
    
    // DONE enviar handshake MSG_HANDSHAKE_MS.
    op_code handshake = MSG_HANDSHAKE_MS;
    enviar_mensaje(fd_km,&handshake,sizeof(MSG_HANDSHAKE_MS));
    
    // DONE enviar el tamaño para que KM lo registre
    enviar_mensaje(fd_km,&size,sizeof(int));
    char* mi_ip = config_get_string_value(config, "IP"); 
    char* mi_port = config_get_string_value(config, "PORT");
    
    // Enviamos la IP y el Puerto incluyendo el terminador nulo (+ 1) 
    // para que el Kernel Memory los reciba con recibir_mensaje() correctamente
    enviar_mensaje(fd_km, mi_ip, strlen(mi_ip) + 1);
    enviar_mensaje(fd_km, mi_port, strlen(mi_port) + 1);

    // DONE Esperar OK de KM y loguear conexión exitosa (log obligatorio)
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (*respuesta == MSG_OK)
        log_info(logger, "## Conectado a Kernel Memory");
    else
        log_warning(logger, "## ATENCION. NO FUE POSIBLE CONECTARSE A KERNEL MEMORY.");
    free(respuesta);

    // CP3: hilo que atiende las lecturas/escrituras que pide Kernel Memory (STDIN/STDOUT).
    int* fd_km_ptr = malloc(sizeof(int));
    *fd_km_ptr = fd_km;
    pthread_t hilo_km;
    pthread_create(&hilo_km, NULL, atender_km, fd_km_ptr);
    pthread_detach(hilo_km);

    // TODO Nico M: levantar servidor para CPUs
    
    char * PORT = config_get_string_value(config, "PORT");
    int fd_servidor = iniciar_servidor(PORT);


    // loop con esperar_cliente() + pthread_create() + pthread_detach()

    while(1){
        log_info(logger, "## Esperando conexion de CPU...");

        int * fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        pthread_t hilo;
        pthread_create(&hilo,NULL,esperar_cpu,fd_cliente);
        pthread_detach(hilo);
    }

    // cuando llega MSG_HANDSHAKE_CPU → log_info(logger, "## CPU <ID> Conectada");
 
    // TODO CP3: lectura y escritura de memoria física
 
    return 0;
}