// =============================================================
//  cpu_3.c  —  Módulo CPU
//  Cómo ejecutar: ./bin/cpu cpu_3.config [identificador]
//
//  Responsable CP1:
//    Adriel → cliente CPU→KS
//             cliente CPU→KM
//             cliente CPU→MS
//
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include "/home/utnso/Desktop/tp-2026-1c-Grupo3113/utils/src/utils/conexiones.h"
#include "/home/utnso/Desktop/tp-2026-1c-Grupo3113/utils/src/utils/mensajes.h"
#include "cpu_3.h"
#include "utils_cpu_3.c"

int main(int argc, char* argv[]) {
   // TODO Adriel: verificar argumentos (config + identificador)
    //Check argumentos
    if (argc != 3) {
        printf("Debe ingreslear %s [Archivo Config] [Identificador]. Verifique los argumentos.", argv[0]); 
        exit(EXIT_FAILURE);
    }
    //Check archivo .config
    char* arc_config = argv[1];
    if (es_config(argv[1])!=1){
        printf("El primer parametro debe ser .config");
        exit(EXIT_FAILURE);
    }
    //Check identificadores
    int id = atoi(argv[2]);
    if(id < 1 || id > 3) {
        printf("Error: el identificador debe ser 1, 2 o 3\n");
        exit(EXIT_FAILURE);
    }

/*------------------------- INICIO CPU 3 -------------------------*/

    // TODO Adriel: crear logger con log_create()
	t_log* logger_cpu_3 = iniciar_logger();
	log_info(logger_cpu_3, "Ejecutando_CPU_3");

    // TODO Adriel: cargar config con config_create()
    log_info(logger_cpu_3, "Creando .config");
    t_config* config_cpu_3 = iniciar_config();

    //Muestra los valores del archivo .config
	char* ip_ks = config_get_string_value(config_cpu_3,"KS_IP");
	char* ip_km = config_get_string_value(config_cpu_3,"KM_IP");
    char* ip_ms = config_get_string_value(config_cpu_3,"MS_IP");
	char* puerto_ks = config_get_string_value(config_cpu_3,"KS_PORT");
	char* puerto_km = config_get_string_value(config_cpu_3,"KM_PORT");
	char* puerto_ms = config_get_string_value(config_cpu_3,"MS_PORT");
	log_info(logger_cpu_3,"IP KS: %s - IP KM: %s - IP MS: %s",ip_ks, ip_km, ip_ms);
	log_info(logger_cpu_3,"PUERTO KS: %s - PUERTO KM: %s - PUERTO MS: %s",puerto_ks,puerto_km,puerto_ms);
 
/*----------------- CONEXION CON KERNEL SCHEDULER -----------------*/

    // int fd_ks = crear_conexion(KS_IP, KS_PORT);
    log_info(logger_cpu_3, "Intentando conexion con Kernel Scheduler...");
	int fd_ks = crear_conexion(ip_ks, puerto_ks);
	log_info(logger_cpu_3,"CPU 3 Conectada con KS");

    // enviar handshake MSG_HANDSHAKE_CPU
    // enviar_mensaje(fd_ks, void* buffer, int size);
 

/*------------------ CONECCION CON KERNEL MEMORY ------------------*/

    // int fd_km = crear_conexion(KM_IP, KM_PORT);
    log_info(logger_cpu_3, "Intentando conexion con Kernel Memory...");
	fd_ks = crear_conexion(ip_km, puerto_km);
	log_info(logger_cpu_3,"CPU 3 Conectada con KM");
    // enviar handshake MSG_HANDSHAKE_CPU


/*------------------- CONECCION CON MEMORY STICK ------------------*/

    log_info(logger_cpu_3, "Intentando conexion con Memory Stick...");
	fd_ks = crear_conexion(ip_ms, puerto_ms);
	log_info(logger_cpu_3,"CPU 3 Conectada con MS");
    // int fd_ms = crear_conexion(MS_IP, MS_PORT);
    // enviar handshake MSG_HANDSHAKE_CPU
    // CP2: ciclo de instrucción Fetch→Decode→Execute→Check Interrupt
 
return 0;
}// FIN MAIN