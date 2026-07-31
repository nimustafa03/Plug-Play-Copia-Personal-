#include "funciones_ks.h"

#include <string.h>            // strcmp, strdup (evita implicit-declaration)
#include <unistd.h>           // usleep
#include <commons/log.h>
#include <utils/conexiones.h>  
#include <utils/mensajes.h>    
#include <utils/tipos.h>   


// CP3 FIX: reintentos/espera para des-suspender un proceso antes de una IO que
// toca memoria de usuario (ver asegurar_proceso_en_memoria).
#define REINTENTOS_DESUSPENSION_IO 20
#define ESPERA_DESUSPENSION_IO_US  50000 // 50ms -> hasta 1s de espera total

// orden corregido para matchear el enum estado_proceso (estaba invertido SUSP_BLOCK/SUSP_READY)
char const* estadosProcesos[] = {"NEW", "READY", "EXEC", "BLOCK", "EXIT", "SUSP_BLOCK", "SUSP_READY"};

t_list* listaProcesosNew = NULL;
t_list* listaProcesosReady = NULL;
t_list* listaProcesosExec = NULL;
t_list* listaProcesosBlock = NULL;
t_list* listaProcesosSuspBlock = NULL;
t_list* listaProcesosSuspReady = NULL;
t_list* listaProcesosExit = NULL;
t_list* listaCPUsLibres = NULL; // lista de FDs de CPUs libres 
t_list* listaIOsLibres = NULL; // lo mismo que arriba, pero para IO
t_list* listaMutex = NULL;

// Colas Multinivel (CMN) - solo se usan cuando PLANIFICATION_ALGORITHM=CMN.
// colasMultinivel[i] es la cola de READY de prioridad i (0 = mas prioritaria).
// algoritmosColas[i] es el string del algoritmo de esa cola ("FIFO" o "RR").
t_list** colasMultinivel = NULL;
char** algoritmosColas = NULL;
int cantidadColas = 0;

// semáforos y mutex nuevos 
sem_t sem_hay_proceso_ready;
sem_t sem_hay_cpu_libre;
sem_t sem_hay_sleep_libre; 
sem_t sem_hay_stdin_libre;  
sem_t sem_hay_stdout_libre; 
pthread_mutex_t mutex_listas;

// CP3
pthread_mutex_t mutex_km;              // serializa toda comunicación KS->KM
int contador_pids = 0;                 // próximo PID a asignar (arranca en 0 = proceso inicial)
volatile bool en_compactacion = false; // gate del corto plazo durante compactación
sem_t sem_desalojo_confirmado;         // confirmaciones de CPUs desalojadas por compactación
static long reloj_suspension = 0;      // sello incremental para desempatar por antigüedad de suspensión

// CP3: mutex por conexión de CPU (ver comentario en funciones_ks.h)
t_list* listaConexionesCPU = NULL;
pthread_mutex_t mutex_conexiones_cpu;


void inicializarListasProcesos() {
  // DEBUG: frontera de funcion
  log_debug(logger_ks, "[DBG][inicializarListasProcesos] ENTRADA");
  listaProcesosNew = list_create();
  listaProcesosReady = list_create();
  listaProcesosExec = list_create();
  listaProcesosBlock = list_create();
  listaProcesosSuspBlock = list_create();
  listaProcesosSuspReady = list_create();
  listaProcesosExit = list_create(); 
  listaCPUsLibres = list_create();
  listaIOsLibres = list_create();
  listaMutex = list_create();

  // arrancan bloqueados
  sem_init(&sem_hay_proceso_ready, 0, 0);
  sem_init(&sem_hay_cpu_libre, 0, 0);
  sem_init(&sem_hay_sleep_libre, 0, 0);
  sem_init(&sem_hay_stdin_libre, 0, 0);
  sem_init(&sem_hay_stdout_libre, 0, 0);
  // mutex para proteger acceso concurrente a las listas
  pthread_mutex_init(&mutex_listas, NULL);

  // CP3
  pthread_mutex_init(&mutex_km, NULL);
  sem_init(&sem_desalojo_confirmado, 0, 0);
  listaConexionesCPU = list_create();               // CP3
  pthread_mutex_init(&mutex_conexiones_cpu, NULL);  // CP3

  // Inicialización defensiva de Colas Multinivel
  cantidadColas = 0;
  
  if (config_has_property(config, "QUEUES_ALGORITHMS")) {
      algoritmosColas = config_get_array_value(config, "QUEUES_ALGORITHMS");
  } else {
      algoritmosColas = NULL;
  }

  if (algoritmosColas != NULL) {
      while (algoritmosColas[cantidadColas] != NULL) {
          cantidadColas++;
      }
  }

  if (cantidadColas > 0) {
      colasMultinivel = malloc(sizeof(t_list*) * cantidadColas);
      log_debug(logger_ks, "[DBG][inicializarListasProcesos] malloc colasMultinivel=%p (%d colas)", (void*)colasMultinivel, cantidadColas);
      for (int i = 0; i < cantidadColas; i++) {
          colasMultinivel[i] = list_create();
      }
      log_info(logger_ks, "CMN: %d colas de prioridad inicializadas", cantidadColas);
  } else {
      colasMultinivel = NULL;
      log_info(logger_ks, "Planificación sin colas multinivel (RR/FIFO)");
  }

  // DEBUG: frontera de funcion
  log_debug(logger_ks, "[DBG][inicializarListasProcesos] SALIDA");
}

// CP3: genera el próximo PID de forma segura. El primero es 0 (proceso inicial).
uint32_t generar_pid() {
    pthread_mutex_lock(&mutex_listas);
    uint32_t pid = contador_pids++;
    pthread_mutex_unlock(&mutex_listas);
    // DEBUG: frontera de funcion (valor devuelto)
    log_debug(logger_ks, "[DBG][generar_pid] SALIDA -> pid=%d", pid);
    return pid;
}

// CP3: le pide a KM que registre el proceso (pid + path) apenas KS lo crea, tanto
// para el proceso inicial como para cada INIT_PROC. Serializado con mutex_km porque
// fd_km es un único socket compartido; leemos el ack para no desincronizar el stream.
void km_crear_proceso(uint32_t pid, char* path) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_crear_proceso] ENTRADA - pid=%d, path=%p ('%s')", pid, (void*)path, path ? path : "NULL");
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_CREAR_PROCESO;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_crear_proceso] pid=%d - envío MSG_CREAR_PROCESO + pid + path a KM fd=%d", pid, fd_km);
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, path, strlen(path) + 1);

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][km_crear_proceso] pid=%d - ack KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);
    if (resp == NULL || *resp != MSG_OK)
        log_error(logger_ks, "## KM no confirmó la creación del PID %d", pid);
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][km_crear_proceso] pid=%d - free(resp=%p)", pid, (void*)resp);
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_crear_proceso] SALIDA - pid=%d", pid);
}

// CP3: crea un proceso con la prioridad dada y lo mete en NEW.
// El "path" (archivo de instrucciones) lo administra Kernel Memory al pedir el
// contexto por PID; KS sólo trackea el ciclo de vida del proceso.
void crear_proceso(char* path, int prioridad) {
    Proceso* p = malloc(sizeof(Proceso));
    // DEBUG: heap - direccion del PCB nuevo (clave para cazar use-after-free)
    log_debug(logger_ks, "[DBG][crear_proceso] malloc Proceso=%p, prioridad=%d, path='%s'", (void*)p, prioridad, path ? path : "NULL");
    p->id_proceso = generar_pid();
    p->estado = NEW;
    p->prioridad = prioridad;
    p->prioridad_original = prioridad;
    p->fd_cpu = -1;  // sin CPU asignada todavía
    p->orden_suspension = 0;
    p->en_swap = false; // todavia no fue suspendido

    // log obligatorio
    log_info(logger_ks, "## (%d) Se crea el proceso - Estado: NEW", p->id_proceso);

    // registrar el proceso (con su path) en KM antes de que pueda despacharse,
    // así KM ya tiene las instrucciones cuando la CPU le pida el contexto por PID.
    km_crear_proceso(p->id_proceso, path);

    pthread_mutex_lock(&mutex_listas);
    // DEBUG: insercion en cola NEW (tamaño antes/despues)
    log_debug(logger_ks, "[DBG][crear_proceso] pid=%d - insertando en NEW (size antes=%d)", p->id_proceso, list_size(listaProcesosNew));
    list_add(listaProcesosNew, p);
    log_debug(logger_ks, "[DBG][crear_proceso] pid=%d - insertado en NEW (size despues=%d)", p->id_proceso, list_size(listaProcesosNew));
    pthread_mutex_unlock(&mutex_listas);
}

// crea el proceso PID 0 desde argv[2] y lo mete en NEW (prioridad máxima = 0)
void crear_proceso_inicial(char* path) {
    crear_proceso(path, 0);
}

// desalojo por cola mas prioritaria (QUEUE_PREEMPTION).
// Si el proceso que entra a READY es mas prioritario que el proceso EXEC menos
// prioritario, lo desaloja mandandole una interrupción a su CPU.
// Debe llamarse con mutex_listas YA tomado.
void desalojar_por_prioridad(Proceso* proceso_entrante) {
  if (strcmp(config_get_string_value(config, "QUEUE_PREEMPTION"), "TRUE") != 0) return;
  if (strcmp(config_get_string_value(config, "PLANIFICATION_ALGORITHM"), "CMN") != 0) return;

  // buscar el proceso en EXEC menos prioritario (mayor numero de prioridad)
  Proceso* victima = NULL;
  for (int i = 0; i < list_size(listaProcesosExec); i++) {
    Proceso* p = list_get(listaProcesosExec, i);
    if (victima == NULL || p->prioridad > victima->prioridad) {
      victima = p;
    }
  }

  // solo desaloja si el entrante es estrictamente mas prioritario (numero menor)
  if (victima == NULL || proceso_entrante->prioridad >= victima->prioridad) return;

  log_info(logger_ks,
    "## (%d) Prioridad: %d - Desalojado por cola más prioritaria por el proceso %d con prioridad %d",
    victima->id_proceso, victima->prioridad, proceso_entrante->id_proceso, proceso_entrante->prioridad);

  // DEBUG: serializacion - interrupcion a la CPU de la victima
  log_debug(logger_ks, "[DBG][desalojar_por_prioridad] envío MSG_INTERRUPT (motivo=1) a fd_cpu=%d, victima pid=%d ptr=%p", victima->fd_cpu, victima->id_proceso, (void*)victima);

  // CP3: interrupción atómica contra cualquier otro envío a la misma CPU
  enviar_interrupcion_cpu(victima->fd_cpu, victima->id_proceso, 1); // 1 = desalojo por prioridad
  // el resto (proceso vuelve a READY, CPU liberada) lo maneja MSG_INTERRUPCION_ATENDIDA
}

void procesoAReady(Proceso* proceso){
  char* algoritmo = config_get_string_value(config, "PLANIFICATION_ALGORITHM");

  if (strcmp(algoritmo, "CMN") == 0) {
    // encolar en la cola de su prioridad. La consigna dice que no se
    // planifican procesos con prioridad fuera del rango de colas configuradas.
    if (proceso->prioridad < 0 || proceso->prioridad >= cantidadColas) {
      log_error(logger_ks, "## (%d) prioridad %d fuera de rango [0..%d], no se encola",
                proceso->id_proceso, proceso->prioridad, cantidadColas - 1);
      return;
    }
    // DEBUG: insercion en cola CMN (tamaño antes)
    log_debug(logger_ks, "[DBG][procesoAReady] pid=%d ptr=%p -> cola CMN[%d] (size antes=%d)", proceso->id_proceso, (void*)proceso, proceso->prioridad, list_size(colasMultinivel[proceso->prioridad]));
    list_add(colasMultinivel[proceso->prioridad], proceso);
    log_info(logger_ks, "proceso %d agregado a cola CMN de prioridad %d",
             proceso->id_proceso, proceso->prioridad);
    desalojar_por_prioridad(proceso);
  } else {
    // DEBUG: insercion en READY (tamaño antes)
    log_debug(logger_ks, "[DBG][procesoAReady] pid=%d ptr=%p -> READY (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosReady));
    list_add(listaProcesosReady, proceso);
    log_info(logger_ks, "proceso %d agregado a lista READY", proceso->id_proceso);
  }
}

void procesoAExec(Proceso* proceso){
  // DEBUG: insercion en EXEC
  log_debug(logger_ks, "[DBG][procesoAExec] pid=%d ptr=%p -> EXEC (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosExec));
  list_add(listaProcesosExec, proceso);
  log_info(logger_ks, "proceso %d agregado a lista EXEC", proceso->id_proceso);
}

void procesoAExit(Proceso* proceso){
  // DEBUG: insercion en EXIT
  log_debug(logger_ks, "[DBG][procesoAExit] pid=%d ptr=%p -> EXIT (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosExit));
  list_add(listaProcesosExit, proceso);
  log_info(logger_ks, "Se terminó la proceso %d agregado a lista EXIT", proceso->id_proceso);
}

void procesoABlock(Proceso* proceso){
  // DEBUG: insercion en BLOCK
  log_debug(logger_ks, "[DBG][procesoABlock] pid=%d ptr=%p -> BLOCK (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosBlock));
  list_add(listaProcesosBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista BLOCK", proceso->id_proceso);
}

void procesoASuspBlock(Proceso* proceso){
  proceso->orden_suspension = reloj_suspension++; // CP3: antigüedad de suspensión
  // DEBUG: insercion en SUSP_BLOCK
  log_debug(logger_ks, "[DBG][procesoASuspBlock] pid=%d ptr=%p -> SUSP_BLOCK (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosSuspBlock));
  list_add(listaProcesosSuspBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP BLOCK", proceso->id_proceso);
}

void procesoASuspReady(Proceso* proceso){
  // conserva el orden_suspension original si venía de SUSP_BLOCK; si entra directo, lo sella
  if (proceso->orden_suspension == 0) proceso->orden_suspension = reloj_suspension++;
  // DEBUG: insercion en SUSP_READY
  log_debug(logger_ks, "[DBG][procesoASuspReady] pid=%d ptr=%p -> SUSP_READY (size antes=%d)", proceso->id_proceso, (void*)proceso, list_size(listaProcesosSuspReady));
  list_add(listaProcesosSuspReady, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP READY", proceso->id_proceso);
}

void* iniciar_planificador_largo_plazo (void* arg) {
  log_info(logger_ks, "Planificador de Largo Plazo iniciado.");
  while (1) {

    // un solo lock que chequea y remueve. Evita race condition entre dos locks separados
    pthread_mutex_lock(&mutex_listas);
    if (list_is_empty(listaProcesosNew)) {
      pthread_mutex_unlock(&mutex_listas);
      usleep(1000); // evita busy-wait
      continue;     // vuelve al while(1)
    }
    Proceso* nuevoProceso = list_get(listaProcesosNew, 0);
    pthread_mutex_unlock(&mutex_listas);

    // DEBUG: extraccion de NEW - puntero y pid antes de mover a READY
    log_debug(logger_ks, "[DBG][planif_largo] tomado de NEW: pid=%d ptr=%p (NEW size=%d) -> pasando a READY", nuevoProceso->id_proceso, (void*)nuevoProceso, list_size(listaProcesosNew));

    actualizarEstadoProceso(nuevoProceso, READY);
    sem_post(&sem_hay_proceso_ready); // avisa al planificador de corto plazo
  }
}

void* timer_rr(void* arg) {
    t_args_rr* args = (t_args_rr*) arg;
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][timer_rr] ENTRADA - pid=%d, fd_cpu=%d, quantum=%d, args=%p", args->pid, args->fd_cpu, args->quantum, (void*)args);

    // dormimos el quantum (usleep espera microsegundos, el config está en ms)
    usleep(args->quantum * 1000);

    // DEBUG: serializacion - interrupcion por fin de quantum
    log_debug(logger_ks, "[DBG][timer_rr] pid=%d - envío MSG_INTERRUPT (motivo=0) a fd_cpu=%d", args->pid, args->fd_cpu);

    // CP3: el timer solo debe interrumpir si el proceso SIGUE ejecutando en esta
    // misma CPU. Si antes del quantum se bloqueó por IO / terminó / fue desalojado,
    // este timer quedó viejo y NO debe mandar nada: la CPU ya se liberó y está en
    // esperar_pid, donde leería el MSG_INTERRUPT (=16) como un PID basura.
    pthread_mutex_lock(&mutex_listas);
    Proceso* proc_rr = buscar_proceso_por_pid_sin_lock(args->pid);
    bool sigue_exec = (proc_rr != NULL && proc_rr->estado == EXEC && proc_rr->fd_cpu == args->fd_cpu);
    pthread_mutex_unlock(&mutex_listas);

    if (sigue_exec) {
        // interrupción atómica contra cualquier otro envío a la misma CPU
        enviar_interrupcion_cpu(args->fd_cpu, args->pid, 0); // 0 = fin de quantum
    } else {
        log_debug(logger_ks, "[DBG][timer_rr] pid=%d - quantum vencido pero el proceso ya no está en EXEC en fd_cpu=%d, timer descartado", args->pid, args->fd_cpu);
    }

    // DEBUG: heap
    log_debug(logger_ks, "[DBG][timer_rr] SALIDA - free(args=%p)", (void*)args);
    free(args);
    return NULL;
}

// timer de suspensión - si al vencer SUSPENSION_TIMEOUT el proceso sigue en BLOCK, pasa a SUSP_BLOCK
void* timer_suspension(void* arg) {
    t_args_suspension* args = (t_args_suspension*) arg;
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][timer_suspension] ENTRADA - pid=%d, timeout=%d, args=%p", args->pid, args->timeout, (void*)args);

    usleep(args->timeout * 1000); // SUSPENSION_TIMEOUT está en ms

    Proceso* proceso = buscar_proceso_por_pid(args->pid);
    // DEBUG: resultado de la busqueda (NULL si el proceso ya salio/termino)
    log_debug(logger_ks, "[DBG][timer_suspension] pid=%d - buscar_proceso -> ptr=%p", args->pid, (void*)proceso);

    if (proceso != NULL) {
        pthread_mutex_lock(&mutex_listas);
        estado_proceso estado_actual = proceso->estado;
        pthread_mutex_unlock(&mutex_listas);

        if (estado_actual == BLOCK) {
            // primero movemos el proceso de lista (para que si la IO termina
            // justo ahora, finalizar_io_y_desbloquear lo mande a SUSP_READY y no a
            // READY), y recien despues le pedimos a KM que lo baje a SWAP.
            actualizarEstadoProceso(proceso, SUSP_BLOCK);

            if (km_suspender_proceso(args->pid)) {
                pthread_mutex_lock(&mutex_listas);
                proceso->en_swap = true;
                estado_proceso estado_post = proceso->estado;
                pthread_mutex_unlock(&mutex_listas);

                // Si la IO termino mientras KM estaba bajando los segmentos a SWAP,
                // el proceso ya paso a SUSP_READY y su intento de des-suspension se
                // salteo (en_swap todavia era false). Lo reintentamos aca.
                if (estado_post == SUSP_READY) intentar_desuspender_procesos();
            } else {
                // KM no pudo suspenderlo: la memoria del proceso sigue ocupada.
                log_error(logger_ks, "## (%d) Kernel Memory no pudo suspender el proceso", args->pid);
            }
        }
        // si ya no está en BLOCK (se desbloqueó antes de vencer el timeout), no hacemos nada
    }

    // DEBUG: heap
    log_debug(logger_ks, "[DBG][timer_suspension] SALIDA - pid=%d, free(args=%p)", args->pid, (void*)args);
    free(args);
    return NULL;
}

// selecciona el proximo proceso a ejecutar segun el algoritmo activo.
// Debe llamarse con mutex_listas YA tomado.
// Para CMN, ademas devuelve por parametro el nivel/cola del que salio el proceso
// (para saber que algoritmo — FIFO o RR — aplicarle). Para FIFO/RR, nivel = -1.
// Devuelve NULL si no hay ningun proceso disponible.
Proceso* seleccionar_proceso_a_ejecutar(char* algoritmo, int* nivel_out) {
    *nivel_out = -1;

    if (strcmp(algoritmo, "CMN") == 0) {
        // recorre las colas de mayor a menor prioridad (0 es la mas prioritaria)
        for (int i = 0; i < cantidadColas; i++) {
            if (!list_is_empty(colasMultinivel[i])) {
                *nivel_out = i;
                Proceso* elegido = list_get(colasMultinivel[i], 0); // consulta sin remover
                // DEBUG: seleccion de cola CMN
                log_debug(logger_ks, "[DBG][seleccionar_proceso] CMN: seleccionado pid=%d ptr=%p de cola[%d] (size=%d)", 
                          elegido->id_proceso, (void*)elegido, i, list_size(colasMultinivel[i]));
                return elegido;
            }
        }
        // DEBUG: frontera de funcion (retorno NULL)
        log_debug(logger_ks, "[DBG][seleccionar_proceso] CMN: todas las colas vacias -> NULL");
        return NULL; // todas las colas vacias
    }

    // FIFO / RR: una sola cola
    if (list_is_empty(listaProcesosReady)) {
        // DEBUG: frontera de funcion (retorno NULL)
        log_debug(logger_ks, "[DBG][seleccionar_proceso] %s: READY vacia -> NULL", algoritmo);
        return NULL;
    }
    Proceso* elegido = list_get(listaProcesosReady, 0); // consulta sin remover
    // DEBUG: seleccion de READY
    log_debug(logger_ks, "[DBG][seleccionar_proceso] %s: seleccionado pid=%d ptr=%p de READY (size=%d)", 
              algoritmo, elegido->id_proceso, (void*)elegido, list_size(listaProcesosReady));
    return elegido;
}

// Planif. Corto plazo: implementa FIFO, RR y CMN
void* iniciar_planificador_corto_plazo(void* arg) {
  log_info(logger_ks, "Planificador de Corto Plazo iniciado.");
 
  char* algoritmo = config_get_string_value(config, "PLANIFICATION_ALGORITHM");
 
  while (1) {
    // CP3: durante una compactación no se despacha ningún proceso nuevo
    while (en_compactacion) usleep(1000);

    // bloquea hasta que haya proceso en READY Y cpu libre
    sem_wait(&sem_hay_proceso_ready);
    sem_wait(&sem_hay_cpu_libre);

    // DEBUG: paso los dos semaforos
    log_debug(logger_ks, "[DBG][planif_corto] semaforos ok (ready+cpu) - entrando a seccion critica");
 
    pthread_mutex_lock(&mutex_listas);
 
    if (list_is_empty(listaCPUsLibres)) {
      pthread_mutex_unlock(&mutex_listas);
      // CP3: no consumimos nada útil -> devolvemos los tokens para no starvar
      sem_post(&sem_hay_proceso_ready);
      sem_post(&sem_hay_cpu_libre);
      continue;
    }

    int nivel = -1;
    Proceso* proceso = seleccionar_proceso_a_ejecutar(algoritmo, &nivel);
    if (proceso == NULL) {
      pthread_mutex_unlock(&mutex_listas);
      // CP3: había token de ready pero ningún proceso despachable (p.ej. prioridad
      // fuera de rango en CMN) -> devolvemos ambos tokens
      sem_post(&sem_hay_proceso_ready);
      sem_post(&sem_hay_cpu_libre);
      continue;
    }

    int* fd_cpu_ptr = list_remove(listaCPUsLibres, 0);
    int  fd_cpu = *fd_cpu_ptr;
    // DEBUG: heap - liberamos el int* que envolvia al fd
    log_debug(logger_ks, "[DBG][planif_corto] pid=%d - CPU tomada fd=%d, free(fd_cpu_ptr=%p), CPUs libres ahora=%d", proceso->id_proceso, fd_cpu, (void*)fd_cpu_ptr, list_size(listaCPUsLibres));
    free(fd_cpu_ptr);
 
    pthread_mutex_unlock(&mutex_listas);
 
    proceso->fd_cpu = fd_cpu;
    actualizarEstadoProceso(proceso, EXEC);
 
    // DEBUG: serializacion - despacho del PID a la CPU
    log_debug(logger_ks, "[DBG][planif_corto] pid=%d - envío PID a CPU fd=%d (nivel CMN=%d)", proceso->id_proceso, fd_cpu, nivel);

    // envia el PID a la CPU para que empiece a ejecutar
    // CP3: protegido para que un timer_rr rezagado no se entrelace con el despacho
    pthread_mutex_t* m_cpu = obtener_mutex_cpu(fd_cpu);
    if (m_cpu) pthread_mutex_lock(m_cpu);
    op_code cod = MSG_SOLICITAR_PID;                 // Mando codigo del mensaje para aclarar al CPU si es un PID o una interrupcion 
    enviar_mensaje(fd_cpu, &cod, sizeof(op_code));
    enviar_mensaje(fd_cpu, &proceso->id_proceso, sizeof(uint32_t));
    if (m_cpu) pthread_mutex_unlock(m_cpu);
 
    // Determina si hay que lanzar timer de quantum:
    // - FIFO/RR plano: segun PLANIFICATION_ALGORITHM
    // - CMN: segun el algoritmo de la cola de la que salio el proceso
    char* algoritmo_efectivo = algoritmo;
    if (strcmp(algoritmo, "CMN") == 0 && nivel >= 0) {
        algoritmo_efectivo = algoritmosColas[nivel];
    }

    if (strcmp(algoritmo_efectivo, "RR") == 0) {
        t_args_rr* args = malloc(sizeof(t_args_rr));
        // DEBUG: heap
        log_debug(logger_ks, "[DBG][planif_corto] pid=%d - malloc t_args_rr=%p, lanzando timer_rr", proceso->id_proceso, (void*)args);
        args->fd_cpu = fd_cpu;
        args->pid = proceso->id_proceso;
        args->quantum = config_get_int_value(config, "RR_QUANTUM");

        pthread_t thread_timer;
        pthread_create(&thread_timer, NULL, timer_rr, args);
        pthread_detach(thread_timer);
    }
  }
}


void actualizarEstadoProceso (Proceso* proceso, estado_proceso nuevoEstado){
  // DEBUG: frontera de funcion - loguea el PUNTERO CRUDO antes de desreferenciarlo.
  // Si aca se ve proceso=(nil) y justo despues hay un SIGSEGV, el crash es el
  // acceso a proceso->id_proceso / proceso->estado con puntero NULL.
  log_debug(logger_ks, "[DBG][actualizarEstadoProceso] ENTRADA - proceso=%p, nuevoEstado=%s", (void*)proceso, estadosProcesos[nuevoEstado]);

  pthread_mutex_lock(&mutex_listas); // evita race conditions entre planificadores

  Proceso* procesoEncontrado = NULL;
  int lanzar_timer_suspension = 0; // se decide adentro del lock, se ejecuta afuera
  log_debug(logger_ks, "Actualizando estado de Query %d de %s a %s", proceso->id_proceso, estadosProcesos[proceso->estado], estadosProcesos[nuevoEstado]);
  
  estado_proceso estado_anterior = proceso->estado;
    switch(estado_anterior){
        case NEW:
          // DEBUG: extraccion de cola NEW (tamaño antes)
          log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en NEW (size=%d)", proceso->id_proceso, list_size(listaProcesosNew));
          for(int i = 0; i < list_size(listaProcesosNew); i++) {
              Proceso* procesoTemporal= list_get(listaProcesosNew, i);
              if(procesoTemporal->id_proceso == proceso->id_proceso) {
                  procesoEncontrado = list_remove(listaProcesosNew, i);
                  break;
              }
          }
          break;
        case READY:
          if (strcmp(config_get_string_value(config, "PLANIFICATION_ALGORITHM"), "CMN") == 0) {
            // DEBUG: extraccion de colas CMN
            log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en colas CMN (%d colas)", proceso->id_proceso, cantidadColas);
            for (int c = 0; c < cantidadColas && procesoEncontrado == NULL; c++) {
              for (int i = 0; i < list_size(colasMultinivel[c]); i++) {
                Proceso* procesoTemporal = list_get(colasMultinivel[c], i);
                if (procesoTemporal->id_proceso == proceso->id_proceso) {
                  procesoEncontrado = list_remove(colasMultinivel[c], i);
                  break;
                }
              }
            }
          } else {
            // DEBUG: extraccion de READY (tamaño antes)
            log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en READY (size=%d)", proceso->id_proceso, list_size(listaProcesosReady));
            for(int i = 0; i < list_size(listaProcesosReady); i++) {
                Proceso* procesoTemporal= list_get(listaProcesosReady, i);
                if(procesoTemporal->id_proceso == proceso->id_proceso) {
                  procesoEncontrado = list_remove(listaProcesosReady, i);
                  break;
                }
            }
          }
          break;
        case EXEC:
          // DEBUG: extraccion de EXEC (tamaño antes)
          log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en EXEC (size=%d)", proceso->id_proceso, list_size(listaProcesosExec));
          for(int i = 0; i < list_size(listaProcesosExec); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosExec, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosExec, i);
              break;
            }
          }
          break;
        case BLOCK:
          // DEBUG: extraccion de BLOCK (tamaño antes)
          log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en BLOCK (size=%d)", proceso->id_proceso, list_size(listaProcesosBlock));
          for(int i = 0; i < list_size(listaProcesosBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosBlock, i);
              break;
            }
          }
          break;
        case SUSP_BLOCK:
          // DEBUG: extraccion de SUSP_BLOCK (tamaño antes)
          log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en SUSP_BLOCK (size=%d)", proceso->id_proceso, list_size(listaProcesosSuspBlock));
          for(int i = 0; i < list_size(listaProcesosSuspBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosSuspBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosSuspBlock, i);
              break;
            }
          }
          break;
        case SUSP_READY:
          // DEBUG: extraccion de SUSP_READY (tamaño antes)
          log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - buscando en SUSP_READY (size=%d)", proceso->id_proceso, list_size(listaProcesosSuspReady));
          for(int i = 0; i < list_size(listaProcesosSuspReady); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosSuspReady, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              log_debug(logger_ks, "Query encontrada en lista EXIT, removiendo");
              procesoEncontrado = list_remove(listaProcesosSuspReady, i);
              break;
            }
          }
          break;
        default:
          log_error(logger_ks, "Error: estado de proceso desconocido");
          pthread_mutex_unlock(&mutex_listas);
          return;
    }

    // DEBUG: resultado de la extraccion - si no lo encontro se usa el puntero recibido
    log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - procesoEncontrado=%p (recibido=%p, %s)", proceso->id_proceso, (void*)procesoEncontrado, (void*)proceso, procesoEncontrado == NULL ? "NO estaba en su lista, se confia en el recibido" : "removido de su lista");

    // CP3: si no lo encontramos en la lista de su estado_anterior, puede ser
    // porque quien nos llamo ya lo saco de ahi antes de pedir el cambio de
    // estado (pasa en el planificador de corto plazo: seleccionar_proceso_a_ejecutar
    // ya hace list_remove antes de llamar a actualizarEstadoProceso). En ese
    // caso confiamos en el puntero que recibimos en vez de cortar con error.
    if(procesoEncontrado == NULL) {
      procesoEncontrado = proceso;
    }
    {
        // log obligatorio 
        log_info(logger_ks, "## (%d) Pasa del estado %s al estado %s",
                 proceso->id_proceso,
                 estadosProcesos[estado_anterior],
                 estadosProcesos[nuevoEstado]);

        switch(nuevoEstado){
            case READY:
                procesoEncontrado->estado = READY;
                procesoAReady(procesoEncontrado);
                break;
            case EXEC:
                procesoEncontrado->estado = EXEC;
                procesoAExec(procesoEncontrado);
                break;
            case EXIT:
                procesoEncontrado->estado = EXIT;
                procesoAExit(procesoEncontrado);
                break;
            case BLOCK:
                procesoEncontrado->estado = BLOCK;
                procesoABlock(procesoEncontrado);
                lanzar_timer_suspension = 1;
                break;
            case SUSP_READY:
                procesoEncontrado->estado = SUSP_READY;
                procesoASuspReady(procesoEncontrado);
                break;
            case SUSP_BLOCK:
                procesoEncontrado->estado = SUSP_BLOCK;
                procesoASuspBlock(procesoEncontrado);
                break;
            default:
                log_error(logger_ks, "Error: nuevo estado de query invalido");
                switch(estado_anterior) {
                    case READY: procesoAReady(procesoEncontrado); break;
                    case EXEC: procesoAExec(procesoEncontrado); break;
                    case EXIT: procesoAExit(procesoEncontrado); break;
                    case BLOCK: procesoABlock(procesoEncontrado); break;
                    case SUSP_READY: procesoASuspReady(procesoEncontrado); break;
                    case SUSP_BLOCK: procesoABlock(procesoEncontrado); break;
                    default: log_error(logger_ks, "ERORR: estado anterior de query inválido");
                }
                pthread_mutex_unlock(&mutex_listas);
                return;
        }
    }
    pthread_mutex_unlock(&mutex_listas);

    if (lanzar_timer_suspension) {
        t_args_suspension* args_susp = malloc(sizeof(t_args_suspension));
        // DEBUG: heap
        log_debug(logger_ks, "[DBG][actualizarEstadoProceso] pid=%d - malloc t_args_suspension=%p, lanzando timer_suspension", proceso->id_proceso, (void*)args_susp);
        args_susp->pid = proceso->id_proceso;
        args_susp->timeout = config_get_int_value(config, "SUSPENSION_TIMEOUT");

        pthread_t thread_susp;
        pthread_create(&thread_susp, NULL, timer_suspension, args_susp);
        pthread_detach(thread_susp);
    }
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][actualizarEstadoProceso] SALIDA - pid=%d ya en %s", proceso->id_proceso, estadosProcesos[nuevoEstado]);
}

t_mutex_ks* buscar_mutex(char* nombre) {
  for (int i = 0; i < list_size(listaMutex); i++) {
      t_mutex_ks* m = list_get(listaMutex, i);
      if (strcmp(m->nombre, nombre) == 0) return m;
  }
  return NULL;
}

void mutex_create(char* nombre) {
  if (buscar_mutex(nombre) != NULL) {
      log_warning(logger_ks, "## MUTEX_CREATE: mutex '%s' ya existe", nombre);
      return;
  }
  t_mutex_ks* m = malloc(sizeof(t_mutex_ks));
  // DEBUG: heap
  log_debug(logger_ks, "[DBG][mutex_create] malloc t_mutex_ks=%p para '%s'", (void*)m, nombre);
  m->nombre = strdup(nombre);
  m->pid_tomador = -1;
  m->cola_bloqueados = list_create();

  pthread_mutex_lock(&mutex_listas);
  list_add(listaMutex, m);
  pthread_mutex_unlock(&mutex_listas);

  log_info(logger_ks, "## MUTEX_CREATE: mutex '%s' creado", nombre);
}

// cambia la prioridad de un proceso con el log obligatorio.
// Debe llamarse con mutex_listas YA tomado.
void cambiar_prioridad(Proceso* proceso, int nueva_prioridad) {
  if (proceso->prioridad == nueva_prioridad) return;
  log_info(logger_ks, "## %d Cambio de prioridad: %d - %d",
           proceso->id_proceso, proceso->prioridad, nueva_prioridad);
  proceso->prioridad = nueva_prioridad;
}

int mutex_lock(char* nombre, Proceso* proceso) {
  // DEBUG: frontera de funcion - PUNTERO CRUDO antes de desreferenciar.
  // Si proceso=(nil) aca, el proceso->id_proceso de abajo segfaultea (llega NULL
  // desde MSG_MUTEX_LOCK cuando buscar_proceso_por_pid no lo encontro).
  log_debug(logger_ks, "[DBG][mutex_lock] ENTRADA - nombre='%s', proceso=%p", nombre ? nombre : "NULL", (void*)proceso);
  pthread_mutex_lock(&mutex_listas);
  t_mutex_ks* m = buscar_mutex(nombre);
  if (m == NULL) {
      log_error(logger_ks, "## MUTEX_LOCK: mutex '%s' no existe", nombre);
      pthread_mutex_unlock(&mutex_listas);
      // FIX: hay que destrabar la CPU igual, sino queda colgada en el recv.
      enviar_ok_cpu(proceso->fd_cpu, MSG_ERROR);
      return 0;
  }

  if (m->pid_tomador == -1) {
      // libre, lo toma
      m->pid_tomador = proceso->id_proceso;
      log_info(logger_ks, "## (%d) Toma el Mutex %s", proceso->id_proceso, nombre);
      pthread_mutex_unlock(&mutex_listas);
      // DEBUG: serializacion
      log_debug(logger_ks, "[DBG][mutex_lock] pid=%d - envío MSG_OK a fd_cpu=%d", proceso->id_proceso, proceso->fd_cpu);
      enviar_ok_cpu(proceso->fd_cpu, MSG_OK); // CP3: envío atómico
  } else {
      // ocupado, bloquear el proceso
      log_info(logger_ks, "## (%d) MUTEX_LOCK: bloqueado esperando mutex '%s'", proceso->id_proceso, nombre);
      list_add(m->cola_bloqueados, proceso);

      // herencia de prioridades - si el proceso que se bloquea es más
      // prioritario (numero menor) que el tomador actual, el tomador hereda
      // esa prioridad para liberar el mutex cuanto antes.
      Proceso* tomador = buscar_proceso_por_pid_sin_lock(m->pid_tomador);
      // DEBUG: puntero del tomador (NULL si no esta en ninguna lista)
      log_debug(logger_ks, "[DBG][mutex_lock] pid=%d - tomador pid=%d ptr=%p", proceso->id_proceso, m->pid_tomador, (void*)tomador);
      if (tomador != NULL && proceso->prioridad < tomador->prioridad) {
          cambiar_prioridad(tomador, proceso->prioridad);
      }

      pthread_mutex_unlock(&mutex_listas);

      // FIX: destrabar la CPU. syscall_mutex_lock se quedaba colgado para siempre
      // en el recv porque en este camino no se le contestaba nada. Con MSG_BLOQUEADO
      // la CPU avanza el PC, guarda contexto en KM y corta el ciclo de instruccion.
      enviar_ok_cpu(proceso->fd_cpu, MSG_BLOQUEADO);

      actualizarEstadoProceso(proceso, BLOCK);
      // DEBUG: frontera de funcion
      log_debug(logger_ks, "[DBG][mutex_lock] SALIDA (bloqueado) - nombre='%s'", nombre);
      return 1; // el llamador tiene que hacer liberar_cpu()
  }
  // DEBUG: frontera de funcion
  log_debug(logger_ks, "[DBG][mutex_lock] SALIDA - nombre='%s'", nombre);
  return 0;
}

void mutex_unlock(char* nombre, Proceso* proceso) {
  // DEBUG: frontera de funcion - PUNTERO CRUDO antes de desreferenciar
  log_debug(logger_ks, "[DBG][mutex_unlock] ENTRADA - nombre='%s', proceso=%p", nombre ? nombre : "NULL", (void*)proceso);
  pthread_mutex_lock(&mutex_listas);
  t_mutex_ks* m = buscar_mutex(nombre);
  if (m == NULL) {
      log_error(logger_ks, "## MUTEX_UNLOCK: mutex '%s' no existe", nombre);
      pthread_mutex_unlock(&mutex_listas);
      return;
  }
  if (m->pid_tomador != proceso->id_proceso) {
      log_error(logger_ks, "## (%d) MUTEX_UNLOCK: no es dueño de '%s'", proceso->id_proceso, nombre);
      pthread_mutex_unlock(&mutex_listas);
      return;
  }

  log_info(logger_ks, "## (%d) Libera el Mutex %s", proceso->id_proceso, nombre);

  // restaurar la prioridad original del que libera (por si habia heredado).
  if (proceso->prioridad != proceso->prioridad_original) {
      cambiar_prioridad(proceso, proceso->prioridad_original);
  }

  if (list_is_empty(m->cola_bloqueados)) {
      m->pid_tomador = -1;
      pthread_mutex_unlock(&mutex_listas);
  } else {
      // desbloquear el primero de la cola
      Proceso* siguiente = list_remove(m->cola_bloqueados, 0);
      // DEBUG: extracción de cola de bloqueados del mutex
      log_debug(logger_ks, "[DBG][mutex_unlock] '%s' - desbloquea siguiente pid=%d ptr=%p (bloqueados ahora=%d)", nombre, siguiente->id_proceso, (void*)siguiente, list_size(m->cola_bloqueados));
      m->pid_tomador = siguiente->id_proceso;
      pthread_mutex_unlock(&mutex_listas);
      
      log_info(logger_ks, "## (%d) Toma el Mutex %s", siguiente->id_proceso, nombre);
      // FIX: el desbloqueado solo pasa a READY. NO se le manda nada por fd_cpu:
      // ese fd es el de la CPU donde ejecutaba ANTES de bloquearse y hoy puede
      // estar corriendo otro proceso (mensaje espurio / framing corrupto).
      // Su PC ya quedo apuntando a la instruccion siguiente al MUTEX_LOCK, asi
      // que cuando se lo re-despache arranca ya siendo dueño del mutex.
      //
      // OJO: actualizarEstadoProceso(..., READY) dispara desalojar_por_prioridad,
      // que le manda MSG_INTERRUPT a la CPU de la victima. Si la victima es el
      // proceso que acaba de hacer el UNLOCK, esa CPU todavia esta esperando el
      // MSG_OK de la syscall -> por eso el handler responde ANTES de llamar aca.
      actualizarEstadoProceso(siguiente, READY);
      sem_post(&sem_hay_proceso_ready);
  }
  // DEBUG: frontera de funcion
  log_debug(logger_ks, "[DBG][mutex_unlock] SALIDA - nombre='%s'", nombre);
}

// ============================================================
// CP3: mutex por conexión de CPU — serializa todo envío a un fd_cpu
// ============================================================

// Busca el mutex asociado a una conexión de CPU ya registrada.
// Devuelve NULL si no está registrada (no debería pasar si se llamó
// registrar_conexion_cpu al conectarse).
pthread_mutex_t* obtener_mutex_cpu(int fd_cpu) {
    pthread_mutex_lock(&mutex_conexiones_cpu);
    for (int i = 0; i < list_size(listaConexionesCPU); i++) {
        t_conexion_cpu* con = list_get(listaConexionesCPU, i);
        if (con->fd_cpu == fd_cpu) {
            pthread_mutex_unlock(&mutex_conexiones_cpu);
            return &con->mutex;
        }
    }
    pthread_mutex_unlock(&mutex_conexiones_cpu);
    return NULL;
}

void registrar_conexion_cpu(int fd_cpu) {
    t_conexion_cpu* con = malloc(sizeof(t_conexion_cpu));
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][registrar_conexion_cpu] malloc con=%p (fd=%d)", (void*)con, fd_cpu);
    con->fd_cpu = fd_cpu;
    pthread_mutex_init(&con->mutex, NULL);
    pthread_mutex_lock(&mutex_conexiones_cpu);
    list_add(listaConexionesCPU, con);
    pthread_mutex_unlock(&mutex_conexiones_cpu);
}

void liberar_conexion_cpu(int fd_cpu) {
    pthread_mutex_lock(&mutex_conexiones_cpu);
    for (int i = 0; i < list_size(listaConexionesCPU); i++) {
        t_conexion_cpu* con = list_get(listaConexionesCPU, i);
        if (con->fd_cpu == fd_cpu) {
            list_remove(listaConexionesCPU, i);
            pthread_mutex_destroy(&con->mutex);
            // DEBUG: heap
            log_debug(logger_ks, "[DBG][liberar_conexion_cpu] free con=%p (fd=%d)", (void*)con, fd_cpu);
            free(con);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_conexiones_cpu);
}

// Envío simple (un solo op_code, ej. MSG_OK/MSG_ERROR) protegido por el mutex
// de esa conexión de CPU. Si la conexión no está registrada, manda igual (sin
// lock) para no romper el flujo, pero eso no debería pasar.
void enviar_ok_cpu(int fd_cpu, op_code codigo) {
    pthread_mutex_t* m = obtener_mutex_cpu(fd_cpu);
    if (m) pthread_mutex_lock(m);
    enviar_mensaje(fd_cpu, &codigo, sizeof(op_code));
    if (m) pthread_mutex_unlock(m);
}

// Envío de interrupción (opcode + t_interrupcion) atómico respecto a
// cualquier otro mensaje que se esté por mandar a la misma CPU.
void enviar_interrupcion_cpu(int fd_cpu, uint32_t pid, int motivo) {
    pthread_mutex_t* m = obtener_mutex_cpu(fd_cpu);
    if (m) pthread_mutex_lock(m);
    op_code interrupcion = MSG_INTERRUPT;
    enviar_mensaje(fd_cpu, &interrupcion, sizeof(op_code));
    t_interrupcion intr;
    intr.pid = pid;
    intr.motivo = motivo;
    enviar_mensaje(fd_cpu, &intr, sizeof(t_interrupcion));
    if (m) pthread_mutex_unlock(m);
}

// CP3: marca la CPU como libre para que el corto plazo despache otro proceso.
// Se usa cuando un proceso se bloquea por IO (SLEEP/STDIN/STDOUT): la CPU no
// espera a que termine la IO, queda disponible de inmediato.
void liberar_cpu(int fd_cpu) {
    pthread_mutex_lock(&mutex_listas);
    int* p = malloc(sizeof(int));
    *p = fd_cpu;
    list_add(listaCPUsLibres, p);
    pthread_mutex_unlock(&mutex_listas);
    sem_post(&sem_hay_cpu_libre);
}

void atender_cpu_ks(int fd_cpu) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][atender_cpu_ks] ENTRADA - fd_cpu=%d", fd_cpu);
    registrar_conexion_cpu(fd_cpu); // CP3: habilita envíos serializados a esta CPU
    // CPU recién conectada, entonces esta libre
    pthread_mutex_lock(&mutex_listas);
    int* fd_libre = malloc(sizeof(int));
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_cpu_ks] malloc fd_libre=%p (fd=%d) -> listaCPUsLibres", (void*)fd_libre, fd_cpu);
    *fd_libre = fd_cpu;
    list_add(listaCPUsLibres, fd_libre);
    pthread_mutex_unlock(&mutex_listas);
    sem_post(&sem_hay_cpu_libre);
    
    while (1) {
        int size;
        op_code* codigo = recibir_mensaje(fd_cpu, &size);
        if (codigo == NULL) {
            log_warning(logger_ks, "CPU FD:%d desconectada", fd_cpu);
            liberar_conexion_cpu(fd_cpu); // CP3
            break;
        }
        // DEBUG: deserializacion - opcode recibido de la CPU
        log_debug(logger_ks, "[DBG][atender_cpu_ks] fd=%d - opcode recibido: %d (ptr=%p, size=%d)", fd_cpu, (int)*codigo, (void*)codigo, size);

        switch (*codigo) {
            case MSG_DONE: {
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);

                if (proceso) {
                    finalizar_proceso(proceso, "EXIT");
                }

                liberar_cpu(fd_cpu); 
                break;
            }

            case MSG_SEG_FAULT: {
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);

                if (proceso) {
                    finalizar_proceso(proceso, "SEG_FAULT");
                }

                // Le confirmamos a la CPU que el SEG_FAULT fue procesado
                enviar_ok_cpu(fd_cpu, MSG_OK);

                // Recién ahora liberamos la CPU para el planificador
                liberar_cpu(fd_cpu);
                break;
            }
            case MSG_INTERRUPCION_ATENDIDA: {
                // CP2/CP3: la CPU atendió una interrupción. motivo: 0=fin de quantum,
                // 1=cola más prioritaria, 2=desalojo por compactación (CP3).
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                if (pid_ptr == NULL || size != sizeof(uint32_t)) {
                    log_error(
                        logger_ks,
                        "Interrupcion atendida: PID invalido. ptr=%p size=%d",
                        (void*)pid_ptr,
                        size
                    );
                    free(pid_ptr);
                    break;
                }

                int* motivo_ptr = recibir_mensaje(fd_cpu, &size);
                if (motivo_ptr == NULL || size != sizeof(int)) {
                    log_error(
                        logger_ks,
                        "Interrupcion atendida: motivo invalido. ptr=%p size=%d",
                        (void*)motivo_ptr,
                        size
                    );

                    free(pid_ptr);
                    free(motivo_ptr);
                    break;
                }

                // DEBUG: deserializacion - CUALQUIERA de los dos en NULL segfaultea abajo
                log_debug(logger_ks, "[DBG][atender_cpu_ks:INT_ATENDIDA] fd=%d - pid_ptr=%p (pid=%d), motivo_ptr=%p (motivo=%d)", fd_cpu, (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)motivo_ptr, motivo_ptr ? *motivo_ptr : -1);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                if (proceso == NULL) {
                    log_warning(
                        logger_ks,
                        "Se recibio interrupcion atendida para PID "
                        "pero el proceso ya no existe o no fue encontrado"
                    );

                    /*
                    * No volver a liberar la CPU sin comprobar si ya fue liberada.
                    */
                    break;
                }
                // DEBUG: SI proceso=(nil) ACA, EL actualizarEstadoProceso/proceso->id_proceso
                // DE MAS ABAJO ES EL SEGFAULT (interrupcion atendida de un proceso que ya
                // termino por MSG_DONE - carrera clasica del timer RR con el fin del proceso)
                log_debug(logger_ks, "[DBG][atender_cpu_ks:INT_ATENDIDA] pid=%d - buscar_proceso -> ptr=%p", *pid_ptr, (void*)proceso);
                int motivo = *motivo_ptr;
                free(pid_ptr);
                free(motivo_ptr);

                // en todos los casos la CPU vuelve a estar libre
                pthread_mutex_lock(&mutex_listas);
                int* fd_libre3 = malloc(sizeof(int));
                // DEBUG: heap
                log_debug(logger_ks, "[DBG][atender_cpu_ks:INT_ATENDIDA] malloc fd_libre3=%p (fd=%d)", (void*)fd_libre3, fd_cpu);
                *fd_libre3 = fd_cpu;
                list_add(listaCPUsLibres, fd_libre3);
                pthread_mutex_unlock(&mutex_listas);
                sem_post(&sem_hay_cpu_libre);

                if (motivo == 2) {
                    // CP3: desalojo por compactación -> el proceso va al FRENTE de READY
                    pthread_mutex_lock(&mutex_listas);
                    for (int i = 0; i < list_size(listaProcesosExec); i++) {
                        Proceso* pp = list_get(listaProcesosExec, i);
                        if (pp->id_proceso == proceso->id_proceso) {
                            list_remove(listaProcesosExec, i);
                            break;
                        }
                    }
                    proceso->estado = READY;
                    proceso->fd_cpu = -1;
                    // DEBUG: insercion al frente de READY/cola CMN
                    log_debug(logger_ks, "[DBG][atender_cpu_ks:INT_ATENDIDA] pid=%d ptr=%p - insertando al FRENTE de READY (compactacion)", proceso->id_proceso, (void*)proceso);
                    if (strcmp(config_get_string_value(config, "PLANIFICATION_ALGORITHM"), "CMN") == 0)
                        list_add_in_index(colasMultinivel[proceso->prioridad], 0, proceso);
                    else
                        list_add_in_index(listaProcesosReady, 0, proceso);
                    log_info(logger_ks, "## (%d) Pasa del estado EXEC al estado READY", proceso->id_proceso);
                    pthread_mutex_unlock(&mutex_listas);
                    sem_post(&sem_hay_proceso_ready);
                    sem_post(&sem_desalojo_confirmado); // avisa a manejar_solicitud_desalojo
                } else {
                    if (motivo == 1)
                        log_info(logger_ks, "## (%d) - Desalojado por cola más prioritaria", proceso->id_proceso);
                    else
                        log_info(logger_ks, "## (%d) - Desalojado por fin de quantum", proceso->id_proceso);
                    actualizarEstadoProceso(proceso, READY);
                    sem_post(&sem_hay_proceso_ready);
                }
                break;
            }
            case MSG_MUTEX_CREATE: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_CREATE] nombre=%p ('%s'), pid_ptr=%p (pid=%d)", (void*)nombre, nombre ? nombre : "NULL", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_CREATE", *pid_ptr);
                mutex_create(nombre);
                // DEBUG: serializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_CREATE] envío MSG_OK a fd=%d", fd_cpu);
                enviar_ok_cpu(fd_cpu, MSG_OK); // CP3: envío atómico
                free(nombre);
                free(pid_ptr);
                break;
            }
            case MSG_MUTEX_LOCK: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_LOCK] nombre=%p ('%s'), pid_ptr=%p (pid=%d)", (void*)nombre, nombre ? nombre : "NULL", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_LOCK", *pid_ptr);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                // DEBUG: si proceso=(nil), mutex_lock lo desreferencia y segfaultea
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_LOCK] pid=%d - buscar_proceso -> ptr=%p", *pid_ptr, (void*)proceso);
                free(pid_ptr);
                // FIX: mutex_lock siempre le contesta algo a la CPU. Si devolvio 1
                // el proceso quedo BLOCK, entonces esta CPU queda libre (mismo
                // patron que MSG_SLEEP).
                int bloqueo_por_mutex = mutex_lock(nombre, proceso);
                free(nombre);
                if (bloqueo_por_mutex) {
                    liberar_cpu(fd_cpu); // FIX: sin esto el planificador se cuelga
                }
                break;
            }
            case MSG_MUTEX_UNLOCK: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_UNLOCK] nombre=%p ('%s'), pid_ptr=%p (pid=%d)", (void*)nombre, nombre ? nombre : "NULL", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_UNLOCK", *pid_ptr);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                // DEBUG: si proceso=(nil), mutex_unlock lo desreferencia y segfaultea
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_UNLOCK] pid=%d - buscar_proceso -> ptr=%p", *pid_ptr, (void*)proceso);
                // FIX: contestar el MSG_OK ANTES de mutex_unlock. Adentro,
                // actualizarEstadoProceso(siguiente, READY) -> procesoAReady ->
                // desalojar_por_prioridad puede mandarle un MSG_INTERRUPT a ESTA
                // misma CPU (el que libera el mutex revierte su prioridad heredada
                // y pasa a ser la victima del que acaba de despertar). La CPU esta
                // clavada en el recv de syscall_mutex_unlock esperando el MSG_OK,
                // leia el 16 del MSG_INTERRUPT como respuesta y hacia exit(1).
                // DEBUG: serializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MUTEX_UNLOCK] envío MSG_OK a fd=%d", fd_cpu);
                enviar_ok_cpu(fd_cpu, MSG_OK); // CP3: envío atómico

                mutex_unlock(nombre, proceso);
                free(nombre);
                free(pid_ptr);
                break;
            }
            case MSG_SLEEP: {
                // CPU pide SLEEP. No respondemos nada por este socket todavia -
                // atender_sleep_ks se encarga de hablar con la IO y recien al final
                // manda la respuesta a fd_cpu para destrabarla.
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                int* tiempo_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:SLEEP] pid_ptr=%p (pid=%d), tiempo_ptr=%p (tiempo=%d)", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)tiempo_ptr, tiempo_ptr ? *tiempo_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: SLEEP", *pid_ptr);

                t_args_sleep* args_sleep = malloc(sizeof(t_args_sleep));
                // DEBUG: heap
                log_debug(logger_ks, "[DBG][atender_cpu_ks:SLEEP] malloc t_args_sleep=%p", (void*)args_sleep);
                args_sleep->fd_cpu = fd_cpu;
                args_sleep->pid = *pid_ptr;
                args_sleep->tiempo = *tiempo_ptr;
                free(pid_ptr);
                free(tiempo_ptr);

                pthread_t thread_sleep;
                pthread_create(&thread_sleep, NULL, atender_sleep_ks, args_sleep);
                pthread_detach(thread_sleep);
                liberar_cpu(fd_cpu); // CP3: la CPU queda libre para otro proceso
                break;
            }
            case MSG_MEM_ALLOC: {
                // CP3: opcode + pid + id_segmento + tamanio (tres uint32_t)
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* id_ptr  = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MEM_ALLOC] pid_ptr=%p (pid=%d), id_ptr=%p (id=%d), tam_ptr=%p (tam=%d)", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)id_ptr, id_ptr ? (int)*id_ptr : -1, (void*)tam_ptr, tam_ptr ? (int)*tam_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MEM_ALLOC", *pid_ptr);

                bool ok = km_mem_alloc(*pid_ptr, *id_ptr, *tam_ptr);
                // DEBUG: serializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MEM_ALLOC] pid=%d - envío %s a fd=%d", *pid_ptr, ok ? "MSG_OK" : "MSG_ERROR", fd_cpu);
                enviar_ok_cpu(fd_cpu, ok ? MSG_OK : MSG_ERROR); // CP3: envío atómico
                free(pid_ptr); free(id_ptr); free(tam_ptr);
                break;
            }
            case MSG_MEM_FREE: {
                // CP3: opcode + pid + id_segmento
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* id_ptr  = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MEM_FREE] pid_ptr=%p (pid=%d), id_ptr=%p (id=%d)", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)id_ptr, id_ptr ? (int)*id_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MEM_FREE", *pid_ptr);

                bool ok = km_mem_free(*pid_ptr, *id_ptr);
                // DEBUG: serializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:MEM_FREE] pid=%d - envío %s a fd=%d", *pid_ptr, ok ? "MSG_OK" : "MSG_ERROR", fd_cpu);
                enviar_ok_cpu(fd_cpu, ok ? MSG_OK : MSG_ERROR); // CP3: envío atómico
                // se liberó memoria -> intentar des-suspender procesos (mediano plazo)
                if (ok) intentar_desuspender_procesos();
                free(pid_ptr); free(id_ptr);
                break;
            }
            case MSG_INIT_PROC: {
                // CP3: opcode + pid(padre) + prioridad(int) + path(string)
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                int* prio_ptr = recibir_mensaje(fd_cpu, &size);
                char* path = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:INIT_PROC] pid_ptr=%p (pid=%d), prio_ptr=%p (prio=%d), path=%p ('%s')", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)prio_ptr, prio_ptr ? *prio_ptr : -1, (void*)path, path ? path : "NULL");
                log_info(logger_ks, "## (%d) - Solicitó syscall: INIT_PROC", *pid_ptr);

                crear_proceso(path, *prio_ptr);
                // DEBUG: serializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:INIT_PROC] envío MSG_OK a fd=%d", fd_cpu);
                enviar_ok_cpu(fd_cpu, MSG_OK); // CP3: envío atómico
                free(pid_ptr); free(prio_ptr); free(path);
                break;
            }
            case MSG_STDIN: {
                // CP3: opcode + pid + dir_logica + tamanio
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* dir_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:STDIN] pid_ptr=%p (pid=%d), dir_ptr=%p (dir=%d), tam_ptr=%p (tam=%d)", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)dir_ptr, dir_ptr ? (int)*dir_ptr : -1, (void*)tam_ptr, tam_ptr ? (int)*tam_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: STDIN", *pid_ptr);

                t_args_io_mem* a = malloc(sizeof(t_args_io_mem));
                // DEBUG: heap
                log_debug(logger_ks, "[DBG][atender_cpu_ks:STDIN] malloc t_args_io_mem=%p", (void*)a);
                a->fd_cpu = fd_cpu; a->pid = *pid_ptr;
                a->dir_logica = *dir_ptr; a->tamanio = *tam_ptr;
                free(pid_ptr); free(dir_ptr); free(tam_ptr);

                pthread_t t; pthread_create(&t, NULL, atender_stdin_ks, a); pthread_detach(t);
                liberar_cpu(fd_cpu); // CP3
                break;
            }
            case MSG_STDOUT: {
                // CP3: opcode + pid + dir_logica + tamanio
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* dir_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                // DEBUG: deserializacion
                log_debug(logger_ks, "[DBG][atender_cpu_ks:STDOUT] pid_ptr=%p (pid=%d), dir_ptr=%p (dir=%d), tam_ptr=%p (tam=%d)", (void*)pid_ptr, pid_ptr ? (int)*pid_ptr : -1, (void*)dir_ptr, dir_ptr ? (int)*dir_ptr : -1, (void*)tam_ptr, tam_ptr ? (int)*tam_ptr : -1);
                log_info(logger_ks, "## (%d) - Solicitó syscall: STDOUT", *pid_ptr);

                t_args_io_mem* a = malloc(sizeof(t_args_io_mem));
                // DEBUG: heap
                log_debug(logger_ks, "[DBG][atender_cpu_ks:STDOUT] malloc t_args_io_mem=%p", (void*)a);
                a->fd_cpu = fd_cpu; a->pid = *pid_ptr;
                a->dir_logica = *dir_ptr; a->tamanio = *tam_ptr;
                free(pid_ptr); free(dir_ptr); free(tam_ptr);

                pthread_t t; pthread_create(&t, NULL, atender_stdout_ks, a); pthread_detach(t);
                liberar_cpu(fd_cpu); // CP3
                break;
            }
            default:
                log_warning(logger_ks, "Syscall desconocida: %d", *codigo);
                break;
        }
        // DEBUG: heap
        log_debug(logger_ks, "[DBG][atender_cpu_ks] fd=%d - fin de iteracion, free(codigo=%p)", fd_cpu, (void*)codigo);
        free(codigo);
    }
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][atender_cpu_ks] SALIDA - fd_cpu=%d (socket cerrado)", fd_cpu);
}

// Version sin lock: debe llamarse con mutex_listas YA tomado.
Proceso* buscar_proceso_por_pid_sin_lock(uint32_t pid) {
    t_list* listas[] = {
        listaProcesosNew, listaProcesosReady, listaProcesosExec,
        listaProcesosBlock, listaProcesosSuspBlock, listaProcesosSuspReady
    };
    Proceso* resultado = NULL;
    for (int l = 0; l < 6 && resultado == NULL; l++) {
        for (int i = 0; i < list_size(listas[l]) && resultado == NULL; i++) {
            Proceso* p = list_get(listas[l], i);
            if (p->id_proceso == (int)pid) {
                resultado = p;
            }
        }
    }
    // con CMN los procesos READY viven en colasMultinivel.
    for (int c = 0; c < cantidadColas && resultado == NULL; c++) {
        for (int i = 0; i < list_size(colasMultinivel[c]) && resultado == NULL; i++) {
            Proceso* p = list_get(colasMultinivel[c], i);
            if (p->id_proceso == (int)pid) {
                resultado = p;
            }
        }
    }
    return resultado;
}

Proceso* buscar_proceso_por_pid(uint32_t pid) {
    pthread_mutex_lock(&mutex_listas);
    Proceso* resultado = buscar_proceso_por_pid_sin_lock(pid);
    pthread_mutex_unlock(&mutex_listas);
    // DEBUG: frontera de funcion (valor devuelto fundamental - NULL si no esta)
    log_debug(logger_ks, "[DBG][buscar_proceso_por_pid] pid=%d -> ptr=%p", pid, (void*)resultado);
    return resultado;
}

// CP3: IO ------------------------------------------------------------

// Devuelve el semáforo correspondiente al tipo de IO dado.
sem_t* semaforo_io_por_tipo(char* tipo) {
    if (strcmp(tipo, "SLEEP") == 0) return &sem_hay_sleep_libre;
    if (strcmp(tipo, "STDIN") == 0) return &sem_hay_stdin_libre;
    if (strcmp(tipo, "STDOUT") == 0) return &sem_hay_stdout_libre;
    return NULL; // tipo desconocido — no debería pasar si IO manda un tipo válido
}

// Recibe el tipo de IO (segundo mensaje del handshake) y la registra como libre.
// Se llama una sola vez, justo después del handshake MSG_HANDSHAKE_IO.
void identificar_io_ks(int fd_io) {
    int size;
    char* tipo = recibir_mensaje(fd_io, &size);
    // DEBUG: deserializacion - si tipo==NULL, el strdup/strcmp de abajo SEGFAULTEA
    log_debug(logger_ks, "[DBG][identificar_io_ks] fd=%d - tipo=%p ('%s'), size=%d", fd_io, (void*)tipo, tipo ? tipo : "NULL", size);

    t_io_ks* io = malloc(sizeof(t_io_ks));
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][identificar_io_ks] malloc t_io_ks=%p (fd=%d)", (void*)io, fd_io);
    io->fd = fd_io;
    io->tipo = strdup(tipo);

    pthread_mutex_lock(&mutex_listas);
    list_add(listaIOsLibres, io);
    pthread_mutex_unlock(&mutex_listas);

    log_info(logger_ks, "## IO %s conectada - FD: %d", io->tipo, fd_io);

    sem_t* sem = semaforo_io_por_tipo(tipo);
    if (sem != NULL) sem_post(sem);
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][identificar_io_ks] free(tipo=%p) y SALIDA", (void*)tipo);
    free(tipo);
}

// Busca la primera IO libre del tipo pedido y la remueve de listaIOsLibres.
// Devuelve NULL si no hay ninguna libre de ese tipo (no debería pasar si se
// llama justo después de un sem_wait del semáforo de ese mismo tipo).
t_io_ks* sacar_io_libre_por_tipo(char* tipo) {
    pthread_mutex_lock(&mutex_listas);
    t_io_ks* encontrada = NULL;
    for (int i = 0; i < list_size(listaIOsLibres); i++) {
        t_io_ks* io = list_get(listaIOsLibres, i);
        if (strcmp(io->tipo, tipo) == 0) {
            encontrada = list_remove(listaIOsLibres, i);
            break;
        }
    }
    pthread_mutex_unlock(&mutex_listas);
    // DEBUG: frontera de funcion (valor devuelto - NULL si no habia IO de ese tipo,
    // los llamadores hacen io->fd sin chequear -> segfault si esto da (nil))
    log_debug(logger_ks, "[DBG][sacar_io_libre_por_tipo] tipo='%s' -> ptr=%p", tipo, (void*)encontrada);
    return encontrada;
}

// Vuelve a dejar la IO disponible para la próxima syscall que la necesite,
// posteando el semáforo específico de su tipo.
void liberar_io(t_io_ks* io) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][liberar_io] ENTRADA - io=%p (fd=%d, tipo='%s')", (void*)io, io ? io->fd : -1, io ? io->tipo : "NULL");
    pthread_mutex_lock(&mutex_listas);
    list_add(listaIOsLibres, io);
    pthread_mutex_unlock(&mutex_listas);

    sem_t* sem = semaforo_io_por_tipo(io->tipo);
    if (sem != NULL) sem_post(sem);
}

// Corre en su propio hilo: tramita un SLEEP de punta a punta.
// 1) pasa el proceso a BLOCK (dispara timer de suspensión automáticamente)
// 2) espera una IO de tipo SLEEP libre
// 3) le manda la orden, espera el MSG_DONE
// 4) libera la IO, pasa el proceso a READY, y recien ahi responde a la CPU
void* atender_sleep_ks(void* arg) {
    t_args_sleep* args = (t_args_sleep*) arg;
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][atender_sleep_ks] ENTRADA - pid=%d, fd_cpu=%d, tiempo=%d, args=%p", args->pid, args->fd_cpu, args->tiempo, (void*)args);

    Proceso* proceso = buscar_proceso_por_pid(args->pid);
    if (proceso == NULL) {
        log_error(logger_ks, "## MSG_SLEEP: proceso %d no encontrado", args->pid);
        free(args);
        return NULL;
    }

    actualizarEstadoProceso(proceso, BLOCK);

    // sem_hay_sleep_libre es exclusivo de IOs tipo SLEEP, así que un solo
    // sem_wait + sacar_io_libre_por_tipo alcanza, sin loop de reintento
    sem_wait(&sem_hay_sleep_libre);
    t_io_ks* io = sacar_io_libre_por_tipo("SLEEP");
    // DEBUG: si io=(nil), el io->fd de abajo SEGFAULTEA
    log_debug(logger_ks, "[DBG][atender_sleep_ks] pid=%d - io obtenida ptr=%p (fd=%d)", args->pid, (void*)io, io ? io->fd : -1);

    op_code cod_sleep = MSG_SLEEP;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][atender_sleep_ks] pid=%d - envío MSG_SLEEP + pid + tiempo a IO fd=%d", args->pid, io->fd);
    enviar_mensaje(io->fd, &cod_sleep, sizeof(op_code));
    enviar_mensaje(io->fd, &args->pid, sizeof(uint32_t));
    enviar_mensaje(io->fd, &args->tiempo, sizeof(int));

    int size;
    op_code* respuesta = recibir_mensaje(io->fd, &size);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][atender_sleep_ks] pid=%d - respuesta IO ptr=%p, size=%d, opcode=%d", args->pid, (void*)respuesta, size, respuesta ? (int)*respuesta : -1);
    // se espera MSG_DONE; si la IO se desconectó (NULL) lo tratamos como error y
    // de todas formas liberamos al proceso para no dejarlo colgado para siempre
    if (respuesta != NULL) free(respuesta);

    liberar_io(io);

    // CP3: el SUSPENSION_TIMEOUT puede haber vencido mientras la IO estaba en
    // curso (SUSP_BLOCK). El helper decide el destino correcto (SUSP_READY o READY).
    finalizar_io_y_desbloquear(proceso);

    // recién ahora destrabamos a la CPU que pidió el sleep
    // DEBUG: serializacion
    // CP3: la CPU ya se liberó al recibir la syscall; NO se le manda MSG_OK.

    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_sleep_ks] SALIDA - free(args=%p)", (void*)args);
    free(args);
    return NULL;
}

// ======================= CP3: helpers nuevos =======================

// Al terminar una IO, desbloquea el proceso contemplando que el SUSPENSION_TIMEOUT
// lo haya movido a SUSP_BLOCK mientras la IO estaba en curso.
void finalizar_io_y_desbloquear(Proceso* proceso) {
    // DEBUG: frontera de funcion - PUNTERO CRUDO antes de desreferenciar
    log_debug(logger_ks, "[DBG][finalizar_io_y_desbloquear] ENTRADA - proceso=%p", (void*)proceso);
    pthread_mutex_lock(&mutex_listas);
    estado_proceso estado_actual = proceso->estado;
    pthread_mutex_unlock(&mutex_listas);

    if (estado_actual == SUSP_BLOCK) {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a SUSP. READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, SUSP_READY);
        // CP3 FIX: el mediano plazo solo corria al liberarse memoria (EXIT) o tras
        // compactar. Si el ultimo proceso vivo entra a SUSP_READY, nadie lo saca nunca.
        // Al entrar a SUSP_READY hay que intentar des-suspender aca mismo.
        intentar_desuspender_procesos();
    } else {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, READY);
        sem_post(&sem_hay_proceso_ready);
    }
}

// CP3 FIX: antes de que KM toque memoria de usuario (STDIN/STDOUT) el proceso TIENE
// que estar en RAM. Si el SUSPENSION_TIMEOUT lo mando a SUSP_BLOCK mientras la IO
// estaba en curso, sus segmentos estan en SWAP y KM no puede traducir la direccion
// logica -> "Traduccion inexistente". Orden correcto: MSG_DESUSPENDER_PROCESO,
// esperar MSG_OK, y recien despues mandar MSG_STDIN / MSG_STDOUT.
// El proceso vuelve a BLOCK (sigue esperando el fin de su IO, no pasa a READY).
// Devuelve true si el proceso quedo (o ya estaba) en memoria.
static bool asegurar_proceso_en_memoria(Proceso* proceso) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][asegurar_proceso_en_memoria] ENTRADA - proceso=%p", (void*)proceso);

    pthread_mutex_lock(&mutex_listas);
    estado_proceso estado_actual = proceso->estado;
    uint32_t pid = (uint32_t) proceso->id_proceso;
    pthread_mutex_unlock(&mutex_listas);

    if (estado_actual != SUSP_BLOCK && estado_actual != SUSP_READY) {
        log_debug(logger_ks, "[DBG][asegurar_proceso_en_memoria] pid=%d ya esta en RAM (estado=%s)", pid, estadosProcesos[estado_actual]);
        return true;
    }

    for (int intento = 1; intento <= REINTENTOS_DESUSPENSION_IO; intento++) {
        if (km_desuspender_proceso(pid)) {
            log_info(logger_ks, "## (%d) des-suspendido para completar su IO", pid);
            actualizarEstadoProceso(proceso, BLOCK); // vuelve a RAM, sigue bloqueado por IO
            return true;
        }
        log_warning(logger_ks, "## (%d) sin espacio en RAM para des-suspender antes de IO (intento %d/%d)", pid, intento, REINTENTOS_DESUSPENSION_IO);
        usleep(ESPERA_DESUSPENSION_IO_US);
    }

    log_error(logger_ks, "## (%d) no se pudo des-suspender: KM sigue sin espacio", pid);
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][asegurar_proceso_en_memoria] SALIDA - pid=%d -> false", pid);
    return false;
}

// Traduce un opcode de syscall a su nombre para el log obligatorio.
const char* nombre_syscall(op_code cod) {
    switch (cod) {
        case MSG_MUTEX_CREATE: return "MUTEX_CREATE";
        case MSG_MUTEX_LOCK:   return "MUTEX_LOCK";
        case MSG_MUTEX_UNLOCK: return "MUTEX_UNLOCK";
        case MSG_MEM_ALLOC:    return "MEM_ALLOC";
        case MSG_MEM_FREE:     return "MEM_FREE";
        case MSG_SLEEP:        return "SLEEP";
        case MSG_STDIN:        return "STDIN";
        case MSG_STDOUT:       return "STDOUT";
        case MSG_INIT_PROC:    return "INIT_PROC";
        case MSG_DONE:         return "EXIT";
        case MSG_SEG_FAULT:    return "SEG_FAULT";
        default:               return "DESCONOCIDA";
    }
}

// ---------------------- comunicación con KM ----------------------

// Avisa a KM que el proceso terminó para que libere su memoria.
void km_notificar_exit(uint32_t pid) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_notificar_exit] ENTRADA - pid=%d", pid);
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_DONE;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_notificar_exit] pid=%d - envío MSG_DONE + pid a KM fd=%d", pid, fd_km);
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][km_notificar_exit] pid=%d - ack KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);
    if (resp == NULL || *resp != MSG_OK)
        log_error(logger_ks, "## KM no confirmó la liberación de memoria del PID %d", pid);
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_notificar_exit] SALIDA - pid=%d", pid);
}

// Finaliza un proceso: avisa a KM, lo pasa a EXIT y loguea el fin obligatorio.
void finalizar_proceso(Proceso* proceso, char* motivo) {
    // DEBUG: frontera de funcion - PUNTERO CRUDO antes de desreferenciar
    log_debug(logger_ks, "[DBG][finalizar_proceso] ENTRADA - proceso=%p, motivo=%s", (void*)proceso, motivo);
    km_notificar_exit(proceso->id_proceso);
    actualizarEstadoProceso(proceso, EXIT);
    log_info(logger_ks, "## (%d) finalizó su ejecución con motivo de %s", proceso->id_proceso, motivo);
}

// Pide a KM crear un segmento. KM puede intercalar un pedido de desalojo por
// compactación (MSG_SOLICITAR_DESALOJO) en el MISMO socket antes de la respuesta
// final; lo manejamos inline. Devuelve true si el segmento se creó OK.
bool km_mem_alloc(uint32_t pid, uint32_t id_segmento, uint32_t tamanio) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_mem_alloc] ENTRADA - pid=%d, id_seg=%d, tam=%d", pid, id_segmento, tamanio);
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_MEM_ALLOC;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_mem_alloc] pid=%d - envío MSG_MEM_ALLOC + pid + id + tam a KM fd=%d", pid, fd_km);
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &id_segmento, sizeof(uint32_t));
    enviar_mensaje(fd_km, &tamanio, sizeof(uint32_t));

    bool ok = false, hubo_compactacion = false;
    while (1) {
        int size;
        op_code* resp = recibir_mensaje(fd_km, &size);
        // DEBUG: deserializacion
        log_debug(logger_ks, "[DBG][km_mem_alloc] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);
        if (resp == NULL) break;
        if (*resp == MSG_SOLICITAR_DESALOJO) {
            free(resp);
            hubo_compactacion = true;
            manejar_solicitud_desalojo(pid); // desaloja CPUs y responde MSG_DESALOJO_REALIZADO
            continue;                        // seguimos esperando el resultado del MEM_ALLOC
        }
        ok = (*resp == MSG_OK);
        free(resp);
        break;
    }

    if (hubo_compactacion) {
        log_info(logger_ks, "## Fin de compactación");
        en_compactacion = false; // el corto plazo vuelve a despachar
    }
    pthread_mutex_unlock(&mutex_km);

    // tras compactar, hubo movimiento de memoria -> puede haber espacio para des-suspender
    if (hubo_compactacion) intentar_desuspender_procesos();
    // DEBUG: frontera de funcion (valor devuelto)
    log_debug(logger_ks, "[DBG][km_mem_alloc] SALIDA - pid=%d -> %s", pid, ok ? "true" : "false");
    return ok;
}

// Pide a KM eliminar un segmento. Devuelve true si se liberó OK.
bool km_mem_free(uint32_t pid, uint32_t id_segmento) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_mem_free] ENTRADA - pid=%d, id_seg=%d", pid, id_segmento);
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_MEM_FREE;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_mem_free] pid=%d - envío MSG_MEM_FREE + pid + id a KM fd=%d", pid, fd_km);
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &id_segmento, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][km_mem_free] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);
    bool ok = (resp != NULL && *resp == MSG_OK);
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    // DEBUG: frontera de funcion (valor devuelto)
    log_debug(logger_ks, "[DBG][km_mem_free] SALIDA - pid=%d -> %s", pid, ok ? "true" : "false");
    return ok;
}

// -------------------------- mediano plazo: suspensión --------------------------

// le pide a KM que mueva TODOS los segmentos del proceso a
// bloques del módulo SWAP y libere el espacio que ocupaban en los Memory Sticks.
bool km_suspender_proceso(uint32_t pid) {

    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_suspender_proceso] ENTRADA - pid=%d", pid);

    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_SUSPENDER_PROCESO;

    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_suspender_proceso] pid=%d - envío MSG_SUSPENDER_PROCESO + pid a KM fd=%d", pid, fd_km);

    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);

    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][km_suspender_proceso] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);

    bool ok = (resp != NULL && *resp == MSG_OK);
    free(resp);
    pthread_mutex_unlock(&mutex_km);

    // DEBUG: frontera de funcion (valor devuelto)
    log_debug(logger_ks, "[DBG][km_suspender_proceso] SALIDA - pid=%d -> %s", pid, ok ? "true" : "false");

    return ok;
}

// mediano plazo) le pide a KM que vuelva a reservar espacio para todos los
// segmentos del proceso y los traiga de SWAP. KM debe responder MSG_ERROR si no
// entran TODOS sin disparar una compactación (así lo pide la consigna).
bool km_desuspender_proceso(uint32_t pid) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][km_desuspender_proceso] ENTRADA - pid=%d", pid);

    pthread_mutex_lock(&mutex_km);

    op_code cod = MSG_DESUSPENDER_PROCESO;

    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][km_desuspender_proceso] pid=%d - envío MSG_DESUSPENDER_PROCESO + pid a KM fd=%d", pid, fd_km);

    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);

    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][km_desuspender_proceso] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", pid, (void*)resp, size, resp ? (int)*resp : -1);

    bool ok = (resp != NULL && *resp == MSG_OK);
    free(resp);

    pthread_mutex_unlock(&mutex_km);

    // DEBUG: frontera de funcion (valor devuelto)
    log_debug(logger_ks, "[DBG][km_desuspender_proceso] SALIDA - pid=%d -> %s", pid, ok ? "true" : "false");
    
    return ok;
}

// ---------------------- desalojo por compactación ----------------------

// KM pidió compactar. Desalojamos todas las CPUs que estén ejecutando (menos la
// que disparó el MEM_ALLOC, que está bloqueada en su propia syscall), esperamos
// que confirmen y le avisamos a KM que puede compactar. Se llama con mutex_km
// tomado (desde km_mem_alloc).
void manejar_solicitud_desalojo(uint32_t pid_issuer) {
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][manejar_solicitud_desalojo] ENTRADA - pid_issuer=%d, EXEC size=%d", pid_issuer, list_size(listaProcesosExec));
    log_info(logger_ks, "## Inicio de compactación");
    en_compactacion = true; // el corto plazo deja de despachar

    pthread_mutex_lock(&mutex_listas);
    int a_esperar = 0;
    for (int i = 0; i < list_size(listaProcesosExec); i++) {
        Proceso* p = list_get(listaProcesosExec, i);
        if (p->fd_cpu < 0) continue;
        if (p->id_proceso == (int) pid_issuer) continue; // su CPU está en la syscall, no la interrumpimos
        // DEBUG: serializacion - interrupcion por compactacion
        log_debug(logger_ks, "[DBG][manejar_solicitud_desalojo] envío MSG_INTERRUPT (motivo=2) a fd_cpu=%d, pid=%d ptr=%p", p->fd_cpu, p->id_proceso, (void*)p);
        // CP3: interrupción atómica contra cualquier otro envío a la misma CPU
        enviar_interrupcion_cpu(p->fd_cpu, p->id_proceso, 2); // 2 = desalojo por compactación
        a_esperar++;
    }
    pthread_mutex_unlock(&mutex_listas);

    // DEBUG: cantidad de confirmaciones a esperar
    log_debug(logger_ks, "[DBG][manejar_solicitud_desalojo] esperando %d confirmaciones de desalojo", a_esperar);

    // esperar la confirmación de cada CPU (MSG_INTERRUPCION_ATENDIDA motivo=2)
    for (int i = 0; i < a_esperar; i++)
        sem_wait(&sem_desalojo_confirmado);

    op_code ok = MSG_DESALOJO_REALIZADO;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][manejar_solicitud_desalojo] envío MSG_DESALOJO_REALIZADO a KM fd=%d y SALIDA", fd_km);
    enviar_mensaje(fd_km, &ok, sizeof(op_code));
    // en_compactacion se apaga en km_mem_alloc al recibir la respuesta final
}

// ---------------------- mediano plazo: des-suspensión ----------------------

// orden de des-suspensión que pide la consigna: primero el más prioritario
// (número de prioridad menor) y, a igual prioridad, el que lleve más tiempo
// suspendido (orden_suspension menor).
static bool desuspension_va_primero(void* a, void* b) {
    Proceso* pa = (Proceso*) a;
    Proceso* pb = (Proceso*) b;
    if (pa->prioridad != pb->prioridad) return pa->prioridad < pb->prioridad;
    return pa->orden_suspension < pb->orden_suspension;
}

// Recorre SUSP_READY por prioridad (menor número = más prioritario) y, a igual
// prioridad, por antigüedad de suspensión. Por cada candidato le pide a KM que lo
// des-suspenda; KM sólo acepta si puede recrear TODOS sus segmentos sin compactar.
// Si KM rechaza, el proceso queda en SUSP_READY y se prueba con el siguiente.
void intentar_desuspender_procesos() {

    //DEBUG: frontera de la funcion
    log_debug(logger_ks, "[DBG][intentar_desuspender] ENTRADA");

    // 1) candidatos. IMPORTANTE: no se puede hablar con KM teniendo
    // mutex_listas tomado. km_mem_alloc toma mutex_km y después mutex_listas
    // (desalojo por compactacion), asi que el orden inverso seria un deadlock.
    pthread_mutex_lock(&mutex_listas);

    t_list* candidatos = list_create();
    for (int i = 0; i < list_size(listaProcesosSuspReady); i++) {
        Proceso* p = list_get(listaProcesosSuspReady, i);
        // en_swap == false => KM todavia no termino de bajarlo a SWAP.
        // timer_suspension reintenta la des-suspension cuando KM le confirma.
        if (p->en_swap) list_add(candidatos, p);
    }

    pthread_mutex_unlock(&mutex_listas);

    list_sort(candidatos, desuspension_va_primero);

    //DEBUG: Cantidad de candidatos
    log_debug(logger_ks, "[DBG][intentar_desuspender] candidatos=%d", list_size(candidatos));

    // 2) Intentar des-suspender uno por uno, en orden.
    for (int i = 0; i < list_size(candidatos); i++) {
        Proceso* p = list_get(candidatos, i);

        if (!km_desuspender_proceso(p->id_proceso)) {
            log_debug(logger_ks, "[DBG][intentar_desuspender] pid=%d - KM lo rechazó (no entra sin compactar), sigue en SUSP_READY", p->id_proceso);
            continue; // probamos con el siguiente candidato
        }

        pthread_mutex_lock(&mutex_listas);

        // pudo haber cambiado de estado mientras hablábamos con KM
        if (p->estado != SUSP_READY) {
            pthread_mutex_unlock(&mutex_listas);
            log_warning(logger_ks, "## (%d) cambió de estado durante la des-suspensión", p->id_proceso);
            continue;
        }

        // DEBUG: candidato elegido
        log_debug(logger_ks, "[DBG][intentar_desuspender] elegido pid=%d ptr=%p (prio=%d) -> READY", p->id_proceso, (void*)p, p->prioridad);

        list_remove_element(listaProcesosSuspReady, p);
        p->estado = READY;
        p->en_swap = false;
        p->orden_suspension = 0; // se vuelve a sellar si se suspende de nuevo
        procesoAReady(p);        // requiere mutex_listas tomado

        pthread_mutex_unlock(&mutex_listas);

        // log obligatorio
        log_info(logger_ks, "## (%d) Pasa del estado SUSP. READY al estado READY", p->id_proceso);
        sem_post(&sem_hay_proceso_ready);
    }

    list_destroy(candidatos); // sólo la lista; los Proceso* siguen vivos

    // DEBUG: frontera de salida de la funcion
    log_debug(logger_ks, "[DBG][intentar_desuspender] SALIDA");
}

// ---------------------- largo plazo: BSOD ----------------------

// Memoria corrupta (desconexión de Memory Stick): finaliza todos los procesos y
// termina el KS. NOTA: KM todavía no envía MSG_MEMORIA_CORRUPTA, así que esto no
// se dispara aún; queda listo para cuando KM lo emita.
void bsod() {
    log_error(logger_ks, "## Se detectó corrupción de memoria - Blue Screen of Death (BSOD)");

    t_list* todas[] = {
        listaProcesosNew, listaProcesosReady, listaProcesosExec,
        listaProcesosBlock, listaProcesosSuspBlock, listaProcesosSuspReady
    };
    pthread_mutex_lock(&mutex_listas);
    for (int l = 0; l < 6; l++)
        for (int i = 0; i < list_size(todas[l]); i++) {
            Proceso* p = list_get(todas[l], i);
            log_info(logger_ks, "## (%d) finalizó su ejecución con motivo de BSOD", p->id_proceso);
        }
    pthread_mutex_unlock(&mutex_listas);

    log_destroy(logger_ks);
    config_destroy(config);
    exit(EXIT_FAILURE);
}

// ---------------------- IO: STDIN / STDOUT ----------------------

// Corre en su propio hilo. Bloquea el proceso, le pide a una IO STDIN que lea del
// usuario, y (pendiente KM) escribe lo leído en la dirección lógica pedida.
void* atender_stdin_ks(void* arg) {
    t_args_io_mem* a = (t_args_io_mem*) arg;
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][atender_stdin_ks] ENTRADA - pid=%d, fd_cpu=%d, dir=%d, tam=%d, args=%p", a->pid, a->fd_cpu, a->dir_logica, a->tamanio, (void*)a);

    Proceso* proceso = buscar_proceso_por_pid(a->pid);
    if (proceso == NULL) {
        log_error(logger_ks, "## STDIN: proceso %d no encontrado", a->pid);
        free(a);
        return NULL;
    }

    actualizarEstadoProceso(proceso, BLOCK);

    sem_wait(&sem_hay_stdin_libre);
    t_io_ks* io = sacar_io_libre_por_tipo("STDIN");
    // DEBUG: si io=(nil), el io->fd de abajo SEGFAULTEA
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - io obtenida ptr=%p (fd=%d)", a->pid, (void*)io, io ? io->fd : -1);

    op_code cod = MSG_STDIN;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - envío MSG_STDIN + pid + cant a IO fd=%d", a->pid, io->fd);
    enviar_mensaje(io->fd, &cod, sizeof(op_code));
    enviar_mensaje(io->fd, &a->pid, sizeof(uint32_t));
    int cant = (int) a->tamanio;
    enviar_mensaje(io->fd, &cant, sizeof(int));

    int size;
    char* texto = recibir_mensaje(io->fd, &size); // la cadena leída (a->tamanio bytes)
    // DEBUG: deserializacion - texto=(nil) si la IO se desconecto; el enviar_mensaje
    // a KM de abajo mandaria un buffer NULL
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - texto leido ptr=%p, size=%d", a->pid, (void*)texto, size);
    liberar_io(io);

    // CP3 FIX: el proceso pudo haberse suspendido mientras esperabamos el teclado.
    // Primero lo traemos de SWAP y despues mandamos MSG_STDIN (orden correcto).
    if (!asegurar_proceso_en_memoria(proceso)) {
        log_error(logger_ks, "## (%d) STDIN: no se pudo traer el proceso a memoria, se descarta la escritura", a->pid);
        // DEBUG: heap
        log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - free(texto=%p) por fallo de des-suspension", a->pid, (void*)texto);
        if (texto != NULL) free(texto);
        finalizar_io_y_desbloquear(proceso);
        free(a);
        return NULL;
    }

    // CP3: pedirle a KM que escriba lo leído en memoria de usuario (a->dir_logica).
    // Serializado con mutex_km porque fd_km es un único socket compartido.
    pthread_mutex_lock(&mutex_km);
    op_code cod_km = MSG_STDIN;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - envío MSG_STDIN + pid + dir + tam + texto a KM fd=%d", a->pid, fd_km);
    enviar_mensaje(fd_km, &cod_km, sizeof(op_code));
    enviar_mensaje(fd_km, &a->pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->dir_logica, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->tamanio, sizeof(uint32_t));
    enviar_mensaje(fd_km, texto, (int) a->tamanio);
    int size_resp;
    op_code* resp = recibir_mensaje(fd_km, &size_resp);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", a->pid, (void*)resp, size_resp, resp ? (int)*resp : -1);
    op_code resultado = (resp != NULL) ? *resp : MSG_ERROR;
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_stdin_ks] pid=%d - free(texto=%p)", a->pid, (void*)texto);
    if (texto != NULL) free(texto);

    if (resultado == MSG_SEG_FAULT) {
        // KM detectó que la escritura se pasa del segmento -> finalizar el proceso
        finalizar_proceso(proceso, "SEG_FAULT");
    } else {
        finalizar_io_y_desbloquear(proceso);
    }

    // destrabamos a la CPU (ver nota del modelo de bloqueo: en SEG_FAULT igual
    // la liberamos para que no quede colgada; el proceso ya fue finalizado)
    // DEBUG: serializacion
    // CP3: la CPU ya se liberó al recibir la syscall; NO se le manda MSG_OK.
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_stdin_ks] SALIDA - free(args=%p)", (void*)a);
    free(a);
    return NULL;
}

// Corre en su propio hilo. Bloquea el proceso, (pendiente KM) lee los bytes de la
// dirección lógica y se los manda a una IO STDOUT para que los imprima.
void* atender_stdout_ks(void* arg) {
    t_args_io_mem* a = (t_args_io_mem*) arg;
    // DEBUG: frontera de funcion
    log_debug(logger_ks, "[DBG][atender_stdout_ks] ENTRADA - pid=%d, fd_cpu=%d, dir=%d, tam=%d, args=%p", a->pid, a->fd_cpu, a->dir_logica, a->tamanio, (void*)a);

    Proceso* proceso = buscar_proceso_por_pid(a->pid);
    if (proceso == NULL) {
        log_error(logger_ks, "## STDOUT: proceso %d no encontrado", a->pid);
        free(a);
        return NULL;
    }

    actualizarEstadoProceso(proceso, BLOCK);

    // CP3 FIX: mismo criterio que STDIN - KM solo puede leer memoria de usuario si el
    // proceso esta en RAM. Normalmente lo esta (recien lo bloqueamos), pero si quedo
    // suspendido lo des-suspendimos antes de mandar MSG_STDOUT.
    if (!asegurar_proceso_en_memoria(proceso)) {
        log_error(logger_ks, "## (%d) STDOUT: no se pudo traer el proceso a memoria, se descarta la lectura", a->pid);
        finalizar_io_y_desbloquear(proceso);
        free(a);
        return NULL;
    }

    // CP3: pedirle a KM los a->tamanio bytes en a->dir_logica (serializado con mutex_km).
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_STDOUT;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - envío MSG_STDOUT + pid + dir + tam a KM fd=%d", a->pid, fd_km);
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &a->pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->dir_logica, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->tamanio, sizeof(uint32_t));
    int size_resp;
    op_code* resp = recibir_mensaje(fd_km, &size_resp);
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - respuesta KM ptr=%p, size=%d, opcode=%d", a->pid, (void*)resp, size_resp, resp ? (int)*resp : -1);
    op_code resultado = (resp != NULL) ? *resp : MSG_ERROR;
    free(resp);

    char* contenido = NULL;
    if (resultado == MSG_OK) {
        int size_datos;
        contenido = recibir_mensaje(fd_km, &size_datos); // a->tamanio bytes
        // DEBUG: deserializacion
        log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - contenido KM ptr=%p, size=%d", a->pid, (void*)contenido, size_datos);
    }
    pthread_mutex_unlock(&mutex_km);

    if (resultado == MSG_SEG_FAULT) {
        finalizar_proceso(proceso, "SEG_FAULT");
        // CP3: la CPU ya se liberó al recibir la syscall; NO se le manda MSG_OK.
        free(a);
        return NULL;
    }

    // aseguramos terminador para que la IO STDOUT lo imprima como cadena
    char* texto = calloc(1, a->tamanio + 1);
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - calloc texto=%p (%d bytes)", a->pid, (void*)texto, a->tamanio + 1);
    if (contenido != NULL) { memcpy(texto, contenido, a->tamanio); free(contenido); }

    sem_wait(&sem_hay_stdout_libre);
    t_io_ks* io = sacar_io_libre_por_tipo("STDOUT");
    // DEBUG: si io=(nil), el io->fd de abajo SEGFAULTEA
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - io obtenida ptr=%p (fd=%d)", a->pid, (void*)io, io ? io->fd : -1);

    op_code cod_io = MSG_STDOUT;
    // DEBUG: serializacion
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - envío MSG_STDOUT + pid + texto a IO fd=%d", a->pid, io->fd);
    enviar_mensaje(io->fd, &cod_io, sizeof(op_code));
    enviar_mensaje(io->fd, &a->pid, sizeof(uint32_t));
    enviar_mensaje(io->fd, texto, strlen(texto) + 1);
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - free(texto=%p)", a->pid, (void*)texto);
    free(texto);

    int size;
    op_code* done = recibir_mensaje(io->fd, &size); // se espera MSG_DONE
    // DEBUG: deserializacion
    log_debug(logger_ks, "[DBG][atender_stdout_ks] pid=%d - done IO ptr=%p, size=%d", a->pid, (void*)done, size);
    if (done != NULL) free(done);
    liberar_io(io);

    finalizar_io_y_desbloquear(proceso);

    // DEBUG: serializacion
    // CP3: la CPU ya se liberó al recibir la syscall; NO se le manda MSG_OK.
    // DEBUG: heap
    log_debug(logger_ks, "[DBG][atender_stdout_ks] SALIDA - free(args=%p)", (void*)a);
    free(a);
    return NULL;
}