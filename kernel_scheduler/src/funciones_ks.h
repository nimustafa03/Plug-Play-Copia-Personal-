#ifndef FUNCIONES_KS_H
#define FUNCIONES_KS_H   

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>          // para uint32_t en Proceso
#include <stdbool.h>         // bool en_compactacion, retorno de km_mem_*
#include <pthread.h>
#include <semaphore.h>       // para sem_t
#include <commons/collections/list.h>
#include <utils/mensajes.h>  // op_code (nombre_syscall)
#include "gestor_ks.h"       // trae logger_ks, config, fd_km, fd_servidor_ks


extern t_list* listaProcesosNew;
extern t_list* listaProcesosReady;
extern t_list* listaProcesosExec;
extern t_list* listaProcesosBlock;
extern t_list* listaProcesosSuspBlock;
extern t_list* listaProcesosSuspReady;
extern t_list* listaProcesosExit;
extern t_list* listaCPUsLibres; // FDs de CPUs que están libres esperando un proceso
extern t_list* listaIOsLibres; // lo mismo que arriba
extern t_list* listaMutex;

// semáforos y mutex
extern sem_t sem_hay_proceso_ready;
extern sem_t sem_hay_cpu_libre;
extern sem_t sem_hay_sleep_libre; // IO de tipo SLEEP libre
extern sem_t sem_hay_stdin_libre; // IO de tipo STDIN libre
extern sem_t sem_hay_stdout_libre; // IO de tipo STDOUT libre
extern pthread_mutex_t mutex_listas;

// comunicacion KS->KM. fd_km es UN solo socket, asi que todo request a KM
// (MEM_ALLOC/MEM_FREE/EXIT) se serializa con este mutex para no intercalar mensajes.
extern pthread_mutex_t mutex_km;
extern int contador_pids; // generador incremental de PIDs (PID 0 = proceso inicial)
extern volatile bool en_compactacion; // el corto plazo no despacha mientras se compacta
extern sem_t sem_desalojo_confirmado; // 1 post por cada CPU que confirma el desalojo por compactación

typedef enum {
  NEW,
  READY,
  EXEC,
  BLOCK,
  EXIT,
  SUSP_BLOCK,
  SUSP_READY
} estado_proceso;

typedef struct{
    int id_proceso;
    estado_proceso estado;
    int prioridad; // lo usa CMN (puede cambiar por herencia)
    int prioridad_original; // prioridad base, para restaurar tras herencia
    int fd_cpu; //FD de la CPU que lo está ejecutando (-1 si ninguna)
    long orden_suspension; // orden en que se suspendio (desempate de mediano plazo: menor = mas viejo)
} Proceso;

// representa una IO conectada y libre, con su tipo (STDIN/STDOUT/SLEEP)
typedef struct {
    int fd;
    char* tipo; // "STDIN", "STDOUT" o "SLEEP"
} t_io_ks;

typedef struct {
    int fd_cpu;
    uint32_t pid;
    int quantum;
} t_args_rr;

// argumentos para el timer de suspension por timeout
typedef struct {
    uint32_t pid;
    int timeout;
} t_args_suspension;

// argumentos para el hilo que tramita un SLEEP de punta a punta
typedef struct {
    int fd_cpu; // CPU que pidio el sleep, bloqueada esperando respuesta
    uint32_t pid;
    int tiempo; // ms a dormir
} t_args_sleep;

// argumentos para los hilos que tramitan STDIN / STDOUT de punta a punta
typedef struct {
    int fd_cpu; // CPU bloqueada esperando el MSG_OK final
    uint32_t pid;
    uint32_t dir_logica; // dirección logica donde leer/escribir en memoria de usuario
    uint32_t tamanio; // cantidad de bytes
} t_args_io_mem;

typedef struct {
    char* nombre;
    uint32_t pid_tomador;  // PID que lo tiene tomado, -1 si esta libre
    t_list* cola_bloqueados; // procesos esperando este mutex
} t_mutex_ks;

// mutex por conexión de CPU. Serializa TODO envío hacia un fd_cpu
// (respuestas de syscalls + interrupciones + despacho de PID) para que dos
// send() de hilos distintos (atender_cpu_ks, timer_rr, desalojar_por_prioridad,
// atender_sleep/stdin/stdout, compactación) nunca se entrelacen sobre el mismo socket.
typedef struct {
    int fd_cpu;
    pthread_mutex_t mutex;
} t_conexion_cpu;

extern t_list* listaConexionesCPU; // lista de t_conexion_cpu*
extern pthread_mutex_t mutex_conexiones_cpu; // protege listaConexionesCPU

pthread_mutex_t* obtener_mutex_cpu(int fd_cpu); // NULL si no está registrada
void registrar_conexion_cpu(int fd_cpu); // llamar al conectarse una CPU
void liberar_conexion_cpu(int fd_cpu); // llamar al desconectarse
void enviar_ok_cpu(int fd_cpu, op_code codigo); // respuesta simple (MSG_OK/MSG_ERROR), atómica
void enviar_interrupcion_cpu(int fd_cpu, uint32_t pid, int motivo); // MSG_INTERRUPT + t_interrupcion, atómico

void inicializarListasProcesos();
void* iniciar_planificador_largo_plazo();
void* iniciar_planificador_corto_plazo();
void actualizarEstadoProceso(Proceso* proceso, estado_proceso nuevoEstado);  // faltaba la declaración
void crear_proceso_inicial(char* path); // funcion nueva
void procesoAReady (Proceso* p); // faltaban las declaraciones de los helpers
void procesoAExec (Proceso* p);
void procesoAExit (Proceso* p);
void procesoABlock (Proceso* p);
void procesoASuspBlock (Proceso* p);
void procesoASuspReady (Proceso* p);
void* timer_rr(void* arg);
void* timer_suspension(void* arg);
t_mutex_ks* buscar_mutex(char* nombre);
void mutex_create(char* nombre);
void mutex_lock(char* nombre, Proceso* proceso);
void mutex_unlock(char* nombre, Proceso* proceso);
void atender_cpu_ks(int fd_cpu);
Proceso* buscar_proceso_por_pid(uint32_t pid);

// IO
sem_t* semaforo_io_por_tipo(char* tipo); // resuelve que semaforo corresponde a un tipo de IO
void identificar_io_ks(int fd_io); // recibe el tipo y registra la IO como libre
t_io_ks* sacar_io_libre_por_tipo(char* tipo); // busca y remueve una IO libre del tipo dado
void liberar_io(t_io_ks* io); // vuelve a poner la IO en listaIOsLibres
void* atender_sleep_ks(void* arg); // arg es t_args_sleep*; corre en hilo propio

// CMN
extern t_list** colasMultinivel;
extern char** algoritmosColas;
extern int cantidadColas;
Proceso* seleccionar_proceso_a_ejecutar(char* algoritmo, int* nivel_out);
void desalojar_por_prioridad(Proceso* proceso_entrante);
void cambiar_prioridad(Proceso* proceso, int nueva_prioridad); // herencia
Proceso* buscar_proceso_por_pid_sin_lock(uint32_t pid); // version sin lock (ya adentro de mutex_listas)

// Creacion de procesos y PIDs
uint32_t generar_pid();
void crear_proceso(char* path, int prioridad); // generaliza crear_proceso_inicial (INIT_PROC)
void km_crear_proceso(uint32_t pid, char* path); // KS->KM registrar proceso (pid + path)

// Finalizacion de proceso (EXIT / SEG_FAULT): avisa a KM y loguea el fin
void finalizar_proceso(Proceso* proceso, char* motivo);

// Comunicacion con Kernel Memory (todo serializado con mutex_km)
bool km_mem_alloc(uint32_t pid, uint32_t id_segmento, uint32_t tamanio);
bool km_mem_free(uint32_t pid, uint32_t id_segmento);
void km_notificar_exit(uint32_t pid);
void manejar_solicitud_desalojo(uint32_t pid_issuer); // desalojo por compactacion (inline en MEM_ALLOC)

// Planificacion de mediano plazo (des-suspensión SUSP_READY -> READY)
void intentar_desuspender_procesos();

// Planificacion de largo plazo - BSOD por memoria corrupta
void bsod();

// IO STDIN / STDOUT (bloquean el proceso y responden a la CPU al finalizar)
void* atender_stdin_ks(void* arg);   // arg es t_args_io_mem*
void* atender_stdout_ks(void* arg);  // arg es t_args_io_mem*
void finalizar_io_y_desbloquear(Proceso* proceso); // SUSP_BLOCK->SUSP_READY o READY

// Log obligatorio "## (<PID>) - Solicito syscall: <NOMBRE>"
const char* nombre_syscall(op_code cod);

#endif // FUNCIONES_KS_H