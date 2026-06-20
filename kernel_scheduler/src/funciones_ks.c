#include "funciones_ks.h"

#include <commons/log.h>
#include <utils/conexiones.h>  
#include <utils/mensajes.h>    
#include <utils/tipos.h>   


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

// semáforos y mutex nuevos 
sem_t sem_hay_proceso_ready;
sem_t sem_hay_cpu_libre;
sem_t sem_hay_sleep_libre; 
sem_t sem_hay_stdin_libre;  
sem_t sem_hay_stdout_libre; 
pthread_mutex_t mutex_listas;


void inicializarListasProcesos() {
  listaProcesosNew = list_create();
  listaProcesosReady = list_create();
  listaProcesosExec = list_create();
  listaProcesosBlock = list_create();
  listaProcesosSuspBlock = list_create();
  listaProcesosSuspReady = list_create();
  listaProcesosExit = list_create(); // faltaba crear esta linea
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
}

// crea el proceso PID 0 desde argv[2] y lo mete en NEW
void crear_proceso_inicial(char* path) {
    Proceso* p = malloc(sizeof(Proceso));
    p->id_proceso = 0;
    p->estado = NEW;
    p->prioridad = 0;   // máxima prioridad
    p->fd_cpu = -1;  // sin CPU asignada todavía
 
    // log obligatorio
    log_info(logger_ks, "## (%d) Se crea el proceso - Estado: NEW", p->id_proceso);
 
    pthread_mutex_lock(&mutex_listas);
    list_add(listaProcesosNew, p);
    pthread_mutex_unlock(&mutex_listas);
}

void procesoAReady(Proceso* proceso){
  list_add(listaProcesosReady, proceso);
  log_info(logger_ks, "proceso %d agregado a lista READY", proceso->id_proceso);
}

void procesoAExec(Proceso* proceso){
  list_add(listaProcesosExec, proceso);
  log_info(logger_ks, "proceso %d agregado a lista EXEC", proceso->id_proceso);
}

void procesoAExit(Proceso* proceso){
  list_add(listaProcesosExit, proceso);
  log_info(logger_ks, "Se terminó la proceso %d agregado a lista EXIT", proceso->id_proceso);
}

void procesoABlock(Proceso* proceso){
  list_add(listaProcesosBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista BLOCK", proceso->id_proceso);
}

void procesoASuspBlock(Proceso* proceso){
  list_add(listaProcesosSuspBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP BLOCK", proceso->id_proceso);
}

void procesoASuspReady(Proceso* proceso){
  list_add(listaProcesosSuspReady, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP READY", proceso->id_proceso);
}

void* iniciar_planificador_largo_plazo () {
  log_info(logger_ks, "Planificador de Largo Plazo iniciado.");
  while (1) {

    // un solo lock que chequea y remueve. Evita race condition entre dos locks separados
    pthread_mutex_lock(&mutex_listas);
    if (list_is_empty(listaProcesosNew)) {
      pthread_mutex_unlock(&mutex_listas);
      usleep(1000); // evita busy-wait
      continue;     // vuelve al while(1)
    }
    Proceso* nuevoProceso = list_remove(listaProcesosNew, 0); //cambio list_get por list_remove, el proceso sale de NEW
    pthread_mutex_unlock(&mutex_listas);

    actualizarEstadoProceso(nuevoProceso, READY);
    sem_post(&sem_hay_proceso_ready); // avisa al planificador de corto plazo
  }
}

void* timer_rr(void* arg) {
    t_args_rr* args = (t_args_rr*) arg;

    // dormimos el quantum (usleep espera microsegundos, el config está en ms)
    usleep(args->quantum * 1000);

    // enviamos la interrupción
    op_code interrupcion = MSG_INTERRUPT;
    enviar_mensaje(args->fd_cpu, &interrupcion, sizeof(op_code));

    t_interrupcion intr;
    intr.pid    = args->pid;
    intr.motivo = 0; // fin de quantum
    enviar_mensaje(args->fd_cpu, &intr, sizeof(t_interrupcion));

    free(args);
    return NULL;
}

// Timer de suspensión - si al vencer SUSPENSION_TIMEOUT el proceso sigue en BLOCK, pasa a SUSP_BLOCK
void* timer_suspension(void* arg) {
    t_args_suspension* args = (t_args_suspension*) arg;

    usleep(args->timeout * 1000); // SUSPENSION_TIMEOUT está en ms

    Proceso* proceso = buscar_proceso_por_pid(args->pid);

    if (proceso != NULL) {
        pthread_mutex_lock(&mutex_listas);
        estado_proceso estado_actual = proceso->estado;
        pthread_mutex_unlock(&mutex_listas);

        if (estado_actual == BLOCK) {
            actualizarEstadoProceso(proceso, SUSP_BLOCK);
        }
        // si ya no está en BLOCK (se desbloqueó antes de vencer el timeout), no hacemos nada
    }

    free(args);
    return NULL;
}


// Planif. Corto plazo: implementa FIFO y RR
void* iniciar_planificador_corto_plazo() {
  log_info(logger_ks, "Planificador de Corto Plazo iniciado.");
 
  char* algoritmo = config_get_string_value(config, "PLANIFICATION_ALGORITHM");
 
  while (1) {
    // bloquea hasta que haya proceso en READY Y cpu libre
    sem_wait(&sem_hay_proceso_ready);
    sem_wait(&sem_hay_cpu_libre);
 
    pthread_mutex_lock(&mutex_listas);
 
    if (list_is_empty(listaProcesosReady) || list_is_empty(listaCPUsLibres)) {
      pthread_mutex_unlock(&mutex_listas);
      continue;
    }
 
    // FIFO y RR toman el primero de READY
    Proceso* proceso = list_remove(listaProcesosReady, 0);
    int* fd_cpu_ptr = list_remove(listaCPUsLibres, 0);
    int  fd_cpu = *fd_cpu_ptr;
    free(fd_cpu_ptr);
 
    pthread_mutex_unlock(&mutex_listas);
 
    proceso->fd_cpu = fd_cpu;
    actualizarEstadoProceso(proceso, EXEC);
 
    // envia el PID a la CPU para que empiece a ejecutar
    enviar_mensaje(fd_cpu, &proceso->id_proceso, sizeof(uint32_t));
 
    // Round Robin: lanza el timer y atender_cpu_ks se encarga del resto.
    if (strcmp(algoritmo, "RR") == 0) {
        t_args_rr* args = malloc(sizeof(t_args_rr));
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
  pthread_mutex_lock(&mutex_listas); // evita race conditions entre planificadores

  Proceso* procesoEncontrado = NULL;
  int lanzar_timer_suspension = 0; 
  log_debug(logger_ks, "Actualizando estado de Query %d de %s a %s", proceso->id_proceso, estadosProcesos[proceso->estado], estadosProcesos[nuevoEstado]);
  
  estado_proceso estado_anterior = proceso->estado;
    switch(estado_anterior){
        case NEW:
          for(int i = 0; i < list_size(listaProcesosNew); i++) {
              Proceso* procesoTemporal= list_get(listaProcesosNew, i);
              if(procesoTemporal->id_proceso == proceso->id_proceso) {
                  procesoEncontrado = list_remove(listaProcesosNew, i);
                  break;
              }
          }
          break;
        case READY:
          for(int i = 0; i < list_size(listaProcesosReady); i++) {
              Proceso* procesoTemporal= list_get(listaProcesosReady, i);
              if(procesoTemporal->id_proceso == proceso->id_proceso) {
                procesoEncontrado = list_remove(listaProcesosReady, i);
                break;
              }
          }
          break;
        case EXEC:
          for(int i = 0; i < list_size(listaProcesosExec); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosExec, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosExec, i);
              break;
            }
          }
          break;
        case BLOCK:
          for(int i = 0; i < list_size(listaProcesosBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosBlock, i);
              break;
            }
          }
          break;
        case SUSP_BLOCK:
          for(int i = 0; i < list_size(listaProcesosSuspBlock); i++) {
            Proceso* procesoTemporal= list_get(listaProcesosSuspBlock, i);
            if(procesoTemporal->id_proceso == proceso->id_proceso) {
              procesoEncontrado = list_remove(listaProcesosSuspBlock, i);
              break;
            }
          }
          break;
        case SUSP_READY:
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
          pthread_mutex_unlock(&mutex_listas);  // unlock antes de cada return
          return;
    }

    if(procesoEncontrado == NULL) {
      log_error(logger_ks, "Error: no se encontro el proceso en la lista correspondiente a su estado actual");
      pthread_mutex_unlock(&mutex_listas); // hay que unlockear antes de cada return
      return;
    } else {
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
                // arranca el timer de suspensión por timeout
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
                // Devolvemos la query a su lista original para no perderla
                switch(estado_anterior) {
                    case READY: procesoAReady(procesoEncontrado); break;
                    case EXEC: procesoAExec(procesoEncontrado); break;
                    case EXIT: procesoAExit(procesoEncontrado); break;
                    case BLOCK: procesoABlock(procesoEncontrado); break;
                    case SUSP_READY: procesoASuspReady(procesoEncontrado); break;
                    case SUSP_BLOCK: procesoABlock(procesoEncontrado); break;
                }
                pthread_mutex_unlock(&mutex_listas); // unlockear antes del return
                return;
        }
    }
    pthread_mutex_unlock(&mutex_listas); // unlock al salir normalmente

    // recien aca, con mutex_listas ya liberado, se crea el hilo del timer
    if (lanzar_timer_suspension) {
        t_args_suspension* args_susp = malloc(sizeof(t_args_suspension));
        args_susp->pid = proceso->id_proceso;
        args_susp->timeout = config_get_int_value(config, "SUSPENSION_TIMEOUT");

        pthread_t thread_susp;
        pthread_create(&thread_susp, NULL, timer_suspension, args_susp);
        pthread_detach(thread_susp);
    }
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
  m->nombre = strdup(nombre);
  m->pid_tomador = -1;
  m->cola_bloqueados = list_create();

  pthread_mutex_lock(&mutex_listas);
  list_add(listaMutex, m);
  pthread_mutex_unlock(&mutex_listas);

  log_info(logger_ks, "## MUTEX_CREATE: mutex '%s' creado", nombre);
}

void mutex_lock(char* nombre, Proceso* proceso) {
  pthread_mutex_lock(&mutex_listas);
  t_mutex_ks* m = buscar_mutex(nombre);
  if (m == NULL) {
      log_error(logger_ks, "## MUTEX_LOCK: mutex '%s' no existe", nombre);
      pthread_mutex_unlock(&mutex_listas);
      return;
  }

  if (m->pid_tomador == -1) {
      // libre, lo toma
      m->pid_tomador = proceso->id_proceso;
      log_info(logger_ks, "## (%d) Toma el Mutex %s", proceso->id_proceso, nombre);
      pthread_mutex_unlock(&mutex_listas);
      op_code ok = MSG_OK;
      enviar_mensaje(proceso->fd_cpu, &ok, sizeof(op_code));
  } else {
      // ocupado, bloquear el proceso
      log_info(logger_ks, "## (%d) MUTEX_LOCK: bloqueado esperando mutex '%s'", proceso->id_proceso, nombre);
      list_add(m->cola_bloqueados, proceso);
      pthread_mutex_unlock(&mutex_listas);
      actualizarEstadoProceso(proceso, BLOCK);
  }
}

void mutex_unlock(char* nombre, Proceso* proceso) {
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

  if (list_is_empty(m->cola_bloqueados)) {
      m->pid_tomador = -1;
      pthread_mutex_unlock(&mutex_listas);
  } else {
      // desbloquear el primero de la cola
      Proceso* siguiente = list_remove(m->cola_bloqueados, 0);
      m->pid_tomador = siguiente->id_proceso;
      pthread_mutex_unlock(&mutex_listas);
      
      log_info(logger_ks, "## (%d) Toma el Mutex %s", siguiente->id_proceso, nombre);
      actualizarEstadoProceso(siguiente, READY);
      sem_post(&sem_hay_proceso_ready);
      op_code ok = MSG_OK;
      enviar_mensaje(siguiente->fd_cpu, &ok, sizeof(op_code));
  }
}

void atender_cpu_ks(int fd_cpu) {
    // CPU recién conectada, entonces esta libre
    pthread_mutex_lock(&mutex_listas);
    int* fd_libre = malloc(sizeof(int));
    *fd_libre = fd_cpu;
    list_add(listaCPUsLibres, fd_libre);
    pthread_mutex_unlock(&mutex_listas);
    sem_post(&sem_hay_cpu_libre);
    
    while (1) {
        int size;
        op_code* codigo = recibir_mensaje(fd_cpu, &size);
        if (codigo == NULL) {
            log_warning(logger_ks, "CPU FD:%d desconectada", fd_cpu);
            break;
        }

        switch (*codigo) {
            case MSG_DONE: {
                // CP2: proceso terminó normalmente
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);
                if (proceso) actualizarEstadoProceso(proceso, EXIT);

                pthread_mutex_lock(&mutex_listas);
                int* fd_libre2 = malloc(sizeof(int));
                *fd_libre2 = fd_cpu;
                list_add(listaCPUsLibres, fd_libre2);
                pthread_mutex_unlock(&mutex_listas);
                sem_post(&sem_hay_cpu_libre);
                break;
            }
            case MSG_INTERRUPCION_ATENDIDA: {
                // CP2: fin de quantum RR — CPU y proceso vuelven a estar libres
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                int* motivo = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);
                free(motivo);

                log_info(logger_ks, "## (%d) - Desalojado por fin de quantum", proceso->id_proceso);

                pthread_mutex_lock(&mutex_listas);
                int* fd_libre3 = malloc(sizeof(int));
                *fd_libre3 = fd_cpu;
                list_add(listaCPUsLibres, fd_libre3);
                pthread_mutex_unlock(&mutex_listas);
                sem_post(&sem_hay_cpu_libre);

                actualizarEstadoProceso(proceso, READY);
                sem_post(&sem_hay_proceso_ready);
                break;
            }
            case MSG_MUTEX_CREATE: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                mutex_create(nombre);
                op_code ok = MSG_OK;
                enviar_mensaje(fd_cpu, &ok, sizeof(op_code));
                free(nombre);
                free(pid_ptr);
                break;
            }
            case MSG_MUTEX_LOCK: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);
                mutex_lock(nombre, proceso);
                // no respondemos acá — mutex_lock responde cuando corresponde
                free(nombre);
                break;
            }
            case MSG_MUTEX_UNLOCK: {
                char* nombre = recibir_mensaje(fd_cpu, &size);
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                mutex_unlock(nombre, proceso);
                op_code ok = MSG_OK;
                enviar_mensaje(fd_cpu, &ok, sizeof(op_code));
                free(nombre);
                free(pid_ptr);
                break;
            }
            case MSG_SLEEP: {
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                int* tiempo_ptr = recibir_mensaje(fd_cpu, &size);

                t_args_sleep* args_sleep = malloc(sizeof(t_args_sleep));
                args_sleep->fd_cpu = fd_cpu;
                args_sleep->pid = *pid_ptr;
                args_sleep->tiempo = *tiempo_ptr;
                free(pid_ptr);
                free(tiempo_ptr);

                pthread_t thread_sleep;
                pthread_create(&thread_sleep, NULL, atender_sleep_ks, args_sleep);
                pthread_detach(thread_sleep);
                break;
            }
            default:
                log_warning(logger_ks, "Syscall desconocida: %d", *codigo);
                break;
        }
        free(codigo);
    }
}

Proceso* buscar_proceso_por_pid(uint32_t pid) {
    t_list* listas[] = {
        listaProcesosNew, listaProcesosReady, listaProcesosExec,
        listaProcesosBlock, listaProcesosSuspBlock, listaProcesosSuspReady
    };
    pthread_mutex_lock(&mutex_listas);
    Proceso* resultado = NULL;
    for (int l = 0; l < 6 && resultado == NULL; l++) {
        for (int i = 0; i < list_size(listas[l]) && resultado == NULL; i++) {
            Proceso* p = list_get(listas[l], i);
            if (p->id_proceso == (int)pid) {
                resultado = p;
            }
        }
    }
    pthread_mutex_unlock(&mutex_listas);
    return resultado;
}

// CheckPoint3: IO ------------------------------------------------------------

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

    t_io_ks* io = malloc(sizeof(t_io_ks));
    io->fd = fd_io;
    io->tipo = strdup(tipo);

    pthread_mutex_lock(&mutex_listas);
    list_add(listaIOsLibres, io);
    pthread_mutex_unlock(&mutex_listas);

    log_info(logger_ks, "## IO %s conectada - FD: %d", io->tipo, fd_io);

    sem_t* sem = semaforo_io_por_tipo(tipo);
    if (sem != NULL) sem_post(sem);
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
    return encontrada;
}

// Vuelve a dejar la IO disponible para la próxima syscall que la necesite,
// posteando el semáforo específico de su tipo.
void liberar_io(t_io_ks* io) {
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
// 4) libera la IO, pasa el proceso a READY/SUSP_READY, y recién ahí responde a la CPU
void* atender_sleep_ks(void* arg) {
    t_args_sleep* args = (t_args_sleep*) arg;

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

    op_code cod_sleep = MSG_SLEEP;
    enviar_mensaje(io->fd, &cod_sleep, sizeof(op_code));
    enviar_mensaje(io->fd, &args->pid, sizeof(uint32_t));
    enviar_mensaje(io->fd, &args->tiempo, sizeof(int));

    int size;
    op_code* respuesta = recibir_mensaje(io->fd, &size);
    // se espera MSG_DONE; si la IO se desconectó (NULL) lo tratamos como error y
    // de todas formas liberamos al proceso para no dejarlo colgado para siempre
    if (respuesta != NULL) free(respuesta);

    liberar_io(io);

    // CP3: el SUSPENSION_TIMEOUT puede haber vencido mientras la IO estaba en
    // curso, moviendo al proceso a SUSP_BLOCK. Si pasó eso, el destino correcto
    // es SUSP_READY, no READY.
    pthread_mutex_lock(&mutex_listas);
    estado_proceso estado_actual = proceso->estado;
    pthread_mutex_unlock(&mutex_listas);

    if (estado_actual == SUSP_BLOCK) {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a SUSP. READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, SUSP_READY);
        // un proceso en SUSP_READY no tiene CPU asignada todavía — el
        // planificador de mediano plazo es quien decide cuándo des-suspenderlo
    } else {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, READY);
        sem_post(&sem_hay_proceso_ready);
    }

    // recién ahora destrabamos a la CPU que pidió el sleep
    op_code ok = MSG_OK;
    enviar_mensaje(args->fd_cpu, &ok, sizeof(op_code));

    free(args);
    return NULL;
}
