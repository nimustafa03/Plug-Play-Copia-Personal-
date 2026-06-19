// =============================================================
//  kernel_scheduler.c  —  Módulo Kernel Scheduler
//  Cómo ejecutar: ./bin/kernel_scheduler kernel_scheduler.config 
//
//  Responsables CP1:
//    Bianca  → cliente KS→KM  +  servidor KS acepta CPUs
//    Santiago → servidor KS acepta IOs   ← ESTA PARTE
//  
//  Responsables CP2: 
//    Nico S → Agregué el código dentro del switch de
//             atender_cliente_ks(), en el case MSG_HANDSHAKE_IO
//             Te toca el resto, exitos!
//    Santiago → planificador corto plazo (FIFO/RR), atención IO,
//               crear_proceso_inicial, loop_aceptar_clientes
// =============================================================
 
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>          // para uint32_t al enviar PID
#include <pthread.h>
#include <semaphore.h>       // para sem_post en atender_cliente_ks
#include <commons/log.h>
#include <commons/config.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include "funciones_ks.h"   // trae también gestor_ks.h 
 
t_log*    logger_ks    = NULL;
t_config* config       = NULL;
int       fd_servidor_ks;
int       fd_km;           // FD de la conexión KS→KM (global para que
                           // los planificadores puedan usarlo en CP3)
 
void* atender_cliente_ks(void* arg) {
    int fd_cliente = *((int*) arg);
    free(arg);
 
    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    op_code respuesta_ok = MSG_OK;
 
    switch (*codigo) {
 
        case MSG_HANDSHAKE_CPU:
            log_info(logger_ks, "CPU conectado - FD: %d", fd_cliente);
            enviar_mensaje(fd_cliente, &respuesta_ok, sizeof(op_code));
            // CP2: listaCPUsLibres y sem_post se hacen dentro de atender_cpu_ks
            atender_cpu_ks(fd_cliente);
            break;
 
        case MSG_HANDSHAKE_IO:
            log_info(logger_ks, "IO conectada - FD: %d", fd_cliente);
            enviar_mensaje(fd_cliente, &respuesta_ok, sizeof(op_code));

            pthread_mutex_lock(&mutex_listas);
            int* fd_io_ptr = malloc(sizeof(int));
            *fd_io_ptr = fd_cliente;
            list_add(listaIOsLibres, fd_io_ptr);
            pthread_mutex_unlock(&mutex_listas);

            sem_post(&sem_hay_io_libre);
            break;
 
        default:
            log_warning(logger_ks, "Conexion desconocida - codigo: %d - FD: %d",
                        *codigo, fd_cliente);
            break;
    }
 
    free(codigo);
    return NULL;
}

// el while(1) que antes bloqueaba main() ahora es esto
void* loop_aceptar_clientes(void* arg) {
    while (1) {
        int* fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor_ks);
        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente_ks, fd_cliente);
        pthread_detach(hilo);
    }
    return NULL;
}
 
int main(int argc, char* argv[]) {
 
    // cambiamos < 2 por < 3 porque ahora argv[2] (path proceso inicial) es obligatorio
    if (argc < 3) {
        printf("Uso: ./bin/kernel_scheduler [archivo_config] [path_proceso_inicial]\n");
        return EXIT_FAILURE;
    }
 
    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }
 
    logger_ks = log_create("kernel_scheduler.log",
                        "KernelScheduler",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger_ks == NULL) {
        printf("Error: no se pudo crear el logger_ks\n");
        return EXIT_FAILURE;
    }
 
    char* km_ip   = config_get_string_value(config, "KM_IP");
    char* km_port = config_get_string_value(config, "KM_PORT");
    char* ks_port = config_get_string_value(config, "PORT");
 
    // fd_km ahora es global (declarada arriba), antes era "int fd_km = crear_conexion(...)"
    fd_km = crear_conexion(km_ip, km_port);
    if (fd_km == -1) {
        log_error(logger_ks, "No se pudo conectar a Kernel Memory en %s:%s",
                  km_ip, km_port);
        return EXIT_FAILURE;
    }
    op_code codigo = MSG_HANDSHAKE_KS;
    enviar_mensaje(fd_km, &codigo, sizeof(op_code));
    log_info(logger_ks, "## Conectado a Kernel Memory");

    int size_resp;
    op_code* respuesta = recibir_mensaje(fd_km, &size_resp);
    
    if (*respuesta == MSG_OK) {
        log_info(logger_ks, "## Conectado a Kernel Memory y aceptado");
    }
    free(respuesta); 

    fd_servidor_ks = iniciar_servidor(ks_port);
    log_info(logger_ks, "Servidor KS listo en puerto %s", ks_port);

    // inicializarListasProcesos() se movió para acá arriba,
    // antes estaba DESPUÉS del while(1) y no se ejecutaba nunca 
    inicializarListasProcesos();
 
    // creamos el proceso inicial PID 0 con el path de argv[2]
    crear_proceso_inicial(argv[2]);

    // el while(1) se sacó de acá y pasó a loop_aceptar_clientes()
    // en funciones_ks.c, que corre en su propio thread.
    pthread_t thread_servidor;
    pthread_create(&thread_servidor, NULL, loop_aceptar_clientes, NULL);
    pthread_detach(thread_servidor);

    pthread_t threadPlanificadorLargoPlazo;
    pthread_t threadPlanificadorCortoPlazo;
    pthread_create(&threadPlanificadorLargoPlazo, NULL, iniciar_planificador_largo_plazo, NULL);
    pthread_create(&threadPlanificadorCortoPlazo, NULL, iniciar_planificador_corto_plazo, NULL);
    // cambiamos pthread_detach por pthread_join en los planificadores.
    // Con detach el main salía de una y mataba todo el proceso.
    // Con join el main queda bloqueado esperando que los planificadores terminen.
    pthread_join(threadPlanificadorLargoPlazo, NULL);
    pthread_join(threadPlanificadorCortoPlazo, NULL);
 
    config_destroy(config);
    log_destroy(logger_ks);
    return EXIT_SUCCESS;
}