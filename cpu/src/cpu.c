// =============================================================
//  cpu.c  —  Módulo CPU
//  Cómo ejecutar: ./bin/cpu cpu.config [identificador]
//
//  Responsable CP1:
//    Adriel → cliente CPU→KS
//             cliente CPU→KM
//             cliente CPU→MS
//
//  Te toca conectarte a los tres módulos. 
//  Copía donde corresponde lo que habías hecho
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/string.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <cpu.h>

t_config* config;

void conexionCPUKernelScheduler (t_log* logger_cpu, int id, const char* archivo_config) {
    log_info(logger_cpu, "EJECUTANDO CPU %d, %s",id, archivo_config);
    t_config* config = config_create(archivo_config);
    char *ks_port = config_get_string_value(config, "KS_PORT");
    char *ks_ip = config_get_string_value(config, "KS_IP");
    int fd_ks = crear_conexion(ks_ip, ks_port);
    log_info(logger_cpu, "Enviando HANDSHAKE a servidores");

    op_code handshake = MSG_HANDSHAKE_CPU;

    enviar_mensaje(fd_ks, &handshake, sizeof(op_code));
    int size_resp_ks;
    op_code* respuesta = recibir_mensaje(fd_ks, &size_resp_ks);
    if (respuesta == NULL){
        log_error(logger_cpu, "Error al recibir mensaje desde KS");
        exit(EXIT_FAILURE);
    } else if (*respuesta == MSG_OK) {
        log_info(logger_cpu, "Handshake con KS exitoso");
    } else if (*respuesta == MSG_ERROR){
        log_error(logger_cpu, "Handshake con KS FALLIDO");
    }
    free(respuesta);
    log_info(logger_cpu, "TERMINANDO PROGRAMA");
    close(fd_ks);
}

void conexionCPUKernelMemory (t_log* logger_cpu, int id, const char* archivo_config) {
    log_info(logger_cpu, "EJECUTANDO CPU %d, %s",id, archivo_config);
    t_config* config = config_create(archivo_config);
    char *km_port = config_get_string_value(config, "KM_PORT");
    char *km_ip = config_get_string_value(config, "KM_IP");
    int fd_km = crear_conexion(km_ip, km_port);
    log_info(logger_cpu, "Enviando HANDSHAKE a servidores");

    op_code handshake = MSG_HANDSHAKE_CPU;

    enviar_mensaje(fd_km, &handshake, sizeof(op_code));
    int size_resp_km;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp_km);
    if (respuesta == NULL){
        log_error(logger_cpu, "Error al recibir mensaje desde KM");
        exit(EXIT_FAILURE);
    } else if (*respuesta == MSG_OK) {
        log_info(logger_cpu, "Handshake con KM exitoso");
    } else if (*respuesta == MSG_ERROR){
        log_error(logger_cpu, "Handshake con KM FALLIDO");
    }
    free(respuesta);
    log_info(logger_cpu, "TERMINANDO PROGRAMA");
    close(fd_km);
}

void conexionCPUMemoryStick (t_log* logger_cpu, int id, const char* archivo_config) {
    log_info(logger_cpu, "EJECUTANDO CPU %d, %s",id, archivo_config);
    t_config* config = config_create(archivo_config);
    char *ms_port = config_get_string_value(config, "MS_PORT");
    char *ms_ip = config_get_string_value(config, "MS_IP");
    int fd_ms = crear_conexion(ms_ip, ms_port);
    log_info(logger_cpu, "Enviando HANDSHAKE a servidores");

    op_code handshake = MSG_HANDSHAKE_CPU;

    enviar_mensaje(fd_ms, &handshake, sizeof(op_code));
    int size_resp_ms;
    op_code* respuesta = recibir_mensaje(fd_ms, &size_resp_ms);
    if (respuesta == NULL){
        log_error(logger_cpu, "Error al recibir mensaje desde MS");
        exit(EXIT_FAILURE);
    } else if (*respuesta == MSG_OK) {
        log_info(logger_cpu, "Handshake con MS exitoso");
    } else if (*respuesta == MSG_ERROR){
        log_error(logger_cpu, "Handshake con MS FALLIDO");
    }
    free(respuesta);
    log_info(logger_cpu, "TERMINANDO PROGRAMA");
    close(fd_ms);
}
 
int main(int argc, char* argv[]) {

    t_log* logger_cpu;
    logger_cpu = log_create("cpu.log","CPU LOGGER",1,LOG_LEVEL_INFO);
    if (logger_cpu == NULL){
	perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
	exit(EXIT_FAILURE);
    }

    //Check argumentos
    if (argc != 3) {
        log_info(logger_cpu, "Debe ingreslear %s [Archivo Config] [Identificador]. Verifique los argumentos.", argv[0]); 
        exit(EXIT_FAILURE);
    }
    //Check archivo .config
    char* archivo_config = argv[1];
    if (string_ends_with(archivo_config, ".config") == 0){
        log_info(logger_cpu, "El primer parametro debe ser .config");
        exit(EXIT_FAILURE);
    }
    //Check identificadores
    int id = atoi(argv[2]);
    if(id < 1 || id > 3) {
        log_info(logger_cpu, "Error: el identificador debe ser 1, 2 o 3\n");
        exit(EXIT_FAILURE);
    }

    // Dependiendo del identificador se lanza cada cpu y se le pasa el archivo .config como parametro
    log_info(logger_cpu, "Iniciando CPU");
    switch (id) {
        case 1:
            log_info(logger_cpu, "Ejecutando CPU 1");
            conexionCPUKernelScheduler(logger_cpu, id, archivo_config);
            break;
        case 2:
            log_info(logger_cpu, "Ejecutando CPU 2");
            conexionCPUKernelMemory(logger_cpu, id, archivo_config);
            break;
        case 3:
            log_info(logger_cpu, "Ejecutando CPU 3");
            conexionCPUMemoryStick(logger_cpu, id, archivo_config);
            break;
        default:
            printf("Identificador invalido. Debe ser 1, 2 o 3.\n");
            return 1;
    }
    log_destroy(logger_cpu);
return 0;
}//FIN MAIN CPU




















    // TODO Adriel: cargar config con config_create()
   
    // TODO Adriel: crear logger con log_create()
    // OJO: el nombre del archivo de log tiene que incluir el identificador
    // Ejemplo: "cpu_1.log" si el identificador es 1
 
    // TODO Adriel: conectarse a Kernel Scheduler
    // int fd_ks = crear_conexion(KS_IP, KS_PORT);
    // enviar handshake MSG_HANDSHAKE_CPU
 
    // TODO Adriel: conectarse a Kernel Memory
    // int fd_km = crear_conexion(KM_IP, KM_PORT);
    // enviar handshake MSG_HANDSHAKE_CPU
 
    // TODO Adriel: conectarse a cada Memory Stick
    // int fd_ms = crear_conexion(MS_IP, MS_PORT);
    // enviar handshake MSG_HANDSHAKE_CPU
 
    // CP2: ciclo de instrucción Fetch→Decode→Execute→Check Interrupt
 

