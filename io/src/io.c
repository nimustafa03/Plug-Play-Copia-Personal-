// =============================================================
//  io.c  —  Módulo IO
//  Cómo ejecutar: ./bin/io io.config [tipo]
//  Tipos posibles: STDIN / STDOUT / SLEEP
//
//  Responsable CP1:
//    Bianca → cliente IO→KS
//
//  Te toca conectarte al Kernel Scheduler e informar tu tipo.
//  Dale que se puede
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/config.h>
#include "conexiones.h"
#include "mensajes.h"
 
t_config* config = config_create(argv[1]);

char* ip = config_get_string_value(config, "IP_KERNEL");
char* puerto = config_get_string_value(config, "PUERTO_KERNEL");

// 1. Primero leemos la palabra del archivo config
char* nivel_de_log_texto = config_get_string_value(config, "LOG_LEVEL");
// 2. La traducimos al "idioma" que entiende el Logger
t_log_level nivel_traducido = log_level_from_string(nivel_de_log_texto);
// 3. Usamos ese nivel traducido al crear el Logger
logger = log_create("io.log", "IO", true, nivel_traducido);

int fd_ks = crear_conexion(ip, puerto);
 
int main(int argc, char* argv[]) {
 
    // TODO Bianca: verificar argumentos (config + tipo)
    // El tipo viene por parámetro: argv[2] → "STDIN", "STDOUT" o "SLEEP"
 
    // TODO Bianca: cargar config con config_create()
 
    // TODO Bianca: crear logger con log_create()
 
    // TODO Bianca: conectarse a Kernel Scheduler
    // int fd_ks = crear_conexion(KS_IP, KS_PORT);
    // enviar handshake MSG_HANDSHAKE_IO
    // Log obligatorio: log_info(logger, "## Conectado a Kernel Scheduler");
 
    // TODO Bianca: quedarse esperando peticiones del KS
    // pause(); por ahora alcanza para CP1
 
    // TODO CP2: implementar STDIN, STDOUT y SLEEP
}
    
