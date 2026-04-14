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
#include "conexiones.h"
#include "mensajes.h"
 
t_log*    logger;
t_config* config;
 
int main(int argc, char* argv[]) {
 
    // TODO Nico M: verificar argumentos (config + tamaño)
    // El tamaño viene por parámetro: argv[2]
 
    // TODO Nico M: cargar config con config_create()
 
    // TODO Nico M: crear logger con log_create()
 
    // TODO Nico M: reservar memoria con malloc(tamaño)
 
    // TODO Nico M: conectarse a Kernel Memory
    // int fd_km = crear_conexion(KM_IP, KM_PORT);
    // enviar handshake MSG_HANDSHAKE_MS
    // enviar el tamaño para que KM lo registre
    // Log obligatorio: log_info(logger, "## Conectado a Kernel Memory");
 
    // TODO Nico M: levantar servidor para CPUs
    // int fd_servidor = iniciar_servidor(PORT);
    // loop con esperar_cliente() + pthread_create() + pthread_detach()
    // cuando llega MSG_HANDSHAKE_CPU → log_info(logger, "## CPU <ID> Conectada");
 
    // TODO CP3: lectura y escritura de memoria física
 
    return 0;
}