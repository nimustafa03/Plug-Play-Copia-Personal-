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

int conexionCPUKernelMemory (t_config* config) {
    char *km_port = config_get_string_value(config, "KM_PORT");
    char *km_ip = config_get_string_value(config, "KM_IP");
    int fd_km = crear_conexion(km_ip, km_port);
    return fd_km;
    }

int conexionCPUKernelScheduler (t_config* config) {
    char *ks_port = config_get_string_value(config, "KS_PORT");
    char *ks_ip = config_get_string_value(config, "KS_IP");
    int fd_ks = crear_conexion(ks_ip, ks_port);
    return fd_ks;
    }

int conexionCPUMemoryStick (t_config* config) {
    char *ms_port = config_get_string_value(config, "MS_PORT");
    char *ms_ip = config_get_string_value(config, "MS_IP");
    int fd_ms = crear_conexion(ms_ip, ms_port);
    return fd_ms;
    }

int esperar_pid(int fd_ks) {
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

int main(int argc, char* argv[]) {

    t_log* logger_cpu;
    logger_cpu = log_create("cpu.log","CPU LOGGER",1,LOG_LEVEL_INFO);
    if (logger_cpu == NULL){
	perror("Error al crear el archivo .log. La funcion log_create este devolviendo NULL");
	exit(EXIT_FAILURE);
    }

    //Check argumentos
    if (argc != 3) {
        log_info(logger_cpu, "Debe ingresar %s [Archivo Config] [Identificador]. Verifique los argumentos.", argv[0]); 
        exit(EXIT_FAILURE);
    }
    //Check archivo .config
    char* direccion_archivo = argv[1];
    if (string_ends_with(direccion_archivo, ".config") == 0){
        log_info(logger_cpu, "El primer parametro debe ser .config");
        exit(EXIT_FAILURE);
    }
    //Check identificadores
    int id = atoi(argv[2]);
    if(id < 1 || id > 3) {
        log_info(logger_cpu, "Error: el identificador debe ser 1, 2 o 3\n");
        exit(EXIT_FAILURE);
    }

    char* archivo_config = string_from_format("cpu_%d.config", id);
    t_config* config = config_create(archivo_config);
    free(archivo_config);

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

    //WHILE DE ESPERA PID
    while (1) {
        int pid = esperar_pid(fd_ks);
        
        log_info(logger_cpu, "FASE FETCH");

        RegistrosCPU cpu = {0}; //inicializo registros
        cpu.PC = 0;

        // WHILE CICLO DE INSTRUCCION
        while (1) {
            char* instruccion = fetch(fd_km, pid, &cpu);

            log_info(logger_cpu, "FASE DECODE");
            op_code_cpu codop = decode(instruccion);

            log_info(logger_cpu, "FASE EXECUTE");
            execute(codop,instruccion,&cpu);
            free(instruccion);

            log_info(logger_cpu, "FASE INTERRUPCIONES");
            
            op_code op;

            if (check_interrupcion(fd_ks, &op)) {
                if (op == MSG_INTERRUPT) {
                    t_interrupcion intr;
                    int bytes = recv(fd_ks, &intr, sizeof(t_interrupcion), MSG_WAITALL);
                    if (bytes <= 0)
                        return 0;
                    if (intr.pid == pid) {
                        enviar_mensaje(fd_km, &cpu, sizeof(RegistrosCPU));
                        enviar_mensaje(fd_ks, &MSG_INTERRUPCION_ATENDIDA, sizeof(op_code));
                        break;
                    }
                }
            }
        }
    }
}













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