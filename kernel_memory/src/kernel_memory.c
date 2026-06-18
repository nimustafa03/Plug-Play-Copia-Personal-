// =============================================================
//  kernel_memory.c  —  Módulo Kernel Memory
//  Cómo ejecutar: ./bin/kernel_memory kernel_memory.config
//
//  Responsables CP1:
//    Nico S  → servidor KM acepta KS, CPU, MS
//    Santiago → servidor KM acepta SWAP
//  CP2:
//    Nico M → atender_cpu, iniciar_proceso, manejar_proceso
// =============================================================

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/list.h>
#include <utils/conexiones.h>
#include <utils/mensajes.h>
#include <kernel_memory.h>

t_log*    logger;
t_config* config;
t_dictionary* diccionario_procesos;

// ── Variables de info del Kernel Scheduler ─────────────
int fd_kernel_scheduler = 0;

// ── Variables de info de Memory Stick ──────────────────
int fd_memory_stick   = 0;
int memory_stick_size = 0;

// ── Variables de info de CPU ───────────────────────────
int fd_cpu = 0;
int id_cpu = 0;

// ── Variables de info del SWAP ─────────────────────────
int fd_swap         = -1;
int swap_block_size = 0;
int swap_total_size = 0;

void solicitar_desalojo(){
    op_code mensaje = MSG_SOLICITAR_COMPACTACION;
    enviar_mensaje(fd_kernel_scheduler, &mensaje, sizeof(op_code));
}

void compactar(uint32_t pid_proceso){
    // NICO M: Avisamos a KS que vamos a compactar y necesitamos que desaloje los CPUs.

    solicitar_desalojo();

    op_code mensaje_recibido;

    while(!mensaje_recibido){
        mensaje_recibido = recibir_mensaje(fd_kernel_scheduler,sizeof(op_code));
    }
    if (mensaje_recibido != MSG_DESALOJO_REALIZADO) { return } // NICO M: Estaria bueno logear bien el error.

    char key_proceso[12];
    snprintf(key_proceso,sizeof(key_proceso), "%lu", (unsigned long)pid_proceso); 
    
    t_contexto_ejecucion* proceso = dictionary_get(diccionario_procesos,key_proceso);

    int cursor = 0;

    for ( int i = 0; i<list_size(proceso->tabla_segmentos), i++){
        /* if segmento.base != cursor:
                movemos segmento a cursor
                actualizamos base a valor de cursor
            movemos cursor a fin de segmento (segmento.tamaño)
        */
    }

    return
}

int espacio_libre_en_segmento(int id_Segmento){ 
    int espacio_libre = config_get_int_value(config,"ESPACIO_LIBRE_MOCK");
    return espacio_libre;
}

void atender_kernel_scheduler(int fd_kernel_scheduler) {
    log_info(logger, "## Kernel Scheduler Conectado - FD del socket: %d", fd_kernel_scheduler);
    op_code respuesta = MSG_OK;
    enviar_mensaje(fd_kernel_scheduler, &respuesta, sizeof(op_code));

    // NICO M: Loop de espera activa, espera órdenes del kernel scheduler.
    while(1){
        int size;
        op_code *codigo_recibido = recibir_mensaje(fd_kernel_scheduler, &size);

        if(codigo_recibido == NULL) break;
        if(size != sizeof(op_code)) {
            log_error(logger, "Mensaje invalido recibido de KS");
            free(codigo_recibido);
            continue;
        }

        switch(*codigo_recibido) {
            case MSG_DONE:
                /* terminar_proceso(int pid); */
                break;
            case MSG_STDIN:
                enviar_mensaje(fd_kernel_scheduler,codigo_recibido,sizeof(op_code));
                break;
            case MSG_STDOUT:
                enviar_mensaje(fd_kernel_scheduler,codigo_recibido,sizeof(op_code)); 
                break;
            default:
                log_warning(logger, "Codigo desconocido: %d", *codigo_recibido);
                break;
        }

        free(codigo_recibido);
    }
}

void atender_cpu(int fd_cpu) {
    int size;

    int* ptr_id_cpu = recibir_mensaje(fd_cpu, &size);
    id_cpu = *ptr_id_cpu;

    log_info(logger, "## CPU %d Conectada", id_cpu);
    op_code ok = MSG_OK;
    enviar_mensaje(fd_cpu, &ok, sizeof(op_code));
    free(ptr_id_cpu);
    // NICO M: Loop de espera activa, hasta que reciba el mensaje de iniciar proceso.
    op_code*codigo;
    while(1){
        codigo = recibir_mensaje(fd_cpu,&size);
        if (*codigo == MSG_INIT_CPU) iniciar_proceso(fd_cpu);
    }
    free(codigo);

}

void manejar_proceso(void *arg){
    t_args_proceso* args = (t_args_proceso*) arg;
    int fd_cpu = args->fd_cpu;
    t_contexto_ejecucion* proceso = args->contexto;
    free(args);

    char ** instrucciones = proceso->instrucciones;
    log_info(logger, "## PID: %d - Imprimiendo lista de instrucciones para el proceso...", proceso->pid);
    log_info(logger,instrucciones);
    // NICO M: Esperamos a que CPU nos envíe el mensaje de pedido de instrucción.
    while(proceso->proximo_a_detener){
        int size;
        op_code*codigo = recibir_mensaje(fd_cpu,&size);
        if (*codigo == MSG_FETCH_CPU){
            // NICO M: KM Recibe PC del CPU.
            usleep(config_get_int_value(config,"INSTRUCTION_DELAY")*1000); // NICO M: Delay obligatorio por consigna.
            uint32_t* pc_ptr = recibir_mensaje(fd_cpu, &size);
            uint32_t pc = *pc_ptr;
            free(pc_ptr);

            char*proxima_instruccion = devolver_instruccion(pc, instrucciones);

            // NICO M: Chequeamos que nos haya devuelto una instrucción y no NULL.
            if (proxima_instruccion == NULL){
                log_error(logger, "## PID: %d - Obtener instruccion: %d - INSTRUCCION FUERA DE RANGO.", proceso->pid, pc);
                op_code resp = MSG_ERROR;
                enviar_mensaje(fd_cpu, &resp, sizeof(op_code));
            }
            else
            {
                log_info(logger,"## PID: %d - Obtener instrucción: %d - Instrucción: %s", proceso->pid,pc,proxima_instruccion);
                op_code resp = MSG_OK;
                enviar_mensaje(fd_cpu, &resp, sizeof(op_code));
                enviar_mensaje(fd_cpu, proxima_instruccion, strlen(proxima_instruccion) + 1);
            }
            free(proxima_instruccion);
        };
        free(codigo);
    };
    eliminar_proceso(proceso->pid);
    /* pthread_exit(); */
}

void iniciar_proceso(int fd_cpu){
    pthread_t nuevo_proceso;
    // NICO M: Recibimos pid.
    int size;
    uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
    uint32_t pid = *pid_ptr;
    free(pid_ptr);
    // NICO M: Recibimos path.
    char*path = recibir_mensaje(fd_cpu,&size);

    // NICO M: Creamos el proceso en si, creamos nuevo thread para manejarlo por separado.
    t_contexto_ejecucion*contexto_ejecucion = crear_proceso(pid,path, diccionario_procesos);
    free(path);
    t_args_proceso* args = malloc(sizeof(t_args_proceso));
    args->fd_cpu = fd_cpu;
    args->contexto = contexto_ejecucion;
    pthread_create(&nuevo_proceso, NULL, manejar_proceso, args);

    // NICO M: Enviamos Contexto de Ejecución al CPU.
    enviar_mensaje(fd_cpu,contexto_ejecucion,sizeof(t_contexto_ejecucion));

    log_info(logger, "## PID: %d - Proceso Creado.", pid);
}

void eliminar_proceso(uint32_t pid){
    t_contexto_ejecucion*a_eliminar = dictionary_get(diccionario_procesos,pid);
    dictionary_remove(diccionario_procesos, pid);
    free(a_eliminar-> tabla_segmentos);
    free(a_eliminar->instrucciones);
    free(a_eliminar);
}

void atender_memory_stick(int fd_memory_stick) {
    int memory_stick_size;

    int* ptr_memory_stick_size = recibir_mensaje(fd_memory_stick, &memory_stick_size);
    memory_stick_size = *ptr_memory_stick_size;
    free(ptr_memory_stick_size);
    
    log_info(logger, "## Memory Stick de %d bytes Conectada", memory_stick_size);
    op_code ok = MSG_OK;
    enviar_mensaje(fd_memory_stick, &ok, sizeof(op_code));
}

// -----------------------------------------------------------------
//  atender_swap  —  SANTIAGO
//  Lee el BLOCK_SIZE y tamaño total que manda el SWAP al conectarse.
// -----------------------------------------------------------------
void atender_swap(int fd) {
    fd_swap = fd;
    
    int size;

    int* block_size = recibir_mensaje(fd, &size);
    swap_block_size = *block_size;
    free(block_size);

    int* total_size = recibir_mensaje(fd, &size);
    swap_total_size = *total_size;
    free(total_size);

    log_info(logger,
             "SWAP conectado - Block size: %d bytes - Tamaño total: %d bytes",
             swap_block_size, swap_total_size);
    op_code ok = MSG_OK;
    enviar_mensaje(fd, &ok, sizeof(op_code));

    // Checkpoint 3: acá va el loop de lectura/escritura de bloques
}


// -----------------------------------------------------------------
//  atender_cliente_km
//  Corre en un hilo por cada cliente que se conecta al servidor KM.
//  Lee el handshake e identifica quién es.
// -----------------------------------------------------------------
void* atender_cliente_km(void* arg) {
    int fd_cliente = *((int*) arg);
    free(arg);

    int size;
    op_code* codigo = recibir_mensaje(fd_cliente, &size);

    switch (*codigo) {

        case MSG_HANDSHAKE_KS:
            fd_kernel_scheduler = fd_cliente;
            atender_kernel_scheduler(fd_cliente);
            break;

        case MSG_HANDSHAKE_CPU:
            fd_cpu = fd_cliente;
            atender_cpu(fd_cliente);
            break;

        case MSG_HANDSHAKE_MS:
            fd_memory_stick = fd_cliente;
            atender_memory_stick(fd_cliente);
            break;

        case MSG_HANDSHAKE_SWAP:
            fd_swap = fd_cliente;
            atender_swap(fd_cliente);
            break;
        // ─────────────────────────────────────────────────────

        default:
            log_warning(logger, "Conexion desconocida - codigo: %d - FD: %d",
                        *codigo, fd_cliente);
            break;
    }

    free(codigo);
    return NULL;
}

// -----------------------------------------------------------------
//  main
// -----------------------------------------------------------------
int main(int argc, char* argv[]) {

    // ---------------------------------------------------------
    // 1. Verificar argumentos
    // ---------------------------------------------------------
    if (argc < 2) {
        printf("Uso: ./bin/kernel_memory [archivo_config]\n");
        return EXIT_FAILURE;
    }

    // ---------------------------------------------------------
    // 2. Cargar configuración
    // ---------------------------------------------------------
    config = config_create(argv[1]);
    if (config == NULL) {
        printf("Error: no se pudo abrir el archivo de configuracion: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    // ---------------------------------------------------------
    // 3. Crear logger
    // ---------------------------------------------------------
    logger = log_create("kernel_memory.log",
                        "KernelMemory",
                        true,
                        log_level_from_string(
                            config_get_string_value(config, "LOG_LEVEL")));
    if (logger == NULL) {
        printf("Error: no se pudo crear el logger\n");
        return EXIT_FAILURE;
    }

    // ---------------------------------------------------------
    // 4. Leer puerto del config
    // ---------------------------------------------------------
    char* km_port = config_get_string_value(config, "PORT");

    // ---------------------------------------------------------
    // 5. Levantar servidor — TODO NICO S
    //    KM es el servidor central, todos se conectan acá
    // ---------------------------------------------------------
    int fd_servidor = iniciar_servidor(km_port);
    log_info(logger, "Kernel Memory listo en puerto %s. Esperando conexiones...", km_port);

    //  CP2 NICO M: Creo un diccionario para los procesos. Otro para sus hilos.
    diccionario_procesos = dictionary_create();

    // ---------------------------------------------------------
    // 6. Loop que acepta todas las conexiones entrantes
    //    (KS, CPUs, Memory Sticks y SWAP)
    // ---------------------------------------------------------
    while (1) {
        int* fd_cliente = malloc(sizeof(int));
        *fd_cliente = esperar_cliente(fd_servidor);

        pthread_t hilo;
        pthread_create(&hilo, NULL, atender_cliente_km, fd_cliente);
        pthread_detach(hilo);
    }

    // ---------------------------------------------------------
    // 7. Limpieza
    // ---------------------------------------------------------
    config_destroy(config);
    log_destroy(logger);
    dictionary_destroy_and_destroy_elements(diccionario_procesos, free);

    return EXIT_SUCCESS;
}