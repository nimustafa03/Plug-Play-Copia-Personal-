#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>    
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <unistd.h> 

void funcion_cpu(char *archivo_config, int id) {
    t_log* logger_cpu;
    logger_cpu = log_create("funcion_cpu.log","FUNCION CPU LOGGER",1,LOG_LEVEL_INFO);
    if (logger_cpu == NULL){
	    perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
	    exit(EXIT_FAILURE);
    }
    int identificador_cpu = id;
    // Tomo el .config pasado por argumento
    t_config* config = config_create(archivo_config);
    if (config == NULL) {
        log_info(logger_cpu,"Error: no se pudo abrir el archivo de config\n");
    }
    // Valido que existan las claves
    if (!config_has_property(config, "KS_IP") || !config_has_property(config, "KS_PORT") ||
        !config_has_property(config, "KM_IP") || !config_has_property(config, "KM_PORT") ||
        !config_has_property(config, "MS_IP") || !config_has_property(config, "MS_PORT")) {
        log_info(logger_cpu, "Error: faltan claves en el archivo de config\n");
        config_destroy(config);
    }
    // Obtener valores
    char *ks_ip = config_get_string_value(config, "KS_IP");
    char *km_ip = config_get_string_value(config, "KM_IP");
    char *ms_ip = config_get_string_value(config, "MS_IP");
    char *ks_port = config_get_string_value(config, "KS_PORT");
    char *km_port = config_get_string_value(config, "KM_PORT");
    char *ms_port = config_get_string_value(config, "MS_PORT");

    // Mostrar valores
    log_info(logger_cpu,"KS_IP: %s KM_IP: %s MS_IP: %s\n", ks_ip, km_ip, ms_ip);
    log_info(logger_cpu,"KS_PORT: %s KM_PORT: %s MS_PORT: %s\n", ks_port, km_port, ms_port);


    config_destroy(config);

/*----------------------------------INICIAR CONEXION CON SERVIDORES -------------------------------*/
    
    log_info(logger_cpu, "Conectando con Servidores");

    int fd_ks = crear_conexion(ks_ip, ks_port);
    if (fd_ks != 0){
        log_error(logger_cpu, "Error al conectar con KS");
        exit(EXIT_FAILURE);
        }else log_info(logger_cpu, "Conexion con KS exitosa");

    int fd_km = crear_conexion(km_ip, km_port);
    if (fd_km != 0){
        log_error(logger_cpu, "Error al conectar con KM");
        exit(EXIT_FAILURE);
        }else log_info(logger_cpu, "Conexion con KM exitosa");

    int fd_ms = crear_conexion(ms_ip, ms_port);
    if (fd_ms != 0){
        log_error(logger_cpu, "Error al conectar con MS");
        exit(EXIT_FAILURE);
        }else log_info(logger_cpu, "Conexion con MS exitosa");
        

/*--------------------------------------- HANDSHAKE ---------------------------------------*/

    log_info(logger_cpu, "Enviando HANDSHAKE a servidores");

    op_code handshake = MSG_HANDSHAKE_CPU;
    op_code* respuesta;

    enviar_mensaje(fd_ks, &handshake, sizeof(op_code));
    int size_resp_ks;
    respuesta = recibir_mensaje(fd_ks, &size_resp_ks);
    if (respuesta == NULL)
        log_error(logger_cpu, "Error al recibir mensaje desde KS");
        exit(EXIT_FAILURE);
    if (*respuesta == MSG_OK)
        log_info(logger_cpu, "Handshake con KS exitoso");
        else if (*respuesta == MSG_ERROR)
            log_error(logger_cpu, "Handshake con KS FALLIDO");
    free(respuesta);

    enviar_mensaje(fd_km, &handshake, sizeof(op_code));
    int size_resp_km;
    respuesta = recibir_mensaje(fd_km, &size_resp_km);
    if (respuesta == NULL)
        log_error(logger_cpu, "Error al recibir mensaje desde KM");
        exit(EXIT_FAILURE);
    if (*respuesta == MSG_OK)
        log_info(logger_cpu, "Handshake con KM exitoso");
        else if (*respuesta == MSG_ERROR)
            log_error(logger_cpu, "Handshake con KM FALLIDO");
    free(respuesta);

    enviar_mensaje(fd_ms, &handshake, sizeof(op_code));
    int size_resp_ms;
    respuesta = recibir_mensaje(fd_ms, &size_resp_ms);
    if (respuesta == NULL)
        log_error(logger_cpu, "Error al recibir mensaje desde MS");
        exit(EXIT_FAILURE);
    if (*respuesta == MSG_OK)
        log_info(logger_cpu, "Handshake con MS exitoso");
        else if (*respuesta == MSG_ERROR)
            log_error(logger_cpu, "Handshake con MS FALLIDO");
    free(respuesta);
    
/*--------------------------------------- CIERRE PROGRAMA ---------------------------------------*/

    log_info(logger_cpu, "TERMINANDO PROGRAMA");
    log_destroy(logger_cpu);
    close(fd_ks);
    close(fd_km);
    close(fd_ms);
    log_info(logger_cpu, "FIN MAIN");

}//FIN CPU MS
