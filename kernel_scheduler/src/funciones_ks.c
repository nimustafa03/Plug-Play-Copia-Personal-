#include "funciones_ks.h"

#include <string.h>            // strcmp, strdup (evita implicit-declaration)
#include <unistd.h>           // usleep
#include <commons/log.h>
#include <utils/conexiones.h>  
#include <utils/mensajes.h>    
#include <utils/tipos.h>   


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

  // CP3
  pthread_mutex_init(&mutex_km, NULL);
  sem_init(&sem_desalojo_confirmado, 0, 0);

  // inicializar colas multinivel a partir de QUEUES_ALGORITHMS.
  // config_get_array_value devuelve un char** terminado en NULL.
  algoritmosColas = config_get_array_value(config, "QUEUES_ALGORITHMS");
  cantidadColas = 0;
  while (algoritmosColas[cantidadColas] != NULL) {
      cantidadColas++;
  }
  colasMultinivel = malloc(sizeof(t_list*) * cantidadColas);
  for (int i = 0; i < cantidadColas; i++) {
      colasMultinivel[i] = list_create();
  }
  log_info(logger_ks, "CMN: %d colas de prioridad inicializadas", cantidadColas);
}

// CP3: genera el próximo PID de forma segura. El primero es 0 (proceso inicial).
uint32_t generar_pid() {
    pthread_mutex_lock(&mutex_listas);
    uint32_t pid = contador_pids++;
    pthread_mutex_unlock(&mutex_listas);
    return pid;
}

// CP3: crea un proceso con la prioridad dada y lo mete en NEW.
// El "path" (archivo de instrucciones) lo administra Kernel Memory al pedir el
// contexto por PID; KS sólo trackea el ciclo de vida del proceso.
// NOTA(coordinación con KM): para que KM asocie este PID nuevo con su archivo de
// instrucciones falta un mensaje KS->KM "crear proceso con path". Hoy no existe.
void crear_proceso(char* path, int prioridad) {
    (void) path;
    Proceso* p = malloc(sizeof(Proceso));
    p->id_proceso = generar_pid();
    p->estado = NEW;
    p->prioridad = prioridad;
    p->prioridad_original = prioridad;
    p->fd_cpu = -1;  // sin CPU asignada todavía
    p->orden_suspension = 0;

    // log obligatorio
    log_info(logger_ks, "## (%d) Se crea el proceso - Estado: NEW", p->id_proceso);

    pthread_mutex_lock(&mutex_listas);
    list_add(listaProcesosNew, p);
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

  // interrumpir la CPU de la victima (mismo patron que timer_rr, motivo 1 = prioridad)
  op_code interrupcion = MSG_INTERRUPT;
  enviar_mensaje(victima->fd_cpu, &interrupcion, sizeof(op_code));

  t_interrupcion intr;
  intr.pid = victima->id_proceso;
  intr.motivo = 1; // 1 = desalojo por prioridad (0 = fin de quantum)
  enviar_mensaje(victima->fd_cpu, &intr, sizeof(t_interrupcion));
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
    list_add(colasMultinivel[proceso->prioridad], proceso);
    log_info(logger_ks, "proceso %d agregado a cola CMN de prioridad %d",
             proceso->id_proceso, proceso->prioridad);
    desalojar_por_prioridad(proceso);
  } else {
    list_add(listaProcesosReady, proceso);
    log_info(logger_ks, "proceso %d agregado a lista READY", proceso->id_proceso);
  }
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
  proceso->orden_suspension = reloj_suspension++; // CP3: antigüedad de suspensión
  list_add(listaProcesosSuspBlock, proceso);
  log_info(logger_ks, "Proceso %d agregado a lista SUSP BLOCK", proceso->id_proceso);
}

void procesoASuspReady(Proceso* proceso){
  // conserva el orden_suspension original si venía de SUSP_BLOCK; si entra directo, lo sella
  if (proceso->orden_suspension == 0) proceso->orden_suspension = reloj_suspension++;
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

// timer de suspensión - si al vencer SUSPENSION_TIMEOUT el proceso sigue en BLOCK, pasa a SUSP_BLOCK
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
                return list_remove(colasMultinivel[i], 0);
            }
        }
        return NULL; // todas las colas vacias
    }

    // FIFO / RR: una sola cola
    if (list_is_empty(listaProcesosReady)) return NULL;
    return list_remove(listaProcesosReady, 0);
}

// Planif. Corto plazo: implementa FIFO, RR y CMN
void* iniciar_planificador_corto_plazo() {
  log_info(logger_ks, "Planificador de Corto Plazo iniciado.");
 
  char* algoritmo = config_get_string_value(config, "PLANIFICATION_ALGORITHM");
 
  while (1) {
    // CP3: durante una compactación no se despacha ningún proceso nuevo
    while (en_compactacion) usleep(1000);

    // bloquea hasta que haya proceso en READY Y cpu libre
    sem_wait(&sem_hay_proceso_ready);
    sem_wait(&sem_hay_cpu_libre);
 
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
    free(fd_cpu_ptr);
 
    pthread_mutex_unlock(&mutex_listas);
 
    proceso->fd_cpu = fd_cpu;
    actualizarEstadoProceso(proceso, EXEC);
 
    // envia el PID a la CPU para que empiece a ejecutar
    enviar_mensaje(fd_cpu, &proceso->id_proceso, sizeof(uint32_t));
 
    // Determina si hay que lanzar timer de quantum:
    // - FIFO/RR plano: segun PLANIFICATION_ALGORITHM
    // - CMN: segun el algoritmo de la cola de la que salio el proceso
    char* algoritmo_efectivo = algoritmo;
    if (strcmp(algoritmo, "CMN") == 0 && nivel >= 0) {
        algoritmo_efectivo = algoritmosColas[nivel];
    }

    if (strcmp(algoritmo_efectivo, "RR") == 0) {
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
  int lanzar_timer_suspension = 0; // se decide adentro del lock, se ejecuta afuera
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
          // con CMN el proceso puede estar en cualquiera de las colas
          // multinivel; con FIFO/RR esta en listaProcesosReady.
          if (strcmp(config_get_string_value(config, "PLANIFICATION_ALGORITHM"), "CMN") == 0) {
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

                // NO lanzamos el hilo aca - todavia estamos con mutex_listas tomado.
                // Solo marcamos la bandera; el pthread_create real pasa
                // a hacerse despues de soltar el lock, al final de la funcion.
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

    // recien aca, con mutex_listas ya liberado, creamos el hilo del timer.
    // Usamos proceso->id_proceso (no procesoEncontrado): el struct sigue vivo
    // en memoria - nada en este código hace free() de un Proceso* - asi que
    // es seguro leerlo fuera del lock.
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

// cambia la prioridad de un proceso con el log obligatorio.
// Debe llamarse con mutex_listas YA tomado.
void cambiar_prioridad(Proceso* proceso, int nueva_prioridad) {
  if (proceso->prioridad == nueva_prioridad) return;
  log_info(logger_ks, "## %d Cambio de prioridad: %d - %d",
           proceso->id_proceso, proceso->prioridad, nueva_prioridad);
  proceso->prioridad = nueva_prioridad;
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

      // herencia de prioridades - si el proceso que se bloquea es más
      // prioritario (numero menor) que el tomador actual, el tomador hereda
      // esa prioridad para liberar el mutex cuanto antes.
      Proceso* tomador = buscar_proceso_por_pid_sin_lock(m->pid_tomador);
      if (tomador != NULL && proceso->prioridad < tomador->prioridad) {
          cambiar_prioridad(tomador, proceso->prioridad);
      }

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
                // CP2/CP3: proceso terminó (fin de instrucciones o syscall EXIT)
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);
                // CP3: finalizar_proceso avisa a KM (libera memoria) + log obligatorio de fin
                if (proceso) finalizar_proceso(proceso, "EXIT");

                pthread_mutex_lock(&mutex_listas);
                int* fd_libre2 = malloc(sizeof(int));
                *fd_libre2 = fd_cpu;
                list_add(listaCPUsLibres, fd_libre2);
                pthread_mutex_unlock(&mutex_listas);
                sem_post(&sem_hay_cpu_libre);
                break;
            }
            case MSG_SEG_FAULT: {
                // CP3: la CPU detectó un Segmentation Fault -> se finaliza el proceso
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                free(pid_ptr);
                if (proceso) finalizar_proceso(proceso, "SEG_FAULT");

                pthread_mutex_lock(&mutex_listas);
                int* fd_libre_sf = malloc(sizeof(int));
                *fd_libre_sf = fd_cpu;
                list_add(listaCPUsLibres, fd_libre_sf);
                pthread_mutex_unlock(&mutex_listas);
                sem_post(&sem_hay_cpu_libre);
                break;
            }
            case MSG_INTERRUPCION_ATENDIDA: {
                // CP2/CP3: la CPU atendió una interrupción. motivo: 0=fin de quantum,
                // 1=cola más prioritaria, 2=desalojo por compactación (CP3).
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                int* motivo_ptr = recibir_mensaje(fd_cpu, &size);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                int motivo = *motivo_ptr;
                free(pid_ptr);
                free(motivo_ptr);

                // en todos los casos la CPU vuelve a estar libre
                pthread_mutex_lock(&mutex_listas);
                int* fd_libre3 = malloc(sizeof(int));
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
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_CREATE", *pid_ptr);
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
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_LOCK", *pid_ptr);
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
                log_info(logger_ks, "## (%d) - Solicitó syscall: MUTEX_UNLOCK", *pid_ptr);
                Proceso* proceso = buscar_proceso_por_pid(*pid_ptr);
                mutex_unlock(nombre, proceso);
                op_code ok = MSG_OK;
                enviar_mensaje(fd_cpu, &ok, sizeof(op_code));
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
                log_info(logger_ks, "## (%d) - Solicitó syscall: SLEEP", *pid_ptr);

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
            case MSG_MEM_ALLOC: {
                // CP3: opcode + pid + id_segmento + tamanio (tres uint32_t)
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* id_ptr  = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MEM_ALLOC", *pid_ptr);

                bool ok = km_mem_alloc(*pid_ptr, *id_ptr, *tam_ptr);
                op_code r = ok ? MSG_OK : MSG_ERROR;
                enviar_mensaje(fd_cpu, &r, sizeof(op_code));
                free(pid_ptr); free(id_ptr); free(tam_ptr);
                break;
            }
            case MSG_MEM_FREE: {
                // CP3: opcode + pid + id_segmento
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* id_ptr  = recibir_mensaje(fd_cpu, &size);
                log_info(logger_ks, "## (%d) - Solicitó syscall: MEM_FREE", *pid_ptr);

                bool ok = km_mem_free(*pid_ptr, *id_ptr);
                op_code r = ok ? MSG_OK : MSG_ERROR;
                enviar_mensaje(fd_cpu, &r, sizeof(op_code));
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
                log_info(logger_ks, "## (%d) - Solicitó syscall: INIT_PROC", *pid_ptr);

                crear_proceso(path, *prio_ptr);
                op_code r = MSG_OK;
                enviar_mensaje(fd_cpu, &r, sizeof(op_code));
                free(pid_ptr); free(prio_ptr); free(path);
                break;
            }
            case MSG_STDIN: {
                // CP3: opcode + pid + dir_logica + tamanio
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* dir_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                log_info(logger_ks, "## (%d) - Solicitó syscall: STDIN", *pid_ptr);

                t_args_io_mem* a = malloc(sizeof(t_args_io_mem));
                a->fd_cpu = fd_cpu; a->pid = *pid_ptr;
                a->dir_logica = *dir_ptr; a->tamanio = *tam_ptr;
                free(pid_ptr); free(dir_ptr); free(tam_ptr);

                pthread_t t; pthread_create(&t, NULL, atender_stdin_ks, a); pthread_detach(t);
                break;
            }
            case MSG_STDOUT: {
                // CP3: opcode + pid + dir_logica + tamanio
                uint32_t* pid_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* dir_ptr = recibir_mensaje(fd_cpu, &size);
                uint32_t* tam_ptr = recibir_mensaje(fd_cpu, &size);
                log_info(logger_ks, "## (%d) - Solicitó syscall: STDOUT", *pid_ptr);

                t_args_io_mem* a = malloc(sizeof(t_args_io_mem));
                a->fd_cpu = fd_cpu; a->pid = *pid_ptr;
                a->dir_logica = *dir_ptr; a->tamanio = *tam_ptr;
                free(pid_ptr); free(dir_ptr); free(tam_ptr);

                pthread_t t; pthread_create(&t, NULL, atender_stdout_ks, a); pthread_detach(t);
                break;
            }
            default:
                log_warning(logger_ks, "Syscall desconocida: %d", *codigo);
                break;
        }
        free(codigo);
    }
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
// 4) libera la IO, pasa el proceso a READY, y recien ahi responde a la CPU
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
    // curso (SUSP_BLOCK). El helper decide el destino correcto (SUSP_READY o READY).
    finalizar_io_y_desbloquear(proceso);

    // recién ahora destrabamos a la CPU que pidió el sleep
    op_code ok = MSG_OK;
    enviar_mensaje(args->fd_cpu, &ok, sizeof(op_code));

    free(args);
    return NULL;
}

// ======================= CP3: helpers nuevos =======================

// Al terminar una IO, desbloquea el proceso contemplando que el SUSPENSION_TIMEOUT
// lo haya movido a SUSP_BLOCK mientras la IO estaba en curso.
void finalizar_io_y_desbloquear(Proceso* proceso) {
    pthread_mutex_lock(&mutex_listas);
    estado_proceso estado_actual = proceso->estado;
    pthread_mutex_unlock(&mutex_listas);

    if (estado_actual == SUSP_BLOCK) {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a SUSP. READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, SUSP_READY);
        // en SUSP_READY no se despacha: lo des-suspende el mediano plazo cuando haya memoria
    } else {
        log_info(logger_ks, "## (%d) finalizó IO y pasa a READY", proceso->id_proceso);
        actualizarEstadoProceso(proceso, READY);
        sem_post(&sem_hay_proceso_ready);
    }
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
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_DONE;
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);
    if (resp == NULL || *resp != MSG_OK)
        log_error(logger_ks, "## KM no confirmó la liberación de memoria del PID %d", pid);
    free(resp);
    pthread_mutex_unlock(&mutex_km);
}

// Finaliza un proceso: avisa a KM, lo pasa a EXIT y loguea el fin obligatorio.
void finalizar_proceso(Proceso* proceso, char* motivo) {
    km_notificar_exit(proceso->id_proceso);
    actualizarEstadoProceso(proceso, EXIT);
    log_info(logger_ks, "## (%d) finalizó su ejecución con motivo de %s", proceso->id_proceso, motivo);
}

// Pide a KM crear un segmento. KM puede intercalar un pedido de desalojo por
// compactación (MSG_SOLICITAR_DESALOJO) en el MISMO socket antes de la respuesta
// final; lo manejamos inline. Devuelve true si el segmento se creó OK.
bool km_mem_alloc(uint32_t pid, uint32_t id_segmento, uint32_t tamanio) {
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_MEM_ALLOC;
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &id_segmento, sizeof(uint32_t));
    enviar_mensaje(fd_km, &tamanio, sizeof(uint32_t));

    bool ok = false, hubo_compactacion = false;
    while (1) {
        int size;
        op_code* resp = recibir_mensaje(fd_km, &size);
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
    return ok;
}

// Pide a KM eliminar un segmento. Devuelve true si se liberó OK.
bool km_mem_free(uint32_t pid, uint32_t id_segmento) {
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_MEM_FREE;
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &id_segmento, sizeof(uint32_t));

    int size;
    op_code* resp = recibir_mensaje(fd_km, &size);
    bool ok = (resp != NULL && *resp == MSG_OK);
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    return ok;
}

// ---------------------- desalojo por compactación ----------------------

// KM pidió compactar. Desalojamos todas las CPUs que estén ejecutando (menos la
// que disparó el MEM_ALLOC, que está bloqueada en su propia syscall), esperamos
// que confirmen y le avisamos a KM que puede compactar. Se llama con mutex_km
// tomado (desde km_mem_alloc).
void manejar_solicitud_desalojo(uint32_t pid_issuer) {
    log_info(logger_ks, "## Inicio de compactación");
    en_compactacion = true; // el corto plazo deja de despachar

    pthread_mutex_lock(&mutex_listas);
    int a_esperar = 0;
    for (int i = 0; i < list_size(listaProcesosExec); i++) {
        Proceso* p = list_get(listaProcesosExec, i);
        if (p->fd_cpu < 0) continue;
        if (p->id_proceso == (int) pid_issuer) continue; // su CPU está en la syscall, no la interrumpimos
        op_code intr = MSG_INTERRUPT;
        enviar_mensaje(p->fd_cpu, &intr, sizeof(op_code));
        t_interrupcion t;
        t.pid = p->id_proceso;
        t.motivo = 2; // 2 = desalojo por compactación
        enviar_mensaje(p->fd_cpu, &t, sizeof(t_interrupcion));
        a_esperar++;
    }
    pthread_mutex_unlock(&mutex_listas);

    // esperar la confirmación de cada CPU (MSG_INTERRUPCION_ATENDIDA motivo=2)
    for (int i = 0; i < a_esperar; i++)
        sem_wait(&sem_desalojo_confirmado);

    op_code ok = MSG_DESALOJO_REALIZADO;
    enviar_mensaje(fd_km, &ok, sizeof(op_code));
    // en_compactacion se apaga en km_mem_alloc al recibir la respuesta final
}

// ---------------------- mediano plazo: des-suspensión ----------------------

// Recorre SUSP_READY por prioridad (menor número = más prioritario) y, a igual
// prioridad, por antigüedad de suspensión, moviéndolos a READY.
// NOTA(coordinación con KM): la consigna pide des-suspender sólo si hay espacio
// para recrear los segmentos SIN compactar. Eso requiere un mensaje "¿hay espacio
// para el PID X?" a KM que hoy no existe -> por ahora es best-effort (mueve todos).
void intentar_desuspender_procesos() {
    pthread_mutex_lock(&mutex_listas);
    while (!list_is_empty(listaProcesosSuspReady)) {
        int best = 0;
        for (int i = 1; i < list_size(listaProcesosSuspReady); i++) {
            Proceso* a = list_get(listaProcesosSuspReady, i);
            Proceso* b = list_get(listaProcesosSuspReady, best);
            if (a->prioridad < b->prioridad ||
               (a->prioridad == b->prioridad && a->orden_suspension < b->orden_suspension))
                best = i;
        }
        Proceso* p = list_get(listaProcesosSuspReady, best);
        pthread_mutex_unlock(&mutex_listas);

        actualizarEstadoProceso(p, READY); // hace su propio lock y saca de SUSP_READY
        sem_post(&sem_hay_proceso_ready);

        pthread_mutex_lock(&mutex_listas);
    }
    pthread_mutex_unlock(&mutex_listas);
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

    Proceso* proceso = buscar_proceso_por_pid(a->pid);
    if (proceso == NULL) {
        log_error(logger_ks, "## STDIN: proceso %d no encontrado", a->pid);
        free(a);
        return NULL;
    }

    actualizarEstadoProceso(proceso, BLOCK);

    sem_wait(&sem_hay_stdin_libre);
    t_io_ks* io = sacar_io_libre_por_tipo("STDIN");

    op_code cod = MSG_STDIN;
    enviar_mensaje(io->fd, &cod, sizeof(op_code));
    enviar_mensaje(io->fd, &a->pid, sizeof(uint32_t));
    int cant = (int) a->tamanio;
    enviar_mensaje(io->fd, &cant, sizeof(int));

    int size;
    char* texto = recibir_mensaje(io->fd, &size); // la cadena leída (a->tamanio bytes)
    liberar_io(io);

    // CP3: pedirle a KM que escriba lo leído en memoria de usuario (a->dir_logica).
    // Serializado con mutex_km porque fd_km es un único socket compartido.
    pthread_mutex_lock(&mutex_km);
    op_code cod_km = MSG_STDIN;
    enviar_mensaje(fd_km, &cod_km, sizeof(op_code));
    enviar_mensaje(fd_km, &a->pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->dir_logica, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->tamanio, sizeof(uint32_t));
    enviar_mensaje(fd_km, texto, (int) a->tamanio);
    int size_resp;
    op_code* resp = recibir_mensaje(fd_km, &size_resp);
    op_code resultado = (resp != NULL) ? *resp : MSG_ERROR;
    free(resp);
    pthread_mutex_unlock(&mutex_km);
    if (texto != NULL) free(texto);

    if (resultado == MSG_SEG_FAULT) {
        // KM detectó que la escritura se pasa del segmento -> finalizar el proceso
        finalizar_proceso(proceso, "SEG_FAULT");
    } else {
        finalizar_io_y_desbloquear(proceso);
    }

    // destrabamos a la CPU (ver nota del modelo de bloqueo: en SEG_FAULT igual
    // la liberamos para que no quede colgada; el proceso ya fue finalizado)
    op_code ok = MSG_OK;
    enviar_mensaje(a->fd_cpu, &ok, sizeof(op_code));
    free(a);
    return NULL;
}

// Corre en su propio hilo. Bloquea el proceso, (pendiente KM) lee los bytes de la
// dirección lógica y se los manda a una IO STDOUT para que los imprima.
void* atender_stdout_ks(void* arg) {
    t_args_io_mem* a = (t_args_io_mem*) arg;

    Proceso* proceso = buscar_proceso_por_pid(a->pid);
    if (proceso == NULL) {
        log_error(logger_ks, "## STDOUT: proceso %d no encontrado", a->pid);
        free(a);
        return NULL;
    }

    actualizarEstadoProceso(proceso, BLOCK);

    // CP3: pedirle a KM los a->tamanio bytes en a->dir_logica (serializado con mutex_km).
    pthread_mutex_lock(&mutex_km);
    op_code cod = MSG_STDOUT;
    enviar_mensaje(fd_km, &cod, sizeof(op_code));
    enviar_mensaje(fd_km, &a->pid, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->dir_logica, sizeof(uint32_t));
    enviar_mensaje(fd_km, &a->tamanio, sizeof(uint32_t));
    int size_resp;
    op_code* resp = recibir_mensaje(fd_km, &size_resp);
    op_code resultado = (resp != NULL) ? *resp : MSG_ERROR;
    free(resp);

    char* contenido = NULL;
    if (resultado == MSG_OK) {
        int size_datos;
        contenido = recibir_mensaje(fd_km, &size_datos); // a->tamanio bytes
    }
    pthread_mutex_unlock(&mutex_km);

    if (resultado == MSG_SEG_FAULT) {
        finalizar_proceso(proceso, "SEG_FAULT");
        op_code ok_sf = MSG_OK;
        enviar_mensaje(a->fd_cpu, &ok_sf, sizeof(op_code));
        free(a);
        return NULL;
    }

    // aseguramos terminador para que la IO STDOUT lo imprima como cadena
    char* texto = calloc(1, a->tamanio + 1);
    if (contenido != NULL) { memcpy(texto, contenido, a->tamanio); free(contenido); }

    sem_wait(&sem_hay_stdout_libre);
    t_io_ks* io = sacar_io_libre_por_tipo("STDOUT");

    op_code cod_io = MSG_STDOUT;
    enviar_mensaje(io->fd, &cod_io, sizeof(op_code));
    enviar_mensaje(io->fd, &a->pid, sizeof(uint32_t));
    enviar_mensaje(io->fd, texto, strlen(texto) + 1);
    free(texto);

    int size;
    op_code* done = recibir_mensaje(io->fd, &size); // se espera MSG_DONE
    if (done != NULL) free(done);
    liberar_io(io);

    finalizar_io_y_desbloquear(proceso);

    op_code ok = MSG_OK;
    enviar_mensaje(a->fd_cpu, &ok, sizeof(op_code));
    free(a);
    return NULL;
}
