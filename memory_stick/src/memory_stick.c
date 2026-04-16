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
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
 
t_log*    logger;
t_config* config;
 
int main(int argc, char*argv[]) {

    // TODO Nico M: verificar argumentos (config + tamaño)
    // El tamaño viene por parámetro: argv[2]

    // NM: Guardamos los argumentos en variables :D
    char *config_path = argv[1]; char * char_size = argv[2]; 
    int size = atoi(char_size);

    // NM: Inicio logger antes para poder enviar mensaje de error en caso de argumentos nulos. Un tamaño menor a cero, por ejemplo, es inválido.
    if(argc != 3 || size < 0){
        logger = log_create("memstickerror.log","MSERR",true,LOG_LEVEL_ERROR);
        log_error(logger,"ERROR. EXISTEN ARGUMENTOS INVALIDOS O NULOS. Abortando...");
        log_destroy(logger);
        abort();
    }

    // TODO Nico M: cargar config con config_create()
    config = config_create(config_path);
    if (config == NULL){
		t_log *error_logger = log_create("memstickerror.log","MSERR",true,LOG_LEVEL_ERROR);
		log_error(error_logger,"ERROR. NO SE PUDO CREAR EL CONFIG. Abortando...");
		log_destroy(error_logger);
		abort();
	}
 
    // TODO Nico M: crear logger con log_create()
    char*LOG_LEVEL = config_get_string_value(config,"LOG_LEVEL");
    logger = log_create("memstickinfo.log", "MSINFO",true,log_level_from_string(LOG_LEVEL));
    log_info(logger,config_path);
    log_info(logger,char_size);


    // TODO Nico M: reservar memoria con malloc(tamaño)
    malloc(size);
 
    // TODO Nico M: conectarse a Kernel Memory
    char * KM_IP = config_get_string_value(config,"KM_IP");
    char * KM_PORT = config_get_string_value(config,"KM_PORT");
    int fd_km = crear_conexion(KM_IP, KM_PORT);
    
    // enviar handshake MSG_HANDSHAKE_MS.
    op_code handshake = MSG_HANDSHAKE_MS;
    enviar_mensaje(fd_km,&handshake,sizeof(MSG_HANDSHAKE_MS));
    
    // enviar el tamaño para que KM lo registre
    enviar_mensaje(fd_km,&size,sizeof(int));

    // Esperar OK de KM y loguear conexión exitosa (log obligatorio)
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    if (*respuesta == MSG_OK)
        log_info(logger, "## Conectado a Kernel Memory.");
    free(respuesta);

    // Borra cuando lo leas Nico: 
    // agregué el recibir_mensaje para esperar el OK de KM antes de loguear
    // el log obligatorio ahora está adentro del if, no lo muevas

    // TODO Nico M: levantar servidor para CPUs
    

    // int fd_servidor = iniciar_servidor(PORT);
    // loop con esperar_cliente() + pthread_create() + pthread_detach()
    // cuando llega MSG_HANDSHAKE_CPU → log_info(logger, "## CPU <ID> Conectada");
 
    // TODO CP3: lectura y escritura de memoria física
 
    return 0;
}