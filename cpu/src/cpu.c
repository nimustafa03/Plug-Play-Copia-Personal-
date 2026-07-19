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

int conexionCPUKernelMemory (t_config* config, int identificador_cpu) {
    char *km_port = config_get_string_value(config, "KM_PORT");
    char *km_ip = config_get_string_value(config, "KM_IP");
    int fd_km = crear_conexion(km_ip, km_port);
    op_code hs = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_km, &hs, sizeof(op_code));
    int id_cpu = identificador_cpu;
    enviar_mensaje(fd_km, &id_cpu, sizeof(int));
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

int conexionCPUMemoryStick (t_config* config, int identificador_cpu) {
    char *ms_port = config_get_string_value(config, "MS_PORT");     
    char *ms_ip = config_get_string_value(config, "MS_IP");
    int fd_ms = crear_conexion(ms_ip, ms_port);
    op_code hs = MSG_HANDSHAKE_CPU;
    enviar_mensaje(fd_ms, &hs, sizeof(op_code));
    int id_cpu = identificador_cpu;
    enviar_mensaje(fd_ms, &id_cpu, sizeof(int));
    int size_ok;
    op_code* ok = recibir_mensaje(fd_ms, &size_ok);
                //conexiones_abiertas_ms ++;
    free(ok);
    return fd_ms;
    }

int esperar_pid(int fd_ks, t_log* logger_cpu) {
    log_info(logger_cpu, "CPU esperando PID del Kernel Scheduler...");
    while (1) {
        int size;
        uint32_t* pid_ptr = recibir_mensaje(fd_ks, &size);

        if (pid_ptr == NULL) {
            return -1;
        }
        uint32_t pid = *pid_ptr;
        free(pid_ptr);

        log_info(logger_cpu, "PID recibido: %u", pid);
        return pid;
    }
}
int ms_conectados = 1;
int fd_ms_agregados[3] = {-1, -1, -1};

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
    } else log_info(logger_cpu,"LOG CREADO");
    //Check archivo .config
    char* direccion_archivo = argv[1];
    if (string_ends_with(direccion_archivo, ".config") == 0){
        log_info(logger_cpu, "El primer parametro debe ser .config");
        exit(EXIT_FAILURE);
    }
    // Crear .config
    t_config* config = config_create(argv[1]);
    if (config == NULL) {
        log_info(logger_cpu, "Error al leer .config");
        exit(EXIT_FAILURE);
    } else log_info(logger_cpu,"CONFIG CREADO");

    // INICIAR CONEXIONES CON SERVIDORES
    int fd_km = conexionCPUKernelMemory(config,id);
    if (fd_km == -1) {
        log_info(logger_cpu, "Error al conectar con Kernel Memory");
        exit(EXIT_FAILURE);
        } else log_info(logger_cpu, "Conexion exitosa con Kernel Memory");
        
    int fd_ks = conexionCPUKernelScheduler(config);
    if (fd_ks == -1) {
        log_info(logger_cpu, "Error al conectar con Kernel Scheduler");
        exit(EXIT_FAILURE);
        } else log_info(logger_cpu, "Conexion exitosa con Kernel Scheduler");
    
    int fd_ms = conexionCPUMemoryStick(config, id);
    if (fd_ms == -1) {
        log_info(logger_cpu, "Error al conectar con Memory Stick");
        exit(EXIT_FAILURE);
        } else log_info(logger_cpu, "Conexion exitosa con Memory Stick");

    //WHILE PID
    bool op_exit;  
    while (1) {
        // CPU espera la llegada de un pid por parte del KS
        int pid = esperar_pid(fd_ks, logger_cpu);
        if (pid == -1) {
            log_info(logger_cpu, "Error al recibir pid");
            exit(EXIT_FAILURE);
        } else log_info(logger_cpu, "Recibido PID: %d", pid);
        
        // Aviso de inicio de proceso al KM
        op_code codigo_init = MSG_INIT_CPU;
        enviar_mensaje(fd_km,&codigo_init,sizeof(op_code));

        // Recibe status de los diferentes ms
        t_mapa_memory_sticks_cpu* mapa = recibir_mapa(fd_km, logger_cpu);
        if (mapa == NULL) {
            log_info(logger_cpu, "No se pudo recibir el mapa de Memory Sticks");
            exit(EXIT_FAILURE);
        }
        if (conectar_memory_sticks_faltantes(mapa, logger_cpu) == -1) {
            log_info(logger_cpu, "Error al conectar los Memory Sticks faltantes");
            exit(EXIT_FAILURE);
            break;
        }

        // Se envia pid a KM
        enviar_mensaje(fd_km, &pid, sizeof(pid));
        int size;
        t_contexto* contexto = recibir_mensaje(fd_km, &size);         // KM envia contexto
        if (contexto == NULL) {
            log_info(logger_cpu, "Error al recibir contexto");
            break;
        }

        log_info(logger_cpu, "Inicia ejecucion de proceso: %d", pid);

        // WHILE CICLO DE INSTRUCCION
        op_exit = false;
        while (op_exit != true) {
            // FETCH
            log_info(logger_cpu, "## PID: %u - FETCH - Program Counter: %d", pid, contexto->registros.pc);
            char* instruccion = fetch(fd_km, pid, &contexto->registros);
            if (instruccion == NULL) {
                log_info(logger_cpu, "Error en FETCH");
                break;}

            // DECODE
            operacion codigo = decode(instruccion);

            // EXECUTE
            char inst[32], parametro1[32], parametro2[32];
            sscanf(instruccion, "%31s %31s %31s", inst, parametro1, parametro2);
            log_info(logger_cpu, "## PID: %u - Ejecutando: %s - %s %s",pid, inst, parametro1, parametro2);
            //log_info(logger_cpu, "OPCODE: %d", codigo);
            int operacion = execute(codigo, instruccion, &contexto->registros,fd_ks,fd_km,fd_ms,pid, contexto->tabla_segmentos,logger_cpu,mapa,fd_ms_agregados);

            if (operacion == -1) {                                  //en caso de SEG FAULT en las operaciones MOV y COPY
                op_code guardar_contexto = MSG_SEG_FAULT;
                enviar_mensaje(fd_km, &guardar_contexto, sizeof(op_code));
                enviar_mensaje(fd_km, contexto, sizeof(t_contexto));
                free(instruccion);
                break;
            } else if (operacion == 1)
                op_exit = true;
            else if (operacion == -2){
                log_info(logger_cpu, "Operacion invalida en proceso %d", pid);
                exit(EXIT_FAILURE);
            }

            // INTERRUPCIONES
            int atender = atender_interrupcion(fd_ks, fd_km, contexto, pid, logger_cpu);
            if (atender == 0){
                log_info(logger_cpu, "## Interrupcion recibida");
                free(instruccion);
                break;
            }
            else if (atender == -1){
                log_info(logger_cpu, "Error (-1) en la atencion de interrupcion del proceso %d", pid);
                exit(EXIT_FAILURE);
            }
            else if (atender == -2){
                log_info(logger_cpu, "Error (-2) en la atencion de interrupcion del proceso %d", pid);
                exit(EXIT_FAILURE);
            }
            free(instruccion);
        }
        if (op_exit == true)    
            log_info(logger_cpu, "Concluyo proceso %d con EXIT", pid);
        free(contexto);
    destruir_mapa_memory_sticks(mapa);
    }
}
