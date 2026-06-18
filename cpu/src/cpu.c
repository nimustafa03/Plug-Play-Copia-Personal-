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
#include <utils/tipos.h>

int conexionCPUKernelMemory (t_config* config) {
    char *km_port = config_get_string_value(config, "KM_PORT");
    char *km_ip = config_get_string_value(config, "KM_IP");
    int fd_km = crear_conexion(km_ip, km_port);
    op_code hs = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_km, &hs, sizeof(op_code));
    int size_ok;
    op_code* ok = recibir_mensaje(fd_km, &size_ok);
    free(ok);
    return fd_km;
    }

int conexionCPUKernelScheduler (t_config* config) {
    char *ks_port = config_get_string_value(config, "KS_PORT");
    char *ks_ip = config_get_string_value(config, "KS_IP");
    int fd_ks = crear_conexion(ks_ip, ks_port);
    op_code hs = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_ks, &hs, sizeof(op_code));
    int size_ok; 
    op_code* ok = recibir_mensaje(fd_ks, &size_ok);
    free(ok);
    return fd_ks;
    }

int conexionCPUMemoryStick (t_config* config) {
    char *ms_port = config_get_string_value(config, "MS_PORT");
    char *ms_ip = config_get_string_value(config, "MS_IP");
    int fd_ms = crear_conexion(ms_ip, ms_port);
    op_code hs = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_ms, &hs, sizeof(op_code));
    int size_ok;
    op_code* ok = recibir_mensaje(fd_ms, &size_ok);
    free(ok);
    return fd_ms;
    }

int esperar_pid(int fd_ks, t_log* logger_cpu) {
    log_info(logger_cpu, "CPU esperando PID del Kernel Scheduler...");
    while (1) {
        int size;
        uint32_t* pid_ptr = recibir_mensaje(fd_ks, &size);

        if (pid_ptr == NULL) {
            log_error(logger_cpu, "Error al recibir PID");
            continue;
        }
        uint32_t pid = *pid_ptr;
        free(pid_ptr);

        log_info(logger_cpu, "PID recibido: %u", pid);
        return pid;
    }
}

/*----------------------------- MAIN -----------------------------*/

int main(int argc, char* argv[]) {

    // Check argumentos
    if (argc != 3) {
        printf("Debe ingresar %s [Archivo Config] [Identificador]\n Programa finalizado",argv[0]);
        exit(EXIT_FAILURE);
    }
    // Identificador
    int id = atoi(argv[2]);

    // Logger usando identificador
    char* nombre_log = string_from_format("cpu_%d.log", id);
    t_log* logger_cpu = log_create(nombre_log,"CPU LOGGER",1,LOG_LEVEL_INFO);
    free(nombre_log);
    if (logger_cpu == NULL){
	    perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
	    exit(EXIT_FAILURE);
    }
    //Check archivo .config
    char* direccion_archivo = argv[1];
    if (string_ends_with(direccion_archivo, ".config") == 0){
        log_error(logger_cpu, "El primer parametro debe ser .config");
        exit(EXIT_FAILURE);
    }
    // Crear .config
    t_config* config = config_create(argv[1]);
    if (config == NULL) {
        log_error(logger_cpu, "Error al leer .config");
        exit(EXIT_FAILURE);}

    // INICIAR CONEXIONES CON SERVIDORES
    int fd_km = conexionCPUKernelMemory(config);
    if (fd_km == -1) {
    log_error(logger_cpu, "Error al conectar con Kernel Memory");
    exit(EXIT_FAILURE);
    }
    int fd_ks = conexionCPUKernelScheduler(config);
    if (fd_ks == -1) {
    log_error(logger_cpu, "Error al conectar con Kernel Scheduler");
    exit(EXIT_FAILURE);
    }
    int fd_ms = conexionCPUMemoryStick(config);
    if (fd_ms == -1) {
    log_error(logger_cpu, "Error al conectar con Memory Stick");
    exit(EXIT_FAILURE);
    }

    //WHILE PID
    while (1) {
        
        // CPU espera la llegada de un pid por parte del KS
        int pid = esperar_pid(fd_ks, logger_cpu); 
        // Aviso de inicio de proceso al KM
        op_code codigo_init = MSG_INIT_CPU;
        enviar_mensaje(fd_km,&codigo_init,sizeof(op_code));
        // Se envia pid a KM
        enviar_mensaje(fd_km, &pid, sizeof(pid));
        // KM envia contexto
        int size;
        t_contexto_ejecucion* contexto = recibir_mensaje(fd_km, &size);
        if (contexto == NULL) {
            log_error(logger_cpu, "Error al recibir contexto");
            break;
        }

        // WHILE CICLO DE INSTRUCCION
        while (1) {
            // FETCH
            char* instruccion = fetch(fd_km, pid, &contexto->registros);
            if (instruccion == NULL) {
                log_error(logger_cpu, "Error en FETCH");
                break;}

            // DECODE
            op_code_cpu codop = decode(instruccion);

            // EXECUTE
            execute(codop, instruccion, &contexto->registros, fd_ks, contexto->pid);
            free(instruccion);

            // INTERRUPCIONES
            
            op_code op;
            int interrumpido = atender_interrupcion(fd_ks, fd_km, contexto);

            if (interrumpido)
                break;
            }
        free(contexto);
        }
}
