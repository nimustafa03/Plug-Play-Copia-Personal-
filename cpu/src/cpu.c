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
 
t_log*    logger;
t_config* config;
 
int main(int argc, char* argv[]) {
 
    // TODO Adriel: verificar argumentos (config + identificador)
 
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
 
    return 0;
}
