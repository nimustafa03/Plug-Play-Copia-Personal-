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

// CP2: Bianca
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
 
t_config* config;
t_log* logger;

 
int main(int argc, char* argv[]) {
 
    // Verificar argumentos (config + tipo)
    // El tipo viene por parámetro: argv[2] → "STDIN", "STDOUT" o "SLEEP"

    // (Si argc es menor a 2, es que no escribieron el nombre del archivo)
    if (argc <3) {
        printf("Error: Faltan parámetros.\nUso: ./bin/io [archivo_config] [TIPO]\n TIPO: STDIN/STDOUT/SLEEP");
        return EXIT_FAILURE;
    }
    
    char* tipo_io = argv[2]; // guardamos el tipo para futuras validaciones
 
    // Cargar config con config_create()
    config = config_create(argv[1]);
    // Verificar que exista
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
 
    // Crear logger 
    // 1. Primero leemos la palabra del archivo config
    char* nivel_de_log_texto = config_get_string_value(config, "LOG_LEVEL");
    // 2. La traducimos al "idioma" que entiende el Logger
    t_log_level nivel_traducido = log_level_from_string(nivel_de_log_texto);
    // 3. Usamos ese nivel traducido al crear el Logger
    logger = log_create("io.log", "IO", true, nivel_traducido);

    // Conexión a Kernel Scheduler
    // 1. Obtener el value de las variables del config
    char* ip = config_get_string_value(config, "KS_IP");
    char* puerto = config_get_string_value(config, "KS_PORT");

    // 2. Conectarse 
    int fd_ks = crear_conexion(ip, puerto); // int fd_ks = crear_conexion(KS_IP, KS_PORT);

    // 3. Manejo de errores en la conexión
    if (fd_ks == -1) {
        log_error(logger, "No se pudo conectar a Kernel Scheduler en %s:%s",
                  ip, puerto);
        config_destroy(config);
        log_destroy(logger);
        return EXIT_FAILURE;
    }

    // Realizar handshake (con MSG_HANDSHAKE_IO)
    // 1. Enviar saludo
    op_code saludo = MSG_HANDSHAKE_IO;
    enviar_mensaje(fd_ks, &saludo, sizeof(op_code));

    // 2. Esperar OK 
    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_ks, &size_resp);
    
    if (*respuesta == MSG_OK) {
        // Log obligatorio: log_info(logger, "## Conectado a Kernel Scheduler");
        log_info(logger, "## Conectado a Kernel Scheduler");
    }
    free(respuesta); // Liberar la memoria del mensaje recibido
 
    // Quedarse esperando peticiones del KS
    while(1) {
        int size_orden;
        op_code* orden = recibir_mensaje(fd_ks, &size_orden);

        if(orden != NULL) {

            // extraer el PID
            int size_pid;
            int* pid_ptr = recibir_mensaje(fd_ks, &size_pid);

            if (pid_ptr == NULL) {
                free(orden);
                log_error(logger, "Error de protocolo: Se desconectó el Kernel al enviar el PID.");
                break;
            }

            int pid = *pid_ptr;
            free(pid_ptr);

            log_info(logger, "## PID: %d - Inicio de IO", pid);

            switch (*orden)
            {
            case MSG_STDIN: {
                // extraer cadena de caracteres
                int size_cant;
                int* cant_ptr = recibir_mensaje(fd_ks, &size_cant);
                int cant_carac = *cant_ptr;
                free(cant_ptr);

                // Validación de tipo de interfaz por seguridad
                    if (strcmp(tipo_io, "STDIN") != 0) {
                        log_error(logger, "Error: Esta interfaz no es de tipo STDIN.");
                        break;
                    }

                log_info(logger, "## PID: %d - Ingrese %d caracteres:" , pid, cant_carac);
                
                // logica: leer, recortar o rellenar 
                char* buffer_leido = malloc(cant_carac + 2); // buffer dinamico basado en la cantidad exacta de caracteres + 2 para considerar el salto de linea y el fin de cadena
                if (fgets(buffer_leido, cant_carac + 2, stdin) != NULL){
                    buffer_leido[strcspn(buffer_leido, "\n")] = '\0';
                }; 


                char* texto_final = calloc(1, cant_carac); // garantizar que el ultimo byte sea \0
                int longitud_leida = strlen(buffer_leido);

                for (int i = 0; i < cant_carac; i++) {
                    if (i< longitud_leida) texto_final[i] = buffer_leido[i];
                }

                // a stdin no se le manda msg_done, se devuelve la cadena leida
                enviar_mensaje(fd_ks, texto_final, cant_carac);
                free(buffer_leido);
                free(texto_final);
                break;
            }
            
            case MSG_STDOUT: {
                // extraer texto a imprimir
                int size_texto;
                char* cont_imprimir = recibir_mensaje(fd_ks, &size_texto);

                if (strcmp(tipo_io, "STDOUT") != 0) {
                        log_error(logger, "Error: Esta interfaz no es de tipo STDOUT.");
                        if (cont_imprimir != NULL) free(cont_imprimir);

                        // destrabar el ks
                        op_code error = MSG_ERROR;
                        enviar_mensaje(fd_ks, &error, sizeof(op_code));
                        break;
                    }

                if(cont_imprimir != NULL){
                    log_info(logger, "## PID: %d - %s", pid, cont_imprimir);
                    printf("%s\n", cont_imprimir);
                    free(cont_imprimir);
                }

                // responde msg_done
                op_code done = MSG_DONE; 
                enviar_mensaje(fd_ks, &done, sizeof(op_code));
                break;
            }

            case MSG_SLEEP: {
                // extraer el tiempo
                int size_tiempo;
                int* tiempo_ptr = recibir_mensaje(fd_ks, &size_tiempo);

                if (tiempo_ptr == NULL) break;

                int tiempo = *tiempo_ptr;
                free(tiempo_ptr);

                if (strcmp(tipo_io, "SLEEP") != 0) {
                        log_error(logger, "Error: Esta interfaz no es de tipo SLEEP.");
                        break;
                    }

                log_info(logger, "## PID: %d - Haciendo sleep por %d milisegundos.", pid, tiempo);
                usleep(tiempo * 1000);

                // responder msg_done
                op_code done = MSG_DONE; 
                enviar_mensaje(fd_ks, &done, sizeof(op_code));
                break;
            }
            }

            log_info(logger, "## PID: %d - Fin de IO", pid);
            free(orden);

        } else{
            log_error(logger, "Se perdió la conexión al Kernel Scheduler en %s:%s",
            ip, puerto);
            return EXIT_FAILURE;
            }
    }
    

    // Eliminar el config y el log para liberar la memoria
    config_destroy(config);
    log_destroy(logger);
    return EXIT_SUCCESS;
}
    
