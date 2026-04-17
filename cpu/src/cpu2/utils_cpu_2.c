#include <stdio.h>
#include <stdlib.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/string.h>
#include "/home/utnso/Desktop/tp-2026-1c-Grupo3113/utils/src/utils/conexiones.h"
#include "/home/utnso/Desktop/tp-2026-1c-Grupo3113/utils/src/utils/mensajes.h"
#include "cpu_2.h"

t_config* iniciar_config(void){
	t_config* nuevo_config = config_create("/home/utnso/Desktop/tp-2026-1c-Grupo3113/cpu/cpu_2.config");
	if (nuevo_config == NULL){
		perror("Error al crear el archivo .config. La funcion config_create esta devolviendo NULL");
		exit(EXIT_FAILURE);
	}
	return nuevo_config;
}

int es_config(char* archivo) {
    return string_ends_with(archivo, ".config");
    }

t_log* iniciar_logger(void)
{
	t_log* nuevo_logger = log_create("cpu_2.log","CPU 2 LOGGER",1,LOG_LEVEL_INFO);
	if (nuevo_logger == NULL){
		perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}