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
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <cpu.h>

t_config* config;
 
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
    if (string_ends_with(archivo_config, ".config") != 0){
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
            funcion_cpu(archivo_config, id);
            break;
        case 2:
            log_info(logger_cpu, "Ejecutando CPU 2");
            funcion_cpu(archivo_config, id);
            break;
        case 3:
            log_info(logger_cpu, "Ejecutando CPU 3");
            funcion_cpu(archivo_config, id);
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
 

